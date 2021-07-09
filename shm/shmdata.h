#pragma once

#include <stdint.h>

// https://www.geoffchappell.com/studies/windows/km/ntoskrnl/inc/api/ntexapi_x/kuser_shared_data/index.htm
struct __attribute__((packed)) shmdata {
	int32_t playback_pos_ms;

	char track_title[512];
	int32_t track_duration_ms;
	int32_t track_idx;

#define ISPLAYING_PLAYING 1
#define ISPLAYING_PAUSED 3
#define ISPLAYING_NOTPLAYING 2
	int32_t isplaying;
};
