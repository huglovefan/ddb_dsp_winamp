#include "fmt.h"

size_t
fmt_frame_size(const struct fmt *self)
{
	return (self->bps>>3)*self->ch;
}

size_t
fmt_frames2bytes(const struct fmt *self, unsigned int frames)
{
	return frames*fmt_frame_size(self);
}

unsigned int
fmt_bytes2frames(const struct fmt *self, size_t bytes)
{
	return bytes/fmt_frame_size(self);
}

bool
fmt_same(const struct fmt *self, const struct fmt *other)
{
	return self->rate == other->rate &&
	    self->bps == other->bps &&
	    self->ch == other->ch;
}

bool
fmt_makes_sense(const struct fmt *self)
{
	return self->rate >= 8000 && self->rate <= 192000 &&
	    self->bps >= 8 && self->bps % 8 == 0 && self->bps <= 32 &&
	    self->ch >= 1 && self->ch <= 8;
}
