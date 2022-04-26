module ddw.common.pipedata;

struct processing_request
{
align(1):
	ulong buffer_size; /* how many bytes are written after this header */
	uint samplerate;
	ubyte bitspersample;
	ubyte channels;
}

struct processing_response
{
align(1):
	ulong buffer_size; /* how many bytes are written after this header */
}
