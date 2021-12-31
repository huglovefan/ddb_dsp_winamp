module ddw.host.plugload;

import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;
import core.sys.windows.winbase;
import core.sys.windows.windef;

import std.algorithm.iteration : splitter;
import std.algorithm.searching : canFind, startsWith;
import std.conv : ConvException, to;
import std.stdio : writefln;
import std.string : fromStringz, indexOf, toStringz;

import ddw.host.fmt;
import ddw.host.main;
import ddw.host.misc;
import ddw.host.plugin;
import ddw.host.winamp;

//debug = forceSafeBufferSizes;
//debug = printParsedOptions;

/**
 * apply default parameters for some known plugins
 * 
 * these can be overwritten by values specified on the command line
 */
void apply_defaults(string path, PluginOpts* outp)
{
	string dllname = superbasename(path);

	switch (dllname)
	{
		case "dsp_centercut.dll":
			outp.doconf = 0;
			outp.bits = "16,24,32"; // 8 = loud
			break;

		case "dsp_freeverb.dll":
			outp.may_stretch = false;
			outp.bits = "8,16,32"; // 24 = static. probably only really works with 16
			break;

		case "dsp_pacemaker.dll":
			outp.doconf = 0;
			outp.bits = "16,24,32"; // 8 = distorts when stretching
			break;

		case "dsp_sps.dll":
			outp.bits = "16"; // only 16 works properly
			break;

		case "dsp_stereo_tool.dll":
			outp.may_stretch = false;
			outp.bits = "16,24,32"; // 8 = loud
			outp.required = true;
			break;

		default:
			break;
	}
}

/**
 * parse a command-line argument containing the dll path and some parameters
 */
