#pragma once

#include <string>
#include <vector>
#include <synchapi.h>

#include "../afmt.h"
#include "buf.hpp"
#include "fp_control.hpp"
#include "ipc_hooks.h"
#include "winamp.hpp"

struct PluginOpts
{
	/* which module index to load from the dll. */
	int module_idx;

	/* process_min_frames: the minimum number of audio frames to
	   call ModifySamples with. */
	/* process_max_frames: the maximum number of frames to pass to
	   ModifySamples. */
	/* process_frames_mult: ensure that the number of frames is a
	   multiple of this value. this is only meaningful if min_frames
	   and max_frames are different. */
	/* the purpose of these variables is to avoid hitting edge cases
	   in poorly written plugins. the downside is that they add
	   latency. for plugins that are known to behave well, the
	   limits can be lifted. */
	unsigned int process_min_frames = 576;
	unsigned int process_max_frames = 576;
	unsigned int process_frames_mult = 576;

	/* nostretch: allow processing code to assume this plugin
	   doesn't stretch sound. this can enable slight optimizations.
	   violations are detected and will exit the program. */
	bool nostretch;

	/* start with the plugin unloaded (don't automatically load its
	   dll.) */
	bool init_unload;

	/* start with the plugin skipped (= unchecked in the gui.) */
	bool want_skip_user;

	/* workaround_input_int32_overflow: when passing 32-bit integer
	   samples to the plugin, limit their positive max value to
	   0x7fffffbf. values above that convert to 1.0 in floating
	   point, which might cause problems for some plugins.

	   >>> numpy.float32(0x7fffffbf) / 0x80000000
	   np.float32(0.99999994)
	   >>> numpy.float32(0x7fffffbf+1) / 0x80000000
	   np.float32(1.0)
	   */
	bool workaround_input_int32_overflow;

	/* clears_internal_buffers_on_format_change: if set, use a
	   format change to clear the plugin's internal buffers.
	   this is a bit field:
	   - 0x1: change bit depth
	   - 0x2: change channel count
	   - 0x4: change sample rate
	   if more than one is set, then all of the specified properties
	   will be changed simultaneously. */
	/* this works by playing a short clip of silence with an audio
	   format different from the last one used. many plugins react
	   to this by clearing their internal buffers. */
	unsigned int clears_internal_buffers_on_format_change;
	/* num_input_frames_to_clear_internal_buffers: if set, feed the
	   plugin this many frames of silence to clear its internal
	   buffers. the value is rounded up following process_min_frames
	   and any other limits. */
	unsigned int num_input_frames_to_clear_internal_buffers;

	/* ipc_as_hwnd_parent: pass the invisible, IPC-only window as
	   the parent window to the plugin. this can be used to work
	   around undesirable behavior related to window management.
	   it might break plugins that try to position their own windows
	   around the main window. */
	/* null_as_hwnd_parent: pass NULL as the parent window. this is
	   a nuclear version of ipc_as_hwnd_parent. it will also prevent
	   the plugin from using IPC messages. */
	bool ipc_as_hwnd_parent;
	bool null_as_hwnd_parent;

	/* slowipc: skip the optimization that hooks SendMessage and
	   friends. normally, the functions are hooked so that we can
	   service winamp ipc messages on the same thread that sent
	   them, without having to back-and-forth between two threads.
	   */
	bool slowipc;

	/* WIP */
	/* ignore_output: treat this plugin as one that only needs to
	   read samples, not modify them. this has the following
	   effects:
	   - ModifySamples is passed a copy of the current audio, which
	     is thrown away after the call
	   - buffering done to satisfy procmin doesn't add latency to
	     the output
	   - sample conversion doesn't affect other plugins */
	bool ignore_output;
	/* status: works with some restrictions. any options that cause
	   buffering must be disabled. otherwise, there can still be
	   format conversions that affect other plugins. */

	bool required; /* exit instead of disabling on unsupported format */
	bool randbuf; /* randomize input buffer size on each call among supported values */

	std::vector<unsigned int> bits = {16};
	std::vector<unsigned int> ch = {2};
	std::vector<unsigned int> rate;

	/* meta info */
	std::wstring path;
	std::wstring dllname;
	std::vector<std::string> modules;
};

#define PLUGIN_OPTS_INIT \
	((struct PluginOpts){ \
		.process_min_frames = 576, \
		.process_max_frames = 576, \
		.process_frames_mult = 576, \
		.bits = {16}, \
		.ch = {2}, \
	})

struct Plugin
{
	struct Plugin *next;
	struct Plugin *prev;

	struct PluginOpts opts = PLUGIN_OPTS_INIT;

	HMODULE          dll;
	winampDSPHeader *header;
	winampDSPModule *module;
	/* protects dll, header and module. */
	/* locked exclusively when e.g. we're changing the module and
	   processing should be temporarily paused. */
	SRWLOCK          dll_lock;
	/* for unloaded plugins, dll, header and module are null. */

	Buf  buf;
	AFMT buf_fmt = AFMT_INVALID;

	/* info only: last audio format processed. */
	AFMT last_fmt = AFMT_INVALID;
	/* info only: last input audio format before any conversion. */
	AFMT last_oldfmt = AFMT_INVALID;

	/* skip processing due to incompatible format. */
	bool skip_fmt;
	/* skip processing due to being unchecked in the gui. */
	bool skip_user;

	/* set by gui if the position of this plugin was changed. this
	   tells proc to clear its buffers. */
	bool did_move;

	/* IB_* enum */
	/* assumed state of the plugin's internal buffers. this is
	   tracked so that we know when to clear them. see the IB_* enum
	   for details. */
	int internal_buffer_state;

	/* when clearing buffers: the number of silent frames left to
	   pass to this plugin. */
	size_t silent_frames_rem;

	/* floating point environemnt of main and processing thread. */
	struct fp_control fp_main;
	struct fp_control fp_process;

	/* in_call: tracks which call into plugin code we're currently
	   executing on the main thread. */
	/* TODO - currently this is just a fancy boolean. should either
	   turn it into one, or add something to make use of the enums
	   (check before changing, assert on unexpected value.) */
	int in_call;

	struct installed_hook_info hooks;
};

#define PLUGIN_INIT \
	((struct Plugin){ \
		.opts = PLUGIN_OPTS_INIT, \
		.buf_fmt = AFMT_INVALID, \
		.last_fmt = AFMT_INVALID, \
		.last_oldfmt = AFMT_INVALID, \
	})

/* possible values for Plugin.in_call. */
enum
{
	IN_NONE,
	IN_CONFIG,
	IN_CHMOD,
	IN_REMOVE,
	IN_UNLOAD,
	IN_RESURRECT
};

enum
{
	/* internal buffer clean. either nothing was played yet, or the
	   buffer was just cleared. */
	IB_CLEAN,
	/* internal buffer is dirty and the plugin is currently skipped.
	   in this case, the buffer should be cleared asap - and when
	   that's done, the enum should be set to IB_CLEAN. */
	IB_DIRTY_SKIPPED,
	/* internal buffer is dirty, and the plugin is currently active.
	   if this plugin becomes skipped, we know from this that the
	   buffer might need to be cleared. */
	IB_DIRTY_PLAYING
};

/* for gui, called with RW lock on plugin list. */
/* check if this plugin will probably process audio next time we get
   some. */
static inline bool may_process_audio(struct Plugin *pl)
{
	if (!pl->dll)
		return false;

	/* the next block can have a different format. */
	/*if (pl->skip_fmt)
		return false;*/

	if (pl->skip_user)
		return false;

	return true;
}
