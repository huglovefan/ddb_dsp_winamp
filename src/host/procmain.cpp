#include "procmain.hpp"

#include "../pipedata.h"
#include "misc.hpp"
#include "plugload.hpp"
#include "plugproc.hpp"
#include <map>
#include <windows.h>
#include <deque>
#include "../conv.h"
#include "../io.h"
#include "../crc32.h"
#include <math.h>
#include <inttypes.h>
#include <heapapi.h>
#include "plugconv.hpp"
#include "plugrest.hpp"

struct PlaybackTimeCounter
{
	std::map<unsigned int, size_t> partials;
	unsigned long long             whole_sec;

	void add(unsigned int rate, size_t frames)
	{
		size_t *part;
		size_t wholes;

		assert(rate);
		part = &partials[rate];
		*part += frames;
		wholes = *part / rate;
		if (wholes)
		{
			whole_sec += wholes;
			*part -= wholes * rate;
		}
	}

	double get()
	{
		decltype(partials)::const_iterator iter;
		double rv;

		rv = (double)whole_sec;

		for (
		    iter = partials.cbegin();
		    iter != partials.cend();
		    ++iter)
		{
			unsigned int rate;
			size_t count;

			rate = iter->first;
			count = iter->second;

			rv += (double)count / (double)rate;
		}

		return rv;
	}
};

struct TimeGetter
{
	double freq;

	double get_time()
	{
		LARGE_INTEGER counter;

		if (isnan(freq)) [[unlikely]]
		{
			LARGE_INTEGER tmp;

			if (QueryPerformanceFrequency(&tmp)) [[likely]]
				freq = (double)tmp.QuadPart;
		}

		if (!QueryPerformanceCounter(&counter)) [[unlikely]]
			return NAN;

		return (double)counter.QuadPart / freq;
	}
};

#define TIME_GETTER_INIT ((struct TimeGetter){NAN})

static constexpr double iter_stat_max_age_sec = 1.5;
#define iter_stat_max_count 100

/* stats from one loop iteration. */
struct iter_stats
{
	double add_tm;
	AFMT   fmt;
	size_t frames_in;
	size_t frames_out;
	size_t samples_clip_hi;
	size_t samples_clip_lo;
	double process_dur_sec;
};

static struct {
	SRWLOCK                    lock;
	struct PlaybackTimeCounter plt_in;
	struct PlaybackTimeCounter plt_out;
	bool                       reset_times;
	AFMT                       last_fmt;
	std::deque<iter_stats>     iter;
	LARGE_INTEGER              updated;
} g_stats = {
	.lock = SRWLOCK_INIT,
};

/*
Check if the stats were updated since this time.
*/
bool process_stats_updated_since(LARGE_INTEGER *val)
{
	bool rv;
	AcquireSRWLockShared(&g_stats.lock);
	rv = false;
	if (memcmp(val, &g_stats.updated, sizeof(*val)))
		rv = true;
	ReleaseSRWLockShared(&g_stats.lock);
	return rv;
}

/*
Get a copy of the current process stats.
*/
size_t process_get_iter_stats(process_stats_iter_sub *is, size_t max)
{
	size_t cnt;
	AcquireSRWLockShared(&g_stats.lock);
	cnt = 0;
	for (iter_stats &i : g_stats.iter)
	{
		if (!max--)
			break;
		is[cnt].fmt             = i.fmt;
		is[cnt].frames_in       = i.frames_in;
		is[cnt].frames_out      = i.frames_out;
		is[cnt].samples_clip_hi = i.samples_clip_hi;
		is[cnt].samples_clip_lo = i.samples_clip_lo;
		is[cnt].process_dur_sec = i.process_dur_sec;
		cnt++;
	}
	ReleaseSRWLockShared(&g_stats.lock);
	return cnt;
}

