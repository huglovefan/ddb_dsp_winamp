import core.stdc.errno;
import core.stdc.stdio;
import core.stdc.stdlib;
import core.sys.posix.dirent;
import core.sys.posix.poll;
import core.sys.posix.sys.wait;
import core.sys.posix.unistd;
import std.exception;
import std.format;
import std.stdio : writefln;
import std.string;
import ddw.plugin.child;
import ddw.plugin.main;

void child_start(Child* self)
{
	assert(self.pid == -1);

	int[2] stdin = [-1, -1];
	errnoEnforce(pipe(stdin) == 0);
	scope (failure) { close(stdin[0]); close(stdin[1]); }

	int[2] stdout = [-1, -1];
	errnoEnforce(pipe(stdout) == 0);
	scope (failure) { close(stdout[0]); close(stdout[1]); }

	string host;
	{
		deadbeef.conf_lock();
		scope (exit) deadbeef.conf_unlock();
		host = deadbeef.conf_get_str_fast("ddw.host_cmd", "ddw_host.exe").fromStringz.idup;
	}

	const(char)* cmd = format("exec %s %s", host, self.pl.dll).toStringz;

	pid_t pid = fork();
	errnoEnforce(pid >= 0);
	if (pid == 0)
	{
		if (
			dup2(stdin[0], STDIN_FILENO) == -1 ||
			dup2(stdout[1], STDOUT_FILENO) == -1)
		{
			perror("dsp_winamp: dup2");
			goto chlderr;
		}
		close_extra();
		execl("/bin/sh", "sh", "-c".ptr, cmd, null);
		perror("dsp_winamp: execl");
chlderr:
		for (;;) _exit(EXIT_FAILURE);
	}
	else
	{
		errnoEnforce(close(stdin[0]) == 0); stdin[0] = -1;
		errnoEnforce(close(stdout[1]) == 0); stdout[1] = -1;

		self.pid = pid;
		self.fds[0] = stdout[0];
		self.fds[1] = stdin[1];
	}
}

void child_stop(Child* self)
{
	if (self.pid == -1)
		return;

	errnoEnforce(close(self.fds[1]) == 0); self.fds[1] = -1;

	scope (exit)
	{
		self.pid = -1;
		errnoEnforce(close(self.fds[0]) == 0); self.fds[0] = -1;
	}

	for (int attempt = 0; /* true */; attempt++)
	{
		int waitstatus;
		int waitrv = (trywait(self, 1000))
			? waitpid(self.pid, &waitstatus, 0)
			: waitpid(self.pid, &waitstatus, WNOHANG);

		if (waitrv == -1)
		{
			if (errno == ECHILD)
				break;

			for (;;) errnoEnforce(0);
		}

		// used WNOHANG (trywait timed out) but the child hasn't yet exited
		if (waitrv == 0)
		{
			if (attempt == 0)
			{
				writefln("dsp_winamp: child didn't exit in 1000ms, sending SIGTERM...");
				if (kill(self.pid, SIGTERM) != 0 && errno != ESRCH)
					errnoEnforce(0);
				continue;
			}
			else if (attempt == 1)
			{
				writefln("dsp_winamp: child didn't exit in 2000ms, sending SIGKILL...");
				if (kill(self.pid, SIGKILL) != 0 && errno != ESRCH)
					errnoEnforce(0);
				continue;
			}
			else
			{
				writefln("dsp_winamp: gave up waiting for child to exit!");
				break;
			}
		}

		if (WIFEXITED(waitstatus))
		{
			writefln("dsp_winamp: child exited with status %d", WEXITSTATUS(waitstatus));
		}
		if (WIFSIGNALED(waitstatus))
		{
			writefln("dsp_winamp: child was killed by signal %d", WTERMSIG(waitstatus));
		}

		break;
	}
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

bool child_is_doomed(Child* self)
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

extern (C) int scandir(const(char)*, dirent***, void*, void*);

void close_extra()
{
	dirent** namelist;
	int count = scandir("/proc/self/fd", &namelist, null, null);
	if (count == -1)
		return;

	for (int i = 0; i < count; i++)
	{
		int fd = atoi(namelist[i].d_name.ptr);
		if (fd > 2)
			close(fd);
		free(namelist[i]);
	}

	free(namelist);
}

//
// try-wait the child with a timeout by polling its stdout
//
bool trywait(Child* self, int ms)
{
	pollfd pfd = {
		fd: self.fds[0],
		events: 0,
	};
again:
	int pollrv = poll(&pfd, 1, ms);
	if (pollrv == -1 && errno == EINTR)
		goto again;

	// success: other end of the pipe is closed
	if (pollrv == 1 && pfd.revents&POLLHUP)
		return true;

	// wait timed out
	if (pollrv == 0)
		return false;

	for (;;) errnoEnforce(0);
}
