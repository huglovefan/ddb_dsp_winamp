/* SPDX-License-Identifier: CC0-1.0 */
/*
 *    crc32.c - calculate crc32 checksums of data
 *
 *    Written in 2020 by Ayman El Didi
 *
 *    To the extent possible under law, the author(s) have dedicated all
 *    domain worldwide. This software is distributed without any warranty.
 *
 *    You should have received a copy of the CC0 Public Domain Dedication along
 *    with this software. If not, see
 *    <http://creativecommons.org/publicdomain/zero/1.0/>.
 */
#include "crc32.h"
#include <stddef.h>
#include <stdint.h>

static uint32_t
crc32_for_byte(uint32_t byte)
{
	const uint32_t polynomial = 0xEDB88320L;
	uint32_t       result     = byte;
	size_t         i          = 0;

	for (; i < 8; i++) {
		/* IMPLEMENTATION: the code below always shifts result right by
		 * 1, but only XORs it by the polynomial if we're on the lowest
		 * bit.
		 *
		 * This is because 1 in binary is 00000001, so ANDing the
		 * result by 1 will always give 0 unless the lowest bit is set.
		 * And since XOR by zero does nothing, the other half only
		 * occurs when we're on the lowest bit.
		 *
		 * I didn't leave the above implementation in, despite being
		 * faster on my machine since it is a more complex operation
		 * which may be slower on less sophisticated processors. It can
		 * be added in in place of the loop code below.
		 */

		result = (result >> 1) ^ (result & 1) * polynomial;

		/* Here is the code I replaced with the branch I tried to
		 * remove:
		if (result & 1) {
			result = (result >> 1) ^ polynomial;
			continue;
		}
		result >>= 1;
		 */
	}
	return result;
}

uint32_t
crc32p(const void *input, size_t size, uint32_t state)
{
	const uint8_t *current = (const uint8_t *)input;
	uint32_t       result  = state;
	size_t         i       = 0;

	for (; i < size; i++) {
		result ^= current[i];
		result = crc32_for_byte(result);
	}

	return result;
}

uint32_t
crc32p_end(uint32_t state)
{
	return ~state;
}

uint32_t
crc32(const void *input, size_t size)
{
	return ~crc32p(input, size, CRC32_INIT);
}

#if defined(UNITTEST)
#include <assert.h>
UNITTEST()
{
	/* note: yes, it's is compatible with zlib.crc32 in python. */
	assert(crc32("", 0) == 0);
	assert(crc32("hello", 5) == 0x3610a686);
}
#endif
