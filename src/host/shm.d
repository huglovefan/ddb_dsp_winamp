module ddw.host.shm;

import core.stdc.stdio;

import core.sys.windows.winbase;
import core.sys.windows.windef;
import core.sys.windows.ntdef;

import ddw.host.misc;

void* shmnew(const(char)* path, size_t sz)
{
	//
	// open the file in Z:\dev\shm
	//
	HANDLE File = CreateFileA(
		path,
		GENERIC_READ|GENERIC_WRITE,
		FILE_SHARE_READ|FILE_SHARE_WRITE,
		null,
		OPEN_EXISTING,
		0,
		null);
	if (File == INVALID_HANDLE_VALUE)
	{
		PrintError("CreateFile");
		return null;
	}
	scope (exit)
	{
		if (File != INVALID_HANDLE_VALUE)
			CloseHandle(File);
	}

	//
	// create some mapping (idk why this is done in two steps)
	//
	HANDLE Mapping = CreateFileMapping(
		File,
		null,
		PAGE_READWRITE,
		0,
		0,
		null);
	if (Mapping == null)
	{
		PrintError("CreateFileMapping");
		return null;
	}
	scope (exit)
	{
		if (Mapping != null)
			CloseHandle(Mapping);
	}

	//
	// temporarily enable DEP to prevent the file from being mapped as
	//  executable, which would fail if Z:\dev\shm is mounted noexec
	//
	// (DEP = data execution prevention, means we're smart enough that we don't
	//  need automatic execute permissions on all mapped files)
	//
	// (it's not enabled by default in wine, maybe better that way for compatibility)
	//
	bool didEnableDEP = tryEnableDEP();
	scope (exit)
	{
		if (didEnableDEP)
			disableDEP();
	}

	//
	// get the pointer from the mapping
	//
	// note: it's safe to close the mapping and file handle after this
	//
	LPVOID View = MapViewOfFile(
		Mapping,
		FILE_MAP_READ|FILE_MAP_WRITE,
		0,
		0,
		sz);
	if (View == null)
	{
		PrintError("MapViewOfFile");
		return null;
	}

	return View;
}

/**
 * enable Data Execution Prevention
 */
bool tryEnableDEP()
{
	LONG Value = MEM_EXECUTE_OPTION_DISABLE;
	NTSTATUS Status = NtSetInformationProcess(
		GetCurrentProcess(),
		PROCESSINFOCLASS.ProcessExecuteFlags,
		&Value,
		Value.sizeof);

	if (Status >= 0) // NT_SUCCESS
		return true;
	else
	{
		printf("NtSetInformationProcess: %s\n", NtStrError(Status));
		return false;
	}
}

/**
 * disable Data Execution Prevention
 */
void disableDEP()
{
	LONG Value = MEM_EXECUTE_OPTION_ENABLE;
	NTSTATUS Status = NtSetInformationProcess(
		GetCurrentProcess(),
		PROCESSINFOCLASS.ProcessExecuteFlags,
		&Value,
		Value.sizeof);

	if (!(Status >= 0)) // NT_SUCCESS
		printf("NtSetInformationProcess: %s\n", NtStrError(Status));
}

enum MEM_EXECUTE_OPTION_DISABLE = 0x01;
enum MEM_EXECUTE_OPTION_ENABLE = 0x02;

enum PROCESSINFOCLASS
{
	ProcessExecuteFlags = 0x22,
}

version (CRuntime_Microsoft)
{
	extern(Windows) NTSTATUS NtSetInformationProcess(HANDLE, PROCESSINFOCLASS, PVOID, ULONG);
}
else
{
	alias extern(Windows) NTSTATUS function(HANDLE, PROCESSINFOCLASS, PVOID, ULONG) TNtSetInformationProcess;
	__gshared TNtSetInformationProcess pNtSetInformationProcess;

	NTSTATUS NtSetInformationProcess(HANDLE arg1, PROCESSINFOCLASS arg2, PVOID arg3, ULONG arg4)
	{
		auto fn = pNtSetInformationProcess;
		if (fn == null) goto load;
ok:
		return fn(arg1, arg2, arg3, arg4);
load:
		{
			HANDLE ntdll = LoadLibrary("ntdll");
			fn = cast(typeof(fn))GetProcAddress(ntdll, "NtSetInformationProcess");
			pNtSetInformationProcess = fn;
			CloseHandle(ntdll);
			goto ok;
		}
	}
}
