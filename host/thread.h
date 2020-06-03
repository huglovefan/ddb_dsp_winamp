#pragma once

#include <thread>
#include <vector>
#include "plugin.h"

class athread {
private:
	std::vector<plugin_info>& plugins;
	int32_t main_thread_id;
	std::thread thread;
	int in_fd;
	int out_fd;

	void thread_runner();
	void thread_main();
public:
	athread(std::vector<plugin_info>& p, int in_fd, int out_fd);
	~athread();

	athread(const athread& other) = delete;
	athread& operator=(const athread& other) = delete;

	athread(athread&& other) noexcept = delete;
	athread& operator=(athread&& other) noexcept = delete;
};
