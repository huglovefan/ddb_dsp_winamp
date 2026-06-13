#include "plugproc.hpp"

#include <stdint.h>
#include <math.h>
#include <atomic>
#include "fastprintf.h"
#include "procmain.hpp"
#include "plugconv.hpp"
#include "../monotime.h"
#include "plugrest.hpp"

enum { MAX_STRETCH_FACTOR = 2 };

#define TEST_DO_HEAP_VALIDATE 1
#if TEST_DO_HEAP_VALIDATE
#include <heapapi.h>
#endif

/**
 * with `frames_avail` frames available to process, get the number that
 * this plugin could process right now in one call to ModifySamples().
 * 
 * returns a nonzero number on success, 0 if nothing can be processed
 * yet (probably need more data.)
 */
unsigned int processable_size(
	const Plugin *const pl,
	unsigned int  frames_avail)
{
	unsigned int min = pl->opts.process_min_frames;
	unsigned int max = pl->opts.process_max_frames;
	unsigned int mult = pl->opts.process_frames_mult;

	/* these cases are checked during option parsing. */
	assert(min % mult == 0);
	assert(max % mult == 0);
	assert(!max || min <= max);

	if (frames_avail < min)
		return 0;

	unsigned int blocks_avail = (frames_avail - min) / (mult);
	unsigned int blocks_max = (max - min) / (mult);

	if (blocks_avail > blocks_max)
		blocks_avail = blocks_max;

	return min + blocks_avail*mult;
}

struct processable_sizes
{
	unsigned int min;
	unsigned int max;
	unsigned int mult;
};

static unsigned int get_random_processable_size(
	processable_sizes sizes,
	int             (*rand)() = rand)
{
	unsigned int range;
	unsigned int chosen;
	unsigned int rv;

	range = (sizes.max-sizes.min)/sizes.mult;
	if (!range)
		chosen = 0;
	else
		chosen = lround((rand() / (double)RAND_MAX) * range);

	rv = sizes.min + chosen*sizes.mult;

	assert(rv >= sizes.min);
	assert(rv <= sizes.max);
	assert(rv % sizes.mult == 0);

	return rv;
}

static processable_sizes processable_size2(
	const Plugin *const pl,
	unsigned int  frames_avail)
{
	unsigned int min = pl->opts.process_min_frames;
	unsigned int max = pl->opts.process_max_frames;
	unsigned int mult = pl->opts.process_frames_mult;

	/* these cases are checked during option parsing. */
	assert(min % mult == 0);
	assert(max % mult == 0);
	assert(!max || min <= max);

	unsigned int blocks_avail = (frames_avail) / (mult);
	unsigned int blocks_min = (min) / (mult);
	unsigned int blocks_max = (max) / (mult);

	if (blocks_avail < blocks_min)
		blocks_min = blocks_avail;

	if (blocks_avail < blocks_max)
		blocks_max = blocks_avail;

	return processable_sizes{
		min: blocks_min*mult,
		max: blocks_max*mult,
		mult: mult,
	};
}

struct plugin_process_stats
{
	size_t modsamp_frames_in;
};

/**
 * modify some samples. this is an "s" wrapper around ModifySamples.
 * 
 * pl: the plugin to use
 * fmt: the current sound format
 * inbuf: pointer to input data to process
 * inbuf_frames_out:
 * - READ: number of frames at inbuf
 * - WRITTEN: number of frames consumed
 * outbuf: pointer where output should be placed
 * outbuf_frames_out:
 * - READ: max. number of frames that fit in outbuf
 * - WRITTEN: number of frames written
 * 
 * tips:
 * - if there's too little input data to process, then *_frames_out will
 *   both be set to zero.
 * - inbuf and outbuf are allowed to be overlapping.
 */
static void ModifySamples_s(
	Plugin                      *const pl,
	const AFMT                  *const fmt,
	const void                  *const inbuf,
	unsigned int                *const inbuf_frames_out,
	void                        *const outbuf,
	unsigned int                *const outbuf_frames_out,
	struct plugin_process_stats *stat)
{
	const unsigned int pl_stretch_factor = (!pl->opts.nostretch)
	    ? MAX_STRETCH_FACTOR
	    : 1;

