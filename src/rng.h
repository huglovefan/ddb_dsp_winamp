#pragma once

#if defined(__GNUC__) && defined(__i386__)
#define RNG_REGPARM1 __attribute__((regparm(1)))
#else
#define RNG_REGPARM1
#endif

/* TODO: find non-chatgpt sources. */

/* chatgpt */
static inline uint64_t splitmix64_next(uint64_t *x)
{
	uint64_t z;

	z = (*x += UINT64_C(0x9e3779b97f4a7c15));
	z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
	z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);

	return z ^ (z >> 31);
}

/* chatgpt */
static inline uint32_t rotl32(uint32_t x, int k)
{
	return (x << k) | (x >> (32 - k));
}

/* chatgpt */
static inline void rng_seed(uint32_t s[4], uint64_t seed)
{
	uint64_t sm;
	uint64_t a;
	uint64_t b;

	sm = seed;
	a = splitmix64_next(&sm);
	b = splitmix64_next(&sm);

	s[0] = a;
	s[1] = (a >> 32);
	s[2] = b;
	s[3] = (b >> 32);

	if (!(s[0] | s[1] | s[2] | s[3]))
		s[0]++;
}

/* chatgpt */
static inline uint32_t RNG_REGPARM1 rng_next(uint32_t s[4])
{
	uint32_t result;
	uint32_t t;

	result = rotl32(s[1] * 5u, 7) * 9u;
	t = s[1] << 9;

	s[2] ^= s[0];
	s[3] ^= s[1];
	s[1] ^= s[2];
	s[0] ^= s[3];

	s[2] ^= t;
	s[3] = rotl32(s[3], 11);

	return result;
}

#undef RNG_REGPARM1
