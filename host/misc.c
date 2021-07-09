#include "misc.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "macros.h"

void
assert_fail(const char *fmt, const char *arg1, const char *arg2)
{
	static _Thread_local char deathmsg[256];
	int rv = snprintf(deathmsg, sizeof(deathmsg), fmt, arg1, arg2);
	if (rv > 0) {
		if ((unsigned)rv >= sizeof(deathmsg)) {
			rv = sizeof(deathmsg)-1;
			deathmsg[sizeof(deathmsg)-1] = '\0';
		}
		write_full(2, deathmsg, rv);
	}
	abort();
}

const char *
superbasename(const char *path_)
{
	const char *path = path_;
	const char *slash = NULL;

	do {
		if (*path == '/') slash = path;
		if (*path == '\\') slash = path;
	} while (*path++);

	return slash ? slash+1 : path_;
}

char *
strchrnul(char *s, char c)
{
	while (*s && *s != c) s++;
	return s;
}

bool
atoi_ok(const char *s, int* out)
{
	int scanned;
	int rv = sscanf(s, "%d%n", out, &scanned);
	return rv == 1 && s[scanned] == '\0';
}

// success                 -> true
// EOF with nothing read   -> false, errno = 0
// EOF with partial read   -> false, errno = EIO
// error with nothing read -> false, errno set
// error with partial read -> false, errno set

bool
read_full(int fd, void *p_, size_t sz)
{
	char *p = p_;
	ssize_t rv;
again:
	rv = read(fd, p, sz);

	if L ((size_t)rv == sz)
		return true;

	if U (rv == 0) {
		errno = U(p != p_) ? EIO : 0;
		return false;
	}

	if U (rv == -1)
		return false;

	sz -= (size_t)rv;
	p += (size_t)rv;

	goto again;
}

bool
write_full(int fd, const void *p_, size_t sz)
{
	const char *p = p_;
	ssize_t rv;
again:
	rv = write(fd, p, sz);

	if L ((size_t)rv == sz)
		return true;

	if U (rv == 0) {
		errno = U(p != p_) ? EIO : 0;
		return false;
	}

	if U (rv == -1)
		return false;

	sz -= (size_t)rv;
	p += (size_t)rv;

	goto again;
}

LPCSTR
StrError(LONG Code)
{
	DWORD Length;
	static _Thread_local CHAR Buf[128];

	Length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM,
	                        0,
	                        Code,
	                        0,
	                        Buf,
	                        sizeof(Buf),
	                        NULL);

	// remove trailing newline
	while (Length > 0 && Buf[Length-1] < 32)
		Buf[--Length] = '\0';

	if (Length == 0)
		snprintf(Buf, sizeof(Buf), "%ld", Code);

	return Buf;
}

VOID
PrintError(LPCSTR What)
{
	if (What != NULL)
		fprintf(stderr, "%s: %s\n", What, StrError(GetLastError()));
	else
		fprintf(stderr, "%s\n", StrError(GetLastError()));
}
