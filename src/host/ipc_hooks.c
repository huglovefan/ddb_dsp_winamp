#include "ipc_hooks.h"

#include <windows.h>
#include <stdbool.h>
#include <stddef.h>
#include "main.hpp"

/* chatgpt: hook functions with the correct signatures (hook_*) */
/* chatgpt: function to install hooks */

/* me: the idea, the interface */

#include "fastprintf.h"
#define PRINTF(x, ...) fastprintf(x, ##__VA_ARGS__)

enum
{
	HOOK_SENDMESSAGEA,
	HOOK_SENDMESSAGEW,
	HOOK_SENDDLGITEMMESSAGEA,
	HOOK_SENDDLGITEMMESSAGEW,
	HOOK_SENDMESSAGETIMEOUTA,
	HOOK_SENDMESSAGETIMEOUTW,
	HOOK_SENDNOTIFYMESSAGEA,
	HOOK_SENDNOTIFYMESSAGEW,
	HOOK_SENDMESSAGECALLBACKA,
	HOOK_SENDMESSAGECALLBACKW,
	HOOK_GETPROCADDRESS,
	HOOK_count
};

#define ORIG(hk, e) ((__typeof__(&hk))hooks[e].orig)

static bool try_fastpath(
	LRESULT *rv_out,
	HWND     hWnd,
	UINT     uMsg,
	WPARAM   wParam,
	LPARAM   lParam,
	void    *ud)
{
	return main_handle_ipc_fast(
	    rv_out,
	    hWnd,
	    uMsg,
	    wParam,
	    lParam,
	    ud);
}

