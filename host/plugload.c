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

	if (strstr(dllname, "centercut") != NULL) {
		out->doconf = 0;
	} else if (strstr(dllname, "freeverb") != NULL) {
		out->may_stretch = false;
	} else if (strstr(dllname, "pacemaker") != NULL) {
		// 576*3 may be buggy
		out->process_max_frames = 576*2;
		out->doconf = 0;
	} else if (strstr(dllname, "stereo_tool") != NULL) {
		out->process_max_frames = 0;
		out->may_stretch = false;
	}
}

bool
parse_plugin_options(const char *arg, struct plugin_options *out)
{
	char *s;
	bool last;

	const struct option {
		const char *name;
		char type;
		int *p;
	} options[] = {
		{"pmf", 'u', &out->process_min_frames},
		{"pMf", 'u', &out->process_max_frames},
		{"pfm", 'u', &out->process_frames_mult},
		{"stretch", 'b', &out->may_stretch},
		{"conf", 'b', &out->doconf},
		{"randomize", 'b', &out->randomize},
		{NULL, 0, NULL},
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

	*out = (struct plugin_options){
		.path = NULL,
		.module_idx = MODULE_IDX_DEFAULT,
		.process_min_frames = 1,
		.process_max_frames = 576,
		.process_frames_mult = 1,
		.may_stretch = 1,
		.doconf = 1,
		.randomize = 0,
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
			if (strcmp(name, opt->name) == 0)
				goto match;
			if (opt->type == 'b' &&
			    (value == NULL || strcmp(value, "1") == 0) &&
			    strncmp(name, "no", 2) == 0 &&
			    strcmp(name+2, opt->name) == 0) {
				name = name+2;
				value = "0";
				goto match;
			}
			continue;
match:
			switch (opt->type) {
			case 'u':
				if (value == NULL) {
					fprintf(stderr, "missing value for option \"%s\"\n", name);
					goto err;
				}
				if (!atoi_ok(value, opt->p)) {
					fprintf(stderr, "failed to parse value \"%s\" for option \"%s\"\n", value, name);
					goto err;
				}
				break;
			case 'b':
				value = value ?: "1";
				*opt->p = atoi(value);
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

void
plugin_randomize_opts(struct plugin *pl)
{
	if L (pl->random_cnt > 0) {
		pl->random_cnt--;
		return;
	}

	if (rand() % 100 >= 96) {
		// make plugin_process() add the data to the buffer without
		//  processing it
		// warning: three of these in a row and we'll be restarted for
		//  misbehaving
		pl->opts.process_frames_mult = 1;
		pl->opts.process_min_frames = 99999;
		pl->opts.process_max_frames = 99999;
		pl->random_cnt = 0; // 0 = this call only
		goto print;
	}

	switch (rand() % 4) {
	case 0: pl->opts.process_frames_mult = 1; break;
	case 1: pl->opts.process_frames_mult = 1; break;
	case 2: pl->opts.process_frames_mult = 8; break;
	case 3: pl->opts.process_frames_mult = 26; break;
	}

	switch (rand() % 4) {
	case 0: pl->opts.process_min_frames = 1; break;
	case 1: pl->opts.process_min_frames = 64; break;
	case 2: pl->opts.process_min_frames = 151; break;
	case 3: pl->opts.process_min_frames = 1567; break;
	}

	switch (rand() % 4) {
	case 0: pl->opts.process_max_frames = 201; break;
	case 1: pl->opts.process_max_frames = 4096; break;
	case 2: pl->opts.process_max_frames = 512; break;
	case 3: pl->opts.process_max_frames = 200; break;
	}

#define NEXTMULT(a, b) ( (a - (a % b)) + b )
#define MULTUP(a, b) if (a % b != 0) a = NEXTMULT(a, b)

	MULTUP(pl->opts.process_min_frames, pl->opts.process_frames_mult);
	MULTUP(pl->opts.process_max_frames, pl->opts.process_frames_mult);

	if (pl->opts.process_max_frames < pl->opts.process_min_frames)
		pl->opts.process_max_frames = pl->opts.process_min_frames;

#undef NEXTMULT
#undef MULTUP

	pl->random_cnt = rand() % 4;
print:
	fprintf(stderr, "pfm=%d pmf=%d pMf=%d\n",
	    pl->opts.process_frames_mult,
	    pl->opts.process_min_frames,
	    pl->opts.process_max_frames);
}
