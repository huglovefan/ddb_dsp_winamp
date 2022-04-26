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
	/// which module index to load from the dll
	/// default: -1 (try both 0 and 1)
	int module_idx = MODULE_IDX_DEFAULT;

	uint process_min_frames = 576;
	uint process_max_frames = 576;
	uint process_frames_mult = 576;

	bool nostretch = false; /// assume the plugin won't stretch sound
	bool noconf = false;    /// skip calling `Config()` for the plugin
	bool required = false;  /// exit instead of disabling on unsupported format

	const(char)[] path;    /// full path to dll
	const(char)[] dllname; /// filename of dll

	uint[] rate; /// set to limit supported sample rates (e.g. `[44100, 48000]`)
	uint[] bits; /// set to limit supported bit depths (e.g. `[16, 24]`)
	uint[] ch;   /// set to limit supported channel counts (e.g. `[1, 2]`)
}
