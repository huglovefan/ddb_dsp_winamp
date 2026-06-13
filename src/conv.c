#include "conv.h"

#include <assert.h>
#include <string.h>
#include <inttypes.h>
#include "float.h"
#include "rng.h"

/* n <= bits in INT_MAX */
#define MASK(n) ( (1u << (n)) - 1 )

#define X_INT_MAX32(bits) ((int32_t)((UINT32_C(1) << (bits-1))-1))
#define X_INT_MIN32(bits) (-X_INT_MAX32(bits) - 1)

#if defined(__GNUC__)
# define likely(x) (__builtin_expect(!!(x), 1))
# define unlikely(x) (__builtin_expect(!!(x), 0))
#else
# define likely(x) (!!(x))
# define unlikely(x) (!!(x))
#endif

#if defined(__GNUC__) && defined(__i386__)
# define REGPARM __attribute__((regparm(3),optimize("-fomit-frame-pointer","-fno-stack-protector")))
#else
# define REGPARM
#endif

#if !defined(__GNUC__)
/* https://stackoverflow.com/a/69589530 */
static bool __builtin_add_overflow(int32_t a, int32_t b, int32_t *r)
{
	int32_t sum;

	sum = (uint32_t)a + b;
	if (a >= 0 ? sum < b : sum > b)
		return true;
	*r = a + b;
	return false;
}
#endif

static inline int32_t min_s32(int32_t a, int32_t b)
{
	if (unlikely(b < a))
		a = b;

	return a;
}

static inline int32_t max_s32(int32_t a, int32_t b)
{
	if (unlikely(b > a))
		a = b;

	return a;
}

enum
{
	S8 = SFMT_S8,
	S16 = SFMT_S16,
	S24 = SFMT_S24,
	S32 = SFMT_S32,
	F32 = SFMT_F32
};

struct conv_state
{
	void       *dst;
	const void *src;
	size_t      count;
	uint32_t    rng[4];
};

/*
Get a random value of lostbits+1 bits.
For example, lostbits=16 -> [-0xffff,0xffff].
The values have a triangular distribution - if you plotted it, it would
look like a pyramid.
*/
static int32_t triangle_dither_bits(
	struct conv_state *conv,
	int                lostbits)
{
	uint32_t r;
	int32_t d;

	r = rng_next(conv->rng);
	d = r & MASK(lostbits);

	if (lostbits > 16)
		r = rng_next(conv->rng);
	else
		r >>= lostbits;

	d -= r & MASK(lostbits);

	return d;
}

static inline int32_t downsample(
	int32_t            samp,
	int                frombits,
	int                tobits,
	struct conv_state *conv)
{
	int lostbits;
	int32_t t, res;

	lostbits = frombits-tobits;

	/* if we're converting from a size smaller than 32 bits, there's
	   no risk of overflowing the int32_t. */
	if (frombits < 32)
	{
		/* mix in randomness. */
		samp += triangle_dither_bits(conv, lostbits);

		/* add this to avoid a DC bias. TODO: explain. */
		/* TODO: i don't think this rounds to even. but that
		   would be better so that it matches the float
		   conversion. */
		samp += (1 << (lostbits-1)) - (samp < 0);
	}
	else
	{
		/* the same steps, just with overflow checks. */

		t = triangle_dither_bits(conv, lostbits);
		if (unlikely(__builtin_add_overflow(samp, t, &res)))
			return (samp >= 0)
			    ? X_INT_MAX32(tobits)
			    : X_INT_MIN32(tobits);
		samp = res;

		t = (1 << (lostbits-1)) - (samp < 0);
		if (unlikely(__builtin_add_overflow(samp, t, &res)))
			return (samp >= 0)
			    ? X_INT_MAX32(tobits)
			    : X_INT_MIN32(tobits);
		samp = res;
	}

	/* scale to destination type. */
	samp >>= lostbits;

