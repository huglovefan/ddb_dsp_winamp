#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include <fcntl.h>
#include <io.h>
#include <windows.h>

#include "Winamp/DSP.H"
#include "../common.h"

struct plugin_info {
	winampDSPModule *module;
	HMODULE          dll;
	unsigned int     process_max_frames;
};

static const char *progname;

static DWORD  main_thread_id;
static HANDLE reader_thread;

static struct plugin_info plugins[10];
static unsigned int       nplugins = 0;

static bool     fixed_mode = false;
static uint32_t fixed_samplerate = 0;
static uint8_t  fixed_bitspersample = 0;
static uint8_t  fixed_channels = 0;

#define BUFFER_SIZE_MULT 2

#define MIN(a, b) ((a)<(b)?(a):(b))

/*
 * like basename() but supports backslashes too
 */
static const char *
__attribute__((cold))
__attribute__((nonnull))
__attribute__((returns_nonnull))
__attribute__((warn_unused_result))
superbasename(const char *path)
{
	const char *r;
	for (r = path; LIKELY(*path != '\0'); path += 1) {
		if (UNLIKELY(*path == '/' || *path == '\\'))
			r = path + 1;
	}
	return r;
}

/*
 * "dir\dsp.dll"   -> "dir\dsp.dll", -1
 * "dir\dsp.dll:2" -> "dir\dsp.dll",  2
 */
static bool
__attribute__((cold))
__attribute__((nonnull))
__attribute__((warn_unused_result))
parse_plugin_path(const char *path, char **filename, int *index)
{
	char *colon;

	colon = strrchr(path, ':');
	if (UNLIKELY(colon != NULL && isdigit(*(colon+1)))) {
		*filename = strdup(path);
		if (UNLIKELY(*filename == NULL))
			return false;
		(*filename)[colon - path] = '\0';
		/* this suck */
		errno = 0;
		*index = (int)strtol(colon + 1, NULL, 10);
		if (UNLIKELY(errno == ERANGE || (*index == 0 && !(*(colon+1) == '0' && *(colon+2) == '\0')))) {
			*index = -1;
			return false;
		}
		return true;
	} else {
		*filename = strdup(path);
		*index = -1;
		if (*filename == NULL)
			return false;
		return true;
	}
}

static bool
__attribute__((cold))
__attribute__((nonnull))
__attribute__((warn_unused_result))
load_plugin_from_path(const char *path, int module_index)
{
	HMODULE                dll;
	winampDSPGetHeaderType get_header;
	winampDSPHeader        *header;
	winampDSPModule        *module;

	if (UNLIKELY(nplugins >= sizeof(plugins)/sizeof(*plugins)))
		return false;

	dll = LoadLibrary(path);
	if (UNLIKELY(dll == NULL))
		return false;

	get_header = (winampDSPGetHeaderType)(void(*)(void)) \
	    GetProcAddress(dll, "winampDSPGetHeader2");
	if (UNLIKELY(get_header == NULL)) {
		FreeLibrary(dll);
		return false;
	}

	header = get_header(GetDesktopWindow());
	if (UNLIKELY(header == NULL || header->getModule == NULL)) {
		FreeLibrary(dll);
		return false;
	}

	if (module_index < 0) {
		if ((module = header->getModule(0)) != NULL)
			module_index = 0;
		else if ((module = header->getModule(1)) != NULL)
			module_index = 1;
	} else
		module = header->getModule(module_index);

	if (UNLIKELY(module == NULL)) {
		FreeLibrary(dll);
		return false;
	}

	module->hDllInstance = dll;
	module->hwndParent = GetDesktopWindow();
	if (LIKELY(module->Init != NULL)) {
		/* "0 on success" */
		if (UNLIKELY(module->Init(module) != 0)) {
			FreeLibrary(dll);
			return false;
		}
	}

	fprintf(stderr, "[ddw_host] plugins[%d] = \"%s\"\n", nplugins, module->description);

	plugins[nplugins].module = module;
	plugins[nplugins].dll = dll;
	plugins[nplugins].process_max_frames = 1024;

	/* known not to crash */
	if (UNLIKELY(strstr(module->description, "Stereo Tool") != NULL))
		plugins[nplugins].process_max_frames = 4096;

	nplugins += 1;

	return true;
}

