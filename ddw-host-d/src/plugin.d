module ddw.host.plugin;

import core.sys.windows.windef;

import ddw.host.buf;
import ddw.host.winamp;

struct Plugin
{
	winampDSPModule* module_;
	Buf buf;
	PluginOpts opts;
	int skip; /// true if this plugin should be skipped when processing (incompatible format)

	bool confdone;
	size_t lastbufsz;

	void* dll;
}

enum MODULE_IDX_DEFAULT = -1;

struct PluginOpts
{
	int trace;

	int module_idx;
	uint process_min_frames;
	uint process_max_frames;
	uint process_frames_mult;
	int may_stretch;
	int doconf;
	int required;
	char* path;
	char* rate;
	char* bits;
	char* ch;
}
