#pragma once

#include <stdbool.h>
#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

enum
{
	SFMT_INVALID,
	SFMT_S8,
	SFMT_S16,
	SFMT_S24,
	SFMT_S32,
	SFMT_F32,
	SFMT_COUNT
};
typedef unsigned int SFMT;

#if defined(__GNUC__)
/* const: pure with no access to pointers/globals. */
# define FMT_CONST __attribute__((const))
#else
# define FMT_CONST
#endif

/* Test if the sample format is valid. */
bool FMT_CONST sfmt_is_valid(SFMT fmt);

/* Test if two sample formats are the same.
   If either of them is invalid, the result is false. */
bool FMT_CONST sfmt_equal(SFMT fmt1, SFMT fmt2);

/* Test if this is a floating-point sample format. */
bool FMT_CONST sfmt_is_float(SFMT fmt);

/* Get the size in bytes of one sample. */
size_t FMT_CONST sfmt_bytes(SFMT fmt);

size_t FMT_CONST sfmt_bytes_n(SFMT fmt, size_t count);

const char *FMT_CONST sfmt_name(SFMT fmt);

SFMT sfmt_parse(const char *s);

#undef FMT_CONST

#if defined(__cplusplus)
}
#endif
