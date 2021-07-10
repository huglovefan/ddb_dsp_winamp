#include "plugin.h"

#include <stdlib.h>
#include <string.h>

#include "ddw.h"
#include "misc.h"

DB_functions_t *deadbeef;

static DB_dsp_t plugindef;

static bool have_patch1 = false;

//
// dspconfig cannot store empty strings without messing up the rest of the
//  options. need to use a replacement string to represent them
//
#define DSPCONFIG_EMPTY_STRING "-"

// -----------------------------------------------------------------------------

static int
ddw_next_needs_conversion(struct ddw *plugin, const ddb_waveformat_t *curfmt)
{
	ddb_dsp_context_t *nextctx = plugin->ctx.next;
	int rv = 0;

	if (nextctx != NULL) {
		if (nextctx->plugin != &plugindef)
			rv |= NEED_32BIT|NEED_FLOAT;
	} else {
		if (!have_patch1)
			rv |= NEED_32BIT;
	}

	if (curfmt != NULL) {
		if (rv&NEED_32BIT && curfmt->bps == 32)
			rv &= ~NEED_32BIT;
		if (rv&NEED_FLOAT && curfmt->is_float)
			rv &= ~NEED_FLOAT;
	}

	return rv;
}

static bool
isbitdepth(const char *s)
{
	int l;

	if (s[l=0] != '\0' &&
	    s[l=1] != '\0' &&
	    s[l=2] != '\0') {
		return false;
	}

	switch (l) {
	case 0:
		return false;
	case 1:
		return memcmp(s, "0", 1) == 0 ||
		    memcmp(s, "8", 1) == 0;
	case 2:
		return memcmp(s, "16", 2) == 0 ||
		    memcmp(s, "24", 2) == 0 ||
		    memcmp(s, "32", 2) == 0;
	default:
		__builtin_unreachable();
	}
}

bool
ddw_has_dll(struct ddw *plugin)
{
	if (plugin->dll[0] == '\0')
		return false;

	if (strcmp(plugin->dll, DSPCONFIG_EMPTY_STRING) == 0)
		return false;

	if (isbitdepth(plugin->dll))
		return false;

	return true;
}

// -----------------------------------------------------------------------------

static ddb_dsp_context_t *
dsp_winamp_open(void)
{
	struct ddw *plugin;
	char *dll;

	plugin = malloc(sizeof(struct ddw));
	dll = strdup("");
	if (plugin == NULL || dll == NULL)
		goto failed;

	DDB_INIT_DSP_CONTEXT(plugin, struct ddw, &plugindef)

	plugin->dll = dll;
	plugin->max_bps = 16;

	plugin->host = CHILD_INITIALIZER(plugin);

	return (ddb_dsp_context_t *)plugin;
failed:
	free(plugin);
	free(dll);
	return NULL;
}

static void
dsp_winamp_close(ddb_dsp_context_t *ctx)
{
	struct ddw *plugin = (struct ddw *)ctx;

	child_stop(&plugin->host);

	free(plugin->dll);
	free(plugin);
}

static int
dsp_winamp_process(ddb_dsp_context_t *ctx,
                   float *samples,
                   int frames,
                   int maxframes,
                   ddb_waveformat_t *fmt,
                   float *ratio)
{
	struct ddw *plugin = (struct ddw *)ctx;
	int frames_in = frames;

	// note: the maxframes value assumes 32-bit samples even if fmt says
	//  something else
	// this should be correct as long another dsp hasn't changed the number
	//  of channels
	size_t outcap = maxframes*(32/8)*fmt->channels;

	ddb_waveformat_t nextfmt = *fmt;

	int convinfo = ddw_next_needs_conversion(plugin, fmt);
	if (convinfo&NEED_32BIT)
		nextfmt.bps = 32;
	if (convinfo&NEED_FLOAT)
		nextfmt.is_float = 1;

	frames = child_process_samples(&plugin->host,
	    fmt, &nextfmt,
	    (char *)samples, frames,
	    outcap);

	if (frames == -1) {
		frames = 0;
		if (child_is_doomed(&plugin->host)) {
			child_stop(&plugin->host);
			deadbeef->log("dsp_winamp: plugin failed to start! check ~/.xsession-errors for errors or try running deadbeef from a terminal\n");
			deadbeef->get_output()->pause();
			child_reset_failures(&plugin->host);
		}
	}

	if (frames > 0) {
		*ratio = ((float)frames_in)/((float)frames);
	} else {
		*ratio = 0.0f;
		frames = 0;
	}

	return frames;
}

