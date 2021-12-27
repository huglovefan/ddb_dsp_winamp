module ddw.host.buf;

import core.stdc.stdlib;
import core.stdc.string;

nothrow:
@nogc:

// -----------------------------------------------------------------------------

struct Buf
{
	char* p;
	size_t sz; /// used size
	size_t cap; /// total capacity

	//
	// how much "reserved space" there is to the left of `p`
	// this isn't included in the other struct members so be careful
	// the real allocated size is `cap+res`, and the real pointer is `p-res`
	//
	size_t res;
}

enum buf_bound
{
	BUF_NONE      = 0b000,
	BUF_TRUE      = 0b001,
	BUF_LEFTEDGE  = 0b010,
	BUF_RIGHTEDGE = 0b100,
	BUF_COMPLETE  = BUF_LEFTEDGE|BUF_RIGHTEDGE,
}

// -----------------------------------------------------------------------------

void buf_prepare_capacity(Buf* self, size_t req)
{
	char* realp;
	size_t realcap;
	char* newp;
	size_t newcap;

	// already have that much?
	if (self.cap >= req)
		return;

	// get the real allocated pointer and capacity
	realp = (self.p != null) ? self.p-self.res : null;
	realcap = self.cap+self.res;

	// include the reserved space when calculating the new allocation size
	req += self.res;

	newcap = (realcap != 0) ? realcap : 512;
	while (newcap < req)
		newcap *= 2;

	newp = cast(char*)realloc(realp, newcap);
	assert(newp != null);

	self.p = newp+self.res;
	self.cap = newcap-self.res;
}

void buf_prepare_append(Buf* self, size_t sz)
{
	buf_prepare_capacity(self, self.sz+sz);
}

void buf_register_append(Buf* self, size_t sz)
{
	assert(self.sz+sz <= self.cap);

	self.sz += sz;
}

void buf_append(Buf* self, const(char)* p, size_t sz)
{
	buf_prepare_append(self, sz);

	memcpy(self.p+self.sz, p, sz);
	self.sz += sz;
}

void buf_clear(Buf* self)
{
	if (self.p != null)
		self.p -= self.res; // rewind to the original pointer

	self.sz = 0;
	self.cap += self.res; // reclaim any reserved space
	self.res = 0;
}

void buf_free(Buf* self)
{
	if (self.p != null)
		free(self.p-self.res); // free the original pointer

	*self = Buf.init;
}

void buf_swap(Buf* self, Buf* other)
{
	Buf tmp = *self;
	*self = *other;
	*other = tmp;
}

void buf_append_buf(Buf* self, Buf* other)
{
	buf_append(self, other.p, other.sz);
}

void buf_prepend_buf(Buf* self, Buf* other)
{
	assert(self.res >= other.sz);

	memcpy(self.p-other.sz, other.p, other.sz);

	self.p -= other.sz;
	self.sz += other.sz;
	self.cap += other.sz;
	self.res -= other.sz;
}

buf_bound buf_boundscheck_read(Buf* self, const(char)* p, size_t sz)
{
	buf_bound flags = buf_bound.BUF_NONE;
	size_t offset;

	if (p == null)
		goto Lout;

	if (self.p == null)
		goto Lout;

	if (!(p >= self.p && p <= self.p+self.sz-!!sz))
		goto Lout;

	offset = cast(size_t)(p-self.p);

	if (offset+sz > self.sz)
		goto Lout;

	flags |= buf_bound.BUF_TRUE;

	if (offset == 0)
		flags |= buf_bound.BUF_LEFTEDGE;

	if (p+sz == self.p+self.sz)
		flags |= buf_bound.BUF_RIGHTEDGE;

Lout:
	return flags;
}

//
// same as buf_boundscheck_read() but checks are with self.cap instead of self.sz
//
buf_bound buf_boundscheck_write(Buf* self, const(char)* p, size_t sz)
{
	buf_bound flags = buf_bound.BUF_NONE;
	size_t offset;

	if (p == null)
		goto Lout;

	if (self.p == null)
		goto Lout;

	if (!(p >= self.p && p <= self.p+self.cap-!!sz))
		goto Lout;

	offset = cast(size_t)(p-self.p);

	if (offset+sz > self.cap)
		goto Lout;

	flags |= buf_bound.BUF_TRUE;

	if (offset == 0)
		flags |= buf_bound.BUF_LEFTEDGE;

	if (p+sz == self.p+self.cap)
		flags |= buf_bound.BUF_RIGHTEDGE;

Lout:
	return flags;
}