bool parse_plugin_options(string s, PluginOpts* outp)
{
	bool last;

	struct Option
	{
		string name;
		enum Type
		{
			OPT_UINT,
			OPT_BOOL,
			OPT_DSTR,
		}
		Type type;
		union Value
		{
			int* i;
			uint* u;
			string* D;
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
		{"rate", Option.Type.OPT_DSTR, {D: &outp.rate}},
		{"bits", Option.Type.OPT_DSTR, {D: &outp.bits}},
		{"ch", Option.Type.OPT_DSTR, {D: &outp.ch}},
	];

	*outp = PluginOpts.init;

	foreach (part; s.splitter(':'))
	{
		if (outp.path == null)
		{
			outp.path = part;
			apply_defaults(part, outp);
			goto next;
		}

		// "goto skips declaration of (blah blah blah...)"
		if (0)
		{
next:
			continue;
		}

		string name, value;
		uint eq = part.indexOf('=');
		if (eq != -1)
		{
			name = part[0..eq];
			value = part[eq+1..$];
		}
		else
		{
			name = part;
		}

		if (part[0] >= '0' && part[0] <= '9')
		{
			try
			{
				outp.module_idx = part.to!int;
				continue;
			}
			catch (ConvException)
			{
			}
		}

		foreach (ref opt; options)
		{
			size_t prefixlen;
			if (opt.name == name)
				goto match;
			if (opt.type == Option.Type.OPT_BOOL)
			{
				if (startsWith(name, "do") && name[2..$] == opt.name)
					{ prefixlen = 2; goto match; }
				if (startsWith(name, "no") && name[2..$] == opt.name)
					{ prefixlen = 2; goto match; }
				if (startsWith(name, "not") && name[3..$] == opt.name)
					{ prefixlen = 3; goto match; }
			}
			continue;
match:
			switch (opt.type)
			{
			case Option.Type.OPT_UINT:
				try
				{
					*opt.v.u = value.to!uint;
				}
				catch (ConvException e)
				{
					writefln("failed to parse value \"%s\" for option \"%s\"", value, name);
					goto err;
				}
				break;
			case Option.Type.OPT_BOOL:
				try
				{
					*opt.v.i = value.length > 0 ? value.to!int : 1;
				}
				catch (ConvException e)
				{
					writefln("failed to parse value \"%s\" for option \"%s\"", value, name);
					goto err;
				}
				if (prefixlen != 0 && name[0] == 'n') // negated // <-- won't this bug out when the real name start with n?
					*opt.v.i = !*opt.v.i;
				break;
			case Option.Type.OPT_DSTR:
				*opt.v.D = value;
				break;
			default:
				assert(0, "option has invalid type");
			}
			goto next;
		}

		if (name == "safemode")
		{
			outp.process_min_frames = 576;
			outp.process_max_frames = 576;
			outp.process_frames_mult = 576;
			outp.may_stretch = 1;
			goto next;
		}

		writefln("unrecognized option \"%s\"", part);
		goto err;
	}

	if (outp.path == null || outp.path[0] == '\0')
		goto err;

	debug (forceSafeBufferSizes)
	{
		outp.process_min_frames = 576;
		outp.process_max_frames = 576;
		outp.process_frames_mult = 576;
	}

	debug (printParsedOptions)
	{
		writefln("%s:", superbasename(outp.path).fromStringz);
		writefln("  module_idx=%d", outp.module_idx);

		foreach (ref opt; options)
		{
			switch (opt.type)
			{
				case Option.Type.OPT_UINT:
					writefln("  %s=%u", opt.name, *opt.v.u);
					break;
				case Option.Type.OPT_BOOL:
					writefln("  %s=%d", opt.name, *opt.v.i);
					break;
				case Option.Type.OPT_DSTR:
					writefln("  %s=\"%s\"", opt.name, *opt.v.D);
					break;
				default:
					assert(0, "option has invalid type");
			}
		}
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
	outp.path = null;
	return false;
}

unittest
{
	PluginOpts opts;

	assert(parse_plugin_options("dsp_stereo_tool.dll", &opts));
	assert(opts.path.fromStringz == "dsp_stereo_tool.dll");
	assert(opts.module_idx == MODULE_IDX_DEFAULT);
	assert(opts.required); // apply_defaults()

	assert(parse_plugin_options("dsp_unknown.dll:1", &opts));
	assert(opts.path.fromStringz == "dsp_unknown.dll");
	assert(opts.module_idx == 1);
	assert(!opts.required);

	assert(parse_plugin_options("dsp_unknown.dll:2:required", &opts));
	assert(opts.path.fromStringz == "dsp_unknown.dll");
	assert(opts.module_idx == 2);
	assert(opts.required);

	assert(parse_plugin_options(
		"dsp_unknown.dll:9:pmf=10:pMf=20:pfm=5:stretch:nostretch:dostretch:stretch=0:stretch=1:conf:noconf:doconf:conf=0:conf=1:required:notrequired:required=0:required=1:trace:notrace:trace=0:trace=1:rate=8000,44100:bits=8,24:ch=1,2", &opts));
	assert(opts.path.fromStringz == "dsp_unknown.dll");
	assert(opts.module_idx == 9);
	assert(opts.process_min_frames == 10);
	assert(opts.process_max_frames == 20);
	assert(opts.process_frames_mult == 5);
	assert(opts.may_stretch);
	assert(opts.doconf);
	assert(opts.required);
	assert(opts.trace);
	assert(opts.rate == "8000,44100");
	assert(opts.bits == "8,24");
	assert(opts.ch == "1,2");
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

	dll = LoadLibraryA(pl.opts.path.toStringz);
	if (dll == null)
	{
		writefln("load_plugin: failed to open %s using LoadLibrary: %s",
			superbasename(pl.opts.path),
			StrError(GetLastError()).fromStringz);
		goto err;
	}

	get_header = cast(winampDSPGetHeaderType)GetProcAddress(dll, "winampDSPGetHeader2");
	if (get_header == null)
	{
		writefln("load_plugin: failed to get winampDSPGetHeader2() from %s: %s",
			superbasename(pl.opts.path),
			StrError(GetLastError()).fromStringz);
		goto err;
	}

	header = get_header();
	if (header == null)
	{
		writefln("load_plugin: winampDSPGetHeader2() returned NULL!");
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
			writefln("load_plugin: %s has no module with index %d",
				superbasename(pl.opts.path),
				pl.opts.module_idx);
		else
			writefln("load_plugin: %s has no module with index 0 or 1!",
				superbasename(pl.opts.path));
		goto err;
	}

	module_.hDllInstance = dll;
	module_.hwndParent = globals.mainwin;

	// DSP.H: "0 on success"
	init_rv = module_.Init(module_);
	if (init_rv != 0)
	{
		writefln("load_plugin: Init() failed! (%d)", init_rv);
		goto err;
	}

	writefln("%s: %s", superbasename(pl.opts.path), header.description.fromStringz);
	writefln("%s:%d: %s", superbasename(pl.opts.path), pl.opts.module_idx, module_.description.fromStringz);

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

bool match_string(string spec, string value) pure
{
	return spec.splitter(',').canFind(value);
}

/**
 * check if a plugin supports a format given its parameters
 * 
 * if the format isn't supported, returns a static string containing the name of
 *  the incompatible property
 * 
 * if the format is supported, returns null
 */
string plugin_supports_format(const(Plugin)* pl, const(Fmt)* fmt) pure
{
	string ratestr;
	string bitstr;
	string chstr;

	ratestr = fmt.rate.to!string;
	bitstr  = fmt.bps.to!string;
	chstr   = fmt.ch.to!string;

	if (pl.opts.rate != null && !match_string(pl.opts.rate, ratestr))
		return "sample rate";

	if (pl.opts.bits != null && !match_string(pl.opts.bits, bitstr))
		return "bit depth";

	if (pl.opts.ch != null && !match_string(pl.opts.ch, chstr))
		return "channel count";

	return null;
}
