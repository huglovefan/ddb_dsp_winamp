#include "monotime.h"

#if defined(_WIN32)

#include <math.h>
#include <profileapi.h>

static double monotime_freq;

__attribute__((constructor))
static void _init_monotime_(void)
{
	LARGE_INTEGER tmp;

	if (!QueryPerformanceFrequency(&tmp))
	{
		monotime_freq = NAN;
		return;
	}

	monotime_freq = (double)tmp.QuadPart;
}

double monotime(void)
{
	LARGE_INTEGER counter;

	if (!QueryPerformanceCounter(&counter))
		return NAN;

	return ((double)counter.QuadPart / monotime_freq);
}

#else

#include <math.h>
#include <time.h>

double monotime(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
		return NAN;

	return (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

#endif
