#include "buf.h"

#include <stdlib.h>
#include <string.h>

#include "macros.h"

void
buf_prepare_capacity(struct buf *self, size_t req)
{
	char *realp;
	size_t realcap;
	char *newp;
	size_t newcap;

	if L (self->cap >= req)
		return;

	// calculations from here on include the reserved space

	realp = (self->p != NULL) ? self->p-self->res : NULL;
	realcap = self->cap+self->res;

	req += self->res;

	newcap = realcap ?: 512;
	while (newcap < req)
		newcap *= 2;

	newp = realloc(realp, newcap);
	if U (newp == NULL)
		assert(!"buf_prepare_append: realloc");

	self->p = newp+self->res;
	self->cap = newcap-self->res;
}

void
buf_prepare_append(struct buf *self, size_t sz)
{
	buf_prepare_capacity(self, self->sz+sz);
}

void
buf_register_append(struct buf *self, size_t sz)
{
	assert(self->sz+sz <= self->cap);

	self->sz += sz;
}

void
buf_append(struct buf *self, const char *p, size_t sz)
{
	buf_prepare_append(self, sz);

	memcpy(self->p+self->sz, p, sz);
	self->sz += sz;
}

void
buf_clear(struct buf *self)
{
	if (self->p != NULL)
		self->p -= self->res; // rewind to the original pointer

	self->sz = 0;
	self->cap += self->res; // reclaim any reserved space
	self->res = 0;
}

void
buf_free(struct buf *self)
{
	if (self->p != NULL)
		free(self->p-self->res); // free the original pointer

	*self = (struct buf){0};
}

void
buf_swap(struct buf *self, struct buf *other)
{
	struct buf tmp = *self;
	*self = *other;
	*other = tmp;
}

void
buf_append_buf(struct buf *self, struct buf *other)
{
	if (other->sz > 0)
		buf_append(self, other->p, other->sz);
}

void
buf_prepend_buf(struct buf *self, struct buf *other)
{
	assert(self->res >= other->sz);

	memcpy(self->p-other->sz, other->p, other->sz);

	self->p -= other->sz;
	self->sz += other->sz;
	self->cap += other->sz;
	self->res -= other->sz;
}

enum buf_bound
buf_boundscheck_read(struct buf *self, const char *p, size_t sz)
{
	enum buf_bound flags = 0;
	size_t offset;

	if U (p == NULL)
		goto out;

	if U (self->p == NULL)
		goto out;

	if U (!(p >= self->p && p <= self->p+self->sz-!!sz))
		goto out;

	offset = p-self->p;

	if U (offset+sz > self->sz)
		goto out;

	flags |= BUF_TRUE;

	if (offset == 0)
		flags |= BUF_LEFTEDGE;

	if (p+sz == self->p+self->sz)
		flags |= BUF_RIGHTEDGE;

out:
	return flags;
}

//
// same as buf_boundscheck_read() but checks are with self->cap instead of self->sz
//
enum buf_bound
buf_boundscheck_write(struct buf *self, const char *p, size_t sz)
{
	enum buf_bound flags = 0;
	size_t offset;

	if U (p == NULL)
		goto out;

	if U (self->p == NULL)
		goto out;

	if U (!(p >= self->p && p <= self->p+self->cap-!!sz))
		goto out;

	offset = p-self->p;

	if U (offset+sz > self->cap)
		goto out;

	flags |= BUF_TRUE;

	if (offset == 0)
		flags |= BUF_LEFTEDGE;

	if (p+sz == self->p+self->cap)
		flags |= BUF_RIGHTEDGE;

out:
	return flags;
}

