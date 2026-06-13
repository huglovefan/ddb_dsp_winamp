#pragma once

#include <processthreadsapi.h> /* GetCurrentThreadId */
#include <synchapi.h> /* SRWLOCK */
#include "fastprintf.h"
#if defined(__cplusplus)
# include <span>
# include "plugin.hpp"
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(__cplusplus)
int ddw_main(std::span<wchar_t *> args);
#endif

void main_request_exit(HWND hwnd);

bool main_handle_ipc_fast(
    LRESULT *rv_out,
    HWND     hWnd,
    UINT     uMsg,
    WPARAM   wParam,
    LPARAM   lParam,
    void    *ud);

struct plugin_list
{
	struct Plugin *head;
	struct Plugin *tail;
	size_t count;
	/* protects head, tail and count. */
	/* !!!TODO!!! this doesn't have to be a SRWLOCK. find something
	   that fits better. */
	SRWLOCK       _lk;
	volatile long _xowner;
};

static inline void plugin_list_lock_rdonly(struct plugin_list *pls, const char *file, int line)
{
	DWORD self;
	DWORD old;

	self = GetCurrentThreadId();

	/* not currently RW by us */
	old = InterlockedCompareExchange(&pls->_xowner, self, self);
	if (old == self)
	{
		fastprintf("%s(%d): lock error 1\n", file, line);
		__builtin_trap();
	}

	//~ fastprintf("%s(%d): lock rdonly\n", file, line);

	AcquireSRWLockShared(&pls->_lk);

	/* RW by nobody */
	old = InterlockedCompareExchange(&pls->_xowner, 0, 0);
	if (old)
	{
		fastprintf("%s(%d): lock error 2\n", file, line);
		__builtin_trap();
	}
}

static inline void plugin_list_unlock_rdonly(struct plugin_list *pls, const char *file, int line)
{
	DWORD old;

	/* still RW by nobody */
	old = InterlockedCompareExchange(&pls->_xowner, 0, 0);
	if (old)
	{
		fastprintf("%s(%d): lock error\n", file, line);
		__builtin_trap();
	}

	//~ fastprintf("%s(%d): unlock rdonly\n", file, line);

	ReleaseSRWLockShared(&pls->_lk);
}

static inline void plugin_list_lock_rdwr(struct plugin_list *pls, const char *file, int line)
{
	DWORD self;
	DWORD old;

	self = GetCurrentThreadId();

	/* not currently RW by us */
	old = InterlockedCompareExchange(&pls->_xowner, self, self);
	if (old == self)
	{
		fastprintf("%s(%d): lock error 1\n", file, line);
		__builtin_trap();
	}

	//~ fastprintf("%s(%d): lock rdwr\n", file, line);

	AcquireSRWLockExclusive(&pls->_lk);

	/* tracking variable RW by nobody, set to us */
	old = InterlockedCompareExchange(&pls->_xowner, self, 0);
	if (old)
	{
		fastprintf("%s(%d): lock error 2\n", file, line);
		__builtin_trap();
	}
}

static inline void plugin_list_unlock_rdwr(struct plugin_list *pls, const char *file, int line)
{
	DWORD self;
	DWORD old;

	self = GetCurrentThreadId();

	/* tracking variable RW by us, set to nobody */
	old = InterlockedCompareExchange(&pls->_xowner, 0, self);
	if (old != self)
	{
		fastprintf("%s(%d): lock error\n", file, line);
		__builtin_trap();
	}

	//~ fastprintf("%s(%d): unlock rdwr\n", file, line);

	ReleaseSRWLockExclusive(&pls->_lk);
}

static inline void plugin_list_assert_rdwr(struct plugin_list *pls, const char *file, int line)
{
	DWORD self;
	DWORD old;

	self = GetCurrentThreadId();

	/* check RW by us */
	old = InterlockedCompareExchange(&pls->_xowner, self, self);
	if (old != self)
	{
		fastprintf("%s(%d): lock error\n", file, line);
		__builtin_trap();
	}
}

#define PLUGIN_LIST_LOCK_RDONLY(pls) \
	plugin_list_lock_rdonly((pls), __FILE__, __LINE__)

#define PLUGIN_LIST_LOCK_RDWR(pls) \
	plugin_list_lock_rdwr((pls), __FILE__, __LINE__)

#define PLUGIN_LIST_UNLOCK_RDONLY(pls) \
	plugin_list_unlock_rdonly((pls), __FILE__, __LINE__)

#define PLUGIN_LIST_UNLOCK_RDWR(pls) \
	plugin_list_unlock_rdwr((pls), __FILE__, __LINE__)

#define PLUGIN_LIST_ASSERT_RDWR(pls) \
	plugin_list_assert_rdwr((pls), __FILE__, __LINE__)

#define PLUGIN_LIST_FOREACH(pl, pls) \
	for ((pl) = (pls)->head; (pl); (pl) = (pl)->next)

#if defined(__cplusplus)
}
#endif
