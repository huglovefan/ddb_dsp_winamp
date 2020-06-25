#include <alloca.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <deadbeef/deadbeef.h>

#include "../common.h"

// need a "read_full_with_timeout" function
// it still freezes sometimes

#undef stdin
#undef stdout

static DB_functions_t *g_deadbeef;
static DB_dsp_t        g_plugin;

static bool have_patch1 = false;

typedef struct {
	ddb_dsp_context_t ctx;
	char *dll;
	unsigned short max_bps;
	short errors;
	struct {
		pid_t pid;
		int stdin;
		int stdout;
	} child;
} plugin_t;

#define SENSIBLE_WAVEFMT(x) ( \
	(x).bps >= 8 && \
	(x).bps <= 32 && \
	(x).bps % 8 == 0 && \
	(x).channels > 0 && \
	(x).samplerate > 0)

static bool
__attribute__((hot))
next_dsp_needs_f32(plugin_t *plugin, const ddb_waveformat_t *infmt)
{
	ddb_waveformat_t tmpfmt = *infmt;
	ddb_dsp_context_t *ctx = plugin->ctx.next;

	tmpfmt.bps = 32;
	tmpfmt.is_float = 1;

	/* get the next non-bypassed dsp */
	while (UNPREDICTABLE(ctx != NULL)) {
		if (LIKELY(ctx->plugin->plugin.api_vminor >= 1) &&
		    ctx->plugin->can_bypass != NULL &&
		    ctx->plugin->can_bypass(ctx, &tmpfmt)) {
			ctx = ctx->next;
			continue;
		}
		break;
	}

	return UNPREDICTABLE(ctx != NULL && ctx->plugin != &g_plugin);
}

static void
__attribute__((cold))
close_extra(void)
{
	struct dirent **namelist;
	int count;

	count = scandir("/proc/self/fd", &namelist, NULL, NULL);
	if (UNLIKELY(count < 0))
		return;

	for (int i = 0; i < count; i++) {
		int fd = atoi(namelist[i]->d_name);
		if (LIKELY(fd > 2))
			close(fd);
		free(namelist[i]);
	}

	free(namelist);
}

static bool
__attribute__((cold))
__attribute__((noinline))
child_start(plugin_t *plugin)
{
	char *host = NULL;
	int stdin[2] = {-1, -1},
	    stdout[2] = {-1, -1}; /* {read_end, write_end} */
	pid_t pid = -1;

	if (UNLIKELY(pipe(stdin) < 0 || pipe(stdout) < 0)) {
		perror("pipe");
		goto failed;
	}

	g_deadbeef->conf_lock();
	host = strdup(g_deadbeef->conf_get_str_fast("ddw.host_cmd", "ddw_host.exe"));
	g_deadbeef->conf_unlock();
	assert(host != NULL);

	pid = fork();
	if (UNLIKELY(pid < 0)) {
		perror("fork");
failed:
		close(stdin[0]);
		close(stdin[1]);
		close(stdout[0]);
		close(stdout[1]);
		free(host);
		return false;
	} else if (UNLIKELY(pid == 0)) {
		char *cmd;
		size_t bufsz;
		if (UNLIKELY(dup2(stdin[0], STDIN_FILENO) < 0 ||
		             dup2(stdout[1], STDOUT_FILENO) < 0)) {
			perror("dup2");
			_exit(EXIT_FAILURE);
		}
		close_extra();
		bufsz = strlen("exec ") + strlen(host) + strlen(" ") + strlen(plugin->dll) + sizeof('\0');
		cmd = alloca(bufsz);
		snprintf(cmd, bufsz, "exec %s %s", host, plugin->dll);
		execl("/bin/sh", "sh", "-c", cmd, NULL);
		perror("execl");
		_exit(EXIT_FAILURE);
	} else {
		close(stdin[0]);
		close(stdout[1]);
		plugin->child.pid = pid;
		plugin->child.stdin = stdin[1];
		plugin->child.stdout = stdout[0];
		free(host);
		return true;
	}
}

