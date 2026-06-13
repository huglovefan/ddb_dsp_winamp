#include "plugconv.hpp"

#include "fastprintf.h"
#include "plugin.hpp"
#include "../conv.h"

/* note about SFMT: at the moment, the highest supported format is
   always used, even for e.g. S16 input that could be processed as-is.
   the justification for this is that the input format also determines
   what format the plugin must return its result in. if the plugin does
   its processing in a format with more precision than S16, then the
   better output format can allow us to preserve more of that precision.
   */

/* note about sample rate conversion: right now, i'm undecided on what
   to do about it. i don't know any plugins that would care about the
   difference. */
/* the current implementation is somewhat half-unfinished, we simply
   reject converting it. in practice, this has the effect that e.g.
   buffered audio might be discarded if playback switches to a new
   sample rate. (some plugins do the same internally.) if opts.rate is
   set for a plugin, then (i think) the plugin would be skipped if
   playback changed to an unsupported rate. */
/* is this fine? there are also some alternatives:
   1. fake the sample rate. keep the original audio, simply change the
      field in AFMT. this hardly counts as converting it, but i think it
      could potentially work around some specific compatibility issues
      (think: plugin sees an unexpected rate and skips processing when
      it would actually work fine, and its processing doesn't depend on
      the sample rate anyway.) but i would really like to have an
      example of such a plugin before implementing this.
   2. actually implement sample rate conversion. this will require some
      changes to the conversion api, since proper sample rate conversion
      requires access to past samples. maybe future ones too. but again,
      where's the plugin that needs this? */

/* Get the highest integer SFMT supported by this plugin. */
static SFMT get_highest_sfmt(struct Plugin *pl)
{
	SFMT highest;

	if (pl->opts.bits.empty())
		return SFMT_S32;

	highest = SFMT_INVALID;

	for (size_t i = 0; i != pl->opts.bits.size(); i++)
	{
		SFMT as_sfmt;

		/* TODO: helper where? */
		switch (pl->opts.bits[i])
		{
		case  8: as_sfmt = SFMT_S8;  break;
		case 16: as_sfmt = SFMT_S16; break;
		case 24: as_sfmt = SFMT_S24; break;
		case 32: as_sfmt = SFMT_S32; break;
		default: __builtin_trap(); /* filtered by opt parsing */
		}

		if (highest == SFMT_INVALID
		    || sfmt_bytes(as_sfmt) > sfmt_bytes(highest))
			highest = as_sfmt;
	}

	assert(highest != SFMT_INVALID);

	return highest;
}

/*
Find a matching channel count from the list of supported values.

1. If the same value is supported, returns it as-is.
2. If a higher value is supported, returns the lowest of them.
3. If a lower value is supported, returns the highest of them.

If the list of supported values is empty, this means it supports any
value, and val is returned as-is.

Originally written by chatgpt.
*/
static unsigned int select_ch(
	unsigned int               val,
	std::vector<unsigned int> &vec)
{
	unsigned int upper;
	unsigned int lower;
	bool found_upper;
	bool found_lower;

	upper = 0;
	lower = 0;
	found_upper = false;
	found_lower = false;

	for (unsigned int x : vec)
	{
		if (x == val)
			return x;
		if (x > val)
		{
			if (!found_upper || x < upper)
			{
				upper = x;
				found_upper = true;
			}
		}
		else if (x < val)
		{
			if (!found_lower || x > lower)
			{
				lower = x;
				found_lower = true;
			}
		}
	}

	if (found_upper)
		return upper;

	if (found_lower)
		return lower;

	return val;
}

static bool can_process_rate(
	unsigned int               rate,
	std::vector<unsigned int> *rates)
{
	if (!rates->empty())
	{
		size_t i;

		for (i = 0; i != rates->size(); i++)
			if (rates->at(i) == rate)
				return true;

		return false;
	}

	return true;
}

/*
Get a common channel count for the two formats.
*/
static unsigned int get_common_ch(
	const AFMT    *a,
	const AFMT    *b,
	struct Plugin *pl)
{
	unsigned int higher;

	/* use the higher of the two as the base. */
	higher = a->ch;
	if (b->ch > higher)
		higher = b->ch;

	return select_ch(higher, pl->opts.ch);
}

/*
Get a common format for the two formats.
*/
static AFMT common_processable_format(
	const AFMT    *a,
	const AFMT    *b,
	struct Plugin *pl)
{
	AFMT res;

