#define BUF_CPP
#include "buf.hpp"

#include <assert.h>
#include <string.h>
#include "misc.hpp"

/*
Grow capacity (data+unused) to req bytes.
Keeps reserved size.
Keeps data, never shrinks it.
*/
void buf_prepare_capacity(Buf *self, size_t req)
{
	size_t dataoff;
	size_t dataendoff;
	size_t newalloc;
	char *newbase;

	if (req <= (self->capend - self->databegin))
		return;

	dataoff    = (self->databegin - self->base);
	dataendoff = (self->dataend   - self->base);

	newalloc = (self->dataend - self->base) + req;

	newbase = (char *)realloc(self->base, newalloc);

	if (!newbase)
		abort_msg("out of memory");

	self->base      = newbase;
	self->databegin = newbase + dataoff;
	self->dataend   = newbase + dataendoff;
	self->capend    = newbase + newalloc; /* this one is new */
}

/*
Ensure that the buffer has at least "sz" bytes of free unused space.
*/
void buf_prepare_append(Buf *self, size_t sz)
{
	size_t oldcap;
	size_t newcap;

	/* data size */
	oldcap = (self->dataend - self->databegin);

	/* oldcap+sz overflow */
	assert(sz <= (SIZE_MAX - oldcap));

	newcap = oldcap+sz;

	buf_prepare_capacity(self, newcap);
}

void buf_register_append(Buf *self, size_t sz)
{
	/* sz in [0..unused] */
	assert(sz <= (self->capend - self->dataend));

	self->dataend += sz;
}

/*
Copy the given data to the end of this buffer's data, preserving any
existing contents.
*/
void buf_append(Buf *self, const char *p, size_t sz)
{
	char *w;

	buf_prepare_append(self, sz);

	w = self->dataend;
	self->dataend = w+sz;
	memcpy(w, p, sz);
}

/*
Reset this buffer to an empty state. This works by a simple pointer
adjustment. No data is cleared, and no allocations are changed.
*/
void buf_clear(Buf *self)
{
	char *p;

	p = self->base;
	self->databegin = p;
	self->dataend   = p;
	self->capend    = p;
}

/*
Free the internal allocation of this buffer. The buffer is reset to an
empty initial state.
*/
void buf_free(Buf *self)
{
	char *p;

	p = self->base;
	if (p)
		free(p);
	memset(self, 0, sizeof(*self));
}

/*
Exchange the contents of the two buffers, including their allocations
and reserved sizes. This works by simply swapping their internal
pointers.
*/
void buf_swap(Buf *self, Buf *other)
{
	Buf tmp;

	if (other != self)
	{
		tmp = *self;
		*self = *other;
		*other = tmp;
	}
}

/*
Copy the other buffer's data to the end of this buffer's data,
preserving any existing data.
*/
void buf_append_buf(Buf *self, const Buf *other)
{
	if (!other->datasz())
		return;

	buf_append(
	    self,
	    other->databegin,
	    (other->dataend - other->databegin));
}

/*
Copy the given buffer's data area contents to the beginning of this
buffer's data area, preserving any existing data.

This buffer must have enough reserved space to hold the other buffer's
data.
*/
void buf_prepend_buf(Buf *self, const Buf *other)
{
	size_t read_size;
	size_t write_avail;

	read_size = (other->dataend - other->databegin); /* data */
	write_avail = (self->databegin - self->base); /* reserved */

	/* other data <= this reserved */
	assert(read_size <= write_avail);

	self->databegin -= read_size;
	memcpy(self->databegin, other->databegin, read_size);
}

/*
Set the size of the buffer's data area. This works by a simple pointer
adjustment.

No data is initialized by this function. It can be used, for example,
after reading data to the buffer.
*/
void buf_set_size(Buf *self, size_t sz)
{
	/* sz in [0..data+unused] */
	assert(sz <= (self->capend - self->databegin));

	self->dataend = (self->databegin + sz);
}

void buf_init_with_reserved_and_capacity(
	Buf   *self,
	size_t res,
	size_t cap)
{
	char *p;

	/* res+cap overflow */
	assert(cap <= (SIZE_MAX - res));

	buf_clear(self);
	buf_prepare_capacity(self, res+cap);
	p = self->base+res;
	self->databegin = p;
	self->dataend   = p;
}

/*
Shift bytes from the beginning of the data area into the end of the
reserved area. This works by a simple pointer adjustment.
*/
void buf_shift_data_into_reserved(Buf *self, size_t sz)
{
	/* sz in [0..data] */
	assert(sz <= (self->dataend - self->databegin));

	self->databegin += sz;
}
