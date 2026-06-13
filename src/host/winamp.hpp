/* SPDX-License-Identifier: Zlib */
#pragma once

#include <windef.h>
#include <winuser.h>

struct winampDSPModule
{
	const char *description;
	HWND        hwndParent;
	HINSTANCE   hDllInstance;
	void      (*Config)(struct winampDSPModule *this_mod);
	int       (*Init)(struct winampDSPModule *this_mod);
	int       (*ModifySamples)(
	    struct winampDSPModule *this_mod,
	    short *samples,
	    int    numsamples,
	    int    bps,
	    int    nch,
	    int    srate);
	void      (*Quit)(struct winampDSPModule *this_mod);
	void       *userData;
};
typedef struct winampDSPModule winampDSPModule;

struct winampDSPHeader
{
	int                       version;
	const char               *description;
	struct winampDSPModule *(*getModule)(int which);
	/* if .version >= 0x21 */
	int                      (*sf)(int);
	/* endif */
};
typedef struct winampDSPHeader winampDSPHeader;

/* with .version >= 0x22, this expects to receive a hwnd. in versions
   before that, it takes no arguments. the difference doesn't matter to
   us because the function is __cdecl (caller cleans up stack). */
typedef struct winampDSPHeader *(*winampDSPGetHeaderType)(HWND);

/* window message constants */
enum
{
	/* frontend.h OCT.5.1999 */
	WM_WA_IPC = WM_USER,

	/* wa502_sdk */
	/* not really sure what these are, but treat them as reserved.
	   */
	WM_WA_SYSTRAY  = WM_USER+1,
	WM_WA_MPEG_EOF = WM_USER+2
};

/* IPC message constants */
enum
{
	/* frontend.h OCT.5.1999 */
	IPC_GETVERSION       = 0,
	IPC_PLAYFILE         = 100, /* copydata */
	IPC_DELETE           = 101,
	IPC_STARTPLAY        = 102,
	IPC_CHDIR            = 103, /* copydata */
	IPC_ISPLAYING        = 104,
	IPC_GETOUTPUTTIME    = 105,
	IPC_JUMPTOTIME       = 106, /* 1.60+ */
	IPC_WRITEPLAYLIST    = 120, /* 1.666+ */
	IPC_SETPLAYLISTPOS   = 121, /* 2.0+ */
	IPC_SETVOLUME        = 122, /* 2.0+ */
	IPC_SETPANNING       = 123, /* 2.0+ */
	IPC_GETLISTLENGTH    = 124, /* 2.0+ */
	IPC_GETLISTPOS       = 125, /* 2.05+ */
	IPC_GETINFO          = 126, /* 2.05+ */
	IPC_GETEQDATA        = 127, /* 2.05+ */
	IPC_SETEQDATA        = 128, /* 2.05+ */
	IPC_SETSKIN          = 200, /* 2.04+ */
	IPC_GETSKIN          = 201, /* 2.04+ */
	IPC_EXECPLUG         = 202, /* 2.04+ */
	IPC_GETPLAYLISTFILE  = 211, /* 2.04+ */
	IPC_GETPLAYLISTTITLE = 212, /* 2.04+ */

	/* frontend.h JUL.12.2000 */
	IPC_ADDBOOKMARK       = 129, /* 2.4+ */
	IPC_RESTARTWINAMP     = 135, /* 2.2+ */
	IPC_MBOPEN            = 241, /* 2.05+ */
	IPC_INETAVAILABLE     = 242, /* 2.05+ */
	IPC_UPDTITLE          = 243, /* 2.2+ */
	IPC_CHANGECURRENTFILE = 245, /* 2.05+ */
	IPC_GETMBURL          = 246, /* 2.2+ */
	IPC_REFRESHPLCACHE    = 247, /* 2.2+ */
	IPC_MBBLOCK           = 248, /* 2.4+ */
	IPC_MBOPENREAL        = 249, /* 2.4+ */
	IPC_GET_SHUFFLE       = 250, /* 2.4+ */
	IPC_GET_REPEAT        = 251, /* 2.4+ */
	IPC_SET_SHUFFLE       = 252, /* 2.4+ */
	IPC_SET_REPEAT        = 253, /* 2.4+ */
};

/* IPC message constants */
/* selection of messages from newer sdk versions. these are either
   potentially useful for plugins or just easy to implement. */
enum
{
	/* wa502_sdk */
	IPC_GETINIFILE      = 334, /* 2.9+ */
	IPC_GETINIDIRECTORY = 335, /* 2.9+ */
	IPC_ISDOUBLESIZE    = 608,

	/* WA5.34_SDK_mar_15_2007 */
	IPC_GETVERSIONSTRING     = 1,
	IPC_GETPLAYLISTTITLEW    = 213,
	IPC_GETPLAYLISTFILEW     = 214,
	IPC_GETPLUGINDIRECTORY   = 336, /* 5.11+ */
	IPC_GETM3UDIRECTORY      = 337, /* 5.11+ */
	IPC_GETM3UDIRECTORYW     = 338, /* 5.3+ */
	IPC_ISMAINWNDVISIBLE     = 900, /* 5.0+ */
	IPC_GET_PROXY_STRING     = 3023, /* 5.11+ */
	IPC_USE_REGISTRY         = 3024, /* 5.11+ */
	IPC_GET_PLAYING_FILENAME = 3031,
};
