module ddw.host.plugload;

import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;
import core.sys.windows.winbase;
import core.sys.windows.windef;

import ddw.host.fmt;
import ddw.host.main;
import ddw.host.misc;
import ddw.host.plugin;
import ddw.host.winamp;

debug = forceSafeBufferSizes;

nothrow:
@nogc:

/**
 * apply default parameters for some known plugins
 * 
 * these can be overwritten by values specified on the command line
 */
void apply_defaults(const(char)* path, PluginOpts* outp)
{
	const(char)* dllname = superbasename(path);

	if (strcmp(dllname, "dsp_centercut.dll") == 0)
	{
		outp.doconf = 0;
		outp.bits = strdup("16,24,32"); // 8 = loud
	}
	else if (strcmp(dllname, "dsp_freeverb.dll") == 0)
	{
		outp.may_stretch = false;
		outp.bits = strdup("8,16,32"); // 24 = static. probably only really works with 16
	}
	else if (strcmp(dllname, "dsp_pacemaker.dll") == 0)
	{
		outp.doconf = 0;
		outp.bits = strdup("16,24,32"); // 8 = distorts when stretching
	}
	else if (strcmp(dllname, "dsp_sps.dll") == 0)
	{
		outp.bits = strdup("16"); // only 16 works properly
	}
	else if (strcmp(dllname, "dsp_stereo_tool.dll") == 0)
	{
		outp.may_stretch = false;
		outp.bits = strdup("16,24,32"); // 8 = loud
		outp.required = true;
	}
}

/**
 * parse a command-line argument containing the dll path and some parameters
 */
bool parse_plugin_options(const(char)* arg, PluginOpts* outp)
{
	char* s;
	bool last;

	struct Option
	{
		const(char)* name;
		enum Type
		{
			OPT_UINT,
			OPT_BOOL,
			OPT_STR,
		}
		Type type;
		union Value
		{
			int* i;
			uint* u;
			char** s;
		}
		Value v;
	}
	Option[10] options = [
		{"pmf", Option.Type.OPT_UINT, {u: &outp.process_min_frames}},
		{"pMf", Option.Type.OPT_UINT, {u: &outp.process_max_frames}},
		{"pfm", Option.Type.OPT_UINT, {u: &outp.process_frames_mult}},
		{"stretch", Option.Type.OPT_BOOL, {i: &outp.may_stretch}},
		{"conf", Option.Type.OPT_BOOL, {i: &outp.doconf}},
		{"required", Option.Type.OPT_BOOL, {i: &outp.required}},
		{"trace", Option.Type.OPT_BOOL, {i: &outp.trace}},
		{"rate", Option.Type.OPT_STR, {s: &outp.rate}},
		{"bits", Option.Type.OPT_STR, {s: &outp.bits}},
		{"ch", Option.Type.OPT_STR, {s: &outp.ch}},
	];

	s = strdup(arg);
	if (s == null)
	{
		perror("strdup");
		return false;
	}

	// the default for process_max_frames is 576 to match the buffer size winamp uses
	// (something to do with mp3 decoding)

	*outp = PluginOpts.init;
	outp.path = null;
	outp.module_idx = MODULE_IDX_DEFAULT;
	outp.process_min_frames = 576;
	outp.process_max_frames = 576;
	outp.process_frames_mult = 576;
	outp.may_stretch = 1;
	outp.doconf = 1;

	do
	{
		char* end;
		char* eq;
		const(char)* name;
		const(char)* value;

		end = strchrnul(s, ':');
		last = (*end == '\0');
		*end = '\0';

		if (outp.path == null)
		{
			outp.path = s;
			apply_defaults(s, outp);
			goto next;
		}
		if (*s >= '0' && *s <= '9' && atoi_ok(s, &outp.module_idx))
			goto next;

		eq = strchr(s, '=');
		name = s;
		value = eq+1;
		if (eq) *eq = '\0';
		else value = null;

		foreach (ref opt; options)
		{
			uint prefixlen = 0;
			if (strcmp(name, opt.name) == 0)
				goto match;
			// accept prefixes for boolean option names
			// "do": ignore, "no"/"not": negate the value
			if (opt.type == Option.Type.OPT_BOOL &&
				((strncmp(name, "do", prefixlen=2) == 0 && strcmp(name+prefixlen, opt.name) == 0) ||
				(strncmp(name, "no", prefixlen=2) == 0 && strcmp(name+prefixlen, opt.name) == 0) ||
				(strncmp(name, "not", prefixlen=3) == 0 && strcmp(name+prefixlen, opt.name) == 0)))
			{
				goto match;
			}
			continue;
match:
			switch (opt.type)
			{
			case Option.Type.OPT_UINT:
				if (value == null)
				{
					fprintf(stderr, "missing value for option \"%s\"\n", name);
					goto err;
				}
				if (!atoi_ok(value, opt.v.i))
				{
					fprintf(stderr, "failed to parse value \"%s\" for option \"%s\"\n", value, name);
					goto err;
				}
				break;
			case Option.Type.OPT_BOOL:
				*opt.v.i = (value != null) ? !!atoi(value) : 1;
				if (prefixlen != 0 && name[0] == 'n') // negated // <-- won't this bug out when the real name start with n?
					*opt.v.i = !*opt.v.i;
				break;
			case Option.Type.OPT_STR:
				free(*opt.v.s);
				*opt.v.s = strdup(value);
				break;
			default:
				assert(0, "option has invalid type");
			}
			goto next;
		}

		if (strcmp(name, "safemode") == 0)
		{
			outp.process_min_frames = 576;
			outp.process_max_frames = 576;
			outp.process_frames_mult = 576;
			outp.may_stretch = 1;
			goto next;
		}

		fprintf(stderr, "unrecognized option \"%s\"\n", s);
		goto err;
next:
		s = end+1;
	}
	while (!last);

	if (outp.path == null || *outp.path == '\0')
		goto err;

	debug (forceSafeBufferSizes)
	{
		outp.process_min_frames = 576;
		outp.process_max_frames = 576;
		outp.process_frames_mult = 576;
	}

	if (outp.process_min_frames % outp.process_frames_mult != 0)
	{
		fprintf(stderr, "error: process_min_frames %d is not a multiple of process_frames_mult %d\n",
			outp.process_min_frames, outp.process_frames_mult);
		goto err;
	}

	if (outp.process_max_frames % outp.process_frames_mult != 0)
	{
		fprintf(stderr, "error: process_max_frames %d is not a multiple of process_frames_mult %d\n",
			outp.process_max_frames, outp.process_frames_mult);
		goto err;
	}

	if (
		outp.process_max_frames != 0 &&
		outp.process_min_frames > outp.process_max_frames)
	{
		fprintf(stderr, "error: process_min_frames %d is greater than process_max_frames %d\n",
			outp.process_min_frames, outp.process_max_frames);
		goto err;
	}

	return true;
err:
	free(s);
	outp.path = null;
	return false;
}

