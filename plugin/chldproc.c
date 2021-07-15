#include "child.h"

#include <assert.h>
#include <alloca.h>
#include <errno.h>
#include <string.h>
#include <sys/uio.h>

#include "ddw.h"
#include "fmt.h"
#include "misc.h"
#include "plugin.h"

// -----------------------------------------------------------------------------

static const char mark1[8] = "password";
static const char mark2[8] = "zanzibar";

#define MIN(a, b) ((a < b) ? (a) : (b))

// the s stands for silly
static void
pcm_convert_s(const ddb_waveformat_t *const infmt,
              const char *const inbuf,
              int const in_frames,
              const ddb_waveformat_t *const outfmt,
              char *const outbuf,
              size_t const outbufcap)
{
	const size_t inbufsz = fmt_frames2bytes(infmt, in_frames);
	const size_t outbufreq = fmt_frames2bytes(outfmt, in_frames);

	char *convbuf = outbuf;
	size_t convbufcap = outbufcap;

	size_t mark1sz;
	char *mark1pos = NULL;

	size_t mark2sz;
	char *mark2pos = NULL;

	assert(outbufcap >= outbufreq);

	// nothing to convert?
	if (in_frames == 0)
		return;

	// already the same format?
	if (memcmp(outfmt, infmt, sizeof(ddb_waveformat_t)) == 0) {
		if (outbuf != inbuf)
			memcpy(outbuf, inbuf, inbufsz);

		return;
	}

	if (outbuf == inbuf) {
		convbufcap = outbufreq+sizeof(mark2);
		convbuf = alloca(convbufcap);
	}

	//
	// verify that pcm_convert() actually does its job
	// it has no error reporting so this has to be done manually
	// - write a known string of bytes to the end of the output buffer
	//   -> will be overwritten if conversion succeeds
	// - write another one just beyond the end (if there's room)
	//   -> should never be overwritten
	//

	mark1sz = MIN(outbufreq, sizeof(mark1));
	mark1pos = convbuf+outbufreq-mark1sz;
	memcpy(mark1pos, mark1, mark1sz);

	if (outbufcap > outbufreq) {
		mark2sz = MIN(outbufcap-outbufreq, sizeof(mark2));
		mark2pos = convbuf+outbufreq;
		memcpy(mark2pos, mark2, mark2sz);
	}

	deadbeef->pcm_convert(
	    infmt, inbuf,
	    outfmt, convbuf,
	    inbufsz);

	if (mark1pos != NULL)
		assert(memcmp(mark1pos, mark1, mark1sz) != 0);
	if (mark2pos != NULL)
		assert(memcmp(mark2pos, mark2, mark2sz) == 0);

	if (convbuf != outbuf)
		memcpy(outbuf, convbuf, outbufreq);
}

// -----------------------------------------------------------------------------

static bool
do_write(struct child *self,
         ddb_waveformat_t *fmt,
         char *data,
         int frames)
{
	struct processing_request request;
	const char *writebuf;
	struct iovec iov[2];
	ssize_t write_rv;

	bool bps_over = (self->pl->max_bps != 0 && fmt->bps > self->pl->max_bps);

	//
	// need to convert before writing?
	//
	if (fmt->is_float || bps_over) {

		ddb_waveformat_t convfmt = *fmt;
		char *p;

		if (bps_over)
			convfmt.bps = self->pl->max_bps;
		convfmt.is_float = 0;

		p = alloca(fmt_frames2bytes(&convfmt, frames));
		pcm_convert_s(
		    fmt, data, frames,
		    &convfmt, p, fmt_frames2bytes(&convfmt, frames));

		fmt->bps = convfmt.bps;
		fmt->is_float = convfmt.is_float;

		writebuf = p;

	} else {

		writebuf = data;

	}

	request = (struct processing_request){
		.buffer_size = fmt_frames2bytes(fmt, frames),
		.samplerate = fmt->samplerate,
		.bitspersample = fmt->bps,
		.channels = fmt->channels,
	};

	iov[0] = (struct iovec){
		.iov_base = &request,
		.iov_len = sizeof(request),
	};
	iov[1] = (struct iovec){
		.iov_base = (void *)writebuf,
		.iov_len = request.buffer_size,
	};

writeagain:
	errno = 0;
	write_rv = writev(self->fds[1], iov, 2);

	if (write_rv == -1) {
		if (errno == EINTR)
			goto writeagain;

		perror("dsp_winamp: writev");

		return false;
	}

	// didn't write everything?
	if ((size_t)write_rv != iov[0].iov_len+iov[1].iov_len) {
		// too lazy to retry this properly
		// i wonder if errno is set in this case

		perror("dsp_winamp: writev (partial write)");

		if (write_rv != 0)
			self->killmenow = true;

		return false;
	}

	return true;
}

static int
do_read(struct child *self,
         ddb_waveformat_t *fmt,
         const ddb_waveformat_t *nextfmt,
         char *data,
         size_t datacap)
{
	struct processing_response response;
	int frames_read = -1;

	if (!read_full(self->fds[0], &response, sizeof(response)))
		goto readerr;

	frames_read = fmt_bytes2frames(fmt, response.buffer_size);

	//
	// output needs to be in a different format?
	//
	if (memcmp(nextfmt, fmt, sizeof(ddb_waveformat_t)) != 0) {

		char *readbuf = alloca(response.buffer_size);

		if (!read_full(self->fds[0], readbuf, response.buffer_size))
			goto readerr;

		pcm_convert_s(
		    fmt, readbuf, frames_read,
		    nextfmt, data, datacap);

		*fmt = *nextfmt;

	} else {

		assert(datacap >= response.buffer_size);

		if (!read_full(self->fds[0], data, response.buffer_size))
			goto readerr;

	}

	return frames_read;
readerr:
	if (errno != 0)
		perror("read");
	else
		fprintf(stderr, "read: unexpected EOF\n");

	self->killmenow = true;

	return -1;
}

// -----------------------------------------------------------------------------

static int
just_convert(struct child *self,
             ddb_waveformat_t *fmt,
             const ddb_waveformat_t *nextfmt,
             char *data,
             int frames,
             size_t datacap)
{
	if (memcmp(nextfmt, fmt, sizeof(ddb_waveformat_t)) != 0) {

		pcm_convert_s(
		    fmt, (const char *)data, frames,
		    nextfmt, (char *)data, datacap);

		*fmt = *nextfmt;

	}

	return frames;
}

// -----------------------------------------------------------------------------

int
child_process_samples(struct child *self,
                      ddb_waveformat_t *fmt,
                      const ddb_waveformat_t *nextfmt,
                      char *data,
                      int frames_in,
                      size_t datacap)
{
	int frames_out = -1;
	bool started = false;

	// child not started?
	if (self->pid == -1) {
		// plugin doesn't have a dll specified?
		if (!ddw_has_dll(self->pl)) {
			frames_out = just_convert(self, fmt, nextfmt, data, frames_in, datacap);
			goto out;
		}

		if (!child_start(self))
			goto out;

		started = true;
	}

	//
	// if the write fails, try restarting the child and retrying the write
	//
write_again:
	if (!do_write(self, fmt, data, frames_in)) {
		if (started)
			goto out;

		child_stop(self);

		if (!child_start(self))
			goto out;

		started = true;

		goto write_again;
	}

	frames_out = do_read(self, fmt, nextfmt, data, datacap);
out:
	if (frames_out >= 0) {
		child_record_success(self);
	} else {
		child_record_failure(self);
		if (self->killmenow)
			child_stop(self);
	}

	return frames_out;
}
