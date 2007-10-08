#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "sys.h"
#include "crisutils.h"

extern inline int cris_abs(int n) {
	int r;
	asm ("abs\t%1, %0\n" : "=r" (r) : "r" (n));
	return r;
}

extern inline void
verify_abs(int val, int res,
	   const int n, const int z, const int v, const int c)
{
	int r;

	cris_tst_cc_init();
	r = cris_abs(val);
	cris_tst_cc(n, z, v, c);
	if (r != res)
		err();
}

int main(void)
{
	verify_abs(-1, 1, 0, 0, 0, 0);
	verify_abs(0x80000000, 0x80000000, 1, 0, 0, 0);
	verify_abs(0x7fffffff, 0x7fffffff, 0, 0, 0, 0);
	verify_abs(42, 42, 0, 0, 0, 0);
	verify_abs(1, 1, 0, 0, 0, 0);
	verify_abs(0xffff, 0xffff, 0, 0, 0, 0);
	verify_abs(0xffff, 0xffff, 0, 0, 0, 0);
	verify_abs(-31, 0x1f, 0, 0, 0, 0);
	verify_abs(0, 0, 0, 1, 0, 0);
	pass();
	return 0;
}