/**
 * loads and initializes the dll of a plugin according to its parsed options
 */
version (Windows)
bool load_plugin(Plugin* pl)
{
	HANDLE dll;
	winampDSPGetHeaderType get_header;
	winampDSPHeader* header;
	winampDSPModule* module_ = null;
	int init_rv;

	dll = LoadLibraryA(pl.opts.path);
	if (dll == null)
	{
		fprintf(stderr, "load_plugin: failed to open %s using LoadLibrary: %s\n",
			superbasename(pl.opts.path),
			StrError(GetLastError()));
		goto err;
	}

	get_header = cast(winampDSPGetHeaderType)GetProcAddress(dll, "winampDSPGetHeader2");
	if (get_header == null)
	{
		fprintf(stderr, "load_plugin: failed to get winampDSPGetHeader2() from %s: %s\n",
			superbasename(pl.opts.path),
			StrError(GetLastError()));
		goto err;
	}

	header = get_header();
	if (header == null)
	{
		fprintf(stderr, "load_plugin: winampDSPGetHeader2() returned NULL!\n");
		goto err;
	}

	if (pl.opts.module_idx != MODULE_IDX_DEFAULT)
	{
		module_ = header.getModule(pl.opts.module_idx);
	}
	else
	{
		if ((module_ = header.getModule(0)) != null)
			pl.opts.module_idx = 0;
		else if ((module_ = header.getModule(1)) != null)
			pl.opts.module_idx = 1;
	}

	if (module_ == null)
	{
		if (pl.opts.module_idx != MODULE_IDX_DEFAULT)
			fprintf(stderr, "load_plugin: %s has no module with index %d\n",
				superbasename(pl.opts.path),
				pl.opts.module_idx);
		else
			fprintf(stderr, "load_plugin: %s has no module with index 0 or 1!\n",
				superbasename(pl.opts.path));
		goto err;
	}

	module_.hDllInstance = dll;
	module_.hwndParent = mainwin;

	// DSP.H: "0 on success"
	init_rv = module_.Init(module_);
	if (init_rv != 0)
	{
		fprintf(stderr, "load_plugin: Init() failed! (%d)\n", init_rv);
		goto err;
	}

	printf("%s: %s\n", superbasename(pl.opts.path), header.description);
	printf("%s:%d: %s\n", superbasename(pl.opts.path), pl.opts.module_idx, module_.description);

	pl.module_ = module_;
	pl.dll = dll;

	return true;
err:
	if (module_ != null)
	{
		module_.hDllInstance = null;
		module_.hwndParent = null;
	}
	if (dll != null)
		FreeLibrary(dll);

	return false;
}

bool match_string(const(char)* spec, const(char)* value)
{
	const(char)* p = spec;
	const(char)* end;
	size_t vallen = strlen(value);
	size_t ptlen;

	for (;;)
	{
		end = strchrnul(cast(char*)p, ',');
		ptlen = cast(size_t)(end-p);

		if (ptlen == vallen && memcmp(p, value, vallen) == 0)
			return true;

		if (*end == '\0')
			break;

		p = end+1;
	}

	return false;
}

/**
 * check if a plugin supports a format given its parameters
 * 
 * if the format isn't supported, returns a static string containing the name of
 *  the incompatible property
 * 
 * if the format is supported, returns null
 */
const(char)* plugin_supports_format(Plugin* pl, const(Fmt)* fmt)
{
	char[16] ratestr;
	char[16] bitstr;
	char[16] chstr;

	snprintf(ratestr.ptr, ratestr.length, "%d", fmt.rate);
	snprintf(bitstr.ptr, bitstr.length, "%d", fmt.bps);
	snprintf(chstr.ptr, chstr.length, "%d", fmt.ch);

	if (pl.opts.rate != null && !match_string(pl.opts.rate, ratestr.ptr))
		return "sample rate";

	if (pl.opts.bits != null && !match_string(pl.opts.bits, bitstr.ptr))
		return "bit depth";

	if (pl.opts.ch != null && !match_string(pl.opts.ch, chstr.ptr))
		return "channel count";

	return null;
}
