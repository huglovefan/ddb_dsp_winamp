#include "plugin.h"

#include <stdio.h>
#include <stdlib.h>

#include "main.h"
#include "macros.h"
#include "misc.h"

#define MAX_STRETCH_FACTOR 2

static int
edible_size(struct plugin *pl, int frames_avail)
{
	int frames_avail_orig = frames_avail;

	int pMf = pl->opts.process_max_frames;
	int pfm = pl->opts.process_frames_mult;
	int pmf = pl->opts.process_min_frames;

	if (pMf != 0 && frames_avail > pMf)
		frames_avail = pMf;

	if (pfm != 1)
		frames_avail -= frames_avail % pfm;

	if (frames_avail < pmf)
		frames_avail = 0;

D	assert(frames_avail >= 0);
D	assert(frames_avail <= frames_avail_orig);

	return frames_avail;
}

static void
plugin_check_tmpbuf(struct plugin *pl, struct fmt *fmt);

static void
plugin_process_twobuf(struct plugin *pl, struct fmt *fmt, struct buf *data, struct buf *tmp);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"

void
plugin_process(struct plugin *pl,
               struct fmt *fmt,
               struct buf *data,
               struct buf *tmp)
{
	if U (pl->opts.randomize) plugin_randomize_opts(pl);

D	buf_shrink_cap(data, data->sz);
D	buf_free(tmp);

	const int avail = fmt_bytes2frames(fmt, pl->buf.sz+data->sz);
	int edible = edible_size(pl, avail);

	//
	// if there's not enough data to process it, save it to this plugin's
	//  temp buffer and get out. the input buffer is emptied so that next
	//  plugins in the chain don't get called
	// on the next call, if there is enough data, the saved data will be
	//  added to the beginning of the input buffer and processed together
	//  with the new data
	//
	if U (edible == 0) {
		if U (pl->opts.trace)
			fprintf(stderr, "[%s] incoming %d frames not yet edible, adding to buffer\n",
			    superbasename(pl->opts.path),
			    fmt_bytes2frames(fmt, data->sz));

		if (pl->buf.sz == 0) {
			buf_swap(&pl->buf, data);
		} else {
			buf_append_buf(&pl->buf, data);
			buf_clear(data);
		}
		plugin_check_tmpbuf(pl, fmt);
D		buf_free(data);
		return;
	}

	// add old saved data to the input buffer
	if (pl->buf.sz != 0) {
		if U (pl->opts.trace)
			fprintf(stderr, "[%s] took %d frames from temp. buffer, data is now %d frames\n",
			    superbasename(pl->opts.path),
			    fmt_bytes2frames(fmt, pl->buf.sz),
			    fmt_bytes2frames(fmt, pl->buf.sz+data->sz));

		buf_prepend_buf(data, &pl->buf);
		buf_clear(&pl->buf);
	}

	/// warning: obsolete historical comment
	//
	// there's two versions of the next part:
	// - one processes the input data in-place in the same buffer. this may
	//    be faster due to less copying
	// - one copies the input piece-by-piece to a second buffer and
	//    processes it there. this is safe against accidentally overwriting
	//    parts of the input data if the plugin might stretch the sound
	//    (writing samples past the end of the buffer given as input)
	//
	// the first version is used if the plugin is known not to stretch the
	//  sound, or if the input is small enough that it can be processed in
	//  one call (no risk of overwriting any following data)
	//
	/// edit: the two functions have been combined. the two-buffer one has
	///  been modified to deal with the two buffers being the same
	/// it still has the advantage of reduced copying when it's not necessary

	bool use_onebuf = false;
	if (edible == avail) // can process all of it in one call
		use_onebuf = true;
	else if (!pl->opts.may_stretch) // plugin known not to stretch sound
		use_onebuf = true;

	// (randomize) if either version would work, then pick one at random
	if U (use_onebuf && pl->opts.randomize) {
		if (rand() % 100 >= 60)
			use_onebuf = false;
	}

	if (use_onebuf)
		tmp = data;

	plugin_process_twobuf(pl, fmt, data, tmp);
}

#pragma GCC diagnostic pop

static void
ModifySamples_s(struct plugin *pl,
                struct fmt *fmt,
                const char *inbuf,
                int *inbuf_frames,
                char *outbuf,
                int *outbuf_frames);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"

static void
plugin_process_twobuf(struct plugin *pl,
                      struct fmt *fmt,
                      struct buf *data,
                      struct buf *tmp)
{
	const size_t fs = fmt_frame_size(fmt);
	int pl_stretch_factor = (pl->opts.may_stretch) ? MAX_STRETCH_FACTOR : 1;

D	buf_shrink_cap(data, data->sz);
D	buf_shrink_cap(tmp, tmp->sz);

	if (tmp == data) {
		buf_prepare_capacity(tmp, data->sz*pl_stretch_factor);
	} else {
		buf_clear(tmp);
		buf_prepare_append(tmp, data->res + data->sz*pl_stretch_factor);
		buf_set_reserved(tmp, data->res);
	}

	const char *const readstart = data->p;
	const char       *readp     = data->p;
	const char *const readend   = data->p + data->sz;

	const char *const writestart = tmp->p;
	char             *writep     = tmp->p;
	const char *const writeend   = tmp->p + tmp->cap;

