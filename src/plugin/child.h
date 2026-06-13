#pragma once

#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>

struct Child
{
	struct Ddw *pl;

	pid_t pid;
	int fds[2];

	/* if the child is currently running, this is the value of the
	   host command that was used to start it. */
	char *host_cmd;

	/* set to true if the child should be restarted. */
	bool config_changed;
};

#define CHILD_INIT \
	((struct Child){ \
		.pid = -1, \
		.fds = {-1, -1}, \
	})
