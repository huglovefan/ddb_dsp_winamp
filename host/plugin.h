#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <Winamp/DSP.H>

#include "buf.h"
#include "fmt.h"

struct plugin {
	winampDSPModule *module;

	// input from previous call that couldn't be processed yet (probably too
	//  little data for process_min_frames)
	struct buf buf;

	struct plugin_options {
#define MODULE_IDX_DEFAULT -1
		int module_idx;
		int process_min_frames;
		int process_max_frames;
		int process_frames_mult;
		int may_stretch;
		int doconf;
		int randomize;
		char *path;
	} opts;

	int random_cnt;
	size_t lastbufsz;

	HMODULE dll;
};

/// plugload.c

bool
parse_plugin_options(const char *arg, struct plugin_options *out);

bool
load_plugin(struct plugin *pl);

void
plugin_randomize_opts(struct plugin *pl);

/// plugproc.c

void
plugin_process(struct plugin *pl,
               struct fmt *fmt,
               struct buf *data,
               struct buf *tmp);
