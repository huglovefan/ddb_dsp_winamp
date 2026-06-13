#include "plugload.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winbase.h>
#include <windef.h>
#include <winreg.h>
#include "../afmt.h"
#include "ipc_hooks.h"
#include "main.hpp"
#include "misc.hpp"
#include "plugin.hpp"
#include "winamp.hpp"

/* task: there are places in this file using wstring that should
   probably be using wstring_view instead. */

static bool find_dll(
    const std::wstring &path,
    std::wstring       &result_out);
static void apply_defaults(PluginOpts &outp);
static bool parse_numseq(const wchar_t *, std::vector<unsigned int> &);

enum
{
	OPT_UINT,
	OPT_IBOOL,
	OPT_BOOL,
	OPT_NSEQ
};

struct Optdef
{
	std::wstring_view name;
	int               type;
	size_t            offset;
};

static const Optdef optdef[] = {
	{L"procmin",   OPT_UINT,  offsetof(PluginOpts, process_min_frames)},
	{L"procmax",   OPT_UINT,  offsetof(PluginOpts, process_max_frames)},
	{L"procmult",  OPT_UINT,  offsetof(PluginOpts, process_frames_mult)},

	{L"randbuf",   OPT_BOOL,  offsetof(PluginOpts, randbuf)},
	{L"off",       OPT_BOOL,  offsetof(PluginOpts, want_skip_user)},
	{L"nostretch", OPT_BOOL,  offsetof(PluginOpts, nostretch)},
	{L"stretch",   OPT_IBOOL, offsetof(PluginOpts, nostretch)},
	{L"required",  OPT_BOOL,  offsetof(PluginOpts, required)},
	{L"slowipc",   OPT_BOOL,  offsetof(PluginOpts, slowipc)},
	{L"fastipc",   OPT_IBOOL, offsetof(PluginOpts, slowipc)},

	{L"unload",    OPT_BOOL,  offsetof(PluginOpts, init_unload)},

	{L"bits",      OPT_NSEQ,  offsetof(PluginOpts, bits)},
	{L"ch",        OPT_NSEQ,  offsetof(PluginOpts, ch)},
	{L"rate",      OPT_NSEQ,  offsetof(PluginOpts, rate)},
};

static size_t opttype_align(int type)
{
	switch (type)
	{
	case OPT_UINT:  return alignof(unsigned int);
	case OPT_IBOOL: return alignof(bool);
	case OPT_BOOL:  return alignof(bool);
	case OPT_NSEQ:  return alignof(std::vector<unsigned int>);
	default: __builtin_trap();
	}
}

static size_t opttype_size(int type)
{
	switch (type)
	{
	case OPT_UINT:  return sizeof(unsigned int);
	case OPT_IBOOL: return sizeof(bool);
	case OPT_BOOL:  return sizeof(bool);
	case OPT_NSEQ:  return sizeof(std::vector<unsigned int>);
	default: __builtin_trap();
	}
}

__attribute__((constructor))
static void _init_optdef_()
{
	for (const Optdef &opt : optdef)
	{
		assert(opt.offset < sizeof(PluginOpts));
		assert(sizeof(PluginOpts)-opt.offset >= opttype_size(opt.type));
		assert((opt.offset % opttype_align(opt.type)) == 0);
	}
}

static const Optdef *find_option(const std::wstring_view name)
{
	for (const Optdef &opt : optdef)
		if (opt.name == name)
			return &opt;

	return nullptr;
}

