#define _GNU_SOURCE /* closefrom, strdup */
#include "chldinit.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "main.h"

static void close_extra_fds(void);
static bool wait_hangup(int fd, int ms);

/*
Build the shell command string for the child process to run.
This also sets self->host_cmd.
The returned string is malloced and should be freed by the caller.
*/
static char *get_cmd(
	struct Child *self,
	const char   *dll)
{
	const char *hostcmd;
	char *p;
	int plen;
	pid_t pid;

	if (!self->pl->dll)
		return NULL;

#define FMTARGS \
	    "export DDW_SHM_NAME=%s%d; exec %s %s", \
	    SHM_FILENAME_BASE, \
	    pid, \
	    hostcmd, \
	    self->pl->dll \

	p = NULL;

	/* lock for conf_get_str_fast(). */
	deadbeef->conf_lock();
	{
		pid = getpid();

		hostcmd = deadbeef->conf_get_str_fast(
		    CONFKEY_HOSTCMD,
		    CONFVAL_HOSTCMD);

		self->host_cmd = strdup(hostcmd);
		/* clumsy. skip the other thing if this failed. */
		if (!self->host_cmd)
			goto unlock;

		plen = snprintf(NULL, 0, FMTARGS);
		if (plen > 0 && plen < INT_MAX)
		{
			p = (char *)malloc(plen+1);
			if (p)
				snprintf(p, plen+1, FMTARGS);
		}

		hostcmd = NULL;
unlock:;
	}
	deadbeef->conf_unlock();

	return p;
}

bool child_start(struct Child *self)
{
	int input[2];
	int output[2];
	char *cmd;
	pid_t pid;

	assert(self->pid < 0);
	assert(self->fds[0] < 0);
	assert(self->fds[1] < 0);
	assert(!self->host_cmd);

	input[0] = -1;
	input[1] = -1;
	output[0] = -1;
	output[1] = -1;
	cmd = NULL;

	if (pipe(input) < 0 ||
	    pipe(output) < 0)
	{
		perror("pipe");
		goto parent_err;
	}

	cmd = get_cmd(self, self->pl->dll);
	if (!cmd)
	{
		fprintf(stderr, "failed to build child command\n");
		goto parent_err;
	}

	pid = fork();

	if (pid < 0)
	{
		perror("fork");
		goto parent_err;
	}
	else if (!pid)
	{
		if (dup2(input[0], STDIN_FILENO) < 0 ||
		    dup2(output[1], STDOUT_FILENO) < 0)
			_exit(errno);
		close_extra_fds();
		execl("/bin/sh", "sh", "-c", cmd, NULL);
		_exit(errno);
	}
	else
	{
		close(input[0]);
		close(output[1]);

		free(cmd);

		self->pid = pid;
		self->fds[0] = output[0];
		self->fds[1] = input[1];

		return true;
	}

parent_err:

	if (input[0] >= 0) close(input[0]);
	if (input[1] >= 0) close(input[1]);

	if (output[0] >= 0) close(output[0]);
	if (output[1] >= 0) close(output[1]);

	free(cmd);

	free(self->host_cmd);
	self->host_cmd = NULL;

	return false;
}

