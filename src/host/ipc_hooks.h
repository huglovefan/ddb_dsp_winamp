#pragma once

#include <stdbool.h>
#include <windef.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct hook
{
	const char *name;
	void      (*fn)(void);
	void      (*orig)(void);
};

#define GET_HOOK_USERDATA(ud_out) __asm__ __volatile__("mov %%eax, %0" : "=rm"(*ud_out) :: "eax")

struct installed_hook_info
{
	void  *_map;
	size_t _mapsize;
	void  *_infos;
	size_t _infocnt;
};

bool hook_ipc_funcs(
    HMODULE                     dll,
    struct installed_hook_info *hi,
    void                       *ud);

void unhook_ipc_funcs(
    HMODULE                     dll,
    struct installed_hook_info *hi);

#if defined(__cplusplus)
}
#endif
