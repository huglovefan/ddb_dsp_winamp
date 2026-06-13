#include "chldproc.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/uio.h>

#include "chldinit.h"
#include "fmt.h"
#include "misc.h"
#include "../afmt.h"
#include "../crc32.h"

static bool do_write(
	struct Child           *self,
	const void             *buf,
	size_t                  buflen,
	size_t                  maxbuflen,
	const ddb_waveformat_t *fmt,
	SFMT_REQ                reqfmt)
{
	struct plug2host_process_samples req;
	struct iovec iov[2];

	req = (struct plug2host_process_samples){
		.code                 = P2H_PROCESS_SAMPLES,
		.buffer_size          = buflen,
		.response_max_bytes   = maxbuflen,
		.samplerate           = fmt->samplerate,
		.sampleformat_in      = sfmt_from_waveformat(fmt),
		.sampleformat_request = reqfmt,
		.channels             = fmt->channels,
		.datacksum            = crc32(buf, buflen),
	};
	req.hdrcksum = crc32(&req, sizeof(req));

	iov[0] = (struct iovec){
		.iov_base = &req,
		.iov_len  = sizeof(req),
	};
	iov[1] = (struct iovec){
		.iov_base = *(void **)(void *)&buf,
		.iov_len  = buflen,
	};

	if (!writev_full(self->fds[1], iov, 2))
	{
		fprintf(stderr, "%s(%d): writev_full: error %d\n",
		    __FILE__, __LINE__, errno);
		return false;
	}

	return true;
}

static bool do_read(
	struct Child     *self,
	void             *buf,
	size_t           *buflen_out,
	size_t            buflen_max,
	ddb_waveformat_t *fmt_out)
{
	struct host2plug_process_samples_response response;
	uint32_t hdrcksum;
	AFMT afmt;

	if (!read_full(self->fds[0], &response, sizeof(response)))
	{
		fprintf(stderr, "%s(%d): read_full: error %d\n",
		    __FILE__, __LINE__, errno);
		return false;
	}

	hdrcksum = response.hdrcksum;
	response.hdrcksum = 0;

	afmt = (AFMT){
		.sfmt = response.sampleformat,
		.ch   = response.channels,
		.rate = response.samplerate,
	};

	/* basics */

	if (response.code != H2P_PROCESS_SAMPLES_RESPONSE)
	{
		fprintf(stderr,
		    "%s(%d): unexpected message code 0x%" PRIx32 "\n",
		    __FILE__, __LINE__,
		    response.code);
		return false;
	}

	if (hdrcksum != crc32(&response, sizeof(response)))
	{
		fprintf(stderr, "%s(%d): header checksum failed\n",
		    __FILE__, __LINE__);
		return false;
	}

	if (!afmt_is_valid(&afmt))
	{
		fprintf(stderr, "%s(%d): bad audio format\n",
		    __FILE__, __LINE__);
		return false;
	}

	/* buffer size */

	if (response.buffer_size > buflen_max)
	{
		fprintf(stderr, "%s(%d): response does not fit\n",
		    __FILE__, __LINE__);
		return false;
	}

	if (response.buffer_size % afmt_frame_bytes(&afmt))
	{
		fprintf(stderr,
		    "%s(%d): buffer size not a multiple of frame"
		    " size\n",
		    __FILE__, __LINE__);
		return false;
	}

	/* read */

	if (!read_full(self->fds[0], buf, response.buffer_size))
	{
		fprintf(stderr, "%s(%d): read_full: error %d\n",
		    __FILE__, __LINE__,
		    errno);
		return false;
	}

	if (response.datacksum != crc32(buf, response.buffer_size))
	{
		fprintf(stderr, "%s(%d): data checksum failed\n",
		    __FILE__, __LINE__);
		return false;
	}

	/* apply format */

	if (afmt.ch != fmt_out->channels)
	{
		fmt_out->channelmask = (1<<response.channels)-1;
		/* mild todo: decide how to handle absurd channel
		   counts. */
		assert(fmt_out->channelmask);
	}

	if (!sfmt_apply_to_waveformat(fmt_out, afmt.sfmt))
		assert(0); /* previously checked valid. */
	fmt_out->channels = afmt.ch;
	fmt_out->samplerate = afmt.rate;

	*buflen_out = response.buffer_size;

	return true;
}

bool child_process_samples(
	struct Child     *self,
	void             *buf,
	size_t           *buflen_inout,
	size_t            buflen_max,
	ddb_waveformat_t *fmt_inout,
	SFMT_REQ          reqfmt)
{
	bool ok;

	if (self->config_changed)
	{
		self->config_changed = false;
		if (self->pid >= 0)
			child_stop(self);
	}

	if (self->pid < 0)
		child_start(self);

	ok = do_write(self, buf, *buflen_inout, buflen_max, fmt_inout, reqfmt);
	if (ok)
		ok = do_read(self, buf, buflen_inout, buflen_max, fmt_inout);

	if (!ok)
		child_stop(self);

	return ok;
}

bool child_inform_playback_interrupted(struct Child *self)
{
	ssize_t rv;
	int32_t msg;

	if (self->pid < 0)
		return true;

	msg = P2H_PLAYBACK_INTERRUPTED;
	rv = write(self->fds[1], &msg, sizeof(msg));
	if (rv < 0)
		fprintf(stderr, "%s: error %d\n", __func__, errno);
	else if (rv != sizeof(msg))
	{
		/* TODO: this should stop the child. figure out locking.
		   */
		fprintf(stderr, "%s: short write\n", __func__);
	}

	return true;
}
