#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <deadbeef/deadbeef.h>

#include "../common.h"

static DB_functions_t *deadbeef;
static DB_dsp_t g_plugin;

typedef struct {
	ddb_dsp_context_t ctx;
	char *command;
	unsigned short max_bps;
	struct {
		pid_t pid;
		int stdin;
		int stdout;
	} child;
} plugin_t;

static void
child_stop(plugin_t *plugin)
{
	close(plugin->child.stdout);
	plugin->child.stdout = -1;
	close(plugin->child.stdin);
	plugin->child.stdin = -1;

	if (plugin->child.pid >= 0) {
		waitpid(plugin->child.pid, NULL, 0);
		plugin->child.pid = -1;
	}
}

static unsigned int
get_max_fd(void)
{
	struct rlimit nofile;

	if (getrlimit(RLIMIT_NOFILE, &nofile) == 0)
		return nofile.rlim_cur;
	else
		return 1024;
}

static void
child_start(plugin_t *plugin)
{
	int stdin[2] = {-1, -1},
	    stdout[2] = {-1, -1}; /* {read_end, write_end} */
	unsigned long max_fd = get_max_fd();
	pid_t pid = -1;

	if ((pipe(stdin) < 0) ||
	    (pipe(stdout) < 0))
		goto failed;

	pid = fork();
	if (pid < 0) {
		perror("dsp_winamp: fork");
failed:
		close(stdin[0]);
		close(stdin[1]);
		close(stdout[0]);
		close(stdout[1]);
	} else if (pid == 0) {
		dup2(stdin[0], STDIN_FILENO);
		dup2(stdout[1], STDOUT_FILENO);

		for (unsigned i = 3; i < max_fd; i++)
			close(i);

		execl("/bin/sh", "sh", "-c", plugin->command, NULL);
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

	plugin->command = strdup("");
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

	free(plugin->command);
	free(plugin);
}

static int
process_check_child(plugin_t *plugin)
{
	if (plugin->child.pid < 0)
		child_start(plugin);
	else if (waitpid(plugin->child.pid, NULL, WNOHANG) > 0) {
		plugin->child.pid = -1;
		child_stop(plugin);
		child_start(plugin);
		if (plugin->child.pid < 0)
			return -1;
	}
	return 0;
}

static int
dsp_winamp_process_normal(ddb_dsp_context_t *ctx,
                         float *samples,
                         int frames,
                         int maxframes,
                         ddb_waveformat_t *fmt,
                         float *ratio)
{
	plugin_t *plugin = (plugin_t *)ctx;
	struct processing_request request;
	struct processing_response response;
	char *intsamples;
	(void)ratio;

	/* start child if not running */
	if (process_check_child(plugin) < 0)
		return 0;

	/* make sure samples are in integer format */
	if (fmt->is_float) {
		intsamples = alloca(maxframes*(fmt->bps/8)*fmt->channels);
		ddb_waveformat_t convfmt = *fmt;
		convfmt.is_float = 0;
		assert(deadbeef->pcm_convert(
		    fmt, (char *)samples,
		    &convfmt, intsamples,
		    frames*(fmt->bps/8)*fmt->channels) == frames*(fmt->bps/8)*fmt->channels);
		fmt->is_float = 0;
	} else {
		intsamples = (char *)samples;
	}

	request.buffer_size = frames*(fmt->bps/8)*fmt->channels;
	request.samplerate = fmt->samplerate;
	request.bitspersample = fmt->bps;
	request.channels = fmt->channels;
	if (write(plugin->child.stdin, &request, sizeof(request)) != sizeof(request))
		goto failed;
	if ((size_t)write(plugin->child.stdin, intsamples, request.buffer_size) != request.buffer_size)
		goto failed;

	if (read(plugin->child.stdout, &response, sizeof(response)) != sizeof(response))
		goto failed;
	if (response.buffer_size > 0) {
		if (response.buffer_size > (size_t)(maxframes*(fmt->bps/8)*fmt->channels))
			goto failed;
		if ((size_t)read(plugin->child.stdout, samples, response.buffer_size) != response.buffer_size)
			goto failed;
	}

	return response.buffer_size/(fmt->bps/8)/fmt->channels;
failed:
	/* something went wrong, child is probably in an inconsistent state now */
	fprintf(stderr, "dsp_winamp: something happened!\n");
	child_stop(plugin);
	return 0;
}

/* limited bit depth version for broken plugins
   (this should probably be done in the host instead) */
static int
dsp_winamp_process_fixed(ddb_dsp_context_t *ctx,
                        float *samples,
                        int frames,
                        int maxframes,
                        ddb_waveformat_t *fmt,
                        float *ratio)
{
	plugin_t *plugin = (plugin_t *)ctx;
	struct processing_request request;
	struct processing_response response;
	ddb_waveformat_t convfmt;
	char *convbuf;
	(void)ratio;

	/* start child if not running */
	if (process_check_child(plugin) < 0)
		return 0;

	convfmt = *fmt;
	convfmt.bps = plugin->max_bps;
	convfmt.is_float = 0;

	convbuf = alloca(maxframes*(convfmt.bps/8)*convfmt.channels);
	assert(deadbeef->pcm_convert(
	    fmt, (char *)samples,
	    &convfmt, convbuf,
	    frames*(fmt->bps/8)*fmt->channels) == frames*(convfmt.bps/8)*convfmt.channels);

	request.buffer_size = frames*(convfmt.bps/8)*convfmt.channels;
	request.samplerate = convfmt.samplerate;
	request.bitspersample = convfmt.bps;
	request.channels = convfmt.channels;
	if (write(plugin->child.stdin, &request, sizeof(request)) != sizeof(request))
		goto failed;
	if ((size_t)write(plugin->child.stdin, convbuf, request.buffer_size) != request.buffer_size)
		goto failed;

	if (read(plugin->child.stdout, &response, sizeof(response)) != sizeof(response))
		goto failed;
	if (response.buffer_size > 0) {
		if (response.buffer_size > (size_t)(maxframes*(convfmt.bps/8)*convfmt.channels))
			goto failed;
		if (response.buffer_size % ((convfmt.bps/8)*convfmt.channels) != 0)
			goto failed;
		if ((size_t)read(plugin->child.stdout, convbuf, response.buffer_size) != response.buffer_size)
			goto failed;
		/* deadbeef assumes the samples are 32 bits even if we set fmt->bps */
		fmt->is_float = 0; /* don't convert to float again (this works) */
		assert((size_t)deadbeef->pcm_convert(
		    &convfmt, convbuf,
		    fmt, (char *)samples,
		    response.buffer_size) == (response.buffer_size/(convfmt.bps/8)/convfmt.channels)*(fmt->bps/8)*fmt->channels);
	}

	return response.buffer_size/(convfmt.bps/8)/convfmt.channels;
failed:
	/* something went wrong, child is probably in an inconsistent state now */
	fprintf(stderr, "dsp_winamp: something happened!\n");
	child_stop(plugin);
	return 0;
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

	if (plugin->max_bps != 0 && fmt->bps > plugin->max_bps)
		return dsp_winamp_process_fixed(ctx, samples, frames, maxframes, fmt, ratio);
	else
		return dsp_winamp_process_normal(ctx, samples, frames, maxframes, fmt, ratio);
}

static void
dsp_winamp_reset(ddb_dsp_context_t *ctx)
{
	(void)ctx;
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
		return "Command";
	case 1:
		return "Max. bit depth";
	}
	return NULL;
}

