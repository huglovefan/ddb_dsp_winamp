#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <deadbeef/deadbeef.h>

#include "../common.h"

static DB_functions_t *g_deadbeef;
static DB_dsp_t        g_plugin;

typedef struct {
	ddb_dsp_context_t ctx;
	char *dll; /* actually a shell pattern */
	bool dll_exists;
	unsigned short max_bps;
	struct {
		pid_t pid;
		int stdin;
		int stdout;
	} child;
} plugin_t;

/* checks if the next dsp in the chain probably expects samples in the default format */
static bool
next_dsp_needs_f32(plugin_t *plugin, ddb_waveformat_t *infmt)
{
	ddb_waveformat_t fmt = *infmt;
	ddb_dsp_context_t *ctx = plugin->ctx.next;

	fmt.bps = 32;
	fmt.is_float = 1;
	while (ctx != NULL) {
		if (ctx->plugin->can_bypass != NULL &&
		    ctx->plugin->can_bypass(ctx, &fmt)) {
			ctx = ctx->next;
			continue;
		}
		if (ctx->plugin == &g_plugin)
			return false;
		break;
	}
	return (ctx != NULL);
}

static void
child_stop(plugin_t *plugin)
{
	close(plugin->child.stdout);
	plugin->child.stdout = -1;
	close(plugin->child.stdin);
	plugin->child.stdin = -1;

	if (plugin->child.pid >= 0) {
		if (waitpid(plugin->child.pid, NULL, 0) < 0)
			perror("dsp_winamp: waitpid");
		plugin->child.pid = -1;
	}
}

static rlim_t
get_max_fd(void)
{
	struct rlimit nofile;

	if (getrlimit(RLIMIT_NOFILE, &nofile) >= 0) {
		return nofile.rlim_cur;
	} else {
		perror("dsp_winamp: getrlimit");
		return 1024;
	}
}

static void
child_start(plugin_t *plugin)
{
	int stdin[2] = {-1, -1},
	    stdout[2] = {-1, -1}; /* {read_end, write_end} */
	rlim_t max_fd = get_max_fd();
	pid_t pid = -1;

	if ((pipe(stdin) < 0) ||
	    (pipe(stdout) < 0)) {
		perror("dsp_winamp: pipe");
		goto failed;
	}

	g_deadbeef->conf_lock();
	const char *host = g_deadbeef->conf_get_str_fast("ddw.host_cmd", "ddw_host.exe");
	char *cmd = alloca(4 + 1 + strlen(host) + 1 + strlen(plugin->dll) + 1);
	sprintf(cmd, "exec %s %s", host, plugin->dll);
	g_deadbeef->conf_unlock();

	pid = fork();
	if (pid < 0) {
		perror("dsp_winamp: fork");
failed:
		close(stdin[0]);
		close(stdin[1]);
		close(stdout[0]);
		close(stdout[1]);
	} else if (pid == 0) {
		if (dup2(stdin[0], STDIN_FILENO) < 0 ||
		    dup2(stdout[1], STDOUT_FILENO) < 0) {
			perror("dsp_winamp: dup2");
			_exit(EXIT_FAILURE);
		}
		for (unsigned i = 3; i < max_fd; i++)
			close(i);
		execl("/bin/sh", "sh", "-c", cmd, NULL);
		perror("dsp_winamp: exec");
		_exit(EXIT_FAILURE);
	} else {
		close(stdin[0]);
		close(stdout[1]);
		plugin->child.pid = pid;
		plugin->child.stdin = stdin[1];
		plugin->child.stdout = stdout[0];
	}
}

static ddb_dsp_context_t *
dsp_winamp_open(void)
{
	plugin_t *plugin;

	plugin = malloc(sizeof(plugin_t));
	DDB_INIT_DSP_CONTEXT(plugin, plugin_t, &g_plugin);

	plugin->dll = strdup("");
	plugin->dll_exists = false;
	plugin->max_bps = 0;

	plugin->child.pid = -1;
	plugin->child.stdin = -1;
	plugin->child.stdout = -1;

	return (ddb_dsp_context_t *)plugin;
}

static void
dsp_winamp_close(ddb_dsp_context_t *ctx)
{
	plugin_t *plugin = (plugin_t *)ctx;

	child_stop(plugin);

	free(plugin->dll);
	free(plugin);
}

