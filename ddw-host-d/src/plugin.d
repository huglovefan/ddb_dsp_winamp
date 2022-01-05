module ddw.host.plugin;

import core.sys.windows.windef;

import ddw.host.buf;
import ddw.host.winamp;

struct Plugin
{
	winampDSPModule* module_;
	Buf buf;
	PluginOpts opts;
	bool skip; /// true if this plugin should be skipped when processing (incompatible format)

	bool confdone;

	HMODULE dll;
}

enum MODULE_IDX_DEFAULT = -1;

// the default for process_max_frames is 576 to match the buffer size winamp uses
// (something to do with mp3 decoding)

struct PluginOpts
{
	int module_idx = MODULE_IDX_DEFAULT;
	uint process_min_frames = 576;
	uint process_max_frames = 576;
	uint process_frames_mult = 576;

	bool nostretch = false;
	bool noconf = false;
	bool required = false;

	string path;
	string dllname;

	string rate;
	string bits;
	string ch;
}
