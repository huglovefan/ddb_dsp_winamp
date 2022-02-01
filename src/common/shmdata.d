module ddw.common.shmdata;

enum ISPLAYING_PLAYING = 1;
enum ISPLAYING_PAUSED = 3;
enum ISPLAYING_NOTPLAYING = 2;

// https://www.geoffchappell.com/studies/windows/km/ntoskrnl/inc/api/ntexapi_x/kuser_shared_data/index.htm
align(1)
struct Shm
{
	int playback_pos_ms;

	char[512] track_title = 0;
	int track_duration_ms;
	int track_idx;

	int isplaying;
}
