#include <cassert>

#include <fcntl.h>
#include <windows.h>

#include "../common.h"
#include "plugin.h"
#include "thread.h"

athread::athread(std::vector<plugin_info>& p, int input, int output)
	: plugins(p)
	, main_thread_id(GetCurrentThreadId())
	, in_fd(input)
	, out_fd(output) {
	// don't mangle data
	_setmode(in_fd, _O_BINARY);
	_setmode(out_fd, _O_BINARY);

	// initialize the event loop first
	MSG msg;
	PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE);

	// i regret using std::thread now since it has no option to embiggen the stack
	// it might be interesting for compatibility testing
	// (if something crashes with a stack overflow on a too-big input)
	thread = std::thread(thread_runner, this);
}

athread::~athread() {
	close(in_fd);
	close(out_fd);
	thread.join();
}

void athread::thread_runner() {
	int rv = 0;
	fprintf(stderr, "[thread] enter loop\n");
	try {
		this->thread_main();
	} catch (const std::exception& e) {
		fprintf(stderr, "dsp thread error: %s\n", e.what());
		rv = 1;
		// todo: print the dll name
	}
	fprintf(stderr, "[thread] exit loop\n");
	fprintf(stderr, "[thread] rv = %d\n", rv);
	PostThreadMessage(main_thread_id, WM_QUIT, rv, 0);
}

[[gnu::hot]]
void athread::thread_main() {
	struct processing_request request;
	struct processing_response response;
	std::vector<char> databuf;
	std::vector<char> tmpbuf;
	unsigned int reasonable_buffer_frames = 4096;

	bool fixed_mode = (getenv("DDW_HOST_FIXED") != nullptr);
	if (fixed_mode) {
		request.samplerate = std::stoi(getenv("SR"));
		request.bitspersample = std::stoi(getenv("BPS"));
		request.channels = std::stoi(getenv("CH"));
		if (getenv("REASONABLE_BUFFER") != nullptr) {
			assert(std::stoi(getenv("REASONABLE_BUFFER")) > 0);
			reasonable_buffer_frames = std::stoi(getenv("REASONABLE_BUFFER"));
		}
		databuf.resize(request.frames2bytes(reasonable_buffer_frames));
	}

	for (;;) {
		// this sucks
		if (!fixed_mode) [[likely]] {
			ssize_t read_rv = read(in_fd, &request, sizeof(request));
			if (read_rv == -1) [[unlikely]] throw std::runtime_error("read error 1");
			if (read_rv == 0) [[unlikely]] return;
			if (read_rv != sizeof(request) || !request.ok()) [[unlikely]] {
				throw std::runtime_error("read error 2");
			}
			databuf.resize(request.buffer_size);
			if (!read_full(in_fd, databuf.data(), request.buffer_size)) [[unlikely]] {
				throw std::runtime_error("read error 3");
			}
		} else {
			// always read the full buffer size to get predictable output from plugins
			char *data = databuf.data();
			char *end = data + request.frames2bytes(reasonable_buffer_frames);
			while (true) {
				ssize_t rv = read(in_fd, data, end - data);
				if (rv == -1) [[unlikely]] throw std::runtime_error("read error 1");
				if (rv == 0) [[unlikely]] { // eof
					if (data == databuf.data()) [[unlikely]] return;
					else break; // read some stuff, process it
				};
				data += (size_t)rv;
				if (data == end) [[likely]] break;
			}
			request.buffer_size = data - databuf.data();
			if (!request.ok()) [[unlikely]] {
				throw std::runtime_error("read error 2");
			}
		}

		unsigned int nframes = request.nframes();
		for (unsigned int i = 0; i < plugins.size() && nframes > 0; i++) {
			auto& plugin = plugins[i];
			plugin.process(request, nframes, databuf, tmpbuf);
		}
		response.buffer_size = request.frames2bytes(nframes);

		if (!fixed_mode) [[likely]] {
			if (!write_full(out_fd, &response, sizeof(response))) [[unlikely]] {
				throw std::runtime_error("write error");
			}
		}

		if (response.buffer_size > 0) [[likely]] {
			if (!write_full(out_fd, databuf.data(), response.buffer_size)) [[unlikely]] {
				throw std::runtime_error("failed to write output buffer");
			}
		}
	}
}
