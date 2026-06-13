#pragma once

/* this file is based on deadbeef's fastftoi.h, which has the following
   header comment: */

// most of the code below was taken from libvorbis/vorbis/lib/os.h
// under the conditions below
/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE OggVorbis SOFTWARE CODEC SOURCE CODE.   *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS     *
 * GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE *
 * IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.       *
 *                                                                  *
 * THE OggVorbis SOURCE CODE IS (C) COPYRIGHT 1994-2009             *
 * by the Xiph.Org Foundation http://www.xiph.org/                  *
 *                                                                  *
 ********************************************************************

contents of the libvorbis COPYING:

Copyright (c) 2002-2008 Xiph.org Foundation

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of the Xiph.org Foundation nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <fenv.h>
#include <math.h>

#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))

/* there are three combinations:
   1. -m64          -> sse/sse2
   2. -m32 -msse    -> sse ftoi, x87 dtoll
   3. -m32 -mno-sse -> x87 both
   */

#include <xmmintrin.h>
#include <emmintrin.h>

typedef struct {
	unsigned int mxcsr;
	unsigned short x87;
} fpu_control;

/* _mm_cvt_ss2si() requires SSE or 64-bit */
#if defined(__SSE__) || defined(__x86_64__)
static inline int ftoi(float f)
{
	return _mm_cvt_ss2si(_mm_set_ss(f));
}
#else
static inline int ftoi(float f)
{
	int i;
	__asm__(
	    "fistpl %0"
	    : "=m"(i)
	    : "t"(f)
	    : "st");
	return i;
}
#endif

/* _mm_cvtsd_si64() requires 64-bit */
#if defined(__x86_64__)
static inline long long dtoll(double d)
{
	return _mm_cvtsd_si64(_mm_set_sd(d));
}
#else
static inline long long dtoll(double d)
{
	long long ll;
	__asm__(
	    "fistpq %0"
	    : "=m"(ll)
	    : "t"(d)
	    : "st");
	return ll;
}
#endif

static inline void fpu_setround(fpu_control *fpu)
{
	unsigned short tmp;
	(void)tmp;

/* SSE used */
#if defined(__SSE__) || defined(__x86_64__)
	fpu->mxcsr = _mm_getcsr();
	/* clear the rounding mode bits to set round-to-nearest */
	_mm_setcsr(fpu->mxcsr & ~0x6000u);
#endif

/* x87 used */
#if !defined(__x86_64__)
	__asm__("fnstcw %0" : "=m"(fpu->x87));
	/* clear the rounding mode bits to set round-to-nearest */
	tmp = fpu->x87 & ~0x0c00u;
	__asm__ volatile("fldcw %0" : : "m"(tmp));
#endif

	__asm__ volatile("" ::: "memory");
}

static inline void fpu_restore(fpu_control fpu)
{
/* SSE used */
#if defined(__SSE__) || defined(__x86_64__)
	_mm_setcsr(fpu.mxcsr);
#endif

/* x87 used */
#if !defined(__x86_64__)
	__asm__ volatile("fldcw %0" : : "m"(fpu.x87));
#endif

	__asm__ volatile("" ::: "memory");
}

#elif defined(__GNUC__) && defined(__aarch64__) && defined(__ARM_NEON)

#include <arm_neon.h>

typedef struct {
	char _unused_;
} fpu_control;

static inline int ftoi(float f)
{
	return vcvtns_s32_f32(f);
}

static inline long long dtoll(double d)
{
	float64x1_t fvec = vdup_n_f64(d);
	int64x1_t ivec = vcvtn_s64_f64(fvec);
	return vget_lane_s64(ivec, 0);
}

static inline void fpu_setround(fpu_control *fpu)
{
}

static inline void fpu_restore(fpu_control fpu)
{
}

#else /* fallback */

typedef struct {
	int rounding_mode;
} fpu_control;

static inline int ftoi(float f)
{
	return (int)lrintf(f);
}

static inline long long dtoll(double d)
{
	return llrint(d);
}

static inline void fpu_setround(fpu_control *fpu)
{
	fpu->rounding_mode = fegetround();
	fesetround(FE_TONEAREST);
}

static inline void fpu_restore(fpu_control fpu)
{
	fesetround(fpu.rounding_mode);
}

#endif
