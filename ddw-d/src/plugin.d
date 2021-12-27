module ddw.plugin;

import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;
import ddw.child;
import ddw.chldinit;
import ddw.chldproc;
import ddw.fmt;
import ddw.zzx_deadbeef;

enum
{
	NEED_32BIT = 0b01,
	NEED_FLOAT = 0b10,
};

__gshared DB_functions_t* deadbeef;

__gshared bool have_patch1 = false;

//
// placeholder value to represent an empty string in dspconfig (it can't store empty strings)
//
enum DSPCONFIG_EMPTY_STRING = "-";

struct ddw2
{
	ddb_dsp_context_t ctx;
	Child host;
	char* dll;
	ushort max_bps;
}

// -----------------------------------------------------------------------------

int ddw_next_needs_conversion(ddw2* plugin, const(ddb_waveformat_t)* curfmt)
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

bool isbitdepth(const(char)* s)
{
	int l;

	if (
		s[l=0] != '\0' &&
		s[l=1] != '\0' &&
		s[l=2] != '\0')
	{
		return false;
	}

	switch (l)
	{
		case 0:
			return false;
		case 1:
			return memcmp(s, "0".ptr, 1) == 0 ||
				memcmp(s, "8".ptr, 1) == 0;
		case 2:
			return memcmp(s, "16".ptr, 2) == 0 ||
				memcmp(s, "24".ptr, 2) == 0 ||
				memcmp(s, "32".ptr, 2) == 0;
		default:
			assert(0);
	}
}

bool ddw_has_dll(ddw2* plugin)
{
	if (plugin.dll[0] == '\0')
		return false;

	if (strcmp(plugin.dll, DSPCONFIG_EMPTY_STRING) == 0)
		return false;

	if (isbitdepth(plugin.dll))
		return false;

	return true;
}

// -----------------------------------------------------------------------------

extern (C) ddb_dsp_context_t* dsp_winamp_open()
{
	ddw2* plugin;
	char* dll;

	plugin = cast(ddw2*)malloc(ddw2.sizeof);
	dll = strdup("");
	if (plugin == null || dll == null)
		goto failed;

	*plugin = ddw2.init;

	// D bug: https://issues.dlang.org/show_bug.cgi?id=22624
	// emergency initialization as .init is currently broken
	memset(plugin, 0, ddw2.sizeof);
	plugin.host.pid = -1;
	plugin.host.fds[0] = -1;
	plugin.host.fds[1] = -1;
	assert(plugin.host.pid == -1);
	assert(plugin.host.fds[0] == -1);
	assert(plugin.host.fds[1] == -1);
	assert(plugin.host.successes == 0);
	assert(plugin.host.failures == 0);
	assert(plugin.host.fatalerror == false);
	assert(plugin.host.pl == null);
	assert(plugin.dll == null);
	assert(plugin.max_bps == 0);

	// D bug: https://issues.dlang.org/show_bug.cgi?id=22623
	plugin.ctx.plugin = cast(typeof(plugin.ctx.plugin))cast(void*)cast(DB_dsp_s*)&plugindef;
	plugin.ctx.enabled = 1;

	plugin.dll = dll;
	plugin.max_bps = 16;
	plugin.host.pl = plugin;

	static assert(plugin.ctx.offsetof == 0);
	return &plugin.ctx;
failed:
	free(plugin);
	free(dll);
	return null;
}

extern (C) void dsp_winamp_close(ddb_dsp_context_t* ctx)
{
	ddw2* plugin = cast(ddw2*)ctx;

	child_stop(&plugin.host);

	free(plugin.dll);
	free(plugin);
}