	samp = min_s32(samp, X_INT_MAX32(tobits));
	samp = max_s32(samp, X_INT_MIN32(tobits));

	return samp;
}

static inline int32_t upsample(int32_t samp, int frombits, int tobits)
{
	return samp * (1 << (tobits-frombits));
}

/* mingw has an inline implementation of fabsf that uses x87 - replace
   it with this version that uses sse instead. */
/* chatgpt or stackoverflow? did not write this myself. */
#if defined(__MINGW32__) && defined(__i386__) && defined(__SSE__)
static inline float my_fabsf(float x)
{
	__m128 vx = _mm_load_ss(&x);
	__m128 mask = _mm_set_ss(-0.0f);
	__m128 abs_v = _mm_andnot_ps(mask, vx);
	_mm_store_ss(&x, abs_v);
	return x;
}
#else
#define my_fabsf fabsf
#endif

/*
Get a random float in the range [0, 1).
*/
/* chatgpt */
static float rng_next_float(struct conv_state *conv)
{
	/* note: this specifically uses the high bits of the rng value,
	   but i'm not sure it makes a difference (does it?) */
	return (rng_next(conv->rng) >> 8) * (1.0f / 16777216.0f);
}

/*
Get a random float in the range (-1, 1) with triangular distribution.
*/
static float triangle_dither_float(struct conv_state *conv)
{
	float f;

	f = rng_next_float(conv);
	f -= rng_next_float(conv);

	return f;
}

#define my_isnan(x) ((x) != (x))

static inline int32_t float2int(
	float              samp,
	int                bits,
	struct conv_state *conv)
{
	int64_t s64;
	int32_t s32;
	float scale;

	/* clamp to [-1, 1], set NAN to 0.0f. */
	if (unlikely(!(my_fabsf(samp) <= 1.0f)))
	{
		if (likely(!my_isnan(samp)))
			samp = copysignf(1.0f, samp);
		else
			samp = 0.0f;
	}

	/* bits 32 and 24: even though the input sample was clamped,
	   the output still has to be checked against the max+1 value
	   because of 1.0f and other values that give the same result
	   after rounding. */

	/* bits 32: this case needs to use dtoll (which has a 64-bit
	   result type) to avoid platform-dependent overflow behavior
	   on the max+1 value. */

	scale = (float)(1u << (bits-1));
	if (bits == 32)
	{
		s64 = dtoll(samp * scale);
		if (s64 == 0x80000000)
			s64--;
		return s64;
	}
	else if (bits == 24)
	{
		s32 = ftoi(samp * scale);
		if (s32 == 0x800000)
			s32--;
		return s32;
	}
	else
	{
		/* float to 16 and 8 uses dithering. because of this,
		   they also need to clamp the output value. */

		/* the dither float is (-1, 1), so it's added after
		   scaling. */

		s32 = ftoi(samp * scale + triangle_dither_float(conv));

		//~ if (s32 > X_INT_MAX32(bits))
			//~ s32 = X_INT_MAX32(bits);
		//~ if (s32 < X_INT_MIN32(bits))
			//~ s32 = X_INT_MIN32(bits);

		s32 = min_s32(s32, X_INT_MAX32(bits));
		s32 = max_s32(s32, X_INT_MIN32(bits));

		return s32;
	}
}

static inline float int2float(int32_t samp, int bits)
{
	return samp / ldexpf(1.0f, bits-1);
}

static int32_t read_s24(const char *buf)
{
	union { char b[4]; int32_t i; } u;
	u.i = 0;
	memcpy(&u.b[1], buf, 3);
	return (u.i >> 8);
}

static void write_s24(char *buf, int32_t val)
{
	union { char b[4]; int32_t i; } u;
	u.i = val;
	memcpy(buf, u.b, 3);
}

