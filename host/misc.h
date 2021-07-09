#pragma once

#include <stdbool.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>

__attribute__((cold, noreturn))
void
assert_fail(const char *fmt, const char *arg1, const char *arg2);

const char *
superbasename(const char *path_);

char *
strchrnul(char *s, char c);

bool
atoi_ok(const char *s, int* out);

bool
read_full(int fd, void *p_, size_t sz);

bool
write_full(int fd, const void *p_, size_t sz);

LPCSTR
StrError(LONG Code);

VOID
PrintError(LPCSTR What);

// link with -lntdll
extern ULONG WINAPI
RtlNtStatusToDosError(NTSTATUS Status);

#define NtStrError(Code) StrError(RtlNtStatusToDosError(Code))
