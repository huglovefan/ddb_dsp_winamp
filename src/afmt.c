#include "afmt.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>

bool afmt_is_valid(const AFMT *fmt)
{
	if (!sfmt_is_valid(fmt->sfmt))
		return false;

	if (!(fmt->ch >= 1 && fmt->ch <= INT_MAX))
		return false;

	if (!(fmt->rate >= 1 && fmt->rate <= INT_MAX))
		return false;

	/* ch mult by sample size may not overflow */
	if (fmt->ch > SIZE_MAX/sfmt_bytes(fmt->sfmt))
		return false;

	return true;
}

bool afmt_equal(const AFMT *fmt1, const AFMT *fmt2)
{
	if (!afmt_is_valid(fmt1))
		return false;
	if (!afmt_is_valid(fmt2))
		return false;

	return (
		fmt1->sfmt == fmt2->sfmt &&
		fmt1->ch   == fmt2->ch &&
		fmt1->rate == fmt2->rate
	);
}

size_t afmt_frame_bytes(const AFMT *fmt)
{
	size_t sample_size;

	sample_size = sfmt_bytes(fmt->sfmt);

	assert(fmt->ch <= SIZE_MAX/sample_size);

	return (sample_size * fmt->ch);
}

size_t afmt_frame_bytes_n(const AFMT *fmt, size_t nframes)
{
	size_t frame_size;

	frame_size = afmt_frame_bytes(fmt);

	assert(nframes <= SIZE_MAX/frame_size);

	return (frame_size * nframes);
}

size_t afmt_buf_frames(const AFMT *fmt, size_t buflen)
{
	size_t frame_size;

	frame_size = afmt_frame_bytes(fmt);

	assert((buflen % frame_size) == 0);

	return (buflen / frame_size);
}

size_t afmt_buf_frames_round_down(const AFMT *fmt, size_t buflen)
{
	return (buflen / afmt_frame_bytes(fmt));
}

char *afmt_tostring(char *buf, size_t buflen, const AFMT *fmt)
{
	char nb[12];
	const char *n;

	n = sfmt_name(fmt->sfmt);
	if (!n)
	{
		snprintf(nb, sizeof(nb), "%d", fmt->sfmt);
		n = nb;
	}

	/* "s16 2ch 44100Hz" */
	/* "0 0ch 0Hz" */
	/* "-2147483648 4294967295ch 4294967295Hz" -> 37ch */

	snprintf(buf, buflen, "%s %uch %uHz",
	    n,
	    fmt->ch,
	    fmt->rate);

	return buf;
}
