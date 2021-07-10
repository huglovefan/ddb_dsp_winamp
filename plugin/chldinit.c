#include "child.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "plugin.h"

// -----------------------------------------------------------------------------

static void
close_extra(void)
{
	struct dirent **namelist;
	int count;

	count = scandir("/proc/self/fd", &namelist, NULL, NULL);
	if (count == -1)
		return;

	for (int i = 0; i < count; i++) {
		int fd = atoi(namelist[i]->d_name);
		if (fd > 2)
			close(fd);
		free(namelist[i]);
	}

	free(namelist);
}

bool
child_start(struct child *self)
{
	char *host = NULL;
	int stdin[2] = {-1, -1},
	    stdout[2] = {-1, -1}; // {read_end, write_end}
	pid_t pid = -1;

	assert(self->pid == -1);

	// should've closed these
	assert(self->fds[0] == -1);
	assert(self->fds[1] == -1);

	if (pipe(stdin) < 0 || pipe(stdout) < 0) {
		perror("dsp_winamp: pipe");
		goto failed;
	}

	deadbeef->conf_lock();
	host = strdup(deadbeef->conf_get_str_fast("ddw.host_cmd", "ddw_host.exe"));
	deadbeef->conf_unlock();
	assert(host != NULL);

	pid = fork();
	if (pid < 0) {
		perror("dsp_winamp: fork");
failed:
		close(stdin[0]);
		close(stdin[1]);
		close(stdout[0]);
		close(stdout[1]);
		free(host);
		return false;
	} else if (pid == 0) {
		char *cmd;
		size_t bufsz;
		if (dup2(stdin[0], STDIN_FILENO) < 0 ||
		    dup2(stdout[1], STDOUT_FILENO) < 0) {
			perror("dsp_winamp: dup2");
			_exit(EXIT_FAILURE);
		}
		close_extra();
		bufsz = strlen("exec ") + strlen(host) + strlen(" ") + strlen(self->pl->dll) + sizeof('\0');
		cmd = alloca(bufsz);
		snprintf(cmd, bufsz, "exec %s %s", host, self->pl->dll);
		execl("/bin/sh", "sh", "-c", cmd, NULL);
		perror("dsp_winamp: execl");
		_exit(EXIT_FAILURE);
	} else {
		close(stdin[0]);
		close(stdout[1]);
		free(host);

		self->pid = pid;
		self->fds[0] = stdout[0];
		self->fds[1] = stdin[1];

		return true;
	}
}

// -----------------------------------------------------------------------------

//
// try-wait the child with a timeout by polling its stdout
//
static bool
trywait(struct child *self, int ms)
{
	struct pollfd pfd = {
		.fd = self->fds[0],
		.events = 0,
	};
	int pollrv;
	int pollerrno;
again:
	pollrv = poll(&pfd, 1, ms);
	if (pollrv == -1 && errno == EINTR)
		goto again;

	// success: other end of the pipe is closed
	if (pollrv == 1 && pfd.revents&POLLHUP)
		return true;

	// wait timed out
	if (pollrv == 0)
		return false;

	// should NEVER get here.....
	pollerrno = errno;
	fprintf(stderr, "dsp_winamp: error: trywait() got an unknown result\n");
	fprintf(stderr, "  pollrv=%d revents=%d errno=%d\n",
	    pollrv, pfd.revents, pollerrno);

	abort();
}

bool
child_stop(struct child *self)
{
	int waitrv;
	int waitstatus;
	int attempt = 0;

	if (self->pid == -1)
		return true;

	// close their stdin
	if (self->fds[1] != -1) {
		close(self->fds[1]);
		self->fds[1] = -1;
	}
again:
	if (trywait(self, 1000))
		waitrv = waitpid(self->pid, &waitstatus, 0);
	else
		waitrv = waitpid(self->pid, &waitstatus, WNOHANG);

	if (waitrv == -1) {
		if (errno == ECHILD) {
			self->pid = -1;
			goto out;
		}

		// ?
		perror("dsp_winamp: waitpid");
		abort();
	}

	// used WNOHANG (trywait timed out) but the child hasn't yet exited
	if (waitrv == 0) {
		if (attempt == 0) {
			fprintf(stderr, "dsp_winamp: child didn't exit in 1000ms, sending SIGTERM...\n");
			kill(self->pid, SIGTERM);
			attempt++;
			goto again;
		} else if (attempt == 1) {
			fprintf(stderr, "dsp_winamp: child didn't exit in 2000ms, sending SIGKILL...\n");
			kill(self->pid, SIGKILL);
			attempt++;
			goto again;
		} else {
			fprintf(stderr, "dsp_winamp: gave up waiting for child to exit!\n");
		}
		goto out;
	}

	// print the status
	if (WIFEXITED(waitstatus)) {
		fprintf(stderr, "dsp_winamp: child exited with status %d\n",
		    WEXITSTATUS(waitstatus));
	}
	if (WIFSIGNALED(waitstatus)) {
		fprintf(stderr, "dsp_winamp: child was killed by signal %d\n",
		    WTERMSIG(waitstatus));
	}

	self->pid = -1;
out:
	if (self->pid != -1)
		return false;

	self->killmenow = false;

	if (self->fds[0] != -1) {
		close(self->fds[0]);
		self->fds[0] = -1;
	}

	return true;
}

// -----------------------------------------------------------------------------

//
// intended usage:
//
// call child_record_success() when processing succeeds and
//  child_record_failure() when it fails
//
// on any failure, if child_is_doomed() returns true, then playback should be
//  paused instead of trying to restart the plugin any more
//
// https://www.trumparea.com/_pics/the-decision-trump-makes-every-day.jpg
//

void
child_record_success(struct child *self)
{
	// already successful
	if (self->successes >= SUCCESS_LIMIT)
		return;

	self->successes += 1;

	// just achieved peak success -> forgive all their previous failures
	if (self->successes == SUCCESS_LIMIT)
		self->failures = 0;
}

void
child_record_failure(struct child *self)
{
	// failure counter already full
	if (self->failures >= FAILURE_LIMIT)
		return;

	// note: this resets consecutive successes too

	self->failures += 1;
	self->successes = 0;
}

bool
child_is_doomed(struct child *self)
{
	return (self->failures >= FAILURE_LIMIT);
}

void
child_reset_failures(struct child *self)
{
	self->failures = 0;
	self->successes = 0;
}
