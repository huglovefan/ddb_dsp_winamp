#pragma once

#include <utility> /* std::move */
#include <ntdef.h>

wchar_t *strchrnul(wchar_t *s, int c);
wchar_t *superbasename(wchar_t *path);

bool read_full(int fd, void *p_, size_t sz);
bool write_full(int fd, const void *p_, size_t sz);

const wchar_t *StrError(unsigned long Code);
void PrintError(const char *What);

const wchar_t *NtStrError(NTSTATUS status);

template <typename T, typename U>
T exchange(T &var, U &&newval)
{
	T oldval = std::move(var);
	var = newval;
	return oldval;
}

__attribute__((noreturn))
void abort_msg(const char *);

__attribute__((noreturn))
__attribute__((format(gnu_printf, 1, 2)))
void abort_fmt(const char *, ...);

/* NOTE: pass fmt without the L, the macro will add it */
void OutputDebugStringf(const wchar_t *fmt, ...);

__attribute__((format(printf, 1, 2)))
static inline void _check_fmt_(const char *fmt, ...)
{
}

/* this madness is to enable format string checking. gcc doesn't seem to
   support pragma(printf) with wchar_t strings. (seriously? might be a
   mingw-w64 thing.) */
#define OutputDebugStringf(fmt, ...) \
do { \
	if (0) _check_fmt_(fmt, ##__VA_ARGS__); \
	OutputDebugStringf(L##fmt, ##__VA_ARGS__); \
} while (0)
