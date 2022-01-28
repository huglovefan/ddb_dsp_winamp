module misclib.druntime.threadinit;

version (D_BetterC) {} else: // betterC doesn't have the runtime in the first place

import core.thread.osthread : Thread, rt_moduleTlsCtor, rt_moduleTlsDtor, thread_attachThis;
import core.thread.threadbase : thread_detachThis;
import misclib.os.atexit_thread : ThreadExitHandler;

pragma(inline, true)
void initForeignThread() nothrow
{
	if (!Thread.getThis())
		doInitForeignThread();
}

// -----------------------------------------------------------------------------

private:

void doInitForeignThread() nothrow
{
	ThreadExitHandler handler = {&deinitForeignThread, cast(void*)1};

	if (!handler.allocate())
		assert(0);

	try
	{
		thread_attachThis();
		rt_moduleTlsCtor();
	}
	catch (Throwable e)
	{
		assert(0, e.msg);
	}

	if (!handler.enable())
		assert(0);
}

extern (C) void deinitForeignThread(void*) nothrow
{
	try
	{
		rt_moduleTlsDtor();
		thread_detachThis();
	}
	catch (Throwable e)
	{
		assert(0, e.msg);
	}
}
