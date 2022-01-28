module ddw.host.wndproc;

import core.stdc.stdio;

import core.sys.windows.winbase;
import core.sys.windows.windef;
import core.sys.windows.winuser;

import core.atomic;

import std.stdio : writefln;
import std.string : fromStringz;

import ddw.host.plugin;
import ddw.host.main;
import ddw.host.misc;
import ddw.host.plugproc;
import ddw.host.winamp;

/**
 * get the name of the plugin that's currently doing stuff
 * 
 * usually this is the one that sent the ipc message
 */
string get_lastplug()
{
	Plugin* pl = cast(Plugin*)procplug.atomicLoad();

	if (pl != null)
		return pl.opts.dllname;
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
	alias shm = globals.shm;
	switch (uMsg)
	{
		case WM_COPYDATA: // 0x004A
			writefln("warning: unsupported WM_COPYDATA: wParam=%s lParam=%s (%s)",
				wParam, lParam, LASTPLUG);
			break;

		case WM_WA_IPC: // WM_USER (0x0400)
			switch (lParam)
			{
				case IPC_GETOUTPUTTIME: // 105
					switch (wParam)
					{
						case 0: // position in ms of the currently playing track
							debug (spammy) writefln("GET shm.playback_pos_ms = %s (%s)", shm.playback_pos_ms, LASTPLUG);
							return shm.playback_pos_ms;

						case 1: // current track length in seconds
							writefln("GET shm.track_duration_ms = %s (%s)", shm.track_duration_ms, LASTPLUG);
							return shm.track_duration_ms/1000;

						case 2: // current track length in milliseconds
							writefln("GET shm.track_duration_ms = %s (%s)", shm.track_duration_ms, LASTPLUG);
							return shm.track_duration_ms;

						default:
							writefln("warning: unsupported IPC_GETOUTPUTTIME: wParam=%s lParam=%s (%s)",
								wParam, lParam, LASTPLUG);
							return -1;
					}

				case IPC_GETLISTPOS: // 125
					debug (spammy) writefln("GET shm.track_idx = %d (%s)", shm.track_idx, LASTPLUG);
					return shm.track_idx;

				case IPC_GETPLAYLISTTITLE: // 212
					debug (spammy) writefln("GET shm.track_title = \"%s\" (%s)", shm.track_title.ptr, LASTPLUG);
					return cast(LRESULT)shm.track_title.ptr;

				case IPC_GET_API_SERVICE: // 3025
					// supposed to return a pointer to some C++ abomination added in winamp 5.12
					return 1; // 1 = not supported

				case IPC_REGISTER_WINAMP_IPCMESSAGE: // 65536
					writefln("warning: unsupported WM_WA_IPC: wParam=\"%s\" lParam=%s (%s)",
						fromStringz(cast(char*)wParam), lParam, LASTPLUG);
					break;

				default:
					writefln("warning: unsupported WM_WA_IPC: wParam=%s lParam=%s (%s)",
						wParam, lParam, LASTPLUG);
			}
			break;

		case WM_WA_SYSTRAY: // WM_USER+1
			writefln("warning: unsupported WM_WA_SYSTRAY: wParam=%s lParam=%s (%s)",
				wParam, lParam, LASTPLUG);
			break;

		case WM_WA_MPEG_EOF: // WM_USER+2
			writefln("warning: unsupported WM_WA_MPEG_EOF: wParam=%s lParam=%s (%s)",
				wParam, lParam, LASTPLUG);
			break;

		case WM_COMMAND: // 0x0111
			writefln("warning: unsupported WM_COMMAND: wParam=%s lParam=%s (%s)",
				wParam, lParam, LASTPLUG);
			break;

		default:
			break;
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