UNITTEST(buf_boundscheck_0) {
	char data[5] = "hi";
	struct buf b = {
		.p = data,
		.sz = 2,
		.cap = 4,
	};

	assert(buf_boundscheck_read(&b, b.p, 0) == (BUF_TRUE|BUF_LEFTEDGE)); // !h.i.
	assert(buf_boundscheck_read(&b, b.p+1, 0) == (BUF_TRUE)); // .h!i.
	assert(buf_boundscheck_read(&b, b.p+2, 0) == (BUF_TRUE|BUF_RIGHTEDGE)); // .h.i!
	assert(buf_boundscheck_read(&b, b.p+3, 0) == 0); // .h.i._!

	assert(buf_boundscheck_write(&b, b.p, 0) == (BUF_TRUE|BUF_LEFTEDGE)); // !h.i.0.0.
	assert(buf_boundscheck_write(&b, b.p+1, 0) == (BUF_TRUE)); // .h!i.0.0.
	assert(buf_boundscheck_write(&b, b.p+2, 0) == (BUF_TRUE)); // .h.i!0.0.
	assert(buf_boundscheck_write(&b, b.p+3, 0) == (BUF_TRUE)); // .h.i.0!0.
	assert(buf_boundscheck_write(&b, b.p+4, 0) == (BUF_TRUE|BUF_RIGHTEDGE)); // .h.i.0.0!
	assert(buf_boundscheck_write(&b, b.p+5, 0) == 0); // .h.i.0.0._!
}

UNITTEST(buf_boundscheck) {
	char data[80] = "the quick brown fox ju";
	struct buf b = {
		.p = data,
		.sz = strlen(data),
		.cap = sizeof(data),
	};

	// unrelated pointers
	assert(buf_boundscheck_read(&b, (void *)&b, 0) == 0);
	assert(buf_boundscheck_write(&b, (void *)&b, 0) == 0);

	// read

	assert(buf_boundscheck_read(&b, b.p, 1) == (BUF_TRUE|BUF_LEFTEDGE));
	assert(buf_boundscheck_read(&b, b.p+1, 1) == (BUF_TRUE));

	assert(buf_boundscheck_read(&b, b.p, strlen(data)) == (BUF_TRUE|BUF_LEFTEDGE|BUF_RIGHTEDGE));
	assert(buf_boundscheck_read(&b, b.p, strlen(data)+1) == 0);

	assert(buf_boundscheck_read(&b, b.p, strlen(data)-1) == (BUF_TRUE|BUF_LEFTEDGE));
	assert(buf_boundscheck_read(&b, b.p+1, strlen(data)-1) == (BUF_TRUE|BUF_RIGHTEDGE));

	assert(buf_boundscheck_read(&b, b.p+strlen(data)-2, 1) == (BUF_TRUE));
	assert(buf_boundscheck_read(&b, b.p+strlen(data)-1, 1) == (BUF_TRUE|BUF_RIGHTEDGE));

	// write

	assert(buf_boundscheck_write(&b, b.p, 1) == (BUF_TRUE|BUF_LEFTEDGE));
	assert(buf_boundscheck_write(&b, b.p+1, 1) == (BUF_TRUE));

	assert(buf_boundscheck_write(&b, b.p, sizeof(data)) == (BUF_TRUE|BUF_LEFTEDGE|BUF_RIGHTEDGE));
	assert(buf_boundscheck_read(&b, b.p, sizeof(data)+1) == 0);

	assert(buf_boundscheck_write(&b, b.p, sizeof(data)-1) == (BUF_TRUE|BUF_LEFTEDGE));
	assert(buf_boundscheck_write(&b, b.p+1, sizeof(data)-1) == (BUF_TRUE|BUF_RIGHTEDGE));

	assert(buf_boundscheck_write(&b, b.p+sizeof(data)-2, 1) == (BUF_TRUE));
	assert(buf_boundscheck_write(&b, b.p+sizeof(data)-1, 1) == (BUF_TRUE|BUF_RIGHTEDGE));

}

void
buf_set_size(struct buf *self, size_t sz)
{
	assert(sz <= self->cap);
	self->sz = sz;
}

void
buf_shrink_cap(struct buf *self, size_t sz)
{
	assert(sz <= self->cap);
	self->cap = sz;
}

void
buf_set_reserved(struct buf *self, size_t sz)
{
	assert(self->sz == 0); // restricted for simplicity
	assert(sz <= self->cap);

	self->p += sz;
	self->sz = 0;
	self->cap -= sz;
	self->res += sz;
}
