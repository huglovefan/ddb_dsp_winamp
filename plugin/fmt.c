#include "fmt.h"

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
