#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>

#include <fcntl.h>
#include <io.h>
#include <windows.h>

#include "Winamp/DSP.H"
#include "../common.h"

static const char *progname;

static DWORD main_thread_id;
static HANDLE reader_thread;

struct plugin_info {
	winampDSPModule *module;
	HMODULE dll;
	/* if we called module.Init() and should call module.Quit() later */
	BOOL did_call_init;
	/* limit for buggy plugins that crash if fed too much data at once */
	unsigned int process_max_frames;
};

static struct plugin_info plugins[50];
static unsigned int nplugins = 0;

#define BUFFER_SIZE_MULT 2
_Static_assert(BUFFER_SIZE_MULT >= 1, "");

#define xrealloc(ptr, sz) assert((ptr = realloc(ptr, sz)) != NULL)
#define  xmalloc(ptr, sz) assert((ptr =  malloc(     sz)) != NULL)

#define MIN(a, b) ((a)<(b)?(a):(b))

#define SWAP(p1, p2) \
	do { \
		void *tmp = p1; \
		p1 = p2; \
		p2 = tmp; \
	} while (0)

/* like basename() but supports backslashes too */
static const char *
superbasename(const char *path)
{
	const char *r = path;
	for (; *path != '\0'; path += 1) {
		if (*path == '/' || *path == '\\')
			r = path + 1;
	}
	return r;
}

/*
 * "dir\dsp.dll"   -> "dir\dsp.dll", -1
 * "dir\dsp.dll:2" -> "dir\dsp.dll",  2
 */
static void
parse_plugin_path(const char *path, char **filename, int *index)
{
	char *colon;

	colon = strrchr(path, ':');
	if (colon != NULL && isdigit(*(colon+1))) {
		*filename = strdup(path);
		(*filename)[colon - path] = '\0';
		errno = 0;
		*index = (int)strtol(colon + 1, NULL, 10);
		if (errno == ERANGE || (*index == 0 && !(*(colon+1) == '0' && *(colon+2) == '\0'))) {
			fprintf(stderr, "%s: failed to parse plugin index from path '%s'\n",
			                progname, path);
			*index = -1;
		}
	} else {
		*filename = strdup(path);
		*index = -1;
	}
}

static BOOL
load_plugin_from_path(const char *path, int module_index)
{
	HMODULE                dll = NULL;
	winampDSPGetHeaderType get_header;
	winampDSPHeader        *header;
	winampDSPModule        *module;

	if (nplugins == sizeof(plugins)/sizeof(*plugins)) {
		fprintf(stderr, "%s: too many plugins!\n",
		                progname);
		goto failed;
	}

	dll = LoadLibrary(path);
	if (dll == NULL) {
		fprintf(stderr, "%s: failed to load dll %s\n",
		                progname, path);
		goto failed;
	}

	get_header = (winampDSPGetHeaderType)(void(*)(void))GetProcAddress(dll, "winampDSPGetHeader2");
	if (get_header == NULL) {
		fprintf(stderr, "%s: winampDSPGetHeader2() not found in dll %s\n",
		                progname, path);
		goto failed;
	}

	header = get_header(GetDesktopWindow());
	if (header == NULL || header->getModule == NULL) {
		fprintf(stderr, "%s: winampDSPGetHeader2() returned invalid result in dll %s\n",
		                progname, path);
		goto failed;
	}

	if (module_index >= 0) {
		module = header->getModule(module_index);
	} else {
		if ((module = header->getModule(0)) != NULL)
			module_index = 0;
		else if ((module = header->getModule(1)) != NULL)
			module_index = 1;
	}
	if (module == NULL) {
		if (module_index >= 0)
			fprintf(stderr, "%s: module index %d not found in dll %s\n",
			                progname, module_index, path);
		else
			fprintf(stderr, "%s: module index 0 or 1 not found in dll %s\n",
			                progname, path);
		goto failed;
	}

	path = superbasename(path);
	fprintf(stderr, "%s: %s\n", path, header->description);
	fprintf(stderr, "%s:%d: %s\n", path, module_index, module->description);

	module->hDllInstance = dll;
	module->hwndParent = GetDesktopWindow();

	plugins[nplugins].module = module;
	plugins[nplugins].dll = dll;
	plugins[nplugins].did_call_init = FALSE;
	plugins[nplugins].process_max_frames = 1024;
	if (strstr(module->description, "Stereo Tool") != NULL)
		plugins[nplugins].process_max_frames = 4096;
	nplugins += 1;

	return TRUE;
failed:
	if (dll != NULL)
		FreeLibrary(dll);
	return FALSE;
}

static int
process_plugin_chunked(int nframes_in,
                       const struct processing_request *request,
                       struct plugin_info *plugin,
                       const char *restrict inbuf,
                       char *restrict outbuf)
{
	int nframes_out = 0;

	assert(nframes_in > 0);
	assert(plugin->process_max_frames != 0);
	assert(inbuf != outbuf);
	while (nframes_in > 0) {
		int chunk_nframes_in = MIN((unsigned)nframes_in, plugin->process_max_frames);
		int chunk_nframes_out;
		memcpy(outbuf, inbuf,
		    chunk_nframes_in*(request->bitspersample/8)*request->channels);
		chunk_nframes_out = plugin->module->ModifySamples(plugin->module,
		    (short int *)outbuf,
		    chunk_nframes_in,
		    request->bitspersample,
		    request->channels,
		    request->samplerate);
		if (chunk_nframes_out < 0)
			chunk_nframes_out = 0;
		inbuf += chunk_nframes_in*(request->bitspersample/8)*request->channels;
		outbuf += chunk_nframes_out*(request->bitspersample/8)*request->channels;
		nframes_in -= chunk_nframes_in;
		nframes_out += chunk_nframes_out;
		assert(chunk_nframes_out <= chunk_nframes_in*BUFFER_SIZE_MULT);
	}
	assert(nframes_in == 0);
	return nframes_out;
}

