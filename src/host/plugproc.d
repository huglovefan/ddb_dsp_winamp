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
		if (pl.buf.sz != 0)
		{
			buf_prepend_buf(data, &pl.buf);
			buf_clear(&pl.buf);
		}

		if (processable == avail || pl.opts.nostretch)
			tmp = data;

		plugin_process_twobuf_or_just_one(pl, fmt, data, tmp);
	}
	else
	{
		if (pl.buf.sz == 0)
		{
			buf_swap(&pl.buf, data);
		}
		else
		{
			buf_append_buf(&pl.buf, data);
			buf_clear(data);
		}
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
	{
		buf_prepare_capacity(tmp, data.sz*pl_stretch_factor);
	}
	else
	{
		buf_clear(tmp);
		buf_prepare_append(tmp, data.res + data.sz*pl_stretch_factor);
		buf_init_reserved(tmp, data.res);
	}

	const(void*) readstart = data.p;
	const(void)* readp     = data.p;
	const(void*) readend   = data.p + data.sz;

	const(void*) writestart = tmp.p;
	void*        writep     = tmp.p;
	const(void*) writeend   = tmp.p + tmp.cap;

	while (readp < readend && writep < writeend)
	{
		uint readable = cast(uint)((readend-readp)/fs);
		uint writable = cast(uint)((writeend-writep)/fs);

		ModifySamples_s(pl, fmt,
			readp, &readable,
			writep, &writable);

		readp += fs*readable;
		writep += fs*writable;

		// skip stretch tail
		if (writestart == readstart && writep > readp)
		{
			assert(readp == readend); // stretch overwrote data

			readp = writep;
		}

		if (readable == 0)
			break;
	}

	if (readp < readend)
	{
		if (data != tmp)
		{
			// "skip" the read amount
			buf_increase_reserved(data, readp-readstart);
			buf_swap(&pl.buf, data);
		}
		else
		{
			buf_clear(&pl.buf);
			buf_append(&pl.buf, readp, readend-readp);
		}
	}

	buf_set_size(tmp, writep-writestart);
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

	if (inbuf_frames == 0)
		goto abort;

	if (inbuf_frames*pl_stretch_factor > outbuf_frames)
		goto abort;

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

	assert(plug_rv >= 0);

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
