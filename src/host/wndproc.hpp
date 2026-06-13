#pragma once

#include <windef.h>

struct ipc_window_data
{
	struct Shm *shm;
};

struct ipc_message_args
{
	HWND   hWnd;
	UINT   uMsg;
	WPARAM wParam;
	LPARAM lParam;
	void  *pl;
};

/* note: the try_* variants are slightly different: they don't print on
   unsupported messages, and they don't call into DefWindowProc. this
   makes them safe to call from contexts that don't necessarily have a
   message loop. */

bool try_handle_wm_copydata(
    LRESULT                 *rv_out,
    struct ipc_window_data  *dat,
    struct ipc_message_args *args);

bool try_handle_wm_command(
    LRESULT                 *rv_out,
    struct ipc_window_data  *dat,
    struct ipc_message_args *args);

bool try_handle_wm_wa_ipc(
    LRESULT                 *rv_out,
    struct ipc_window_data  *dat,
    struct ipc_message_args *args);

bool try_handle_wm_wa_systray(
    LRESULT                 *rv_out,
    struct ipc_window_data  *dat,
    struct ipc_message_args *args);

bool try_handle_wm_wa_mpeg_eof(
    LRESULT                 *rv_out,
    struct ipc_window_data  *dat,
    struct ipc_message_args *args);

LRESULT handle_wm_copydata(
    struct ipc_window_data  *dat,
    struct ipc_message_args *args);

LRESULT handle_wm_command(
    struct ipc_window_data  *dat,
    struct ipc_message_args *args);

LRESULT handle_wm_wa_ipc(
    struct ipc_window_data  *dat,
    struct ipc_message_args *args);

LRESULT handle_wm_wa_systray(
    struct ipc_window_data  *dat,
    struct ipc_message_args *args);

LRESULT handle_wm_wa_mpeg_eof(
    struct ipc_window_data  *dat,
    struct ipc_message_args *args);