#define X(FNAME, DTYPE, STMT) \
static void REGPARM conv_##FNAME( \
	const void        *src_, \
	void              *dst_, \
	size_t             count, \
	struct conv_state *conv) \
{ \
	const STYPE *src; \
	DTYPE *dst; \
	size_t i; \
 \
	src = (const STYPE *)src_; \
	dst = (DTYPE *)dst_; \
 \
	i = count-1; do { STMT; } while (i--); \
}

#define STYPE float
X(f32_s32, int32_t, (dst[i] = float2int(src[i], 32, conv)))
X(f32_s24, char,    write_s24(&dst[i*3], float2int(src[i], 24, conv)))
X(f32_s16, int16_t, (dst[i] = float2int(src[i], 16, conv)))
X(f32_s8,  int8_t,  (dst[i] = float2int(src[i], 8, conv)))
#undef STYPE

#define STYPE int32_t
X(s32_f32, float,   (dst[i] = int2float(src[i], 32)))
X(s32_s24, char,    write_s24(&dst[i*3], downsample(src[i], 32, 24, conv)))
X(s32_s16, int16_t, (dst[i] = downsample(src[i], 32, 16, conv)))
X(s32_s8,  int8_t,  (dst[i] = downsample(src[i], 32, 8, conv)))
#undef STYPE

#define STYPE char
X(s24_f32, float,   (dst[i] = int2float(read_s24(&src[i*3]), 24)))
X(s24_s32, int32_t, (dst[i] = upsample(read_s24(&src[i*3]), 24, 32)))
X(s24_s16, int16_t, (dst[i] = downsample(read_s24(&src[i*3]), 24, 16, conv)))
X(s24_s8,  int8_t,  (dst[i] = downsample(read_s24(&src[i*3]), 24, 8, conv)))
#undef STYPE

#define STYPE int16_t
X(s16_f32, float,   (dst[i] = int2float(src[i], 16)))
X(s16_s32, int32_t, (dst[i] = upsample(src[i], 16, 32)))
X(s16_s24, char,    write_s24(&dst[i*3], upsample(src[i], 16, 24)))
X(s16_s8,  int8_t,  (dst[i] = downsample(src[i], 16, 8, conv)))
#undef STYPE

#define STYPE int8_t
X(s8_f32,  float,   (dst[i] = int2float(src[i], 8)))
X(s8_s32,  int32_t, (dst[i] = upsample(src[i], 8, 32)))
X(s8_s24,  char,    write_s24(&dst[i*3], upsample(src[i], 8, 24)))
X(s8_s16,  int16_t, (dst[i] = upsample(src[i], 8, 16)))
#undef STYPE

#undef X

/* work in progress!!! would like to make this faster and add other conversions like it */
/* next would like to do f32->s24 (i realized that's the one i need, not this) */
#define TEST_USE_NEW_F32_S32 1
#if TEST_USE_NEW_F32_S32
__attribute__((no_sanitize("undefined")))
__attribute__((optimize("-fwrapv")))
static void REGPARM conv_f32_s32_inner(const float *f_in, int32_t *i_out, size_t cnt)
{
	float mul;
	size_t i;

	mul = 2147483648.0f;

	if (!cnt)
		__builtin_unreachable();

	for (i = 0; i != cnt; i++)
	{
		i_out[i] = _mm_cvt_ss2si(_mm_set_ss(f_in[i] * mul));

		if (unlikely(i_out[i] == INT32_MIN))
			i_out[i] -= !(((const uint32_t *)f_in)[i] & 0x80000000);
	}
}
#if defined(UNITTEST)
__attribute__((no_sanitize("address")))
static int32_t float2int32(float f)
{
	int32_t rv;
	conv_f32_s32_inner(&f, &rv, 1);
	return rv;
}
#endif
static void REGPARM conv_f32_s32_new(
	const void        *src_,
	void              *dst_,
	size_t             count,
	struct conv_state *conv)
{
	const float *src;
	int32_t *dst;

	src = (const float *)src_;
	dst = (int32_t *)dst_;
	(void)conv;

