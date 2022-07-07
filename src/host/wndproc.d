module ddw.host.wndproc;

import core.stdc.stdio;
import core.sys.windows.winbase;
import core.sys.windows.windef;
import core.sys.windows.winuser;
import core.atomic;
import ddw.common.shmdata;
import ddw.host.plugin;
import ddw.host.main;
import ddw.host.plugproc;
import ddw.host.winamp;

private
{
	static immutable IpcHandler[LPARAM] winampIpcHandlers;
	alias LRESULT function(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) nothrow IpcHandler;
}

// -----------------------------------------------------------------------------

extern(Windows)
LRESULT WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) nothrow
{
	/*
	 * handle all messages that are mentioned in wa_ipc.h
	 * print a warning for any unsupported ones so they can be implemented when needed
	 */

	switch (uMsg)
	{
		// 0x004A
		case WM_COPYDATA:
			printf("warning: unsupported WM_COPYDATA: wParam=%u lParam=%u (%s)\n",
				wParam, lParam, lastplug());
			break;

		// 0x0111
		case WM_COMMAND:
			printf("warning: unsupported WM_COMMAND: wParam=%u lParam=%u (%s)\n",
				wParam, lParam, lastplug());
			break;

		// WM_USER (0x0400)
		case WM_WA_IPC:
			if (auto handler = lParam in winampIpcHandlers)
				return (*handler)(hwnd, uMsg, wParam, lParam);
			else
			{
				printf("warning: unsupported WM_WA_IPC: wParam=%u lParam=%u (%s)\n",
					wParam, lParam, lastplug());
			}
			break;

		// WM_USER+1
		case WM_WA_SYSTRAY:
			printf("warning: unsupported WM_WA_SYSTRAY: wParam=%u lParam=%u (%s)\n",
				wParam, lParam, lastplug());
			break;

		// WM_USER+2
		case WM_WA_MPEG_EOF:
			printf("warning: unsupported WM_WA_MPEG_EOF: wParam=%u lParam=%u (%s)\n",
				wParam, lParam, lastplug());
			break;

		default:
			break;
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
	if (Plugin* pl = cast(Plugin*)procplug.atomicLoad())
		return pl.opts.dllname.ptr;
	else
		return "?";
}

// -----------------------------------------------------------------------------

/*
 * handlers for winamp IPC messages
 * 
 * for documentation, see "Winamp/wa_ipc.h" in the winamp SDK
 * 
 * wa_ipc.h says: if an ipc method isn't supported, the return value is 1
 */

//debug = spammy;

shared static this()
{
	alias shm = globals.shm;

	/*
	 * IPC_GETOUTPUTTIME (105)
	 */
	winampIpcHandlers[IPC_GETOUTPUTTIME] = (hwnd, uMsg, wParam, lParam)
	{
		switch (wParam)
		{
			// playback position in milliseconds
			case 0:
			{
				debug(spammy) printf("GET shm.playback_pos_ms = %u (%s)\n",
					shm.playback_pos_ms, lastplug());

				// "Will return -1 if Winamp is not playing."
				if (shm.isplaying == ISPLAYING_NOTPLAYING)
					return -1;

				return shm.playback_pos_ms;
			}

			// track duration in seconds
			case 1:
			{
				printf("GET shm.track_duration_ms = %u (%s)\n",
					shm.track_duration_ms, lastplug());

				return shm.track_duration_ms/1000;
			}

			// track duration in milliseconds
			case 2:
			{
				printf("GET shm.track_duration_ms = %u (%s)\n",
					shm.track_duration_ms, lastplug());

				return shm.track_duration_ms;
			}

			default:
			{
				printf("warning: unsupported IPC_GETOUTPUTTIME: wParam=%u lParam=%u (%s)\n",
					wParam, lParam, lastplug());

				// the other ones return -1 on error, do the same here
				return -1;
			}
		}
	};

	/*
	 * IPC_GETLISTPOS (125)
	 * 
	 * get the 0-based track index in the current playlist
	 */
	winampIpcHandlers[IPC_GETLISTPOS] = (hwnd, uMsg, wParam, lParam)
	{
		debug(spammy) printf("GET shm.track_idx = %d (%s)\n",
			shm.track_idx, lastplug());

		return shm.track_idx;
	};

	/*
	 * IPC_GETPLAYLISTTITLE (212)
	 * 
	 * get the title of a playlist entry
	 * 
	 * in:  wParam = index of playlist item (returned by IPC_GETLISTPOS)
	 * out: pointer to ansi-encoded track title, or null on error
	 * 
	 * bugs:
	 * - the string we return is actually utf-8
	 */
	winampIpcHandlers[IPC_GETPLAYLISTTITLE] = (hwnd, uMsg, wParam, lParam)
	{
		debug(spammy) printf("GET shm.track_title = \"%s\" (%s)\n",
			shm.track_title.ptr, lastplug());

		// not the current track? (we only have the title for that one)
		if (wParam != shm.track_idx)
		{
			printf("warning: unsupported IPC_GETPLAYLISTTITLE for track pos %d (current %d)\n",
				wParam, shm.track_idx);
			return 0;
		}

		return cast(LRESULT)shm.track_title.ptr;
	};

	/*
	 * IPC_GET_API_SERVICE (3025)
	 * 
	 * get a pointer to some C++ abomination (since winamp 5.12)
	 * 
	 * nothing to do here besides warn that the plugin probably won't work if
	 *  it depends on this
	 * 
	 * if a plugin needs this, then maybe try looking for a version from before
	 *  winamp 5.12 was released (on 2005-12-09)
	 */
	winampIpcHandlers[IPC_GET_API_SERVICE] = (hwnd, uMsg, wParam, lParam)
	{
		printf("warning: unsupported WM_WA_IPC: wParam=%u lParam=IPC_GET_API_SERVICE (%s)\n",
			wParam, lastplug());

		return 1;
	};

	/*
	 * IPC_REGISTER_WINAMP_IPCMESSAGE (65536)
	 * 
	 * not supported, but define the callback to print `wParam` as a string
	 */
	winampIpcHandlers[IPC_REGISTER_WINAMP_IPCMESSAGE] = (hwnd, uMsg, wParam, lParam)
	{
		printf("warning: unsupported WM_WA_IPC: wParam=\"%s\" lParam=IPC_REGISTER_WINAMP_IPCMESSAGE (%s)\n",
			cast(char*)wParam, lastplug());

		return 1;
	};

	// optimize lookup
	winampIpcHandlers.rehash();
}
