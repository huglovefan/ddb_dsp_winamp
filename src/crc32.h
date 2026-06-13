/* SPDX-License-Identifier: CC0-1.0 */
/*
 *    crc32.h - calculate crc32 checksums of data
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
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define CRC32_INIT 0xFFFFFFFF

uint32_t
crc32p(const void *input, size_t size, uint32_t state);

uint32_t
crc32p_end(uint32_t state);

/* Calculate crc32 checksum of size_t size bytes of const void *input and
 * return the result.
 *
 * No lookup tables are used, its simply a byte by byte crc32 algorithm.
 */
uint32_t
crc32(const void *input, size_t size);

#if defined(__cplusplus)
}
#endif
