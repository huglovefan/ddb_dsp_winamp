module ddw.host.misc;

import core.stdc.errno;
import core.stdc.stdio;
import core.stdc.stdlib;
import core.sys.windows.ntdef;
import core.sys.windows.winbase;
import core.sys.windows.windef;

nothrow:
@nogc:

// -----------------------------------------------------------------------------

inout(char)* strchrnul(inout(char)* s, int c)
{
	while (*s && *s != c) s++;
	return s;
}

// -----------------------------------------------------------------------------

inout(char)[] superbasename(inout(char)[] path)
{
	size_t sp = 0;
	foreach_reverse (i; 0..path.length)
	{
		if (path.ptr[i] == '/' || path.ptr[i] == '\\')
		{
			sp = i+1;
			break;
		}
	}
	return path[sp..$];
}

unittest
{
	assert(superbasename("a") == "a");
	assert(superbasename("a/b") == "b");
	assert(superbasename("a\\b") == "b");
	superbasename("");
	superbasename("a");
	superbasename("/");
	superbasename("a/");
	superbasename("/a");
	superbasename("a/a");
}

// -----------------------------------------------------------------------------

// https://github.com/wine-mirror/wine/blob/master/dlls/msvcrt/file.c
private extern(C) int write(int fd, const(void)*, uint);
private extern(C) int read(int fd, void*, uint);

// success                 -> true
// EOF with nothing read   -> false, errno = 0
// EOF with partial read   -> false, errno = EIO
// error with nothing read -> false, errno set
// error with partial read -> false, errno set

bool read_full(int fd, void* p, size_t sz)
{
	const void* base = p;

	for (;;)
	{
		int rv = read(fd, p, sz);

		if (cast(size_t)rv == sz)
			return true;

		if (rv == 0)
		{
			errno = (p != base) ? EIO : 0;
			return false;
		}

		if (rv == -1)
			return false;

		sz -= cast(size_t)rv;
		p += cast(size_t)rv;
	}
}

bool write_full(int fd, const(void)* p, size_t sz)
{
	const void* base = p;

	for (;;)
	{
		int rv = write(fd, p, sz);

		if (cast(size_t)rv == sz)
			return true;

		if (rv == 0)
		{
			errno = (p != base) ? EIO : 0;
			return false;
		}

		if (rv == -1)
			return false;

		sz -= cast(size_t)rv;
		p += cast(size_t)rv;
	}
}

// -----------------------------------------------------------------------------

private extern(Windows) ULONG RtlNtStatusToDosError(NTSTATUS);

const(char)* NtStrError(NTSTATUS Status)
{
	version(CRuntime_Microsoft)
		return StrError(RtlNtStatusToDosError(Status));
	else
	{
		__gshared static char[24] buf = 0;
		snprintf(buf.ptr, buf.length, "NTSTATUS %d", Status);
		return buf.ptr;
	}
}

// -----------------------------------------------------------------------------

PCSTR StrError(DWORD Code)
{
	DWORD Length;
	__gshared static CHAR[128] Buf = 0;

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
		Buf[--Length] = 0;

	if (Length == 0)
		snprintf(Buf.ptr, Buf.length, "%u", Code);

	return Buf.ptr;
}

// -----------------------------------------------------------------------------

VOID PrintError(PCSTR What)
{
	PCSTR se = StrError(GetLastError());
	if (What)
		printf("%s: %s\n", What, se);
	else
		printf("%s\n", se);
}
