module ddw.shm.plugin;

import core.stdc.stdio;
import core.sys.posix.unistd;
import core.runtime : rt_init, rt_term;
import std.exception;
import std.process;
import std.string;
import misclib.druntime.threadinit;
import ddw.shmdata;
import ddw.shm.shm;
import ddw.shm.tickmain;
import ddw.shm.zzx_deadbeef;

__gshared DB_functions_t *deadbeef;
__gshared Shm *shm;

// -----------------------------------------------------------------------------

private:

__gshared string shmname;

extern (C) int shm_message(uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2)
{
	initForeignThread();

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
	initForeignThread();
	shmname = format!"/dev/shm/deadbeef.%s"(getpid());
	shm = cast(Shm*)shmnew(shmname, Shm.sizeof);
	environment["DDW_SHM_NAME"] = shmname;
	tickthread_init();
	return 0;
}

extern (C) int shm_disconnect()
{
	initForeignThread();
	tickthread_deinit();
	shmfree(shm, Shm.sizeof);
	errnoEnforce(unlink(shmname.ptr) == 0);
	environment.remove("DDW_SHM_NAME");
	return 0;
}

extern (C) int shm_start()
{
	rt_init();
	return 0;
}

extern (C) int shm_stop()
{
	rt_term();
	return 0;
}

__gshared DB_misc_t plugin = {
	plugin: {
		api_vmajor: 1,
		api_vminor: /* DDB_API_LEVEL */ 10,
		type: DB_PLUGIN_MISC,
		version_major: 1,
		version_minor: 0,
		id: "shm",
		name: "Shared Memory",
		message: &shm_message,
		connect: &shm_connect,
		disconnect: &shm_disconnect,
		start: &shm_start,
		stop: &shm_stop,
	},
};

extern (C) DB_plugin_t* ddb_shm_load(DB_functions_t* ddb)
{
	deadbeef = ddb;
	return &plugin.plugin;
}
