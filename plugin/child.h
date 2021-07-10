#pragma once

#include <stdbool.h>

#include <deadbeef/deadbeef.h>

struct child {
	pid_t pid;
	int fds[2];

#define SUCCESS_LIMIT 10
#define FAILURE_LIMIT 3
	int successes;
	int failures;

	// an error happened and stdin/out may be in an inconsistent state
	bool killmenow;

	struct ddw *pl;
};
#define CHILD_INITIALIZER(plz) (struct child){.pid = -1, .fds = {-1, -1}, .pl = plz}

/// chldinit.c

bool child_start(struct child *self);
bool child_stop(struct child *self);

void child_record_success(struct child *self);
void child_record_failure(struct child *self);

bool child_is_doomed(struct child *self);
void child_reset_failures(struct child *self);

/// chldproc.c

int child_process_samples(struct child *self,
                          ddb_waveformat_t *fmt,
                          const ddb_waveformat_t *nextfmt,
                          char *data,
                          int frames_in,
                          size_t datacap);
