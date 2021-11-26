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
		int trace;

#define MODULE_IDX_DEFAULT -1
		int module_idx;
		unsigned process_min_frames;
		unsigned process_max_frames;
		unsigned process_frames_mult;
		int may_stretch;
		int doconf;
		int required;
		char *path;
		char *rate;
		char *bits;
		char *ch;
	} opts;

	int random_cnt;
	size_t lastbufsz;

	int skip;
	int didconf;

	HMODULE dll;
};

/// plugload.c

bool
parse_plugin_options(const char *arg, struct plugin_options *out);

bool
load_plugin(struct plugin *pl);

const char *
plugin_supports_format(struct plugin *pl, const struct fmt *fmt);

/// plugproc.c

extern struct plugin *_Atomic procplug; // the one currently doing ModifySamples()

void
plugin_process_all(const struct fmt *fmt, struct buf *data, struct buf *tmp);
