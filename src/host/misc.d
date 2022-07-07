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

T exchange(T)(ref T var, T newval)
{
	T oldval = var;
	var = newval;
	return oldval;
}

// -----------------------------------------------------------------------------

inout(char)* strchrnul(inout(char)* s, int c)
{
	while (*s && *s != c) s++;
	return s;
}

// -----------------------------------------------------------------------------

inout(char)[] superbasename(inout(char)[] path)
{
	size_t sp;
	foreach_reverse (i, c; path)
	{
		if (c == '/' || c == '\\')
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

bool read_full(int fd, void* p, size_t sz)
{
	const void* base = p;

	for (;;)
	{
		uint rv = read(fd, p, (sz <= int.max) ? sz : int.max);

		if (rv == sz)
			return true;

		if (rv == 0)
		{
			errno = (p != base) ? EIO : 0;
			return false;
		}

		if (rv == -1)
			return false;

		sz -= rv;
		p += rv;
	}
}

bool write_full(int fd, const(void)* p, size_t sz)
{
	for (;;)
	{
		uint rv = write(fd, p, (sz <= int.max) ? sz : int.max);

		if (rv == sz)
			return true;

		if (rv == 0)
		{
			errno = EIO;
			return false;
		}

		if (rv == -1)
			return false;

		sz -= rv;
		p += rv;
	}
}

private
{
	// https://github.com/wine-mirror/wine/blob/wine-7.0/dlls/msvcrt/file.c#L2927
	extern(C) int read(int fd, void*, uint);

	// https://github.com/wine-mirror/wine/blob/wine-7.0/dlls/msvcrt/file.c#L3426
	extern(C) int write(int fd, const(void)*, uint);
}

// -----------------------------------------------------------------------------

PCSTR StrError(DWORD Code)
{
	DWORD Length;
	static CHAR[128] Buf = 0;

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

// -----------------------------------------------------------------------------

const(char)* NtStrError(NTSTATUS status)
{
	ULONG error = RtlNtStatusToDosError(status);

	if (error == ERROR_MR_MID_NOT_FOUND && status != STATUS_MESSAGE_NOT_FOUND)
	{
		// couldn't convert the error code
		// return a stringified version of the ntstatus number
		static char[32] buf = 0;
		snprintf(buf.ptr, buf.length, "NTSTATUS %d", status);
		return buf.ptr;
	}

	return StrError(error);
}

private
{
	// https://github.com/wine-mirror/wine/blob/wine-7.0/dlls/ntdll/error.c#L66
	extern(Windows) ULONG RtlNtStatusToDosError(NTSTATUS);

	// https://github.com/wine-mirror/wine/blob/wine-7.0/include/winerror.h#L329
	enum ERROR_MR_MID_NOT_FOUND = 317;

	// https://github.com/wine-mirror/wine/blob/wine-7.0/include/ntstatus.h#L475
	enum STATUS_MESSAGE_NOT_FOUND = 0xc0000109;
}

// -----------------------------------------------------------------------------

/**
 * exit without running any destructors or other cleanup code
 * 
 * compared to exit(), this skips any cleanup done by the C runtime, which
 *  might include flusing stdio buffers and running atexit() handlers
 */
noreturn _exit(int status)
{
	TerminateProcess(GetCurrentProcess(), status);
	// should be unreachable, but crash if we somehow get here
	asm nothrow @nogc { ud2; }
	// satisfy noreturn
	assert(0);
}
