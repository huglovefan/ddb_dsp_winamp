#include "main.hpp"

#include <fcntl.h>
#include <stdio.h>
#include <inttypes.h>

#include <winbase.h>
#include <windef.h>

#include "fastprintf.h"
#include "../float.h"
#include "main.hpp"
#include "misc.hpp"
#include "plugload.hpp"
#include "plugproc.hpp"
#include "procmain.hpp"
#include "../shmdata.h"
#include "../monotime.h"
#include "shm.hpp"
#include "wndproc.hpp"

#include <imgui.h>
#include <backends/imgui_impl_win32.h>

#include <backends/imgui_impl_dx9.h>
#include <d3d9.h>

/* note that WM_USER, WM_USER+1 and WM_USER+2 are taken by ipc. */
enum
{
	WM_PLZ_CHECK_ACTIONS = WM_USER+3,

	/* request the main thread to exit. */
	/* this is received by the ipc window. */
	WM_PLZ_EXIT = WM_USER+4,
};

enum
{
	GUI_DRAW_TIMER_ID = 0xbadface
};

struct main_vars;

struct CoolGui
{
	struct main_vars *host;

	LPDIRECT3D9           d3d;
	LPDIRECT3DDEVICE9     d3d_device;
	D3DPRESENT_PARAMETERS d3d_pp;
	bool                  d3d_device_lost;

	struct ipc_window_data ipcdat;

	DWORD in_gui_draw;

	int width;
	int height;
	bool resized;

	int selected_plugin = -1;

	bool show_plt_in_out_diff;
	bool shmdebug;
	bool memstat;
	bool showfps;
	double gui_drawn_at[2];

	struct {
		char plt_in[32];
		char plt_out[32];
		char plt_difs[32];
		double proc_ms;
		double buf_ms;
		double proc_mult;
	} calc;

	UINT_PTR draw_timer;

	process_stats                       pstat;
	std::vector<process_stats_iter_sub> iterstat;
	size_t                              iterstatcnt;

	char plugarg_txtbox[MAX_PATH * 4 + 1];

	/* checked in response to WM_PLZ_CHECK_ACTIONS */
	struct {
		bool set;
		size_t plugidx;
	} plz_config;
	struct {
		bool set;
		size_t plugidx;
		int modidx;
	} plz_chmod;
	struct {
		bool set;
		size_t plugidx;
	} plz_rmplug;
	struct {
		bool set;
		wchar_t path[MAX_PATH + 1];
	} plz_addplug;
	struct {
		bool set;
		size_t plugidx;
	} plz_unload, plz_resurrect;
	struct {
		bool set;
		size_t plugidx;
		int direction;
	} plz_mvmod;
};

struct main_vars
{
	int                  in_fd;
	int                  out_fd;
	Shm                 *shm;
	struct plugin_list   plugins;

	/* windows */
	HWND                 hwnd_main;
	HWND                 hwnd_ipc;
	bool                 test_disable_fastipc;
	WNDCLASSEX           wc;

	/* processing thread */
	HANDLE               procthread;
	process_thread_vars *procvars;

	/* gui */
	CoolGui              gui;

};

#define MAIN_VARS_INIT \
	((struct main_vars){ \
		.in_fd = -1, \
		.out_fd = -1, \
	})

static volatile struct main_vars *g_host;

static bool create_windows(struct main_vars *host);
static void destroy_windows(struct main_vars *host);

static bool setup_fds(int *in_fd_out, int *out_fd_out)
{
	int in_fd;
	int out_fd;
	int nul;
	int log;

	in_fd = dup(0);
	out_fd = dup(1);

	nul = _wopen(L"NUL", O_RDWR);
	log = dup(2);

	/* none of these should be in the std{in,out,err} range. */
	if (in_fd  <= 2 ||
	    out_fd <= 2 ||
	    nul    <= 2 ||
	    log    <= 2)
	{
		fprintf(stderr,
		    "setup_fds: fd setup failed"
		    " (log=%d out=%d in=%d nul=%d)\n",
		    log,
		    out_fd,
		    in_fd,
		    nul);
		goto err_close;
	}

	if (dup2(nul, 0) < 0 ||
	    dup2(log, 1) < 0)
	{
		fprintf(stderr,
		    "setup_fds: fd redirect failed (should not"
		    " happen)\n");
		goto err_close;
	}

	close(nul);
	close(log);

	// disable buffering to have printfs show up immediately
	// windows doesn't have line buffering so this is the next best option
	setvbuf(stdout, nullptr, _IONBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);

	// don't mangle newlines
	_setmode(in_fd, _O_BINARY);
	_setmode(out_fd, _O_BINARY);

	*in_fd_out = in_fd;
	*out_fd_out = out_fd;

	return true;

err_close:

	close(in_fd);
	close(out_fd);
	close(nul);
	close(log);

	return false;
}

static void open_shm(Shm **shm_out)
{
	const wchar_t *shmpath;

	shmpath = _wgetenv(L"DDW_SHM_NAME");

	if (!shmpath)
	{
		fastprintf("warning: DDW_SHM_NAME not set\n");
		return;
	}

	*shm_out = (Shm *)shmnew(shmpath, sizeof(Shm));
}

/*
 * initialize message queue, or something
 * from: https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postthreadmessagea#remarks
 */
static void init_message_queue()
{
	MSG tmp;
	PeekMessage(&tmp, nullptr, 0, 0, PM_NOREMOVE);
}

static struct Plugin *plugin_new_from_arg(
	struct main_vars *vars,
	wchar_t          *arg,
	bool              initial_load = false)
{
	Plugin *pl, *other;
	HWND parent_window;

	pl = new (std::nothrow) Plugin{};

	if (!pl)
		return nullptr;

	if (!parse_plugin_options(arg, pl->opts))
	{
		fastprintf(
		    "error: option parsing failed for argument"
		    " \"%ls\"\n",
		    arg);
		delete pl;
		return nullptr;
	}

	PLUGIN_LIST_FOREACH(other, &vars->plugins)
	{
		if (other->opts.path == pl->opts.path)
		{
			fastprintf("dup mod\n");
			delete pl;
			return nullptr;
		}
	}

	if (initial_load && pl->opts.init_unload)
	{
		fp_control_write(&pl->fp_main);
		fp_control_write(&pl->fp_process);
	}
	else
	{
		parent_window = vars->hwnd_main;
		if (pl->opts.ipc_as_hwnd_parent)
			parent_window = vars->hwnd_ipc;

		if (!load_plugin(pl, parent_window))
		{
			fastprintf(
			    "error: plugin load failed for dll \"%ls\"\n",
			    pl->opts.path.c_str());
			delete pl;
			return nullptr;
		}
	}

	if (initial_load)
		pl->skip_user = pl->opts.want_skip_user;

	return pl;
}

static void plugin_deinit_delete_nodll(struct Plugin *pl)
{
	assert(!pl->dll);

	buf_free(&pl->buf);
	pl->buf_fmt = AFMT_INVALID;

	delete pl;
}

static void plugin_deinit_delete(struct Plugin *pl)
{
	fp_control fp_save;

	assert(!pl->in_call);

	if (pl->dll)
	{
		fp_control_write(&fp_save);
		fp_control_read(pl->fp_main);

		pl->module->Quit(pl->module);
		FreeLibrary(pl->dll);

		fp_control_read(fp_save);

		pl->dll = nullptr;
		pl->header = nullptr;
		pl->module = nullptr;
	}

	plugin_deinit_delete_nodll(pl);
}