extern (C) int dsp_winamp_process(
	ddb_dsp_context_t* ctx,
	float* samples,
	int frames,
	int maxframes,
	ddb_waveformat_t* fmt,
	float* ratio)
{
	ddb_waveformat_t nextfmt;
	ddw2* plugin = cast(ddw2*)ctx;
	size_t outcap;
	const(int) frames_in = frames;
	int convinfo;

	// guess the size in bytes of the output buffer
	// note: the maxframes value assumes 32-bit samples even if fmt says
	//  something else
	// this should be correct as long another dsp hasn't changed the number
	//  of channels
	outcap = maxframes*(32/8)*fmt.channels;

	nextfmt = *fmt;
	convinfo = ddw_next_needs_conversion(plugin, fmt);
	if (convinfo&NEED_32BIT)
		nextfmt.bps = 32;
	if (convinfo&NEED_FLOAT)
		nextfmt.is_float = 1;

	frames = child_process_samples(&plugin.host,
		fmt, &nextfmt,
		cast(char*)samples, frames,
		outcap);

	if (frames == -1)
	{
		// stop playback if we're epically failing
		if (child_is_doomed(&plugin.host))
		{
			child_stop(&plugin.host);
			deadbeef.log("dsp_winamp: plugin failed to start! check ~/.xsession-errors for errors or try running deadbeef from a terminal\n");
			deadbeef.get_output().pause();
			child_reset_failures(&plugin.host);
		}
	}

	// nothing came out
	if (frames <= 0)
	{
		frames = 0;
		*fmt = nextfmt;
	}

	assert(fmt_same(fmt, &nextfmt));
	assert(fmt_frames2bytes(fmt, frames) <= outcap);

	if (frames > 0)
		*ratio = (cast(float)frames_in)/(cast(float)frames);
	else
		*ratio = 0.0f;

	return frames;
}

//
// called when playback is stopped or un-stopped
//
extern (C) void dsp_winamp_reset(ddb_dsp_context_t* ctx)
{
	have_patch1 = !!deadbeef.conf_get_int("ddw.patch1", 0);
}

enum NUM_PARAMS = 2;

extern (C) int dsp_winamp_num_params()
{
	return NUM_PARAMS;
}

extern (C) const(char)* dsp_winamp_get_param_name(int p)
{
	switch (p)
	{
		case 0:
			return "Path to plugin";
		case 1:
			return "Max. bit depth";
		default:
			return "?";
	}
}

extern (C) void dsp_winamp_set_param(ddb_dsp_context_t* ctx, int p, const(char)* val)
{
	ddw2* plugin = cast(ddw2*)ctx;
	char* newdll;

	if (strcmp(val, DSPCONFIG_EMPTY_STRING) == 0)
		val = "";

	//
	// warn about too-long options
	// when parsing options from dspconfig, they're read to a 100-byte
	//  buffer so longer ones will get truncated (and also mess up later
	//  options)
	//
	if (strlen(val) > 99)
		deadbeef.log("dsp_winamp: warning: dsp options are limited to 99 characters\n");

	switch (p)
	{
		case 0:
			if (strcmp(val, plugin.dll) == 0)
				break;

			newdll = strdup(val);
			if (newdll == null)
				break;

			child_stop(&plugin.host);
			child_reset_failures(&plugin.host);

			free(plugin.dll);
			plugin.dll = newdll;

			break;

		case 1:
			plugin.max_bps = cast(ushort)atoi(val);
			if (!isbitdepth(val)) {
				deadbeef.log("dsp_winamp: invalid bit depth entered\n");
				plugin.max_bps = 16;
			}
			child_reset_failures(&plugin.host);
			break;

		default:
			// probably an earlier option was too long and messed up the rest
			fprintf(stderr, "dsp_winamp: tried to set nonexistent option index %d to \"%s\"\n", p, val);
			break;
	}
}

extern (C) void dsp_winamp_get_param(ddb_dsp_context_t* ctx, int p, char* str, int len)
{
	ddw2* plugin = cast(ddw2*)ctx;

	switch (p)
	{
		case 0:
			snprintf(str, len, "%s", plugin.dll);
			break;
		case 1:
			snprintf(str, len, "%u", plugin.max_bps);
			break;
		default:
			fprintf(stderr, "dsp_winamp: tried to get nonexistent option index %d\n", p);
			str[0] = '\0';
			break;
	}

	if (*str == '\0')
		snprintf(str, len, "%s", DSPCONFIG_EMPTY_STRING.ptr);
}

//
// returns true if there's nothing for dsp_winamp_process() to do
//
extern (C) int dsp_winamp_can_bypass(ddb_dsp_context_t* ctx, ddb_waveformat_t* fmt)
{
	ddw2* plugin = cast(ddw2*)ctx;
	int convinfo;

	// have some processing to do?
	if (ddw_has_dll(plugin))
		return false;

	// need to convert for the next dsp?
	convinfo = ddw_next_needs_conversion(plugin, fmt);
	if (convinfo != 0)
		return false;

	return true;
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
