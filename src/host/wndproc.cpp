#include "wndproc.hpp"

#include "fastprintf.h"
#include "../float.h"
#include "plugin.hpp"
#include "../shmdata.h"
#include "winamp.hpp"

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

static void unsup(
	struct ipc_window_data  *dat,
	struct ipc_message_args *args)
{
	COPYDATASTRUCT *cds;
	const wchar_t *plname;

	if (args->pl)
		plname =
		    ((struct Plugin *)args->pl)->opts.dllname.c_str();
	else
		plname = L"?";

	switch (args->uMsg)
	{
	/* 0x004a */
	case WM_COPYDATA:
		cds = (COPYDATASTRUCT *)args->lParam;
		fastprintf_cons(
		    "ipc: unsupported WM_COPYDATA %lu (wparam=%u"
		    " lpdata=%p cbdata=%lu pl=%ls)\n",
		    cds->dwData,
		    args->wParam,
		    cds->lpData,
		    cds->cbData,
		    plname);
		break;

	/* 0x0111 */
	case WM_COMMAND:
		fastprintf_cons(
		    "ipc: unsupported WM_COMMAND %u (lparam=%ld"
		    " pl=%ls)\n",
		    args->wParam,
		    args->lParam,
		    plname);
		break;

	/* 0x0400 */
	case WM_WA_IPC:
		fastprintf_cons(
		    "ipc: unsupported WM_WA_IPC %lu (wparam=%u"
		    " pl=%ls)\n",
		    args->lParam,
		    args->wParam,
		    plname);
		break;

	/* 0x0401 */
	case WM_WA_SYSTRAY:
		fastprintf_cons(
		    "ipc: unsupported WM_WA_SYSTRAY (wparam=%u"
		    " lparam=%ld pl=%ls)\n",
		    args->wParam,
		    args->lParam,
		    plname);
		break;

	/* 0x0402 */
	case WM_WA_MPEG_EOF:
		fastprintf_cons(
		    "ipc: unsupported WM_WA_MPEG_EOF (wparam=%u"
		    " lparam=%ld)\n",
		    args->wParam,
		    args->lParam);
		break;
	}
}

#define RET(x) do { *rv_out = (x); return true; } while (0)

bool try_handle_wm_copydata(
	LRESULT                 *rv_out,
	struct ipc_window_data  *dat,
	struct ipc_message_args *args)
{
	return false;
}

bool try_handle_wm_command(
	LRESULT                 *rv_out,
	struct ipc_window_data  *dat,
	struct ipc_message_args *args)
{
	return false;
}

bool try_handle_wm_wa_ipc(
	LRESULT                 *rv_out,
	struct ipc_window_data  *dat,
	struct ipc_message_args *args)
{
	enum { EQ_ZERO_DB = 31 }; /* 0 dB value for IPC_GETEQDATA */