static void append_plugin(struct main_vars *vars, Plugin *pl)
{
	if (vars->plugins.count++)
	{
		pl->prev = vars->plugins.tail;
		pl->prev->next = pl;
		vars->plugins.tail = pl;
	}
	else
	{
		vars->plugins.head = pl;
		vars->plugins.tail = pl;
	}
}

static bool initial_load_plugins_argv(
	struct main_vars    *vars,
	std::span<wchar_t *> argv)
{
	Plugin *pl;

	if (argv.size() <= 1)
		return true;

	for (size_t i = 1; i != argv.size(); i++)
	{
		pl = plugin_new_from_arg(vars, argv[i], true);

		if (!pl)
			return false;

		PLUGIN_LIST_LOCK_RDWR(&vars->plugins);
		append_plugin(vars, pl);
		PLUGIN_LIST_UNLOCK_RDWR(&vars->plugins);
	}

	return true;
}

static HANDLE start_processing_thread(
	struct plugin_list *plugins,
	int                 in_fd,
	int                 out_fd,
	HWND                hwnd_main)
{
	process_thread_vars *procvars;
	HANDLE procthread;

	procvars = new (std::nothrow) process_thread_vars{};

	if (!procvars)
		return nullptr;

	*procvars = {
		plugins:      plugins,
		mainThreadId: GetCurrentThreadId(),
		in_fd:        in_fd,
		out_fd:       out_fd,
		hwnd_main:    hwnd_main,
	};

	procthread = CreateThread(
	    nullptr,
	    0,
	    &process_thread_entry,
	    procvars,
	    0,
	    nullptr);

	if (!procthread)
	{
		PrintError("CreateThread");
		delete procvars;
		return nullptr;
	}

	return procthread;
}

static void goodbye(main_vars &vars);
static bool gui_main(CoolGui *gui, HWND hwnd);

int ddw_main(std::span<wchar_t *> args)
{
	main_vars vars = MAIN_VARS_INIT;
	int rv;

	rv = 0;
	g_host = &vars;

	srand(__builtin_ia32_rdtsc());

	if (!setup_fds(&vars.in_fd, &vars.out_fd))
	{
		rv = 1;
		goto end;
	}

	open_shm(&vars.shm);
	if (!vars.shm)
		fastprintf("warning: failed to open shm file\n");

	vars.gui.host = &vars;
	if (!create_windows(&vars))
	{
		rv = 1;
		goto end;
	}

	init_message_queue();

	if (!initial_load_plugins_argv(&vars, args))
	{
		rv = 1;
		goto end;
	}

	vars.procthread = start_processing_thread(
	    &vars.plugins,
	    vars.in_fd,
	    vars.out_fd,
	    vars.hwnd_ipc);
	if (!vars.procthread)
	{
		rv = 1;
		goto end;
	}

	if (!gui_main(&vars.gui, vars.hwnd_main))
		rv = 1;

end:

	goodbye(vars);

	g_host = nullptr;

	return rv;
}

static void goodbye(main_vars &vars)
{
	struct plugin_list plugins;
	fp_control fp_save;
	Plugin *pl;

	if (vars.procthread)
	{
		/* there's an assumption here that we're exiting because
		   the proc thread told us to. currently we don't have a
		   way to tell the thread itself to exit. so just hope
		   it's about to do that. */

		fastprintf("ddw_host: waiting proc thread...\n");

		if (WaitForSingleObject(vars.procthread, 5000)
		    != WAIT_OBJECT_0)
		{
			fastprintf(
			    "failed to join processing thread in 5s\n");
			abort();
		}

		fastprintf("ddw_host: waiting proc thread... done\n");

		CloseHandle(vars.procthread);
		vars.procthread = nullptr;
	}

	fastprintf("ddw_host: unloading plugins...\n");

	/* move the chain so that no one else can access it from the
	   global. might be unnecessary. */
	PLUGIN_LIST_LOCK_RDWR(&vars.plugins);
	plugins.head = vars.plugins.head;
	plugins.tail = vars.plugins.tail;
	plugins.count = vars.plugins.count;
	vars.plugins.head = nullptr;
	vars.plugins.tail = nullptr;
	vars.plugins.count = 0;
	PLUGIN_LIST_UNLOCK_RDWR(&vars.plugins);

	fp_control_write(&fp_save);

	for (pl = plugins.tail; pl; /* empty */)
	{
		Plugin *prev;

		prev = pl->prev;
		plugin_deinit_delete(pl);
		pl = prev;
	}

	fastprintf("ddw_host: unloading plugins... done\n");

	/* do this last. in winamp, plugins can use ipc even in the dll
	   unload callback. */
	destroy_windows(&vars);

	// close the duplicates
	if (vars.in_fd != -1) close(exchange(vars.in_fd, -1));
	if (vars.out_fd != -1) close(exchange(vars.out_fd, -1));
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND   hWnd,
    UINT   msg,
    WPARAM wParam,
    LPARAM lParam);

typedef LRESULT (*ipc_handler_func)(
    ipc_window_data  *dat,
    ipc_message_args *args);

typedef bool (*try_ipc_handler_func)(
    LRESULT          *rv_out,
    ipc_window_data  *dat,
    ipc_message_args *args);

static Plugin *get_plugin_by_idx(struct plugin_list *plugins, size_t i)
{
	Plugin *pl;

	PLUGIN_LIST_FOREACH(pl, plugins)
	{
		if (!i--)
			return pl;
	}

	return nullptr;
}

static void gui_draw(CoolGui *gui);

static bool xable_gui_updates(CoolGui *gui, bool which)
{
	if (which == !!gui->draw_timer)
		return true;

	if (which)
	{
		gui->draw_timer = SetTimer(
		    gui->host->hwnd_main,
		    GUI_DRAW_TIMER_ID,
		    33,
		    nullptr);

		if (!gui->draw_timer)
		{
			PrintError("SetTimer");
			return false;
		}
	}
	else
	{
		if (!KillTimer(gui->host->hwnd_main, gui->draw_timer))
		{
			PrintError("KillTimer");
			/* whatever, let it be */
		}

		gui->draw_timer = 0;
	}

	return true;
}

static ipc_handler_func get_ipc_handler(UINT msg)
{
	switch (msg)
	{
	/* 0x004a */
	case WM_COPYDATA:
		return &handle_wm_copydata;

	/* 0x0111 */
	case WM_COMMAND:
		return &handle_wm_command;

	/* WM_USER (0x0400) */
	case WM_WA_IPC:
		return &handle_wm_wa_ipc;

	/* WM_USER+1 */
	case WM_WA_SYSTRAY:
		return &handle_wm_wa_systray;

	/* WM_USER+2 */
	case WM_WA_MPEG_EOF:
		return &handle_wm_wa_mpeg_eof;
	}

	return nullptr;
}

static try_ipc_handler_func get_try_ipc_handler(UINT msg)
{
	switch (msg)
	{
	/* 0x004a */
	case WM_COPYDATA:
		return &try_handle_wm_copydata;

	/* 0x0111 */
	case WM_COMMAND:
		return &try_handle_wm_command;

	/* WM_USER (0x0400) */
	case WM_WA_IPC:
		return &try_handle_wm_wa_ipc;

	/* WM_USER+1 */
	case WM_WA_SYSTRAY:
		return &try_handle_wm_wa_systray;

	/* WM_USER+2 */
	case WM_WA_MPEG_EOF:
		return &try_handle_wm_wa_mpeg_eof;
	}

	return nullptr;
}