	/* can't make this processable. rates can't be converted. */
	if (a->rate != b->rate
	    || !can_process_rate(a->rate, &pl->opts.rate))
		return AFMT_INVALID;

	res = (AFMT){
		.sfmt = get_highest_sfmt(pl),
		.ch   = get_common_ch(a, b, pl),
		.rate = a->rate,
	};

	assert(afmt_is_valid(&res));

	return res;
}

/*
Get a suitable format for processing the buffer in.
*/
static AFMT processable_format(const AFMT *fmt, struct Plugin *pl)
{
	AFMT res;

	/* can't make this processable. */
	if (!can_process_rate(fmt->rate, &pl->opts.rate))
		return AFMT_INVALID;

	res = (AFMT){
		.sfmt = get_highest_sfmt(pl),
		.ch   = select_ch(fmt->ch, pl->opts.ch),
		.rate = fmt->rate,
	};

	assert(afmt_is_valid(&res));

	return res;
}

/*
Low-level helper.
*/
static bool convert_with_tmp(
	Buf        *buf,
	Buf        *tmp,
	AFMT       *fmt,
	const AFMT *tofmt)
{
	bool ok;

	assert(afmt_is_valid(fmt));
	assert(afmt_is_valid(tofmt));

	if (afmt_equal(fmt, tofmt))
		return true;

	ok = buf_convert_audio(
	    buf,
	    tmp,
	    fmt,
	    tofmt);

	if (!ok)
		return false;

	*fmt = *tofmt;

	return true;
}

bool can_convert_fmts(const AFMT *afmt, const AFMT *bfmt)
{
	/* rate is the only property that we don't have conversion for.
	   */
	if (afmt->rate != bfmt->rate)
		return false;

	return true;
}

/*
Convert the buffer to a suitable format for processing by the plugin.
*/
bool conv_buf_for_plugin(
	Buf           *a,
	AFMT          *afmt,
	Buf           *tmp,
	struct Plugin *pl)
{
	AFMT convfmt;
	bool ok;

	convfmt = processable_format(afmt, pl);

	/* unable to convert. */
	if (!afmt_is_valid(&convfmt))
		return false;

	ok = convert_with_tmp(
	    a,
	    tmp,
	    afmt,
	    &convfmt);
	assert(ok);
	assert(afmt_equal(afmt, &convfmt));

	return true;
}

/*
Convert the two buffers to the same format for processing by the plugin.
*/
bool conv_bufs_same_for_plugin(
	Buf           *a,
	AFMT          *afmt,
	Buf           *b,
	AFMT          *bfmt,
	Buf           *tmp,
	struct Plugin *pl)
{
	AFMT convfmt;
	bool ok;

	convfmt = common_processable_format(
	    afmt,
	    bfmt,
	    pl);

	/* unable to convert. */
	if (!afmt_is_valid(&convfmt))
		return false;

	ok = convert_with_tmp(
	    a,
	    tmp,
	    afmt,
	    &convfmt);
	assert(ok);
	assert(afmt_equal(afmt, &convfmt));

	ok = convert_with_tmp(
	    b,
	    tmp,
	    bfmt,
	    &convfmt);
	assert(ok);
	assert(afmt_equal(bfmt, &convfmt));

	return true;
}

static void conv_channels(
	Buf         *buf,
	Buf         *tmpbuf,
	AFMT        *curfmt,
	unsigned int fromch,
	unsigned int toch);

