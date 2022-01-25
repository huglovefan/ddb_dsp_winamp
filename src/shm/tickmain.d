import core.stdc.errno;
import core.sys.posix.poll;
import core.sys.posix.pthread;
import core.sys.posix.unistd;
import std.exception : errnoEnforce;
import misclib.druntime.threadinit;
import ddw.shm.main;

// -----------------------------------------------------------------------------

void tickthread_init()
{
	errnoEnforce(pipe(msgpipe) == 0);

	if (int err = pthread_create(&tickthread, null, &tickthread_main, null))
		{ errno = err; errnoEnforce(0); }
}

void tickthread_deinit()
{
	Msg die = Msg.quit;
	errnoEnforce(write(msgpipe[1], &die, die.sizeof) == die.sizeof);

	if (int err = pthread_join(tickthread, null))
		{ errno = err; errnoEnforce(0); }
	tickthread = 0;

	errnoEnforce(close(msgpipe[0]) == 0); msgpipe[0] = -1;
	errnoEnforce(close(msgpipe[1]) == 0); msgpipe[1] = -1;
}

// -----------------------------------------------------------------------------

void tickthread_start_ticking()
{
	if (!ticking)
	{
		ticking = true;
		Msg msg = Msg.hey;
		errnoEnforce(write(msgpipe[1], &msg, msg.sizeof) == msg.sizeof);
	}
}

void tickthread_stop_ticking()
{
	ticking = false;
}

// -----------------------------------------------------------------------------

private:

__gshared pthread_t tickthread;
__gshared int[2] msgpipe;
__gshared bool ticking;

enum Msg
{
	hey,
	quit,
}

extern (C) void* tickthread_main(void* ud)
{
	initForeignThread();
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
				if (errno == EINTR)
					continue;
				errnoEnforce(0);
				break;
			case 0:
				update_tick();
				break;
			case 1:
				if (pfd.revents & POLLIN)
				{
					Msg msg;
					errnoEnforce(read(msgpipe[0], &msg, msg.sizeof) == msg.sizeof);
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
