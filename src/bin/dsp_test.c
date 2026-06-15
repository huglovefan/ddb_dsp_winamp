#include <assert.h>
#include <stdio.h>
#include <windows.h>
#include "../host/winamp.hpp"

#define USE_DSP_HDR_HWND 1

#if USE_DSP_HDR_HWND
# define DSP_HDRVER 0x22
#else
# define DSP_HDRVER 0x20
#endif

/**********************************************************************/

BOOL WINAPI DllMain(
    HANDLE instance,
    ULONG  reason,
    LPVOID reserved);

#if USE_DSP_HDR_HWND
struct winampDSPHeader *winampDSPGetHeader2(HWND hwndParent);
#else
struct winampDSPHeader *winampDSPGetHeader2(void);
#endif

/**********************************************************************/

static struct winampDSPModule *dogetmodule(int);
static int dosf(int);

static void doconfig(struct winampDSPModule *);
static int doinit(struct winampDSPModule *);
static int domodsamples(struct winampDSPModule *, short *, int, int, int, int);
static int domodsamples_sleep(struct winampDSPModule *, short *, int, int, int, int);
static void doquit(struct winampDSPModule *);

/**********************************************************************/

static struct winampDSPHeader header = {
	.version     = DSP_HDRVER,
	.description = "Test DSP",
	.getModule   = &dogetmodule,
	.sf          = &dosf,
};

static struct winampDSPModule mod0 = {
	.description   = "Do nothing",
	.hwndParent    = NULL,
	.hDllInstance  = NULL,
	.Config        = &doconfig,
	.Init          = &doinit,
	.ModifySamples = &domodsamples,
	.Quit          = &doquit,
	.userData      = NULL,
};

static struct winampDSPModule mod1 = {
	.description   = "Sleep 10ms",
	.hwndParent    = NULL,
	.hDllInstance  = NULL,
	.Config        = &doconfig,
	.Init          = &doinit,
	.ModifySamples = &domodsamples_sleep,
	.Quit          = &doquit,
	.userData      = NULL,
};

struct userdata
{
	unsigned int config_calls;
};

/**********************************************************************/

BOOL WINAPI DllMain(
	HANDLE instance,
	ULONG  reason,
	LPVOID reserved)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		break;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		break;
	}

	return TRUE;
}

#if USE_DSP_HDR_HWND
struct winampDSPHeader *winampDSPGetHeader2(HWND hwndParent)
#else
struct winampDSPHeader *winampDSPGetHeader2(void)
#endif
{
	return &header;
}

static struct winampDSPModule *dogetmodule(int idx)
{
	switch (idx)
	{
	case 0:  return &mod0;
	case 1:  return &mod1;
	default: return NULL;
	}
}

static int dosf(int key)
{
	return 0;
}

static void doconfig(struct winampDSPModule *mod)
{
	char buf[128];
	struct userdata *ud;
	LRESULT res;

	ud = (struct userdata *)mod->userData;
	assert(ud);

	ud->config_calls++;

	/* mind the bug: the host currently returns track titles as
	   utf-8, so non-ascii characters will appear garbled here.
	   the real winamp doesn't have this problem. */

	res = SendMessage(mod->hwndParent, WM_WA_IPC, 0, IPC_GETLISTPOS);

	res = SendMessage(mod->hwndParent, WM_WA_IPC, res, IPC_GETPLAYLISTTITLE);

	snprintf(buf, sizeof(buf),
	    "hi from dsp_test\n\n"
	    "current track:\n%s\n\n"
	    "checked this %u time(s) so far",
	    (char *)res,
	    ud->config_calls);

	MessageBoxA(mod->hwndParent, buf, "hi", 0);
}

static int doinit(struct winampDSPModule *mod)
{
	struct userdata *ud;

	if (mod->userData)
	{
		/* init twice, should never happen. */
		assert(0);
		return 1;
	}

	ud = (struct userdata *)calloc(1, sizeof(struct userdata));
	if (!ud)
		return 1;

	mod->userData = ud;

	return 0;
}

static int domodsamples(
	struct winampDSPModule *mod,
	short                  *samples,
	int                     nsamples,
	int                     bits,
	int                     ch,
	int                     rate)
{
	return nsamples;
}

static int domodsamples_sleep(
	struct winampDSPModule *mod,
	short                  *samples,
	int                     nsamples,
	int                     bits,
	int                     ch,
	int                     rate)
{
	/* note that this waits 10msec *per call to ModifySamples* - if
	   the function is called several times (like the host might do
	   with a low `process_max_frames`), then the overall wait will
	   be longer. */

	Sleep(/* msec */ 10);

	return nsamples;
}

static void doquit(struct winampDSPModule *mod)
{
	struct userdata *ud;

	ud = (struct userdata *)mod->userData;
	if (!ud)
	{
		/* quit twice, should never happen. */
		assert(0);
		return;
	}

	free(ud);
	mod->userData = NULL;
}
