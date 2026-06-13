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
