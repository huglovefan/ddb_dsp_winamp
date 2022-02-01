module ddw.plugin.main;

import core.stdc.stdio;
import core.stdc.string;
import core.sys.posix.stdlib;
import core.sys.posix.unistd;
import core.memory : GC;
import core.runtime : rt_init, rt_term;
import misclib.druntime.threadinit;
import ddw.common.gc;
import ddw.common.rtopts : rt_options;
import ddw.common.shmdata;
import ddw.plugin.child;
import ddw.plugin.chldinit;
import ddw.plugin.chldproc;
import ddw.plugin.deadbeef;
import ddw.plugin.fmt;
import ddw.plugin.shm;
import ddw.plugin.tickmain;

__gshared
{
	DB_functions_t* deadbeef;
	Shm* shm;
	char[32] shmname = 0;
}

struct Ddw
{
	ddb_dsp_context_t ctx;
	Child host;
	string dll;
	ushort max_bps;
}

bool ddw_has_dll(const(Ddw)* plugin)
{
	if (plugin.dll == "")
		return false;

	if (plugin.dll == DSPCONFIG_EMPTY_STRING)
		return false;

	return true;
}

// -----------------------------------------------------------------------------

private:

enum
{
	NEED_32BIT = 0b0001,
	NEED_FLOAT = 0b0010,
}

__gshared bool have_patch1 = false;

enum DSPCONFIG_EMPTY_STRING = "-";

// -----------------------------------------------------------------------------

int ddw_next_needs_conversion(Ddw* plugin, const(ddb_waveformat_t)* curfmt)
{
	ddb_dsp_context_t* nextctx = plugin.ctx.next;
	int rv = 0;

	if (nextctx != null)
	{
		// D bug: https://issues.dlang.org/show_bug.cgi?id=22623
		if (cast(void*)nextctx.plugin != cast(void*)&plugindef)
			rv |= NEED_32BIT|NEED_FLOAT;
	}
	else
	{
		if (!have_patch1)
			rv |= NEED_32BIT;
	}

	if (curfmt != null)
	{
		if (rv&NEED_32BIT && curfmt.bps == 32)
			rv &= ~NEED_32BIT;
		if (rv&NEED_FLOAT && curfmt.is_float)
			rv &= ~NEED_FLOAT;
	}

	return rv;
}

// -----------------------------------------------------------------------------

extern(C)
ddb_dsp_context_t* dsp_winamp_open()
{
	initForeignThread();
	Ddw* plugin = new Ddw;

	// D bug: https://issues.dlang.org/show_bug.cgi?id=22624
	// emergency initialization as .init is currently broken
	memset(plugin, 0, Ddw.sizeof);
	plugin.host.pid = -1;
	plugin.host.fds[0] = -1;
	plugin.host.fds[1] = -1;
	debug
	{
		assert(plugin.host.pid == -1);
		assert(plugin.host.fds[0] == -1);
		assert(plugin.host.fds[1] == -1);
		assert(plugin.host.successes == 0);
		assert(plugin.host.failures == 0);
		assert(plugin.host.pl == null);
		assert(plugin.dll == null);
		assert(plugin.max_bps == 0);
	}

	// D bug: https://issues.dlang.org/show_bug.cgi?id=22623
	plugin.ctx.plugin = cast(typeof(plugin.ctx.plugin))cast(void*)cast(DB_dsp_s*)&plugindef;
	plugin.ctx.enabled = 1;

	plugin.max_bps = 16;
	plugin.host.pl = plugin;

	GC.addRoot(plugin);
	static assert(plugin.ctx.offsetof == 0);
	return &plugin.ctx;
}

extern(C)
void dsp_winamp_close(ddb_dsp_context_t* ctx)
{
	initForeignThread();
	Ddw* plugin = cast(Ddw*)ctx;

	child_stop(&plugin.host);

	GC.removeRoot(plugin);
}