static void
__attribute__((cold))
child_stop(plugin_t *plugin)
{
	if (LIKELY(plugin->child.pid >= 0)) {
		close(plugin->child.stdin);
		close(plugin->child.stdout);
		waitpid(plugin->child.pid, NULL, 0);
		plugin->child.stdin = -1;
		plugin->child.stdout = -1;
		plugin->child.pid = -1;
	}
}

static ddb_dsp_context_t *
__attribute__((cold))
dsp_winamp_open(void)
{
	plugin_t *plugin;
	char *dll;

	plugin = malloc(sizeof(plugin_t));
	dll = strdup("");
	if (UNLIKELY(plugin == NULL || dll == NULL))
		goto failed;

	DDB_INIT_DSP_CONTEXT(plugin, plugin_t, &g_plugin)

	plugin->dll = dll;
	plugin->max_bps = 0;
	plugin->errors = 0;

	plugin->child.pid = -1;
	plugin->child.stdin = -1;
	plugin->child.stdout = -1;

	return (ddb_dsp_context_t *)plugin;
failed:
	free(plugin);
	free(dll);
	return NULL;
}

static void
__attribute__((cold))
dsp_winamp_close(ddb_dsp_context_t *ctx)
{
	plugin_t *plugin = (plugin_t *)ctx;

	child_stop(plugin);

	free(plugin->dll);
	free(plugin);
}

static bool
__attribute__((cold))
process_child_is_running(plugin_t *plugin)
{
	return LIKELY(plugin->child.pid > 0 &&
	              waitpid(plugin->child.pid, NULL, WNOHANG) == 0);
}

static bool
__attribute__((always_inline))
__attribute__((hot))
process_start_child_if_not_started(plugin_t *plugin)
{
	if (UNLIKELY(plugin->child.pid < 0))
		return LIKELY(child_start(plugin) && process_child_is_running(plugin));

	return LIKELY(plugin->child.pid >= 0);
}

static bool
__attribute__((always_inline))
__attribute__((hot))
process_write_request(plugin_t *plugin,
                      float *samples,
                      int frames,
                      ddb_waveformat_t *fmt)
{
	struct processing_request request;
	char *writebuf;

	assert(plugin != NULL);
	assert(samples != NULL);
	assert(frames > 0);
	assert(fmt != NULL);

	if (LIKELY(fmt->is_float || (plugin->max_bps != 0 && fmt->bps > plugin->max_bps))) {
		ddb_waveformat_t convfmt = *fmt;
		if (LIKELY(plugin->max_bps != 0 && fmt->bps > plugin->max_bps))
			convfmt.bps = plugin->max_bps;
		convfmt.is_float = 0;
		writebuf = alloca(frames*(convfmt.bps/8)*convfmt.channels);
		g_deadbeef->pcm_convert(
		    fmt, (char *)samples,
		    &convfmt, writebuf,
		    frames*(fmt->bps/8)*fmt->channels);
		fmt->bps = convfmt.bps;
		fmt->is_float = convfmt.is_float;
	} else
		writebuf = (char *)samples;

	request.buffer_size = frames*(fmt->bps/8)*fmt->channels;
	request.samplerate = fmt->samplerate;
	request.bitspersample = fmt->bps;
	request.channels = fmt->channels;

	if (UNLIKELY(!write_full(plugin->child.stdin, &request, sizeof(request))))
		return false;

	return LIKELY(write_full(plugin->child.stdin, writebuf, request.buffer_size));
}

