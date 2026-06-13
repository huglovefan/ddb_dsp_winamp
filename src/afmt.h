#pragma once

#include <assert.h>
#include <stddef.h>
#include "sfmt.h"

#if defined(__cplusplus)
extern "C" {
#endif

struct audio_format
{
	SFMT         sfmt;
	unsigned int ch;
	unsigned int rate;
};
typedef struct audio_format AFMT;

#define AFMT_INVALID \
	((struct audio_format){ \
		.sfmt = SFMT_INVALID, \
	})

/* Test if the audio format is valid. */
bool afmt_is_valid(const AFMT *fmt);

/* Test if two audio formats are the same.
   If either of them is invalid, the result is false. */
bool afmt_equal(const AFMT *fmt1, const AFMT *fmt2);

/* Get the size in bytes of one frame.
   A frame is one sample for each channel. */
size_t afmt_frame_bytes(const AFMT *fmt);

/* Get the size in bytes of the specified count of frames. */
size_t afmt_frame_bytes_n(const AFMT *fmt, size_t nframes);

/* Convert a size in bytes to a count of frames in this audio format.
   The size is asserted to be a multiple of the frame size. */
size_t afmt_buf_frames(const AFMT *fmt, size_t buflen);

size_t afmt_buf_frames_round_down(const AFMT *fmt, size_t buflen);

/* Get a string representation of the audio format.
   Example: "s16 2ch 44100Hz".
   It's OK to call this even if the format isn't valid. */
char *afmt_tostring(char *buf, size_t buflen, const AFMT *fmt);

/* Buffer size that can hold a maximally long string representation of
   an AFMT. The size includes the null byte. */
enum { AFMT_STRBUF = 38 };

#if defined(__cplusplus)
}
#endif
