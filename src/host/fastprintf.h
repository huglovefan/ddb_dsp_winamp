#pragma once

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

int fastprintf(const wchar_t *fmt, ...);

/* like fastprintf, but only writes if the output is to a console (not
   a file). useful for spammy developer messages that would otherwise
   cause .xsession-errors to explode in size. */
/* TODO: implementation didn't work. does the console check just not
   work in wine? is everything actually a file like they say? */
#define fastprintf_cons fastprintf

#if defined(__cplusplus)
}
#endif

#if defined(__GNUC__)

__attribute__((format(gnu_printf, 1, 2)))
static inline void _check_format_(const char *fmt, ...)
{
}

#define fastprintf(fmt, ...) \
	(__extension__({ \
		if (0) \
			_check_format_(fmt, ##__VA_ARGS__); \
		fastprintf(L##fmt, ##__VA_ARGS__); \
	}))

#endif /* __GNUC__ */
