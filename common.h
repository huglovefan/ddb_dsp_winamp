#include <stdint.h>

struct __attribute__((__packed__)) processing_request {
	uint64_t buffer_size; /* how many bytes are written after this header */
	uint32_t samplerate;
	uint32_t bitspersample;
	uint8_t channels;
};

struct __attribute__((__packed__)) processing_response {
	uint64_t buffer_size; /* how many bytes are written after this header */
};