/*
Record stats from one run of the iter function.
*/
static void record_iter_time(
	double start_tm,
	double end_tm,
	const AFMT *fmt,
	size_t frames_in,
	size_t frames_out,
	size_t samples_clip_hi,
	size_t samples_clip_lo)
{
	AcquireSRWLockExclusive(&g_stats.lock);
	while (g_stats.iter.size() >= iter_stat_max_count)
		g_stats.iter.pop_back();
	g_stats.iter.push_front(iter_stats{
		add_tm:          end_tm,
		fmt:             *fmt,
		frames_in:        frames_in,
		frames_out:       frames_out,
		samples_clip_hi:  samples_clip_hi,
		samples_clip_lo:  samples_clip_lo,
		process_dur_sec: (end_tm-start_tm),
	});
	while (!g_stats.iter.empty())
	{
		double age_sec;
		age_sec = end_tm-g_stats.iter.back().add_tm;
		if (age_sec > iter_stat_max_age_sec)
			g_stats.iter.pop_back();
		else
			break;
	}
	QueryPerformanceCounter(&g_stats.updated);
	ReleaseSRWLockExclusive(&g_stats.lock);
}

/*
Record a count of input samples.
*/
static void record_plt_in(unsigned int rate, size_t frames)
{
	AcquireSRWLockExclusive(&g_stats.lock);
	g_stats.plt_in.add(rate, frames);
	ReleaseSRWLockExclusive(&g_stats.lock);
}

/*
Record a count of output samples.
*/
static void record_plt_out(unsigned int rate, size_t frames)
{
	AcquireSRWLockExclusive(&g_stats.lock);
	g_stats.plt_out.add(rate, frames);
	if (g_stats.reset_times)
	{
		g_stats.plt_in = {};
		g_stats.plt_out = {};
		g_stats.reset_times = false;
	}
	ReleaseSRWLockExclusive(&g_stats.lock);
}

/*
Record a new input format.
*/
static void record_fmt(const AFMT *fmt)
{
	AcquireSRWLockExclusive(&g_stats.lock);
	g_stats.last_fmt = *fmt;
	ReleaseSRWLockExclusive(&g_stats.lock);
}

/*
Get the flat stats struct.
*/
bool process_get_stats(process_stats *stats_out)
{
	AcquireSRWLockShared(&g_stats.lock);
	stats_out->input_dur_sec = g_stats.plt_in.get();
	stats_out->output_dur_sec = g_stats.plt_out.get();
	stats_out->last_fmt = g_stats.last_fmt;
	stats_out->updated = g_stats.updated;
	ReleaseSRWLockShared(&g_stats.lock);
	return true;
}

/*
Reset the playback time stats.
*/
void process_reset_time_stats()
{
	AcquireSRWLockExclusive(&g_stats.lock);
	g_stats.reset_times = true;
	ReleaseSRWLockExclusive(&g_stats.lock);
}

/*
check plugin compatibility on format change
*/
static bool check_compatible(Plugin *pl, const AFMT *fmt)
{
	const char *reason;

	reason = plugin_supports_format(pl, fmt);

	pl->skip_fmt = (reason != nullptr);

	if (pl->skip_fmt)
	{
		if (pl->opts.required)
		{
			fastprintf(
			    "error: required plugin %ls doesn't support"
			    " this %s, exiting\n",
			    pl->opts.dllname.c_str(),
			    reason);

			return false;
		}

		fastprintf(
		    "warning: %ls doesn't support this %s,"
		    " disabling it\n",
		    pl->opts.dllname.c_str(),
		    reason);
	}

	return true;
}

/*
process a format change

returns true if everything is ok, false if execution should be halted (a
plugin with the "required" flag doesn't support the format)
*/
static bool process_format_change(
	struct plugin_list *plugins,
	const AFMT         *fmt)
{
	char nb[AFMT_STRBUF];
	Plugin *pl;

	afmt_tostring(nb, sizeof(nb), fmt);

	fastprintf("format change: %s\n", nb);

	PLUGIN_LIST_FOREACH(pl, plugins)
	{
		/* if we can't convert between these formats, drop the
		   buffered data. */
		if (pl->buf.datasz()
		    && !can_convert_fmts(&pl->buf_fmt, fmt))
		{
			size_t frames;

			frames = afmt_buf_frames(
			    &pl->buf_fmt, pl->buf.datasz());
			fastprintf(
			    "discarding %zu buffered frames due to"
			    " format change (%ls)\n",
			    frames,
			    pl->opts.dllname.c_str());
			buf_clear(&pl->buf);
		}

		// re-check compatibility
		if (!check_compatible(pl, fmt))
			return false;
	}

	return true;
}

