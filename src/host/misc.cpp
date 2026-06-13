#include "misc.hpp"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <windef.h>
#include <winbase.h>
#include <ntstatus.h>
#include <winternl.h>
#include <io.h>

wchar_t *strchrnul(wchar_t *s, int c)
{
	while (*s && *s != c)
		s++;

	return s;
}

wchar_t *superbasename(wchar_t *const path)
{
	wchar_t *slash;
	wchar_t *p;

	assert(path);

	slash = nullptr;

	for (p = path; *p; p++)
		if (*p == '/' || *p == '\\')
			slash = p;

	if (!slash)
		return path;

	return slash+1;
}

bool read_full(int fd, void *p_, size_t sz)
{
	char *p;
	const char *base;
	int rv;

	if (!sz)
	{
		errno = EINVAL;
		return false;
	}

	p = static_cast<char *>(p_);
	base = p;

	while (sz)
	{
		rv = read(fd, p, (sz <= INT_MAX) ? sz : INT_MAX);

		if (rv < 0)
			return false;

		if (!rv)
		{
			errno = (p != base) ? EIO : 0;
			return false;
		}

		sz -= rv;
		p += rv;
	}

	return true;
}

bool write_full(int fd, const void *p_, size_t sz)
{
	const char *p;
	int rv;

	if (!sz)
	{
		errno = EINVAL;
		return false;
	}

	p = static_cast<const char *>(p_);

	while (sz)
	{
		rv = write(fd, p, (sz <= INT_MAX) ? sz : INT_MAX);

		if (rv < 0)
			return false;

		if (!rv)
		{
			errno = EIO;
			return false;
		}

		sz -= rv;
		p += rv;
	}

	return true;
}

const wchar_t *StrError(unsigned long Code)
{
	enum { buflen = 512 };
	static wchar_t buf[buflen];
	unsigned long length;

	length = FormatMessage(
	    FORMAT_MESSAGE_FROM_SYSTEM,
	    nullptr,
	    Code,
	    0,
	    buf,
	    buflen,
	    nullptr);

	if (length)
		/* remove trailing newlines */
		while (length && buf[length-1] < 32)
			buf[--length] = 0;
	else
		buf[length] = 0;

	return buf;
}

void PrintError(const char *What)
{
	const wchar_t *se;

	se = StrError(GetLastError());

	if (What)
		fprintf(stderr, "%s: %ls\n", What, se);
	else
		fprintf(stderr, "%ls\n", se);
}

const wchar_t *NtStrError(NTSTATUS status)
{
	enum { buflen = 24 };
	static wchar_t buf[buflen];
	unsigned long error;

	error = RtlNtStatusToDosError(status);

	if (error == ERROR_MR_MID_NOT_FOUND &&
	    status != STATUS_MESSAGE_NOT_FOUND)
	{
		/* couldn't convert the error code. return a
		   stringified version of the ntstatus number. */
		snwprintf(buf, buflen, L"NTSTATUS %ld", status);
		return buf;
	}

	return StrError(error);
}

void abort_msg(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	abort();
}

void abort_fmt(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	abort();
}

#undef OutputDebugStringf
void OutputDebugStringf(const wchar_t *fmt, ...)
{
	va_list ap;
	wchar_t *buf;
	int len;

	va_start(ap, fmt);
	len = _vscwprintf(fmt, ap);
	va_end(ap);

	if (len < 0
	    || (uintmax_t)len >= (uintmax_t)SIZE_MAX/sizeof(wchar_t))
		return;

	buf = (wchar_t *)malloc(((size_t)len+1)*sizeof(wchar_t));

	if (!buf)
		return;

	va_start(ap, fmt);
	len = vswprintf(buf, (size_t)len+1, fmt, ap);
	va_end(ap);

	if (len >= 0)
		OutputDebugString(buf);

	free(buf);
}
