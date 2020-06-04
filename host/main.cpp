#include <fcntl.h>
#include <unistd.h>

#include <cassert>

#include <windows.h>

#include "plugin.h"
#include "thread.h"

[[gnu::hot]]
static int mainloop() {
	MSG msg;
	int status = 0;

	fprintf(stderr, "[main] enter main loop\n");
	while (GetMessage(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	fprintf(stderr, "[main] exit main loop\n");
	if (msg.message == WM_QUIT) {
		status = msg.wParam; // should this be in the loop?
	}
	fprintf(stderr, "[main] rv = %d\n", status);

	return status;
}

int main(int argc, char **argv) {

	if (argc <= 1) {
		fprintf(stderr, "usage: ddw_host.exe path\\to\\dsp_plugin.dll[:module_index] ...\n");
		return 1;
	}

	srand(time(0));

	int nul = open("/dev/null", O_RDWR);
	assert(nul != -1);

	int in = dup(0); assert(in != -1); dup2(nul, 0); close(nul);
	int out = dup(1); assert(out != -1); dup2(2, 1);

	std::vector<plugin_info> plugins;
	for (int i = 1; i < argc; i++) {
		try {
			plugins.emplace_back(argv[i]);
		} catch (const std::exception& e) {
			fprintf(stderr, "error: failed to load plugin '%s': %s\n", argv[i], e.what());
			return 1;
		}
	}

	std::unique_ptr<athread> thread;
	try {
		thread = std::make_unique<athread>(plugins, in, out);
	} catch (const std::exception& e) {
		fprintf(stderr, "error: failed to start thread: %s\n", e.what());
		return 1;
	}

	for (auto& plugin : plugins) {
		std::thread t([&plugin](){
			plugin.maybe_config();
		});
		t.detach();
	}

	return mainloop();
}