/*
check that the format and buffer size match and aren't random data
*/
static bool check_read_params(const AFMT &fmt, uint64_t buffer_size)
{
	char nb[AFMT_STRBUF];
	bool ok;

	ok = true;

	if (!afmt_is_valid(&fmt))
	{
		afmt_tostring(nb, sizeof(nb), &fmt);
		fastprintf(
		    "error: read nonsensical input format: %s\n",
		    nb);
		ok = false;
	}

	if (buffer_size > SIZE_MAX)
	{
		fastprintf(
		    "error: input data size %llu doesn't fit in"
		    " size_t\n",
		    buffer_size);
		ok = false;
	}

	if (afmt_is_valid(&fmt)
	    && (buffer_size % afmt_frame_bytes(&fmt)) != 0)
	{
		fastprintf(
		    "error: input data size %llu is not a multiple of"
		    " frame size %zu\n",
		    buffer_size,
		    afmt_frame_bytes(&fmt));
		ok = false;
	}

	return ok;
}

/*
Get the total number of frames that the list of active plugins might
prepend to the input buffer.
*/
static size_t required_reserve_frames(struct plugin_list *plugins)
{
	Plugin *pl;
	size_t total;

	total = 0;
	PLUGIN_LIST_FOREACH(pl, plugins)
		if (!pl->skip_fmt && pl->buf.datasz())
			total += afmt_buf_frames(
			    &pl->buf_fmt,
			    pl->buf.datasz());

	return total;
}

static void afmt_from_request(
	AFMT                                   *fmt,
	const struct plug2host_process_samples *req)
{
	fmt->sfmt = req->sampleformat_in;
	fmt->ch   = req->channels;
	fmt->rate = req->samplerate;
}

struct iter_vars
{
	struct plugin_list *plugins;
	bool                plugins_locked;
	bool                normal_exit;
	AFMT                lastfmt;
	Buf                 data;
	Buf                 tmp;
	/* holds output that couldn't be written last time because of
	   the limit on output size. */
	Buf                 pending_output;
	AFMT                pending_output_fmt;
	struct TimeGetter   tg;
	/* io */
	int                 _in_fd;
	int                 out_fd;
	struct io_reader    r;
	struct io_readparse rp;
};

#define ITER_VARS_INIT \
	((struct iter_vars){ \
		.lastfmt = AFMT_INVALID, \
		.pending_output_fmt = AFMT_INVALID, \
		.tg = TIME_GETTER_INIT, \
		._in_fd = -1, \
		.out_fd = -1, \
		.r = IO_READER_INIT, \
	})

static uint32_t crc32_bytes_data(
	size_t       nbytes,
	struct Buf **bufs,
	size_t       nbufs)
{
	struct Buf *buf;
	size_t i;
	size_t use;
	uint32_t crc;

	crc = CRC32_INIT;
	for (i = 0; i != nbufs && nbytes; i++)
	{
		buf = bufs[i];

		use = buf->datasz();
		if (!use)
			continue;
		if (use > nbytes)
			use = nbytes;

		assert(use <= buf->datasz());
		crc = crc32p(buf->datap(), use, crc);
		assert(use <= nbytes);
		nbytes -= use;
	}
	assert(!nbytes);

	return crc32p_end(crc);
}

