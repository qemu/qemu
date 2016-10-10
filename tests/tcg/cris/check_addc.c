#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "sys.h"
#include "crisutils.h"

static always_inline int cris_addc(int a, const int b)
{
	asm ("addc\t%1, %0\n" : "+r" (a) : "r" (b));
	return a;
}

#define verify_addc(a, b, res, n, z, v, c)  \
{                                           \
	int r;                              \
	r = cris_addc((a), (b));            \
	cris_tst_cc((n), (z), (v), (c));    \
	if (r != (res))                     \
		err();                      \
}

int main(void)
{
	cris_tst_cc_init();
	asm volatile ("clearf cz");
	verify_addc(0, 0, 0, 0, 0, 0, 0);

	cris_tst_cc_init();
	asm volatile ("setf z");
	verify_addc(0, 0, 0, 0, 1, 0, 0);

	cris_tst_cc_init();
	asm volatile ("setf cz");
	verify_addc(0, 0, 1, 0, 0, 0, 0);
	cris_tst_cc_init();
	asm volatile ("clearf c");
	verify_addc(-1, 2, 1, 0, 0, 0, 1);

	cris_tst_cc_init();
	asm volatile ("clearf nzv");
	asm volatile ("setf c");
	verify_addc(-1, 2, 2, 0, 0, 0, 1);

	cris_tst_cc_init();
	asm volatile ("setf c");
	verify_addc(0xffff, 0xffff, 0x1ffff, 0, 0, 0, 0);

	cris_tst_cc_init();
	asm volatile ("clearf nzvc");
	verify_addc(-1, -1, 0xfffffffe, 1, 0, 0, 1);

	cris_tst_cc_init();
	asm volatile ("setf c");
	verify_addc(0x78134452, 0x5432f789, 0xcc463bdc, 1, 0, 1, 0);

	pass();
	return 0;
}
