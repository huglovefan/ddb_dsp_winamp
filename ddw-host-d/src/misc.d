module ddw.host.misc;

import core.stdc.config : ssize_t = c_long;
import core.stdc.errno;
import core.stdc.stdio;
import core.stdc.stdlib;
import core.sys.windows.ntdef;
import core.sys.windows.winbase;
import core.sys.windows.windef;

nothrow:
@nogc:

extern (C) ssize_t write(int fd, const(void)*, size_t);
extern (C) ssize_t read(int fd, void*, size_t);

inout(char)* superbasename(return inout(char)* path_) pure
{
	inout(char)* path = path_;
	inout(char)* slash = null;

	do
	{
		if (*path == '/') slash = path;
		if (*path == '\\') slash = path;
	}
	while (*path++);

	return slash ? slash+1 : path_;
}

inout(char)* strchrnul(return inout(char)* s, char c) pure
{
	while (*s && *s != c) s++;
	return s;
}

bool atoi_ok(const(char)* s, int* outp)
{
	int scanned;
	int rv = sscanf(s, "%d%n", outp, &scanned);
	return rv == 1 && s[scanned] == '\0';
}

// success                 -> true
// EOF with nothing read   -> false, errno = 0
// EOF with partial read   -> false, errno = EIO
// error with nothing read -> false, errno set
// error with partial read -> false, errno set

bool read_full(int fd, void* p_, size_t sz)
{
	char* p = cast(char*)p_;
	ssize_t rv;
again:
	rv = read(fd, p, sz);

	if (cast(size_t)rv == sz)
		return true;

	if (rv == 0)
	{
		errno = (p != p_) ? EIO : 0;
		return false;
	}

	if (rv == -1)
		return false;

	sz -= cast(size_t)rv;
	p += cast(size_t)rv;

	goto again;
}

bool write_full(int fd, const(void)* p_, size_t sz)
{
	const(char)* p = cast(char*)p_;
	ssize_t rv;
again:
	rv = write(fd, p, sz);

	if (cast(size_t)rv == sz)
		return true;

	if (rv == 0)
	{
		errno = (p != p_) ? EIO : 0;
		return false;
	}

	if (rv == -1)
		return false;

	sz -= cast(size_t)rv;
	p += cast(size_t)rv;

	goto again;
}

version (Windows):

extern (Windows) ULONG RtlNtStatusToDosError(NTSTATUS Status);

const(char)* NtStrError(NTSTATUS Status)
{
	version (CRuntime_Microsoft)
		return StrError(RtlNtStatusToDosError(Status));
	else
	{
		__gshared static char[24] buf = '\0';
		snprintf(buf.ptr, buf.length, "NTSTATUS %d", Status);
		return buf.ptr;
	}
}

LPCSTR StrError(DWORD Code)
{
	DWORD Length;
	__gshared static CHAR[128] Buf = '\0';

	Length = FormatMessageA(
		FORMAT_MESSAGE_FROM_SYSTEM,
		null,
		Code,
		0,
		Buf.ptr,
		Buf.length,
		null);

	// remove trailing newline
	while (Length > 0 && Buf[Length-1] < 32)
		Buf[--Length] = '\0';

	if (Length == 0)
		snprintf(Buf.ptr, Buf.length, "%ld", Code);

	return Buf.ptr;
}

VOID PrintError(LPCSTR What)
{
	if (What != null)
		fprintf(stderr, "%s: %s\n", What, StrError(GetLastError()));
	else
		fprintf(stderr, "%s\n", StrError(GetLastError()));
}