static bool write_bytes_data(
	int          fd,
	size_t       nbytes,
	struct Buf **bufs,
	size_t       nbufs)
{
	struct Buf *buf;
	size_t i;
	size_t use;

	for (i = 0; i != nbufs && nbytes; i++)
	{
		buf = bufs[i];

		use = buf->datasz();
		if (!use)
			continue;
		if (use > nbytes)
			use = nbytes;

		assert(use <= buf->datasz());
		if (!write_full(fd, buf->datap(), use))
		{
			perror("write");
			return false;
		}

		if (use == buf->datasz())
			buf_clear(buf);
		else
			buf_shift_data_into_reserved(buf, use);

		assert(use <= nbytes);
		nbytes -= use;
	}
	assert(!nbytes);

	return true;
}

static bool iter_process_samples(iter_vars *vars)
{
	struct plug2host_process_samples req;
	struct host2plug_process_samples_response res;
	AFMT curfmt;
	AFMT infmt;
	double start_tm;
	size_t restotal;
	uint32_t hdrcksum;
	uint32_t datacksum;
	bool ok;

	/*
	 * [1/7] read header
	 */

	if (!io_r_read_consume(&vars->r, (char *)&req, sizeof(req)))
	{
		if (errno)
			perror("io_rp_read");
		else
			fastprintf(
			    "io_rp_read: unexpected end of input\n");

		return false;
	}

	assert(req.code == P2H_PROCESS_SAMPLES);

	hdrcksum = req.hdrcksum;
	datacksum = req.datacksum;

	req.hdrcksum = 0;

	if (hdrcksum != crc32(&req, sizeof(req)))
	{
		fastprintf(
		    "header crc error: struct says %08x, calculated"
		    " %08x\n",
		    hdrcksum,
		    crc32(&req, sizeof(req)));
		assert(0);
	}

	/* mild todo: this break somewhere later. */
	assert(req.buffer_size);

	/*
	 * [2/7] make format, check and verify it
	 */

	afmt_from_request(&curfmt, &req);
	infmt = curfmt;

	if (!check_read_params(curfmt, req.buffer_size))
		return false;

	assert(!vars->plugins_locked);
	PLUGIN_LIST_LOCK_RDONLY(vars->plugins);
	vars->plugins_locked = true;

	if (!afmt_equal(&curfmt, &vars->lastfmt))
	{
		if (!process_format_change(vars->plugins, &curfmt))
			return false;

		if (vars->pending_output.datasz()
		    && !can_convert_fmts(&vars->pending_output_fmt, &curfmt))
		{
			buf_clear(&vars->pending_output);
			vars->pending_output_fmt = AFMT_INVALID;
		}

		vars->lastfmt = curfmt;
	}

	record_plt_in(
	    curfmt.rate,
	    afmt_buf_frames(&curfmt, req.buffer_size));

	record_fmt(&curfmt);

	start_tm = vars->tg.get_time();

	/*
	 * [3/7] prepare buffer
	 */

	if (!HeapValidate(GetProcessHeap(), 0, nullptr))
	{
		fastprintf("HeapValidate failed at %s(%d)\n", __FILE__, __LINE__);
		__builtin_trap();
	}

	restotal = afmt_frame_bytes_n(
	    &curfmt,
	    required_reserve_frames(vars->plugins));

	buf_clear(&vars->data);
	buf_clear(&vars->tmp);

	buf_init_with_reserved_and_capacity(
	    &vars->data,
	    restotal,
	    req.buffer_size);

	/*
	 * [4/7] read data
	 */

	ok = io_r_read_consume(
	    &vars->r,
	    vars->data.datap(),
	    req.buffer_size);

	if (!ok)
	{
		if (errno)
			perror("io_rp_read");
		else
			fastprintf(
			    "io_rp_read: unexpected end of input\n");

		return false;
	}

	if (datacksum != crc32(vars->data.datap(), req.buffer_size))
		assert(0);

	buf_register_append(
	    &vars->data,
	    req.buffer_size);

	/*
	 * [5/7] process!
	 */

	if (!HeapValidate(GetProcessHeap(), 0, nullptr))
	{
		fastprintf("HeapValidate failed at %s(%d)\n", __FILE__, __LINE__);
		__builtin_trap();
	}

	plugin_process_all(
	    vars->plugins,
	    curfmt,
	    vars->data,
	    vars->tmp);

	if (!HeapValidate(GetProcessHeap(), 0, nullptr))
	{
		fastprintf("HeapValidate failed at %s(%d)\n", __FILE__, __LINE__);
		__builtin_trap();
	}

	assert(vars->plugins_locked);
	PLUGIN_LIST_UNLOCK_RDONLY(vars->plugins);
	vars->plugins_locked = false;

	/* process might set AFMT_INVALID if the buffer became empty. */
	if (!vars->data.datasz() && !afmt_is_valid(&curfmt))
		afmt_from_request(&curfmt, &req);

	/**/
	{
		AFMT tmpfmt;

		tmpfmt = curfmt;
		switch (req.sampleformat_request)
		{
		case SFMT_REQ_ANY:
			break;
		case SFMT_REQ_ANY_32BIT:
			/* if it's not f32, it's an int format - convert
			   all of those to s32. */
			if (tmpfmt.sfmt != SFMT_F32)
				tmpfmt.sfmt = SFMT_S32;
			break;
		case SFMT_REQ_ONLY_F32:
			tmpfmt.sfmt = SFMT_F32;
			break;
		case SFMT_REQ_ONLY_S32:
			tmpfmt.sfmt = SFMT_S32;
			break;
		default:
			fastprintf(
			    "error: unknown sampleformat_request enum"
			    " value\n");
			return false;
		}

		/* skip if empty or face the assert. */
		if (vars->pending_output.datasz())
		{
			ok = buf_convert_audio(
			    &vars->pending_output,
			    &vars->tmp,
			    &vars->pending_output_fmt,
			    &tmpfmt);
			if (!ok)
			{
				fastprintf(
				    "error: failed to convert audio for"
				    " output\n");
				return false;
			}
			vars->pending_output_fmt = tmpfmt;
		}

		/* skip if empty or face the assert. */
		if (vars->data.datasz())
		{
			ok = buf_convert_audio(
			    &vars->data,
			    &vars->tmp,
			    &curfmt,
			    &tmpfmt);
			if (!ok)
			{
				fastprintf(
				    "error: failed to convert audio for"
				    " output\n");
				return false;
			}
		}

		curfmt = tmpfmt;
	}

	size_t out_frames_avail =
	    afmt_buf_frames(&curfmt, vars->pending_output.datasz())
	    + afmt_buf_frames(&curfmt, vars->data.datasz());

	size_t out_frames_max =
	    afmt_buf_frames_round_down(&curfmt, req.response_max_bytes);

	size_t out_frames = out_frames_avail;
	if (out_frames > out_frames_max)
		out_frames = out_frames_max;

	/*
	 * [6/7] write header
	 */

	memset(&res, 0, sizeof(res));
	res.code = H2P_PROCESS_SAMPLES_RESPONSE;
	res.buffer_size = afmt_frame_bytes_n(&curfmt, out_frames);
	res.channels = curfmt.ch;
	res.sampleformat = curfmt.sfmt;
	res.samplerate = curfmt.rate;

	if (!out_frames)
		res.datacksum = crc32p_end(CRC32_INIT);
	else
	{
		struct Buf *bufs[] = {
			&vars->pending_output,
			&vars->data,
		};
		res.datacksum =
		    crc32_bytes_data(res.buffer_size, bufs, 2);
	}
	res.hdrcksum = crc32(&res, sizeof(res));

	record_plt_out(
	    curfmt.rate,
	    afmt_buf_frames(&curfmt, res.buffer_size));

	record_iter_time(
	    start_tm,
	    vars->tg.get_time(),
	    &curfmt,
	    afmt_buf_frames(&infmt, req.buffer_size),
	    afmt_buf_frames(&curfmt, res.buffer_size),
	    0,
	    0);

	if (!write_full(vars->out_fd, &res, sizeof(res)))
	{
		perror("write");
		return false;
	}

	/*
	 * [7/7] write data
	 */

	if (res.buffer_size)
	{
		struct Buf *bufs[] = {
			&vars->pending_output,
			&vars->data,
		};
		if (!write_bytes_data(vars->out_fd, res.buffer_size, bufs, 2))
			return false;
	}

	buf_clear(&vars->tmp);
	buf_append_buf(&vars->tmp, &vars->pending_output);
	buf_append_buf(&vars->tmp, &vars->data);
	buf_swap(&vars->tmp, &vars->pending_output);
	buf_clear(&vars->tmp);
	buf_clear(&vars->data);

	if (!vars->pending_output.datasz())
		vars->pending_output_fmt = AFMT_INVALID;
	else
	{
		size_t frames;
		frames = afmt_buf_frames(
		    &curfmt, vars->pending_output.datasz());
		fastprintf(
		    "note: buffered %zu output frames due to size"
		    " limit\n",
		    frames);
		vars->pending_output_fmt = curfmt;
	}

	return true;
}

