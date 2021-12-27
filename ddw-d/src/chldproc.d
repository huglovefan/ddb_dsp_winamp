module ddw.chldproc;

import core.stdc.errno;
import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;
import core.sys.posix.sys.uio;

import std.algorithm : min;

import ddw.pipedata;
import ddw.child;
import ddw.chldinit;
import ddw.fmt;
import ddw.misc;
import ddw.plugin;
import ddw.zzx_deadbeef;

enum REASONABLE_STACK_BUFFER_SIZE = 2*1025*(32/8)*2+8;
//debug = alloca_max_size;
//debug = malloc_trace;

// -----------------------------------------------------------------------------

// source: https://www.random.org/bytes/
static immutable char[8] mark1 = [0x8f, 0xad, 0xb2, 0xe9, 0xcd, 0x17, 0xec, 0xda];
static immutable char[8] mark2 = [0x1c, 0xd3, 0x96, 0xe0, 0x0c, 0xd2, 0x42, 0xac];

// the s stands for silly
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

	char* convbuf = outbuf;
	size_t convbufcap = outbufcap;
	bool convbufalloc;

	size_t mark1sz;
	char* mark1pos = null;

	size_t mark2sz;
	char* mark2pos = null;

	assert(outfmt != infmt); // detect misuse
	fmt_assert_reasonable(infmt);
	fmt_assert_reasonable(outfmt);

	assert(outbufcap >= outbufreq);

	// nothing to convert?
	if (in_frames == 0)
		return;

	// member           can be converted
	//  bps              yes
	//  is_float         yes
	//  channels         yes
	//  channelmask      yes
	//  samplerate       no
	//  is_bigendian     no

	// these can't be converted here
	assert(outfmt.samplerate == infmt.samplerate);
	assert(outfmt.is_bigendian == infmt.is_bigendian);

	//
	// if all convertible members are the same already, then there's nothing to do
	//
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

	//
	// if the source and destination buffers are the same, then we need a temporary buffer to hold the result
	//
	if (outbuf == inbuf)
	{
		convbufcap = outbufreq+mark2.sizeof;

		if (convbufcap <= REASONABLE_STACK_BUFFER_SIZE)
		{
			debug (alloca_max_size)
			{
				convbuf = cast(char*)alloca(REASONABLE_STACK_BUFFER_SIZE);
				convbuf[0..REASONABLE_STACK_BUFFER_SIZE] = 1;
				size_t sum = 0;
				for (size_t i = 0; i < REASONABLE_STACK_BUFFER_SIZE; i++) sum += convbuf[i];
				assert(sum == REASONABLE_STACK_BUFFER_SIZE);
			}
			else
			{
				convbuf = cast(char*)alloca(convbufcap);
			}
		}
		else
		{
			convbuf = cast(char*)malloc(convbufcap);
			convbufalloc = true;
			debug (malloc_trace)
				fprintf(stderr, "pcm_convert_s: malloc %zu bytes\n", convbufcap);
		}
	}

	//
	// verify that pcm_convert() actually does its job
	// it has no error reporting so this has to be done manually
	// - write a known string of bytes to the end of the output buffer
	//   -> will be overwritten if conversion succeeds
	// - write another one just beyond the end (if there's room)
	//   -> should never be overwritten
	//

	//
	// leave a mark at the beginning of the output buffer to verify that it is overwritten
	//
	mark1sz = min(outbufreq, mark1.sizeof);
	mark1pos = convbuf+outbufreq-mark1sz;
	memcpy(mark1pos, mark1.ptr, mark1sz);

	//
	// if there's space, leave a mark past the end of the expected data in the output buffer to verify that it isn't overwritten
	//
	if (outbufcap > outbufreq)
	{
		mark2sz = min(outbufcap-outbufreq, mark2.sizeof);
		mark2pos = convbuf+outbufreq;
		memcpy(mark2pos, mark2.ptr, mark2sz);
	}

	deadbeef.pcm_convert(
		infmt, inbuf,
		outfmt, convbuf,
		cast(int)inbufsz);

	if (mark1pos != null)
		assert(memcmp(mark1pos, mark1.ptr, mark1sz) != 0);
	if (mark2pos != null)
		assert(memcmp(mark2pos, mark2.ptr, mark2sz) == 0);

	// if the conversion wasn't done to the output buffer, then we need to copy it
	if (convbuf != outbuf)
		memcpy(outbuf, convbuf, outbufreq);

	if (convbufalloc)
		free(convbuf);
}