static unsigned int
__attribute__((hot))
__attribute__((warn_unused_result))
process_plugin_chunked(const struct plugin_info *plugin,
                       const struct processing_request *request,
                       unsigned int nframes_in,
                       const char *restrict inbuf,
                       char *restrict outbuf)
{
	unsigned int nframes_out = 0;
	unsigned int nframes_in_orig = nframes_in;
	int chunk_nframes_in;
	int chunk_nframes_out;
	static int alignment_msg_seen;

	assert(plugin != NULL);
	assert(plugin->process_max_frames != 0);
	assert(request != NULL);
	assert(PRREQ_IS_VALID(*request));
	assert(nframes_in > 0);
	assert(nframes_in > plugin->process_max_frames);
	assert(inbuf != NULL);
	assert(outbuf != NULL);
	assert(inbuf != outbuf);

	do {
		chunk_nframes_in = MIN(nframes_in, plugin->process_max_frames);

		assert(chunk_nframes_in > 0);

		/*
		 * can this happen? could it cause compatibility issues with plugins? what does alignment even do?
		 * what sample size would be needed to get this if pointers are 32 bits?
		 * 16-bit or 24-bit mono with uneven chunk size?
		 */
		if (UNLIKELY((uintptr_t)(const void * restrict)outbuf % sizeof(void *) != 0 && alignment_msg_seen < 50)) {
			fprintf(stderr, "[ddw_host] note: outbuf not aligned (%dch %dbps)\n", request->channels, request->bitspersample);
			alignment_msg_seen += 1;
		}

		memcpy(outbuf, inbuf,
		    chunk_nframes_in*(request->bitspersample/8)*request->channels);

		chunk_nframes_out = plugin->module->ModifySamples(plugin->module,
		    (short int *)outbuf,
		    chunk_nframes_in,
		    request->bitspersample,
		    request->channels,
		    request->samplerate);

		if (UNLIKELY(chunk_nframes_out < 0))
			chunk_nframes_out = 0;

		assert(chunk_nframes_out <= chunk_nframes_in*BUFFER_SIZE_MULT);

		inbuf += chunk_nframes_in*(request->bitspersample/8)*request->channels;
		outbuf += chunk_nframes_out*(request->bitspersample/8)*request->channels;
		nframes_in -= chunk_nframes_in;
		nframes_out += chunk_nframes_out;
	} while (nframes_in > 0);

	assert(nframes_out <= nframes_in_orig*BUFFER_SIZE_MULT);

	return nframes_out;
}

static unsigned int
__attribute__((hot))
__attribute__((warn_unused_result))
process_plugin_full(const struct plugin_info *plugin,
                    const struct processing_request *request,
                    unsigned int nframes_in,
                    char *restrict buf)
{
	int nframes_out;

	assert(plugin != NULL);
	assert(request != NULL);
	assert(PRREQ_IS_VALID(*request));
	assert(nframes_in > 0);
	assert(buf != NULL);

	nframes_out = plugin->module->ModifySamples(plugin->module,
	    (short int *)buf,
	    nframes_in,
	    request->bitspersample,
	    request->channels,
	    request->samplerate);

	if (UNLIKELY(nframes_out < 0))
		nframes_out = 0;

	assert((unsigned)nframes_out <= nframes_in*BUFFER_SIZE_MULT);

	return nframes_out;
}

