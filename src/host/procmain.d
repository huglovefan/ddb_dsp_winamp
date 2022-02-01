module ddw.host.procmain;

import core.stdc.errno;
import core.stdc.stdint;
import core.stdc.stdio;

import core.sys.windows.winbase;
import core.sys.windows.windef;
import core.sys.windows.winuser;

import core.atomic;
import core.thread.osthread : rt_moduleTlsCtor, rt_moduleTlsDtor, thread_attachThis;
import core.thread.threadbase : thread_detachThis;

import ddw.common.pipedata;
import ddw.host.buf;
import ddw.host.fmt;
import ddw.host.main;
import ddw.host.misc;
import ddw.host.plugin;
import ddw.host.plugload;
import ddw.host.plugproc;

extern(Windows)
uint process_thread_main(void*)
{
	try
	{ // ---

	Buf data;
	Buf tmp;
	Fmt lastfmt;
	int thread_rv = 0;

	thread_attachThis();
	rt_moduleTlsCtor();

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

		if (!checkreadparams(&curfmt, req.buffer_size))
			goto err;

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
	rt_moduleTlsDtor();
	thread_detachThis();
	return 0;
err:
	thread_rv = 1;
	goto Lout;
read1fail:
	if (errno != 0)
		goto readerr;
	printf("process thread got EOF\n");
	goto Lout;
writeerr:
	if (errno != 0)
		perror("write");
	else
		printf("write: unexpected EOF\n");
	goto err;
readerr:
	if (errno != 0)
		perror("read");
	else
		printf("read: unexpected EOF\n");
	goto err;

	} // ---
	catch (Throwable e)
	{
		Plugin* pl = cast(Plugin*)procplug.atomicLoad();
		if (pl != null)
			printf("fatal error: %s threw during processing\n",
				pl.opts.dllname.ptr);
		else
			printf("fatal error: uncaught exception in processing thread\n");
		while (e)
		{
			string s = e.toString();
			printf("%.*s", cast(int)s.length, s.ptr);
			e = e.next;
		}
		TerminateProcess(GetCurrentProcess(), 1);
		assert(0);
	}
}

// -----------------------------------------------------------------------------

private:

/**
 * check that the format and buffer size match and aren't random data
 */
bool checkreadparams(const(Fmt)* fmt, ulong buffer_size)
{
	bool ok = true;

	if (!fmt_makes_sense(fmt))
	{
		printf("error: read nonsensical input format: rate=%u bps=%u ch=%u\n",
			fmt.rate, fmt.bps, fmt.ch);
		ok = false;
	}

	if (buffer_size > size_t.max)
	{
		printf("error: input data size %llu doesn't fit in size_t\n",
			buffer_size);
		ok = false;
	}

	if ((buffer_size % fmt_frame_size(fmt)) != 0)
	{
		printf("error: input data size %llu is not a multiple of frame size %u\n",
			buffer_size, fmt_frame_size(fmt));
		ok = false;
	}

	return ok;
}

/**
 * process a format change
 * 
 * returns false if execution should be halted (a plugin with the "required"
 *  flag doesn't support the format)
 */
bool fmtchange(Plugin[] plugins, const(Fmt)* fmt)
{
	printf("format change: rate=%u bps=%u ch=%u\n",
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
	const(char)* what = plugin_supports_format(pl, fmt);
	pl.skip = (what != null);

	if (pl.skip)
	{
		if (pl.opts.required)
		{
			printf("error: required plugin %s doesn't support this %s, exiting\n",
				pl.opts.dllname.ptr,
				what);

			return false;
		}

		printf("warning: %s doesn't support this %s, disabling it\n",
			pl.opts.dllname.ptr,
			what);
	}

	return true;
}
