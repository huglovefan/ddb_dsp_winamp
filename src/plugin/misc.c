#include "misc.h"

#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

bool read_full(int fd, void *data, size_t size)
{
	while (size)
	{
		ssize_t rv;

		while ((rv = read(fd, data, size)) < 0
		    && errno == EINTR)
			continue;

		if (rv < 0)
			return false;

		if (!rv)
		{
			errno = EIO;
			return false;
		}

		data = (char *)data + rv;
		size -= rv;
	}

	return true;
}

bool writev_full(int fd, struct iovec *iovecs, size_t count)
{
	if (count > sysconf(_SC_IOV_MAX))
	{
		errno = EINVAL;
		return false;
	}

	while (count)
	{
		ssize_t rv;

		while ((rv = writev(fd, iovecs, count)) < 0
		    && errno == EINTR)
			continue;

		if (rv < 0)
			return false;

		/* writev returns 0 if the iovecs had a total length
		   of 0. this'll be handled in the loop below. */
		/*if (rv == 0)*/

		/* skip the iovecs that were written. */
		while (count)
		{
			/* fully wrote this iovec? */
			if (rv >= iovecs[0].iov_len)
			{
				rv -= iovecs[0].iov_len;
				iovecs++;
				count--;
			}
			else
			/* wrote a part of it. */
			{
				iovecs[0].iov_base =
				    (char *)iovecs[0].iov_base + rv;
				iovecs[0].iov_len -= rv;
				rv = 0;
				/* should've been the last one that
				   had anything written. */
				break;
			}
		}
	}

	return true;
}
