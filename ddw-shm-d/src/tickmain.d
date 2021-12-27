module ddw.shm.tickmain;

import core.stdc.errno;
import core.sys.posix.poll;
import core.sys.posix.pthread;
import core.sys.posix.unistd;
import core.stdc.string;
import core.stdc.stdio;

import core.atomic;

import ddw.shm.plugin;

extern (C) int pthread_timedjoin_np(pthread_t thread, void** retval, const(timespec)* abstime);

__gshared pthread_t tickthread;
__gshared int[2] msgpipe;
__gshared bool ticking;

// -----------------------------------------------------------------------------

enum Msg {
	hey,
	quit,
}

extern (C) void* tickthread_main(void* ud)
{
	for (;;)
	{
		int timeout = ticking ? 100 : -1;
		pollfd pfd = {
			fd: msgpipe[0],
			events: POLLIN,
		};
		int rv = poll(&pfd, 1, timeout);
		final switch (rv)
		{
			case -1:
				if (rv == EINTR)
					continue;
				perror("ddb_shm: poll");
				goto Lout;
			case 0:
				update_tick();
				break;
			case 1:
				if (pfd.revents & POLLIN)
				{
					Msg msg;
					if (read(msgpipe[0], &msg, msg.sizeof) != msg.sizeof)
						assert(0, "short read");
					final switch (msg)
					{
						case Msg.hey:
							update_tick();
							break;
						case Msg.quit:
							goto Lout;
					}
				}
				break;
		}
	}
Lout:
	return null;
}

void update_tick()
{
	shm.playback_pos_ms = cast(int)(1000.0f*deadbeef.streamer_get_playpos());
}

// -----------------------------------------------------------------------------

void tickthread_start_ticking()
{
	if (!ticking)
	{
		ticking = true;
		Msg msg = Msg.hey;
		write(msgpipe[1], &msg, msg.sizeof);
	}
}

void tickthread_stop_ticking()
{
	ticking = false;
}

// -----------------------------------------------------------------------------

bool tickthread_init()
{
	int err;

	if (tickthread != 0)
		return false;

	if (pipe(msgpipe) == -1)
	{
		perror("ddb_shm: pipe");
		goto err;
	}

	err = pthread_create(&tickthread, null, &tickthread_main, null);
	if (err != 0)
	{
		tickthread = 0;
		fprintf(stderr, "ddb_shm: pthread_create: %s\n", strerror(errno));
		goto err;
	}

	return true;
err:
	if (msgpipe[0] != -1)
	{
		close(msgpipe[0]);
		close(msgpipe[1]);
		msgpipe[0] = -1;
		msgpipe[1] = -1;
	}

	return false;
}

void tickthread_deinit()
{
	if (tickthread == 0)
		return;

	// tell tickthread to exit
	if (msgpipe[0] != -1)
	{
		if (tickthread != 0)
			tickthread_stop_ticking();

		Msg die = Msg.quit;
		if (write(msgpipe[1], &die, die.sizeof) == -1)
			perror("tickthread_deinit: write");
	}

	// try-join tickthread
	if (tickthread != 0)
	{
		timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += 1;
		int err = pthread_timedjoin_np(tickthread, null, &ts);
		if (err == 0)
		{
			tickthread = 0;
		}
		else
		{
			fprintf(stderr, "ddb_shm: pthread_timedjoin_np: %s\n", strerror(err));
		}
	}

	// close the kqueue if the thread was joined successfully
	if (tickthread == 0)
	{
		if (msgpipe[0] != -1)
		{
			close(msgpipe[0]);
			close(msgpipe[1]);
			msgpipe[0] = -1;
			msgpipe[1] = -1;
		}
	}
}
