#include "shm.hpp"

#include <stdio.h>
#include <windef.h>
#include <winbase.h>
#include <ntdef.h>
#include <winternl.h>
#include "misc.hpp"

/* missing from mingw-w64 headers:
   https://github.com/wine-mirror/wine/blob/wine-10.0/include/winternl.h#L1904
   */
enum
{
	MEM_EXECUTE_OPTION_DISABLE = 0x01,
	MEM_EXECUTE_OPTION_ENABLE  = 0x02
};

/**
 * enable "data execution prevention", which means mmap'd files won't
 * have the execute permission by default.
 * 
 * this is required if the shm file is on a filesystem mounted "noexec".
 */
static bool set_dep(bool state)
{
	ULONG info;
	NTSTATUS status;

	/* true means "enable DEP", which means "disable memory
	   execution". */
	info = (state)
	    ? MEM_EXECUTE_OPTION_DISABLE
	    : MEM_EXECUTE_OPTION_ENABLE;

	status = NtSetInformationProcess(
	    GetCurrentProcess(),
	    ProcessExecuteFlags,
	    &info,
	    sizeof(info));

	if (!NT_SUCCESS(status))
	{
		printf(
		    "NtSetInformationProcess: %ls\n",
		    NtStrError(status));
		return false;
	}

	return true;
}

void *shmnew(const wchar_t *path, size_t sz)
{
	HANDLE file = INVALID_HANDLE_VALUE;
	HANDLE mapping = nullptr;
	bool didEnableDEP = false;
	void *view = nullptr;

	file = CreateFile(
	    path,
	    GENERIC_READ|GENERIC_WRITE,
	    FILE_SHARE_READ|FILE_SHARE_WRITE,
	    nullptr,
	    OPEN_EXISTING,
	    0,
	    nullptr);

	if (file == INVALID_HANDLE_VALUE)
	{
		PrintError("CreateFile");
		goto end;
	}

	mapping = CreateFileMapping(
	    file,
	    nullptr,
	    PAGE_READWRITE,
	    0,
	    0,
	    nullptr);

	if (!mapping)
	{
		PrintError("CreateFileMapping");
		goto end;
	}

	didEnableDEP = set_dep(true);

	view = MapViewOfFile(
	    mapping,
	    FILE_MAP_READ|FILE_MAP_WRITE,
	    0,
	    0,
	    sz);

	if (!view)
	{
		PrintError("MapViewOfFile");
		goto end;
	}

end:

	if (didEnableDEP)
		set_dep(false);

	if (mapping)
		CloseHandle(mapping);

	if (file != INVALID_HANDLE_VALUE)
		CloseHandle(file);

	return view;
}
