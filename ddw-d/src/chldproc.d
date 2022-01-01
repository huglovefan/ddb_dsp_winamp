module ddw.chldproc;

import core.stdc.errno;
import core.stdc.stdlib;
import core.stdc.string;
import core.sys.posix.sys.uio;

import std.algorithm : min;
import std.conv;
import std.exception;
import std.stdio;

import ddw.pipedata;
import ddw.child;
import ddw.chldinit;
import ddw.fmt;
import ddw.misc;
import ddw.plugin;
import ddw.zzx_deadbeef;

uint child_process_samples(
	Child* self,
	ddb_waveformat_t* fmt,
	const(ddb_waveformat_t)* wantfmt,
	char* data,
	uint frames_in,
	size_t datacap)
{
	if (self.pid == -1)
	{
		if (!ddw_has_dll(self.pl))
		{
			return just_convert(self, fmt, wantfmt, data, frames_in, datacap);
		}

		child_start(self);
	}

	do_write(self, fmt, data, frames_in);
	return do_read(self, fmt, wantfmt, data, datacap);
}

// -----------------------------------------------------------------------------

private:

uint just_convert(
	Child* self,
	ddb_waveformat_t* fmt,
	const(ddb_waveformat_t)* nextfmt,
	char* data,
	uint frames,
	size_t datacap)
{
	if (*fmt != *nextfmt)
	{
		pcm_convert_s(
			fmt, data, frames,
			nextfmt, data, datacap);

		*fmt = *nextfmt;
	}

	return frames;
}

void do_write(
	Child* self,
	ddb_waveformat_t* fmt,
	char* data,
	int frames)
{
	const(char)* writebuf;

	bool bps_over = false;
	if (self.pl.max_bps != 0 && fmt.bps > self.pl.max_bps)
		bps_over = true;

	if (fmt.is_float || bps_over)
	{
		ddb_waveformat_t convfmt = *fmt;
		convfmt.bps = (bps_over) ? self.pl.max_bps : fmt.bps;
		convfmt.is_float = 0;

		char[] convbuf = new char[fmt_frames2bytes(&convfmt, frames)+8];

		pcm_convert_s(
		    fmt, data, frames,
		    &convfmt, convbuf.ptr, convbuf.length);

		fmt.bps = convfmt.bps;
		fmt.is_float = convfmt.is_float;

		writebuf = convbuf.ptr;
	}
	else
	{
		writebuf = data;
	}

	processing_request request = {
		buffer_size: fmt_frames2bytes(fmt, frames),
		samplerate: fmt.samplerate,
		bitspersample: fmt.bps.to!ubyte,
		channels: fmt.channels.to!ubyte,
	};
	write_req_and_data(self, &request, writebuf);
}

void write_req_and_data(
	Child* self,
	const(processing_request)* request,
	const(char)* data)
{
	iovec[2] iov = [
		{
			iov_base: cast(void*)request,
			iov_len: (*request).sizeof,
		},
		{
			iov_base: cast(void*)data,
			iov_len: request.buffer_size,
		},
	];
	errnoEnforce(writev(self.fds[1], iov.ptr, iov.length) == iov[0].iov_len+iov[1].iov_len);
}

uint do_read(
	Child* self,
	ddb_waveformat_t* fmt,
	const(ddb_waveformat_t)* wantfmt,
	char* data,
	size_t datacap)
{
	processing_response response = void;
	errnoEnforce(read_full(self.fds[0], &response, response.sizeof));

	enforce((response.buffer_size % fmt_frame_size(fmt)) == 0);

	const(uint) frames_read = fmt_bytes2frames(fmt, response.buffer_size);

	if (frames_read > 0)
	{
		if (*fmt != *wantfmt)
		{
			char[] readbuf = new char[response.buffer_size];
			errnoEnforce(read_full(self.fds[0], readbuf.ptr, readbuf.length));
			pcm_convert_s(fmt, readbuf.ptr, frames_read, wantfmt, data, datacap);
			*fmt = *wantfmt;
		}
		else
		{
			enforce(response.buffer_size <= datacap);
			errnoEnforce(read_full(self.fds[0], data, response.buffer_size));
		}
	}
	else
	{
		*fmt = *wantfmt;
	}

	return frames_read;
}

// -----------------------------------------------------------------------------

// source: https://www.random.org/bytes/
static immutable char[8] mark1 = [0x8f, 0xad, 0xb2, 0xe9, 0xcd, 0x17, 0xec, 0xda];
static immutable char[8] mark2 = [0x1c, 0xd3, 0x96, 0xe0, 0x0c, 0xd2, 0x42, 0xac];

void pcm_convert_s(
	const(ddb_waveformat_t)* infmt,
	const(char)* inbuf,
	int in_frames,
	const(ddb_waveformat_t)* outfmt,
	char* outbuf,
	size_t outbufcap)
{
	const(size_t) inbufsz = fmt_frames2bytes(infmt, in_frames);
	const(size_t) outbufreq = fmt_frames2bytes(outfmt, in_frames);

	char[] convbuf = outbuf[0..outbufcap];

	char[] mark1at;
	char[] mark2at;

	if (in_frames == 0)
		return;

	fmt_assert_reasonable(infmt);
	fmt_assert_reasonable(outfmt);

	assert(outbufcap >= outbufreq);

	assert(outfmt.samplerate == infmt.samplerate);
	assert(outfmt.is_bigendian == infmt.is_bigendian);

	if (
		outfmt.bps == infmt.bps &&
		outfmt.is_float == infmt.is_float &&
		outfmt.channels == infmt.channels &&
		outfmt.channelmask == infmt.channelmask)
	 {
		if (outbuf != inbuf)
			memcpy(outbuf, inbuf, inbufsz);

		return;
	}

	if (outbuf == inbuf)
	{
		convbuf = new char[outbufreq+mark2.sizeof];
	}

	{
		size_t mark1sz = min(outbufreq, mark1.sizeof);
		char* mark1pos = convbuf.ptr+outbufreq-mark1sz;
		mark1pos[0..mark1sz] = mark1[0..mark1sz];
		mark1at = mark1pos[0..mark1sz];
	}

	if (outbufcap > outbufreq)
	{
		size_t mark2sz = min(outbufcap-outbufreq, mark2.sizeof);
		char* mark2pos = convbuf.ptr+outbufreq;
		mark2pos[0..mark2sz] = mark2[0..mark2sz];
		mark2at = mark2pos[0..mark2sz];
	}

	deadbeef.pcm_convert(
		infmt, inbuf,
		outfmt, convbuf.ptr,
		cast(int)inbufsz);

	assert(mark1at != mark1[0..mark1at.length]);
	if (mark2at != null)
		assert(mark2at == mark2[0..mark2at.length]);

	if (convbuf.ptr != outbuf)
		memcpy(outbuf, convbuf.ptr, outbufreq);
}
