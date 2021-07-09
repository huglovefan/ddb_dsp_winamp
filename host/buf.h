#pragma once

#include <stddef.h>

struct buf {
	char *p;
	size_t sz;
	size_t cap;
};

enum buf_bound {
	BUF_TRUE      = 0b001,
	BUF_LEFTEDGE  = 0b010,
	BUF_RIGHTEDGE = 0b100,
	BUF_COMPLETE  = BUF_LEFTEDGE|BUF_RIGHTEDGE,
};

void
buf_prepare_append(struct buf *self, size_t sz);

void
buf_register_append(struct buf *self, size_t sz);

void
buf_append(struct buf *self, const char *p, size_t sz);

void
buf_clear(struct buf *self);

void
buf_free(struct buf *self);

void
buf_swap(struct buf *self, struct buf *other);

void
buf_append_buf(struct buf *self, struct buf *other);

enum buf_bound
buf_boundscheck_read(struct buf *self, const char *p, size_t sz);

//
// same as buf_boundscheck_read() but checks are with self->cap instead of self->sz
//
enum buf_bound
buf_boundscheck_write(struct buf *self, const char *p, size_t sz);

void
buf_set_size(struct buf *self, size_t sz);

void
buf_shrink_cap(struct buf *self, size_t sz);
