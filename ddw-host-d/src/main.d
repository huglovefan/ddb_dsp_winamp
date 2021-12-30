module ddw.host.main;

import core.stdc.stdio;
import core.stdc.stdlib;

import core.sys.windows.winbase;
import core.sys.windows.windef;
import core.sys.windows.winuser;

import std.string : toStringz;

import ddw.shmdata;
import ddw.host.buf;
import ddw.host.misc;
import ddw.host.plugin;
import ddw.host.plugproc;
import ddw.host.plugload;
import ddw.host.procmain;
import ddw.host.shm;
import ddw.host.wndproc;

enum STDIN_FILENO = 0;
enum STDOUT_FILENO = 1;
enum STDERR_FILENO = 2;

extern (C) int dup(int);
extern (C) int dup2(int, int);
extern (C) int close(int);
extern (C) int open(const(char)*, int);

// https://docs.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread
enum STACK_SIZE_PARAM_IS_A_RESERVATION = 0x00010000;

// https://github.com/wine-mirror/wine/blob/80e2154/include/msvcrt/fcntl.h
enum O_RDWR = 2;

// -----------------------------------------------------------------------------

__gshared int in_fd = -1;
__gshared int out_fd = -1;

__gshared Shm* shm;

__gshared Plugin[] plugins;

__gshared void* mainwin;
__gshared uint main_tid;

// -----------------------------------------------------------------------------

bool new_plugin(const(char)* arg, Plugin* pl)
{
	if (!parse_plugin_options(arg, &pl.opts))
	{
		fprintf(stderr, "error: option parsing failed for argument \"%s\"\n", arg);
		return false;
	}

	if (!load_plugin(pl))
	{
		fprintf(stderr, "error: plugin load failed for dll \"%s\"\n", pl.opts.path);
		return false;
	}

	return true;
}

// -----------------------------------------------------------------------------

int mainloop()
{
	MSG msg;
	int status = 0;

	for (;;)
	{
		int rv = GetMessage(&msg, null, 0, 0);
		if (rv > 0)
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}
		if (rv < 0)
		{
			PrintError("GetMessage");
			status = 1;
		}
		break;
	}

	if (msg.message == WM_QUIT)
		status = cast(int)msg.wParam;

	return status;
}

extern (Windows) uint conf_thread_main(void* ud)
{
	Plugin* pl = cast(Plugin*)ud;
	pl.module_.Config(pl.module_);
	pl.confdone = true;
	return 0;
}

// https://github.com/dlang/druntime/blob/master/src/rt/dmain2.d
extern (C) int _d_run_main(int, char**, MainFunc) nothrow @nogc;
alias extern (C) int function(string[]) MainFunc;

extern (Windows) int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	return _d_run_main(0, null, &_Dmain);
}

extern (C) int _Dmain(string[] args)
{
	void* procthread = null;
	int rv = 0;

	main_tid = GetCurrentThreadId();

	//
	// set up fds
	//
	{
		int nul = -1;

		bool nok =
			(nul = open("NUL", O_RDWR)) == -1 || // open NUL for redirecting
			(in_fd = dup(STDIN_FILENO)) == -1 || // duplicate stdin to in_fd
			(out_fd = dup(STDOUT_FILENO)) == -1 || // duplicate stdout to out_fd
			dup2(nul, STDIN_FILENO) == -1 || // set stdin to the /dev/null fd
			dup2(STDERR_FILENO, STDOUT_FILENO) == -1; // set stdout to stderr

		if (nul != -1)
		{
			close(nul);
			nul = -1;
		}

		if (nok)
		{
			fprintf(stderr, "error: fd shuffle failed\n");
			goto err;
		}

		setvbuf(stdout, null, _IONBF, 0);
		setvbuf(stderr, null, _IONBF, 0);
	}

	//
	// open shm file
	//
	if (getenv("DDW_SHM_NAME") != null)
	{
		shm = cast(Shm*)shmnew(getenv("DDW_SHM_NAME"), Shm.sizeof);
		if (shm == null)
			fprintf(stderr, "warning: shm open failed\n");
	}
	else
	{
		fprintf(stderr, "warning: DDW_SHM_NAME not set\n");
	}

	//
	// create IPC message window
	// https://stackoverflow.com/a/4081383
	//
	{
		WNDCLASSEX wx = {
			cbSize: WNDCLASSEX.sizeof,
			lpfnWndProc: (shm != null)
				? cast(typeof(&DefWindowProc))&WindowProc // cast to nothrow
				: &DefWindowProc,
			hInstance: GetModuleHandle(null),
			lpszClassName: "Winamp v1.x",
		};
		if (RegisterClassEx(&wx) == 0)
		{
			PrintError("RegisterClassEx");
			goto err;
		}

		mainwin = CreateWindowEx(
			0,
			wx.lpszClassName,
			"Winamp",
			0,
			0, 0, 0, 0,
			HWND_MESSAGE,
			null,
			wx.hInstance,
			null);
		if (mainwin == null)
		{
			PrintError("CreateWindowEx");
			goto err;
		}
	}

	//
	// ancient ritual
	//
	{
		MSG tmp;
		PeekMessage(&tmp, null, 0, 0, PM_NOREMOVE);
	}

	//
	// load plugins
	//
	{
		Plugin* pluginsp = cast(Plugin*)calloc(args.length-1, Plugin.sizeof);
		plugins = pluginsp[0..args.length-1];
	}
	for (int i = 1; i < args.length; i++)
	{
		version (D_BetterC)
		{
			if (!new_plugin(args[i].ptr, &plugins[i-1]))
				goto err;
		}
		else
		{
			if (!new_plugin(args[i].toStringz, &plugins[i-1]))
				goto err;
		}
	}
	if (plugins.length == 0)
	{
		fprintf(stderr, "it works\n");
		goto err;
	}

	//
	// start processing thread
	//
	procthread = CreateThread(
		null,
		16*1024*1024,
		&process_thread_main,
		null,
		STACK_SIZE_PARAM_IS_A_RESERVATION,
		null);
	if (procthread == null)
	{
		PrintError("CreateThread");
		goto err;
	}

	//
	// call config() for plugins that need it
	//
	foreach (ref pl; plugins)
	{
		if (!pl.opts.doconf)
		{
			pl.confdone = true;
			continue;
		}

		pl.confdone = false;

		HANDLE confthread = CreateThread(
			null,
			0,
			&conf_thread_main,
			&pl,
			0,
			null);

		if (confthread == null)
		{
			PrintError("CreateThread");
			pl.confdone = true;
			continue;
		}

		CloseHandle(confthread);
	}

	//
	// run main loop
	//
	rv = mainloop();

	//
	// wait (2000ms) for the processing thread to exit
	//
Lout:
	if (procthread != null)
	{
		if (WaitForSingleObject(procthread, 2000) != WAIT_OBJECT_0)
			assert(0, "failed to join thread in 2000ms");

		CloseHandle(procthread);
		procthread = null;
	}

	while (plugins.length != 0)
	{
		Plugin* pl = &plugins[$-1];

		if (!pl.confdone)
		{
			pl.module_.Quit(pl.module_);
			FreeLibrary(pl.dll);
		}

		buf_free(&pl.buf);

		plugins = plugins[0..$-1];
	}

	return rv;
err:
	if (rv == 0)
		rv = 1;

	goto Lout;
}
