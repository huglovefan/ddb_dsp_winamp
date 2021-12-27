module ddw.host.wndproc;

version (Windows):

import core.stdc.stdio;

import core.sys.windows.winbase;
import core.sys.windows.windef;
import core.sys.windows.winuser;

import core.atomic;

import ddw.host.plugin;
import ddw.host.main;
import ddw.host.misc;
import ddw.host.plugproc;
import ddw.host.winamp;

nothrow:
@nogc:

/**
 * get the name of the plugin that's currently doing stuff
 * 
 * usually this is the one that sent the ipc message
 */
const(char)* get_lastplug()
{
	Plugin* pl = cast(Plugin*)procplug.atomicLoad();

	if (pl != null)
		return superbasename(pl.opts.path);
	else
		return "?";
}
alias LASTPLUG = get_lastplug;

//debug = spammy;

/**
 * window procedure of the "message-only window" used to receive winamp ipc messages
 */
extern (Windows) LRESULT WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	//
	// handle winamp IPC messages
	// see:
	// - wa_ipc.h in the SDK
	// - http://wiki.winamp.com/wiki/Basic_Plugin_Guide_-_Tutorial
	// - https://wiki.winehq.org/List_Of_Windows_Messages
	//
	// this file best viewed with narrow tabs
	//
	switch (uMsg)
	{
		case WM_COPYDATA: // 0x004A
			fprintf(stderr, "warning: unsupported WM_COPYDATA: wParam=%d lParam=%ld (%s)\n",
				wParam, lParam, LASTPLUG);
			break;

		case WM_WA_IPC: // WM_USER (0x0400)
			switch (lParam)
			{
				case IPC_GETOUTPUTTIME: // 105
					switch (wParam)
					{
						case 0: // position in ms of the currently playing track
							debug (spammy) fprintf(stderr, "GET shm.playback_pos_ms = %d (%s)\n", shm.playback_pos_ms, LASTPLUG);
							return shm.playback_pos_ms;

						case 1: // current track length in seconds
							fprintf(stderr, "GET shm.track_duration_ms = %d (%s)\n", shm.track_duration_ms, LASTPLUG);
							return shm.track_duration_ms/1000;

						case 2: // current track length in milliseconds
							fprintf(stderr, "GET shm.track_duration_ms = %d (%s)\n", shm.track_duration_ms, LASTPLUG);
							return shm.track_duration_ms;

						default:
							fprintf(stderr, "warning: unsupported IPC_GETOUTPUTTIME: wParam=%d lParam=%ld (%s)\n",
								wParam, lParam, LASTPLUG);
							return -1;
					}

				case IPC_GETLISTPOS: // 125
					debug (spammy) fprintf(stderr, "GET shm.track_idx = %d (%s)\n", shm.track_idx, LASTPLUG);
					return shm.track_idx;

				case IPC_GETPLAYLISTTITLE: // 212
					debug (spammy) fprintf(stderr, "GET shm.track_title = \"%s\" (%s)\n", shm.track_title.ptr, LASTPLUG);
					return cast(LRESULT)shm.track_title.ptr;

				case IPC_GET_API_SERVICE: // 3025
					// supposed to return a pointer to some C++ abomination added in winamp 5.12
					return 1; // 1 = not supported

				case IPC_REGISTER_WINAMP_IPCMESSAGE: // 65536
					fprintf(stderr, "warning: unsupported WM_WA_IPC: wParam=\"%s\" lParam=%ld (%s)\n",
						cast(const(char)*)wParam, lParam, LASTPLUG);
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

		default:
			break;
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