static bool iter_playback_interrupted(struct iter_vars *vars)
{
	struct Plugin *pl;

	fastprintf(
	   "ddw_host: playback interrupted, resetting buffers\n");

	assert(!vars->plugins_locked);
	PLUGIN_LIST_LOCK_RDONLY(vars->plugins);
	vars->plugins_locked = true;

	PLUGIN_LIST_FOREACH(pl, vars->plugins)
	{
		size_t frames;

		if (pl->buf.datasz())
		{
			frames = afmt_buf_frames(
			    &pl->buf_fmt, pl->buf.datasz());
			fastprintf(
			    "discarding %zu buffered frames due to"
			    " playback interruption (%ls)\n",
			    frames,
			    pl->opts.dllname.c_str());
			buf_clear(&pl->buf);
			pl->buf_fmt = AFMT_INVALID;
		}

		plugin_process_reset(pl, &vars->tmp);
	}

	assert(vars->plugins_locked);
	PLUGIN_LIST_UNLOCK_RDONLY(vars->plugins);
	vars->plugins_locked = false;

	buf_clear(&vars->pending_output);
	vars->pending_output_fmt = AFMT_INVALID;

	return true;
}

static bool iter(struct iter_vars *vars)
{
	int32_t code;

	io_r_maybe_compact(&vars->r);

	if (!io_r_read_retain(&vars->r, &code, sizeof(code)))
	{
		if (io_r_has_eos(&vars->r) && !io_r_has_error(&vars->r))
		{
			fastprintf("ddw_host: proc thread got EOF\n");
			vars->normal_exit = true;
		}
		return false;
	}

	switch (code)
	{
	case P2H_PROCESS_SAMPLES:
		return iter_process_samples(vars);
	case P2H_PLAYBACK_INTERRUPTED:
		io_r_consume(&vars->r, sizeof(code));
		return iter_playback_interrupted(vars);
	default:
		fastprintf(
		    "error: unknown message code %" PRIu32 "\n",
		    code);
		return false;
	}
}

static int process_thread_main(process_thread_vars *tvars)
{
	struct iter_vars vars;

	vars = ITER_VARS_INIT;
	vars.plugins = tvars->plugins;
	vars._in_fd   = tvars->in_fd;
	vars.out_fd  = tvars->out_fd;
	vars.r.fd = tvars->in_fd;
	vars.rp.r = &vars.r;

	while (iter(&vars))
		continue;

	if (vars.plugins_locked)
	{
		PLUGIN_LIST_UNLOCK_RDONLY(vars.plugins);
		vars.plugins_locked = false;
	}

	buf_free(&vars.data);
	buf_free(&vars.tmp);
	buf_free(&vars.pending_output);
	io_r_free_buf(&vars.r);

	fastprintf(
	    "ddw_host: proc thread will now exit, telling main to do"
	    " the same\n");

	main_request_exit(tvars->hwnd_main);

	if (!vars.normal_exit)
		return 1;

	return 0;
}

WINAPI unsigned long process_thread_entry(void *ud)
{
	process_thread_vars *vars;
	int rv;

	vars = (process_thread_vars *)ud;
	rv = process_thread_main(vars);
	delete vars;
	return rv;
}