static bool
process_check_child(plugin_t *plugin)
{
	if (plugin->child.pid < 0) {
		child_start(plugin);
	} else if (waitpid(plugin->child.pid, NULL, WNOHANG) > 0) {
		plugin->child.pid = -1;
		child_stop(plugin);
		child_start(plugin);
	}
	return (plugin->child.pid >= 0);
}

static bool
process_write_request(plugin_t *ctx,
                      float *samples,
                      int frames,
                      ddb_waveformat_t *fmt)
{
	plugin_t *plugin = (plugin_t *)ctx;
	struct processing_request request;
	char *writebuf;

	/* convert if needed */
	if (fmt->is_float || (plugin->max_bps != 0 && fmt->bps > plugin->max_bps)) {
		ddb_waveformat_t convfmt = *fmt;
		int rv;
		if (plugin->max_bps != 0 && fmt->bps > plugin->max_bps)
			convfmt.bps = plugin->max_bps;
		convfmt.is_float = 0;
		writebuf = alloca(frames*(convfmt.bps/8)*convfmt.channels);
		rv = g_deadbeef->pcm_convert(
		    fmt, (char *)samples,
		    &convfmt, writebuf,
		    frames*(fmt->bps/8)*fmt->channels);
		assert(rv == frames*(convfmt.bps/8)*convfmt.channels);
		fmt->bps = convfmt.bps;
		fmt->is_float = convfmt.is_float;
	} else {
		writebuf = (char *)samples;
	}

	request.buffer_size = frames*(fmt->bps/8)*fmt->channels;
	request.samplerate = fmt->samplerate;
	request.bitspersample = fmt->bps;
	request.channels = fmt->channels;

	if ((size_t)write(plugin->child.stdin, &request, sizeof(request)) != sizeof(request))
		return false;
	if ((size_t)write(plugin->child.stdin, writebuf, request.buffer_size) != request.buffer_size)
		return false;

	return true;
}

static bool
process_read_response(plugin_t *plugin,
                      float *samples,
                      int *frames,
                      int maxframes,
                      ddb_waveformat_t *fmt)
{
	struct processing_response response;
	bool need_f32;

	if ((size_t)read(plugin->child.stdout, &response, sizeof(response)) != sizeof(response))
		return false;

	*frames = response.buffer_size/(fmt->bps/8)/fmt->channels;

	if (response.buffer_size == 0)
		return true;
	if (*frames > maxframes)
		return false;
	if (response.buffer_size % ((fmt->bps/8)*fmt->channels) != 0)
		return false;

	assert(!fmt->is_float); /* we cleared this earlier */
	need_f32 = next_dsp_needs_f32(plugin, fmt);
	if ((need_f32 && !(fmt->bps == 32 && fmt->is_float)) ||
	    (!g_deadbeef->conf_get_int("ddw.patch1", 0) && fmt->bps != 32)) {
		char *readbuf = alloca(response.buffer_size);
		ddb_waveformat_t convfmt = *fmt;
		int rv;
		convfmt.bps = 32;
		/* only convert to float if we have to */
		if (need_f32)
			convfmt.is_float = 1;
		if ((size_t)read(plugin->child.stdout, readbuf, response.buffer_size) != response.buffer_size)
			return false;
		rv = g_deadbeef->pcm_convert(
		    fmt, readbuf,
		    &convfmt, (char *)samples,
		    response.buffer_size);
		assert(rv == (*frames)*(convfmt.bps/8)*convfmt.channels);
		fmt->bps = convfmt.bps;
		fmt->is_float = convfmt.is_float;
	} else {
		/* don't need to convert */
		if ((size_t)read(plugin->child.stdout, (char *)samples, response.buffer_size) != response.buffer_size)
			return false;
	}
	return true;
}

