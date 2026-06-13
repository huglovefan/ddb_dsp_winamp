#pragma once

#include <stdint.h>

enum
{
	SHM_PLSTATE_STOPPED,
	SHM_PLSTATE_PLAYING,
	SHM_PLSTATE_PAUSED
};

struct Shm
{
	int32_t playback_state; /* SHM_PLSTATE_* enum */
	float   playback_position; /* sec */
	float   track_duration; /* sec, -1.0f = stream */
	int32_t playlist_length; /* num items */
	int32_t playlist_position; /* playing track index (0+) */
	int32_t track_sample_rate; /* Hz */
	int32_t track_bitrate; /* kilobits per second */
	int32_t track_channel_count;
	char    track_file_path[260 * 4 + 1]; /* utf-8 */
	char    track_title[260 * 4 + 1]; /* utf-8 */
	int32_t player_shuffle; /* bool */
	int32_t player_repeat; /* bool */
};