	conv_f32_s32_inner(src, dst, count);
}
#define conv_f32_s32 conv_f32_s32_new
#endif

typedef void (*REGPARM conv_func)(
    const void        *src,
    void              *dst,
    size_t             count,
    struct conv_state *conv);

struct conv_info
{
	conv_func fn;
	bool fpu;
	bool rng;
};

/* note: the minus one skips SFMT_INVALID. */
#define FMTS(a, b) ((a-1)*(SFMT_COUNT-1) + (b-1))

#define SFMT_VALID_COUNT (SFMT_COUNT-1)
#define CONV_FUNCS_COUNT (SFMT_VALID_COUNT*SFMT_VALID_COUNT)

static const struct conv_info conv_funcs[CONV_FUNCS_COUNT] = {
	[FMTS( S8,  S8)] = {0},
	[FMTS( S8, S16)] = {.fn=conv_s8_s16,  .fpu=0, .rng=0},
	[FMTS( S8, S24)] = {.fn=conv_s8_s24,  .fpu=0, .rng=0},
	[FMTS( S8, S32)] = {.fn=conv_s8_s32,  .fpu=0, .rng=0},
	[FMTS( S8, F32)] = {.fn=conv_s8_f32,  .fpu=1, .rng=0},

	[FMTS(S16,  S8)] = {.fn=conv_s16_s8,  .fpu=0, .rng=1},
	[FMTS(S16, S16)] = {0},
	[FMTS(S16, S24)] = {.fn=conv_s16_s24, .fpu=0, .rng=0},
	[FMTS(S16, S32)] = {.fn=conv_s16_s32, .fpu=0, .rng=0},
	[FMTS(S16, F32)] = {.fn=conv_s16_f32, .fpu=1, .rng=0},

	[FMTS(S24,  S8)] = {.fn=conv_s24_s8,  .fpu=0, .rng=1},
	[FMTS(S24, S16)] = {.fn=conv_s24_s16, .fpu=0, .rng=1},
	[FMTS(S24, S24)] = {0},
	[FMTS(S24, S32)] = {.fn=conv_s24_s32, .fpu=0, .rng=0},
	[FMTS(S24, F32)] = {.fn=conv_s24_f32, .fpu=1, .rng=0},

	[FMTS(S32,  S8)] = {.fn=conv_s32_s8,  .fpu=0, .rng=1},
	[FMTS(S32, S16)] = {.fn=conv_s32_s16, .fpu=0, .rng=1},
	[FMTS(S32, S24)] = {.fn=conv_s32_s24, .fpu=0, .rng=1},
	[FMTS(S32, S32)] = {0},
	[FMTS(S32, F32)] = {.fn=conv_s32_f32, .fpu=1, .rng=0},

	[FMTS(F32,  S8)] = {.fn=conv_f32_s8,  .fpu=1, .rng=1},
	[FMTS(F32, S16)] = {.fn=conv_f32_s16, .fpu=1, .rng=1},
	[FMTS(F32, S24)] = {.fn=conv_f32_s24, .fpu=1, .rng=0},
	[FMTS(F32, S32)] = {.fn=conv_f32_s32, .fpu=1, .rng=0},
	[FMTS(F32, F32)] = {0},
};

