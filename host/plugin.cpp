#include <cassert>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <memory>

#include <unistd.h>

#include "../common.h"
#include "plugin.h"

#include "dll.h"

#include <windows.h>
#include "Winamp/DSP.H"

namespace fs = std::filesystem;

// todo: c++ify
static bool parse_plugin_path(const char *path, char **filename, int *index) {
	char *colon;

	*filename = strdup(path);
	*index = -1;
	if (*filename == NULL)
		return false;

	colon = strrchr(path, ':');
	if (colon != NULL && isdigit(*(colon+1))) {
		(*filename)[colon - path] = '\0';
		/* this suck */
		errno = 0;
		*index = (int)strtol(colon + 1, NULL, 10);
		if (errno == ERANGE || (*index == 0 && !(*(colon+1) == '0' && *(colon+2) == '\0'))) {
			free(*filename);
			return false;
		}
	}

	return true;
}

plugin_info::plugin_info(const char *path, int module_index) {
	load(path, module_index);
}

plugin_info::plugin_info(const char *path) {
	char *filename;
	int index;
	if (!parse_plugin_path(path, &filename, &index)) {
		throw std::runtime_error("invalid plugin path");
	}
	try {
		load(filename, index);
		// why the fug can't i call the other constructor from this one
		// needed to make load() its own function
	} catch (const std::exception& e) {
		free(filename);
		throw;
	}
	free(filename);
}

void plugin_info::load(const char *path, int module_index) {

	dll_name = fs::path(path).stem().string();
	dll = std::make_unique<Dll>(path);

	winampDSPGetHeaderType get_header;
	get_header = (winampDSPGetHeaderType)(void(*)(void))dll->getProc("winampDSPGetHeader2");
	if (get_header == nullptr) {
		throw std::runtime_error("winampDSPGetHeader2 function not found");
	}

	winampDSPHeader *header = get_header(GetDesktopWindow());
	if (header == nullptr || header->getModule == nullptr) {
		throw std::runtime_error("winampDSPGetHeader2 returned invalid result");
	}

	winampDSPModule *module;
	if (module_index < 0) {
		if ((module = header->getModule(0)) != nullptr) {
			module_index = 0;
		} else if ((module = header->getModule(1)) != nullptr) {
			module_index = 1;
		}
	} else {
		module = header->getModule(module_index);
	}

	if (module == nullptr) {
		throw std::runtime_error("dsp module not found");
	}

	module->hDllInstance = static_cast<HINSTANCE>(dll->getHandle());
	module->hwndParent = GetDesktopWindow();
	if (module->Init != nullptr) {
		// "0 on success"
		if (module->Init(module) != 0) {
			throw std::runtime_error("module Init() failed");
		}
	}

	fprintf(stderr, "Loaded plugin:\n");
	fprintf(stderr, "  Path: %s\n", path);
	fprintf(stderr, "  Index: %d\n", module_index);
	fprintf(stderr, "  Name: %s\n", header->description);
	fprintf(stderr, "  Description: %s\n", module->description);

	this->module = module;

	// lol
	// i duckduckgoed "winamp source code"
	// it had been leaked 7 days prior and there was a /g/ thread with that exact title
	// (it even had a download link)

	// the default in winamp seems to be 576 frames
	// something to do with mp3 decoding
	this->process_max_frames = 576;

	// winamp supports values up to 2 (200% stretch)
	this->max_stretch_mult = TWO;

	if (strstr(module->description, "Freeverb") != nullptr) {
		// 4096 freezes
		// 2048/1024 causes stack overflow(?) when the first file played is 24-bit
		// even if it's converted to 16-bit
		// why does it do this?? i have no idea
		// 576 seems to work reliably
		// todo: replicate the crash in test.sh
		this->process_max_frames = 576;
		this->max_stretch_mult = ONE;
	}
	if (strstr(module->description, "PaceMaker") != nullptr) {
		// 4096 crashes
		// 2048 crashes sometimes
		this->process_max_frames = 576*3;
		this->max_stretch_mult = TWO;
	}
	if (strstr(module->description, "Stereo Tool") != nullptr) {
		this->process_max_frames = 0; // hard worker
		this->max_stretch_mult = ONE;
	}
}

