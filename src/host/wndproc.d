module ddw.host.wndproc;

import core.stdc.stdio;
import core.sys.windows.winbase;
import core.sys.windows.windef;
import core.sys.windows.winuser;
import core.atomic;
import ddw.host.plugin;
import ddw.host.main;
import ddw.host.plugproc;
import ddw.host.winamp;

//debug = spammy;

/**
 * window procedure of the "message-only window" used to receive winamp ipc messages
 */
extern(Windows)
LRESULT WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) nothrow
{
	//
	// handle winamp IPC messages
	// see:
	// - wa_ipc.h in the SDK
	// - http://wiki.winamp.com/wiki/Basic_Plugin_Guide_-_Tutorial
	// - https://wiki.winehq.org/List_Of_Windows_Messages
	//
	alias shm = globals.shm;

	if (uMsg == WM_COPYDATA) // 0x004A
	{
		printf("warning: unsupported WM_COPYDATA: wParam=%u lParam=%u (%s)\n",
			wParam, lParam, lastplug());
	}
	else if (uMsg == WM_WA_IPC) // WM_USER (0x0400)
	{
		if (lParam == IPC_GETOUTPUTTIME) // 105
		{
			if (wParam == 0) // position in ms of the currently playing track
			{
				debug(spammy) printf("GET shm.playback_pos_ms = %u (%s)\n",
					shm.playback_pos_ms, lastplug());
				return shm.playback_pos_ms;
			}
			else if (wParam == 1) // current track length in seconds
			{
				printf("GET shm.track_duration_ms = %u (%s)\n",
					shm.track_duration_ms, lastplug());
				return shm.track_duration_ms/1000;
			}
			else if (wParam == 2) // current track length in milliseconds
			{
				printf("GET shm.track_duration_ms = %u (%s)\n",
					shm.track_duration_ms, lastplug());
				return shm.track_duration_ms;
			}
			else
			{
				printf("warning: unsupported IPC_GETOUTPUTTIME: wParam=%u lParam=%u (%s)\n",
					wParam, lParam, lastplug());
				return -1;
			}
		}
		else if (lParam == IPC_GETLISTPOS) // 125
		{
			debug(spammy) printf("GET shm.track_idx = %d (%s)\n",
				shm.track_idx, lastplug());
			return shm.track_idx;
		}
		else if (lParam == IPC_GETPLAYLISTTITLE) // 212
		{
			debug(spammy) printf("GET shm.track_title = \"%s\" (%s)\n",
				shm.track_title.ptr, lastplug());
			return cast(LRESULT)shm.track_title.ptr;
		}
		else if (lParam == IPC_GET_API_SERVICE) // 3025
		{
			// supposed to return a pointer to some C++ abomination added in winamp 5.12
			printf("warning: unsupported WM_WA_IPC: wParam=%u lParam=IPC_GET_API_SERVICE (%s)\n",
				wParam, lastplug());
			return 1; // 1 = not supported
		}
		else if (lParam == IPC_REGISTER_WINAMP_IPCMESSAGE) // 65536
		{
			printf("warning: unsupported WM_WA_IPC: wParam=\"%s\" lParam=IPC_REGISTER_WINAMP_IPCMESSAGE (%s)\n",
				cast(char*)wParam, lastplug());
		}
		else
		{
			printf("warning: unsupported WM_WA_IPC: wParam=%u lParam=%u (%s)\n",
				wParam, lParam, lastplug());
		}
	}
	else if (uMsg == WM_WA_SYSTRAY) // WM_USER+1
	{
		printf("warning: unsupported WM_WA_SYSTRAY: wParam=%u lParam=%u (%s)\n",
			wParam, lParam, lastplug());
	}
	else if (uMsg == WM_WA_MPEG_EOF) // WM_USER+2
	{
		printf("warning: unsupported WM_WA_MPEG_EOF: wParam=%u lParam=%u (%s)\n",
			wParam, lParam, lastplug());
	}
	else if (uMsg == WM_COMMAND) // 0x0111
	{
		printf("warning: unsupported WM_COMMAND: wParam=%u lParam=%u (%s)\n",
			wParam, lParam, lastplug());
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// -----------------------------------------------------------------------------

private:

/**
 * get the name of the plugin that's currently doing stuff
 * 
 * usually this is the one that sent the ipc message
 */
const(char)* lastplug() nothrow
{
	Plugin* pl = cast(Plugin*)procplug.atomicLoad();

	if (pl != null)
		return pl.opts.dllname.ptr;
	else
		return "?";
}
