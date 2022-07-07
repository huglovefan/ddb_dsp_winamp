module ddw.host.plugproc;

import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;
import core.atomic;
import std.algorithm : min;
import ddw.host.buf;
import ddw.host.fmt;
import ddw.host.misc;
import ddw.host.plugin;

nothrow:
@nogc:

/**
 * which plugin we're currently calling ModifySamples for
 */
shared(Plugin*) procplug;

/**
 * processes the data with all the plugins
 * 
 * `data` contains the input data, and should have enough reserved space to fit
 *  all active plugins' temporary buffers
 * 
 * tmp is a scratch buffer that the function can use without having to allocate
 *  one itself
 * 
 * the processed output will be in the buffer pointed to by `data`. the function
 *  may swap the innards of `data` and `tmp` to achieve this goal
 */
void plugin_process_all(Plugin[] plugins, const(Fmt)* fmt, Buf* data, Buf* tmp)
{
	foreach (ref pl; plugins)
	{
		if (pl.skip)
			continue;

		plugin_process(&pl, fmt, data, tmp);

		if (data.sz == 0)
			break;
	}
}

// -----------------------------------------------------------------------------

private:

enum MAX_STRETCH_FACTOR = 2;

/**
 * with `frames_avail` frames available to process, get the number that this
 *  plugin could process right now in one call to ModifySamples()
 * 
 * returns nonzero number on success, 0 if nothing can be processed yet
 *  (probably need more data)
 */
uint processable_size(const(Plugin)* pl, uint frames_avail) pure
{
	uint pMf = pl.opts.process_max_frames;
	uint pfm = pl.opts.process_frames_mult;
	uint pmf = pl.opts.process_min_frames;

	if (pMf != 0 && frames_avail > pMf)
		frames_avail = pMf;

	if (pfm != 1)
		frames_avail -= frames_avail % pfm;

	if (frames_avail < pmf)
		frames_avail = 0;

	return frames_avail;
}

void plugin_process(
	Plugin* pl,
	const(Fmt)* fmt,
	Buf* data,
	Buf* tmp)
{
	const(uint) avail = fmt_bytes2frames(fmt, pl.buf.sz+data.sz);
	const(uint) processable = processable_size(pl, avail);

	if (processable != 0)
	{
		// if we have stored data from a previous call, prepend it to the buffer
		if (pl.buf.sz != 0)
		{
			buf_prepend_buf(data, &pl.buf);
			buf_clear(&pl.buf);
		}

		// if we can process the entire buffer in one call OR the plugin is
		//  known not to stretch, it's safe to do the operation with one buffer
		if (processable == avail || pl.opts.nostretch)
			tmp = data;

		plugin_process_twobuf_or_just_one(pl, fmt, data, tmp);
	}
	else
	// not going to process, move the data to this plugin's temporary buffer
	{
		if (pl.buf.sz == 0)
		// if there's no older stored data, we can do a swap
		{
			buf_swap(&pl.buf, data);
		}
		else
		// append normally
		{
			buf_append_buf(&pl.buf, data);
			buf_clear(data);
		}

		// next plugin won't be called
		assert(data.sz == 0);
	}
}

void plugin_process_twobuf_or_just_one(
	Plugin* pl,
	const(Fmt)* fmt,
	Buf* data,
	Buf* tmp)
{
	const(size_t) fs = fmt_frame_size(fmt);
	const(uint) pl_stretch_factor = (!pl.opts.nostretch) ? MAX_STRETCH_FACTOR : 1;

	if (tmp == data)
	// using one buffer -> preallocate maximum stretch size
	{
		buf_prepare_capacity(tmp, data.sz*pl_stretch_factor);
	}
	else
	// using two buffers -> prepare `tmp` to become a copy of `data` with the
	//  processed result
	{
		buf_clear(tmp);
		// same reserved space + maximum stretch size
		buf_prepare_append(tmp, data.res + data.sz*pl_stretch_factor);
		buf_init_reserved(tmp, data.res);
	}

	const(void*) readstart = data.p;
	const(void)* readp     = data.p;
	const(void*) readend   = data.p + data.sz;

	const(void*) writestart = tmp.p;
	void*        writep     = tmp.p;
	const(void*) writeend   = tmp.p + tmp.cap;

	// loop while there's data to read && space in the output buffer
	while (readp < readend && writep < writeend)
	{
		uint readable = cast(uint)((readend-readp)/fs);
		uint writable = cast(uint)((writeend-writep)/fs);

		ModifySamples_s(pl, fmt,
			readp, &readable,
			writep, &writable);

		readp += fs*readable;
		writep += fs*writable;

		// if using one buffer: the write pointer going past read pointer means
		//  the plugin stretched sound
		if (writestart == readstart && writep > readp)
		{
			// if this wasn't at the end of the input buffer, some of the
			//  following input data must've been overwritten
			// (assert if that happens)
			assert(readp == readend);

			// skip the stretch tail so we don't process it twice
			readp = writep;
		}

		// nothing was read?
		if (readable == 0)
			break;
	}

	// didn't read all of the input data?
	// save it to this plugin's temporary buffer to be processed on next call
	if (readp < readend)
	{
		if (data != tmp)
		// used two buffers -> swap the old input buffer to pl.buf
		{
			// "skip" the part we did read by marking it as reserved
			buf_increase_reserved(data, readp-readstart);
			// swap it into place
			buf_swap(&pl.buf, data);
		}
		else
		// used one buffer -> copy it normally
		{
			// this was cleared before calling plugin_process_twobuf_or_just_one()
			assert(pl.buf.sz == 0);

			buf_append(&pl.buf, readp, readend-readp);
		}
	}

	// finalize the length of `tmp` and swap it into `data`
	buf_set_size(tmp, writep-writestart);
	// this is a no-op if just one buffer was used
	buf_swap(data, tmp);
}

void ModifySamples_s(
	Plugin* pl,
	const(Fmt)* fmt,
	const(void)* inbuf,
	uint* inbuf_frames_out,
	void* outbuf,
	uint* outbuf_frames_out)
{
	const(size_t) fs = fmt_frame_size(fmt);
	const(uint) pl_stretch_factor = (!pl.opts.nostretch) ? MAX_STRETCH_FACTOR : 1;

	const(uint) inbuf_frames = processable_size(pl, *inbuf_frames_out);
	const(uint) outbuf_frames = *outbuf_frames_out;

	int plug_rv;

	// nothing to process?
	if (inbuf_frames == 0)
		goto abort;

	// stretched result won't fit?
	if (inbuf_frames*pl_stretch_factor > outbuf_frames)
		goto abort;

	// move the input where we want the output
	if (outbuf != inbuf)
		memmove(outbuf, inbuf, fs*inbuf_frames);

	procplug.atomicStore(cast(shared)pl);
	plug_rv = pl.module_.ModifySamples(pl.module_,
		cast(short*)outbuf,
		cast(int)inbuf_frames,
		cast(int)fmt.bps,
		cast(int)fmt.ch,
		cast(int)fmt.rate);
	procplug.atomicStore(null);

	// funny return value?
	assert(plug_rv >= 0);

	// stretched more than allowed / fits?
	assert(cast(uint)plug_rv <= inbuf_frames*pl_stretch_factor);
	assert(cast(uint)plug_rv <= outbuf_frames);

	*inbuf_frames_out = inbuf_frames;
	*outbuf_frames_out = cast(uint)plug_rv;

	return;
abort:
	*inbuf_frames_out = 0;
	*outbuf_frames_out = 0;
	return;
}
