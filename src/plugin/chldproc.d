module ddw.plugin.chldproc;

import core.stdc.stdint;
import core.sys.posix.sys.uio;

import std.exception : ErrnoException;

import ddw.common.pipedata;
import ddw.plugin.child;
import ddw.plugin.chldinit;
import ddw.plugin.fmt;
import ddw.plugin.misc;
import ddw.plugin.main;
import ddw.plugin.deadbeef;

void[] child_process_samples(
	Child* self,
	const(void[]) inbuf,
	const(ddb_waveformat_t*) infmt,
	void[] outbuf,
	const(ddb_waveformat_t*) wantfmt)
{
	if (self.pid == -1)
	{
		if (!ddw_has_dll(self.pl))
		{
			return pcm_convert_s(
				inbuf, infmt,
				outbuf, wantfmt);
		}

		child_start(self);
	}

	ddb_waveformat_t tmpfmt = *infmt;
	do_write(self, &tmpfmt, inbuf);
	return do_read(self, &tmpfmt, wantfmt, outbuf);
}

// -----------------------------------------------------------------------------

private:

size_t min(size_t a, size_t b)
{
	if (a > b) a = b;
	return a;
}

void do_write(
	Child* self,
	ddb_waveformat_t* curfmt,
	const(void[]) inbuf)
{
	const(void)[] writebuf;

	bool bps_over = false;
	if (self.pl.max_bps != 0 && curfmt.bps > self.pl.max_bps)
		bps_over = true;

	if (curfmt.is_float || bps_over)
	{
		ddb_waveformat_t convfmt = *curfmt;
		convfmt.bps = (bps_over) ? self.pl.max_bps : curfmt.bps;
		convfmt.is_float = 0;

		uint frames = fmt_bytes2frames(curfmt, inbuf.length);
		size_t convbytes = fmt_frames2bytes(&convfmt, frames);
		void[] convbuf = new void[convbytes+8];

		writebuf = pcm_convert_s(
			inbuf, curfmt,
			convbuf, &convfmt);

		curfmt.bps = convfmt.bps;
		curfmt.is_float = convfmt.is_float;
	}
	else
	{
		writebuf = inbuf;
	}

	processing_request request = {
		buffer_size: writebuf.length,
		samplerate: curfmt.samplerate,
		bitspersample: cast(uint8_t)curfmt.bps,
		channels: cast(uint8_t)curfmt.channels,
	};
	iovec[2] iov = [
		{
			iov_base: cast(void*)&request,
			iov_len: request.sizeof,
		},
		{
			iov_base: cast(void*)writebuf.ptr,
			iov_len: writebuf.length,
		},
	];
	if (writev(self.fds[1], iov.ptr, iov.length) != iov[0].iov_len+iov[1].iov_len)
		throw new ErrnoException("writev");
}

void[] do_read(
	Child* self,
	ddb_waveformat_t* curfmt,
	const(ddb_waveformat_t*) wantfmt,
	void[] outbuf)
out
{
	assert(*curfmt == *wantfmt);
}
do
{
	processing_response response;
	if (!read_full(self.fds[0], &response, response.sizeof))
		throw new ErrnoException("read_full");

	if (response.buffer_size > 0)
	{
		if (*curfmt != *wantfmt)
		{
			void[] tmpbuf = new void[response.buffer_size];
			if (!read_full(self.fds[0], tmpbuf))
				throw new ErrnoException("read_full");

			outbuf = pcm_convert_s(
				tmpbuf, curfmt,
				outbuf, wantfmt);

			*curfmt = *wantfmt;
		}
		else
		{
			outbuf = outbuf[0..response.buffer_size];
			if (!read_full(self.fds[0], outbuf))
				throw new ErrnoException("read_full");
		}
	}
	else
	{
		outbuf = outbuf[0..0];
		*curfmt = *wantfmt;
	}

	return outbuf;
}

// -----------------------------------------------------------------------------

// source: https://www.random.org/bytes/
static immutable char[8] mark1 = [0x8f, 0xad, 0xb2, 0xe9, 0xcd, 0x17, 0xec, 0xda];
static immutable char[8] mark2 = [0x1c, 0xd3, 0x96, 0xe0, 0x0c, 0xd2, 0x42, 0xac];
static immutable char[8] mark3 = ['D',  'e',  'a',  'D',  'B',  'e',  'e',  'F',];

void[] pcm_convert_s(
	const(void[]) inbuf,
	const(ddb_waveformat_t*) infmt,
	void[] outbuf,
	const(ddb_waveformat_t*) outfmt)
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
		return outbuf[0..0];
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
			outbuf[0..outbufreq] = inbuf[0..outbufreq];
		}

		return outbuf[0..outbufreq];
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

	return outbuf[0..outbufreq];
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
