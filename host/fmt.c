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
	unsigned diff = 0;

	diff |= self->rate ^ other->rate;
	diff |= self->bps ^ other->bps;
	diff |= self->ch ^ other->ch;

	return diff == 0;
}

bool
fmt_makes_sense(const struct fmt *self)
{
	unsigned err = 0;

	err |= self->rate < 8000;
	err |= self->rate > 192000;

	// fixme: check 0
	err |= self->bps & 7; // not a multiple of 8
	err |= self->bps & (unsigned)~63; // bits above 32 set

	err |= self->ch < 1;
	err |= self->ch > 8;

	return err == 0;
}