static DWORD
__attribute__((hot))
reader_main(void *ud)
{
	struct processing_request request;
	struct processing_response response;
	struct abuffer buffer1 = EMPTY_ABUFFER;
	struct abuffer buffer2 = EMPTY_ABUFFER;
	unsigned int buffer_frames = 4096;
	unsigned int nframes;
	unsigned int i;
	int rv = 1;
	(void)ud;

	assert(nplugins > 0);

	if (UNLIKELY(fixed_mode)) {
		assert(fixed_samplerate > 0);
		assert(fixed_bitspersample > 0);
		assert(fixed_channels > 0);
		request.samplerate = fixed_samplerate;
		request.bitspersample = fixed_bitspersample;
		request.channels = fixed_channels;
	} else {
		request.samplerate = 44100;
		request.bitspersample = 16;
		request.channels = 2;
	}
	abuffer_xgrow(&buffer1, BUFFER_SIZE_MULT*buffer_frames*(request.bitspersample/8)*request.channels);
	abuffer_xgrow(&buffer2, BUFFER_SIZE_MULT*buffer_frames*(request.bitspersample/8)*request.channels);

	for (;;) {
		if (LIKELY(!fixed_mode)) {
			if (UNLIKELY(!read_full(STDIN_FILENO, &request, sizeof(request)))) {
				rv = 0; /* probably eof */
				break;
			}
			assert(PRREQ_IS_VALID(request));
			abuffer_xgrow(&buffer1, request.buffer_size);
			if (UNLIKELY(!read_full(STDIN_FILENO, buffer1.data, request.buffer_size)))
				break;
		} else {
			ssize_t read_rv;
			read_rv = read(STDIN_FILENO, buffer1.data, buffer_frames*(request.bitspersample/8)*request.channels);
			if (UNLIKELY(read_rv <= 0)) {
				rv = (read_rv == EOF) ? 0 : 1;
				break;
			}
			request.buffer_size = (uint64_t)(size_t)read_rv;
			assert(PRREQ_IS_VALID(request));
		}

		nframes = request.buffer_size/(request.bitspersample/8)/request.channels;
		for (i = 0; nframes > 0 && i < nplugins; i++) {
			if (LIKELY(plugins[i].process_max_frames != 0) &&
			    nframes > plugins[i].process_max_frames) {
				abuffer_xgrow(&buffer2, BUFFER_SIZE_MULT*nframes*(request.bitspersample/8)*request.channels);
				nframes = process_plugin_chunked(&plugins[i], &request, nframes, buffer1.data, buffer2.data);
				abuffer_swap(&buffer2, &buffer1);
			} else {
				abuffer_xgrow(&buffer1, BUFFER_SIZE_MULT*nframes*(request.bitspersample/8)*request.channels);
				nframes = process_plugin_full(&plugins[i], &request, nframes, buffer1.data);
			}
			/* processed output is in buffer1 */
		}
		response.buffer_size = nframes*(request.bitspersample/8)*request.channels;

		if (LIKELY(!fixed_mode)) {
			if (UNLIKELY(!write_full(STDOUT_FILENO, &response, sizeof(response))))
				break;
		}

		if (LIKELY(response.buffer_size > 0)) {
			assert(buffer1.size >= response.buffer_size);
			if (UNLIKELY(!write_full(STDOUT_FILENO, buffer1.data, response.buffer_size)))
				break;
		}
	}

	PostThreadMessage(main_thread_id, WM_QUIT, rv, 0);

	return 0;
}

int
__attribute__((hot))
main(int argc, char **argv)
{
	MSG msg;
	int i, status = 0;

	progname = superbasename(argv[0]);
	main_thread_id = GetCurrentThreadId();

	if (UNLIKELY(argc <= 1)) {
		fprintf(stderr, "usage: %s path\\to\\dsp_plugin.dll[:module_index] ...\n", progname);
		status = 1;
		goto exit;
	}

	if (UNLIKELY(getenv("DDW_HOST_FIXED") != NULL)) {
		assert(getenv("SR") != NULL);
		assert(getenv("BPS") != NULL);
		assert(getenv("CH") != NULL);
		fixed_samplerate = (uint32_t)atoi(getenv("SR"));
		fixed_bitspersample = (uint8_t)atoi(getenv("BPS"));
		fixed_channels = (uint8_t)atoi(getenv("CH"));
		assert(fixed_samplerate != 0);
		assert(fixed_bitspersample != 0);
		assert(fixed_channels != 0);
		fixed_mode = true;
	}

	for (i = 1; i < argc; i++) {
		char *filename;
		int index;
		BOOL ok;
		if (UNLIKELY(!parse_plugin_path(argv[i], &filename, &index)))
			goto exit;
		ok = load_plugin_from_path(filename, index);
		free(filename);
		if (UNLIKELY((status = !ok)))
			goto exit;
	}

	/* don't mangle stdio */
	_setmode(STDIN_FILENO, _O_BINARY);
	_setmode(STDOUT_FILENO, _O_BINARY);

	/* initialize the event loop before starting the thread */
	PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE);

	reader_thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)reader_main, NULL, 0, NULL);
	if (UNLIKELY(reader_thread == NULL)) {
		status = 1;
		goto exit;
	}

	for (i = 0; (unsigned)i < nplugins; i++) {
		if (LIKELY(plugins[i].module->Config != NULL))
			plugins[i].module->Config(plugins[i].module);
	}

	while (LIKELY(GetMessage(&msg, NULL, 0, 0) > 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	if (LIKELY(msg.message == WM_QUIT))
		status = msg.wParam;

exit:
	if (LIKELY(reader_thread != NULL)) {
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		WaitForSingleObject(reader_thread, INFINITE);
	}

	for (i = nplugins - 1; i >= 0; i--) {
		if (LIKELY(plugins[i].module->Quit != NULL))
			plugins[i].module->Quit(plugins[i].module);
		FreeLibrary(plugins[i].dll);
	}

	return status;
}
