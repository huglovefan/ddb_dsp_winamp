#pragma once

#include <memory>
#include <string>
#include <vector>
#include "dll.h"

struct winampDSPModule;

enum one_or_two {
	ONE = 1,
	TWO = 2,
};

class plugin_info {
private:
	std::string          dll_name;
	winampDSPModule      *module;
	std::unique_ptr<Dll> dll; // retarted pointer toa pointer
	unsigned int         process_max_frames;
	enum one_or_two      max_stretch_mult;

	void load(const char *, int);

	unsigned int process_inplace(const struct processing_request& request,
	                             unsigned int nframes_in,
	                             char *buf);
	unsigned int process_chunked(const struct processing_request& request,
	                             unsigned int nframes_in,
	                             const char *inbuf,
	                             char *outbuf);
	bool can_process_full(unsigned int nframes);
public:
	plugin_info(const char *path);
	plugin_info(const char *path, int idx);

	~plugin_info();

	plugin_info(const plugin_info& other) = delete;
	plugin_info& operator=(const plugin_info& other) = delete;

	plugin_info(plugin_info&& other) noexcept
		: dll_name(std::move(other.dll_name))
		, module(std::exchange(other.module, nullptr)) // <-- important
		, dll(std::move(other.dll))
		, process_max_frames(std::exchange(other.process_max_frames, 576))
		, max_stretch_mult(std::exchange(other.max_stretch_mult, TWO)) {
	}
	plugin_info& operator=(plugin_info&& other) noexcept = delete;

	void process(const struct processing_request& request,
	             unsigned int& nframes,
	             std::vector<char>& input,
	             std::vector<char>& output);

	void maybe_config();
};
