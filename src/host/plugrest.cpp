#include "plugrest.hpp"

#include "fastprintf.h"
#include "plugin.hpp"
#include "plugproc.hpp"

enum { MAX_STRETCH_FACTOR = 2 };

/*
i am a really lazy bastard.
---
can you write this function for me:

static unsigned int find_closest_match(unsigned int val, std::vector<unsigned int> &vec);

if vec has one or more numbers bigger than val, return the smallest of those bigger numbers.
if vec has one or more numbers smaller than val, return the biggest of those smaller numbers.
otherwise, return val.

that's the logic, but it should only loop through the numbers once.
---
*/
/* chatgpt */
static unsigned int closest_alternative(
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

#if defined(UNITTEST)
UNITTEST()
{
	std::vector<unsigned int> vec;
	vec = {16, 24, 32};
	assert(closest_alternative(32, vec) == 24);
	assert(closest_alternative(24, vec) == 32);
	assert(closest_alternative(16, vec) == 24);
	assert(closest_alternative(8, vec) == 16);
	vec = {16};
	assert(closest_alternative(32, vec) == 16);
	assert(closest_alternative(24, vec) == 16);
	assert(closest_alternative(16, vec) == 16);
	assert(closest_alternative(8, vec) == 16);
}
#endif

static SFMT sfmt_from_bits(unsigned int bits)
{
	switch (bits)
	{
	case  8: return SFMT_S8;
	case 16: return SFMT_S16;
	case 24: return SFMT_S24;
	case 32: return SFMT_S32;
	default: __builtin_trap();
	}
}

static int sfmt_bits(SFMT sfmt)
{
	return sfmt_bytes(sfmt)*8;
}

static AFMT perturb_fmt(const AFMT *fmt, struct Plugin *pl)
{
	AFMT rv;

	assert(afmt_is_valid(fmt));

	/* TODO: enum for the bits */

	rv.sfmt = fmt->sfmt;
	if (pl->opts.clears_internal_buffers_on_format_change & (1<<0))
	{
		if (pl->opts.bits.size())
			rv.sfmt = sfmt_from_bits(closest_alternative(
			    sfmt_bits(rv.sfmt),
			    pl->opts.bits));
		else
			/* use the next bigger one, except for s32,
			   which uses s24. */
			switch (rv.sfmt)
			{
			case SFMT_S8:  rv.sfmt = SFMT_S16; break;
			case SFMT_S16: rv.sfmt = SFMT_S24; break;
			case SFMT_S24: rv.sfmt = SFMT_S32; break;
			case SFMT_S32: rv.sfmt = SFMT_S24; break;
			default: __builtin_trap();
			}
	}

	rv.ch = fmt->ch;
	if (pl->opts.clears_internal_buffers_on_format_change & (1<<1))
	{
		if (pl->opts.ch.size())
			rv.ch = closest_alternative(rv.ch, pl->opts.ch);
		else
			/* swap 1 and 2, otherwise use the latter. */
			switch (rv.ch)
			{
			case 1:  rv.ch = 2; break;
			case 2:  rv.ch = 1; break;
			default: rv.ch = 2; break;
			}
	}

	rv.rate = fmt->rate;
	if (pl->opts.clears_internal_buffers_on_format_change & (1<<2))
	{
		if (pl->opts.rate.size())
			rv.rate = closest_alternative(rv.rate, pl->opts.rate);
		else
			/* swap 44100 and 48000, otherwise use the former. */
			switch (rv.rate)
			{
			case 44100: rv.rate = 48000; break;
			case 48000: rv.rate = 44100; break;
			default:    rv.rate = 44100; break;
			}
	}

	assert(afmt_is_valid(&rv));

	return rv;
}

static void feed_silence(
	Plugin     *pl,
	const AFMT *fmt,
	Buf        *tmp)
{
	const unsigned int stretch = (!pl->opts.nostretch)
	    ? MAX_STRETCH_FACTOR
	    : 1;
	struct {
		short *buf;
		int    nframes;
		int    bits;
		int    ch;
		int    rate;
	} args;
	fp_control fp_save;
	fp_control fp_new;
	int rv;

	size_t chunk;

	chunk = processable_size(pl, pl->silent_frames_rem);
	if (!chunk)
		chunk = pl->opts.process_min_frames;

	buf_prepare_capacity(tmp,
	    afmt_frame_bytes_n(fmt, chunk*stretch));

	memset(tmp->datap(), 0,
	    afmt_frame_bytes_n(fmt, chunk));

	args.buf     = (short *)tmp->datap();
	args.nframes = chunk;
	args.bits    = sfmt_bits(fmt->sfmt);
	args.ch      = fmt->ch;
	args.rate    = fmt->rate;

	fp_control_write(&fp_save);
	fp_control_read(pl->fp_process);

	rv = pl->module->ModifySamples(
	    pl->module,
	    args.buf,
	    args.nframes,
	    args.bits,
	    args.ch,
	    args.rate);

	fp_control_write(&fp_new);
	log_fp_change(&pl->fp_process, &fp_new, "ModifySamples", pl->opts.dllname.c_str());
	memcpy(&pl->fp_process, &fp_new, sizeof(fp_new));
	fp_control_read(fp_save);

	fastprintf("bufreset: silence in %zu/%u (%ls)\n",
	    chunk,
	    pl->opts.num_input_frames_to_clear_internal_buffers,
	    pl->opts.dllname.c_str());

	assert(rv >= 0);
	assert(rv <= chunk*stretch);

	if (chunk > pl->silent_frames_rem)
		chunk = pl->silent_frames_rem;
	pl->silent_frames_rem -= chunk;
}

/* returns true if we were able to do the thing. */
static bool bufreset_fmt_trick(
	struct Plugin *pl,
	Buf           *tmp,
	bool           use_changed_format)
{
	const unsigned int stretch = (!pl->opts.nostretch)
	    ? MAX_STRETCH_FACTOR
	    : 1;
	struct {
		short *buf;
		int    nframes;
		int    bits;
		int    ch;
		int    rate;
	} args;
	char nb[AFMT_STRBUF];
	AFMT fmt;
	fp_control fp_save;
	fp_control fp_new;
	int rv;

	/* we should've processed some sound already and set this.
	   if we didn't, there was no reason to call this function. */
	assert(afmt_is_valid(&pl->last_fmt));

	fmt = pl->last_fmt;

	if (use_changed_format)
	{
		fmt = perturb_fmt(&fmt, pl);

		if (afmt_equal(&fmt, &pl->last_fmt))
		{
			/* the call won't do anything if the format
			   isn't different. */
			fastprintf(
			    "bufreset: unable to create a different"
			    " format from [%s] (%ls)\n",
			    afmt_tostring(nb, sizeof(nb), &fmt),
			    pl->opts.dllname.c_str());
			return false;
		}
	}

	buf_prepare_capacity(tmp,
	    afmt_frame_bytes_n(&fmt, pl->opts.process_min_frames*stretch));

	memset(tmp->datap(), 0,
	    afmt_frame_bytes_n(&fmt, pl->opts.process_min_frames));

	args.buf     = (short *)tmp->datap();
	args.nframes = pl->opts.process_min_frames;
	args.bits    = sfmt_bits(fmt.sfmt);
	args.ch      = fmt.ch;
	args.rate    = fmt.rate;

	fp_control_write(&fp_save);
	fp_control_read(pl->fp_process);

	rv = pl->module->ModifySamples(
	    pl->module,
	    args.buf,
	    args.nframes,
	    args.bits,
	    args.ch,
	    args.rate);

	fp_control_write(&fp_new);
	log_fp_change(&pl->fp_process, &fp_new, "ModifySamples", pl->opts.dllname.c_str());
	memcpy(&pl->fp_process, &fp_new, sizeof(fp_new));
	fp_control_read(fp_save);

	/* something i noticed: the return value can provide a hint as
	   to whether this is working. */
	/* if the plugin buffers a certain amount of audio before it
	   returns anything, the return value here might be 0, because
	   it has to start from scratch. */
	/* pacemaker seems to work like this. */
	fastprintf(
	    "bufreset: fmt=[%s] %d->%d (%ls)\n",
	    afmt_tostring(nb, sizeof(nb), &fmt),
	    args.nframes,
	    rv,
	    pl->opts.dllname.c_str());

	assert(rv >= 0);
	assert(rv <= pl->opts.process_min_frames*stretch);

	return true;
}

void bufreset_single_step(
	struct Plugin *pl,
	const AFMT    *fmt,
	Buf           *tmp)
{
	if (pl->internal_buffer_state == IB_CLEAN)
		return;

	if (pl->opts.clears_internal_buffers_on_format_change)
	{
		/* do this twice, second time with the original format.
		   this might avoid a delay when playback resumes with
		   this format again. */

		if (bufreset_fmt_trick(pl, tmp, true))
			bufreset_fmt_trick(pl, tmp, false);

		pl->internal_buffer_state = IB_CLEAN;
	}
	else if (pl->opts.num_input_frames_to_clear_internal_buffers)
	{
		const size_t silent_cnt =
		    pl->opts.num_input_frames_to_clear_internal_buffers;

		if (!pl->silent_frames_rem)
			pl->silent_frames_rem = silent_cnt;

		/* mild todo: this can actually process fewer samples
		   than we normally would in one round. remember that
		   the actual processing path splits the input buffer
		   into processable chunks. here, we process just one
		   such chunk. */

		feed_silence(pl, fmt, tmp);

		if (pl->silent_frames_rem)
			/* meta: not entirely satisfied with the enum
			   name. i'm not sure it makes sense to assume
			   we're skipped here. */
			pl->internal_buffer_state = IB_DIRTY_SKIPPED;
		else
			pl->internal_buffer_state = IB_CLEAN;
	}
	else
	{
		pl->internal_buffer_state = IB_CLEAN;
	}
}

void bufreset_full(
	struct Plugin *pl,
	const AFMT    *fmt,
	Buf           *tmp)
{
	while (pl->internal_buffer_state != IB_CLEAN)
		bufreset_single_step(pl, fmt, tmp);
}

/*
Check if we're able to reset the internal buffers of this plugin.
*/
static bool bufreset_enabled(struct Plugin *pl)
{
	if (pl->opts.clears_internal_buffers_on_format_change)
		return true;

	if (pl->opts.num_input_frames_to_clear_internal_buffers)
		return true;

	return false;
}

void plugin_process_reset(struct Plugin *pl, Buf *tmp)
{
	if (pl->internal_buffer_state == IB_CLEAN)
		return;

	if (!bufreset_enabled(pl))
		return;

	/* see comment in plugin_process_all. */
	if (!pl->dll)
		return;

	AcquireSRWLockShared(&pl->dll_lock);

	if (pl->dll)
		bufreset_full(pl, &pl->last_fmt, tmp);

	ReleaseSRWLockShared(&pl->dll_lock);
}