void conv_bits(struct conv_request req)
{
	struct conv_state conv;
	const struct conv_info *in;
	fpu_control fpu;

	const void *src = req.src;
	void       *dst = req.dst;
	size_t      count = req.count;
	SFMT        from = req.from;
	SFMT        to = req.to;

	assert(src);
	assert(dst);
	assert(from != 0 && from < SFMT_COUNT);
	assert(count <= SIZE_MAX/sfmt_bytes(from));
	assert(to != 0 && to < SFMT_COUNT);
	assert(count <= SIZE_MAX/sfmt_bytes(to));
	/* FIXME: why do these fail in host? seems to play fine. */
	/* i guess s24 has no alignment requirement since we access it
	   using memcpy. */
	if (from != S24)
		assert((uintptr_t)src % sfmt_bytes(from) == 0);
	if (to != S24)
		assert((uintptr_t)dst % sfmt_bytes(to) == 0);

	if (!count)
		return;

	if (to == from)
	{
		memcpy(dst, src, sfmt_bytes_n(from, count));
		return;
	}

	memset(&conv, 0, sizeof(conv));
	conv.src = src;
	conv.dst = dst;
	conv.count = count;

	in = &conv_funcs[FMTS(from, to)];

	if (in->fpu)
		fpu_setround(&fpu);
	if (in->rng)
	{
#if defined(__GNUC__)
		req.entropy ^= __builtin_ia32_rdtsc();
#else
		/* TODO: tcc can use inline asm */
		/* TODO: anything better to put here that isn't a
		   syscall? */
		req.entropy ^= (uintptr_t)&req;
#endif
		/* if no dithering is requested, just skip seeding the
		   rng - this makes it always return zero. i found this
		   out by accident. */
		if (!req.no_dither)
			rng_seed(conv.rng, req.entropy);
	}

	in->fn(src, dst, count, &conv);

	if (in->fpu)
		fpu_restore(fpu);
}

