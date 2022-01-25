module ddw.plugin.main;

import core.stdc.stdio : snprintf;
import core.stdc.stdlib;
import core.stdc.string;
import core.memory : GC;
import core.runtime : rt_init, rt_term;
import std.conv;
import std.string;
import std.stdio : _IOLBF, stdout, writefln, writeln;
import misclib.druntime.threadinit;
import ddw.plugin.child;
import ddw.plugin.chldinit;
import ddw.plugin.chldproc;
import ddw.plugin.fmt;
import ddw.plugin.deadbeef;

struct Ddw
{
	ddb_dsp_context_t ctx;
	Child host;
	string dll;
	ushort max_bps;
}

__gshared DB_functions_t* deadbeef;

bool ddw_has_dll(const(Ddw)* plugin)
{
	if (plugin.dll == "")
		return false;

	if (plugin.dll == DSPCONFIG_EMPTY_STRING)
		return false;

	if (isbitdepth(plugin.dll))
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

bool isbitdepth(string s)
{
	switch (s)
	{
		case "8":
		case "16":
		case "24":
		case "32":
			return true;
		default:
			return false;
	}
}

// -----------------------------------------------------------------------------

extern (C) ddb_dsp_context_t* dsp_winamp_open()
{
	initForeignThread();
	Ddw* plugin = new Ddw;

	// D bug: https://issues.dlang.org/show_bug.cgi?id=22624
	// emergency initialization as .init is currently broken
	memset(plugin, 0, Ddw.sizeof);
	plugin.host.pid = -1;
	plugin.host.fds[0] = -1;
	plugin.host.fds[1] = -1;
	assert(plugin.host.pid == -1);
	assert(plugin.host.fds[0] == -1);
	assert(plugin.host.fds[1] == -1);
	assert(plugin.host.successes == 0);
	assert(plugin.host.failures == 0);
	assert(plugin.host.pl == null);
	assert(plugin.dll == null);
	assert(plugin.max_bps == 0);

	// D bug: https://issues.dlang.org/show_bug.cgi?id=22623
	plugin.ctx.plugin = cast(typeof(plugin.ctx.plugin))cast(void*)cast(DB_dsp_s*)&plugindef;
	plugin.ctx.enabled = 1;

	plugin.max_bps = 16;
	plugin.host.pl = plugin;

	stdout.setvbuf(256, _IOLBF);

	GC.addRoot(plugin);
	static assert(plugin.ctx.offsetof == 0);
	return &plugin.ctx;
}

extern (C) void dsp_winamp_close(ddb_dsp_context_t* ctx)
{
	initForeignThread();
	Ddw* plugin = cast(Ddw*)ctx;

	child_stop(&plugin.host);

	GC.removeRoot(plugin);
}

extern (C) int dsp_winamp_process(
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
		writeln(e);
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

extern (C) void dsp_winamp_reset(ddb_dsp_context_t* ctx)
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
			snprintf(buf.ptr, buf.length, "%s", plugin.dll.toStringz);
		},
	},
	{
		name: "Max. bit depth",
		set: (plugin, val)
		{
			if (isbitdepth(val))
			{
				plugin.max_bps = val.to!ushort;
			}
			else
			{
				deadbeef.log("dsp_winamp: invalid bit depth entered\n");
				plugin.max_bps = 16;
			}
			child_reset_failures(&plugin.host);
		},
		get: (plugin, buf)
		{
			snprintf(buf.ptr, buf.length, "%u", plugin.max_bps);
		},
	},
];

extern (C) int dsp_winamp_num_params()
{
	initForeignThread();

	return cast(int)params.length;
}

extern (C) const(char)* dsp_winamp_get_param_name(int p)
{
	initForeignThread();

	if (cast(uint)p < params.length)
		return params[p].name.ptr;
	else
		return "?";
}

extern (C) void dsp_winamp_set_param(ddb_dsp_context_t* ctx, int p, const(char)* val_)
{
	initForeignThread();
	Ddw* plugin = cast(Ddw*)ctx;
	string val = cast(string)val_.fromStringz;

	if (val == DSPCONFIG_EMPTY_STRING)
		val = "";

	if (val.length > 99)
		deadbeef.log("dsp_winamp: warning: dsp options are limited to 99 characters\n");

	if (cast(uint)p < params.length)
		params[p].set(plugin, val);
	else
		writefln("dsp_winamp: tried to set nonexistent option index %s to \"%s\"", p, val);
}

extern (C) void dsp_winamp_get_param(ddb_dsp_context_t* ctx, int p, char* str_, int len)
{
	initForeignThread();
	Ddw* plugin = cast(Ddw*)ctx;
	char[] buf = str_[0..len];

	if (len <= 0)
		return;

	if (cast(uint)p < params.length)
	{
		params[p].get(plugin, buf);

		if (buf[0] == '\0')
			snprintf(buf.ptr, buf.length, "%s", DSPCONFIG_EMPTY_STRING.ptr);
	}
	else
	{
		writefln("dsp_winamp: tried to get nonexistent option index %s", p);
		buf[0] = '\0';
	}
}

extern (C) int dsp_winamp_can_bypass(ddb_dsp_context_t* ctx, ddb_waveformat_t* fmt)
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

extern (C) int dsp_winamp_start()
{
	rt_init();
	return 0;
}

extern (C) int dsp_winamp_stop()
{
	rt_term();
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

extern (C) DB_plugin_t* dsp_winamp_load(DB_functions_t* ddb)
{
	deadbeef = ddb;
	return &plugindef.plugin;
}
