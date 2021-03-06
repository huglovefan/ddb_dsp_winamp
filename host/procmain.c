#include "procmain.h"

#include <stdio.h>

#include "../plugin/ddw.h"

#include "macros.h"
#include "main.h"
#include "misc.h"

DWORD WINAPI
process_thread_main(void *ud)
{
	struct buf data = {0};
	struct buf tmp = {0};
	struct fmt fmt = {0};
	struct fmt oldfmt = {0};
	size_t restotal;
	int thread_rv = 0;
	(void)ud;

	for (;;) {
		struct processing_request req;
		struct processing_response res;

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

		if U (!fmt_same(&fmt, &oldfmt)) {
			bool warn = false;
			const char *what;

			fprintf(stderr, "format change: rate=%d bps=%d ch=%d\n",
			    fmt.rate, fmt.bps, fmt.ch);

			for (unsigned int i = 0; i < plugins_cnt; i++) {
				if (plugins[i].buf.sz != 0) {
					plugins[i].buf.sz = 0;
					warn = true;
				}

				//
				// re-check compatibility
				//
				plugins[i].skip = false;
				what = plugin_supports_format(&plugins[i], &fmt);
				if (what != NULL) {
					if (plugins[i].opts.required) {
						fprintf(stderr, "error: required plugin %s doesn't support this %s, exiting\n",
						    superbasename(plugins[i].opts.path),
						    what);
						goto err;
					}
					fprintf(stderr, "warning: disabling %s due to unsupported %s\n",
					    superbasename(plugins[i].opts.path),
					    what);
					plugins[i].skip = true;
				}
			}
			if (warn)
				fprintf(stderr, "warning: threw out buffered data due to format change\n");

			oldfmt = fmt;
		}

		restotal = 0;
		for (unsigned int i = 0; i < plugins_cnt; i++) {
			if (!plugins[i].skip)
				restotal += plugins[i].buf.sz;
		}

		buf_clear(&data);
		buf_prepare_append(&data, restotal+req.buffer_size);
		buf_set_reserved(&data, restotal);

		if U (!read_full(in_fd, data.p, req.buffer_size))
			goto readerr;

		buf_register_append(&data, req.buffer_size);

		for (unsigned int i = 0; i < plugins_cnt; i++) {
			size_t oldtmpsz, oldres, resused;

			if (plugins[i].skip)
				continue;

			oldtmpsz = plugins[i].buf.sz;
			oldres = data.res;

			procidx = i;
			plugin_process(&plugins[i], &fmt, &data, &tmp);

			resused = oldres-data.res;

			// plugin used either 0 reserved space OR the exact old
			//  size of its tmp buffer
D			assert(resused == 0 || resused == oldtmpsz);

			if (data.sz == 0)
				break;
		}
		procidx = -1;

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
	    /* wParam */ thread_rv,
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