/* i got this from chatgpt so you can be sure it's correct! */
/* chatgpt */
static void swap_motherfucker(
	struct Plugin  *left,
	struct Plugin  *right,
	struct Plugin **head,
	struct Plugin **tail)
{
	assert(left);
	assert(right);
	/* this thing depends on their order. */
	assert(left->next == right);
	assert(left == right->prev);

	struct Plugin *lp = left->prev;
	struct Plugin *rn = right->next;

	// splice right into left's position
	right->prev = lp;
	right->next = left;

	// splice left into right's position
	left->prev = right;
	left->next = rn;

	// fix external neighbors
	if (lp) lp->next = right; else *head = right;
	if (rn) rn->prev = left;  else *tail = left;
}

static bool pl_can_initiate_up(struct Plugin *pl)
{
	return pl->prev != nullptr;
}
static bool pl_can_initiate_dn(struct Plugin *pl)
{
	return pl->next != nullptr;
}
static bool pl_can_initiate_config(struct Plugin *pl)
{
	if (!pl->dll)
		return false;
	return !pl->in_call;
}
static bool pl_can_initiate_chmod(struct Plugin *pl)
{
	if (!pl->dll)
		return false;
	return !pl->in_call;
}
static bool pl_can_initiate_remove(struct Plugin *pl)
{
	return !pl->in_call;
}
static bool pl_can_initiate_unload(struct Plugin *pl)
{
	if (!pl->dll)
		return false;
	return !pl->in_call;
}
static bool pl_can_initiate_resurrect(struct Plugin *pl)
{
	if (pl->dll)
		return false;
	return !pl->in_call;
}
static bool gui_can_initiate_addplug(CoolGui *gui)
{
	/* i guess so. */
	return true;
}

static bool srwlock_rdwr_msgloop(SRWLOCK *lk, int max_wait_msec)
{
	while (!TryAcquireSRWLockExclusive(lk))
	{
		enum { SINGLE_WAIT_MAX_MSEC = 10 };
		MSG msg;
		double prewait, postwait;
		DWORD rv;
		int this_wait;
		bool got_msg;

		got_msg = false;
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			got_msg = true;
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		if (got_msg)
			continue;

		if (max_wait_msec <= 0)
			return false;

		/* don't actually wait the entire max value at once.
		   as for why, consider that the lock becoming unlocked
		   doesn't actually do anything to signal this wait to
		   end. we're essentially doing try-lock with a timer,
		   so more attempts is likely to get us the lock sooner.
		   */
		/* in practice, the wait would also end up being limited
		   by the gui draw timer (if the window isn't minimized)
		   - but it's better that we don't depend on that. */
		/* if this disgusts you, good. direct that energy
		   towards figuring out what to replace SRWLOCK with. */
		this_wait = max_wait_msec;
		if (this_wait > SINGLE_WAIT_MAX_MSEC)
			this_wait = SINGLE_WAIT_MAX_MSEC;

		prewait = monotime_msec();
		rv = MsgWaitForMultipleObjectsEx(
		    0, nullptr,
		    this_wait,
		    QS_ALLINPUT,
		    MWMO_INPUTAVAILABLE);

		if (rv == WAIT_FAILED)
		{
			PrintError("MsgWaitForMultipleObjectsEx");
			max_wait_msec = 0;
		}
		else if (rv == WAIT_TIMEOUT)
			max_wait_msec -= this_wait;
		else
		{
			double elapsed;

			/* ugly */
			postwait = monotime_msec();
			elapsed = postwait-prewait;
			if (elapsed >= 0.0
			    && elapsed < (double)max_wait_msec)
				max_wait_msec -= (int)elapsed;
			else
				max_wait_msec = 0;
		}
	}

	return true;
}

static bool plugin_list_lock_rdwr_msgloop(CoolGui *gui)
{
	/* !!!danger!!! callers should remember that this function can
	   call into the message loop, which can cause various actions
	   to be triggered. */

	/* this is a message-loop-integrated version of lock_rdwr().
	   it's needed to avoid a deadlock. consider:
	   - proc is calling into a plugin that's using ipc
	   - main is here, about to lock rdwr
	   - but proc doesn't release lock until ipc processed
	   - and main doesn't process ipc until proc releases lock */

	/* yes, it's pretty stupid. or is it? */
	/* but also, it works... or does it? */

	if (!srwlock_rdwr_msgloop(&gui->host->plugins._lk, 5000))
		return false;

	gui->host->plugins._xowner = GetCurrentThreadId();

	PLUGIN_LIST_ASSERT_RDWR(&gui->host->plugins);

	return true;
}

static bool dll_lock_rdwr_msgloop(struct Plugin *pl)
{
	/* implemented this before reproducing it, but i'm 90% sure dll
	   locks need this too - they have the same deadlock hazard as
	   the plugin list lock. */

	return srwlock_rdwr_msgloop(&pl->dll_lock, 5000);
}

static void pl_execute_move(CoolGui *gui, size_t plugidx, int direction)
{
	struct Plugin *pl;
	struct Plugin *other;

	pl = get_plugin_by_idx(&gui->host->plugins, plugidx);
	if (!pl)
		return;

	if (!plugin_list_lock_rdwr_msgloop(gui))
		return;

	if (direction > 0)
	{
		if (!pl->prev)
			goto end;
		other = pl->prev;
		swap_motherfucker(
		    pl->prev,
		    pl,
		    &gui->host->plugins.head,
		    &gui->host->plugins.tail);
	}
	else
	{
		if (!pl->next)
			goto end;
		other = pl->next;
		swap_motherfucker(
		    pl,
		    pl->next,
		    &gui->host->plugins.head,
		    &gui->host->plugins.tail);
	}

	if (plugidx == gui->selected_plugin)
		gui->selected_plugin -= direction;

	/* only need to clear buffers if both of them are active. */
	if (may_process_audio(pl)
	    && may_process_audio(other))
	{
		pl->did_move = true;
		other->did_move = true;
	}

end:
	PLUGIN_LIST_UNLOCK_RDWR(&gui->host->plugins);
}

static void pl_execute_config(CoolGui *gui, size_t plugidx)
{
	fp_control fp_save;
	struct Plugin *pl;

	pl = get_plugin_by_idx(&gui->host->plugins, plugidx);
	if (!pl || !pl_can_initiate_config(pl))
		return;

	pl->in_call = IN_CONFIG;

	fp_control_write(&fp_save);
	fp_control_read(pl->fp_main);

	pl->module->Config(pl->module);

	fp_control_write(&pl->fp_main);
	fp_control_read(fp_save);

	pl->in_call = 0;
}

static void pl_execute_chmod(CoolGui *gui, size_t plugidx, int modidx)
{
	fp_control fp_save;
	Plugin *pl;
	winampDSPModule *newmodule;
	int init_rv;

	pl = get_plugin_by_idx(&gui->host->plugins, plugidx);
	if (!pl || !pl_can_initiate_chmod(pl))
		return;

	pl->in_call = IN_CHMOD;

	if (!dll_lock_rdwr_msgloop(pl))
	{
		pl->in_call = 0;
		return;
	}

	fp_control_write(&fp_save);
	fp_control_read(pl->fp_main);

	newmodule = pl->header->getModule(modidx);

	if (!newmodule)
		/* silly motherfucker! */
		fastprintf(
		    "gui: module change: getModule returned no"
		    " module\n");
	else
	{
		pl->module->Quit(pl->module);

		newmodule->hDllInstance = pl->dll;
		if (pl->opts.null_as_hwnd_parent)
			newmodule->hwndParent = nullptr;
		else if (pl->opts.ipc_as_hwnd_parent)
			newmodule->hwndParent = gui->host->hwnd_ipc;
		else
			newmodule->hwndParent = gui->host->hwnd_main;

		init_rv = newmodule->Init(newmodule);

		if (init_rv)
		{
			/* note that this leaves module_idx unchanged so
			   that on resurrect, we get the old module that
			   presumably worked. */

			fastprintf(
			    "gui: module change: init returned %d,"
			    " unloading plugin\n",
			    init_rv);

			FreeLibrary(pl->dll);

			pl->dll = nullptr;
			pl->header = nullptr;
			pl->module = nullptr;
		}
		else
		{
			pl->module = newmodule;
			pl->opts.module_idx = modidx;
		}

		buf_free(&pl->buf);
		pl->buf_fmt = AFMT_INVALID;
		pl->internal_buffer_state = IB_CLEAN;
	}

	fp_control_write(&pl->fp_main);
	fp_control_read(fp_save);

	ReleaseSRWLockExclusive(&pl->dll_lock);

	pl->in_call = 0;
}

