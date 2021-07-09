#include "wndproc.h"

#include <stdio.h>

#include <Winamp/wa_ipc.h>

#include "main.h"
#include "misc.h"
#include "shm.h"

// slow and terrible
/*
#define IPC_FILE "/tmp/ddw_ipc.tmp"
static FILE *
deadbeef_ipc_real(const char *cmdline)
{
	if (0 != unlink(IPC_FILE) && errno != ENOENT) {
		perror("unlink");
		goto err;
	}
	if (0 != system(cmdline)) {
		perror("system");
		goto err;
	}
	FILE *f;
	Sleep( 10); if ((f = fopen(IPC_FILE, "rb")) != NULL) goto ok;
	Sleep( 10); if ((f = fopen(IPC_FILE, "rb")) != NULL) goto ok; // 20ms
	Sleep( 20); if ((f = fopen(IPC_FILE, "rb")) != NULL) goto ok; // 40ms
	Sleep( 20); if ((f = fopen(IPC_FILE, "rb")) != NULL) goto ok; // 60ms
	Sleep( 40); if ((f = fopen(IPC_FILE, "rb")) != NULL) goto ok; // 100ms
	Sleep(100); if ((f = fopen(IPC_FILE, "rb")) != NULL) goto ok; // 200ms
	Sleep(100); if ((f = fopen(IPC_FILE, "rb")) != NULL) goto ok; // 300ms
	Sleep(200); if ((f = fopen(IPC_FILE, "rb")) != NULL) goto ok; // 500ms
	Sleep(500); if ((f = fopen(IPC_FILE, "rb")) != NULL) goto ok; // 1000ms
	goto err;
ok:
	return f;
err:
	return NULL;
}
#define deadbeef_ipc(x) deadbeef_ipc_real("start /b /unix /bin/sh -c \"cd; deadbeef --" x " > " IPC_FILE " 2>/dev/null\"")
*/

static const char *
get_lastplug(void)
{
	int idx = procidx;

	if (idx >= 0 && (unsigned)idx < plugins_cnt)
		return superbasename(plugins[idx].opts.path);
	else
		return "?";
}
#define LASTPLUG get_lastplug()

LRESULT CALLBACK
WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	//
	// handle winamp IPC messages
	// see:
	// - wa_ipc.h in the SDK
	// - http://wiki.winamp.com/wiki/Basic_Plugin_Guide_-_Tutorial
	// - https://wiki.winehq.org/List_Of_Windows_Messages
	// - https://wiki.hydrogenaud.io/index.php?title=Foobar2000:Title_Formatting_Reference
	//
	switch (uMsg) {
	case WM_COPYDATA: // 0x004A
		fprintf(stderr, "warning: unsupported WM_COPYDATA: wParam=%d lParam=%ld (%s)\n",
		    wParam, lParam, LASTPLUG);
		break;
	case WM_WA_IPC: // WM_USER (0x0400)
		switch (lParam) {
		case IPC_GETOUTPUTTIME: // 105
			switch (wParam) {
			case 0: // position in ms of the currently playing track
//				fprintf(stderr, "GET shm->playback_pos_ms = %d (%s)\n", shm->playback_pos_ms, LASTPLUG);
				return shm->playback_pos_ms;
			case 1: // current track length in seconds
				fprintf(stderr, "GET shm->track_duration_ms = %d (%s)\n", shm->track_duration_ms, LASTPLUG);
				return shm->track_duration_ms/1000;
			case 2: // current track length in milliseconds
				fprintf(stderr, "GET shm->track_duration_ms = %d (%s)\n", shm->track_duration_ms, LASTPLUG);
				return shm->track_duration_ms;
			}
			fprintf(stderr, "warning: unsupported IPC_GETOUTPUTTIME: wParam=%d lParam=%ld (%s)\n",
			    wParam, lParam, LASTPLUG);
			return -1;
		case IPC_GETLISTPOS: // 125
//			fprintf(stderr, "GET shm->track_idx = %d (%s)\n", shm->track_idx, LASTPLUG);
			return shm->track_idx;
		case IPC_GETPLAYLISTTITLE: // 212
//			fprintf(stderr, "GET shm->track_title = \"%s\" (%s)\n", shm->track_title, LASTPLUG);
			return (uintptr_t)shm->track_title;
		case IPC_GET_API_SERVICE: // 3025
			// supposed to return a pointer to some C++ abomination added in winamp 5.12
			return 1; // 1 = not supported
		case IPC_REGISTER_WINAMP_IPCMESSAGE: // 65536
			fprintf(stderr, "warning: unsupported WM_WA_IPC: wParam=\"%s\" lParam=%ld (%s)\n",
			    (const char *)wParam, lParam, LASTPLUG);
			break;
		default:
			fprintf(stderr, "warning: unsupported WM_WA_IPC: wParam=%d lParam=%ld (%s)\n",
			    wParam, lParam, LASTPLUG);
		}
		break;
	case WM_WA_SYSTRAY: // WM_USER+1
		fprintf(stderr, "warning: unsupported WM_WA_SYSTRAY: wParam=%d lParam=%ld (%s)\n",
		    wParam, lParam, LASTPLUG);
		break;
	case WM_WA_MPEG_EOF: // WM_USER+2
		fprintf(stderr, "warning: unsupported WM_WA_MPEG_EOF: wParam=%d lParam=%ld (%s)\n",
		    wParam, lParam, LASTPLUG);
		break;
	case WM_COMMAND: // 0x0111
		fprintf(stderr, "warning: unsupported WM_COMMAND: wParam=%d lParam=%ld (%s)\n",
		    wParam, lParam, LASTPLUG);
		break;
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
