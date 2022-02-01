module ddw.plugin.chldinit;

import core.stdc.errno;
import core.stdc.stdio;
import core.stdc.stdlib;
import core.sys.posix.dirent;
import core.sys.posix.poll;
import core.sys.posix.sys.wait;
import core.sys.posix.unistd;
import ddw.common.gc;
import ddw.plugin.child;
import ddw.plugin.main;

bool child_start(Child* self)
{
	assert(self.pid == -1);

	// pipe = [read_end, write_end]
	//         ↓ read()  ↑ write()

	int[2] input = [-1, -1];
	if (pipe(input) != 0)
	{
		perror("pipe");
		return false;
	}

	int[2] output = [-1, -1];
	if (pipe(output) != 0)
	{
		perror("pipe");
		close(input[0]);
		close(input[1]);
		return false;
	}

	deadbeef.conf_lock();
	string host = gcstrdup(deadbeef.conf_get_str_fast("ddw.host_cmd", "ddw_host.exe"));
	deadbeef.conf_unlock();

	char[] cmd = gcprintf("exec %.*s %.*s",
		cast(int)host.length, host.ptr,
		cast(int)self.pl.dll.length, self.pl.dll.ptr);

	pid_t pid = fork();
	if (pid == -1)
	{
		perror("fork");
		close(input[0]);
		close(input[1]);
		close(output[0]);
		close(output[1]);
		return false;
	}
	else if (pid == 0)
	{
		if (dup2(input[0], STDIN_FILENO) < 0) goto Lchldfail;
		if (dup2(output[1], STDOUT_FILENO) < 0) goto Lchldfail;
		close_extra();
		execl("/bin/sh", "sh", "-c".ptr, cmd.ptr, null);
Lchldfail:
		_exit(errno);
		asm { ud2; }
	}
	else
	{
		close(input[0]);
		close(output[1]);

		self.pid = pid;
		self.fds[0] = output[0];
		self.fds[1] = input[1];

		return true;
	}
}

void child_stop(Child* self)
{
	if (self.pid == -1)
		return;

	debug assert(self.fds[0] != -1);
	debug assert(self.fds[1] != -1);

	// close their stdin
	// this should cause them to eventually exit
	close(self.fds[1]); self.fds[1] = -1;

	int killcnt = 0;
	for (;;)
	{
		int waitstatus = void;
		int waitrv = waitpid(self.pid, &waitstatus,
			(trywait(self, 1000)) ? 0 : WNOHANG);

		if (waitrv == -1)
		{
			if (errno == EINTR) // wait interrupted by signal
				continue;

			perror("waitpid");
			break;
		}
		else if (waitrv == 0)
		{
			if (killcnt == 0)
			{
				printf("dsp_winamp: sending SIGTERM\n");
				if (kill(self.pid, SIGTERM) != 0 && errno != ESRCH)
					perror("kill");
				killcnt += 1;
				continue;
			}
			if (killcnt == 1)
			{
				printf("dsp_winamp: sending SIGKILL\n");
				if (kill(self.pid, SIGKILL) != 0 && errno != ESRCH)
					perror("kill");
				killcnt += 1;
				continue;
			}

			printf("dsp_winamp: gave up waiting for child to exit\n");
			break;
		}
		else if (waitrv == self.pid)
		{
			if (WIFEXITED(waitstatus))
				printf("dsp_winamp: child exited with status %d\n", WEXITSTATUS(waitstatus));
			if (WIFSIGNALED(waitstatus))
				printf("dsp_winamp: child was killed by signal %d\n", WTERMSIG(waitstatus));

			break;
		}
		assert(0, "unreachable");
	}

	self.pid = -1;
	close(self.fds[0]); self.fds[0] = -1;
}

void child_record_success(Child* self)
{
	// already successful
	if (self.successes >= SUCCESS_LIMIT)
		return;

	self.successes += 1;

	// just achieved peak success -> forgive all their previous failures
	if (self.successes == SUCCESS_LIMIT)
		self.failures = 0;
}

void child_record_failure(Child* self)
{
	// failure counter already full
	if (self.failures >= FAILURE_LIMIT)
		return;

	// note: this resets consecutive successes too

	self.failures += 1;
	self.successes = 0;
}

bool child_is_doomed(const(Child)* self)
{
	return (self.failures >= FAILURE_LIMIT);
}

void child_reset_failures(Child* self)
{
	self.failures = 0;
	self.successes = 0;
}

// -----------------------------------------------------------------------------

private:

extern(C) int scandir(const(char)*, dirent***, void*, void*) nothrow @nogc;

/**
 * close all open fds above 2
 * 
 * bug: scandir isn't safe to be called after fork
 */
void close_extra() nothrow @nogc
{
	dirent** namelist = void;
	int count = scandir("/proc/self/fd", &namelist, null, null);
	if (count == -1)
		return;

	foreach (i; 0..cast(size_t)count)
	{
		int fd = atoi(namelist[i].d_name.ptr);
		if (fd > 2)
			close(fd);
		free(namelist[i]);
	}

	free(namelist);
}

/**
 * try-wait the child process by using poll() on their stdout
 * 
 * when they exit and their end of the stdout pipe gets closed, poll() on the
 *  other end should return POLLHUP
 * 
 * polling the fd with empty "pollfd.events" does nothing but wait for the
 *  timeout or POLLHUP
 */
bool trywait(const(Child)* self, int ms)
{
	debug assert(self.pid != -1);
	debug assert(self.fds[0] != -1);

	pollfd pfd = {
		fd: self.fds[0],
		events: 0,
	};
	for (;;)
	{
		int pollrv = poll(&pfd, 1, ms);

		if (pollrv == -1 && errno == EINTR)     // interrupted by signal
			continue;
		if (pollrv == 1 && pfd.revents&POLLHUP) // pipe was closed
			return true;
		if (pollrv == 0)                        // wait timed out
			return false;

		// should NEVER get here...

		int err = errno;
		printf("trywait: unknown poll result\n");
		printf("  pollrv = %d\n", pollrv);
		printf("  errno = %d\n", err);
		printf("  pfd.revents = %d\n", pfd.revents);

		debug assert(0);

		return false;
	}
}
