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
import std.process;

struct globals
{
	__gshared static:

	Plugin[] plugins;

	Shm* shm;

	HWND  mainWindow;
	DWORD mainThreadId;

	static struct datapipe
	{
		__gshared static:

		int in_fd = -1;
		int out_fd = -1;
	}
}

extern(C)
int ddw_main(string[] args)
{
	HANDLE procthread;
	int rv = 0;

	globals.mainThreadId = GetCurrentThreadId();

	/*
	 * set up fds
	 */
	{
		globals.datapipe.in_fd = dup(0);
		globals.datapipe.out_fd = dup(1);

		int nul = open("NUL", O_RDWR);
		int log = dup(2);

		dup2(nul, 0);
		dup2(log, 1);
		dup2(log, 2);

		close(nul);
		close(log);

		// disable buffering to have printfs show up immediately
		// windows doesn't have line buffering so this is the next sane option
		setvbuf(stdout, null, _IONBF, 0);
		setvbuf(stderr, null, _IONBF, 0);

		// don't mangle newlines
		// old versions of wine didn't require this but newer ones do, so don't
		//  remove this even if it appears to work without for you
		_setmode(globals.datapipe.in_fd, _O_BINARY);
		_setmode(globals.datapipe.out_fd, _O_BINARY);
	}

	// these should all
	// - appear on their own lines
	// - be written to stderr
	static if (0)
	{
		{ write_full(1, "write1\n".ptr, 7); }
		{ write_full(2, "write2\n".ptr, 7); }
		{ import core.stdc.stdio; printf("printf test\n"); }
		{ import core.stdc.stdio; fprintf(stdout, "fprintf stdout test\n"); }
		{ import core.stdc.stdio; fprintf(stderr, "fprintf stderr test\n"); }
		{ import std.stdio; writeln("writeln test"); }
		{ import std.stdio; stdout.writeln("stdout.writeln test"); }
		{ import std.stdio; stderr.writeln("stderr.writeln test"); }
	}

	/*
	 * make these be null-terminated
	 */
	foreach (ref arg; args)
		arg = arg.gcdup;

	/*
	 * open shm file
	 */
	if (string shmpath = environment.get("DDW_SHM_NAME"))
	{
		globals.shm = cast(Shm*)shmnew(shmpath, Shm.sizeof);
		if (!globals.shm)
			printf("warning: shm open failed\n");
	}
	else
	{
		printf("warning: DDW_SHM_NAME not set\n");
	}

	/*
	 * create IPC message window
	 * https://stackoverflow.com/a/4081383
	 */
	{
		enum className = "Winamp v1.x";

		WNDCLASSEX windowClass = {
			cbSize: WNDCLASSEX.sizeof,
			lpfnWndProc: (globals.shm) ? &WindowProc : &DefWindowProc,
			hInstance: GetModuleHandle(null),
			lpszClassName: className,
		};
		if (!RegisterClassEx(&windowClass))
		{
			PrintError("RegisterClassEx");
			goto err;
		}

		globals.mainWindow = CreateWindowEx(
			0,
			className,
			"Winamp",
			0,
			0, 0, 0, 0,
			HWND_MESSAGE,
			null,
			GetModuleHandle(null),
			null);
		if (!globals.mainWindow)
		{
			PrintError("CreateWindowEx");
			goto err;
		}
	}

	/*
	 * initialize message queue, or something
	 * from: https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postthreadmessagea#remarks
	 */
	{
		MSG tmp;
		PeekMessage(&tmp, null, 0, 0, PM_NOREMOVE);
	}

	/*
	 * load plugins
	 */
	globals.plugins = new Plugin[args.length-1];
	foreach (i, ref pl; globals.plugins)
	{
		if (!new_plugin(args[i+1], &pl))
			goto err;
	}
	if (!globals.plugins.length)
	{
		printf("it works\n");
		goto err;
	}

	/*
	 * start processing thread
	 */
	procthread = CreateThread(
		null,
		16*1024*1024,
		&process_thread_entry,
		null,
		STACK_SIZE_PARAM_IS_A_RESERVATION,
		null);
	if (!procthread)
	{
		PrintError("CreateThread");
		goto err;
	}

	/*
	 * call config() for plugins that need it
	 */
	foreach (ref pl; globals.plugins)
	{
		if (pl.opts.noconf)
		{
			pl.confdone = true;
			continue;
		}

		HANDLE confthread = CreateThread(
			null,
			0,
			&conf_thread_main,
			&pl,
			0,
			null);

		if (!confthread)
		{
			PrintError("CreateThread");
			pl.confdone = true;
			continue;
		}

		CloseHandle(confthread); // same as pthread_detach()
	}

	/*
	 * run main loop
	 */
	rv = mainloop();

Lout:
	/*
	 * wait (2000ms) for the processing thread to exit
	 */
	if (procthread)
	{
		if (WaitForSingleObject(procthread, 2000) != WAIT_OBJECT_0)
		{
			printf("failed to join processing thread in 2000ms\n");
			assert(0);
		}

		CloseHandle(exchange(procthread, null));
	}

	/*
	 * unload plugins in reverse order
	 */
	foreach_reverse (ref pl; globals.plugins)
	{
		// don't unload if it might still be calling Config()
		if (pl.confdone)
		{
			pl.module_.Quit(pl.module_);
			FreeLibrary(exchange(pl.dll, null));
		}

		buf_free(&pl.buf);
	}

	return rv;
err:
	if (!rv)
		rv = 1;

	goto Lout;
}

// -----------------------------------------------------------------------------

private:

// -----------------------------------------------------------------------------

extern(C) int dup(int);
extern(C) int dup2(int, int);
extern(C) int open(scope const(char)*, int);
extern(C) int close(int);

// https://docs.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread
enum STACK_SIZE_PARAM_IS_A_RESERVATION = 0x00010000;

// https://github.com/wine-mirror/wine/blob/wine-7.0/include/msvcrt/fcntl.h#L15
enum O_RDWR = 2;

// -----------------------------------------------------------------------------

extern(Windows)
uint conf_thread_main(void* ud)
{
	Plugin* pl = cast(Plugin*)ud;
	pl.module_.Config(pl.module_);
	pl.confdone = true;
	return 0;
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