// -----------------------------------------------------------------------------

bool do_write(
	Child* self,
	ddb_waveformat_t* fmt,
	char* data,
	int frames)
{
	processing_request request;
	ddb_waveformat_t convfmt;
	char* convbuf;
	bool convalloc;
	const(char)* writebuf;
	size_t convsz;
	bool bps_over;
	bool success;

	bps_over = false;
	if (self.pl.max_bps != 0 && fmt.bps > self.pl.max_bps)
		bps_over = true;

	//
	// need to convert before writing?
	//
	if (fmt.is_float || bps_over)
	{
		convfmt = *fmt;

		if (bps_over)
			convfmt.bps = self.pl.max_bps;
		convfmt.is_float = 0;

		convsz = fmt_frames2bytes(&convfmt, frames);

		enum convbuf_extra = 8; // room for error detection
		if (convsz+convbuf_extra <= REASONABLE_STACK_BUFFER_SIZE)
		{
			debug (alloca_max_size)
			{
				convbuf = cast(char*)alloca(REASONABLE_STACK_BUFFER_SIZE);
				convbuf[0..REASONABLE_STACK_BUFFER_SIZE] = 1;
				size_t sum = 0;
				for (size_t i = 0; i < REASONABLE_STACK_BUFFER_SIZE; i++) sum += convbuf[i];
				assert(sum == REASONABLE_STACK_BUFFER_SIZE);
			}
			else
			{
				convbuf = cast(char*)alloca(convsz+convbuf_extra);
			}
		}
		else
		{
			convbuf = cast(char*)malloc(convsz+convbuf_extra);
			convalloc = true;
			debug (malloc_trace)
				fprintf(stderr, "do_write: malloc %zu bytes\n", convsz+convbuf_extra);
		}

		if (convbuf == null)
		{
			success = false;
			goto out_notmpbuf;
		}

		pcm_convert_s(
		    fmt, data, frames,
		    &convfmt, convbuf, convsz+convbuf_extra);

		fmt.bps = convfmt.bps;
		fmt.is_float = convfmt.is_float;

		writebuf = convbuf;
	}
	else
	{
		writebuf = data;
	}

	request = processing_request.init;
	request.buffer_size = fmt_frames2bytes(fmt, frames);
	request.samplerate = fmt.samplerate;
	request.bitspersample = cast(ubyte)fmt.bps;
	request.channels = cast(ubyte)fmt.channels;

	success = write_req_and_data(self, &request, writebuf);

	if (convalloc)
		free(convbuf);
out_notmpbuf:
	return success;
}

bool write_req_and_data(
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
		}
	];
	ssize_t write_rv;

again:
	errno = 0;
	write_rv = writev(self.fds[1], iov.ptr, iov.length);

	if (write_rv == -1)
	{
		if (errno == EINTR)
			goto again;

		perror("dsp_winamp: writev");

		return false;
	}

	// didn't write everything?
	if (cast(size_t)write_rv != iov[0].iov_len+iov[1].iov_len)
	{
		// too lazy to retry this properly
		// i wonder if errno is set in this case

		perror("dsp_winamp: writev (partial write)");

		if (write_rv != 0)
			self.fatalerror = true;

		return false;
	}

	return true;
}

// -----------------------------------------------------------------------------

