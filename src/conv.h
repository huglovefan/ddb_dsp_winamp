#pragma once

#include <stddef.h>
#include "sfmt.h"

#if defined(__cplusplus)
extern "C" {
#endif

struct conv_request
{
	const void  *src;
	void        *dst;
	size_t       count;
	SFMT         from;
	SFMT         to;

	/* optional - additional bits of entropy to use for seeding the
	   dithering prng. */
	unsigned long long entropy;

	/* skip dithering altogether. */
	bool no_dither;
};

void conv_bits(struct conv_request req);

#if defined(__cplusplus)
}
#endif
