#include "fmt.h"

#include <assert.h>

size_t
fmt_frame_size(const ddb_waveformat_t *fmt)
{
	return fmt->channels*(fmt->bps>>3);
}

size_t
fmt_frames2bytes(const ddb_waveformat_t *fmt, int frames)
{
	return frames*fmt_frame_size(fmt);
}

size_t
fmt_bytes2frames(const ddb_waveformat_t *fmt, int frames)
{
	return frames/fmt_frame_size(fmt);
}

void
fmt_assert_reasonable(const ddb_waveformat_t *fmt)
{
	// 8, 16, 24 or 32
	assert(fmt->bps >= 8 && fmt->bps <= 32 && fmt->bps % 8 == 0);

	// alsa supports 1-8
	assert(fmt->channels >= 1 && fmt->channels <= 8);

	// 8000 = mp3 minimum
	// 192000 = biggest one i've seen
	assert(fmt->samplerate >= 8000 && fmt->samplerate <= 192000);

	// bitmask. see DDB_SPEAKER_* in deadbeef.h
	// currently 18 values are defined
	// there should be one set for each channel (probably)
	// pcm_convert tolerates and converts wrong values here (i think)
#define EIGHTEEN_ONES 0b111111111111111111
	assert((fmt->channelmask&EIGHTEEN_ONES) != 0);
	assert((fmt->channelmask&~EIGHTEEN_ONES) == 0);

	// pcm_convert assumes this is 0 or 1
	assert(fmt->is_float == 0 || fmt->is_float == 1);

	// samples are little-endian on wintel
	assert(fmt->is_bigendian == 0);

	// deadbeef.h: "bps must be 32 if this is true"
	if (fmt->is_float)
		assert(fmt->is_float ? fmt->bps == 32 : 1);
}