static void pl_execute_remove(CoolGui *gui, size_t plugidx)
{
	HMODULE dll;
	fp_control fp_save;
	Plugin *pl;

	pl = get_plugin_by_idx(&gui->host->plugins, plugidx);
	if (!pl || !pl_can_initiate_remove(pl))
		return;

	pl->in_call = IN_REMOVE;

	if (pl->dll)
	{
		if (!dll_lock_rdwr_msgloop(pl))
		{
			pl->in_call = 0;
			return;
		}

		dll = pl->dll;
		pl->dll = nullptr;

		fp_control_write(&fp_save);
		fp_control_read(pl->fp_main);

		pl->module->Quit(pl->module);
		FreeLibrary(dll);

		fp_control_read(fp_save);

		pl->header = nullptr;
		pl->module = nullptr;

		ReleaseSRWLockExclusive(&pl->dll_lock);

		buf_free(&pl->buf);
		pl->buf_fmt = AFMT_INVALID;
		pl->internal_buffer_state = IB_CLEAN;
	}

	if (!plugin_list_lock_rdwr_msgloop(gui))
	{
		/* this will leave the plugin unloaded. */
		pl->in_call = 0;
		return;
	}

	if (!pl->prev)
		gui->host->plugins.head = pl->next;
	if (!pl->next)
		gui->host->plugins.tail = pl->prev;

	if (pl->prev)
		pl->prev->next = pl->next;
	if (pl->next)
		pl->next->prev = pl->prev;

	gui->host->plugins.count--;

	if (plugidx == gui->selected_plugin)
		gui->selected_plugin = -1;

	PLUGIN_LIST_UNLOCK_RDWR(&gui->host->plugins);

	plugin_deinit_delete_nodll(pl);
}

static void pl_execute_unload(CoolGui *gui, size_t plugidx)
{
	fp_control fp_save;
	Plugin *pl;

	pl = get_plugin_by_idx(&gui->host->plugins, plugidx);
	if (!pl || !pl_can_initiate_unload(pl))
		return;

	pl->in_call = IN_UNLOAD;

	if (!dll_lock_rdwr_msgloop(pl))
	{
		pl->in_call = 0;
		return;
	}

	fp_control_write(&fp_save);
	fp_control_read(pl->fp_main);

	pl->module->Quit(pl->module);
	FreeLibrary(pl->dll);

	fp_control_write(&pl->fp_main);
	fp_control_read(fp_save);

	pl->dll = nullptr;
	pl->header = nullptr;
	pl->module = nullptr;

	buf_free(&pl->buf);
	pl->buf_fmt = AFMT_INVALID;
	pl->internal_buffer_state = IB_CLEAN;

	ReleaseSRWLockExclusive(&pl->dll_lock);

	pl->in_call = 0;
}

static void pl_execute_resurrect(CoolGui *gui, size_t plugidx)
{
	fp_control fp_save;
	Plugin *pl;

	pl = get_plugin_by_idx(&gui->host->plugins, plugidx);
	if (!pl || !pl_can_initiate_resurrect(pl))
		return;

	pl->in_call = IN_RESURRECT;

	if (!dll_lock_rdwr_msgloop(pl))
	{
		pl->in_call = 0;
		return;
	}

	fp_control_write(&fp_save);
	fp_control_read(pl->fp_main);

	load_plugin(pl, gui->host->hwnd_main);

	fp_control_read(fp_save);

	ReleaseSRWLockExclusive(&pl->dll_lock);

	pl->in_call = 0;
}

static bool pl_execute_addplug(CoolGui *gui, wchar_t *cmdarg)
{
	struct Plugin *pl;

	pl = plugin_new_from_arg(gui->host, cmdarg);
	if (!pl)
		return false;

	pl->skip_user = true;

	if (!plugin_list_lock_rdwr_msgloop(gui))
	{
		/* we're not able to add it to the list. */
		plugin_deinit_delete(pl);
		return false;
	}

	append_plugin(gui->host, pl);

	PLUGIN_LIST_UNLOCK_RDWR(&gui->host->plugins);

	return true;
}

static LRESULT WINAPI WndProc(
	HWND   hWnd,
	UINT   msg,
	WPARAM wParam,
	LPARAM lParam)
{
	CoolGui *gui;
	ipc_handler_func ipc_fn;
	LRESULT rv;

	gui = (CoolGui *)GetWindowLongPtr(hWnd, 0);

	/* just ignore these early messages. hopefully nothing below
	   would care about them. */
	if (!gui) [[unlikely]]
		goto hnd_default;

	/* can this happen??? */
	if (gui->in_gui_draw) [[unlikely]]
		fastprintf_cons(
		    "test - msg %d while in gui draw\n",
		    msg);

	/* for cost saving reasons, we reuse the same window class with
	   the same window procedure for the ipc window. distinguish
	   them here. */
	if (hWnd == gui->host->hwnd_ipc)
	{
		/* note: this has to be received by the ipc window
		   specifically. otherwise, we could lose it if main is
		   in a modal loop. */
		if (msg == WM_PLZ_EXIT) [[unlikely]]
		{
			PostMessage(
			    gui->host->hwnd_main, WM_CLOSE, 0, 0);
			return 0;
		}

		goto hnd_ipc_or_default;
	}

	rv = ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
	if (rv)
		return rv;

	/* msdn says for a lot of messages that "If an application
	   processes this message, it should return zero." that seems
	   to be the convention. */

	switch (msg)
	{
	/* 0x0002 */
	case WM_DESTROY:
		fastprintf("ddw_host: main got WM_DESTROY\n");
		xable_gui_updates(gui, false);
		PostQuitMessage(0);
		return 0;

	/* 0x0005 */
	case WM_SIZE:
	{
		bool minimized;
		minimized = (wParam == SIZE_MINIMIZED);
		if (!minimized) [[likely]]
		{
			gui->width = LOWORD(lParam);
			gui->height = HIWORD(lParam);
			gui->resized = true;
		}
		xable_gui_updates(gui, !minimized);
		return 0;
	}

	/* 0x0082 */
	case WM_NCDESTROY:
		gui->host->hwnd_main = nullptr;
		return 0;

	/* 0x0113 */
	case WM_TIMER:
		if (wParam == GUI_DRAW_TIMER_ID) [[likely]]
		{
			if (gui->in_gui_draw) [[unlikely]]
			{
				/* can this happen??? */
				fastprintf_cons(
				    "test - in_gui_draw already -"
				    " self=%lu other=%lu\n",
				    GetCurrentThreadId(),
				    gui->in_gui_draw);
				return 0;
			}
			gui->in_gui_draw = GetCurrentThreadId();
			gui_draw(gui);
			gui->in_gui_draw = 0;
			return 0;
		}
		/* ? */
		/* itching to assert or something but i'm not sure if
		   any plugins might use this. can they? */
		goto hnd_default;

	/* WM_USER+ */
	case WM_PLZ_CHECK_ACTIONS:
		if (gui->plz_config.set)
		{
			gui->plz_config.set = false;
			pl_execute_config(gui, gui->plz_config.plugidx);
		}
		if (gui->plz_chmod.set)
		{
			gui->plz_chmod.set = false;
			pl_execute_chmod(
			    gui,
			    gui->plz_chmod.plugidx,
			    gui->plz_chmod.modidx);
		}
		if (gui->plz_rmplug.set)
		{
			gui->plz_rmplug.set = false;
			pl_execute_remove(gui, gui->plz_rmplug.plugidx);
		}
		if (gui->plz_addplug.set)
		{
			bool ok;
			gui->plz_addplug.set = false;
			ok = pl_execute_addplug(gui, gui->plz_addplug.path);
			if (ok)
				gui->plugarg_txtbox[0] = 0;
		}
		if (gui->plz_unload.set)
		{
			gui->plz_unload.set = false;
			pl_execute_unload(gui, gui->plz_unload.plugidx);
		}
		if (gui->plz_resurrect.set)
		{
			gui->plz_resurrect.set = false;
			pl_execute_resurrect(gui, gui->plz_resurrect.plugidx);
		}
		if (gui->plz_mvmod.set)
		{
			gui->plz_mvmod.set = false;
			pl_execute_move(
			    gui,
			    gui->plz_mvmod.plugidx,
			    gui->plz_mvmod.direction);
		}
		return 0;
	}

hnd_ipc_or_default:

	ipc_fn = get_ipc_handler(msg);

	if (ipc_fn)
	{
		ipc_message_args args;

		memset(&args, 0, sizeof(args));
		args.hWnd = hWnd;
		args.uMsg = msg;
		args.wParam = wParam;
		args.lParam = lParam;

		return ipc_fn(&gui->ipcdat, &args);
	}

hnd_default:

	return DefWindowProc(hWnd, msg, wParam, lParam);
}

