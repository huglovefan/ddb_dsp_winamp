struct ThreadExitHandler
{
	extern (C) void function(void*) fn;
	void* ptr;

	version (Posix)
	{
		private pthread_key_t key;

		bool allocate() nothrow @nogc
		{
			int err = pthread_key_create(&key, fn);
			return err == 0;
		}
		bool enable() nothrow @nogc
		{
			int err = pthread_setspecific(key, ptr);
			return err == 0;
		}
		bool cancel() nothrow @nogc
		{
			int err = pthread_key_delete(key);
			return err == 0;
		}
	}

	version (Windows)
	{
		private DWORD idx;

		bool allocate() nothrow @nogc
		{
			idx = FlsAlloc(fn);
			return idx != FLS_OUT_OF_INDEXES;
		}
		bool enable() nothrow @nogc
		{
			BOOL ok = FlsSetValue(idx, ptr);
			return ok != 0;
		}
		bool cancel() nothrow @nogc
		{
			BOOL ok = FlsFree(idx);
			return ok != 0;
		}
	}
}

// -----------------------------------------------------------------------------

private:

version (Posix)
{
	import core.sys.posix.pthread : pthread_key_create, pthread_key_delete, pthread_key_t, pthread_setspecific;
}

version (Windows)
{
	import core.sys.windows.windef : BOOL, DWORD, PVOID;

	pragma(lib, "kernel32");

	// https://docs.microsoft.com/en-us/windows/win32/api/fibersapi/nf-fibersapi-flsalloc
	extern (Windows) DWORD FlsAlloc(PFLS_CALLBACK_FUNCTION) nothrow @nogc;
	enum DWORD FLS_OUT_OF_INDEXES = -1;

	// https://docs.microsoft.com/en-us/windows/win32/api/winnt/nc-winnt-pfls_callback_function
	alias PFLS_CALLBACK_FUNCTION = extern (C) void function(PVOID);

	// https://docs.microsoft.com/en-us/windows/win32/api/fibersapi/nf-fibersapi-flssetvalue
	extern (Windows) BOOL FlsSetValue(DWORD, PVOID) nothrow @nogc;

	// https://docs.microsoft.com/en-us/windows/win32/api/fibersapi/nf-fibersapi-flsfree
	extern (Windows) BOOL FlsFree(DWORD) nothrow @nogc;
}