static bool parse_and_write_option(
	PluginOpts         &outp,
	const Optdef       &opt,
	const std::wstring &value,
	bool                hasvalue)
{
	void *vp;

	vp = reinterpret_cast<char *>(&outp) + opt.offset;

	switch (opt.type)
	{
	case OPT_IBOOL:
	case OPT_BOOL:
	{
		if (hasvalue)
		{
			unsigned int tmp;
			int n;
			int scanrv;

			/* note: must read to variable first
			   since %hh isn't supported. */
			scanrv = swscanf(
			    value.c_str(),
			    L"%u%n",
			    &tmp,
			    &n);

			if (scanrv != 1 || n != value.size())
				return false;

			*static_cast<bool *>(vp) = !!tmp;
		}
		else
			*static_cast<bool *>(vp) = true;

		if (opt.type == OPT_IBOOL)
			*static_cast<bool *>(vp) =
			    !*static_cast<bool *>(vp);

		return true;
	}

	case OPT_UINT:
	{
		int n;
		int scanrv;

		scanrv = swscanf(
		    value.c_str(),
		    L"%u%n",
		    static_cast<unsigned int *>(vp),
		    &n);

		if (scanrv != 1 || n != value.size())
			return false;

		return true;
	}

	case OPT_NSEQ:
		return parse_numseq(
		    value.c_str(),
		    *static_cast<std::vector<unsigned int> *>(vp));

	default:
		assert(0);
		return false;
	}
}

static bool validate_opts(PluginOpts *opts)
{
	bool ok;

	ok = true;

	if (!opts->process_frames_mult)
	{
		ok = false;
		printf("error: process_frames_mult is zero\n");
	}

	if ((opts->process_min_frames % opts->process_frames_mult) != 0)
	{
		ok = false;
		printf(
		    "error: process_min_frames %u is not a multiple of"
		    " process_frames_mult %u\n",
		    opts->process_min_frames,
		    opts->process_frames_mult);
	}

	if ((opts->process_max_frames % opts->process_frames_mult) != 0)
	{
		ok = false;
		printf(
		    "error: process_max_frames %u is not a multiple of"
		    " process_frames_mult %u\n",
		    opts->process_max_frames,
		    opts->process_frames_mult);
	}

	if (opts->process_max_frames != 0 &&
	    opts->process_min_frames > opts->process_max_frames)
	{
		ok = false;
		printf(
		    "error: process_min_frames %u is greater than"
		    " process_max_frames %u\n",
		    opts->process_min_frames,
		    opts->process_max_frames);
	}

	for (size_t i = 0; i != opts->bits.size(); i++)
	{
		unsigned int val;
		val = opts->bits[i];
		if (val < 8 || val > 32 || (val % 8) != 0)
		{
			ok = false;
			printf("error: bad bits value %u\n", val);
		}
	}

	/* ch and rate:
	   - can't be zero
	   - must fit in signed int for ModifySamples */
	for (size_t i = 0; i != opts->ch.size(); i++)
	{
		unsigned int val;
		val = opts->ch[i];
		if (!val || val > INT_MAX)
		{
			ok = false;
			printf("error: bad ch value %u\n", val);
		}
	}
	for (size_t i = 0; i != opts->rate.size(); i++)
	{
		unsigned int val;
		val = opts->rate[i];
		if (!val || val > INT_MAX)
		{
			ok = false;
			printf("error: bad rate value %u\n", val);
		}
	}

	return ok;
}

bool parse_plugin_options(
	const std::wstring &cmdarg,
	PluginOpts         &opts_out)
{
	PluginOpts outp = {};

	const wchar_t *ptr = cmdarg.c_str();

	while (*ptr)
	{
		std::wstring part;
		std::wstring name;
		std::wstring value;
		wchar_t *eq;
		const Optdef *opt;
		const wchar_t *nextsep;
		bool hasvalue;

		nextsep = strchrnul(const_cast<wchar_t *>(ptr), ':');
		part = std::wstring(ptr, nextsep-ptr);
		ptr = nextsep+!!*nextsep;

		// first option is the dll name
		if (outp.path.empty())
		{
			if (!find_dll(part, outp.path))
			{
				printf(
				    "dll not found: \"%ls\"\n",
				    part.c_str());
				return false;
			}
			outp.dllname = superbasename(
			    const_cast<wchar_t *>(outp.path.c_str()));
			apply_defaults(outp);
			continue;
		}

		// option starting with a digit is the module index
		if (!part.empty() && part[0] >= '0' && part[0] <= '9')
		{
			int n;
			int scanrv;
			scanrv = swscanf(
			    part.c_str(),
			    L"%d%n",
			    &outp.module_idx,
			    &n);
			if (scanrv != 1 || n != part.size())
			{
				printf(
				    "failed to parse"
				    " module index \"%ls\"\n",
				    part.c_str());
				return false;
			}
			continue;
		}

		name = part;
		hasvalue = false;

		eq = wmemchr(part.c_str(), '=', part.size());
		if (eq)
		{
			name =
			    std::wstring(part.data(), eq-part.data());
			value = std::wstring(eq+1);
			hasvalue = true;
		}

		opt = find_option(name);

		if (!opt)
		{
			printf(
			    "unknown option \"%ls\"\n",
			    name.c_str());
			return false;
		}

		if (!parse_and_write_option(
		    outp, *opt, value, hasvalue))
		{
			printf(
			    "failed to parse"
			    " value \"%ls\" for option %ls\n",
			    value.c_str(),
			    name.c_str());
			return false;
		}
	}

	if (outp.path.empty())
		return false;

	if (!validate_opts(&outp))
		return false;

	opts_out = std::move(outp);

	return true;
}

