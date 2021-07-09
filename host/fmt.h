#pragma once

#include <stdbool.h>
#include <stddef.h>

struct fmt {
	int rate;
	int bps;
	int ch;
};

size_t
fmt_frame_size(const struct fmt *self);

size_t
fmt_frames2bytes(const struct fmt *self, unsigned int frames);

unsigned int
fmt_bytes2frames(const struct fmt *self, size_t bytes);

bool
fmt_same(const struct fmt *self, const struct fmt *other);

bool
fmt_makes_sense(const struct fmt *self);
