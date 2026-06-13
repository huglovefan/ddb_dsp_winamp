#pragma once

#include <stddef.h>
#include <span>

/*

            xxxxxxxxxxxxxx            <- a_read - "readable"
            xxxxxxxxxxxxxxxxxxxxxxxxx <- a_overwrite - "overwritable"
                         xxxxxxxxxxxx <- "writable"
            xxxxxxxxxxxxxxxxxxxxxxxxx <- "capacity"
 [ reserved |    data    |  unused  ]
 ^          ^            ^          ^
 base       databegin    dataend    capend

For the most part, Buf is just like a standard resizable buffer class,
but its special feature is the reserved space.

Reserved space makes it possible to add data to the beginning of the
buffer without moving the old data.

*/

struct Buf
{
#if !defined(BUF_CPP)
private:
#endif

	char *base;
	char *databegin;
	char *dataend;
	char *capend;

public:

	inline char  *reservedp()        { return base; }
	inline size_t reservedsz() const { return databegin-base; }

	inline char  *datap()            { return databegin; }
	inline char  *dataendp()         { return dataend; }
	inline size_t datasz()     const { return dataend-databegin; }

	inline char  *unusedp()          { return dataend; }
	inline char  *unusend()          { return capend; }
	inline size_t unusedsz()   const { return capend-dataend; }

	inline std::span<char> a_read()
	    { return std::span(databegin, dataend-databegin); }
	inline std::span<char> a_overwrite()
	    { return std::span(databegin, capend-databegin); }
};

void buf_prepare_capacity(Buf *self, size_t req);
void buf_prepare_append(Buf *self, size_t sz);
void buf_register_append(Buf *self, size_t sz);
void buf_append(Buf *self, const char *p, size_t sz);
void buf_clear(Buf *self);
void buf_free(Buf *self);
void buf_swap(Buf *self, Buf *other);
void buf_append_buf(Buf *self, const Buf *other);
void buf_prepend_buf(Buf *self, const Buf *other);
void buf_set_size(Buf *self, size_t sz);
void buf_init_with_reserved_and_capacity(
    Buf *self, size_t res, size_t cap);
void buf_shift_data_into_reserved(Buf *self, size_t sz);
