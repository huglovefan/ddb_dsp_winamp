#include "io.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(_WIN32)
#include "host/fastprintf.h"
#else
#include <stdio.h>
#define fastprintf(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#endif

enum
{
	IO_EOS = 1,
	IO_ERROR = 2,
	/* partial result read, unable to return to normal state */
	IO_CORRUPT = 4,

	IO_ANYERR = 2|4
};

void io_r_free_buf(struct io_reader *r)
{
	if (!r->buf)
		return;
	free(r->buf);
	r->buf = 0;
	r->consum = 0;
	r->fill = 0;
	r->cap = 0;
}

/*
Move yet-to-be consumed data to the beginning of the buffer.
Invalidates pointers.
*/
void io_r_compact(struct io_reader *r)
{
	void *src;
	size_t len;

	if (!r->buf)
		return;

	src = r->buf + r->consum;
	len = r->fill - r->consum;

	r->fill -= r->consum;
	r->consum = 0;

	memmove(r->buf, src, len);
}

void io_r_maybe_compact(struct io_reader *r)
{
	if (!r->buf)
		return;
	if (r->consum && r->fill == r->consum)
	{
		r->consum = 0;
		r->fill = 0;
		return;
	}
	if (r->consum < 4096)
		return;
	io_r_compact(r);
}

/*
Read some more data.
Invalidates pointers.
*/
bool io_r_read_more(struct io_reader *r)
{
	enum { MIN = 512, MAX = 4096 };
	char *p;
	ssize_t rv;

	if (r->status)
		return false;

	io_r_compact(r);

	if (r->cap - r->fill < MIN)
	{
		assert(MAX <= SIZE_MAX-r->fill);
		p = (char *)realloc(r->buf, r->fill + MAX);
		if (!p)
		{
			r->status = IO_ERROR;
			return false;
		}
		r->buf = p;
		r->cap = r->fill + MAX;
	}

	rv = read(r->fd, r->buf + r->fill, r->cap - r->fill);
	if (rv < 0)
	{
		r->status |= IO_ERROR;
		return false;
	}
	if (!rv)
	{
		r->status |= IO_EOS;
		return false;
	}
	r->fill += rv;

	return true;
}

bool io_r_read_consume(struct io_reader *r, char *p, size_t len)
{
	memset(p, 0xaa, len);

	if (r->buf)
	{
		size_t have;

		assert(r->consum <= r->fill);
		have = r->fill - r->consum;
		if (have > len)
			have = len;
		memcpy(p, r->buf + r->consum, have);
		r->consum += have;
		p += have;
		len -= have;
		if (!len)
			return true;
	}

	r->consum = 0;
	r->fill = 0;

	while (len)
	{
		ssize_t rv;

		rv = read(r->fd, p, len);
		if (rv < 0)
		{
			r->status |= IO_ERROR;
			r->status |= IO_CORRUPT;
			return false;
		}
		if (!rv)
		{
			r->status |= IO_EOS;
			r->status |= IO_CORRUPT;
			errno = 0;
			return false;
		}
		p += rv;
		len -= rv;
	}

	return true;
}

bool io_r_has_data(struct io_reader *r)
{
	if (r->fill > r->consum)
		return true;

	return io_r_read_more(r);
}

bool io_r_read_retain(struct io_reader *r, void *p, size_t len)
{
	assert(r->consum <= r->fill);

	while ((r->fill - r->consum) < len && io_r_read_more(r))
		continue;

	if ((r->fill - r->consum) < len)
	{
		memset(p, 0, len);
		return false;
	}
	else
	{
		memcpy(p, r->buf + r->consum, len);
		return true;
	}
}

char *io_r_readp(struct io_reader *r)
{
	return (r->buf) ? r->buf + r->consum : NULL;
}

size_t io_r_readsz(struct io_reader *r)
{
	assert(r->consum <= r->fill);

	return r->fill - r->consum;
}

void io_r_consume(struct io_reader *r, size_t len)
{
	assert(r->consum <= r->fill && len <= (r->fill - r->consum));

	r->consum += len;
}

bool io_r_has_error(struct io_reader *r)
{
	return !!(r->status & IO_ANYERR);
}

bool io_r_has_eos(struct io_reader *r)
{
	return !!(r->status & IO_EOS);
}

/*** io_readparse ***/

bool io_rp_read(struct io_readparse *rp, void *buf, size_t len)
{
	io_r_read_consume(rp->r, (char *)buf, len);
	return !rp->r->status;
}

void *io_rp_alloc(struct io_readparse *rp, size_t len)
{
	void *p;

	if (rp->r->status)
		return NULL;

	p = malloc(len);
	if (!p)
	{
		rp->r->status = IO_CORRUPT;
		return NULL;
	}

	io_r_read_consume(rp->r, (char *)p, len);
	if (rp->r->status)
	{
		rp->r->status = IO_CORRUPT;
		free(p);
		return NULL;
	}

	return p;
}

int32_t io_rp_s32(struct io_readparse *rp)
{
	int32_t rv;

	io_rp_read(rp, &rv, sizeof(rv));
	return rv;
}

uint32_t io_rp_u32(struct io_readparse *rp)
{
	uint32_t rv;

	io_rp_read(rp, &rv, sizeof(rv));
	return rv;
}

uint8_t io_rp_u8(struct io_readparse *rp)
{
	uint8_t rv;

	io_rp_read(rp, &rv, sizeof(rv));
	return rv;
}
