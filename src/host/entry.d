module ddw.host.entry;

import core.sys.windows.windef;
import ddw.host.main : _Dmain;

// https://github.com/dlang/druntime/blob/master/src/rt/dmain2.d
extern(C) int _d_run_main(int, char**, MainFunc) nothrow @nogc;
alias extern(C) int function(const(char)[][]) MainFunc;

extern(Windows)
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	return _d_run_main(0, null, &_Dmain);
}
