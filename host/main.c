#include "main.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "macros.h"
#include "misc.h"
#include "procmain.h"
#include "shm.h"
#include "wndproc.h"

// -----------------------------------------------------------------------------

int in_fd = -1;
int out_fd = -1;

struct shmdata *shm = NULL;

struct plugin plugins[MAX_PLUGINS];
unsigned int plugins_cnt = 0;
_Atomic int procidx = -1;

HWND mainwin = NULL;
DWORD main_tid;

// -----------------------------------------------------------------------------

__attribute__((optimize("-Os")))
static bool
new_plugin(const char *arg)
{
	if (plugins_cnt == MAX_PLUGINS) {
		fprintf(stderr, "error: too many plugins (%d max)\n",
		    MAX_PLUGINS);
		return false;
	}

	if (!parse_plugin_options(arg, &plugins[plugins_cnt].opts)) {
		fprintf(stderr, "error: option parsing failed for argument \"%s\"\n",
		    arg);
		return false;
	}

	if (!load_plugin(&plugins[plugins_cnt])) {
		fprintf(stderr, "error: plugin load failed for dll \"%s\"\n",
		    plugins[plugins_cnt].opts.path);
		return false;
	}

	plugins_cnt++;

	return true;
}

// -----------------------------------------------------------------------------

static int
mainloop(void)
{
	MSG msg = {0};
	int status = 0;

	for (;;) {
		int rv = GetMessage(&msg, NULL, 0, 0);
		if (rv > 0) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else if (rv < 0) {
			PrintError("GetMessage");
			// idk what you're supposed to do here
			status = 1;
			break;
		} else {
			break;
		}
	}

	if (msg.message == WM_QUIT)
		status = msg.wParam;

	return status;
}

static DWORD WINAPI
conf_thread_main(void *ud)
{
	struct plugin *pl = ud;
	pl->module->Config(pl->module);
	return 0;
}

__attribute__((optimize("-Os")))
int
main(int argc, char **argv)
{
	WNDCLASSEX wx;
	HANDLE procthread = NULL;
	int nul = -1;
	int rv = 0;

	// disable newline conversion
	_setmode(0, _O_BINARY);
	_setmode(1, _O_BINARY);
	_setmode(2, _O_BINARY);

	// open /dev/null for redirecting
	nul = open("/dev/null", O_RDWR);
	if (nul == -1) {
		perror("open");
		goto err;
	}

	// duplicate stdin to in_fd
	in_fd = dup(STDIN_FILENO);
	if (in_fd == -1) {
		perror("dup");
		goto err;
	}

	// duplicate stdout to out_fd
	out_fd = dup(STDOUT_FILENO);
	if (out_fd == -1) {
		perror("dup");
		goto err;
	}

	// set stdin to the /dev/null fd
	if (dup2(nul, STDIN_FILENO) == -1) {
		perror("dup2");
		goto err;
	}

	// set stdout to stderr
	if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1) {
		perror("dup2");
		goto err;
	}

	close(nul);
	nul = -1;

	setvbuf(stdin,  NULL, _IONBF, 0);
	setvbuf(stdout, NULL, _IOLBF, 256);
	setvbuf(stderr, NULL, _IOLBF, 256);

D	fprintf(stderr, "ddw_host.exe: debug checks are enabled\n");

	main_tid = GetCurrentThreadId();

	srand(main_tid*time(NULL)^time(NULL));

	PeekMessage(&(MSG){0}, NULL, 0, 0, PM_NOREMOVE);

	//
	// open the shared memory file if DDW_SHM_NAME= is set
	//

	if (getenv("DDW_SHM_NAME") != NULL) {
		shm = shmnew(getenv("DDW_SHM_NAME"), sizeof(struct shmdata));
		if (shm == NULL) {
			fprintf(stderr, "error: opening shm failed\n");
			goto err;
		}
	} else {
		fprintf(stderr, "warning: DDW_SHM_NAME not set\n");
	}

	//
	// create the "window" to receive winamp IPC messages
	// https://stackoverflow.com/a/4081383
	//

	wx = (WNDCLASSEX){
		.cbSize = sizeof(wx),
		.lpfnWndProc = (shm != NULL) ? WindowProc : DefWindowProc,
		.hInstance = GetModuleHandle(NULL),
		.lpszClassName = "Winamp v1.x",
	};
	if (RegisterClassEx(&wx) == 0) {
		PrintError("RegisterClassEx");
		goto err;
	}

	mainwin = CreateWindowEx(0,
	                         wx.lpszClassName,
	                         "Winamp",
	                         0,
	                         0, 0, 0, 0,
	                         HWND_MESSAGE,
	                         NULL,
	                         wx.hInstance,
	                         NULL);
	if (mainwin == NULL) {
		PrintError("CreateWindowEx");
		goto err;
	}

	//
	// load plugins
	//

	for (int i = 1; i < argc; i++) {
		if (!new_plugin(argv[i]))
			goto err;
	}

	if (plugins_cnt == 0) {
		fprintf(stderr, "it works\n");
		goto err;
	}

	//
	// start the processing thread
	//

	procthread = CreateThread(NULL,
	                          16*1024*1024,
	                          process_thread_main,
	                          NULL,
	                          STACK_SIZE_PARAM_IS_A_RESERVATION,
	                          NULL);
	if (procthread == NULL) {
		PrintError("CreateThread");
		goto err;
	}

	//
	// call config() for plugins that need it
	//

	for (unsigned int i = 0; i < plugins_cnt; i++) {
		HANDLE confthread;

		if (!plugins[i].opts.doconf)
			continue;

		confthread = CreateThread(NULL,
					  0,
					  conf_thread_main,
					  &plugins[i],
					  0,
					  NULL);
		if (confthread == NULL) {
			PrintError("CreateThread");
			continue;
		}

		// equivalent to pthread_detach()
		CloseHandle(confthread);
	}

	//
	// run the event loop
	//

	rv = mainloop();

	//
	// wait (1000ms) for the processing thread to exit
	//
out:
	if (procthread != NULL) {
		if (WaitForSingleObject(procthread, 1000) == WAIT_OBJECT_0) {
			CloseHandle(procthread);
			procthread = NULL;
		} else {
			fprintf(stderr, "warning: failed to join thread in 1000ms\n");
		}
	}

	while (plugins_cnt > 0) {
		struct plugin *pl = &plugins[plugins_cnt-1];
		pl->module->Quit(pl->module);
		FreeLibrary(pl->dll);
		buf_free(&pl->buf);
		plugins_cnt--;
	}

	if (nul != -1) {
		close(nul);
		nul = -1;
	}

	return rv;
err:
	if (rv == 0)
		rv = 1;

	goto out;
}
