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

// -----------------------------------------------------------------------------

void process_thread_main()
{
	Buf data;
	Buf tmp;
	Fmt lastfmt;
	bool normalExit;

	for (;;)
	{
		/*
		 * read header
		 */

		processing_request req;
		if (!read_full(globals.datapipe.in_fd, &req, req.sizeof))
		{
			if (!errno)
			{
				printf("process thread got EOF\n");
				normalExit = true;
				break;
			}
			perror("read");
			break;
		}

		/*
		 * make format, check and verify it
		 */

		Fmt curfmt = {
			rate: req.samplerate,
			bps: req.bitspersample,
			ch: req.channels,
		};

		if (!checkreadparams(&curfmt, req.buffer_size))
			break;

		if (curfmt != lastfmt)
		{
			if (!fmtchange(globals.plugins, &curfmt))
				break;

			lastfmt = curfmt;
		}

		/*
		 * prepare buffer
		 */

		// get the total size of data the plugins might prepend to the buffer
		size_t restotal;
		foreach (ref pl; globals.plugins)
		{
			if (!pl.skip)
				restotal += pl.buf.sz;
		}

		buf_clear(&data);
		buf_clear(&tmp);

		buf_prepare_append(&data, restotal + cast(size_t)req.buffer_size);
		buf_init_reserved(&data, restotal);

		/*
		 * read data
		 */

		if (!read_full(globals.datapipe.in_fd, data.p, cast(size_t)req.buffer_size))
		{
			if (errno != 0)
				perror("read");
			else
				printf("read: unexpected EOF\n");
			break;
		}
		buf_register_append(&data, cast(size_t)req.buffer_size);

		/*
		 * process!
		 */

		plugin_process_all(globals.plugins, &curfmt, &data, &tmp);

		/*
		 * write header
		 */

		processing_response res = {
			buffer_size: data.sz,
		};
		if (!write_full(globals.datapipe.out_fd, &res, res.sizeof))
		{
			perror("write");
			break;
		}

		/*
		 * write data
		 */

		if (data.sz != 0)
		{
			if (!write_full(globals.datapipe.out_fd, data.p, data.sz))
			{
				perror("write");
				break;
			}

			buf_clear(&data);
		}
	}

	buf_free(&data);
	buf_free(&tmp);

	int rv = (normalExit) ? 0 : 1;

	PostThreadMessage(globals.mainThreadId, WM_QUIT,
		/* wParam */ rv,
		/* lParam */ 0);
}

extern(Windows)
uint process_thread_entry(void*)
{
	try
	{
		thread_attachThis();
		rt_moduleTlsCtor();
		process_thread_main(); // <--
		rt_moduleTlsDtor();
		thread_detachThis();
		return 0;
	}
	catch (Throwable e)
	{
		if (Plugin* pl = cast(Plugin*)procplug.atomicLoad())
			printf("fatal error: %s threw during processing\n",
				pl.opts.dllname.ptr);
		else
			printf("fatal error: uncaught exception in processing thread\n");

		// print exception chain
		int limit = 10;
		while (e && limit --> 0)
		{
			e.toString((in char[] s)
			{
				printf("%.*s", cast(int)s.length, s.ptr);
			});
			printf("\n");
			e = e.next;
		}

		_exit(1);
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
 * returns true if everything is ok, false if execution should be halted (a
 *  plugin with the "required" flag doesn't support the format)
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

/**
 * check plugin compatibility on format change
 */
bool compat_update(Plugin* pl, const(Fmt)* fmt)
{
	const(char)* reason = plugin_supports_format(pl, fmt);

	pl.skip = (reason != null);

	if (pl.skip)
	{
		if (pl.opts.required)
		{
			printf("error: required plugin %s doesn't support this %s, exiting\n",
				pl.opts.dllname.ptr,
				reason);

			return false;
		}

		printf("warning: %s doesn't support this %s, disabling it\n",
			pl.opts.dllname.ptr,
			reason);
	}

	return true;
}