int do_read(
	Child* self,
	ddb_waveformat_t* fmt,
	const(ddb_waveformat_t)* nextfmt,
	char* data,
	size_t datacap)
{
	processing_response response;
	int frames_read;
	bool needconv;

	//
	// read and parse the response struct
	//

	if (!read_full(self.fds[0], &response, response.sizeof))
		goto readerr;

	if (response.buffer_size % fmt_frame_size(fmt) != 0)
		goto err;

	frames_read = cast(int)fmt_bytes2frames(fmt, cast(int)response.buffer_size);

	//
	// check if we need to convert
	//

	needconv = false;
	if (!fmt_same(fmt, nextfmt))
		needconv = true;

	//
	// read (if needed) and convert (if needed)
	//

	if (frames_read == 0)
	{
		if (needconv)
			*fmt = *nextfmt;

		goto Lout;
	}

	if (needconv)
	{
		char* readbuf;
		bool readalloc;

		if (response.buffer_size <= REASONABLE_STACK_BUFFER_SIZE)
		{
			debug (alloca_max_size)
			{
				readbuf = cast(char*)alloca(REASONABLE_STACK_BUFFER_SIZE);
				readbuf[0..REASONABLE_STACK_BUFFER_SIZE] = 1;
				size_t sum = 0;
				for (size_t i = 0; i < REASONABLE_STACK_BUFFER_SIZE; i++) sum += readbuf[i];
				assert(sum == REASONABLE_STACK_BUFFER_SIZE);
			}
			else
			{
				readbuf = cast(char*)alloca(response.buffer_size);
			}
		}
		else
		{
			readbuf = cast(char*)malloc(response.buffer_size);
			readalloc = true;
			debug (malloc_trace)
				fprintf(stderr, "do_read: malloc %zu bytes\n", response.buffer_size);
		}

		if (readbuf == null)
			goto err;

		if (!read_full(self.fds[0], readbuf, response.buffer_size))
		{
			if (readalloc)
				free(readbuf);

			goto readerr;
		}

		pcm_convert_s(
			fmt, readbuf, frames_read,
			nextfmt, data, datacap);

		*fmt = *nextfmt;

		if (readalloc)
			free(readbuf);
	}
	else
	{
		if (response.buffer_size > datacap)
			goto err;

		if (!read_full(self.fds[0], data, response.buffer_size))
			goto readerr;
	}
Lout:
	return frames_read;
readerr:
	if (errno != 0)
		perror("read");
	else
		fprintf(stderr, "read: unexpected EOF\n");
err:
	self.fatalerror = true;
	return -1;
}

// -----------------------------------------------------------------------------

int just_convert(
	Child* self,
	ddb_waveformat_t* fmt,
	const(ddb_waveformat_t)* nextfmt,
	char* data,
	int frames,
	size_t datacap)
{
	if (!fmt_same(fmt, nextfmt))
	{
		pcm_convert_s(
			fmt, cast(const(char)*)data, frames,
			nextfmt, cast(char*)data, datacap);

		*fmt = *nextfmt;
	}

	return frames;
}

// -----------------------------------------------------------------------------

int child_process_samples(
	Child* self,
	ddb_waveformat_t* fmt,
	const(ddb_waveformat_t)* nextfmt,
	char* data,
	int frames_in,
	size_t datacap)
{
	int frames_out = -1;
	bool started = false;

	if (frames_in < 0)
	{
		frames_out = 0;
		goto out_norecord;
	}

	// child not started?
	if (self.pid == -1)
	{
		// plugin doesn't have a dll set?
		if (!ddw_has_dll(self.pl))
		{
			frames_out = just_convert(self, fmt, nextfmt, data, frames_in, datacap);
			goto out_norecord;
		}

		if (!child_start(self))
			goto Lout;

		started = true;
	}

	//
	// if the write fails, try restarting the child and retrying the write
	//
write_again:
	if (!do_write(self, fmt, data, frames_in))
	{
		// already started by us?
		if (started)
			goto Lout;

		child_stop(self);
		if (!child_start(self))
			goto Lout;
		started = true;

		goto write_again;
	}

	frames_out = do_read(self, fmt, nextfmt, data, datacap);
Lout:
	if (frames_out >= 0)
	{
		child_record_success(self);
	}
	else
	{
		child_record_failure(self);
		if (self.fatalerror)
			child_stop(self);
	}
out_norecord:
	return frames_out;
}