static void
dsp_winamp_set_param(ddb_dsp_context_t *ctx, int p, const char *val)
{
	plugin_t *plugin = (plugin_t *)ctx;

	switch (p) {
	case 0:
		if (strcmp(val, plugin->command) != 0) {
			child_stop(plugin);
			plugin->command = strdup(val);
		}
		break;
	case 1:
		plugin->max_bps = atoi(val);
		if (plugin->max_bps % 8 != 0)
			plugin->max_bps = 0;
		break;
	}
}

static void
dsp_winamp_get_param(ddb_dsp_context_t *ctx, int p, char *str, int len)
{
	plugin_t *plugin = (plugin_t *)ctx;

	switch (p) {
	case 0:
		snprintf(str, len, "%s", plugin->command);
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

	return (plugin->command[0] == '\0');
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
	.reset = dsp_winamp_reset,
	.num_params = dsp_winamp_num_params,
	.get_param_name = dsp_winamp_get_param_name,
	.set_param = dsp_winamp_set_param,
	.get_param = dsp_winamp_get_param,
	.configdialog =
		"property \"Command\" entry 0 \"\" ;\n"
		"property \"Max. bit depth\" entry 1 \"\" ;\n",
	.can_bypass = dsp_winamp_can_bypass,
};

DB_plugin_t *
dsp_winamp_load(DB_functions_t *api)
{
	deadbeef = api;
	return &g_plugin.plugin;
}