/**
 * loads and initializes the dll of a plugin according to its parsed options
 */
bool load_plugin(Plugin *pl, HWND main_window)
{
	HMODULE                dll;
	winampDSPGetHeaderType get_header;
	winampDSPHeader       *header;
	winampDSPModule       *module;
	int                    init_rv;
	fp_control             fp_save;
	unsigned long err;

	assert(!pl->opts.path.empty());
	assert(!pl->module);

	fp_control_write(&fp_save);

	dll = LoadLibrary(pl->opts.path.c_str());

	if (!dll)
	{
		err = GetLastError();
		fastprintf(
		    "load_plugin: failed to open %ls using LoadLibrary:"
		    " %ls (%lu)\n",
		    pl->opts.dllname.c_str(),
		    StrError(err),
		    err);
		goto err;
	}

	if (!pl->opts.slowipc && !hook_ipc_funcs(dll, &pl->hooks, pl))
		fastprintf(
		    "load_plugin: warning: hook_ipc_funcs failed"
		    " (%ls)\n",
		    pl->opts.dllname.c_str());

	*(FARPROC *)&get_header =
	    GetProcAddress(dll, "winampDSPGetHeader2");

	if (!get_header)
	{
		err = GetLastError();
		fastprintf(
		    "load_plugin: failed to find winampDSPGetHeader2()"
		    " function in %ls: %ls (%lu)\n",
		    pl->opts.dllname.c_str(),
		    StrError(err),
		    err);
		goto err;
	}

	header = get_header(main_window);

	if (!header)
	{
		printf(
		    "load_plugin: winampDSPGetHeader2() returned"
		    " NULL!\n");
		goto err;
	}

	if (header->version != 0x20 && header->version != 0x22)
		fastprintf(
		    "load_plugin: warning: unknown header version 0x%x"
		    " (%ls)\n",
		    header->version,
		    pl->opts.dllname.c_str());

	if (!pl->opts.modules.size())
	for (int i = 0; i < 100; i++)
	{
		module = header->getModule(i);
		if (!module)
			break;
		pl->opts.modules.emplace_back(module->description);
	}

	module = header->getModule(pl->opts.module_idx);

	if (!module)
	{
		printf(
		    "load_plugin: %ls has no module with index %d\n",
		    pl->opts.dllname.c_str(),
		    pl->opts.module_idx);
		goto err;
	}

	module->hDllInstance = dll;

	/* note: ipc_as_hwnd_parent is handled by caller. */
	if (pl->opts.null_as_hwnd_parent)
		module->hwndParent = nullptr;
	else
		module->hwndParent = main_window;

	init_rv = module->Init(module);

	/* dsp.h: "0 on success" */
	if (init_rv)
	{
		printf("load_plugin: Init() failed! (%d)\n", init_rv);
		goto err;
	}

	pl->dll = dll;
	pl->header = header;
	pl->module = module;

	fp_control_write(&pl->fp_main);
	fp_control_read(fp_save);
	memcpy(&pl->fp_process, &pl->fp_main, sizeof(pl->fp_main));

	return true;

err:

	if (dll)
		FreeLibrary(dll);

	fp_control_read(fp_save);

	return false;
}