	const unsigned int inbuf_frames = (pl->opts.randbuf)
	    ? get_random_processable_size(processable_size2(pl, *inbuf_frames_out))
	    : processable_size(pl, *inbuf_frames_out);

	const unsigned int outbuf_frames = *outbuf_frames_out;

	fp_control fp_save;
	fp_control fp_new;
	int plug_rv;

	/* nothing to process? */
	if (!inbuf_frames)
	{
skip:
		*inbuf_frames_out = 0;
		*outbuf_frames_out = 0;
		return;
	}

	/* stretched output may not fit? */
	/* [unimplemented: consider this limit in processable_size().
	   we could potentially process a smaller size.] */
	if (!(inbuf_frames*pl_stretch_factor <= outbuf_frames))
	{
		/* TODO(2026): i can't figure out when this would
		   actually happen. would like to address the
		   unimplemented comment above. or is the usual
		   processing path just not affected? */
		if (1)
		{
			fastprintf(
			    "%s(%d): stretched output may not fit"
			    " (%ls)\n",
			    __FILE__,
			    __LINE__,
			    pl->opts.dllname.c_str());
			fastprintf(" -> *inbuf_frames_out=%u", *inbuf_frames_out);
			fastprintf(" -> *outbuf_frames_out=%u", *outbuf_frames_out);
			fastprintf(" -> pl_stretch_factor=%u", pl_stretch_factor);
			fastprintf(" -> inbuf_frames=%u", inbuf_frames);
			fastprintf(" -> outbuf_frames=%u", outbuf_frames);
		}
		goto skip;
	}

	/* move the input to where we want the output to be. it'll be
	   processed in place and left there. use memmove since these
	   are allowed to be overlapping. */
	if (outbuf != inbuf)
		memmove(
		    outbuf,
		    inbuf,
		    afmt_frame_bytes_n(fmt, inbuf_frames));

	/* first, gently massage the input samples if this flag is set.
	   */
	if (fmt->sfmt == SFMT_S32
	    && pl->opts.workaround_input_int32_overflow)
	{
		int32_t *buf;
		size_t nsamples;
		size_t i;
		int32_t max_val;

		max_val = 0x7fff'ffbf;
		buf = static_cast<int32_t *>(outbuf);
		nsamples = inbuf_frames*fmt->ch;

		for (i = 0; i != nsamples; i++)
			if (buf[i] > max_val)
				buf[i] = max_val;
	}

	if (TEST_DO_HEAP_VALIDATE)
	{
		/* this shouldn't fail but is included for completeness.
		   if it fails, the heap was likely corrupted by someone
		   other than us. */
		if (!HeapValidate(GetProcessHeap(), 0, nullptr))
		{
			fastprintf("HeapValidate pre failed\n");
			__builtin_trap();
		}
	}

	fp_control_write(&fp_save);
	fp_control_read(pl->fp_process);

	plug_rv = pl->module->ModifySamples(pl->module,
	    static_cast<short *>(outbuf),
	    static_cast<int>(inbuf_frames),
	    static_cast<int>(sfmt_bytes(fmt->sfmt)*8),
	    static_cast<int>(fmt->ch),
	    static_cast<int>(fmt->rate));

	fp_control_write(&fp_new);
	log_fp_change(&pl->fp_process, &fp_new, "ModifySamples", pl->opts.dllname.c_str());
	memcpy(&pl->fp_process, &fp_new, sizeof(fp_new));
	fp_control_read(fp_save);

	if (TEST_DO_HEAP_VALIDATE)
	{
		if (!HeapValidate(GetProcessHeap(), 0, nullptr))
		{
			fastprintf("HeapValidate post failed\n");
			__builtin_trap();
		}
	}

	/* negative return value? shouldn't happen. */
	if (plug_rv < 0)
	{
		fastprintf(
		    "plugin %ls returned negative value: %d\n",
		    pl->opts.dllname.c_str(),
		    plug_rv);
		__builtin_trap();
	}