extern(C)
int dsp_winamp_process(
	ddb_dsp_context_t* ctx,
	float* samples_,
	int frames_in,
	int maxframes,
	ddb_waveformat_t* fmt,
	float* ratio)
{
	initForeignThread();
	Ddw* plugin = cast(Ddw*)ctx;

	const(void[]) inbuf = (cast(void*)samples_)[0..fmt_frames2bytes(fmt, frames_in)];
	void[] outbuf = (cast(void*)samples_)[0..(maxframes*(32/8)*fmt.channels)];

	ddb_waveformat_t wantfmt = *fmt;
	int convinfo = ddw_next_needs_conversion(plugin, fmt);
	if (convinfo&NEED_32BIT)
		wantfmt.bps = 32;
	if (convinfo&NEED_FLOAT)
		wantfmt.is_float = 1;

	update_tick();

	void[] plugoutbuf;
	try
	{
		plugoutbuf = child_process_samples(&plugin.host,
			inbuf, fmt,
			outbuf, &wantfmt);

		assert(plugoutbuf.ptr == outbuf.ptr);
		assert(plugoutbuf.length <= outbuf.length);
	}
	catch (Exception e)
	{
		string s = e.toString();
		printf("%.*s\n", cast(int)s.length, s.ptr);

		child_record_failure(&plugin.host);
		child_stop(&plugin.host);
	}

	uint frames_out = fmt_bytes2frames(&wantfmt, plugoutbuf.length);

	if (frames_out == 0)
	{
		if (child_is_doomed(&plugin.host))
		{
			child_stop(&plugin.host);
			child_reset_failures(&plugin.host);
			deadbeef.log("dsp_winamp: plugin failed to start! check ~/.xsession-errors for errors or try running deadbeef from a terminal\n");
			deadbeef.get_output().pause();
		}
	}

	*fmt = wantfmt;

	if (frames_out > 0)
		*ratio = cast(float)frames_in / cast(float)frames_out;
	else
		*ratio = 0.0;

	return cast(int)frames_out;
}

extern(C)
void dsp_winamp_reset(ddb_dsp_context_t* ctx)
{
	initForeignThread();
	have_patch1 = !!deadbeef.conf_get_int("ddw.patch1", 0);
}

struct Param
{
	string name;
	void function(Ddw*, string) set;
	void function(Ddw*, char[]) get;
}

static immutable Param[] params = [
	{
		name: "Path to plugin",
		set: (plugin, val)
		{
			if (val != plugin.dll)
			{
				child_stop(&plugin.host);
				child_reset_failures(&plugin.host);

				plugin.dll = val.dup;
			}
		},
		get: (plugin, buf)
		{
			snprintf(buf.ptr, buf.length, "%.*s",
				cast(int)plugin.dll.length, plugin.dll.ptr);
		},
	},
	{
		name: "Max. bit depth",
		set: (plugin, val)
		{
			switch (val)
			{
				case "8": plugin.max_bps = 8; break;
				case "16": plugin.max_bps = 16; break;
				case "24": plugin.max_bps = 24; break;
				case "32": plugin.max_bps = 32; break;
				default:
					deadbeef.log("dsp_winamp: invalid bit depth entered\n");
					plugin.max_bps = 16;
					break;
			}
			child_reset_failures(&plugin.host);
		},
		get: (plugin, buf)
		{
			snprintf(buf.ptr, buf.length, "%u", plugin.max_bps);
		},
	},
];

extern(C)
int dsp_winamp_num_params()
{
	initForeignThread();

	return cast(int)params.length;
}

extern(C)
const(char)* dsp_winamp_get_param_name(int p)
{
	initForeignThread();

	if (cast(uint)p < params.length)
		return params[p].name.ptr;
	else
		return "?";
}

extern(C)
void dsp_winamp_set_param(ddb_dsp_context_t* ctx, int p, const(char)* val_)
{
	initForeignThread();
	Ddw* plugin = cast(Ddw*)ctx;
	string val = gcstrdup(val_);

	if (val == DSPCONFIG_EMPTY_STRING)
		val = "";

	if (val.length > 99)
		deadbeef.log("dsp_winamp: warning: dsp options are limited to 99 characters\n");

	if (cast(uint)p < params.length)
		params[p].set(plugin, val);
	else
		printf("dsp_winamp: tried to set nonexistent option index %d to \"%s\"\n", p, val_);
}

extern(C)
void dsp_winamp_get_param(ddb_dsp_context_t* ctx, int p, char* str_, int len)
{
	initForeignThread();
	Ddw* plugin = cast(Ddw*)ctx;
	char[] buf = str_[0..len];

	if (len <= 0)
		return;

	if (cast(uint)p < params.length)
	{
		params[p].get(plugin, buf);

		if (buf[0] == 0)
			snprintf(buf.ptr, buf.length, "%s", DSPCONFIG_EMPTY_STRING.ptr);
	}
	else
	{
		printf("dsp_winamp: tried to get nonexistent option index %d\n", p);
		buf[0] = 0;
	}
}

extern(C)
int dsp_winamp_can_bypass(ddb_dsp_context_t* ctx, ddb_waveformat_t* fmt)
{
	initForeignThread();
	Ddw* plugin = cast(Ddw*)ctx;

	if (plugin.host.pid != -1)
		return false;

	if (ddw_has_dll(plugin))
		return false;

	int convinfo = ddw_next_needs_conversion(plugin, fmt);
	if (convinfo != 0)
		return false;

	return true;
}

