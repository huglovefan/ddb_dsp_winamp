module ddw.pipedata;

import core.stdc.stdint;

align (1) struct processing_request {
	uint64_t buffer_size; /* how many bytes are written after this header */
	uint32_t samplerate;
	uint8_t bitspersample;
	uint8_t channels;
}
static assert(processing_request.sizeof == (64+32+8+8)/8);

align (1) struct processing_response {
	uint64_t buffer_size; /* how many bytes are written after this header */
}
static assert(processing_response.sizeof == (64)/8);