	/* stretched more than allowed by options. */
	if (plug_rv > inbuf_frames*pl_stretch_factor)
	{
		fastprintf(
		    "plugin %ls stretched more than allowed by"
		    " options\n",
		    pl->opts.dllname.c_str());
		__builtin_trap();
	}

	/* stretched more than fits in the buffer. */
	if (plug_rv > outbuf_frames)
	{
		fastprintf(
		    "plugin %ls overflowed output buffer!\n",
		    pl->opts.dllname.c_str());
		__builtin_trap();
	}

	*inbuf_frames_out = inbuf_frames;
	*outbuf_frames_out = plug_rv;

	stat->modsamp_frames_in += inbuf_frames;
}

static void plugin_process_inner(
	Plugin                      *const pl,
	const AFMT                  *const fmt,
	Buf                         *const data,
	Buf                         *const tmp,
	struct plugin_process_stats *stat)
{
	const unsigned int pl_stretch_factor = (!pl->opts.nostretch)
	    ? MAX_STRETCH_FACTOR
	    : 1;

	assert(data->datasz()); /* debug */

	if (tmp == data)
	/* using one buffer -> prepare it for stretching the data it
	   contains. */
	{
		buf_prepare_capacity(
		    data,
		    data->datasz()*pl_stretch_factor);
		assert(data->datap() != nullptr); /* debug */
		assert(data->datap() == tmp->datap()); /* debug */
	}
	else
	/* using two buffers -> prepare `tmp` to become a copy of `data`
	   with the processed result. */
	{
		/* same reserved space, size for maximum stretch. */
		buf_init_with_reserved_and_capacity(
		    tmp,
		    data->reservedsz(),
		    data->datasz()*pl_stretch_factor);
		assert(tmp->datap() != nullptr); /* debug */
		assert(tmp->datap() != data->datap()); /* debug */
	}

	const char *const readstart = &*data->a_read().begin();
	const char       *readp     = &*data->a_read().begin();
	const char *const readend   = &*data->a_read().end();

	const char *const writestart = &*tmp->a_overwrite().begin();
	char             *writep     = &*tmp->a_overwrite().begin();
	const char *const writeend   = &*tmp->a_overwrite().end();

	/* debug: these should mean the same thing. */
	assert((tmp == data) == (writestart == readstart));

	/* debug: the loop will do at least one iteration. this function
	   shouldn't be called if there's nothing for it to do. */
	assert(readp < readend);
	assert(writep < writeend);

	/* loop while there's data to read && space in the output
	   buffer. */
	while (readp < readend && writep < writeend)
	{
		unsigned int readable;
		unsigned int writable;

		readable = afmt_buf_frames(fmt, readend-readp);
		writable = afmt_buf_frames(fmt, writeend-writep);

		ModifySamples_s(pl, fmt,
		    readp, &readable,
		    writep, &writable,
		    stat);

		/* readable/writable are now what was actually
		   read/written. */
		readp += afmt_frame_bytes_n(fmt, readable);
		writep += afmt_frame_bytes_n(fmt, writable);

		/* when using one buffer: if the write pointer goes past
		   the read pointer, it means the plugin stretched
		   sound. */
		if (tmp == data && writep > readp)
		{
			/* if we just processed the end of the buffer,
			   then the stretch was harmless - past the data
			   was only free space. */
			/* otherwise, the stretch must've overwritten
			   some of the samples that we were going to
			   process next. */
			bool at_end = (readp == readend);
			if (at_end)
			{
				/* advance the read pointer past the
				   stretch tail so we don't use it as
				   input again. */
				readp = writep;
			}
			else
			{
				/* this will cause a glitch in playback,
				   so disallow it. */
				/* this shouldn't happen unless you set
				   nostretch=1 on a plugin that does
				   stretch sound. */
				fastprintf(
				    "plugin %ls stretched sound but"
				    " wasn't supposed to (onebuf=%d"
				    " nostretch=%d)\n",
				    pl->opts.dllname.c_str(),
				    (tmp == data),
				    pl->opts.nostretch);
				__builtin_trap();
			}
		}

		/* nothing was read? */
		if (!readable)
		{
			/* at least one iteration read something. */
			assert(readstart < readp);
			break;
		}
	}

