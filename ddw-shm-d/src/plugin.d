module ddw.shm.plugin;

import core.stdc.errno;
import core.stdc.stdint;
import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;
import core.sys.posix.stdlib;
import core.sys.posix.unistd;

import ddw.shmdata;
import ddw.shm.shm;
import ddw.shm.tickmain;
import ddw.shm.zzx_deadbeef;

__gshared DB_functions_t *deadbeef;
__gshared Shm *shm;

__gshared char[64] shmname = '\0';

// -----------------------------------------------------------------------------

extern (C) int shm_message(uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2)
{
	switch (id)
	{
		case DB_EV_SONGSTARTED:
			shm.isplaying = ISPLAYING_PLAYING;
			tickthread_start_ticking();

			deadbeef.pl_lock();
			scope (exit)
				deadbeef.pl_unlock();

			ddb_playlist_t* plt = deadbeef.plt_get_curr();
			scope (exit)
			{
				if (plt)
					deadbeef.plt_unref(plt);
			}

			DB_playItem_t* it = deadbeef.streamer_get_playing_track();
			scope (exit)
			{
				if (it)
					deadbeef.pl_item_unref(it);
			}

			if (it)
			{
				const(char)* v = deadbeef.pl_find_meta(it, "title");
				if (!v) v = "";
				snprintf(shm.track_title.ptr, shm.track_title.length, "%s", v);

				shm.track_duration_ms = cast(int)(1000.0f*deadbeef.pl_get_item_duration(it));

				if (plt)
					shm.track_idx = deadbeef.plt_get_item_idx(plt, it, PL_MAIN);
			}
			break;

		case DB_EV_STOP:
			shm.isplaying = ISPLAYING_NOTPLAYING;
			tickthread_stop_ticking();
			break;

		case DB_EV_PAUSED:
			if (p1)
			{
				shm.isplaying = ISPLAYING_PAUSED;
				tickthread_stop_ticking();
			}
			else
			{
				shm.isplaying = ISPLAYING_PLAYING;
				tickthread_start_ticking();
			}
			break;

		default:
			break;
	}

	return 0;
}

extern (C) int shm_connect()
{
	snprintf(shmname.ptr, shmname.length, "/dev/shm/deadbeef.%d", getpid());

	shm = cast(Shm*)shmnew(shmname.ptr, Shm.sizeof);
	if (shm == null)
		goto err;

	setenv("DDW_SHM_NAME", shmname.ptr, 1);

	if (!tickthread_init())
		goto err;

	return 0;
err:
	shm_disconnect();

	return -1;
}

extern (C) int shm_disconnect()
{
	tickthread_deinit();

	if (shm != null)
	{
		shmfree(shm, Shm.sizeof);
		shm = null;
	}

	if (unlink(shmname.ptr) == -1 && errno != ENOENT)
		perror("ddb_shm: unlink");

	unsetenv("DDW_SHM_NAME");

	return 0;
}

static DB_misc_t plugin = {
	plugin: {
		api_vmajor: 1,
		api_vminor: /* DDB_API_LEVEL */ 10,
		type: DB_PLUGIN_MISC,
		version_major: 1,
		version_minor: 0,
		id: "shm",
		name: "Shared Memory",
		descr: "Maintains a shared memory file in /dev/shm",
		copyright: "human",
		message: &shm_message,
		connect: &shm_connect,
		disconnect: &shm_disconnect,
	},
};

extern (C) DB_plugin_t* ddb_shm_load(DB_functions_t* ddb)
{
	deadbeef = ddb;
	return &plugin.plugin;
}
