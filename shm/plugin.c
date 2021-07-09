#include "plugin.h"

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>

#include "shm.h"
#include "tickmain.h"

DB_functions_t *deadbeef;
struct shmdata *shm;

static char shmname[64];

// -----------------------------------------------------------------------------

static int
shm_message(uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2) {
	switch (id) {
	case DB_EV_SONGSTARTED:
		shm->isplaying = ISPLAYING_PLAYING;
		tickthread_start_ticking();

		deadbeef->pl_lock();
		ddb_playlist_t *plt = deadbeef->plt_get_curr();
		DB_playItem_t *it = deadbeef->streamer_get_playing_track();
		if (it) {
			const char *v = deadbeef->pl_find_meta(it, "title") ?: "";
			snprintf(shm->track_title, sizeof(shm->track_title), "%s", v);

			shm->track_duration_ms = (int)(1000.0f*deadbeef->pl_get_item_duration(it));

			if (plt)
				shm->track_idx = deadbeef->plt_get_item_idx(plt, it, PL_MAIN);
		}
		if (it)
			deadbeef->pl_item_unref(it);
		if (plt)
			deadbeef->plt_unref(plt);
		deadbeef->pl_unlock();

		break;
	case DB_EV_STOP:
		shm->isplaying = ISPLAYING_NOTPLAYING;
		tickthread_stop_ticking();
		break;
	case DB_EV_PAUSED:
		if (p1) {
			shm->isplaying = ISPLAYING_PAUSED;
			tickthread_stop_ticking();
		} else {
			shm->isplaying = ISPLAYING_PLAYING;
			tickthread_start_ticking();
		}
		break;
	}

	return 0;
}

static int
shm_disconnect(void);

static int
shm_connect(void)
{
	snprintf(shmname, sizeof(shmname), "/dev/shm/deadbeef.%d", getpid());

	shm = shmnew(shmname, sizeof(struct shmdata));
	if (shm == NULL)
		goto err;

	setenv("DDW_SHM_NAME", shmname, 1);

	if (!tickthread_init())
		goto err;

	return 0;
err:
	shm_disconnect();

	return -1;
}

static int
shm_disconnect(void)
{
	tickthread_deinit();

	if (shm != NULL) {
		shmfree(shm, sizeof(struct shmdata));
		shm = NULL;
	}

	if (unlink(shmname) == -1 && errno != ENOENT)
		perror("ddb_shm: unlink");

	unsetenv("DDW_SHM_NAME");

	return 0;
}

static DB_misc_t plugin = {
	.plugin.api_vmajor = DB_API_VERSION_MAJOR,
	.plugin.api_vminor = DB_API_VERSION_MINOR,
	.plugin.type = DB_PLUGIN_MISC,
	.plugin.version_major = 1,
	.plugin.version_minor = 0,
	.plugin.id = "shm",
	.plugin.name = "Shared Memory",
	.plugin.descr = "Maintains a shared memory file in /dev/shm",
	.plugin.copyright = "human",
	.plugin.message = shm_message,
	.plugin.connect = shm_connect,
	.plugin.disconnect = shm_disconnect,
};

extern DB_plugin_t *
ddb_shm_load(DB_functions_t *ddb);

DB_plugin_t *
ddb_shm_load(DB_functions_t *ddb) {
	deadbeef = ddb;
	return DB_PLUGIN(&plugin);
}
