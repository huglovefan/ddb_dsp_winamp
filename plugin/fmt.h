#pragma once

#include <stddef.h>

#include <deadbeef/deadbeef.h>

size_t
fmt_frame_size(const ddb_waveformat_t *fmt);

size_t
fmt_frames2bytes(const ddb_waveformat_t *fmt, int frames);

size_t
fmt_bytes2frames(const ddb_waveformat_t *fmt, int frames);