static int
process_plugin_full(int nframes_in,
                    const struct processing_request *request,
                    struct plugin_info *plugin,
                    char *restrict buf)
{
	int nframes;

	assert(nframes_in > 0);
	assert(plugin->process_max_frames == 0 || (unsigned)nframes_in <= plugin->process_max_frames);
	nframes = plugin->module->ModifySamples(plugin->module,
	    (short int *)buf,
	    nframes_in,
	    request->bitspersample,
	    request->channels,
	    request->samplerate);
	if (nframes < 0)
		nframes = 0;
	assert(nframes <= nframes_in*BUFFER_SIZE_MULT);
	return nframes;
}

static DWORD
reader_main(void *ud)
{
	struct processing_request request;
	struct processing_response response;
	size_t buffer_size = BUFFER_SIZE_MULT*4096*(32/8)*2;
	char *buffer1;
	char *buffer2;
	unsigned int i;
	int nframes;
	(void)ud;

	xmalloc(buffer1, buffer_size);
	xmalloc(buffer2, buffer_size);
	for (;;) {
		if ((size_t)read(STDIN_FILENO, &request, sizeof(request)) != sizeof(request))
			break;
		assert(request.buffer_size != 0 && request.buffer_size % ((request.bitspersample/8)*request.channels) == 0);
		assert(request.samplerate != 0);
		assert(request.bitspersample != 0 && request.bitspersample % 8 == 0);
		assert(request.channels != 0);
		xrealloc(buffer1, request.buffer_size);
		if ((size_t)read(STDIN_FILENO, buffer1, request.buffer_size) != request.buffer_size)
			break;
		nframes = request.buffer_size/(request.bitspersample/8)/request.channels;
		for (i = 0; i < nplugins && nframes > 0; i++) {
			if (plugins[i].process_max_frames != 0 && (unsigned)nframes > plugins[i].process_max_frames) {
				xrealloc(buffer2, BUFFER_SIZE_MULT*nframes*(request.bitspersample/8)*request.channels);
				nframes = process_plugin_chunked(nframes, &request, &plugins[i], buffer1, buffer2);
				SWAP(buffer2, buffer1);
			} else {
				xrealloc(buffer1, BUFFER_SIZE_MULT*nframes*(request.bitspersample/8)*request.channels);
				nframes = process_plugin_full(nframes, &request, &plugins[i], buffer1);
			}
			/* processed output is in buffer1 */
		}
		if (nframes < 0)
			nframes = 0;
		response.buffer_size = nframes*(request.bitspersample/8)*request.channels;
		if ((size_t)write(STDOUT_FILENO, &response, sizeof(response)) != sizeof(response))
			break;
		if (response.buffer_size > 0) {
			if ((size_t)write(STDOUT_FILENO, buffer1, response.buffer_size) != response.buffer_size)
				break;
		}
	}
	PostThreadMessage(main_thread_id, WM_QUIT, 0, 0);
	return 0;
}

int
main(int argc, char **argv)
{
	int i, status = 0;

	progname = superbasename(argv[0]);
	main_thread_id = GetCurrentThreadId();

	if (argc <= 1) {
		fprintf(stderr, "usage: %s path\\to\\dsp_plugin.dll[:module_index] ...\n", progname);
		status = 1;
		goto exit;
	}

	for (i = 1; i < argc; i++) {
		char *filename;
		int index;
		BOOL ok;
		parse_plugin_path(argv[i], &filename, &index);
		ok = load_plugin_from_path(filename, index);
		free(filename);
		if (!ok) {
			status = 1;
			goto exit;
		}
	}
	for (i = 0; (unsigned)i < nplugins; i++) {
		if (plugins[i].module->Init != NULL)
			plugins[i].module->Init(plugins[i].module);
		plugins[i].did_call_init = TRUE;
	}

	_setmode(STDIN_FILENO, _O_BINARY);
	_setmode(STDOUT_FILENO, _O_BINARY);
	/* make sure event loop is initialized */
	{ MSG msg; PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE); }
	if ((reader_thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)reader_main, NULL, 0, NULL)) == NULL) {
		fprintf(stderr, "%s: thread creation failed\n", progname);
		status = 1;
		goto exit;
	}

	for (i = 0; (unsigned)i < nplugins; i++) {
		if (plugins[i].module->Config != NULL)
			plugins[i].module->Config(plugins[i].module);
	}

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	if (msg.message == WM_QUIT)
		status = msg.wParam;

exit:
	if (reader_thread != NULL) {
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		WaitForSingleObject(reader_thread, 1000);
	}

	for (i = nplugins - 1; i >= 0; i--) {
		if (plugins[i].did_call_init && plugins[i].module->Quit != NULL)
			plugins[i].module->Quit(plugins[i].module);
		FreeLibrary(plugins[i].dll);
	}

	return status;
}