static void format_tm(
	char  *buf,
	size_t buflen,
	double dur_sec,
	bool   ex = false)
{
	unsigned long long ms;
	unsigned long long h, m, s;

	ms = dtoll(dur_sec * 1000.0);

	h = ms / (1000 * 60 * 60);
	ms -= h * (1000 * 60 * 60);

	m = ms / (1000 * 60);
	ms -= m * (1000 * 60);

	s = ms / 1000;
	ms -= s * 1000;

	if (!ex)
		if (h)
			snprintf(buf, buflen, "%llu:%02llu:%02llu", h, m, s);
		else
			snprintf(buf, buflen, "%llu:%02llu", m, s);
	else
		if (h)
			snprintf(buf, buflen, "%llu:%02llu:%02llu.%03llu", h, m, s, ms);
		else
			snprintf(buf, buflen, "%llu:%02llu.%03llu", m, s, ms);
}

#if defined(UNITTEST)
UNITTEST()
{
	char buf[64];
	format_tm(buf, sizeof(buf), 0.0); assert(!strcmp(buf, "0:00"));
	format_tm(buf, sizeof(buf), 0.9); assert(!strcmp(buf, "0:00"));
	format_tm(buf, sizeof(buf), 1.0); assert(!strcmp(buf, "0:01"));
	format_tm(buf, sizeof(buf), 1.9); assert(!strcmp(buf, "0:01"));
	format_tm(buf, sizeof(buf), 2.0); assert(!strcmp(buf, "0:02"));
	format_tm(buf, sizeof(buf), 12345.0);
	assert(!strcmp(buf, "3:25:45"));
}
#endif

void main_request_exit(HWND hwnd)
{
	if (!PostMessage(hwnd, WM_PLZ_EXIT, 0, 0))
		PrintError("main_request_exit: PostMessage");
}