buf_bound buf_boundscheck_read(Buf* self, const(char)[] p)
{
	return buf_boundscheck_read(self, p.ptr, p.length);
}

buf_bound buf_boundscheck_write(Buf* self, const(char)[] p)
{
	return buf_boundscheck_write(self, p.ptr, p.length);
}

void buf_set_size(Buf* self, size_t sz)
{
	assert(sz <= self.cap);
	self.sz = sz;
}

void buf_shrink_cap(Buf* self, size_t sz)
{
	assert(sz <= self.cap);
	self.cap = sz;
}

void buf_init_reserved(Buf* self, size_t sz)
{
	assert(self.sz == 0); // restricted for simplicity
	assert(sz <= self.cap);

	self.p += sz;
	self.sz = 0;
	self.cap -= sz;
	self.res += sz;
}

void buf_increase_reserved(Buf* self, size_t sz)
{
	assert(sz <= self.cap);

	self.p += sz;
	self.sz -= sz;
	self.cap -= sz;
	self.res += sz;
}

unittest { with (buf_bound)
{
	char[5] data = "hi";
	Buf b = {
		p: data.ptr,
		sz: 2,
		cap: 4,
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
} }

unittest { with (buf_bound)
{
	char[80] data = "the quick brown fox ju";
	Buf b = {
		p: data.ptr,
		sz: strlen(data.ptr),
		cap: data.sizeof,
	};
	int x;

	// unrelated pointers
	assert(buf_boundscheck_read(&b, cast(char*)&x, 0) == 0);
	assert(buf_boundscheck_write(&b, cast(char*)&x, 0) == 0);

	// read

	assert(buf_boundscheck_read(&b, b.p, 1) == (BUF_TRUE|BUF_LEFTEDGE));
	assert(buf_boundscheck_read(&b, b.p+1, 1) == (BUF_TRUE));

	assert(buf_boundscheck_read(&b, b.p, strlen(data.ptr)) == (BUF_TRUE|BUF_LEFTEDGE|BUF_RIGHTEDGE));
	assert(buf_boundscheck_read(&b, b.p, strlen(data.ptr)+1) == 0);

	assert(buf_boundscheck_read(&b, b.p, strlen(data.ptr)-1) == (BUF_TRUE|BUF_LEFTEDGE));
	assert(buf_boundscheck_read(&b, b.p+1, strlen(data.ptr)-1) == (BUF_TRUE|BUF_RIGHTEDGE));

	assert(buf_boundscheck_read(&b, b.p+strlen(data.ptr)-2, 1) == (BUF_TRUE));
	assert(buf_boundscheck_read(&b, b.p+strlen(data.ptr)-1, 1) == (BUF_TRUE|BUF_RIGHTEDGE));

	// write

	assert(buf_boundscheck_write(&b, b.p, 1) == (BUF_TRUE|BUF_LEFTEDGE));
	assert(buf_boundscheck_write(&b, b.p+1, 1) == (BUF_TRUE));

	assert(buf_boundscheck_write(&b, b.p, data.sizeof) == (BUF_TRUE|BUF_LEFTEDGE|BUF_RIGHTEDGE));
	assert(buf_boundscheck_read(&b, b.p, data.sizeof+1) == 0);

	assert(buf_boundscheck_write(&b, b.p, data.sizeof-1) == (BUF_TRUE|BUF_LEFTEDGE));
	assert(buf_boundscheck_write(&b, b.p+1, data.sizeof-1) == (BUF_TRUE|BUF_RIGHTEDGE));

	assert(buf_boundscheck_write(&b, b.p+data.sizeof-2, 1) == (BUF_TRUE));
	assert(buf_boundscheck_write(&b, b.p+data.sizeof-1, 1) == (BUF_TRUE|BUF_RIGHTEDGE));
} }
