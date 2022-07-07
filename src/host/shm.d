module ddw.host.shm;

import core.stdc.stdio;
import core.sys.windows.winbase;
import core.sys.windows.windef;
import core.sys.windows.ntdef;
import ddw.host.misc;
import std.exception;
import std.utf;

// -----------------------------------------------------------------------------

void* shmnew(string path, size_t sz) nothrow
{
	/*
	 * open the file in Z:\dev\shm
	 */
	HANDLE file = CreateFile(
		path.toUTF16z.assumeWontThrow,
		GENERIC_READ|GENERIC_WRITE,
		FILE_SHARE_READ|FILE_SHARE_WRITE,
		null,
		OPEN_EXISTING,
		0,
		null);
	if (file == INVALID_HANDLE_VALUE)
	{
		PrintError("CreateFile");
		return null;
	}
	scope (exit)
	{
		if (file != INVALID_HANDLE_VALUE)
			CloseHandle(file);
	}

	/*
	 * create some mapping (idk why this is done in two steps)
	 */
	HANDLE mapping = CreateFileMapping(
		file,
		null,
		PAGE_READWRITE,
		0,
		0,
		null);
	if (!mapping)
	{
		PrintError("CreateFileMapping");
		return null;
	}
	scope (exit)
	{
		if (mapping)
			CloseHandle(mapping);
	}

	/*
	 * temporarily enable DEP to prevent the file from being mapped as
	 *  executable, which would fail if Z:\dev\shm is mounted noexec
	 * 
	 * (DEP = data execution prevention, means we're smart enough that we don't
	 *  need automatic execute permissions on all mapped files)
	 */
	bool didEnableDEP = tryEnableDEP();
	scope (exit)
	{
		if (didEnableDEP)
			disableDEP();
	}

	/*
	 * get the pointer from the mapping
	 * 
	 * note: it's safe to close the mapping and file handle after this
	 */
	LPVOID view = MapViewOfFile(
		mapping,
		FILE_MAP_READ|FILE_MAP_WRITE,
		0,
		0,
		sz);
	if (!view)
	{
		PrintError("MapViewOfFile");
		return null;
	}

	return view;
}

// -----------------------------------------------------------------------------

private:

/**
 * enable Data Execution Prevention
 */
bool tryEnableDEP() nothrow
{
	ULONG info = MEM_EXECUTE_OPTION_DISABLE;
	NTSTATUS status = NtSetInformationProcess(
		GetCurrentProcess(),
		ProcessExecuteFlags,
		&info,
		info.sizeof);

	if (NT_SUCCESS(status))
		return true;
	else
	{
		printf("NtSetInformationProcess: %s\n", NtStrError(status));
		return false;
	}
}

/**
 * disable Data Execution Prevention
 */
void disableDEP() nothrow
{
	ULONG info = MEM_EXECUTE_OPTION_ENABLE;
	NTSTATUS status = NtSetInformationProcess(
		GetCurrentProcess(),
		ProcessExecuteFlags,
		&info,
		info.sizeof);

	if (!NT_SUCCESS(status))
		printf("NtSetInformationProcess: %s\n", NtStrError(status));
}

// -----------------------------------------------------------------------------

/*
 * undocumented windows things that aren't in druntime
 */

// https://github.com/wine-mirror/wine/blob/wine-7.0/include/winternl.h#L1575
enum MEM_EXECUTE_OPTION_DISABLE = 0x01;
enum MEM_EXECUTE_OPTION_ENABLE = 0x02;

// https://github.com/wine-mirror/wine/blob/wine-7.0/include/winternl.h#L1524
alias int PROCESSINFOCLASS;
enum : PROCESSINFOCLASS
{
	ProcessExecuteFlags = 34,
}

// https://github.com/wine-mirror/wine/blob/wine-7.0/dlls/ntdll/unix/process.c#L1509
extern(Windows) NTSTATUS NtSetInformationProcess(HANDLE, PROCESSINFOCLASS, PVOID, ULONG) nothrow;

// -----------------------------------------------------------------------------

// druntime one is missing `nothrow`
bool NT_SUCCESS(NTSTATUS status) nothrow
{
	return status >= 0;
}
