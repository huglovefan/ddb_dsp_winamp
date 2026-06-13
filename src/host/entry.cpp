#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <shellapi.h>
#include "main.hpp"
#include "misc.hpp"

#if defined(UNITTEST)

#include <stdio.h>

_unittest_ *_unittest_head_;

WINAPI int wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	srand(GetTickCount());

	for (_unittest_ *p = _unittest_head_; p; p = p->next)
	{
		printf("RUN: %s:%d\n", p->file, p->line);
		p->fn();
	}

	printf("end\n");

	return 0;
}

#else

WINAPI int wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	wchar_t **argv;
	int argc;
	int rv;

	/* yes, use GetCommandLine() instead of the parameter:
	   https://devblogs.microsoft.com/oldnewthing/20100916-00/?p=12843
	   */
	argv = CommandLineToArgvW(GetCommandLine(), &argc);

	if (!argv)
		__builtin_trap();

	rv = ddw_main(std::span(argv, argc));

	LocalFree(argv);

	return rv;
}

#endif
