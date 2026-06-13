#pragma once

#include <stdint.h>

#define PACKED __attribute__((__packed__))

/* request output in... */
enum
{
	SFMT_REQ_ANY       = 100, /* any format */
	SFMT_REQ_ANY_32BIT = 101, /* any 32-bit format (s32 or f32) */
	SFMT_REQ_ONLY_F32  = 102, /* f32 */
	SFMT_REQ_ONLY_S32  = 103  /* s32 */
};
typedef int SFMT_REQ;

enum
{
	P2H_PROCESS_SAMPLES          = 1000,
	H2P_PROCESS_SAMPLES_RESPONSE = 1001,
	/* sent when playback is interrupted (seek, stop, track
	   change...) */
	/* not sent for pauses - that alone doesn't cause a
	   discontinuity in playback. */
	P2H_PLAYBACK_INTERRUPTED     = 2000
};

struct PACKED plug2host_process_samples
{
	uint32_t code;
	uint64_t buffer_size; /* bytes */
	uint32_t sampleformat_in; /* SFMT enum */
	uint32_t channels;
	uint32_t samplerate;
	uint32_t sampleformat_request; /* SFMT_REQ enum */
	uint64_t response_max_bytes;
	uint32_t datacksum; /* crc32 of data */
	uint32_t hdrcksum; /* crc32 of this struct with hdrcksum=0 */
	/* data follows: char[.buffer_size] */
};

struct PACKED host2plug_process_samples_response
{
	uint32_t code;
	uint64_t buffer_size; /* bytes */
	uint32_t sampleformat; /* SFMT enum */
	uint32_t channels;
	uint32_t samplerate;
	uint32_t datacksum;
	uint32_t hdrcksum;
	/* data follows: char[.buffer_size] */
};

#undef PACKED
