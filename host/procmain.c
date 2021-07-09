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

			fprintf(stderr, "format change: rate=%d bps=%d ch=%d\n",
			    fmt.rate, fmt.bps, fmt.ch);

			for (unsigned int i = 0; i < plugins_cnt; i++) {
				if (plugins[i].buf.sz != 0) {
					plugins[i].buf.sz = 0;
					warn = true;
				}
			}
			if (warn)
				fprintf(stderr, "warning: threw out buffered data due to format change\n");

			oldfmt = fmt;
		}

		buf_clear(&data);
		buf_prepare_append(&data, req.buffer_size);

		if U (!read_full(in_fd, data.p, req.buffer_size))
			goto readerr;

		buf_register_append(&data, req.buffer_size);

		for (unsigned int i = 0; i < plugins_cnt; i++) {
			procidx = i;
			plugin_process(&plugins[i], &fmt, &data, &tmp);
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
