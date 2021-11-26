#include "plugin.h"

#include <stdio.h>
#include <stdlib.h>

#include "main.h"
#include "macros.h"
#include "misc.h"

//
// apply safe default values for known plugins
//
static void
apply_defaults(const char *path, struct plugin_options *out)
{
	const char *dllname = superbasename(path);

	if (strcmp(dllname, "dsp_centercut.dll") == 0) {
		out->doconf = 0;
		// 8 = loud
		out->bits = strdup("16,24,32");
	}

	else if (strcmp(dllname, "dsp_freeverb.dll") == 0) {
		// doesn't actually seem to matter
		out->process_max_frames = 2048;
		out->may_stretch = false;
		// 24 = static. probably only really works with 16
		out->bits = strdup("8,16,32");
	}

	else if (strcmp(dllname, "dsp_pacemaker.dll") == 0) {
		// 576*2 = safe
		// 576*3 = freezes when starting a 48000Hz file as the first thing (set speed to +20%)
		// 2048+32 = crashes when stretching
		// this also works: [pmf=128 pfm=128 pMf=2048] but too-high minimum might be bad
		out->process_max_frames = 576*2;
		out->doconf = 0;
		// 8 = distorts when stretching
		out->bits = strdup("16,24,32");
	}

	else if (strcmp(dllname, "dsp_sps.dll") == 0) {
		// only 16 works properly
		out->bits = strdup("16");
	}

	else if (strcmp(dllname, "dsp_stereo_tool.dll") == 0) {
		out->process_max_frames = 0;
		out->may_stretch = false;
		// 8 = loud
		out->bits = strdup("16,24,32");
		out->required = true;
	}
}

bool
parse_plugin_options(const char *arg, struct plugin_options *out)
{
	char *s;
	bool last;

	const struct option {
		const char *name;
		enum {
			OPT_UINT,
			OPT_BOOL,
			OPT_STR,
		} type;
		union {
			int *i;
			unsigned *u;
			char **s;
		} v;
	} options[] = {
		{"pmf", OPT_UINT, {.u=&out->process_min_frames}},
		{"pMf", OPT_UINT, {.u=&out->process_max_frames}},
		{"pfm", OPT_UINT, {.u=&out->process_frames_mult}},
		{"stretch", OPT_BOOL, {.i=&out->may_stretch}},
		{"conf", OPT_BOOL, {.i=&out->doconf}},
		{"required", OPT_BOOL, {.i=&out->required}},
		{"trace", OPT_BOOL, {.i=&out->trace}},
		{"rate", OPT_STR, {.s=&out->rate}},
		{"bits", OPT_STR, {.s=&out->bits}},
		{"ch", OPT_STR, {.s=&out->ch}},
		{NULL, 0, {NULL}},
	};

	s = strdup(arg);
	if (s == NULL) {
		perror("strdup");
		return false;
	}

	// the default for process_max_frames is 576 to match the buffer size winamp uses
	// something to do with mp3 decoding

	// the DSP.H header says
	// "numsamples should always be at least 128. should, but I'm not sure"
	// should process_min_frames be 128 then? i'm not sure

	// pmf and pfm default to 32 to solve weirdness with non-power-of-two
	//  buffer sizes (24-bit flac)

	*out = (struct plugin_options){
		.path = NULL,
		.module_idx = MODULE_IDX_DEFAULT,
		.process_min_frames = 32,
		.process_max_frames = 576,
		.process_frames_mult = 32,
		.may_stretch = 1,
		.doconf = 1,
	};

	do {
		char *end;
		char *eq;
		const char *name;
		const char *value;

		end = strchrnul(s, ':');
		last = (*end == '\0');
		*end = '\0';

		if (out->path == NULL) {
			out->path = s;
			apply_defaults(s, out);
			goto next;
		}
		if (*s >= '0' && *s <= '9' && atoi_ok(s, &out->module_idx))
			goto next;

		eq = strchr(s, '=');
		name = s;
		value = eq+1;
		if (eq) *eq = '\0';
		else value = NULL;

		for (const struct option *opt = options; opt->name != NULL; opt++) {
			unsigned int prefixlen = 0;
			if (strcmp(name, opt->name) == 0)
				goto match;
			// accept prefixes for boolean option names
			// "do": ignore, "no"/"not": negate the value
			if (opt->type == OPT_BOOL &&
			    ((strncmp(name, "do", prefixlen=2) == 0 && strcmp(name+prefixlen, opt->name) == 0) ||
			     (strncmp(name, "no", prefixlen=2) == 0 && strcmp(name+prefixlen, opt->name) == 0) ||
			     (strncmp(name, "not", prefixlen=3) == 0 && strcmp(name+prefixlen, opt->name) == 0))) {
				goto match;
			}
			continue;
match:
			switch (opt->type) {
			case OPT_UINT:
				if (value == NULL) {
					fprintf(stderr, "missing value for option \"%s\"\n", name);
					goto err;
				}
				if (!atoi_ok(value, opt->v.i)) {
					fprintf(stderr, "failed to parse value \"%s\" for option \"%s\"\n", value, name);
					goto err;
				}
				break;
			case OPT_BOOL:
				*opt->v.i = (value != NULL) ? !!atoi(value) : 1;
				if (prefixlen != 0 && *name == 'n') // negated
					*opt->v.i = !*opt->v.i;
				break;
			case OPT_STR:
				free(*opt->v.s);
				*opt->v.s = strdup(value);
				break;
			default:
				assert(!"option has invalid type");
			}
			goto next;
		}

		if (strcmp(name, "safemode") == 0) {
			out->process_min_frames = 576;
			out->process_max_frames = 576;
			out->process_frames_mult = 576;
			out->may_stretch = 1;
			goto next;
		}

		fprintf(stderr, "unrecognized option \"%s\"\n", s);
		goto err;
next:
		s = end+1;
	} while (!last);

	if (out->path == NULL || *out->path == '\0')
		goto err;

	if (out->process_min_frames % out->process_frames_mult != 0) {
		fprintf(stderr, "error: process_min_frames %d is not a multiple of process_frames_mult %d\n",
		    out->process_min_frames, out->process_frames_mult);
		goto err;
	}

	if (out->process_max_frames % out->process_frames_mult != 0) {
		fprintf(stderr, "error: process_max_frames %d is not a multiple of process_frames_mult %d\n",
		    out->process_max_frames, out->process_frames_mult);
		goto err;
	}

	if (out->process_max_frames != 0 &&
	    out->process_min_frames > out->process_max_frames) {
		fprintf(stderr, "error: process_min_frames %d is greater than process_max_frames %d\n",
		    out->process_min_frames, out->process_max_frames);
		goto err;
	}

	return true;
err:
	free(s);
	out->path = NULL;
	return false;
}