	/* didn't read all of the input data? save it to this plugin's
	   temporary buffer to be processed next time. */
	if (readp < readend)
	{
		/* the temp buffer was cleared in the other function. */
		assert(!pl->buf.datasz());

		if (data != tmp)
		/* used two buffers -> swap the old input buffer to
		   pl.buf. */
		{
			/* "skip" the part we did read by marking it as
			   reserved. */
			buf_shift_data_into_reserved(
			    data,
			    readp-readstart);
			/* swap it into place. */
			buf_swap(&pl->buf, data);
		}
		else
		/* used one buffer -> copy it normally. (can't swap it
		   because this buffer needs to be returned as the
		   output buffer.) */
		{
			buf_append(&pl->buf, readp, readend-readp);
		}

		pl->buf_fmt = *fmt;
	}

	/* update the output buffer with its final length. */
	buf_set_size(tmp, writep-writestart);
	/* swap the output to be the new input. (this is a no-op if the
	   buffers are the same.) */
	buf_swap(data, tmp);
}

static void plugin_process(
	Plugin                      *const pl,
	AFMT                        *fmt,
	Buf                         *data,
	Buf                         *const tmp,
	struct plugin_process_stats *stat)
{
	assert(afmt_is_valid(fmt));

	const unsigned int avail =
	    (pl->buf.datasz() ? afmt_buf_frames(&pl->buf_fmt, pl->buf.datasz()) : 0)
	    + afmt_buf_frames(fmt, data->datasz());

	unsigned int processable = processable_size(pl, avail);

	/* if using randbuf with a maximum size set, wait until we have
	   at least that amount. this way the full range gets tested. */
	/* (the issue remains with max=0.) */
	if (pl->opts.randbuf && pl->opts.process_max_frames)
		if (processable < pl->opts.process_max_frames)
			processable = 0;

	/* can process something? */
	if (processable)
	{
		bool ok;

		pl->last_oldfmt = *fmt;

		/* if there's stored data from a previous call, prepend
		   it to the input buffer. */
		if (pl->buf.datasz())
		{
			ok = conv_bufs_same_for_plugin(
			    &pl->buf, &pl->buf_fmt,
			    data, fmt,
			    tmp,
			    pl);
			assert(ok);

			buf_prepend_buf(data, &pl->buf);

			buf_clear(&pl->buf);
			pl->buf_fmt = AFMT_INVALID;
		}
		else if (!pl->opts.ignore_output)
		/* plugin has no stored data. just convert the input. */
		/* skipped if ignore_output is set. we don't want to
		   cause a conversion that affects other plugins. */
		{
			ok = conv_buf_for_plugin(data, fmt, tmp, pl);
			assert(ok);
		}

		pl->last_fmt = *fmt;

		if (pl->opts.ignore_output)
		{
			/* this is hacky. but you're not going to hear
			   it so who cares. */

			/* there's a minor, missed optimization
			   opportunity here. if the next plugin would
			   use the same format that we're converting to,
			   it would be nice to detect that and avoid
			   doing the same conversion twice. */

			/* make pl.buf a copy of data. */
			buf_prepare_append(&pl->buf, data->datasz());
			memcpy(pl->buf.datap(), data->datap(), data->datasz());
			buf_register_append(&pl->buf, data->datasz());
			pl->buf_fmt = *fmt;

			/* convert pl.buf to suitable format. */
			ok = conv_buf_for_plugin(&pl->buf, &pl->buf_fmt, tmp, pl);
			assert(ok);

			/* update this since it was converted. */
			pl->last_fmt = pl->buf_fmt;

			/* process! */
			plugin_process_inner(pl, &pl->buf_fmt, &pl->buf, &pl->buf, stat);

			/* clear this since we're done with it. */
			buf_clear(&pl->buf);
			pl->buf_fmt = AFMT_INVALID;
		}
		else
		{
			/* for this next part, we can use either one or
			   two buffers. */

			/* if the plugin can read the entire input in
			   one call, OR the plugin is known not to
			   stretch sound, it's safe to do the operation
			   with one buffer. */

			Buf *use_tmp;

			use_tmp = tmp;

			if (processable == avail || pl->opts.nostretch)
				use_tmp = data;

			plugin_process_inner(pl, fmt, data, use_tmp, stat);
		}
	}
	else
	/* not going to process, move the data to this plugin's
	   temporary buffer. */
	{
		if (!pl->buf.datasz())
		/* if there's no older stored data, we can do a swap. */
		{
			buf_swap(&pl->buf, data);
			pl->buf_fmt = *fmt;
			*fmt = AFMT_INVALID;
		}
		else
		/* there is pre-existing data, append normally. */
		{
			bool ok;

			ok = conv_bufs_same_for_plugin(
			    &pl->buf, &pl->buf_fmt,
			    data, fmt,
			    tmp,
			    pl);
			assert(ok);

			buf_append_buf(&pl->buf, data);

			buf_clear(data);
			*fmt = AFMT_INVALID;
		}

		/* just checking: the output is empty, the next plugin
		   won't be called. */
		assert(!data->datasz());
	}
}

