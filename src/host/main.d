module ddw.host.main;

import core.stdc.stdio;
import core.stdc.stdlib;

import core.sys.windows.winbase;
import core.sys.windows.windef;
import core.sys.windows.winuser;

import ddw.common.gc;
import ddw.common.rtopts : rt_options;
import ddw.common.shmdata;
import ddw.host.buf;
import ddw.host.misc;
import ddw.host.plugin;
import ddw.host.plugproc;
import ddw.host.plugload;
import ddw.host.procmain;
import ddw.host.shm;
import ddw.host.wndproc;

struct globals
{
	__gshared static:

	Plugin[] plugins;

	Shm* shm;

	HWND mainwin;
	DWORD main_tid;

	static struct datapipe
	{
		__gshared static:

		int in_fd = -1;
		int out_fd = -1;
	}
}

extern(C)
int _Dmain(const(char)[][] args)
{
	HANDLE procthread;
	int rv = 0;

	globals.main_tid = GetCurrentThreadId();

	//
	// set up fds
	//
	{
		int nul = -1;

		bool nok =
			(nul = open("NUL", O_RDWR)) == -1 || // open NUL for redirecting
			(globals.datapipe.in_fd = dup(STDIN_FILENO)) == -1 || // duplicate stdin to in_fd
			(globals.datapipe.out_fd = dup(STDOUT_FILENO)) == -1 || // duplicate stdout to out_fd
			dup2(nul, STDIN_FILENO) == -1 || // set stdin to the /dev/null fd
			dup2(STDERR_FILENO, STDOUT_FILENO) == -1; // set stdout to stderr

		if (nul != -1)
		{
			close(nul);
			nul = -1;
		}

		if (nok)
		{
			printf("error: fd shuffle failed\n");
			goto err;
		}

		setvbuf(stdout, null, _IONBF, 0);
		setvbuf(stderr, null, _IONBF, 0);

		_setmode(globals.datapipe.in_fd, _O_BINARY);
		_setmode(globals.datapipe.out_fd, _O_BINARY);
	}

	//
	// make these be null-terminated
	//
	foreach (i; 0..args.length) args[i] = args[i].gcdup;

	//
	// open shm file
	//
	if (char* shmpath = getenv("DDW_SHM_NAME"))
	{
		globals.shm = cast(Shm*)shmnew(shmpath, Shm.sizeof);
		if (!globals.shm)
			printf("warning: shm open failed\n");
	}
	else
	{
		printf("warning: DDW_SHM_NAME not set\n");
	}

	//
	// create IPC message window
	// https://stackoverflow.com/a/4081383
	//
	{
		WNDCLASSEX wx = {
			cbSize: WNDCLASSEX.sizeof,
			lpfnWndProc: (globals.shm) ? &WindowProc : &DefWindowProc,
			hInstance: GetModuleHandle(null),
			lpszClassName: "Winamp v1.x",
		};
		if (RegisterClassEx(&wx) == 0)
		{
			PrintError("RegisterClassEx");
			goto err;
		}

		globals.mainwin = CreateWindowEx(
			0,
			wx.lpszClassName,
			"Winamp",
			0,
			0, 0, 0, 0,
			HWND_MESSAGE,
			null,
			wx.hInstance,
			null);
		if (globals.mainwin == null)
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
	globals.plugins = new Plugin[args.length-1];
	foreach (i; 1..args.length)
	{
		if (!new_plugin(args[i], &globals.plugins[i-1]))
			goto err;
	}
	if (globals.plugins.length == 0)
	{
		printf("it works\n");
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
	foreach (ref pl; globals.plugins)
	{
		if (pl.opts.noconf)
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
	if (procthread)
	{
		if (WaitForSingleObject(procthread, 2000) != WAIT_OBJECT_0)
		{
			printf("failed to join processing thread in 2000ms\n");
			_exit(1);
		}

		CloseHandle(procthread);
		procthread = null;
	}

	while (globals.plugins.length != 0)
	{
		Plugin* pl = &globals.plugins[$-1];

		if (pl.confdone)
		{
			pl.module_.Quit(pl.module_);
			FreeLibrary(pl.dll);
		}

		buf_free(&pl.buf);

		globals.plugins = globals.plugins[0..$-1];
	}

	return rv;
err:
	if (rv == 0)
		rv = 1;

	goto Lout;
}

// -----------------------------------------------------------------------------

private:

// -----------------------------------------------------------------------------

enum STDIN_FILENO = 0;
enum STDOUT_FILENO = 1;
enum STDERR_FILENO = 2;

extern(C) int dup(int);
extern(C) int dup2(int, int);
extern(C) int close(int);
extern(C) int open(const(char)*, int);

// https://docs.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread
enum STACK_SIZE_PARAM_IS_A_RESERVATION = 0x00010000;

// https://github.com/wine-mirror/wine/blob/80e2154/include/msvcrt/fcntl.h
enum O_RDWR = 2;

extern(Windows)
uint conf_thread_main(void* ud)
{
	Plugin* pl = cast(Plugin*)ud;
	pl.module_.Config(pl.module_);
	pl.confdone = true;
	return 0;
}

noreturn _exit(int status)
{
	TerminateProcess(GetCurrentProcess(), status);
	for (;;) abort();
}

// -----------------------------------------------------------------------------

bool new_plugin(const(char)[] arg, Plugin* pl)
{
	if (!parse_plugin_options(arg, &pl.opts))
	{
		printf("error: option parsing failed for argument \"%s\"\n", arg.ptr);
		return false;
	}

	if (!load_plugin(pl))
	{
		printf("error: plugin load failed for dll \"%s\"\n", pl.opts.path.ptr);
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