static void gui_mk_plugtable(CoolGui *gui)
{
	struct Plugin *pl, *pl_selected;
	size_t i;

	if (!ImGui::BeginTable("pls", 3, ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg))
		return;

	pl_selected = nullptr;

	ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
	ImGui::TableSetupColumn("Name");
	ImGui::TableSetupColumn("DLL", ImGuiTableColumnFlags_WidthFixed);
	ImGui::TableHeadersRow();

	if (gui->selected_plugin != -1
	    && gui->selected_plugin >= gui->host->plugins.count)
		gui->selected_plugin = -1;

	i = 0;
	PLUGIN_LIST_FOREACH(pl, &gui->host->plugins)
	{
		char idstr[32];
		bool cbval;

		ImGui::TableNextRow();

		snprintf(idstr, sizeof(idstr), "##en_%zu", i);
		ImGui::TableNextColumn();
		cbval = !pl->skip_user;
		ImGui::Checkbox(idstr, &cbval);
		pl->skip_user = !cbval;

		ImGui::TableNextColumn();
		if (pl->dll)
			ImGui::Text("%s", pl->module->description);
		else
		{
			/* just want to dim it. */
			ImGui::BeginDisabled();
			if (pl->opts.modules.size()
			    && pl->opts.module_idx >= 0
			    && pl->opts.module_idx < pl->opts.modules.size())
				ImGui::Text("[%s]", pl->opts.modules[pl->opts.module_idx].c_str());
			else
				ImGui::Text("[%ls:%d]", pl->opts.dllname.c_str(), pl->opts.module_idx);
			ImGui::EndDisabled();
		}

		ImGui::TableNextColumn();
		ImGui::Text("%ls", pl->opts.dllname.c_str());

		snprintf(idstr, sizeof(idstr), "##sel_%zu", i);
		ImGui::SameLine(0.0, 0.0);
		ImGui::Selectable(idstr,
		    (gui->selected_plugin == ImGui::TableGetRowIndex()-1),
		    ImGuiSelectableFlags_SpanAllColumns|ImGuiSelectableFlags_AllowOverlap);
		if (ImGui::IsItemClicked()) [[unlikely]]
		{
			gui->selected_plugin = i;
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				if (pl->dll)
				{
					gui->plz_config.set = true;
					gui->plz_config.plugidx = i;
					PostMessage(gui->host->hwnd_main, WM_PLZ_CHECK_ACTIONS, 0, 0);
				}
				else
				{
					gui->plz_resurrect.set = true;
					gui->plz_resurrect.plugidx = i;
					PostMessage(gui->host->hwnd_main, WM_PLZ_CHECK_ACTIONS, 0, 0);
				}
			}
		}
		/* now that we know the selected plugin for this
		   frame... */
		if (i == gui->selected_plugin)
			pl_selected = pl;

		snprintf(idstr, sizeof(idstr), "plctx_%zu", i);
		ImGui::OpenPopupOnItemClick(idstr, ImGuiPopupFlags_MouseButtonRight);
		if (ImGui::BeginPopup(idstr)) [[unlikely]]
		{
			if (ImGui::MenuItem("Config", nullptr, nullptr, pl_can_initiate_config(pl)))
			{
				gui->plz_config.set = true;
				gui->plz_config.plugidx = i;
				PostMessage(gui->host->hwnd_main, WM_PLZ_CHECK_ACTIONS, 0, 0);
			}

			ImGui::BeginDisabled(pl->opts.modules.size() <= 1);
			if (ImGui::BeginMenu("Module"))
			{
				size_t idx;
				bool chk;

				if (!pl->opts.modules.size())
				{
					ImGui::Text("(not loaded)");
				}
				for (idx = 0; idx != pl->opts.modules.size(); idx++)
				{
					chk = (idx == pl->opts.module_idx);
					if (ImGui::MenuItem(
					    pl->opts.modules[idx].c_str(),
					    nullptr,
					    &chk,
					    !chk && pl_can_initiate_chmod(pl)))
					{
						gui->plz_chmod.set = true;
						gui->plz_chmod.plugidx = i;
						gui->plz_chmod.modidx = idx;
						PostMessage(gui->host->hwnd_main, WM_PLZ_CHECK_ACTIONS, 0, 0);
					}
				}
				ImGui::EndMenu();
			}
			ImGui::EndDisabled();

			if (ImGui::MenuItem(
			    "Move Up",
			    nullptr,
			    nullptr,
			    pl_can_initiate_up(pl)))
			{
				gui->plz_mvmod.set = true;
				gui->plz_mvmod.plugidx = i;
				gui->plz_mvmod.direction = 1;
				PostMessage(gui->host->hwnd_main, WM_PLZ_CHECK_ACTIONS, 0, 0);
			}

			if (ImGui::MenuItem(
			    "Move Down",
			    nullptr,
			    nullptr,
			    pl_can_initiate_dn(pl)))
			{
				gui->plz_mvmod.set = true;
				gui->plz_mvmod.plugidx = i;
				gui->plz_mvmod.direction = -1;
				PostMessage(gui->host->hwnd_main, WM_PLZ_CHECK_ACTIONS, 0, 0);
			}

			if (pl->dll)
			{
				if (ImGui::MenuItem("Unload", nullptr, nullptr, pl_can_initiate_unload(pl)))
				{
					gui->plz_unload.set = true;
					gui->plz_unload.plugidx = i;
					PostMessage(gui->host->hwnd_main, WM_PLZ_CHECK_ACTIONS, 0, 0);
				}
			}
			else
				if (ImGui::MenuItem("Load", nullptr, nullptr, pl_can_initiate_resurrect(pl)))
				{
					gui->plz_resurrect.set = true;
					gui->plz_resurrect.plugidx = i;
					PostMessage(gui->host->hwnd_main, WM_PLZ_CHECK_ACTIONS, 0, 0);
				}

			if (ImGui::MenuItem("Remove", nullptr, nullptr, pl_can_initiate_remove(pl)))
			{
				gui->plz_rmplug.set = true;
				gui->plz_rmplug.plugidx = i;
				PostMessage(gui->host->hwnd_main, WM_PLZ_CHECK_ACTIONS, 0, 0);
			}

			/* todo: show on alt+hover? or a toggle? */
			if (0)
			if (afmt_is_valid(&pl->last_fmt))
			{
				char nb1[AFMT_STRBUF];
				char nb2[AFMT_STRBUF];
				if (afmt_equal(&pl->last_fmt, &pl->last_oldfmt))
				{
					afmt_tostring(nb1, sizeof(nb1), &pl->last_fmt);
					ImGui::Text("%s", nb1);
				}
				else
				{
					afmt_tostring(nb1, sizeof(nb1), &pl->last_oldfmt);
					afmt_tostring(nb2, sizeof(nb2), &pl->last_fmt);
					ImGui::Text("%s -> %s", nb1, nb2);
				}
			}

			ImGui::EndPopup();
		}

		i++;
	}

	ImGui::EndTable();

	/* >>> button row */

	ImGui::BeginDisabled(
	    !pl_selected || !pl_can_initiate_up(pl_selected));
	if (ImGui::Button("Up")) [[unlikely]]
	{
		gui->plz_mvmod.set = true;
		gui->plz_mvmod.plugidx = gui->selected_plugin;
		gui->plz_mvmod.direction = 1;
		PostMessage(gui->host->hwnd_main, WM_PLZ_CHECK_ACTIONS, 0, 0);
		
	}
	ImGui::EndDisabled();

	ImGui::SameLine();

	ImGui::BeginDisabled(
	    !pl_selected || !pl_can_initiate_dn(pl_selected));
	if (ImGui::Button("Dn")) [[unlikely]]
	{
		gui->plz_mvmod.set = true;
		gui->plz_mvmod.plugidx = gui->selected_plugin;
		gui->plz_mvmod.direction = -1;
		PostMessage(gui->host->hwnd_main, WM_PLZ_CHECK_ACTIONS, 0, 0);
	}
	ImGui::EndDisabled();

	ImGui::SameLine();

	ImGui::BeginDisabled(
	    !pl_selected || !pl_can_initiate_config(pl_selected));
	if (ImGui::Button("Config")) [[unlikely]]
	{
		gui->plz_config.set = true;
		gui->plz_config.plugidx = gui->selected_plugin;
		PostMessage(gui->host->hwnd_main, WM_PLZ_CHECK_ACTIONS, 0, 0);
	}
	ImGui::EndDisabled();

	bool submit;

	submit = false;
	ImGui::SameLine();
	ImGui::PushItemWidth(175.0f);
	ImGui::BeginDisabled(!gui_can_initiate_addplug(gui));
	if (ImGui::InputText(
	    "##plugarg",
	    gui->plugarg_txtbox,
	    sizeof(gui->plugarg_txtbox),
	    ImGuiInputTextFlags_EnterReturnsTrue)) [[unlikely]]
		submit = true;
	ImGui::EndDisabled();
	ImGui::PopItemWidth();
	ImGui::SameLine();
	ImGui::BeginDisabled(
	    !gui_can_initiate_addplug(gui)
	    || !gui->plugarg_txtbox[0]);
	if (ImGui::Button("Add")) [[unlikely]]
		submit = true;
	ImGui::EndDisabled();
	if (submit) [[unlikely]]
	{
		int rv;

		rv = MultiByteToWideChar(
		    CP_UTF8,
		    0,
		    gui->plugarg_txtbox,
		    strlen(gui->plugarg_txtbox)+1,
		    gui->plz_addplug.path,
		    sizeof(gui->plz_addplug.path)/sizeof(wchar_t));
		if (rv > 0)
		{
			gui->plz_addplug.set = true;
			PostMessage(gui->host->hwnd_main, WM_PLZ_CHECK_ACTIONS, 0, 0);
		}
	}

	/* <<< button row */
}

