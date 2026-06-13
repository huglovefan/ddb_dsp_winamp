#pragma once

#include <pthread.h>
#include <deadbeef/deadbeef.h>
#include "child.h"

/* the name of the shm file is formed from this + pid in decimal. */
/* this string is assumed to be shell-safe. */
#define SHM_FILENAME_BASE "/dev/shm/deadbeef_wadsp."

#define CONFKEY_HOSTCMD   "wadsp.host_cmd"
#define CONFKEY_INTOUTPUT "wadsp.allow_int_output"

extern DB_functions_t *deadbeef;
extern struct Shm *shm;

struct Ddw
{
	/* must be the first member. */
	ddb_dsp_context_t ctx;

	struct Child host;
	char        *dll;

	char        *host_cmd;

	/* if set, we're in the middle of resetting playback (e.g.
	   track change) and dsp_process should skip processing while
	   this is the current track. */
	/* set by dsp_message at the earliest sign of a track change,
	   unset when it gets confirmation that the track change is
	   complete. this is done to avoid playing some old audio on
	   track change. */
	ddb_playItem_t *interrupted_trk;

	pthread_mutex_t lk;

	struct Ddw *prev;
	struct Ddw *next;
};

#define DDW_INIT \
	((struct Ddw){ \
		.host = CHILD_INIT, \
		.lk = PTHREAD_MUTEX_INITIALIZER, \
	})
