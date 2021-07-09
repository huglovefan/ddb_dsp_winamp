#include "tickmain.h"

#include <errno.h>
#include <pthread.h>
#include <sys/event.h>
#include <unistd.h>
#include <string.h>

#include "plugin.h"

#define IDENT_TICK 123
#define IDENT_DIENOW 666

#if !defined(NOTE_MSECONDS)
 #define NOTE_MSECONDS 0
#endif

static int kq = -1;
static pthread_t tickthread;

// -----------------------------------------------------------------------------

static void
update_tick(void);

static void *
tickthread_main(void *ud)
{
	(void)ud;

	for (;;) {
		struct kevent ev = {0};
		int rv = kevent(kq, NULL, 0, &ev, 1, NULL);
		if (rv == -1) {
			if (rv == EINTR) continue;
			perror("ddb_shm: kevent");
			break;
		}
		if (rv == 0) continue;

		if (ev.filter == EVFILT_TIMER && ev.ident == IDENT_TICK) {
			update_tick();
			continue;
		}
		if (ev.filter == EVFILT_USER && ev.ident == IDENT_DIENOW) {
			goto out;
		}
	}
out:
	return NULL;
}

static void
update_tick(void)
{
	shm->playback_pos_ms = (int)(1000.0f*deadbeef->streamer_get_playpos());
}

// -----------------------------------------------------------------------------

void
tickthread_start_ticking(void)
{
	struct kevent ev = {
		.ident = IDENT_TICK,
		.filter = EVFILT_TIMER,
		.flags = EV_ADD,
		.fflags = NOTE_MSECONDS,
		.data = 100,
	};

	kevent(kq, &ev, 1, NULL, 0, NULL);
}

void
tickthread_stop_ticking(void)
{
	struct kevent ev = {
		.ident = IDENT_TICK,
		.filter = EVFILT_TIMER,
		.flags = EV_DELETE,
	};

	kevent(kq, &ev, 1, NULL, 0, NULL);
}

// -----------------------------------------------------------------------------

bool
tickthread_init(void)
{
	if (tickthread != 0)
		return false;

	kq = kqueue();
	if (kq == -1) {
		perror("ddb_shm: kqueue");
		goto err;
	}

	int err = pthread_create(&tickthread, NULL, tickthread_main, NULL);
	if (err != 0) {
		tickthread = 0;
		fprintf(stderr, "ddb_shm: pthread_create: %s\n", strerror(errno));
		goto err;
	}

	return true;
err:
	if (kq != -1) {
		close(kq);
		kq = -1;
	}

	return false;
}

void
tickthread_deinit(void)
{
	if (tickthread == 0)
		return;

	// tell tickthread to exit
	if (kq != -1) {
		if (tickthread != 0)
			tickthread_stop_ticking();

		//
		// (is there no way to do this in one call?)
		//
		struct kevent ev = {
			.ident = IDENT_DIENOW,
			.filter = EVFILT_USER,
			.flags = EV_ADD|EV_CLEAR,
		};
		struct kevent ev2 = {
			.ident = IDENT_DIENOW,
			.filter = EVFILT_USER,
			.fflags = NOTE_TRIGGER,
		};
		if (-1 == kevent(kq, &ev, 1, NULL, 0, NULL))
			perror("ddb_shm: kevent");
		if (-1 == kevent(kq, &ev2, 1, NULL, 0, NULL))
			perror("ddb_shm: kevent");
	}

	// try-join tickthread
	if (tickthread != 0) {
		struct timespec ts = {0};
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += 1;
		int err = pthread_timedjoin_np(tickthread, NULL, &ts);
		if (err == 0) {
			tickthread = 0;
		} else {
			fprintf(stderr, "ddb_shm: pthread_timedjoin_np: %s\n", strerror(err));
		}
	}

	// close the kqueue if the thread was joined successfully
	if (tickthread == 0) {
		if (kq != -1) {
			close(kq);
			kq = -1;
		}
	}
}
