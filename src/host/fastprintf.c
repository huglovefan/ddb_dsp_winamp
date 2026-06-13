#include "fastprintf.h"

#include <stdbool.h>
#include <stdio.h>
#include <windows.h>

#undef fastprintf

/* this entire file was originally from chatgpt. */

/* !!!NOTE!!! i'm not sure the console write path works at all in wine.
   needs testing to find out why. but at least the other path *is*
   faster than regular printf. */

/* chatgpt */
static bool is_console(HANDLE h)
{
	DWORD mode;

	return !!GetConsoleMode(h, &mode);
}

/* chatgpt */
static int write_file(HANDLE h, const char *buf, size_t len)
{
	DWORD wrote;

	while (len)
	{
		if (!WriteFile(h, buf, len, &wrote, NULL))
			return -1;

		buf += wrote;
		len -= wrote;
	}

	return 0;
}

/* chatgpt */
static int write_console(HANDLE h, const wchar_t *buf, size_t len)
{
	DWORD wrote;

	while (len)
	{
		if (!WriteConsole(h, buf, len, &wrote, NULL))
			return -1;

		buf += wrote;
		len -= wrote;
	}

	return 0;
}

/* chatgpt */
static bool write_file_conv(HANDLE h, const wchar_t *wstr, int wlen)
{
	int nbytes;
	char *utf8;

	nbytes = WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, NULL, 0, NULL, NULL);
	if (nbytes <= 0)
		return false;

	utf8 = (char *)malloc(nbytes);
	if (!utf8)
		return false;

	nbytes = WideCharToMultiByte(CP_UTF8, 0, wstr, wlen, utf8, nbytes, NULL, NULL);
	if (nbytes <= 0)
	{
		free(utf8);
		return false;
	}

	if (write_file(h, utf8, nbytes))
	{
		free(utf8);
		return false;
	}

	free(utf8);

	return true;
}

/* chatgpt */
static int fastvprintf(const wchar_t *fmt, va_list ap)
{
	enum { BUF = 512 };
	wchar_t stackbuf[BUF];
	HANDLE h;
	wchar_t *dyn;
	wchar_t *out;
	va_list ap2;
	int len;

	h = GetStdHandle(STD_OUTPUT_HANDLE);
	if (!h || h == INVALID_HANDLE_VALUE)
		return -1;

	va_copy(ap2, ap);
	len = vswprintf(stackbuf, BUF, fmt, ap2);
	va_end(ap2);

	dyn = NULL;
	out = stackbuf;

	if (len < 0)
	{
		va_copy(ap2, ap);
		len = _vscwprintf(fmt, ap2);
		va_end(ap2);

		if (len < 0)
			return -1;

		dyn = (wchar_t *)calloc((size_t)len + 1, sizeof(wchar_t));
		if (!dyn)
			return -1;

		va_copy(ap2, ap);
		len = vswprintf(dyn, (size_t)len + 1, fmt, ap2);
		va_end(ap2);

		if (len < 0)
		{
			free(dyn);
			return -1;
		}

		out = dyn;
	}

	if (is_console(h)
	    ? write_console(h, out, len)
	    : write_file_conv(h, out, len))
		len = -1;

	if (dyn)
		free(dyn);

	return len;
}

/* chatgpt */
int fastprintf(const wchar_t *fmt, ...)
{
	va_list ap;
	int rv;

	va_start(ap, fmt);
	rv = fastvprintf(fmt, ap);
	va_end(ap);

	return rv;
}