#if defined(UNITTEST)
#include <stdio.h>
#define float2int(a,b) float2int(a,b,&conv)
UNITTEST()
{
	float f32;
	int32_t s32;
	int16_t s16;
	int8_t s8;
	bool fail;
	struct conv_state conv = {0};
	enum { S32_LIMIT = 0x100000 };

	fail = false;

	/* float can fit all values of s8, s16 and s24. this isn't true
	   for s32, so its tests look a little different.
	   */

	for (s8 = INT8_MAX; /* empty */; s8--)
	{
		int8_t t;
		f32 = int2float(s8, 8);
		t = float2int(f32, 8);
		if (t != s8)
		{
			printf(
			    "E: %" PRIi8 " -> %f -> %" PRIi8 "\n",
			    s8, f32, t);
			fail = true;
			break;
		}
		if (s8 == INT8_MIN)
			break;
	}
	for (s16 = INT16_MAX; /* empty */; s16--)
	{
		int16_t t;
		f32 = int2float(s16, 16);
		t = float2int(f32, 16);
		if (t != s16)
		{
			printf(
			    "E: %" PRIi16 " -> %f -> %" PRIi16 "\n",
			    s16, f32, t);
			fail = true;
			break;
		}
		if (s16 == INT16_MIN)
			break;
	}
	for (s32 = 0x7fffff; /* empty */; s32--)
	{
		int32_t t;
		f32 = int2float(s32, 24);
		t = float2int(f32, 24);
		if (t != s32)
		{
			printf(
			    "E: %" PRIi32 " -> %f -> %" PRIi32 "\n",
			    s32, f32, t);
			fail = true;
			break;
		}
		if (s32 == -0x800000)
			break;
	}

	/* test near limits of float/s32 conversion. */
	/* can't check the exact values, but test for sign flips. */

	for (s32 = INT32_MIN; /* empty */; s32++)
	{
		int32_t t;
		f32 = int2float(s32, 32);
		t = float2int(f32, 32);
		if ((t < 0.0f) != (s32 < 0))
		{
			printf(
			    "E: %" PRIi32 " -> %f -> %" PRIi32 "\n",
			    s32, f32, t);
			fail = true;
			break;
		}
		if (s32 == INT32_MIN+S32_LIMIT)
			break;
	}
	for (s32 = INT32_MAX-S32_LIMIT; /* empty */; s32++)
	{
		int32_t t;
		f32 = int2float(s32, 32);
		t = float2int(f32, 32);
		if ((t < 0.0f) != (s32 < 0))
		{
			printf(
			    "E: %" PRIi32 " -> %f -> %" PRIi32 "\n",
			    s32, f32, t);
			fail = true;
			break;
		}
		if (s32 == INT32_MAX)
			break;
	}

	assert(!fail);
}
UNITTEST()
{
	struct conv_state conv = {0};

	float bigf = nextafterf(1.0f, 0.0f);
	float smlf = nextafterf(-1.0f, 0.0f);

	assert(float2int( 1.5f, 32) == INT32_MAX);
	assert(float2int( 1.0f, 32) == INT32_MAX);
	assert(float2int( bigf, 32) < INT32_MAX);
	assert(float2int( bigf, 32) > 0x7fffff00); /* wiggle room */
	assert(float2int( 0.0f, 32) == 0);
	assert(float2int( smlf, 32) > INT32_MIN);
	assert(float2int( smlf, 32) < -0x7fffff00); /* wiggle room */
	assert(float2int(-1.0f, 32) == INT32_MIN);
	assert(float2int(-1.5f, 32) == INT32_MIN);

#if TEST_USE_NEW_F32_S32
	//~ printf("0.0 -> %d\n", float2int32(0.0f));
	//~ printf("0.1 -> %d\n", float2int32(0.1f));
	//~ printf("0.5 -> %d\n", float2int32(0.5f));
	//~ printf("1.0 -> %d\n", float2int32(1.0f));
	//~ printf("2.0 -> %d\n", float2int32(2.0f));
	//~ printf("3.0 -> %d\n", float2int32(3.0f));
	assert(float2int32( 1.5f) == INT32_MAX);
	assert(float2int32( 1.0f) == INT32_MAX);
	assert(float2int32( bigf) < INT32_MAX);
	assert(float2int32( bigf) > 0x7fffff00); /* wiggle room */
	assert(float2int32( 0.0f) == 0);
	assert(float2int32( smlf) > INT32_MIN);
	assert(float2int32( smlf) < -0x7fffff00); /* wiggle room */
	assert(float2int32(-1.0f) == INT32_MIN);
	assert(float2int32(-1.5f) == INT32_MIN);
#endif

	assert(float2int( 1.5f, 24) == 0x7fffff);
	assert(float2int( 1.0f, 24) == 0x7fffff);
	assert(float2int( bigf, 24) == 0x7fffff);
	assert(float2int( smlf, 24) == -0x800000);
	assert(float2int(-1.0f, 24) == -0x800000);
	assert(float2int(-1.5f, 24) == -0x800000);

	assert(float2int( 1.5f, 16) == INT16_MAX);
	assert(float2int( 1.0f, 16) == INT16_MAX);
	assert(float2int( bigf, 16) == INT16_MAX);
	assert(float2int( smlf, 16) == INT16_MIN);
	assert(float2int(-1.0f, 16) == INT16_MIN);
	assert(float2int(-1.5f, 16) == INT16_MIN);

	assert(float2int( 1.5f,  8) == INT8_MAX);
	assert(float2int( 1.0f,  8) == INT8_MAX);
	assert(float2int( bigf,  8) == INT8_MAX);
	assert(float2int( smlf,  8) == INT8_MIN);
	assert(float2int(-1.0f,  8) == INT8_MIN);
	assert(float2int(-1.5f,  8) == INT8_MIN);

	/* it's unlikely these would ever matter */

	assert(float2int(NAN, 32) == 0);
	assert(float2int(NAN, 16) == 0);
	assert(float2int(NAN, 24) == 0);
	assert(float2int(NAN, 8) == 0);

	assert(float2int(INFINITY, 32) == INT32_MAX);
	assert(float2int(INFINITY, 24) == 0x7fffff);
	assert(float2int(INFINITY, 16) == INT16_MAX);
	assert(float2int(INFINITY,  8) == INT8_MAX);

	assert(float2int(-INFINITY, 32) == INT32_MIN);
	assert(float2int(-INFINITY, 24) == -0x800000);
	assert(float2int(-INFINITY, 16) == INT16_MIN);
	assert(float2int(-INFINITY,  8) == INT8_MIN);
}
#endif