	switch (args->lParam)
	{
	/* 0 */
	case IPC_GETVERSION:
		/* pretend to be a version that somewhat matches the
		   level of support for ipc messages. */
		RET(0x2901); /* 2.91 */

	/* 1 */
	case IPC_GETVERSIONSTRING:
		/* this api doesn't actually exist in the 2.x version
		   that we pretend to be. */
		RET((LRESULT)"2.91 Build 0000");

	/* 104 */
	case IPC_ISPLAYING:
		if (!dat->shm)
			RET(1);
		switch (dat->shm->playback_state)
		{
		case SHM_PLSTATE_STOPPED: RET(0);
		case SHM_PLSTATE_PLAYING: RET(1);
		case SHM_PLSTATE_PAUSED:  RET(3);
		default: __builtin_trap();
		}

	/* 105 */
	/* -1 on error, or if stopped */
	case IPC_GETOUTPUTTIME:
		if (!dat->shm)
			RET(-1);
		switch (args->wParam)
		{
		case 0:
			if (dat->shm->playback_state == SHM_PLSTATE_STOPPED)
				RET(-1);
			RET(ftoi(dat->shm->playback_position * 1000.0f));
		case 1:
			RET(ftoi(dat->shm->track_duration));
		/* WA5.55_SDK */
		case 2:
			RET(ftoi(dat->shm->track_duration * 1000.0f));
		default:
			goto unsup;
		}

	/* 124 */
	case IPC_GETLISTLENGTH:
		if (!dat->shm)
			RET(1);
		RET(dat->shm->playlist_length);

	/* 125 */
	case IPC_GETLISTPOS:
		if (!dat->shm)
			RET(0);
		RET(dat->shm->playlist_position);

	/* 126 */
	/* note: WA5.34_SDK_mar_15_2007 clarifies that 0 returns the
	   value in kHz. */
	case IPC_GETINFO:
		if (!dat->shm)
			switch (args->wParam)
			{
			case 0:  RET(44100 / 1000);
			case 1:  RET(128);
			case 2:  RET(2);
			case 3:  RET(0);
			case 4:  RET(0);
			case 5:  RET(44100);
			default: goto unsup;
			}
		switch (args->wParam)
		{
		case 0:  RET(dat->shm->track_sample_rate / 1000);
		case 1:  RET(dat->shm->track_bitrate);
		case 2:  RET(dat->shm->track_channel_count);
		/* 5+ */
		case 3:  RET(0); /* video dimensions */
		case 4:  RET(0); /* pointer to video description */
		/* 5.25+ */
		case 5:  RET(dat->shm->track_sample_rate);
		default: goto unsup;
		}

	/* 127 */
	case IPC_GETEQDATA:
		switch (args->wParam)
		{
		case 0 ... 9: RET(EQ_ZERO_DB); /* eq band */
		case 10:      RET(EQ_ZERO_DB); /* preamp */
		case 11:      RET(0); /* enabled */
		case 12:      RET(0); /* autoload */
		default:      goto unsup;
		}

	/* 211 */
	/* NULL on error */
	case IPC_GETPLAYLISTFILE:
		if (!dat->shm)
			RET(0);
		if (args->wParam != dat->shm->playlist_position)
			goto unsup;
		if (!dat->shm->track_file_path[0])
			RET(0);
		RET((LRESULT)dat->shm->track_file_path);

	/* 212 */
	/* NULL on error */
	case IPC_GETPLAYLISTTITLE:
		if (!dat->shm)
			RET(0);
		if (args->wParam != dat->shm->playlist_position)
			goto unsup;
		if (!dat->shm->track_title[0])
			RET(0);
		RET((LRESULT)dat->shm->track_title);

	/* 242 */
	case IPC_INETAVAILABLE:
		RET(1);

	/* 250 */
	case IPC_GET_SHUFFLE:
		if (!dat->shm)
			RET(0);
		RET(dat->shm->player_shuffle);

	/* 251 */
	case IPC_GET_REPEAT:
		if (!dat->shm)
			RET(0);
		RET(dat->shm->player_repeat);

	/* 334 */
	case IPC_GETINIFILE:
		RET((LRESULT)"\\Winamp.ini");

	/* 335 */
	case IPC_GETINIDIRECTORY:
		RET((LRESULT)"\\");

	/* 336 */
	case IPC_GETPLUGINDIRECTORY:
		RET((LRESULT)"\\");

	/* 337 */
	case IPC_GETM3UDIRECTORY:
		RET((LRESULT)"\\");

	/* 608 */
	case IPC_ISDOUBLESIZE:
		RET(0);

	/* 900 */
	case IPC_ISMAINWNDVISIBLE:
		RET(1);

	/* 3023 */
	case IPC_GET_PROXY_STRING:
		RET((LRESULT)"");

	/* 3024 */
	case IPC_USE_REGISTRY:
		RET(1);
	}

unsup:

	return false;
}

bool try_handle_wm_wa_systray(
	LRESULT                 *rv_out,
	struct ipc_window_data  *dat,
	struct ipc_message_args *args)
{
	return false;
}

bool try_handle_wm_wa_mpeg_eof(
	LRESULT                 *rv_out,
	struct ipc_window_data  *dat,
	struct ipc_message_args *args)
{
	return false;
}

#undef RET

LRESULT handle_wm_copydata(
	struct ipc_window_data  *dat,
	struct ipc_message_args *args)
{
	LRESULT rv;

	if (!try_handle_wm_copydata(&rv, dat, args))
	{
		unsup(dat, args);

		rv = DefWindowProc(
		    args->hWnd,
		    args->uMsg,
		    args->wParam,
		    args->lParam);
	}

	return rv;
}

LRESULT handle_wm_command(
	struct ipc_window_data  *dat,
	struct ipc_message_args *args)
{
	LRESULT rv;

	if (!try_handle_wm_command(&rv, dat, args))
	{
		unsup(dat, args);

		rv = DefWindowProc(
		    args->hWnd,
		    args->uMsg,
		    args->wParam,
		    args->lParam);
	}

	return rv;
}

LRESULT handle_wm_wa_ipc(
	struct ipc_window_data  *dat,
	struct ipc_message_args *args)
{
	LRESULT rv;

	if (!try_handle_wm_wa_ipc(&rv, dat, args))
	{
		unsup(dat, args);

		rv = DefWindowProc(
		    args->hWnd,
		    args->uMsg,
		    args->wParam,
		    args->lParam);
	}

	return rv;
}

LRESULT handle_wm_wa_systray(
	struct ipc_window_data  *dat,
	struct ipc_message_args *args)
{
	LRESULT rv;

	if (!try_handle_wm_wa_systray(&rv, dat, args))
	{
		unsup(dat, args);

		rv = DefWindowProc(
		    args->hWnd,
		    args->uMsg,
		    args->wParam,
		    args->lParam);
	}

	return rv;
}

LRESULT handle_wm_wa_mpeg_eof(
	struct ipc_window_data  *dat,
	struct ipc_message_args *args)
{
	LRESULT rv;

	if (!try_handle_wm_wa_mpeg_eof(&rv, dat, args))
	{
		unsup(dat, args);

		rv = DefWindowProc(
		    args->hWnd,
		    args->uMsg,
		    args->wParam,
		    args->lParam);
	}

	return rv;
}