static int
dsp_winamp_process(ddb_dsp_context_t *ctx,
                   float *samples,
                   int frames,
                   int maxframes,
                   ddb_waveformat_t *fmt,
                   float *ratio)
{
	plugin_t *plugin = (plugin_t *)ctx;
	int frames_in = frames;
	(void)ratio;

	if (ctx->plugin->can_bypass(ctx, fmt))
		return frames;
	/* start child if not running */
	if (!process_check_child(plugin))
		return 0;

	if (!process_write_request(plugin, samples, frames, fmt))
		goto failed;
	if (!process_read_response(plugin, samples, &frames, maxframes, fmt))
		goto failed;

	if (frames > 0) {
#ifndef NDEBUG
		if (!g_deadbeef->conf_get_int("ddw.patch1", 0))
			assert(fmt->bps == 32);
		if (next_dsp_needs_f32(plugin, fmt))
			assert(fmt->bps == 32 && fmt->is_float);
#endif
		*ratio = ((float)frames_in)/((float)frames);
	} else
		*ratio = 0;
	return frames;
failed:
	/* something went wrong, child is probably in an inconsistent state now */
	fprintf(stderr, "dsp_winamp: something happened!\n");
	child_stop(plugin);
	return 0;
}

static int
dsp_winamp_num_params(void)
{
	return 2;
}

static const char *
dsp_winamp_get_param_name(int p)
{
	switch (p) {
	case 0:
		return "Path to plugin";
	case 1:
		return "Max. bit depth";
	}
	return NULL;
}

static const char code[] =
	"f() {\n"
	"    [ $# -gt 0 ] || exit 1\n"
	"    for f; do\n"
	"        f=${f%:[0-9]}\n"
	"        [ -f \"$f\" -a -r \"$f\" ] && continue\n"
	"        >&2 echo \"dsp_winamp: dll not found: $f\"\n"
	"        exit 1\n"
	"    done\n"
	"    exit 0\n"
	"}\n"
	"f";

static bool
dll_exists(const char *dll)
{
	char cmd[sizeof(code) + 1 + strlen(dll) + 1];
	pid_t pid;

	sprintf(cmd, "%s %s", code, dll);
	pid = fork();
	if (pid < 0) {
		perror("dsp_winamp: fork");
		return true;
	} else if (pid == 0) {
		execl("/bin/sh", "sh", "-c", cmd, NULL);
		perror("dsp_winamp: exec");
		_exit(EXIT_FAILURE);
	} else {
		int status;
		waitpid(pid, &status, 0);
		return WIFEXITED(status) && WEXITSTATUS(status) == 0;
	}
}

static void
dsp_winamp_set_param(ddb_dsp_context_t *ctx, int p, const char *val)
{
	plugin_t *plugin = (plugin_t *)ctx;

	switch (p) {
	case 0:
		if (strcmp(val, plugin->dll) != 0) {
			child_stop(plugin);
			free(plugin->dll);
			plugin->dll = strdup(val);
			plugin->dll_exists = dll_exists(plugin->dll);
			if (!plugin->dll_exists)
				g_deadbeef->log("dsp_winamp: dll not found: %s\n", plugin->dll);
		}
		break;
	case 1:
		plugin->max_bps = atoi(val);
		if (plugin->max_bps % 8 != 0 || plugin->max_bps > 32) {
			g_deadbeef->log("dsp_winamp: invalid bit depth\n");
			plugin->max_bps = 0;
		}
		break;
	}
}

static void
dsp_winamp_get_param(ddb_dsp_context_t *ctx, int p, char *str, int len)
{
	plugin_t *plugin = (plugin_t *)ctx;

	switch (p) {
	case 0:
		snprintf(str, len, "%s", plugin->dll);
		break;
	case 1:
		snprintf(str, len, "%u", plugin->max_bps);
		break;
	}
}

static int
dsp_winamp_can_bypass(ddb_dsp_context_t *ctx, ddb_waveformat_t *fmt)
{
	plugin_t *plugin = (plugin_t *)ctx;
	(void)fmt;

	return !plugin->dll_exists;
}

static DB_dsp_t g_plugin = {
	.plugin.type = DB_PLUGIN_DSP,
	DDB_REQUIRE_API_VERSION(1, DDB_API_LEVEL)
	.plugin.id = "dsp_winamp",
	.plugin.name = "winamp dsp",
	.plugin.website = "https://github.com/huglovefan/ddb_dsp_winamp",
	.open = dsp_winamp_open,
	.close = dsp_winamp_close,
	.process = dsp_winamp_process,
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
	g_deadbeef = api;
	return &g_plugin.plugin;
}
