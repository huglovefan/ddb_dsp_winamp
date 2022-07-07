module ddw.host.entry;

import core.sys.windows.windef;
import ddw.host.main : ddw_main;

/*
 * entry point to run the D main function
 * required when using "/subsystem:windows"
 */

extern(Windows)
int WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	return _d_run_main(0, null, &ddw_main);
}

// https://github.com/dlang/druntime/blob/master/src/rt/dmain2.d
extern(C) int _d_run_main(int, char**, MainFunc) nothrow;
alias extern(C) int function(string[]) MainFunc;
