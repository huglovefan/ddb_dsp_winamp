#include "shm.h"

#include <stdio.h>

#include "misc.h"

// constants for NtSetInformationProcess()
#ifndef MEM_EXECUTE_OPTION_DISABLE
 #define MEM_EXECUTE_OPTION_DISABLE 0x01
#endif
#ifndef MEM_EXECUTE_OPTION_ENABLE
 #define MEM_EXECUTE_OPTION_ENABLE 0x02
#endif

void *
shmnew(const char *path, size_t sz)
{
	HANDLE File = INVALID_HANDLE_VALUE;
	HANDLE Mapping = NULL;
	LONG MapError;
	LPVOID View = NULL;
	NTSTATUS DEPEnableStatus;

	File = CreateFile(path,
	                  GENERIC_READ|GENERIC_WRITE,
	                  FILE_SHARE_READ|FILE_SHARE_WRITE,
	                  NULL,
	                  OPEN_EXISTING,
	                  0,
	                  NULL);
	if (File == INVALID_HANDLE_VALUE) {
		PrintError("CreateFile");
		goto out;
	}

	Mapping = CreateFileMapping(File,
	                            NULL,
	                            PAGE_READWRITE,
	                            0,
	                            0,
	                            NULL);
	if (Mapping == NULL) {
		PrintError("CreateFileMapping");
		goto out;
	}

	//
	// temporarily enable DEP to prevent the file from being mapped as
	//  executable (fails if /dev/shm is mounted noexec)
	//
	DEPEnableStatus = NtSetInformationProcess(GetCurrentProcess(),
	                                          ProcessExecuteFlags,
	                                          &(LONG){MEM_EXECUTE_OPTION_DISABLE},
	                                          sizeof(LONG));
	if (!NT_SUCCESS(DEPEnableStatus))
		fprintf(stderr, "NtSetInformationProcess: %s\n", NtStrError(DEPEnableStatus));

	View = MapViewOfFile(Mapping,
	                     FILE_MAP_READ|FILE_MAP_WRITE,
	                     0,
	                     0,
	                     sz);
	MapError = GetLastError();

	//
	// re-disable DEP if it was successfully enabled
	//
	if (NT_SUCCESS(DEPEnableStatus)) {
		NTSTATUS Status;
		Status = NtSetInformationProcess(GetCurrentProcess(),
		                                 ProcessExecuteFlags,
		                                 &(LONG){MEM_EXECUTE_OPTION_ENABLE},
		                                 sizeof(LONG));
		if (!NT_SUCCESS(Status))
			fprintf(stderr, "NtSetInformationProcess: %s\n", NtStrError(Status));
	}

	if (View == NULL) {
		fprintf(stderr, "MapViewOfFile: %s\n", StrError(MapError));
		goto out;
	}
out:
	//
	// these are always safe to close
	//
	if (Mapping != NULL)
		CloseHandle(Mapping);
	if (File != INVALID_HANDLE_VALUE)
		CloseHandle(File);

	return View;
}
