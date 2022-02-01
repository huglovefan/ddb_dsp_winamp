module ddw.host.winamp;

extern(C):

import core.sys.windows.windef;
import core.sys.windows.winuser;

// -----------------------------------------------------------------------------

struct winampDSPModule
{
	char* description;
	HWND hwndParent;
	HINSTANCE hDllInstance;
	void function(winampDSPModule*) nothrow @nogc Config;
	int function(winampDSPModule*) nothrow @nogc Init;
	int function(winampDSPModule*, short* samples, int numsamples, int bps, int nch, int srate) nothrow @nogc ModifySamples;
	void function(winampDSPModule*) nothrow @nogc Quit;
	void* userData;
}

struct winampDSPHeader
{
	int version_;
	char* description;
	winampDSPModule* function(int) nothrow @nogc getModule;
}

alias winampDSPHeader* function(HWND) nothrow @nogc winampDSPGetHeaderType;

// -----------------------------------------------------------------------------

enum WM_WA_IPC = WM_USER;
enum IPC_GETOUTPUTTIME = 105;
enum IPC_GETLISTPOS = 125;
enum IPC_GETPLAYLISTTITLE = 212;
enum IPC_GET_API_SERVICE = 3025;
enum IPC_REGISTER_WINAMP_IPCMESSAGE = 65536;
enum WM_WA_SYSTRAY = WM_USER+1;
enum WM_WA_MPEG_EOF = WM_USER+2;
