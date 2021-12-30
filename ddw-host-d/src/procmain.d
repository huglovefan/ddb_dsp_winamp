module ddw.host.procmain;

import core.stdc.errno;
import core.stdc.stdio;

import core.sys.windows.winbase;
import core.sys.windows.windef;
import core.sys.windows.winuser;

import ddw.pipedata;
import ddw.host.buf;
import ddw.host.fmt;
import ddw.host.main;
import ddw.host.misc;
import ddw.host.plugin;
import ddw.host.plugload;
import ddw.host.plugproc;

extern (Windows) uint process_thread_main(void* ud)
{
	Buf data;
	Buf tmp;
	Fmt lastfmt;
	int thread_rv = 0;

	for (;;)
	{
		processing_request req = void;
		if (!read_full(globals.datapipe.in_fd, &req, req.sizeof))
			goto read1fail;

		Fmt curfmt = {
			rate: req.samplerate,
			bps: req.bitspersample,
			ch: req.channels,
		};
		assert(fmt_makes_sense(&curfmt));
		assert(req.buffer_size % fmt_frame_size(&curfmt) == 0);

		if (curfmt != lastfmt)
		{
			if (!fmtchange(globals.plugins, &curfmt))
				goto err;

			lastfmt = curfmt;
		}

		size_t restotal = 0;
		foreach (ref pl; globals.plugins)
		{
			if (!pl.skip)
				restotal += pl.buf.sz;
		}

		buf_clear(&data);
		buf_prepare_append(&data, restotal + cast(size_t)req.buffer_size);
		buf_init_reserved(&data, restotal);
		buf_register_append(&data, cast(size_t)req.buffer_size);
		if (!read_full(globals.datapipe.in_fd, data.p, cast(size_t)req.buffer_size))
			goto readerr;

		plugin_process_all(globals.plugins, &curfmt, &data, &tmp);

		processing_response res = {
			buffer_size: data.sz,
		};
		if (!write_full(globals.datapipe.out_fd, &res, res.sizeof))
			goto writeerr;

		if (data.sz != 0)
		{
			if (!write_full(globals.datapipe.out_fd, data.p, data.sz))
				goto writeerr;

			buf_clear(&data);
		}
	}
Lout:
	buf_free(&data);
	buf_free(&tmp);
	PostThreadMessage(globals.main_tid, WM_QUIT,
		/* wParam */ thread_rv,
		/* lParam */ 0);
	return 0;
err:
	thread_rv = 1;
	goto Lout;
read1fail:
	if (errno != 0)
		goto readerr;
	fprintf(stderr, "process thread got EOF\n");
	goto Lout;
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

private:

/**
 * process a format change
 * 
 * returns false if execution should be halted (a plugin with the "required"
 *  flag doesn't support the format)
 */
bool fmtchange(Plugin[] plugins, const(Fmt)* fmt)
{
	fprintf(stderr, "format change: rate=%d bps=%d ch=%d\n",
		fmt.rate, fmt.bps, fmt.ch);

	foreach (ref pl; plugins)
	{
		// discard cached data
		if (pl.buf.sz != 0)
			buf_clear(&pl.buf);

		// re-check compatibility
		if (!compat_update(&pl, fmt))
			return false;
	}

	return true;
}

bool compat_update(Plugin* pl, const(Fmt)* fmt)
{
	const(char)* what;

	what = plugin_supports_format(pl, fmt);
	pl.skip = (what != null);

	if (pl.skip)
	{
		if (pl.opts.required)
		{
			fprintf(stderr, "error: required plugin %s doesn't support this %s, exiting\n",
				superbasename(pl.opts.path),
				what);

			return false;
		}

		fprintf(stderr, "warning: disabling %s due to unsupported %s\n",
			superbasename(pl.opts.path),
			what);
	}

	return true;
}