plugin_info::~plugin_info() {
	if (this->module != nullptr) {
		if (this->module->Quit != nullptr) {
			this->module->Quit(this->module);
		}
		this->module = nullptr;
	}
}

// randomly disable optimizations to make it test more code paths
#define testing 0

[[gnu::hot]]
void plugin_info::process(const struct processing_request& request,
                          unsigned int& nframes,
                          std::vector<char>& databuf,
                          std::vector<char>& tmpbuf) {
#if testing
# define maybe() ((rand()&1)!=0)
	auto pmf = process_max_frames;
	auto msm = max_stretch_mult;
	if (process_max_frames > 576 && maybe()) {
		if (process_max_frames >= 576*16 && maybe()) {
			process_max_frames = 576*8;
		} else if (process_max_frames >= 576*8 && maybe()) {
			process_max_frames = 576*4;
		} else if (process_max_frames >= 576*4 && maybe()) {
			process_max_frames = 576*2;
		} else {
			process_max_frames = 576;
		}
	}
	if (max_stretch_mult == ONE && maybe()) {
		max_stretch_mult = TWO;
	}
# undef maybe
#endif
	if (this->can_process_full(nframes)) {
		databuf.resize(request.frames2bytes(nframes)*max_stretch_mult);
		nframes = process_inplace(request, nframes, databuf.data());
	} else if (max_stretch_mult == ONE) {
		nframes = process_chunked(request, nframes, databuf.data(), databuf.data());
	} else {
		tmpbuf.resize(request.frames2bytes(nframes)*max_stretch_mult);
		nframes = process_chunked(request, nframes, databuf.data(), tmpbuf.data());
		std::swap(databuf, tmpbuf);
	}
#if testing
	process_max_frames = pmf;
	max_stretch_mult = msm;
#endif
}

#undef testing

[[gnu::hot]]
[[gnu::stack_protect]]
unsigned int plugin_info::process_inplace(const struct processing_request& request,
                                          unsigned int nframes_in,
                                          char *buf) {
	int nframes_out = this->module->ModifySamples(this->module,
	    (short int *)buf,
	    nframes_in,
	    request.bitspersample,
	    request.channels,
	    request.samplerate);
	nframes_out = std::max(0, nframes_out);
	if ((unsigned int)nframes_out > nframes_in*max_stretch_mult) [[unlikely]] {
		fprintf(stderr, "dll_name = %s\n", dll_name.c_str());
		fprintf(stderr, "nframes_in = %u\n", nframes_in);
		fprintf(stderr, "nframes_out = %u\n", nframes_out);
		fprintf(stderr, "max_stretch_mult = %d\n", max_stretch_mult);
		throw std::runtime_error("plugin stretched sound beyond allowed limit");
	}

	return nframes_out;
}

[[gnu::hot]]
unsigned int plugin_info::process_chunked(const struct processing_request& request,
                                          unsigned int nframes,
                                          const char *in,
                                          char *out) {
	size_t fs = request.frame_size();
	const char *in_end = in + fs*nframes;
	char *out_start = out;

	do {
		unsigned int chunk_in = std::min(this->process_max_frames, (in_end-in)/fs);

		if (in != out) [[likely]] memmove(out, in, fs*chunk_in);
		unsigned int chunk_out = process_inplace(request, chunk_in, out);

		in += fs*chunk_in;
		out += fs*chunk_out;
	} while (in != in_end);

	return (out-out_start)/fs;
}

bool plugin_info::can_process_full(unsigned int nframes) {
	return this->process_max_frames == 0 || nframes <= this->process_max_frames;
}

void plugin_info::maybe_config() {
	if (this->module->Config == nullptr) {
		return;
	}

	if (strstr(module->description, "PaceMaker") != nullptr) {
		return;
	}

	this->module->Config(this->module);
}
