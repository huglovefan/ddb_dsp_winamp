#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "plugin.h"

extern HWND mainwin;
extern DWORD main_tid;

extern int in_fd;
extern int out_fd;

extern struct shmdata *shm;

#define MAX_PLUGINS 16
extern struct plugin plugins[MAX_PLUGINS];
extern unsigned int plugins_cnt;
extern _Atomic int procidx;