//
// called when playback is stopped or un-stopped
//
static void
dsp_winamp_reset(ddb_dsp_context_t *ctx)
{
	have_patch1 = deadbeef->conf_get_int("ddw.patch1", 0);
}

#define NUM_PARAMS 2

static int
dsp_winamp_num_params(void)
{
	return NUM_PARAMS;
}

static const char *
dsp_winamp_get_param_name(int p)
{
	switch (p) {
	case 0:
		return "Path to plugin";
	case 1:
		return "Max. bit depth";
	default:
		return "?";
	}
}

static void
dsp_winamp_set_param(ddb_dsp_context_t *ctx, int p, const char *val)
{
	struct ddw *plugin = (struct ddw *)ctx;
	char *newdll;

	if (strcmp(val, DSPCONFIG_EMPTY_STRING) == 0)
		val = "";

	//
	// warn about too-long options
	// when parsing options from dspconfig, they're read to a 100-byte
	//  buffer so longer ones will get truncated (and also mess up later
	//  options)
	//
	if (strlen(val) > 99)
		deadbeef->log("dsp_winamp: warning: dsp options are limited to 99 characters\n");

	switch (p) {
	case 0:
		if (strcmp(val, plugin->dll) == 0)
			break;

		newdll = strdup(val);
		if (newdll == NULL)
			break;

		child_stop(&plugin->host);
		child_reset_failures(&plugin->host);

		free(plugin->dll);
		plugin->dll = newdll;

		break;
	case 1:
		plugin->max_bps = atoi(val);
		if (!isbitdepth(val)) {
			deadbeef->log("dsp_winamp: invalid bit depth entered\n");
			plugin->max_bps = 16;
		}
		child_reset_failures(&plugin->host);
		break;
	default:
		// probably dspconfig is malformed or a previous option
		//  contained an invalid character. the parsing isn't very
		//  robust
		fprintf(stderr, "dsp_winamp: tried to set nonexistent option index %d to \"%s\"\n", p, val);
		break;
	}
}

static void
dsp_winamp_get_param(ddb_dsp_context_t *ctx, int p, char *str, int len)
{
	struct ddw *plugin = (struct ddw *)ctx;

	switch (p) {
	case 0:
		snprintf(str, len, "%s", plugin->dll);
		break;
	case 1:
		snprintf(str, len, "%u", plugin->max_bps);
		break;
	default:
		fprintf(stderr, "dsp_winamp: tried to get nonexistent option index %d\n", p);
		str[0] = '\0';
		break;
	}

	if (*str == '\0')
		snprintf(str, len, "%s", DSPCONFIG_EMPTY_STRING);
}

//
// returns true if there's nothing for dsp_winamp_process() to do
//
static int
dsp_winamp_can_bypass(ddb_dsp_context_t *ctx, ddb_waveformat_t *fmt)
{
	struct ddw *plugin = (struct ddw *)ctx;
	int convinfo;

	// have some processing to do?
	if (ddw_has_dll(plugin))
		return false;

	// need to convert for the next dsp?
	convinfo = ddw_next_needs_conversion(plugin, fmt);
	if (convinfo != 0)
		return false;

	return true;
}

static DB_dsp_t plugindef = {
	.plugin.type = DB_PLUGIN_DSP,
	DDB_REQUIRE_API_VERSION(1, DDB_API_LEVEL)
	.plugin.id = "dsp_winamp",
	.plugin.name = "winamp dsp",
	.plugin.descr = "",
	.plugin.website = "https://github.com/huglovefan/ddb_dsp_winamp",
	.open = dsp_winamp_open,
	.close = dsp_winamp_close,
	.process = dsp_winamp_process,
	.reset = dsp_winamp_reset,
	.num_params = dsp_winamp_num_params,
	.get_param_name = dsp_winamp_get_param_name,
	.set_param = dsp_winamp_set_param,
	.get_param = dsp_winamp_get_param,
	.configdialog =
		"property \"Path to plugin\" entry 0 \"\";\n"
		"property \"Max. bit depth\" entry 1 \"\";\n",
	.plugin.configdialog =
		"property \"Host command\" entry ddw.host_cmd \"ddw_host.exe\";\n"
		"property \"DSP plugin can return non-32bit samples\" checkbox ddw.patch1 0;\n",
	.can_bypass = dsp_winamp_can_bypass,
};

DB_plugin_t *
dsp_winamp_load(DB_functions_t *ddb);

DB_plugin_t *
dsp_winamp_load(DB_functions_t *ddb)
{
	deadbeef = ddb;
	return &plugindef.plugin;
}
