module ddw.host.winamp;

import core.sys.windows.windef;
import core.sys.windows.winuser;

// -----------------------------------------------------------------------------

/*
 * dsp plugin interface from DSP.h
 */

struct winampDSPModule
{
	char* description;
	HWND hwndParent;
	HINSTANCE hDllInstance;
	extern(C) void function(winampDSPModule*) nothrow @nogc Config;
	extern(C) int function(winampDSPModule*) nothrow @nogc Init;
	extern(C) int function(winampDSPModule*, short* samples, int numsamples, int bps, int nch, int srate) nothrow @nogc ModifySamples;
	extern(C) void function(winampDSPModule*) nothrow @nogc Quit;
	void* userData;
}

struct winampDSPHeader
{
	int version_;
	char* description;
	extern(C) winampDSPModule* function(int) nothrow @nogc getModule;
}

alias extern(C) winampDSPHeader* function(HWND) nothrow @nogc winampDSPGetHeaderType;

// -----------------------------------------------------------------------------

/*
 * window message constants from wa_ipc.h
 */

enum WM_WA_IPC = WM_USER;
enum WM_WA_SYSTRAY = WM_USER+1;
enum WM_WA_MPEG_EOF = WM_USER+2;

enum IPC_GETOUTPUTTIME = 105;
enum IPC_GETLISTPOS = 125;
enum IPC_GETPLAYLISTTITLE = 212;
enum IPC_GET_API_SERVICE = 3025;
enum IPC_REGISTER_WINAMP_IPCMESSAGE = 65536;