void child_stop(struct Child *self)
{
	int killcnt;

	if (self->pid < 0)
		return;

	assert(self->fds[0] >= 0);
	assert(self->fds[1] >= 0);
	assert(self->host_cmd);

	fprintf(stderr, "dsp_winamp: stopping child...\n");

	fprintf(stderr, "dsp_winamp: closing child stdin\n");

	/* close their stdin. this should
	   cause the process to exit normally. */
	close(self->fds[1]);
	self->fds[1] = -1;

	killcnt = 0;

	for (;;)
	{
		bool maybe_exited;
		int waitflag;
		int waitstatus;
		int waitrv;

		maybe_exited = wait_hangup(self->fds[0], 1000);

		/* use WNOHANG if they might not have exited yet */
		waitflag = (maybe_exited) ? 0 : WNOHANG;

		while ((waitrv =
		    waitpid(self->pid, &waitstatus, waitflag)) < 0
		    && errno == EINTR)
			continue;

		if (waitrv < 0)
		{
			fprintf(stderr,
			    "waitpid %d: error %d\n",
			    self->pid,
			    errno);
			break;
		}

		/* still running */
		if (!waitrv)
		{
			int killrv;

			/* first signal: SIGTERM */
			if (!killcnt)
			{
				fprintf(stderr,
				    "dsp_winamp: sending SIGTERM\n");

				while ((killrv =
				    kill(self->pid, SIGTERM)) < 0
				    && errno == EINTR)
					continue;

				if (killrv < 0 && errno != ESRCH)
					fprintf(stderr,
					    "kill %d: error %d\n",
					    self->pid,
					    errno);

				killcnt += 1;
				continue;
			}

			/* later send SIGKILL */
			if (killcnt == 1)
			{
				fprintf(stderr,
				    "dsp_winamp: sending SIGKILL\n");

				while ((killrv =
				    kill(self->pid, SIGKILL)) < 0
				    && errno == EINTR)
					continue;

				if (killrv < 0 && errno != ESRCH)
					fprintf(stderr,
					    "kill %d: error %d\n",
					    self->pid,
					    errno);

				killcnt += 1;
				continue;
			}

			/* signals didn't work, just get out of here */

			fprintf(stderr,
			    "dsp_winamp: gave up waiting for"
			    " child to exit\n");

			break;
		}

		/* something happened to the child */
		if (waitrv == self->pid)
		{
			/* exited normally or by signal */
			if (WIFEXITED(waitstatus) ||
			    WIFSIGNALED(waitstatus))
			{
				if (WIFEXITED(waitstatus))
					fprintf(stderr,
					    "dsp_winamp: child exited"
					    " with status %d\n",
					    WEXITSTATUS(waitstatus));

				if (WIFSIGNALED(waitstatus))
					fprintf(stderr,
					    "dsp_winamp: child was"
					    " killed by signal %d\n",
					    WTERMSIG(waitstatus));

				break;
			}

			/* something else */

			if (WIFSTOPPED(waitstatus))
				fprintf(stderr,
				    "dsp_winamp: child was stopped"
				    " by signal %d\n",
				    WSTOPSIG(waitstatus));

			if (WIFCONTINUED(waitstatus))
				fprintf(stderr,
				    "dsp_winamp: child was"
				    " continued\n");

			continue;
		}

		assert(0); /* unreachable */
	}

	fprintf(stderr, "dsp_winamp: stopping child... done\n");

	self->pid = -1;
	close(self->fds[0]);
	self->fds[0] = -1;

	free(self->host_cmd);
	self->host_cmd = NULL;
}

#if !defined(__GLIBC_PREREQ)
# define __GLIBC_PREREQ(x, y) 0
#endif

static void close_extra_fds(void)
{
#if __GLIBC_PREREQ(2, 34)
	closefrom(3);
#else
	struct dirent **namelist;
	int count;

	count = scandir("/proc/self/fd", &namelist, NULL, NULL);
	if (count < 0)
		return;

	for (int i = 0; i < count; i++)
	{
		int fd = atoi(namelist[i]->d_name);
		if (fd >= 3)
			close(fd);
		free(namelist[i]);
	}

	free(namelist);
#endif
}

static bool wait_hangup(int fd, int timeout_ms)
{
	struct pollfd pfd;
	int pollrv;

	assert(fd >= 0);

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = fd;
	pfd.events = 0;

	while ((pollrv = poll(&pfd, 1, timeout_ms)) < 0
	    && errno == EINTR)
		continue;

	/* other end closed */
	if (pollrv == 1 && (pfd.revents & POLLHUP))
		return true;

	/* wait timed out */
	if (!pollrv)
		return false;

	/* shouldn't happen */
	fprintf(stderr,
	    "wait_hangup: unknown poll result"
	    " (pollrv=%d errno=%d revents=%d)\n",
	    pollrv, errno, pfd.revents);
	return false;
}