static void gui_mk_stats(CoolGui *gui)
{
	process_stats &pstat = gui->pstat;
	size_t &statcnt = gui->iterstatcnt;
	const char *plstate;
	bool updstats;

	updstats = process_stats_updated_since(&pstat.updated);

	if (updstats && !process_get_stats(&pstat)) [[unlikely]]
		return;

	plstate = "";
	if (gui->host->shm)
		switch (gui->host->shm->playback_state)
		{
		case SHM_PLSTATE_PLAYING: plstate = "Playing  --  "; break;
		case SHM_PLSTATE_STOPPED: plstate = "Stopped  --  "; break;
		case SHM_PLSTATE_PAUSED:  plstate =  "Paused  --  "; break;
		}

	if (updstats)
	{
		if (!gui->iterstat.size()) [[unlikely]]
			gui->iterstat.resize(32);

		statcnt = process_get_iter_stats(
		    gui->iterstat.data(),
		    gui->iterstat.size());
	}

	/* lazy stringify playback timestamps */
	if (updstats)
	{
		format_tm(
		    gui->calc.plt_in,
		    sizeof(gui->calc.plt_in),
		    pstat.input_dur_sec);

		format_tm(
		    gui->calc.plt_out,
		    sizeof(gui->calc.plt_out),
		    pstat.output_dur_sec);
	}

	/* lazy calculate proc time and multiplier */
	if (updstats)
	{
		double procs;
		double durs;
		size_t i;

		procs = 0.0;
		durs = 0.0;
		for (i = 0; i < statcnt; i++)
		{
			double audio_dur_sec;
			audio_dur_sec =
			    (double)gui->iterstat[i].frames_out
			    / (double)gui->iterstat[i].fmt.rate;
			procs += gui->iterstat[i].process_dur_sec;
			durs += audio_dur_sec;
		}
		gui->calc.proc_ms = (procs/(double)statcnt)*1000.0;
		gui->calc.buf_ms = (durs/(double)statcnt)*1000.0;
		gui->calc.proc_mult = durs/procs;
	}

	if (
	    updstats ||
	    gui->show_plt_in_out_diff != !!gui->calc.plt_difs[0])
	{
		double dif;
		char pl[1];

		if (!gui->show_plt_in_out_diff)
			gui->calc.plt_difs[0] = 0;
		else
		{
			pl[0] = '+';
			/* usual case: input is slightly higher. */
			/* subtracted this way, dif is negative. */
			dif = (
			    pstat.output_dur_sec
			    - pstat.input_dur_sec);
			snprintf(
			    gui->calc.plt_difs,
			    sizeof(gui->calc.plt_difs),
			    " (%.*s%.0f ms)",
			    (dif >= 0.0), pl,
			    dif * 1000.0);
		}
	}

	ImGui::Text(
	    "%sIn: %s  Out: %s%s  Proc: %.0f/%.0f ms (%.1fx)",
	    plstate,
	    gui->calc.plt_in,
	    gui->calc.plt_out,
	    gui->calc.plt_difs,
	    gui->calc.proc_ms,
	    gui->calc.buf_ms,
	    gui->calc.proc_mult);

	ImGui::OpenPopupOnItemClick("statctx", ImGuiPopupFlags_MouseButtonRight);
	if (ImGui::BeginPopup("statctx")) [[unlikely]]
	{
		if (ImGui::Selectable("Reset times"))
			process_reset_time_stats();

		ImGui::MenuItem("Show In/Out diff", nullptr, &gui->show_plt_in_out_diff);
		ImGui::MenuItem("Show SHM debug info", nullptr, &gui->shmdebug);
		ImGui::MenuItem("Show mem stat", nullptr, &gui->memstat);
		ImGui::MenuItem("Show FPS", nullptr, &gui->showfps);

		ImGui::MenuItem("Disable fast IPC", nullptr, &gui->host->test_disable_fastipc);

		ImGui::EndPopup();
	}

	if (gui->shmdebug) [[unlikely]]
	if (gui->host->shm)
	{
		struct Shm *shm;

		shm = gui->host->shm;

		ImGui::Text("\n");
		ImGui::Text("SHM:\n");
		ImGui::Text("playback_state=%" PRIi32, shm->playback_state);
		ImGui::Text("playback_position=%f", (double)shm->playback_position);
		ImGui::Text("track_duration=%f", (double)shm->track_duration);
		ImGui::Text("playlist_length=%" PRIi32, shm->playlist_length);
		ImGui::Text("playlist_position=%" PRIi32, shm->playlist_position);
		ImGui::Text("track_sample_rate=%" PRIi32, shm->track_sample_rate);
		ImGui::Text("track_bitrate=%" PRIi32, shm->track_bitrate);
		ImGui::Text("track_channel_count=%" PRIi32, shm->track_channel_count);
		ImGui::Text("track_file_path=%s", shm->track_file_path);
		ImGui::Text("track_title=%s", shm->track_title);
		ImGui::Text("player_shuffle=%" PRIi32, shm->player_shuffle);
		ImGui::Text("player_repeat=%" PRIi32, shm->player_repeat);
	}

	if (gui->memstat) [[unlikely]]
	{
		MEMORYSTATUSEX ms;

		ms.dwLength = sizeof(ms);
		if (GlobalMemoryStatusEx(&ms))
		{
			ImGui::Text("ullTotalVirtual = %llu", ms.ullTotalVirtual);
			ImGui::Text("ullAvailVirtual = %llu", ms.ullAvailVirtual);

			ImGui::Text("Used: %.6f%%", (double)ms.ullTotalVirtual/(double)ms.ullAvailVirtual);
		}
	}

	if (gui->showfps) [[unlikely]]
	{
		double tm;

		tm = (gui->gui_drawn_at[1]-gui->gui_drawn_at[0])*1000.0;
		ImGui::Text("GUI: %.3f msec", tm);
	}

	if (statcnt == gui->iterstat.size()) [[unlikely]]
		gui->iterstat.resize(statcnt+2);
}

static void destroy_windows(struct main_vars *host)
{
	if (host->hwnd_main)
	{
		if (!DestroyWindow(host->hwnd_main))
			PrintError("DestroyWindow main");
		host->hwnd_main = nullptr;
	}

	if (host->hwnd_ipc)
	{
		if (!DestroyWindow(host->hwnd_ipc))
			PrintError("DestroyWindow ipc");
		host->hwnd_ipc = nullptr;
	}

	if (host->wc.lpszClassName)
	{
		UnregisterClass(
		    host->wc.lpszClassName,
		    host->wc.hInstance);
		memset(&host->wc, 0, sizeof(host->wc));
	}
}

static bool create_windows(struct main_vars *host)
{
	assert(!host->wc.lpszClassName);
	assert(!host->hwnd_main);
	assert(!host->hwnd_ipc);

	memset(&host->wc, 0, sizeof(host->wc));
	host->wc.cbSize = sizeof(host->wc);
	host->wc.style = CS_CLASSDC;
	host->wc.lpfnWndProc = WndProc;
	host->wc.cbWndExtra = sizeof(void *);
	host->wc.hInstance = GetModuleHandle(nullptr);
	host->wc.lpszClassName = L"Winamp v1.x";

	if (!RegisterClassEx(&host->wc))
	{
		PrintError("RegisterClassEx");
		memset(&host->wc, 0, sizeof(host->wc));
		return false;
	}

	/* TODO: load size and pos from imgui.ini */

	host->hwnd_main = CreateWindow(
	    host->wc.lpszClassName,
	    L"Winamp DSP Host",
	    WS_OVERLAPPEDWINDOW,
	    100, 100, 550, 300,
	    nullptr, nullptr, host->wc.hInstance, nullptr);

	if (!host->hwnd_main)
	{
		PrintError("CreateWindow");
		goto winfail;
	}

	host->hwnd_ipc = CreateWindow(
	    host->wc.lpszClassName,
	    nullptr,
	    0,
	    0, 0, 0, 0,
	    HWND_MESSAGE, nullptr, host->wc.hInstance, nullptr);

	if (!host->hwnd_ipc)
	{
		PrintError("CreateWindow");
		goto winfail;
	}

	/* TODO: i think this could be cleaned up somewhat. remove this
	   separate variable. pass it some other way to the ipc window
	   procedure. */
	host->gui.ipcdat.shm = host->shm;

	SetWindowLongPtr(host->hwnd_main, 0, (LONG_PTR)&host->gui);
	SetWindowLongPtr(host->hwnd_ipc, 0, (LONG_PTR)&host->gui);

	return true;

winfail:

	destroy_windows(host);

	return false;
}