bool buf_convert_audio(
	Buf        *buf,
	Buf        *tmpbuf,
	const AFMT *from,
	const AFMT *to)
{
	AFMT curfmt;

	assert(buf->datasz());
	assert(buf != tmpbuf);
	assert(afmt_is_valid(from));
	assert(afmt_is_valid(to));
	//~ assert(!afmt_equal(from, to)); /* test and add? */
	/* i think there are calls to this where a conversion isn't
	   needed. worth cleaning those up? */

	curfmt = *from;

	if (from->rate != to->rate)
	{
		/* TODO: implement conversion */
		/* hm, i think it's not possible to do this very well
		   without access to samples around the buffer. */
		/* wonder what should be done here. internal plugin for
		   sample rate conversion with buffering? */
		/* and more importantly, what are some plugins where
		   implementing this would make a difference? without
		   one, this conversion is just a theoretical problem.
		   */
		/* hm, we only have a shared lock on the plugin list, so
		   an internal plugin for conversion can't be added
		   there. but if it was a pointer in struct Plugin, that
		   would work. */
		return false;
		/* rate is the only property that doesn't affect the
		   frame size, so i think the compatibility impact would
		   be low. */
	}

	if (from->sfmt != to->sfmt)
	{
		struct conv_request req;
		size_t resframes;
		size_t dataframes;
		AFMT idealfmt;

		resframes = afmt_buf_frames(&curfmt, buf->reservedsz());
		dataframes = afmt_buf_frames(&curfmt, buf->datasz());

		idealfmt = curfmt;
		idealfmt.sfmt = to->sfmt;

		buf_init_with_reserved_and_capacity(
		    tmpbuf,
		    afmt_frame_bytes_n(&idealfmt, resframes),
		    afmt_frame_bytes_n(&idealfmt, dataframes));

		memset(&req, 0, sizeof(req));
		req.src   = buf->datap();
		req.dst   = tmpbuf->datap();
		req.count = (dataframes * from->ch); /* TODO - helper where? */
		req.from  = curfmt.sfmt;
		req.to    = idealfmt.sfmt;

		conv_bits(req);

		curfmt = idealfmt;

		buf_register_append(
		    tmpbuf,
		    afmt_frame_bytes_n(&idealfmt, dataframes));

		buf_swap(buf, tmpbuf);
		buf_clear(tmpbuf);

	}

	if (from->ch != to->ch)
		conv_channels(buf, tmpbuf, &curfmt, from->ch, to->ch);

	assert(buf->datasz());
	assert(afmt_equal(&curfmt, to));

	return true;
}

/*
Channel conversion.

When adding channels, the new channels are silent. If converting from
mono, then the mono channel is duplicated to the second channel.

When removing channels, the extra channels are simply dropped.
*/
static void conv_channels(
	Buf         *buf,
	Buf         *tmpbuf,
	AFMT        *curfmt,
	unsigned int fromch,
	unsigned int toch)
{
	size_t resframes;
	size_t dataframes;
	size_t fs_src, fs_dst, fs_copy;
	const char *src;
	char *dst;
	size_t i;
	AFMT idealfmt;

	assert(fromch == curfmt->ch);
	assert(toch != curfmt->ch);

	idealfmt = *curfmt;
	idealfmt.ch = toch;

	resframes = afmt_buf_frames(curfmt, buf->reservedsz());
	dataframes = afmt_buf_frames(curfmt, buf->datasz());

	buf_init_with_reserved_and_capacity(
	    tmpbuf,
	    afmt_frame_bytes_n(&idealfmt, resframes),
	    afmt_frame_bytes_n(&idealfmt, dataframes));

	/* if we're adding channels, zero them. */
	if (toch > fromch)
		memset(
		    tmpbuf->datap(),
		    0,
		    afmt_frame_bytes_n(&idealfmt, dataframes));

	fs_src = afmt_frame_bytes(curfmt);
	fs_dst = afmt_frame_bytes(&idealfmt);
	fs_copy = fs_src;
	if (fs_dst < fs_copy)
		fs_copy = fs_dst;

	src = buf->datap();
	dst = tmpbuf->datap();

	/* if we're adding channels to mono audio, duplicate the mono
	   channel to the second channel. */
	/* todo: have not verified that this is where the second channel
	   is with 3+ channel audio. */
	if (fromch == 1 && toch >= 2)
	{
		size_t ss;

		ss = sfmt_bytes(curfmt->sfmt);
		for (i = 0; i != dataframes; i++)
		{
			memcpy(dst, src, fs_copy);
			memcpy(dst+ss, src, ss);
			src += fs_src;
			dst += fs_dst;
		}
	}
	else
		for (i = 0; i != dataframes; i++)
		{
			memcpy(dst, src, fs_copy);
			src += fs_src;
			dst += fs_dst;
		}

	buf_register_append(
	    tmpbuf,
	    afmt_frame_bytes_n(&idealfmt, dataframes));

	assert(src == buf->datap()+buf->datasz());
	assert(dst == tmpbuf->datap()+tmpbuf->datasz());

	buf_swap(buf, tmpbuf);
	buf_clear(tmpbuf);

	*curfmt = idealfmt;
}