bool
load_plugin(struct plugin *pl)
{
	HANDLE dll;
	winampDSPGetHeaderType get_header;
	winampDSPHeader *header;
	winampDSPModule *module = NULL;
	int init_rv;

	dll = LoadLibrary(pl->opts.path);
	if (dll == NULL) {
		fprintf(stderr, "load_plugin: failed to open %s using LoadLibrary: %s\n",
		    superbasename(pl->opts.path),
		    StrError(GetLastError()));
		goto err;
	}

	get_header = (winampDSPGetHeaderType)(void *)GetProcAddress(dll, "winampDSPGetHeader2");
	if (get_header == NULL) {
		fprintf(stderr, "load_plugin: failed to get winampDSPGetHeader2() from %s: %s\n",
		    superbasename(pl->opts.path),
		    StrError(GetLastError()));
		goto err;
	}

	//
	// DSP.H says get_header() wasn't passed the HWND before winamp 5.5,
	//  unless the plugin was compiled with the macros USE_DSP_HDR_HWND=1
	//  and DSP_HDRVER=0x22, but idk how you're meant to check the plugin's
	//  version without calling the function first
	//

	header = get_header(mainwin);
	if (header == NULL) {
		fprintf(stderr, "load_plugin: winampDSPGetHeader2() returned NULL!\n");
		goto err;
	}

	if (pl->opts.module_idx != MODULE_IDX_DEFAULT) {
		module = header->getModule(pl->opts.module_idx);
	} else {
		if ((module = header->getModule(0)) != NULL)
			pl->opts.module_idx = 0;
		else if ((module = header->getModule(1)) != NULL)
			pl->opts.module_idx = 1;
	}
	if (module == NULL) {
		if (pl->opts.module_idx != MODULE_IDX_DEFAULT)
			fprintf(stderr, "load_plugin: %s has no module with index %d\n",
			    superbasename(pl->opts.path),
			    pl->opts.module_idx);
		else
			fprintf(stderr, "load_plugin: %s has no module with index 0 or 1!\n",
			    superbasename(pl->opts.path));
		goto err;
	}

	module->hDllInstance = dll;
	module->hwndParent = mainwin;

	// DSP.H: "0 on success"
	init_rv = module->Init(module);
	if (init_rv != 0) {
		fprintf(stderr, "load_plugin: Init() failed! (%d)\n", init_rv);
		goto err;
	}

	printf("%s: %s\n", superbasename(pl->opts.path), header->description);
	printf("%s:%d: %s\n", superbasename(pl->opts.path), pl->opts.module_idx, module->description);

	pl->module = module;
	pl->dll = dll;

	return true;
err:
	if (module != NULL) {
		module->hDllInstance = NULL;
		module->hwndParent = NULL;
	}
	if (dll != NULL)
		FreeLibrary(dll);

	return false;
}

static bool
match_string(const char *spec, const char *value)
{
	const char *p = spec;
	const char *end;
	size_t vallen = strlen(value);
	size_t ptlen;

	for (;;) {
		end = strchrnul((char *)p, ',');
		ptlen = (size_t)(end-p);

		if (ptlen == vallen && memcmp(p, value, vallen) == 0)
			return true;

		if (*end == '\0')
			break;

		p = end+1;
	}

	return false;
}

const char *
plugin_supports_format(struct plugin *pl, const struct fmt *fmt)
{
	char ratestr[16];
	char bitstr[16];
	char chstr[16];

	snprintf(ratestr, sizeof(ratestr), "%d", fmt->rate);
	snprintf(bitstr, sizeof(bitstr), "%d", fmt->bps);
	snprintf(chstr, sizeof(chstr), "%d", fmt->ch);

	if (pl->opts.rate != NULL && !match_string(pl->opts.rate, ratestr))
		return "sample rate";

	if (pl->opts.bits != NULL && !match_string(pl->opts.bits, bitstr))
		return "bit depth";

	if (pl->opts.ch != NULL && !match_string(pl->opts.ch, chstr))
		return "channel count";

	return NULL;
}