static bool createDx(CoolGui *gui, HWND hWnd)
{
	HRESULT rv;

	gui->d3d = Direct3DCreate9(D3D_SDK_VERSION);

	if (!gui->d3d) [[unlikely]]
	{
		fastprintf("Direct3DCreate9 failed\n");
		return false;
	}

	gui->d3d_pp = (D3DPRESENT_PARAMETERS){
		.BackBufferFormat       = D3DFMT_UNKNOWN,
		.SwapEffect             = D3DSWAPEFFECT_DISCARD,
		.Windowed               = TRUE,
		.EnableAutoDepthStencil = TRUE,
		.AutoDepthStencilFormat = D3DFMT_D16,
		.PresentationInterval   = D3DPRESENT_INTERVAL_IMMEDIATE,
	};

	rv = gui->d3d->CreateDevice(
	    D3DADAPTER_DEFAULT,
	    D3DDEVTYPE_HAL,
	    hWnd,
	    D3DCREATE_HARDWARE_VERTEXPROCESSING,
	    &gui->d3d_pp,
	    &gui->d3d_device);

	if (rv != D3D_OK) [[unlikely]]
	{
		fastprintf("CreateDevice failed (%ld)\n", rv);
		return false;
	}

	return true;
}

static void resetDx(CoolGui *gui)
{
	HRESULT rv;

	ImGui_ImplDX9_InvalidateDeviceObjects();
	rv = gui->d3d_device->Reset(&gui->d3d_pp);
	if (rv != D3D_OK) [[unlikely]]
		fastprintf("Reset failed (%lu)\n", rv);
	ImGui_ImplDX9_CreateDeviceObjects();
}

static void goodbyeDx(CoolGui *gui)
{
	if (gui->d3d_device) [[likely]]
	{
		gui->d3d_device->Release();
		gui->d3d_device = nullptr;
	}

	if (gui->d3d) [[likely]]
	{
		gui->d3d->Release();
		gui->d3d = nullptr;
	}
}

static void gui_draw(CoolGui *gui)
{
	const ImGuiViewport *vp;
	ImGuiWindowFlags flags;
	HRESULT rv;

	if (gui->d3d_device_lost) [[unlikely]]
	{
		rv = gui->d3d_device->TestCooperativeLevel();
		if (rv == D3DERR_DEVICELOST)
			return;
		if (rv == D3DERR_DEVICENOTRESET)
			resetDx(gui);
		gui->d3d_device_lost = false;
	}

	if (gui->resized) [[unlikely]]
	{
		gui->resized = false;
		gui->d3d_pp.BackBufferWidth = gui->width;
		gui->d3d_pp.BackBufferHeight = gui->height;
		resetDx(gui);
	}

	/* new frame */
	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	/* >>> setup single-window */

	vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->Pos);
	ImGui::SetNextWindowSize(vp->Size);

	flags =
	    ImGuiWindowFlags_NoDecoration |
	    ImGuiWindowFlags_NoMove |
	    ImGuiWindowFlags_NoSavedSettings;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0, 8.0));
	ImGui::Begin("##root", nullptr, flags);
	ImGui::PopStyleVar(3);

	/* <<< */

	gui_mk_plugtable(gui);
	gui_mk_stats(gui);

	/* end window */
	ImGui::End();

	/* >>> rendering */

	ImGui::EndFrame();

	rv = gui->d3d_device->SetRenderState(D3DRS_ZENABLE, FALSE);
	if (rv != D3D_OK) [[unlikely]]
		fastprintf("SetRenderState failed (%ld)\n", rv);
	rv = gui->d3d_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	if (rv != D3D_OK) [[unlikely]]
		fastprintf("SetRenderState failed (%ld)\n", rv);
	rv = gui->d3d_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	if (rv != D3D_OK) [[unlikely]]
		fastprintf("SetRenderState failed (%ld)\n", rv);

	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	D3DCOLOR clear_col_dx = D3DCOLOR_RGBA(
	    (int)(clear_color.x*clear_color.w*255.0f),
	    (int)(clear_color.y*clear_color.w*255.0f),
	    (int)(clear_color.z*clear_color.w*255.0f),
	    (int)(clear_color.w*255.0f));

	rv = gui->d3d_device->Clear(
	    0,
	    nullptr,
	    D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
	    clear_col_dx,
	    1.0f,
	    0);
	if (rv != D3D_OK) [[unlikely]]
		fastprintf("Clear failed (%ld)\n", rv);

	rv = gui->d3d_device->BeginScene(); 
	if (rv != D3D_OK) [[unlikely]]
		fastprintf("BeginScene failed (%ld)\n", rv);
	else
	{
		ImGui::Render();
		ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
		rv = gui->d3d_device->EndScene();
		if (rv != D3D_OK) [[unlikely]]
			fastprintf("EndScene failed (%ld)\n", rv);
	}

	rv = gui->d3d_device->Present(
	    nullptr,
	    nullptr,
	    nullptr,
	    nullptr);
	if (rv != D3D_OK) [[unlikely]]
	{
		fastprintf("Present failed (%ld)\n", rv);
		if (rv == D3DERR_DEVICELOST)
			gui->d3d_device_lost = true;
	}

	if (gui->showfps)
	{
		gui->gui_drawn_at[0] = gui->gui_drawn_at[1];
		gui->gui_drawn_at[1] = monotime();
	}

	/* <<< */
}

static bool gui_main(CoolGui *gui, HWND hwnd)
{
	if (!createDx(gui, hwnd))
		return false;

	/* setup imgui */
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	/* ... style */
	ImGui::StyleColorsDark();
	/* ... backends */
	if (!ImGui_ImplWin32_Init(hwnd)) [[unlikely]]
	{
		fastprintf("ImGui_ImplWin32_Init failed\n");
		goto err_im_win;
	}
	if (!ImGui_ImplDX9_Init(gui->d3d_device)) [[unlikely]]
	{
		fastprintf("ImGui_ImplDX9_Init failed\n");
		goto err_im_dx;
	}

	xable_gui_updates(gui, true);

	ShowWindow(hwnd, SW_SHOWDEFAULT);

	for (;;)
	{
		MSG msg;
		BOOL rv;

		rv = GetMessage(&msg, nullptr, 0, 0);
		if (!rv) [[unlikely]]
			fastprintf("ddw_host: main got WM_QUIT\n");
		if (rv < 0) [[unlikely]]
			PrintError("GetMessage");
		if (rv > 0) [[likely]]
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}
		break;
	}

	ImGui_ImplDX9_Shutdown();
err_im_dx:
	ImGui_ImplWin32_Shutdown();
err_im_win:
	ImGui::DestroyContext();

	goodbyeDx(gui);

	return true;
}

bool main_handle_ipc_fast(
	LRESULT *rv_out,
	HWND     hWnd,
	UINT     uMsg,
	WPARAM   wParam,
	LPARAM   lParam,
	void    *ud)
{
	struct main_vars *host;

	host = (struct main_vars *)(uintptr_t)g_host;

	if (host && (hWnd == host->hwnd_main || hWnd == host->hwnd_ipc))
	{
		try_ipc_handler_func fn;

		if (host->test_disable_fastipc) [[unlikely]]
			return false;

		fn = get_try_ipc_handler(uMsg);

		if (fn)
		{
			ipc_message_args args;

			memset(&args, 0, sizeof(args));
			args.hWnd = hWnd;
			args.uMsg = uMsg;
			args.wParam = wParam;
			args.lParam = lParam;
			args.pl = ud;

			if (fn(rv_out, &host->gui.ipcdat, &args))
				return true;
		}
	}

	return false;
}
