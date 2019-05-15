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

/* regularbasename() doesn't do backslashes */
static const char *
superbasename(const char *path)
{
	const char *r = path;
	for (; *path != '\0'; path += 1)
		if (*path == '/' || *path == '\\')
			r = path + 1;
	return r;
}

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

struct plugin_info {
	winampDSPModule *module;
	HMODULE dll;
	BOOL did_call_init;
	size_t process_max_frames;
};

static struct plugin_info plugins[50];
static unsigned int nplugins = 0;

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
	plugins[nplugins].process_max_frames = 0;
	if (module->description != NULL) {
		if (strstr(module->description, "Freeverb") != NULL)
			plugins[nplugins].process_max_frames = 2048;
		else if (strstr(module->description, "PaceMaker") != NULL)
			plugins[nplugins].process_max_frames = 2048;
	}
	nplugins += 1;

	return TRUE;
failed:
	if (dll != NULL)
		FreeLibrary(dll);
	return FALSE;
}

static DWORD main_thread;

#define BUFFER_SIZE_MULT 2

#define xrealloc(ptr, sz) assert((ptr = realloc(ptr, sz)) != NULL)
static inline char *xmalloc(size_t sz) { char *p = malloc(sz); assert(p != NULL); return p; }

#define MIN(a,b)((a)<(b)?(a):(b))

static int
process_plugin_chunked(int nframes_in,
                       const struct processing_request *request,
                       struct plugin_info *plugin,
                       const char *restrict inbuf,
                       char *restrict outbuf)
{
	size_t nbytes_in = nframes_in*(request->bitspersample/8)*request->channels;
	size_t max_bytes = plugin->process_max_frames*(request->bitspersample/8)*request->channels;
	unsigned offset;
	int nframes, nframes_total = 0;

	assert(plugin->process_max_frames != 0);
	for (offset = 0; offset < nbytes_in; offset += max_bytes) {
		size_t bytes = MIN(nbytes_in-offset, max_bytes);
		assert(bytes != 0);
		assert(bytes % ((request->bitspersample/8)*request->channels) == 0);
		char buf[bytes*BUFFER_SIZE_MULT];
		memcpy(buf, inbuf+offset, bytes);
		nframes = plugin->module->ModifySamples(plugin->module,
		    (short int *)buf,
		    bytes/(request->bitspersample/8)/request->channels,
		    request->bitspersample,
		    request->channels,
		    request->samplerate);
		if (nframes < 0)
			fprintf(stderr, "%s: nframes = %d\n", progname, nframes);
		if (nframes > 0) {
			assert(nframes*(request->bitspersample/8)*request->channels <= sizeof(buf));
			memcpy(
			    outbuf + (nframes_total*(request->bitspersample/8)*request->channels),
			    buf,
			    nframes*(request->bitspersample/8)*request->channels);
			nframes_total += nframes;
		}
	}
	return nframes_total;
}

static int
process_plugin_full(int nframes_in,
                    const struct processing_request *request,
                    struct plugin_info *plugin,
                    const char *restrict inbuf,
                    char *restrict outbuf)
{
	size_t nbytes_in = nframes_in*(request->bitspersample/8)*request->channels;
	int nframes;

	assert(nbytes_in != 0);
	assert(nbytes_in % ((request->bitspersample/8)*request->channels) == 0);
	memcpy(outbuf, inbuf, nbytes_in);
	nframes = plugin->module->ModifySamples(plugin->module,
	    (short int *)outbuf,
	    nbytes_in/(request->bitspersample/8)/request->channels,
	    request->bitspersample,
	    request->channels,
	    request->samplerate);
	if (nframes < 0) {
		fprintf(stderr, "%s: nframes = %d\n", progname, nframes);
		nframes = 0;
	}
	return nframes;
}

static HANDLE reader_thread;

static DWORD
reader_main(void *ud)
{
	struct processing_request request;
	struct processing_response response;
	size_t buffer_size = 4096*(32*2)*2;
	char *buffer1 = (char *)xmalloc(buffer_size);
	char *buffer2 = (char *)xmalloc(buffer_size);
	unsigned int i;
	int nframes;
	(void)ud;

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
		for (i = 0; i < nplugins; i++) {
			xrealloc(buffer2, BUFFER_SIZE_MULT*nframes*(request.bitspersample/8)*request.channels);
			if (plugins[i].process_max_frames != 0 && (unsigned)nframes > plugins[i].process_max_frames)
				nframes = process_plugin_chunked(nframes, &request, &plugins[i], buffer1, buffer2);
			else
				nframes = process_plugin_full(nframes, &request, &plugins[i], buffer1, buffer2);
			if (nframes <= 0) {
				nframes = 0; /* not negative */
				break;
			}
			char *tmp = buffer2;
			buffer2 = buffer1;
			buffer1 = tmp;
		}
		response.buffer_size = nframes*(request.bitspersample/8)*request.channels;
		if ((size_t)write(STDOUT_FILENO, &response, sizeof(response)) != sizeof(response))
			break;
		if (response.buffer_size > 0) {
			if ((size_t)write(STDOUT_FILENO, buffer1, response.buffer_size) != response.buffer_size)
				break;
		}
	}
	PostThreadMessage(main_thread, WM_QUIT, 0, 0);
	return 0;
}

int
main(int argc, char **argv)
{
	int i, status = 0;

	progname = superbasename(argv[0]);
	main_thread = GetCurrentThreadId();

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

	for (i = nplugins-1; i >= 0; i--) {
		if (plugins[i].did_call_init && plugins[i].module->Quit != NULL)
			plugins[i].module->Quit(plugins[i].module);
		FreeLibrary(plugins[i].dll);
	}

	return status;
}

