#include "fmt.h"

#include <assert.h>

static size_t waveformat_frame_bytes(const ddb_waveformat_t *fmt)
{
	assert(fmt->bps > 0 && (fmt->bps % 8) == 0);
	assert(fmt->channels > 0
	    && fmt->channels <= SIZE_MAX/(fmt->bps / 8));

	return fmt->channels * (fmt->bps / 8);
}

size_t waveformat_frame_bytes_n(
	const ddb_waveformat_t *fmt,
	size_t                  frames)
{
	size_t fs;

	fs = waveformat_frame_bytes(fmt);
	assert(frames <= SIZE_MAX/fs);

	return frames * fs;
}

size_t waveformat_buf_frames(const ddb_waveformat_t *fmt, size_t bytes)
{
	size_t fs;

	fs = waveformat_frame_bytes(fmt);
	assert((bytes % fs) == 0);

	return bytes / fs;
}

bool waveformat_is_valid(const ddb_waveformat_t *fmt)
{
	if (!(
	    fmt->bps == 8 ||
	    fmt->bps == 16 ||
	    fmt->bps == 24 ||
	    fmt->bps == 32))
		return false;

	if (!(fmt->channels >= 1 && fmt->channels <= 8))
		return false;

	if (!(fmt->samplerate >= 8000 && fmt->channels <= 192000))
		return false;

	/* this bit field has 18 values defined.
	   bits above that shouldn't be set. */
	if ((fmt->channelmask >> 18) != 0)
		return false;

	/* channel count equals number of set bits */
	//~ if (std::popcount(fmt->channelmask) != fmt->channels)
		//~ return false;

	/* boolean stored as int */
	if (!(fmt->is_float == 0 || fmt->is_float == 1))
		return false;

#if DDB_API_LEVEL >= 17
	/* dsd over pcm, aka not pcm. wikipedia: "A DoP stream is
	   designed to sound like low-volume noise when played back by a
	   PCM-only DAC [...]" */
	if (fmt->flags & DDB_WAVEFORMAT_FLAG_IS_DOP)
		return false;
#else
	if (fmt->is_bigendian)
		return false;
#endif

	if (fmt->is_float)
	{
		if (fmt->bps != 32)
			return false;
	}

	return true;
}

SFMT sfmt_from_waveformat(const ddb_waveformat_t *wf)
{
#if DDB_API_LEVEL >= 17
	if (wf->flags & DDB_WAVEFORMAT_FLAG_IS_DOP)
		return SFMT_INVALID;
#else
	if (wf->is_bigendian)
		return SFMT_INVALID;
#endif

	if (wf->is_float)
		if (wf->bps == 32)
			return SFMT_F32;
		else
			return SFMT_INVALID;
	else
		switch (wf->bps)
		{
		case 8:  return SFMT_S8;
		case 16: return SFMT_S16;
		case 24: return SFMT_S24;
		case 32: return SFMT_S32;
		default: return SFMT_INVALID;
		}
}

bool sfmt_apply_to_waveformat(ddb_waveformat_t *wf, SFMT fmt)
{
	if (!sfmt_is_valid(fmt))
		return false;

	wf->bps = sfmt_bytes(fmt)*8;
	wf->is_float = sfmt_is_float(fmt);

	return true;
}
