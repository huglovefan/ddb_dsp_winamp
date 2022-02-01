module ddw.plugin.tickmain;

import core.stdc.errno;
import core.stdc.stdio;
import core.sys.posix.poll;
import core.sys.posix.pthread;
import core.sys.posix.unistd;
import misclib.druntime.threadinit;
import ddw.plugin.main : deadbeef, shm;

private __gshared
{
	pthread_t thr;
	int[2] msgpipe = [-1, -1];
	bool ticking;

	enum Msg
	{
		quit,
		hey,
	}

	extern(C) int pthread_timedjoin_np(pthread_t, void**, const(timespec)*) nothrow @nogc;
}

/**
 * start the tick thread if it isn't started already
 */
void tickthread_init()
{
	if (thr) return;

	if (pipe(msgpipe) != 0)
	{
		perror("pipe");
		return;
	}

	if (int err = pthread_create(&thr, null, &tickthread_main, null))
	{
		thr = 0;
		errno = err;
		perror("pthread_create");
		goto err;
	}

	return;
err:
	close(msgpipe[0]); msgpipe[0] = -1;
	close(msgpipe[1]); msgpipe[1] = -1;
}

/**
 * stop the tick thread if it is started
 */
void tickthread_deinit()
{
	if (!thr) return;

	Msg die = Msg.quit;
	write(msgpipe[1], &die, die.sizeof);

	timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += 1;
	if (int err = pthread_timedjoin_np(thr, null, &ts))
	{
		errno = err;
		perror("pthread_timedjoin_np");
		return;
	}
	thr = 0;

	close(msgpipe[0]); msgpipe[0] = -1;
	close(msgpipe[1]); msgpipe[1] = -1;
}

// -----------------------------------------------------------------------------

void tickthread_unpause()
{
	if (!ticking && msgpipe[1] != -1)
	{
		ticking = true;
		Msg msg = Msg.hey;
		write(msgpipe[1], &msg, msg.sizeof);
	}
}

void tickthread_pause()
{
	ticking = false;
}

// -----------------------------------------------------------------------------

void update_tick()
{
	if (!shm) return;

	shm.playback_pos_ms = cast(int)(1000.0f*deadbeef.streamer_get_playpos());
}

// -----------------------------------------------------------------------------

private:

extern(C)
void* tickthread_main(void*)
{
	initForeignThread();

	pollfd pfd = {
		fd: msgpipe[0],
		events: POLLIN,
	};
	loop: for (;;)
	{
		int rv = poll(&pfd, 1, (ticking) ? 100 : -1);

		if (rv == -1)
		{
			if (errno == EINTR)
				continue;

			perror("poll");
			break;
		}
		else if (rv == 0)
		{
			update_tick();
			continue;
		}
		else if (rv == 1 && pfd.revents&POLLIN)
		{
			Msg msg = Msg.quit;
			if (read(msgpipe[0], &msg, msg.sizeof) == -1)
				perror("read");
			final switch (msg)
			{
				case Msg.hey:
					update_tick();
					continue loop;
				case Msg.quit:
					break loop;
			}
		}
		assert(0, "unreachable");
	}

	return null;
}
