module ddw.host.plugload;

import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;
import core.sys.windows.winbase;
import core.sys.windows.windef;

import ddw.common.gc;
import ddw.host.fmt;
import ddw.host.main;
import ddw.host.misc;
import ddw.host.plugin;
import ddw.host.winamp;

//debug = parse;
//debug = search;

bool parse_plugin_options(const(char)[] s, PluginOpts* outp)
{
	*outp = PluginOpts.init;

	const(char)* p = s.ptr;
	argloop: while (*p)
	{
		const(char)* nul = strchrnul(p, ':');
		char[] part = p[0..nul-p].gcdup;
		p = nul+!!*nul;

		debug(parse) printf("part=[%.*s]\n", cast(int)part.length, part.ptr);

		if (!outp.path)
		{
			const(char)[] found = find_dll(part);
			if (!found)
			{
				printf("dll not found\n");
				return false;
			}
			outp.path = found;
			outp.dllname = superbasename(found);
			debug(parse) printf("path=[%.*s]\n", cast(int)outp.path.length, outp.path.ptr);
			debug(parse) printf("dllname=[%.*s]\n", cast(int)outp.dllname.length, outp.dllname.ptr);
			apply_defaults(outp);
			continue argloop;
		}

		if (part.length && part[0] >= '0' && part[0] <= '9')
		{
			int n;
			if (sscanf(part.ptr, "%d%n", &outp.module_idx, &n) != 1 || n != part.length)
			{
				printf("failed to parse module index \"%s\"\n", part.ptr);
				return false;
			}
			continue argloop;
		}

		char[] name = part;
		char[] value = null;
		if (char* eq = cast(char*)memchr(part.ptr, '=', part.length))
		{
			name = part[0..eq-part.ptr].gcdup;
			value = part[(eq-part.ptr)+1..$].gcdup;
		}

		debug(parse) printf("name=[%.*s]\n", cast(int)name.length, name.ptr);
		debug(parse) printf("value=[%.*s]\n", cast(int)value.length, value.ptr);

		optloop: foreach (ref opt; optdef)
		{
			if (name != opt.name)
				continue optloop;

			void* vp = cast(void*)outp + opt.off;
			final switch (opt.typ)
			{
				case Optdef.T.IBool:
				case Optdef.T.Bool:
					if (value)
					{
						// note: can't read it to the struct directly because m$ doesn't support the %hh size modifier
						uint tmp;
						int n;
						if (sscanf(value.ptr, "%u%n", &tmp, &n) != 1 || n != value.length)
						{
							printf("failed to parse value \"%s\" for option %s\n", value.ptr, name.ptr);
							return false;
						}
						*cast(ubyte*)vp = !!tmp;
					}
					else
						*cast(bool*)vp = true;
					if (opt.typ == Optdef.T.IBool)
						*cast(bool*)vp = !*cast(bool*)vp;
					continue argloop;
				case Optdef.T.Uint:
					int n;
					if (sscanf(value.ptr, "%u%n", cast(uint*)vp, &n) != 1 || n != value.length)
					{
						printf("failed to parse value \"%s\" for option %s\n", value.ptr, name.ptr);
						return false;
					}
					continue argloop;
				case Optdef.T.NSeq:
					uint[] vs = parse_numseq(value);
					if (!vs)
					{
						printf("failed to parse value \"%s\" for option %s\n", value.ptr, name.ptr);
						return false;
					}
					*cast(uint[]*)vp = vs;
					continue argloop;
			}
		}

		printf("unknown option \"%s\"\n", name.ptr);
		return false;
	}

	if (outp.process_min_frames % outp.process_frames_mult != 0)
	{
		printf("error: process_min_frames %u is not a multiple of process_frames_mult %u\n",
			outp.process_min_frames, outp.process_frames_mult);
		return false;
	}

	if (outp.process_max_frames % outp.process_frames_mult != 0)
	{
		printf("error: process_max_frames %u is not a multiple of process_frames_mult %u\n",
			outp.process_max_frames, outp.process_frames_mult);
		return false;
	}

	if (
		outp.process_max_frames != 0 &&
		outp.process_min_frames > outp.process_max_frames)
	{
		printf("error: process_min_frames %u is greater than process_max_frames %u\n",
			outp.process_min_frames, outp.process_max_frames);
		return false;
	}

	return true;
}

/**
 * loads and initializes the dll of a plugin according to its parsed options
 */