/**
 * process the data with all the plugins.
 * 
 * `data` contains the input data, and it should have enough reserved
 * space to fit all active plugins' temporary buffers.
 * 
 * tmp is a scratch buffer that this function can use without having to
 * allocate one itself.
 * 
 * the processed output will be in the buffer pointed to by `data`. the
 * function may swap the innards of `data` and `tmp` to achieve this
 * goal.
 */
void plugin_process_all(
	struct plugin_list *plugins,
	AFMT               &fmt,
	Buf                &data,
	Buf                &tmp)
{
	struct Plugin *pl;

	assert(&data != &tmp); /* debug: assumed to be different */
	assert(afmt_is_valid(&fmt));

	if (!data.datasz()) /* assumed non-empty downstream */
		return;

	PLUGIN_LIST_FOREACH(pl, plugins)
	{
		struct plugin_process_stats stat;

		/* check twice to potentially avoid an obscure hang. */
		/* this was for dlls that take a long time to unload for
		   whatever reason (ex: MessageBox from DllMain.) */
		/* [todo: check if this could be made race-free using
		    atomics somehow.] */
		if (!pl->dll)
			continue;
		AcquireSRWLockShared(&pl->dll_lock);
		if (!pl->dll)
			goto next;

		if (pl->skip_fmt || pl->skip_user)
		/* plugin is skipped this round. */
		{
			if (pl->skip_fmt)
				assert(!pl->opts.required);

			buf_clear(&pl->buf);
			pl->buf_fmt = AFMT_INVALID;

			/* don't need to do anything to handle this,
			   we're clearing the buffer already. */
			pl->did_move = false;

			bufreset_single_step(pl, &fmt, &tmp);

			goto next;
		}
		else if (pl->internal_buffer_state == IB_DIRTY_SKIPPED)
		/* plugin was skipped, and we're in the middle of
		   clearing its internal buffer. */
		{
			/* don't need to do anything to handle this,
			   we're clearing the buffer already. */
			pl->did_move = false;

			bufreset_full(pl, &fmt, &tmp);
		}
		else if (pl->did_move)
		{
			if (pl->buf.datasz())
			{
				size_t nframes;

				nframes = afmt_buf_frames(
				    &pl->buf_fmt, pl->buf.datasz());
				fastprintf(
				    "discarding %zu frames due to move"
				    " (%ls)\n",
				    nframes,
				    pl->opts.dllname.c_str());

				buf_clear(&pl->buf);
				pl->buf_fmt = AFMT_INVALID;
			}

			pl->did_move = false;
			bufreset_full(pl, &fmt, &tmp);
		}

		memset(&stat, 0, sizeof(stat));
		plugin_process(pl, &fmt, &data, &tmp, &stat);

		if (stat.modsamp_frames_in)
			pl->internal_buffer_state = IB_DIRTY_PLAYING;

next:
		ReleaseSRWLockShared(&pl->dll_lock);
		if (!data.datasz())
			break;
	}
}