static bool
__attribute__((always_inline))
__attribute__((hot))
process_read_response(plugin_t *plugin,
                      float *samples,
                      int *frames,
                      int maxframes,
                      ddb_waveformat_t *fmt)
{
	struct processing_response response;
	bool need_f32;

	assert(plugin != NULL);
	assert(samples != NULL);
	assert(frames != NULL);
	assert(maxframes > 0);
	assert(fmt != NULL);

	if (UNLIKELY(!read_full(plugin->child.stdout, &response, sizeof(response))))
		return false;

	assert(response.buffer_size % ((fmt->bps/8)*fmt->channels) == 0);

	*frames = response.buffer_size/(fmt->bps/8)/fmt->channels;
	if (UNLIKELY(*frames == 0))
		return true;
	if (UNLIKELY(*frames > maxframes))
		return false;

	need_f32 = next_dsp_needs_f32(plugin, fmt);
	if (LIKELY(((need_f32 && !(fmt->bps == 32 && fmt->is_float)) ||
	           (fmt->bps != 32 && !have_patch1)))) {
		char readbuf[response.buffer_size];
		ddb_waveformat_t convfmt;
		if (UNLIKELY(!read_full(plugin->child.stdout, readbuf, response.buffer_size)))
			return false;
		convfmt = *fmt;
		convfmt.bps = 32;
		convfmt.is_float = need_f32;
		g_deadbeef->pcm_convert(
		    fmt, readbuf,
		    &convfmt, (char *)samples,
		    response.buffer_size);
		fmt->bps = convfmt.bps;
		fmt->is_float = convfmt.is_float;
		return true;
	} else {
		return LIKELY(read_full(plugin->child.stdout, (char *)samples, response.buffer_size));
	}
}

static int
__attribute__((hot))
dsp_winamp_process(ddb_dsp_context_t *ctx,
                   float *samples,
                   int frames,
                   int maxframes,
                   ddb_waveformat_t *fmt,
                   float *ratio)
{
	plugin_t *plugin;
	int frames_in = frames;

//	strdup((void *)((long long unsigned int)samples*(long long unsigned int)samples));

	assert(ctx != NULL);
	assert(samples != NULL);
	assert(frames > 0);
	assert(maxframes >= frames);
	assert(fmt != NULL);
	assert(SENSIBLE_WAVEFMT(*fmt));
	assert(ratio != NULL);

	plugin = (plugin_t *)ctx;

	if (UNLIKELY(plugin->dll[0] == '\0' ||
	             (plugin->dll[0] == '0' && plugin->dll[1] == '\0'))) {
		/* no dll, convert samples back to float for other dsps */
		/* the settings interface sucks and puts a number here when you enter an empty string */
		if (LIKELY((fmt->bps != 32 && !have_patch1)) ||
		           (!(fmt->bps == 32 && fmt->is_float) && next_dsp_needs_f32(plugin, fmt))) {
			char convbuf[frames*sizeof(float)*fmt->channels];
			ddb_waveformat_t convfmt = *fmt;
			convfmt.bps = 32;
			convfmt.is_float = 1;
			assert((convfmt.bps/fmt->bps)*frames <= maxframes);
			g_deadbeef->pcm_convert(
			    fmt, (char *)samples,
			    &convfmt, convbuf,
			    frames*(fmt->bps/8)*fmt->channels);
			memcpy(samples, convbuf, frames*sizeof(float)*fmt->channels);
			fmt->bps = 32;
			fmt->is_float = 1;
		}
		return frames;
	}

	if (UNLIKELY(!process_start_child_if_not_started(plugin)))
		goto failed;
	if (UNLIKELY(!process_write_request(plugin, samples, frames, fmt)))
		goto failed;
	if (UNLIKELY(!process_read_response(plugin, samples, &frames, maxframes, fmt)))
		goto failed;

	assert(frames >= 0);

	if (LIKELY(frames > 0)) {
		*ratio = ((float)frames_in)/((float)frames);
		plugin->errors = 0;
	} else
		*ratio = 0;

	return frames;
failed:
	plugin->errors += 1;
	if (UNLIKELY(plugin->errors >= 3)) {
		/* this only piles up if it's repeatedly restarted without any output
		   (so the error message is right) */
		g_deadbeef->sendmessage(DB_EV_PAUSE, 0, 0, 0);
		g_deadbeef->log("dsp_winamp: failed to start the plugin! check .xsession-errors or run deadbeef from a terminal to see the error message.\n");
		plugin->errors = 0;
	}
	child_stop(plugin);
	return 0;
}

