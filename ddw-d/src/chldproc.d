module ddw.chldproc;

import core.stdc.stdint;
import core.sys.posix.sys.uio;

import std.algorithm : min;
import std.conv;
import std.exception;

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
		bitspersample: fmt.bps.to!uint8_t,
		channels: fmt.channels.to!uint8_t,
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
static immutable char[8] mark3 = ['D',  'e',  'a',  'D',  'B',  'e',  'e',  'F',];

void pcm_convert_s(
	const(void[]) inbuf,
	const(ddb_waveformat_t)* infmt,
	void[] outbuf,
	const(ddb_waveformat_t)* outfmt)
{
	const(uint) nframes_in = fmt_bytes2frames(infmt, inbuf.length);

	const(size_t) outbufreq = fmt_frames2bytes(outfmt, nframes_in);

	// check input size (deadbeef api takes this as a signed int)
	assert(inbuf.length <= int.max);

	// check output buffer size
	assert(outbuf.length >= outbufreq);

	// check format sanity
	assert(fmt_is_reasonable(infmt));
	assert(fmt_is_reasonable(outfmt));

	// check properties that can't be converted here
	assert(outfmt.samplerate == infmt.samplerate);
	assert(outfmt.is_bigendian == infmt.is_bigendian);

	// nothing to do?
	if (nframes_in == 0)
	{
		return;
	}

	// are all convertible properties the same already?
	if (
		outfmt.bps == infmt.bps &&
		outfmt.is_float == infmt.is_float &&
		outfmt.channels == infmt.channels &&
		outfmt.channelmask == infmt.channelmask)
	 {
		if (outbuf.ptr != inbuf.ptr)
		{
			outbuf[0..inbuf.length] = inbuf[0..inbuf.length];
		}

		return;
	}

	void[] convbuf = outbuf;

	// if output and input buffers are the same, we need a temporary buffer to hold the result
	if (outbuf.ptr == inbuf.ptr)
	{
		convbuf = new void[outbufreq+8];
	}

	void[] mark1at;
	void[] mark2at;
	void[] mark3at;
	{
		{
			size_t mark1fit = min(8, outbufreq);
			mark1at = convbuf[0..mark1fit];
			mark1at[] = cast(char[])mark1[0..mark1fit];
		}

		if (outbufreq > 8)
		{
			size_t mark2fit = min(8, outbufreq-8);
			mark2at = convbuf[8..8+mark2fit];
			mark2at[] = cast(char[])mark2[0..mark2fit];
		}

		if (convbuf.length > outbufreq)
		{
			size_t mark3fit = min(8, convbuf.length-outbufreq);
			mark3at = convbuf[outbufreq..outbufreq+mark3fit];
			mark3at[] = cast(char[])mark3[0..mark3fit];
		}
	}

	version (unittest)
	{
		unittest_pcm_convert(
			infmt, cast(char*)inbuf.ptr,
			outfmt, cast(char*)convbuf.ptr,
			cast(int)inbuf.length);
	}
	else
	{
		deadbeef.pcm_convert(
			infmt, cast(char*)inbuf.ptr,
			outfmt, cast(char*)convbuf.ptr,
			cast(int)inbuf.length);
	}

	assert(mark1at.ptr != null && mark1at != mark1[0..mark1at.length]);
	assert(mark2at.ptr == null || mark2at != mark2[0..mark2at.length]);
	assert(mark3at.ptr == null || mark3at == mark3[0..mark3at.length]);

	// if conversion was done to a temporary buffer, copy it to the output
	if (convbuf.ptr != outbuf.ptr)
	{
		outbuf[0..outbufreq] = convbuf[0..outbufreq];
	}
}

void pcm_convert_s(
	const(ddb_waveformat_t)* infmt,
	const(char)* inbuf,
	int in_frames,
	const(ddb_waveformat_t)* outfmt,
	char* outbuf,
	size_t outbufcap)
{
	pcm_convert_s(
		inbuf[0..fmt_frames2bytes(infmt, in_frames)], infmt,
		outbuf[0..outbufcap], outfmt);
}

version (unittest)
{
	__gshared size_t acnt = 0;
	__gshared size_t bcnt = 0;

	int unittest_pcm_convert(
		const(ddb_waveformat_t)* inputfmt,
		const(char)* input,
		const(ddb_waveformat_t)* outputfmt,
		char* output,
		int inputsize)
	{
		uint inframes = inputsize / (inputfmt.channels*(inputfmt.bps/8));
		size_t outbufsz = cast(ulong)inframes * (outputfmt.channels*(outputfmt.bps/8));

		acnt = 0;
		foreach (i; 0..inputsize)
		{
			if (input[i] == 'a') acnt++;
		}

		output[0..outbufsz] = 'b';
		bcnt = outbufsz;

		return 0;
	}
}

unittest
{
	import core.exception;
	import std.exception;

	enum channels = 2;
	foreach (nframes; [1, 7, 15, 20])
	foreach (bps1; [8, 16, 24, 32])
	foreach (bps2; [8, 16, 24, 32])
	foreach (markspace; [0, 6, 8])
	foreach (samebuf; [false, true])
	{
		ubyte[] buf1 = new ubyte[nframes*channels*(bps1/8)];
		ubyte[] buf2 = !samebuf ? new ubyte[markspace + nframes*channels*(bps2/8)] : buf1;
		ddb_waveformat_t fmt1 = {
			bps: bps1,
			channels: channels,
			samplerate: 44100,
			channelmask: 0b11,
			is_float: 0,
			is_bigendian: 0,
		};
		ddb_waveformat_t fmt2 = {
			bps: bps2,
			channels: channels,
			samplerate: 44100,
			channelmask: 0b11,
			is_float: 0,
			is_bigendian: 0,
		};

		if (samebuf && bps2 > bps1)
		{
			assertThrown!AssertError(pcm_convert_s(
				cast(void[])buf1, &fmt1,
				cast(void[])buf2, &fmt2));

			continue;
		}
		else
		{
			buf1[] = 'a';
			if (!samebuf) buf2[] = 'x';
			acnt = 0;
			bcnt = 0;

			pcm_convert_s(
				cast(void[])buf1, &fmt1,
				cast(void[])buf2, &fmt2);

			if (bps1 != bps2)
			{
				assert(acnt == buf1.length);
				assert(bcnt == nframes*channels*(bps2/8));
				size_t checkbcnt = 0;
				foreach (i; 0..bcnt)
				{
					if (buf2[i] == 'b') checkbcnt++;
				}
				assert(checkbcnt == bcnt);
			}
			else
			{
				assert(acnt == 0);
				assert(bcnt == 0);
			}
		}
	}
}