#if defined(UNITTEST)
UNITTEST()
{
	struct Plugin pl{};
	processable_sizes sz;
	unsigned int frames;

	frames = processable_size(&pl, 0);
	assert(frames == 0);
	/* i would like to see the buffer that can hold this much */
	frames = processable_size(&pl, INT_MAX);
	assert(frames == 576);

	frames = processable_size(&pl, 576);
	assert(frames == 576);
	frames = processable_size(&pl, 576-1);
	assert(frames == 0);
	frames = processable_size(&pl, 576+1);
	assert(frames == 576);

	/* default is 576-only */
	frames = processable_size(&pl, 576*2);
	assert(frames == 576);
	frames = processable_size(&pl, 576*2-1);
	assert(frames == 576);
	frames = processable_size(&pl, 576*2+1);
	assert(frames == 576);

	/* 1 or 2 blocks of 576 */
	pl.opts.process_max_frames = 576*2;
	frames = processable_size(&pl, 576*2);
	assert(frames == 576*2);
	frames = processable_size(&pl, 576*2-1);
	assert(frames == 576*1);
	frames = processable_size(&pl, 576*2+1);
	assert(frames == 576*2);

	pl.opts.process_max_frames = 576;
	sz = processable_size2(&pl, 0);
	assert(sz.min == 0);
	assert(sz.max == 0);
	sz = processable_size2(&pl, 1);
	assert(sz.min == 0);
	assert(sz.max == 0);

	/* near 576 */
	sz = processable_size2(&pl, 576);
	assert(sz.min == 576);
	assert(sz.max == 576);
	sz = processable_size2(&pl, 576-1);
	assert(sz.min == 0);
	assert(sz.max == 0);
	sz = processable_size2(&pl, 576+1);
	assert(sz.min == 576);
	assert(sz.max == 576);
	/* near 576*2 */
	sz = processable_size2(&pl, 576*2);
	assert(sz.min == 576);
	assert(sz.max == 576);
	sz = processable_size2(&pl, 576*2-1);
	assert(sz.min == 576);
	assert(sz.max == 576);
	sz = processable_size2(&pl, 576*2+1);
	assert(sz.min == 576);
	assert(sz.max == 576);

	pl.opts.process_max_frames = 576*2;
	/* near 576 */
	sz = processable_size2(&pl, 576);
	assert(sz.min == 576);
	assert(sz.max == 576);
	sz = processable_size2(&pl, 576-1);
	assert(sz.min == 0);
	assert(sz.max == 0);
	sz = processable_size2(&pl, 576+1);
	assert(sz.min == 576);
	assert(sz.max == 576);
	/* near 576*2 */
	sz = processable_size2(&pl, 576*2);
	assert(sz.min == 576);
	assert(sz.max == 576*2);
	sz = processable_size2(&pl, 576*2-1);
	assert(sz.min == 576);
	assert(sz.max == 576);
	sz = processable_size2(&pl, 576*2+1);
	assert(sz.min == 576);
	assert(sz.max == 576*2);
	/* near 576*3 */
	sz = processable_size2(&pl, 576*3);
	assert(sz.min == 576);
	assert(sz.max == 576*2);
	sz = processable_size2(&pl, 576*3-1);
	assert(sz.min == 576);
	assert(sz.max == 576*2);
	sz = processable_size2(&pl, 576*3+1);
	assert(sz.min == 576);
	assert(sz.max == 576*2);

	/* normal */
	sz = {min: 576, max: 576, mult: 576};
	frames = get_random_processable_size(sz, [](){return 0;});
	assert(frames == 576);
	frames = get_random_processable_size(sz, [](){return RAND_MAX;});
	assert(frames == 576);
	/* daring */
	sz = {min: 576, max: 576*2, mult: 576};
	frames = get_random_processable_size(sz, [](){return 0;});
	assert(frames == 576);
	frames = get_random_processable_size(sz, [](){return RAND_MAX;});
	assert(frames == 576*2);
	/* crazy */
	sz = {min: 1, max: 100'000, mult: 1};
	frames = get_random_processable_size(sz, [](){return 0;});
	assert(frames == 1);
	frames = get_random_processable_size(sz, [](){return RAND_MAX/2;});
	assert(frames == 49'999); /* close enough lol */
	frames = get_random_processable_size(sz, [](){return RAND_MAX;});
	assert(frames == 100'000);
}
#endif