static void
__attribute__((cold))
dsp_winamp_reset(ddb_dsp_context_t *ctx)
{
	assert(ctx != NULL);

	have_patch1 = g_deadbeef->conf_get_int("ddw.patch1", 0);
}

#define NUM_PARAMS 2

static int
__attribute__((cold))
dsp_winamp_num_params(void)
{
	return NUM_PARAMS;
}

static const char *
__attribute__((cold))
dsp_winamp_get_param_name(int p)
{
	assert(p >= 0 && p < NUM_PARAMS);

	switch (p) {
	case 0:
		return "Path to plugin";
	case 1:
		return "Max. bit depth";
	default:
		return NULL;
	}
}

static void
__attribute__((hot))
dsp_winamp_set_param(ddb_dsp_context_t *ctx, int p, const char *val)
{
	plugin_t *plugin;

	assert(ctx != NULL);
	assert(p >= 0 && p < NUM_PARAMS);
	assert(val != NULL);

	plugin = (plugin_t *)ctx;

	switch (p) {
	case 0: {
		char *dll2;
		if (UNLIKELY(strcmp(val, plugin->dll) == 0))
			break;
		dll2 = strdup(val);
		if (UNLIKELY(dll2 == NULL))
			break;
		child_stop(plugin);
		free(plugin->dll);
		plugin->dll = dll2;
		plugin->errors = 0;
		break;
	}
	case 1:
		plugin->max_bps = atoi(val);
		if (UNLIKELY(plugin->max_bps % 8 != 0 || plugin->max_bps > 32)) {
			g_deadbeef->log("dsp_winamp: invalid bit depth entered\n");
			plugin->max_bps = 16;
		}
		plugin->errors = 0;
		break;
	}
}

static void
__attribute__((cold))
dsp_winamp_get_param(ddb_dsp_context_t *ctx, int p, char *str, int len)
{
	plugin_t *plugin;

	assert(ctx != NULL);
	assert(p >= 0 && p < NUM_PARAMS);
	assert(str != NULL);
	assert(len > 0);

	plugin = (plugin_t *)ctx;

	switch (p) {
	case 0:
		snprintf(str, len, "%s", plugin->dll);
		break;
	case 1:
		snprintf(str, len, "%u", plugin->max_bps);
		break;
	default:
		str[0] = '\0';
		break;
	}
}

static int
__attribute__((cold))
dsp_winamp_can_bypass(ddb_dsp_context_t *ctx, ddb_waveformat_t *fmt)
{
	plugin_t *plugin;

	assert(ctx != NULL);
	assert(fmt != NULL);
	assert(SENSIBLE_WAVEFMT(*fmt));

	plugin = (plugin_t *)ctx;

	if (UNLIKELY(plugin->dll[0] == '\0' ||
	             (plugin->dll[0] == '0' && plugin->dll[1] == '\0'))) {
		if (LIKELY((fmt->bps != 32 && !have_patch1) ||
		           (!(fmt->bps == 32 && fmt->is_float) && next_dsp_needs_f32(plugin, fmt)))) {
			return false;
		}
		return true;
	}

	return false;
}

static DB_dsp_t g_plugin = {
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
		"property \"Max. bit depth\" entry 1 \"0\";\n",
	.plugin.configdialog =
		"property \"Host command\" entry ddw.host_cmd \"ddw_host.exe\";\n"
		"property \"DSP plugin can return non-32bit samples\" checkbox ddw.patch1 0;\n",
	.can_bypass = dsp_winamp_can_bypass,
};

DB_plugin_t *
dsp_winamp_load(DB_functions_t *api)
{
	assert(api != NULL);
	g_deadbeef = api;
	return &g_plugin.plugin;
}
