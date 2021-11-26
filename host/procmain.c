#include "procmain.h"

#include <stdio.h>

#include "../plugin/ddw.h"

#include "macros.h"
#include "main.h"
#include "misc.h"

__attribute__((cold))
static bool
fmtchange(const struct fmt *fmt);

DWORD WINAPI
process_thread_main(void *ud)
{
	struct buf data = {0};
	struct buf tmp = {0};
	struct fmt lastfmt = {0};
	int thread_rv = 0;
	(void)ud;

	for (;;) {
		struct processing_request req;
		struct processing_response res;
		struct fmt fmt = {0};
		size_t restotal;

		if U (!read_full(in_fd, &req, sizeof(req))) {
			if U (errno != 0)
				goto readerr;

			// pipe closed
			break;
		}

		fmt = (struct fmt){
			.rate = req.samplerate,
			.bps = req.bitspersample,
			.ch = req.channels,
		};

		assert(fmt_makes_sense(&fmt));
		assert(req.buffer_size % fmt_frame_size(&fmt) == 0);

		if U (!fmt_same(&fmt, &lastfmt)) {
			if U (!fmtchange(&fmt))
				goto err;

			lastfmt = fmt;
		}

		restotal = 0;
		for (unsigned int i = 0; i < plugins_cnt; i++) {
			if (!plugins[i].skip)
				restotal += plugins[i].buf.sz;
		}

		buf_clear(&data);
		buf_prepare_append(&data, restotal+(size_t)req.buffer_size);
		buf_set_reserved(&data, restotal);

		if U (!read_full(in_fd, data.p, (size_t)req.buffer_size))
			goto readerr;

		buf_register_append(&data, (size_t)req.buffer_size);

		plugin_process_all(&fmt, &data, &tmp);

		res = (struct processing_response){
			.buffer_size = data.sz,
		};
		if U (!write_full(out_fd, &res, sizeof(res)))
			goto writeerr;

		if L (data.sz != 0) {
			if U (!write_full(out_fd, data.p, data.sz))
				goto writeerr;

			buf_clear(&data);
		}
	}
out:
	buf_free(&data);
	buf_free(&tmp);
	PostThreadMessage(main_tid, WM_QUIT,
	    /* wParam */ (unsigned int)thread_rv,
	    /* lParam */ 0);
	return 0;
err:
	thread_rv = 1;
	goto out;
writeerr:
	if (errno != 0)
		perror("write");
	else
		fprintf(stderr, "write: unexpected EOF\n");
	goto err;
readerr:
	if (errno != 0)
		perror("read");
	else
		fprintf(stderr, "read: unexpected EOF\n");
	goto err;
}

// -----------------------------------------------------------------------------

#pragma GCC push_options
#pragma GCC optimize "-Os"

static bool
compat_update(struct plugin *pl, const struct fmt *fmt);

static bool
fmtchange(const struct fmt *fmt)
{
	fprintf(stderr, "format change: rate=%d bps=%d ch=%d\n",
	    fmt->rate, fmt->bps, fmt->ch);

	for (unsigned int i = 0; i < plugins_cnt; i++) {
		// discard cached data
		if (plugins[i].buf.sz != 0)
			buf_clear(&plugins[i].buf);

		// re-check compatibility
		if (!compat_update(&plugins[i], fmt))
			return false;
	}

	return true;
}

static bool
compat_update(struct plugin *pl, const struct fmt *fmt)
{
	const char *what;

	what = plugin_supports_format(pl, fmt);
	pl->skip = (what != NULL);

	if (pl->skip) {
		if (pl->opts.required) {
			fprintf(stderr, "error: required plugin %s doesn't support this %s, exiting\n",
			    superbasename(pl->opts.path),
			    what);
			return false;
		}

		fprintf(stderr, "warning: disabling %s due to unsupported %s\n",
		    superbasename(pl->opts.path),
		    what);
	}

	return true;
}

#pragma GCC pop_options
