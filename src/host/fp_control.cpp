#include "fp_control.hpp"

#include <stdio.h>
#include <xmmintrin.h>

static const char *setcl[] = {"cleared", "set"};

void fp_control_read(struct fp_control fpu)
{
	_mm_setcsr(fpu.sse);
	__asm__ __volatile__("fldcw %0" :: "m"(fpu.x87));
	__asm__ __volatile__("" ::: "memory");
}

void fp_control_write(struct fp_control *fpu)
{
	fpu->sse = _mm_getcsr();
	__asm__("fnstcw %0" : "=m"(fpu->x87));
	__asm__ __volatile__("" ::: "memory");
}

void log_fp_change(
	const fp_control *a,
	const fp_control *b,
	const char       *where,
	const wchar_t    *dllname)
{
	union mxcsr_bits_union before;
	union mxcsr_bits_union after;
	union x87_fpu_cw_bits_union oldx87;
	union x87_fpu_cw_bits_union newx87;

	before.i = a->sse;
	after.i = b->sse;
	oldx87.i = a->x87;
	newx87.i = b->x87;

#define PR(fmt, ...) printf("-%ls: %s: " fmt, dllname, where, ##__VA_ARGS__)

	if (before.bits.ie != after.bits.ie)
		PR("%s SSE Invalid Operation Flag\n", setcl[after.bits.ie]);
	if (before.bits.de != after.bits.de)
		PR("%s SSE Denormal Flag\n", setcl[after.bits.de]);
	if (before.bits.ze != after.bits.ze)
		PR("%s SSE Divide-By-Zero Flag\n", setcl[after.bits.ze]);
	if (before.bits.oe != after.bits.oe)
		PR("%s SSE Overflow Flag\n", setcl[after.bits.oe]);
	if (before.bits.ue != after.bits.ue)
		PR("%s SSE Underflow Flag\n", setcl[after.bits.ue]);
	if (before.bits.pe != after.bits.pe)
		PR("%s SSE Precision Flag\n", setcl[after.bits.pe]);

	if (before.bits.daz != after.bits.daz)
		PR("changed SSE Denormals Are Zero %u -> %u\n", before.bits.daz, after.bits.daz);

	if (before.bits.im != after.bits.im)
		PR("%s SSE Invalid Operation Mask\n", setcl[after.bits.im]);
	if (before.bits.dm != after.bits.dm)
		PR("%s SSE Denormal Mask\n", setcl[after.bits.dm]);
	if (before.bits.zm != after.bits.zm)
		PR("%s SSE Divide-By-Zero Mask\n", setcl[after.bits.zm]);
	if (before.bits.om != after.bits.om)
		PR("%s SSE Overflow Mask\n", setcl[after.bits.om]);
	if (before.bits.um != after.bits.um)
		PR("%s SSE Underflow Mask\n", setcl[after.bits.um]);
	if (before.bits.pm != after.bits.pm)
		PR("%s SSE Precision Mask\n", setcl[after.bits.pm]);

	if (before.bits.rc != after.bits.rc)
		PR("changed SSE Rounding Control %u -> %u\n", before.bits.rc, after.bits.rc);

	if (before.bits.fz != after.bits.fz)
		PR("changed SSE Flush To Zero %u -> %u\n", before.bits.fz, after.bits.fz);

	if (oldx87.bits.im != newx87.bits.im)
		PR("%s x87 Invalid Operation Mask\n", setcl[newx87.bits.im]);
	if (oldx87.bits.dm != newx87.bits.dm)
		PR("%s x87 Denormal Mask\n", setcl[newx87.bits.dm]);
	if (oldx87.bits.zm != newx87.bits.zm)
		PR("%s x87 Divide-By-Zero Mask\n", setcl[newx87.bits.zm]);
	if (oldx87.bits.om != newx87.bits.om)
		PR("%s x87 Overflow Mask\n", setcl[newx87.bits.om]);
	if (oldx87.bits.um != newx87.bits.um)
		PR("%s x87 Underflow Mask\n", setcl[newx87.bits.um]);
	if (oldx87.bits.pm != newx87.bits.pm)
		PR("%s x87 Precision Mask\n", setcl[newx87.bits.pm]);

	if (oldx87.bits.pc != newx87.bits.pc)
		PR("changed x87 Precision Control %u -> %u\n", oldx87.bits.pc, newx87.bits.pc);

	if (oldx87.bits.rc != newx87.bits.rc)
		PR("changed x87 Rounding Control %u -> %u\n", oldx87.bits.rc, newx87.bits.rc);

	if (oldx87.bits.ic != newx87.bits.ic)
		PR("changed x87 Infinity Control %u -> %u\n", oldx87.bits.ic, newx87.bits.ic);

#undef PR
}
