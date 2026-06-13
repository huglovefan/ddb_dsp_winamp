#pragma once

#include <windef.h>
#include "../afmt.h"
#include "buf.hpp"
#include "plugin.hpp"

struct process_thread_vars
{
	struct plugin_list *plugins;
	DWORD mainThreadId;
	int in_fd = -1;
	int out_fd = -1;
	HWND hwnd_main;
};

struct process_stats
{
	double        input_dur_sec;
	double        output_dur_sec;
	AFMT          last_fmt;
	LARGE_INTEGER updated;
};

struct process_stats_iter_sub
{
	AFMT   fmt;
	size_t frames_in;
	size_t frames_out;
	size_t samples_clip_hi;
	size_t samples_clip_lo;
	double process_dur_sec;
};

unsigned long int WINAPI process_thread_entry(void *);

bool process_get_stats(process_stats *stats_out);
size_t process_get_iter_stats(process_stats_iter_sub *, size_t max);
bool process_stats_updated_since(LARGE_INTEGER *);
void process_reset_time_stats();