	// xxx: how to check if samples are overwritten by a stretch?

	while (readp < readend && writep < writeend) {
		int readable = (readend-readp)/fs;
		int writable = (writeend-writep)/fs;

		// sanity
D		assert(buf_boundscheck_read(data, readp, fs*readable));
D		assert(buf_boundscheck_write(tmp, writep, fs*writable));

		ModifySamples_s(pl, fmt,
		    readp, &readable,
		    writep, &writable);

		readp += fs*readable;
		writep += fs*writable;

		// if they're the same buffer and the sound was stretched, skip
		//  over the stretch tail in readp to avoid processing it twice
		if (writestart == readstart && writep > readp)
			readp = writep;

		// pointers still valid
		// note: the check of readp is with _write because a stretch
		//  might've advanced it past the original readable size
D		assert(buf_boundscheck_write(data, readp, 0));
D		assert(buf_boundscheck_write(tmp, writep, 0));

		// nothing was read
		if (readable == 0)
			break;
	}

	//
	// if we processed part of the input data but there's still some left in
	//  the input buffer, save the unprocessed part to this plugin's temp.
	//  buffer
	// when this plugin is called next time, it'll be added to the beginning
	//  of the new input data and processed together with it
	//
	// (i think this makes sense? it has to be in this plugin's buffer
	//  specifically because it might've been touched by earlier plugins in
	//  the chain)
	//
	if (readp < readend) {
		const char *rest = readp;
		size_t rest_sz = readend-rest;

		if U (pl->opts.trace)
			fprintf(stderr, "[%s] leftover frames after processing: %d\n",
			    superbasename(pl->opts.path),
			    fmt_bytes2frames(fmt, rest_sz));

D		assert(buf_boundscheck_read(data, rest, rest_sz)&BUF_RIGHTEDGE);

		buf_append(&pl->buf, rest, rest_sz);

		plugin_check_tmpbuf(pl, fmt);
	}

	buf_set_size(tmp, writep-writestart);
	buf_swap(data, tmp);
}

#pragma GCC diagnostic pop

// the s stands for silly
static void
ModifySamples_s(struct plugin *pl,
                struct fmt *fmt,
                const char *inbuf,
                int *inbuf_frames,
                char *outbuf,
                int *outbuf_frames)
{
	const size_t fs = fmt_frame_size(fmt);
	int pl_stretch_factor = (pl->opts.may_stretch) ? MAX_STRETCH_FACTOR : 1;
	int plug_rv;

	*inbuf_frames = edible_size(pl, *inbuf_frames);

	// can't bite off a satisfactory chunk
	if (*inbuf_frames == 0) {
		*outbuf_frames = 0;
		return;
	}

	// not enough space to process it
	// (shouldn't normally happen since the buffer is resized in advance,
	//  but maybe the plugin has stretched more than it was supposed to)
	if U (*inbuf_frames*pl_stretch_factor > *outbuf_frames) {
		*inbuf_frames = 0;
		*outbuf_frames = 0;
		return;
	}

	if (outbuf != inbuf)
		memmove(outbuf, inbuf, fs**inbuf_frames);

	if U (pl->opts.trace)
		fprintf(stderr, "[%s] ModifySamples %d",
		    superbasename(pl->opts.path),
		    *inbuf_frames);

	plug_rv = pl->module->ModifySamples(pl->module,
	    (short int *)outbuf,
	    *inbuf_frames,
	    fmt->bps,
	    fmt->ch,
	    fmt->rate);

	if U (pl->opts.trace)
		fprintf(stderr, " -> %d\n",
		    plug_rv);

	if U (plug_rv < 0) {
		fprintf(stderr, "warning: ModifySamples() for plugin %s returned %d\n",
		    superbasename(pl->opts.path),
		    plug_rv);
		plug_rv = 0;
	}

	if U (plug_rv > *inbuf_frames*pl_stretch_factor) {
		fprintf(stderr, "warning: %.1fx stretch (%d -> %d) by plugin %s is above %s %d\n",
		    (float)plug_rv / (float)*inbuf_frames,
		    *inbuf_frames, plug_rv,
		    superbasename(pl->opts.path),
		    (pl->opts.may_stretch) ?
		        "MAX_STRETCH_FACTOR" :
		        "its allowed stretch factor",
		    pl_stretch_factor);
	}

	assert(plug_rv <= *outbuf_frames);

	*outbuf_frames = plug_rv;

	return;
}

//
// memory leak avoidance
//
static void
plugin_check_tmpbuf(struct plugin *pl,
                    struct fmt *fmt)
{
	size_t fs = fmt_frame_size(fmt);
	size_t onesec = fs*fmt->rate;

	if (pl->buf.sz > pl->lastbufsz) {
		if U (pl->lastbufsz <= onesec && pl->buf.sz > onesec) {
			fprintf(stderr, "warning: temp. buffer of plugin %s exceeds one second. is it working properly?\n",
			    superbasename(pl->opts.path));
		}

		if U (pl->lastbufsz <= 5*onesec && pl->buf.sz > 5*onesec) {
			fprintf(stderr, "error: temp. buffer of plugin %s exceeds five seconds. aborting now to stop the memory leak\n",
			    superbasename(pl->opts.path));
			abort();
		}
	}

	pl->lastbufsz = pl->buf.sz;
}
