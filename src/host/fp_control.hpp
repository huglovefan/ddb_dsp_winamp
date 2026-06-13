#pragma once

/* see: /usr/include/fpu_control.h */
struct x87_fpu_cw_bits
{
	unsigned short im : 1;
	unsigned short dm : 1;
	unsigned short zm : 1;
	unsigned short om : 1;
	unsigned short um : 1;
	unsigned short pm : 1;
	unsigned short _reserved_1_ : 2;

	unsigned short pc : 2; /* 0=float 2=double 3=real */
	unsigned short rc : 2; /* 0=near 1=dn 2=up 3=zero */
	unsigned short ic : 1; /* infinity control, unused */
	unsigned short _reserved_2_ : 3;
};

union x87_fpu_cw_bits_union
{
	unsigned short i;
	struct x87_fpu_cw_bits bits;
};

/* https://gist.github.com/moyix/5bac4b2e383a466b7d015b8c04db13b5#file-ensure_fpu-py-L50
   */
struct mxcsr_bits
{
	unsigned int ie : 1;
	unsigned int de : 1;
	unsigned int ze : 1;
	unsigned int oe : 1;
	unsigned int ue : 1;
	unsigned int pe : 1;
	unsigned int daz : 1; /* denormals are zero */
	unsigned int im : 1;

	unsigned int dm : 1;
	unsigned int zm : 1;
	unsigned int om : 1;
	unsigned int um : 1;
	unsigned int pm : 1;
	unsigned int rc : 2; /* 0=near 1=dn 2=up 3=zero */
	unsigned int fz : 1; /* flush to zero */

	unsigned int _reserved_ : 16;
};

union mxcsr_bits_union
{
	unsigned int i;
	struct mxcsr_bits bits;
};

struct fp_control
{
	unsigned int sse;
	unsigned short x87;
};

void fp_control_read(struct fp_control fpu);
void fp_control_write(struct fp_control *fpu);

void log_fp_change(
    const fp_control *a,
    const fp_control *b,
    const char       *where,
    const wchar_t    *dllname);