/* https://stackoverflow.com/a/35350573 */
template <typename Container> 
bool contains(
	const Container                      &container,
	const typename Container::value_type &element) 
{
	for (auto x : container)
		if (x == element)
			return true;

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
const char *plugin_supports_format(const Plugin *pl, const AFMT *fmt)
{
	if (!pl->opts.rate.empty() &&
	    !contains(pl->opts.rate, fmt->rate))
		return "sample rate";

	return nullptr;
}

// -----------------------------------------------------------------------------

/**
 * apply default parameters for some known plugins
 * 
 * these can be overwritten by values specified on the command line
 */
static void apply_defaults(PluginOpts &outp)
{
	assert(!outp.dllname.empty());

	if (outp.dllname == L"dsp_centercut.dll")
	{
		outp.nostretch = true;
		outp.bits = {16,24,32}; // 8 = loud
		outp.process_min_frames = 1;
		outp.process_frames_mult = 1;
		/* gets stuck if you feed it too much input at once. the
		   actual limit is higher (635XX?) but this is already
		   a good amount. (todo: investigate) */
		outp.process_max_frames = 8192;
		/* kWindowSize in the source code. */
		outp.num_input_frames_to_clear_internal_buffers = 8192;
	}
	else if (outp.dllname == L"dsp_freeverb.dll")
	{
		outp.nostretch = true;
		// 8 = doubt it works correctly based on the others
		// 24 = static [forgot details]
		// 32 = notable background hissing
		outp.bits = {16};
		outp.ch = {2};
		/* this plugin *appears* to be fine with different
		   buffer sizes, but its problem is that changes to the
		   size (even in the 1-576 range) can cause memory
		   corruption that's detectable with HeapValidate. */
	}
	else if (outp.dllname == L"dsp_pacemaker.dll")
	{
		outp.bits = {16,24,32}; // 8 = distorts when stretching
		/* only a bit depth change works for this. */
		outp.clears_internal_buffers_on_format_change = 0x1;
	}
	else if (outp.dllname == L"dsp_sps.dll")
	{
		outp.bits = {16}; // only 16 works properly
	}
	else if (outp.dllname == L"dsp_stereo_tool.dll")
	{
		outp.nostretch = true;
		outp.process_min_frames = 1;
		outp.process_max_frames = 0;
		outp.process_frames_mult = 1;

		outp.clears_internal_buffers_on_format_change = 0x4;

		/* bits=8 doesn't work. */
		/* disable s32 too because of the overflow issue. */
		outp.bits = {16,24};
		outp.ch = {1,2};

		/* last tested: version 10.75 in 2026
		   1. only s32 is affected
		   2. only input is affected, clipped output is fine

		   but it's weird. the only time i know 1.0 turns into
		   INT_MIN is when converting it back to int32_t. look
		   at this:

		   >>> int(numpy.float32(1.0) * 0x80000000)
		   2147483648

		   that value doesn't fit in int32_t, so it wraps around
		   to INT32_MIN.

		   point [1] matches - this is only a problem when the
		   destination type is int32_t.

		   but point [2] is the opposite - if stereo tool uses
		   float samples internally, then the conversion above
		   would logically be needed to convert them for output.
		   but somehow, the bug only affects input.
		   */
		outp.workaround_input_int32_overflow = true;
	}
	else if (outp.dllname == L"dsp_tool.dll")
	{
		/* https://winampheritage.com/plugin/dsp-spectrum-tool-version-2/80359
		   */
		/* ignores input with bits other than 16. 8 also works?
		   but you probably want 16 instead. */
		outp.bits = {16};
		outp.process_min_frames  = 1;
		outp.process_max_frames  = 0;
		outp.process_frames_mult = 1;
		outp.nostretch = true;
		outp.ignore_output = true;
	}
	else
	{
		fastprintf(
		    "note: no default settings for unrecognized plugin"
		    " %ls\n",
		    outp.dllname.c_str());
	}
}

static bool is_wow64()
{
	BOOL rv;

	if (!IsWow64Process(GetCurrentProcess(), &rv))
		return false;

	return !!rv;
}

static bool get_winamp_plugins_dir(wchar_t *buf, size_t buflen)
{
	DWORD bufsiz;
	LSTATUS rv;

	assert(buflen <= SIZE_MAX/sizeof(wchar_t));

	bufsiz = (buflen * sizeof(wchar_t));

	rv = RegGetValue(
	    HKEY_CURRENT_USER,
	    L"Software\\Winamp",
	    nullptr,
	    RRF_RT_REG_SZ,
	    nullptr,
	    buf,
	    &bufsiz);

	if (rv != ERROR_SUCCESS)
	{
		const wchar_t *fallback;

		if (rv != ERROR_FILE_NOT_FOUND)
			PrintError("RegGetValue");

		if (is_wow64())
			fallback =
			    L"C:\\Program Files (x86)\\Winamp\\Plugins";
		else
			fallback =
			    L"C:\\Program Files\\Winamp\\Plugins";

		snwprintf(buf, buflen, L"%ls", fallback);
	}
	else
	{
		assert((bufsiz % sizeof(wchar_t)) == 0);
		assert((bufsiz / sizeof(wchar_t)) <= buflen);

		buf += (bufsiz / sizeof(wchar_t));
		buflen -= (bufsiz / sizeof(wchar_t));

		snwprintf(buf, buflen, L"\\Plugins");
	}

	return true;
}

static bool exists(const wchar_t *path)
{
	DWORD attr;
	attr = GetFileAttributes(path);
	if (attr == INVALID_FILE_ATTRIBUTES
	    || (attr & FILE_ATTRIBUTE_DIRECTORY))
		return false;
	return true;
}

/**
 * takes a dll path or name, returns a path to a dll that exists (or null)
 * 
 * if given just the name, the dll is searched for in some directories
 */
static bool find_dll(
	const std::wstring &dllname,
	std::wstring       &path_out)
{
	enum { BUF = MAX_PATH+1 };
	wchar_t buf[BUF];

	if (exists(dllname.c_str()))
	{
		path_out = dllname;
		return true;
	}

	/* if it's a path (not just a bare name), don't do any of the
	   search steps below. */
	if (dllname.c_str()
	    != superbasename(const_cast<wchar_t *>(dllname.c_str())))
		return false;

	const auto test_dir =
	    [&dllname, &path_out](const std::wstring_view dir)
	{
		static const std::wstring_view sufs[] = {L"", L".dll"};

		for (const std::wstring_view suf : sufs)
		{
			std::wstring candidate{};
			candidate += dir;
			candidate += L"/";
			candidate += dllname;
			candidate += suf;
			if (exists(candidate.c_str()))
			{
				path_out = std::move(candidate);
				return true;
			}
		}

		return false;
	};

	if (const wchar_t *ptr = _wgetenv(L"WINEHOMEDIR"))
	{
		std::wstring_view pre = L"\\??\\";

		/* wine adds this, but it breaks the existence check. */
		if (!wcsncmp(ptr, pre.data(), pre.size()))
			ptr += pre.size();

		std::wstring homedir = std::wstring(ptr);

		if (test_dir(homedir+L"/.local/lib/winamp") ||
		    test_dir(homedir+L"/.local/lib"))
			return true;
	}

	if (get_winamp_plugins_dir(buf, BUF))
		if (test_dir(buf))
			return true;

	return false;
}

static bool parse_numseq(
	const wchar_t             *str,
	std::vector<unsigned int> &ints_out)
{
	std::vector<unsigned int> ints;
	const wchar_t *ptr;

	ptr = str;

	/* empty string */
	if (!*ptr)
		return false;

	while (*ptr)
	{
		const wchar_t *start;
		const wchar_t *end;
		unsigned int val;
		int n;
		int rv;

		start = ptr;
		end = strchrnul(const_cast<wchar_t *>(ptr), ',');
		ptr = end+!!*end;

		/* task: make this check for overflow */

		rv = swscanf(start, L"%u%n", &val, &n);
		if (rv != 1 || n != end-start)
			return false;

		ints.push_back(val);
	}

	ints_out = std::move(ints);

	return true;
}
