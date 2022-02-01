module ddw.common.pipedata;

align(1)
struct processing_request
{
	ulong buffer_size; /* how many bytes are written after this header */
	uint samplerate;
	ubyte bitspersample;
	ubyte channels;
}
static assert(processing_request.sizeof == (64+32+8+8)/8);

align(1)
struct processing_response
{
	ulong buffer_size; /* how many bytes are written after this header */
}
static assert(processing_response.sizeof == (64)/8);