extern(C)
int dsp_winamp_start()
{
	if (!rt_init()) return 1;

	// https://github.com/ldc-developers/ldc/issues/2782
	version(LDC)
	{
		import c_deadbeef :
			c_setvbuf = setvbuf,
			c_stdout = stdout;
		c_setvbuf(c_stdout, null, _IOLBF, 256);
	}
	else
		setvbuf(stdout, null, _IOLBF, 256);

	return 0;
}

extern(C)
int dsp_winamp_stop()
{
	initForeignThread();
	rt_term();
	return 0;
}

extern(C)
int dsp_winamp_connect()
{
	initForeignThread();

	snprintf(shmname.ptr, shmname.length, "/dev/shm/deadbeef.%d", getpid());

	shm = cast(Shm*)shmnew(shmname.ptr, Shm.sizeof);
	if (shm)
	{
		setenv("DDW_SHM_NAME", shmname.ptr, 1);
		tickthread_init();
	}

	return 0;
}

extern(C)
int dsp_winamp_disconnect()
{
	initForeignThread();

	tickthread_deinit();

	if (shm)
	{
		shmfree(shm, Shm.sizeof);
		shm = null;
		unlink(shmname.ptr);
		unsetenv("DDW_SHM_NAME");
	}

	return 0;
}

extern(C)
int dsp_winamp_message(uint id, uintptr_t ctx, uint p1, uint p2)
{
	initForeignThread();

	if (id == DB_EV_SONGSTARTED)
	{
		shm.isplaying = ISPLAYING_PLAYING;
		tickthread_unpause();

		deadbeef.pl_lock();
		ddb_playlist_t* pl = deadbeef.plt_get_curr();
		DB_playItem_t* trk = deadbeef.streamer_get_playing_track();
		if (trk)
		{
			const(char)* v = deadbeef.pl_find_meta(trk, "title");
			if (!v) v = "";
			snprintf(shm.track_title.ptr, shm.track_title.length, "%s", v);

			shm.track_duration_ms = cast(int)(1000.0f*deadbeef.pl_get_item_duration(trk));

			if (pl)
				shm.track_idx = deadbeef.plt_get_item_idx(pl, trk, PL_MAIN);
		}
		if (trk) deadbeef.pl_item_unref(trk);
		if (pl) deadbeef.plt_unref(pl);
		deadbeef.pl_unlock();
	}
	else if (id == DB_EV_STOP)
	{
		shm.isplaying = ISPLAYING_NOTPLAYING;
		tickthread_pause();
	}
	else if (id == DB_EV_PAUSED)
	{
		if (p1)
		{
			shm.isplaying = ISPLAYING_PAUSED;
			tickthread_pause();
		}
		else
		{
			shm.isplaying = ISPLAYING_PLAYING;
			tickthread_unpause();
		}
	}

	return 0;
}

__gshared DB_dsp_t plugindef = {
	plugin: {
		api_vmajor: 1,
		api_vminor: /* DDB_API_LEVEL */ 10,
		type: DB_PLUGIN_DSP,
		id: "dsp_winamp",
		name: "winamp dsp",
		descr: "",
		website: "https://github.com/huglovefan/ddb_dsp_winamp",
		configdialog:
			"property \"Host command\" entry ddw.host_cmd \"ddw_host.exe\";\n"~
			"property \"DSP plugin can return non-32bit samples\" checkbox ddw.patch1 0;\n",
		start: &dsp_winamp_start,
		stop: &dsp_winamp_stop,
		connect: &dsp_winamp_connect,
		disconnect: &dsp_winamp_disconnect,
		message: &dsp_winamp_message,
	},
	open: &dsp_winamp_open,
	close: &dsp_winamp_close,
	process: &dsp_winamp_process,
	reset: &dsp_winamp_reset,
	num_params: &dsp_winamp_num_params,
	get_param_name: &dsp_winamp_get_param_name,
	set_param: &dsp_winamp_set_param,
	get_param: &dsp_winamp_get_param,
	configdialog:
		"property \"Path to plugin\" entry 0 \"\";\n"~
		"property \"Max. bit depth\" entry 1 \"\";\n",
	can_bypass: &dsp_winamp_can_bypass,
};

extern(C)
DB_plugin_t* dsp_winamp_load(DB_functions_t* ddb)
{
	deadbeef = ddb;
	return &plugindef.plugin;
}
