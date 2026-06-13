#pragma once

/* note: somewhat experimental code - it works, so some places use it
   already. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Buffered reading from a file descriptor. */
struct io_reader
{
	int    fd;
	char  *buf;
	size_t consum; /* size to be compacted away */
	size_t fill;   /* filled size */
	size_t cap;    /* alloc size */
	char   status;
};

#define IO_READER_INIT ((struct io_reader){.fd = -1})

void   io_r_free_buf(struct io_reader *r);
void   io_r_compact(struct io_reader *r);
void   io_r_maybe_compact(struct io_reader *r);
bool   io_r_read_more(struct io_reader *r);
bool   io_r_has_data(struct io_reader *r);
bool   io_r_read_consume(struct io_reader *r, char *p, size_t len);
bool   io_r_read_retain(struct io_reader *r, void *p, size_t len);
char  *io_r_readp(struct io_reader *r);
size_t io_r_readsz(struct io_reader *r);
void   io_r_consume(struct io_reader *r, size_t len);
bool   io_r_has_error(struct io_reader *r);
bool   io_r_has_eos(struct io_reader *r);

/* High-level read interface. */
struct io_readparse
{
	struct io_reader *r;
};

bool     io_rp_read(struct io_readparse *rp, void *buf, size_t len);
void    *io_rp_alloc(struct io_readparse *rp, size_t len);
int32_t  io_rp_s32(struct io_readparse *rp);
uint32_t io_rp_u32(struct io_readparse *rp);
uint8_t  io_rp_u8(struct io_readparse *rp);

#if defined(__cplusplus)
}
#endif
