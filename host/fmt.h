#pragma once

#include <stdbool.h>
#include <stddef.h>

struct fmt {
	unsigned rate;
	unsigned bps;
	unsigned ch;
};

__attribute__((pure))
size_t
fmt_frame_size(const struct fmt *self);

__attribute__((pure))
size_t
fmt_frames2bytes(const struct fmt *self, unsigned int frames);

__attribute__((pure))
unsigned int
fmt_bytes2frames(const struct fmt *self, size_t bytes);

__attribute__((pure))
bool
fmt_same(const struct fmt *self, const struct fmt *other);

__attribute__((pure))
bool
fmt_makes_sense(const struct fmt *self);