static LRESULT WINAPI hook_SendMessageA(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static LRESULT WINAPI hook_SendMessageW(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI hook_SendDlgItemMessageA(HWND hDlg, int nIDDlgItem, UINT uMsg, WPARAM wParam, LPARAM lParam);
static LRESULT WINAPI hook_SendDlgItemMessageW(HWND hDlg, int nIDDlgItem, UINT uMsg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI hook_SendMessageTimeoutA(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT fuFlags, UINT uTimeout, PDWORD_PTR lpdwResult);
static LRESULT WINAPI hook_SendMessageTimeoutW(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT fuFlags, UINT uTimeout, PDWORD_PTR lpdwResult);

static BOOL WINAPI hook_SendNotifyMessageA(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
static BOOL WINAPI hook_SendNotifyMessageW(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

static BOOL WINAPI hook_SendMessageCallbackA(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, SENDASYNCPROC lpResultCallBack, ULONG_PTR dwData);
static BOOL WINAPI hook_SendMessageCallbackW(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, SENDASYNCPROC lpResultCallBack, ULONG_PTR dwData);

static FARPROC hook_GetProcAddress(HMODULE hModule, LPCSTR lpProcName);

static struct hook hooks[HOOK_count] = {
	[HOOK_SENDMESSAGEA]         = {"SendMessageA",         (void(*)(void))hook_SendMessageA},
	[HOOK_SENDMESSAGEW]         = {"SendMessageW",         (void(*)(void))hook_SendMessageW},
	[HOOK_SENDDLGITEMMESSAGEA]  = {"SendDlgItemMessageA",  (void(*)(void))hook_SendDlgItemMessageA},
	[HOOK_SENDDLGITEMMESSAGEW]  = {"SendDlgItemMessageW",  (void(*)(void))hook_SendDlgItemMessageW},
	[HOOK_SENDMESSAGETIMEOUTA]  = {"SendMessageTimeoutA",  (void(*)(void))hook_SendMessageTimeoutA},
	[HOOK_SENDMESSAGETIMEOUTW]  = {"SendMessageTimeoutW",  (void(*)(void))hook_SendMessageTimeoutW},
	[HOOK_SENDNOTIFYMESSAGEA]   = {"SendNotifyMessageA",   (void(*)(void))hook_SendNotifyMessageA},
	[HOOK_SENDNOTIFYMESSAGEW]   = {"SendNotifyMessageW",   (void(*)(void))hook_SendNotifyMessageW},
	[HOOK_SENDMESSAGECALLBACKA] = {"SendMessageCallbackA", (void(*)(void))hook_SendMessageCallbackA},
	[HOOK_SENDMESSAGECALLBACKW] = {"SendMessageCallbackW", (void(*)(void))hook_SendMessageCallbackW},
	[HOOK_GETPROCADDRESS]       = {"GetProcAddress",       (void(*)(void))hook_GetProcAddress},
};

static LRESULT WINAPI hook_SendMessageA(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LRESULT rv;
	void *ud;

	GET_HOOK_USERDATA(&ud);
	if (try_fastpath(&rv, hWnd, uMsg, wParam, lParam, ud))
		return rv;

	return ORIG(hook_SendMessageA, HOOK_SENDMESSAGEA)(hWnd, uMsg, wParam, lParam);
}

static LRESULT WINAPI hook_SendMessageW(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LRESULT rv;
	void *ud;

	GET_HOOK_USERDATA(&ud);
	if (try_fastpath(&rv, hWnd, uMsg, wParam, lParam, ud))
		return rv;

	return ORIG(hook_SendMessageW, HOOK_SENDMESSAGEW)(hWnd, uMsg, wParam, lParam);
}

static LRESULT WINAPI hook_SendDlgItemMessageA(HWND hDlg, int nIDDlgItem, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LRESULT rv;
	HWND hCtrl;
	void *ud;

	GET_HOOK_USERDATA(&ud);
	hCtrl = GetDlgItem(hDlg, nIDDlgItem);
	if (hCtrl && try_fastpath(&rv, hCtrl, uMsg, wParam, lParam, ud))
		return rv;

	return ORIG(hook_SendDlgItemMessageA, HOOK_SENDDLGITEMMESSAGEA)(hDlg, nIDDlgItem, uMsg, wParam, lParam);
}

static LRESULT WINAPI hook_SendDlgItemMessageW(HWND hDlg, int nIDDlgItem, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LRESULT rv;
	HWND hCtrl;
	void *ud;

	GET_HOOK_USERDATA(&ud);
	hCtrl = GetDlgItem(hDlg, nIDDlgItem);
	if (hCtrl && try_fastpath(&rv, hCtrl, uMsg, wParam, lParam, ud))
		return rv;

	return ORIG(hook_SendDlgItemMessageW, HOOK_SENDDLGITEMMESSAGEW)(hDlg, nIDDlgItem, uMsg, wParam, lParam);
}

static LRESULT WINAPI hook_SendMessageTimeoutA(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT fuFlags, UINT uTimeout, PDWORD_PTR lpdwResult)
{
	LRESULT rv;
	void *ud;

	GET_HOOK_USERDATA(&ud);
	if (try_fastpath(&rv, hWnd, uMsg, wParam, lParam, ud))
	{
		if (lpdwResult)
			*lpdwResult = (DWORD_PTR)rv;

		return 1;
	}

	return ORIG(hook_SendMessageTimeoutA, HOOK_SENDMESSAGETIMEOUTA)(hWnd, uMsg, wParam, lParam, fuFlags, uTimeout, lpdwResult);
}

static LRESULT WINAPI hook_SendMessageTimeoutW(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT fuFlags, UINT uTimeout, PDWORD_PTR lpdwResult)
{
	LRESULT rv;
	void *ud;

	GET_HOOK_USERDATA(&ud);
	if (try_fastpath(&rv, hWnd, uMsg, wParam, lParam, ud))
	{
		if (lpdwResult)
			*lpdwResult = (DWORD_PTR)rv;

		return 1;
	}

	return ORIG(hook_SendMessageTimeoutW, HOOK_SENDMESSAGETIMEOUTW)(hWnd, uMsg, wParam, lParam, fuFlags, uTimeout, lpdwResult);
}

static BOOL WINAPI hook_SendNotifyMessageA(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LRESULT rv;
	void *ud;

	GET_HOOK_USERDATA(&ud);
	if (try_fastpath(&rv, hWnd, uMsg, wParam, lParam, ud))
		return TRUE;

	return ORIG(hook_SendNotifyMessageA, HOOK_SENDNOTIFYMESSAGEA)(hWnd, uMsg, wParam, lParam);
}

static BOOL WINAPI hook_SendNotifyMessageW(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LRESULT rv;
	void *ud;

	GET_HOOK_USERDATA(&ud);
	if (try_fastpath(&rv, hWnd, uMsg, wParam, lParam, ud))
		return TRUE;

	return ORIG(hook_SendNotifyMessageW, HOOK_SENDNOTIFYMESSAGEW)(hWnd, uMsg, wParam, lParam);
}

static BOOL WINAPI hook_SendMessageCallbackA(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, SENDASYNCPROC lpResultCallBack, ULONG_PTR dwData)
{
	LRESULT rv;
	void *ud;

	GET_HOOK_USERDATA(&ud);
	if (try_fastpath(&rv, hWnd, uMsg, wParam, lParam, ud))
	{
		if (lpResultCallBack)
			lpResultCallBack(hWnd, uMsg, dwData, rv);

		return TRUE;
	}

	return ORIG(hook_SendMessageCallbackA, HOOK_SENDMESSAGECALLBACKA)(hWnd, uMsg, wParam, lParam, lpResultCallBack, dwData);
}

static BOOL WINAPI hook_SendMessageCallbackW(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, SENDASYNCPROC lpResultCallBack, ULONG_PTR dwData)
{
	LRESULT rv;
	void *ud;

	GET_HOOK_USERDATA(&ud);
	if (try_fastpath(&rv, hWnd, uMsg, wParam, lParam, ud))
	{
		if (lpResultCallBack)
			lpResultCallBack(hWnd, uMsg, dwData, rv);

		return TRUE;
	}

	return ORIG(hook_SendMessageCallbackW, HOOK_SENDMESSAGECALLBACKW)(hWnd, uMsg, wParam, lParam, lpResultCallBack, dwData);
}

static FARPROC hook_GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
	//~ fastprintf("look up %s\n", lpProcName);

	return ORIG(hook_GetProcAddress, HOOK_GETPROCADDRESS)(hModule, lpProcName);
}

static bool patch_iat_slot(void(**slot)(void), void (*replacement)(void))
{
	DWORD old_prot;
	DWORD unused;

	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old_prot))
	{
		PRINTF("VirtualProtect PAGE_READWRITE: %lu\n", GetLastError());
		return false;
	}

	*slot = replacement;

	if (!VirtualProtect(slot, sizeof(*slot), old_prot, &unused))
	{
		PRINTF("VirtualProtect old_protect: %lu\n", GetLastError());
		return false;
	}

	return true;
}

enum code_regs
{
	AX,
	CX,
	DX,
	BX,
	SP,
	BP,
	SI,
	DI
};

static size_t code_jmp_addr(unsigned char *p, void *ptr)
{
	ptrdiff_t offset;
	if (p)
	{
		p[0] = 0xe9;
		offset = (unsigned char *)ptr - (p+5);
		memcpy(&p[1], &offset, 4);
	}
	return 5;
}

static size_t code_mov_to_reg(
	unsigned char *p,
	enum code_regs reg,
	uintptr_t      value)
{
	// https://www.felixcloutier.com/x86/mov
	// B8+ rd id -> MOV r32, imm32
	if (p)
	{
		p[0] = 0xb8 | (char)reg;
		memcpy(&p[1], &value, 4);
	}
	return 5;
}

struct patch_info
{
	size_t  hookidx;
	void (**slot)(void);
	void  (*tramp)(void);
	void  (*orig)(void);
};

struct patch_state
{
	HMODULE               dll;
	struct hook          *fns;
	size_t                cnt;
	void                 *ud;

	unsigned char        *base;
	IMAGE_DATA_DIRECTORY *import_dir;

	struct patch_info    *infos;
	size_t                infocnt;
	size_t                infocap;

	void                 *tramps;
	size_t                trampbufsize;
};

static void found_patch(struct patch_state *s, size_t hookidx, void (**slot)(void))
{
	if (s->infocap == s->infocnt)
	{
		size_t newcap;
		void *p;

		newcap = s->infocap+16;

		p = realloc(s->infos, newcap*sizeof(struct patch_info));
		if (!p)
			return;
		s->infos = (struct patch_info *)p;
		s->infocap = newcap;
	}

	s->infos[s->infocnt++] = (struct patch_info){
		.slot    = slot,
		.hookidx = hookidx,
	};
}

static void inner(struct patch_state *s)
{
	IMAGE_IMPORT_DESCRIPTOR *desc;

	desc = (IMAGE_IMPORT_DESCRIPTOR *)&s->base[s->import_dir->VirtualAddress];

	/* for each dll that the module imports... */
	for (; desc->Name; desc++)
	{
		IMAGE_THUNK_DATA32 *first_thunk;
		IMAGE_THUNK_DATA32 *orig_thunk;
		const char         *import_dll_name;
		HMODULE             import_dll;
		size_t              i;

		import_dll_name = (const char *)&s->base[desc->Name];
		first_thunk = (IMAGE_THUNK_DATA32 *)&s->base[desc->FirstThunk];
		orig_thunk = NULL;
		import_dll = GetModuleHandleA(import_dll_name);

		if (desc->OriginalFirstThunk)
			orig_thunk = (IMAGE_THUNK_DATA32 *)&s->base[desc->OriginalFirstThunk];

		/* for each function imported from that dll... */
		for (; first_thunk->u1.Function; first_thunk++)
		{
			const char *import_name;

			import_name = NULL;

			/* Preferred path: use OriginalFirstThunk so we
			   can match by imported name. */
			if (orig_thunk)
			{
				if (!IMAGE_SNAP_BY_ORDINAL32(orig_thunk->u1.Ordinal))
				{
					IMAGE_IMPORT_BY_NAME *import_by_name;

					import_by_name = (IMAGE_IMPORT_BY_NAME *)&s->base[orig_thunk->u1.AddressOfData];
					import_name = (const char *)import_by_name->Name;
				}

				orig_thunk++;
			}

			/* for each hook... */
			for (i = 0; i != s->cnt; i++)
			{
				bool match;

				if (!s->fns[i].name || !s->fns[i].fn)
					continue;

				match = false;

				if (import_name)
				{
					if (!strcmp(import_name, s->fns[i].name))
						match = true;
				}
				else if (import_dll)
				{
					FARPROC proc;

					/* Fallback path: if
					   OriginalFirstThunk is absent,
					   compare the resolved IAT
					   target against
					   GetProcAddress(import_dll, name).
					   This still lets us identify
					   common named imports. */

					proc = GetProcAddress(import_dll, s->fns[i].name);

					if (proc && (void(*)(void))first_thunk->u1.Function == (void(*)(void))proc)
						match = true;
				}

				if (!match)
					continue;

				found_patch(s, i, (void(**)(void))&first_thunk->u1.Function);
			}
		}
	}
}

static inline void *code2data(void (*p)(void))
{
	return (void *)(uintptr_t)p;
}

static inline void (*data2code(void *p))(void)
{
	return (void(*)(void))(uintptr_t)p;
}

static bool create_tramps(struct patch_state *s)
{
	void *map;
	unsigned char *p;
	size_t needsize;
	size_t i;
	DWORD unused;

	needsize = 0;

	for (i = 0; i != s->infocnt; i++)
	{
		needsize += code_mov_to_reg(NULL, AX, (uintptr_t)s->ud);
		needsize += code_jmp_addr(NULL, (void *)(uintptr_t)s->fns[s->infos[i].hookidx].fn);
		needsize += (needsize % 4);
	}

	map = VirtualAlloc(NULL, needsize, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);

	if (!map)
	{
		fastprintf(
		    "VirtualAlloc failed (%lu)\n",
		    GetLastError());
		return false;
	}

	p = (unsigned char *)map;

	for (i = 0; i != s->infocnt; i++)
	{
		s->infos[i].tramp = data2code(p);
		p += code_mov_to_reg(p, AX, (uintptr_t)s->ud);
		p += code_jmp_addr(p, (void *)(uintptr_t)s->fns[s->infos[i].hookidx].fn);
		p += ((uintptr_t)p % 4);
	}

	if (!VirtualProtect(map, needsize, PAGE_EXECUTE_READ, &unused))
	{
		fastprintf(
		    "VirtualProtect failed (%lu)\n",
		    GetLastError());

		if (!VirtualFree(map, needsize, MEM_DECOMMIT))
			fastprintf(
			    "VirtualFree failed (%lu)\n",
			    GetLastError());

		return false;
	}

	s->tramps = map;
	s->trampbufsize = needsize;

	return true;
}

static bool install_tramps(struct patch_state *s)
{
	size_t i;
	BOOL ok;

	for (i = 0; i != s->infocnt; i++)
	{
		struct patch_info *info;
		struct hook *hk;

		info = &s->infos[i];
		hk = &s->fns[info->hookidx];

		info->orig = *info->slot;
		if (!hk->orig)
			hk->orig = *info->slot;

		if (!patch_iat_slot(info->slot, info->tramp))
		{
			PRINTF("%s(%d)\n", __FILE__, __LINE__);
			return false;
		}
	}

	ok = FlushInstructionCache(
	    GetModuleHandle(NULL),
	    s->tramps,
	    s->trampbufsize);

	if (!ok)
		fastprintf(
		    "FlushInstructionCache failed (%ld)\n",
		    GetLastError());

	return true;
}

static bool patch_dll_funcs(
	HMODULE                     dll,
	struct hook                *fns,
	size_t                      cnt,
	void                       *ud,
	struct installed_hook_info *hi)
{
	struct patch_state  s;
	IMAGE_DOS_HEADER   *dos;
	IMAGE_NT_HEADERS32 *nt;

	if (!dll)
		return false;

	s = (struct patch_state){
		.dll = dll,
		.fns = fns,
		.cnt = cnt,
		.ud  = ud,
	};

	s.base = (unsigned char *)dll;

	dos = (IMAGE_DOS_HEADER *)s.base;
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
	{
		PRINTF("%s(%d)\n", __FILE__, __LINE__);
		return false;
	}

	nt = (IMAGE_NT_HEADERS32 *)&s.base[dos->e_lfanew];
	if (nt->Signature != IMAGE_NT_SIGNATURE)
	{
		PRINTF("%s(%d)\n", __FILE__, __LINE__);
		return false;
	}

	s.import_dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!s.import_dir->VirtualAddress
	    || !s.import_dir->Size)
	{
		PRINTF("%s(%d)\n", __FILE__, __LINE__);
		return false;
	}

	inner(&s);

	if (!s.infocnt)
		return true;

	if (!create_tramps(&s))
	{
		free(s.infos);
		return false;
	}

	if (!install_tramps(&s))
	{
		fastprintf("failed to install trampolines\n");
		__builtin_trap();
	}

	hi->_map = s.tramps;
	hi->_mapsize = s.trampbufsize;
	hi->_infos = s.infos;
	hi->_infocnt = s.infocnt;

	return true;
}

static bool unpatch(struct installed_hook_info *hi)
{
	size_t i;

	for (i = 0; i != hi->_infocnt; i++)
	{
		struct patch_info *pi;

		pi = &((struct patch_info *)hi->_infos)[i];

		patch_iat_slot(pi->slot, pi->orig);
	}

	free(hi->_infos);
	hi->_infos = NULL;

	if (!VirtualFree(hi->_map, hi->_mapsize, MEM_DECOMMIT))
		fastprintf(
		    "VirtualFree failed (%lu)\n",
		    GetLastError());

	return true;
}

bool hook_ipc_funcs(HMODULE dll, struct installed_hook_info *hi, void *ud)
{
	return patch_dll_funcs(dll, hooks, HOOK_count, ud, hi);
}

void unhook_ipc_funcs(HMODULE dll, struct installed_hook_info *hi)
{
	unpatch(hi);
}

#if defined(UNITTEST)
#include <assert.h>
extern volatile int testcalled;
volatile int testcalled;
extern LRESULT WINAPI testhook_SendMessageW(
    HWND   hWnd,
    UINT   uMsg,
    WPARAM wParam,
    LPARAM lParam);
LRESULT WINAPI testhook_SendMessageW(
	HWND   hWnd,
	UINT   uMsg,
	WPARAM wParam,
	LPARAM lParam)
{
	void *ud;

	GET_HOOK_USERDATA(&ud);
	fastprintf("from hook - eax=%p\n", ud);
	testcalled++;
	return 0;
}
UNITTEST()
{
	enum { CNT = 1 };
	struct hook hooks[CNT] = {
		{"SendMessageW", (void(*)(void))testhook_SendMessageW},
	};
	struct installed_hook_info hi;

	/* not hooked */
	assert(testcalled == 0);
	//~ SendMessageW(NULL, 0, 0, 0); /* #2 */
	assert(testcalled == 0);

	fastprintf(">> patch_dll_funcs\n");
	if (!patch_dll_funcs(GetModuleHandle(NULL), hooks, CNT, (void *)0xdeadbeef, &hi))
		assert(0);
	fastprintf("<< patch_dll_funcs\n");

	//~ fastprintf(">> unpatch\n");
	//~ if (!unpatch(GetModuleHandle(NULL), hooks, CNT, &hi))
		//~ assert(0);
	//~ fastprintf("<< unpatch\n");

	assert(hooks[0].orig);

	/* hooked, first call */
	assert(testcalled == 0);
	SendMessageW(NULL, 0, 0, 0);
	//~ assert(testcalled == 0);
	assert(testcalled == 1); /* #1 */

}
#endif