bool load_plugin(Plugin* pl)
{
	HANDLE dll;
	winampDSPGetHeaderType get_header;
	winampDSPHeader* header;
	winampDSPModule* module_ = null;
	int init_rv;

	dll = LoadLibraryA(pl.opts.path.ptr);
	if (!dll)
	{
		int err = GetLastError();
		printf("load_plugin: failed to open %s using LoadLibrary: %s (%u)\n",
			pl.opts.dllname.ptr,
			StrError(err),
			err);
		goto err;
	}

	get_header = cast(winampDSPGetHeaderType)GetProcAddress(dll, "winampDSPGetHeader2");
	if (!get_header)
	{
		int err = GetLastError();
		printf("load_plugin: failed to get winampDSPGetHeader2() from %s: %s (%u)\n",
			pl.opts.dllname.ptr,
			StrError(err),
			GetLastError());
		goto err;
	}

	header = get_header(globals.mainWindow);
	if (!header)
	{
		printf("load_plugin: winampDSPGetHeader2() returned NULL!\n");
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

	if (!module_)
	{
		if (pl.opts.module_idx != MODULE_IDX_DEFAULT)
			printf("load_plugin: %s has no module with index %d\n",
				pl.opts.dllname.ptr,
				pl.opts.module_idx);
		else
			printf("load_plugin: %s has no module with index 0 or 1!\n",
				pl.opts.dllname.ptr);
		goto err;
	}

	module_.hDllInstance = dll;
	module_.hwndParent = globals.mainWindow;

	// DSP.H: "0 on success"
	init_rv = module_.Init(module_);
	if (init_rv != 0)
	{
		printf("load_plugin: Init() failed! (%d)\n", init_rv);
		goto err;
	}

	printf("%s: %s\n", pl.opts.dllname.ptr, header.description);
	printf("%s:%d: %s\n", pl.opts.dllname.ptr, pl.opts.module_idx, module_.description);

	pl.module_ = module_;
	pl.dll = dll;

	return true;
err:
	if (module_)
	{
		module_.hDllInstance = null;
		module_.hwndParent = null;
	}
	if (dll)
		FreeLibrary(dll);

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
const(char)* plugin_supports_format(const(Plugin)* pl, const(Fmt)* fmt)
{
	if (pl.opts.rate.ptr && !includes(pl.opts.rate, fmt.rate))
		return "sample rate";

	if (pl.opts.bits.ptr && !includes(pl.opts.bits, fmt.bps))
		return "bit depth";

	if (pl.opts.ch.ptr && !includes(pl.opts.ch, fmt.ch))
		return "channel count";

	return null;
}

// -----------------------------------------------------------------------------

private:

// -----------------------------------------------------------------------------

struct Optdef
{
	enum T
	{
		Uint,
		IBool,
		Bool,
		NSeq,
	}

	const(char)[] name;
	T typ;
	size_t off;
}

static immutable Optdef[] optdef = [
	Optdef("pmf", Optdef.T.Uint, PluginOpts.process_min_frames.offsetof),
	Optdef("pMf", Optdef.T.Uint, PluginOpts.process_max_frames.offsetof),
	Optdef("pfm", Optdef.T.Uint, PluginOpts.process_frames_mult.offsetof),

	Optdef("stretch",   Optdef.T.IBool, PluginOpts.nostretch.offsetof),
	Optdef("dostretch", Optdef.T.IBool, PluginOpts.nostretch.offsetof),
	Optdef("nostretch", Optdef.T.Bool,  PluginOpts.nostretch.offsetof),

	Optdef("conf",   Optdef.T.IBool, PluginOpts.noconf.offsetof),
	Optdef("doconf", Optdef.T.IBool, PluginOpts.noconf.offsetof),
	Optdef("noconf", Optdef.T.Bool,  PluginOpts.noconf.offsetof),

	Optdef("required",    Optdef.T.Bool,  PluginOpts.required.offsetof),
	Optdef("notrequired", Optdef.T.IBool, PluginOpts.required.offsetof),

	Optdef("rate", Optdef.T.NSeq, PluginOpts.rate.offsetof),
	Optdef("bits", Optdef.T.NSeq, PluginOpts.bits.offsetof),
	Optdef("ch",   Optdef.T.NSeq, PluginOpts.ch.offsetof),
];

// -----------------------------------------------------------------------------

/**
 * apply default parameters for some known plugins
 * 
 * these can be overwritten by values specified on the command line
 */
void apply_defaults(PluginOpts* outp)
{
	switch (outp.dllname)
	{
		case "dsp_centercut.dll":
			outp.noconf = true;
			outp.bits = [16,24,32]; // 8 = loud
			break;

		case "dsp_freeverb.dll":
			outp.nostretch = true;
			outp.bits = [8,16,32]; // 24 = static. probably only really works with 16
			break;

		case "dsp_pacemaker.dll":
			outp.noconf = true;
			outp.bits = [16,24,32]; // 8 = distorts when stretching
			break;

		case "dsp_sps.dll":
			outp.bits = [16]; // only 16 works properly
			break;

		case "dsp_stereo_tool.dll":
			outp.nostretch = true;
			outp.bits = [16,24,32]; // 8 = loud
			outp.required = true;
			break;

		default:
			break;
	}
}

unittest
{
	PluginOpts opts;

	assert(parse_plugin_options("dsp_stereo_tool.dll", &opts));
	assert(opts.path == "dsp_stereo_tool.dll");
	assert(opts.module_idx == MODULE_IDX_DEFAULT);
	assert(opts.required); // apply_defaults()

	assert(parse_plugin_options("dsp_unknown.dll:1", &opts));
	assert(opts.path == "dsp_unknown.dll");
	assert(opts.module_idx == 1);
	assert(!opts.required);

	assert(parse_plugin_options("dsp_unknown.dll:2:required", &opts));
	assert(opts.path == "dsp_unknown.dll");
	assert(opts.module_idx == 2);
	assert(opts.required);

	assert(parse_plugin_options(
		"dsp_unknown.dll:9:pmf=10:pMf=20:pfm=5:stretch:nostretch:dostretch:stretch=0:stretch=1:conf:noconf:doconf:conf=0:conf=1:required:notrequired:required=0:required=1:rate=8000,44100:bits=8,24:ch=1,2", &opts));
	assert(opts.path == "dsp_unknown.dll");
	assert(opts.module_idx == 9);
	assert(opts.process_min_frames == 10);
	assert(opts.process_max_frames == 20);
	assert(opts.process_frames_mult == 5);
	assert(!opts.nostretch);
	assert(!opts.noconf);
	assert(opts.required);
	assert(opts.rate == [8000,44100]);
	assert(opts.bits == [8,24]);
	assert(opts.ch == [1,2]);
}

/**
 * takes a dll path or name, returns a path to a dll that exists (or null)
 * 
 * if given just the name, the dll is searched for in some directories
 * 
 * the DDW_DLL_PATH environment variable should contain linux paths to dll
 * search directories separated by a ":" (colon, same as linux $PATH)
 */
const(char)[] find_dll(const(char)[] path)
{
	version (unittest)
	{
		if (path.ptr == path.ptr) // suppress unreachable code warning
			return path;
	}

	if (FILE* f = fopen(path.ptr, "r"))
	{
		debug(search) printf("search %s: file exists\n", path.ptr);
		fclose(f);
		return path;
	}

	if (path != superbasename(path))
	{
		debug(search) printf("search %s: nonexistent absolute path\n", path.ptr);
		return null;
	}

	string[] dirs;

	if (char* sp = getenv("DDW_DLL_PATH"))
	{
		while (*sp)
		{
			char* nul = strchrnul(sp, ':');
			dirs ~= sp[0..nul-sp].gcdup;
			debug(search) printf("search: add dir %s\n", dirs[$-1].ptr);
			sp = nul+!!*nul;
		}
	}

	if (char* whd = getenv("WINEHOMEDIR"))
	{
		enum pre = `\??\`;
		if (!strncmp(whd, pre.ptr, pre.length))
			whd += pre.length;
		dirs ~= cast(string)gcprintf("%s/.local/lib/winamp", whd);
		dirs ~= cast(string)gcprintf("%s/.local/lib", whd);
	}

	static immutable sufs = ["", ".dll"];
	foreach (dir; dirs)
	{
		foreach (suf; sufs)
		{
			char[] candidate = gcprintf("%s/%s%s", dir.ptr, path.ptr, suf.ptr);
			if (FILE* f = fopen(candidate.ptr, "r"))
			{
				debug(search) printf("search %s: exists: %s\n", path.ptr, candidate.ptr);
				fclose(f);
				return candidate;
			}
			debug(search) printf("search %s: does not exist: %s\n", path.ptr, candidate.ptr);
		}
	}

	debug(search) printf("search %s: not found\n", path.ptr);

	return null;
}

bool includes(const(uint)[] vs, uint v)
{
	foreach (i; 0..vs.length) { if (vs[i] == v) return true; }
	return false;
}

uint[] parse_numseq(const(char)[] s)
{
	uint[] vs;

	const(char)* p = s.ptr;
	while (*p)
	{
		const(char)* pt = p;
		const(char)* nul = strchrnul(p, ',');
		p = nul+!!*nul;

		uint v;
		int n;
		if (sscanf(pt, "%u%n", &v, &n) != 1 || n != nul-pt)
		{
			debug(parse) printf("bad numseq %s\n", pt);
			debug(parse) printf("parsing %.*s\n", nul-pt, pt);
			return null;
		}
		vs ~= v;
	}

	return vs;
}
