#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "sys.h"
#include "crisutils.h"

extern inline int cris_bound_b(int v, int b) {
	int r = v;
	asm ("bound.b\t%1, %0\n" : "+r" (r) : "ri" (b));
	return r;
}

extern inline int cris_bound_w(int v, int b) {
	int r = v;
	asm ("bound.w\t%1, %0\n" : "+r" (r) : "ri" (b));
	return r;
}

extern inline int cris_bound_d(int v, int b) {
	int r = v;
	asm ("bound.d\t%1, %0\n" : "+r" (r) : "ri" (b));
	return r;
}

int main(void)
{
	int r;

	cris_tst_cc_init();
	r = cris_bound_d(-1, 2);
	cris_tst_cc(0, 0, 0, 0);
	if (r != 2)
		err();

	cris_tst_cc_init();
	r = cris_bound_d(2, 0xffffffff);
	cris_tst_cc(0, 0, 0, 0);
	if (r != 2)
		err();

	cris_tst_cc_init();
	r = cris_bound_d(0xffff, 0xffff);
	cris_tst_cc(0, 0, 0, 0);
	if (r != 0xffff)
		err();

	cris_tst_cc_init();
	r = cris_bound_d(-1, 0xffffffff);
	cris_tst_cc(1, 0, 0, 0);
	if (r != 0xffffffff)
		err();

	cris_tst_cc_init();
	r = cris_bound_d(0x78134452, 0x5432f789);
	cris_tst_cc(0, 0, 0, 0);
	if (r != 0x5432f789)
		err();

	cris_tst_cc_init();
	r = cris_bound_w(-1, 2);
	cris_tst_cc(0, 0, 0, 0);
	if (r != 2)
		err();

	cris_tst_cc_init();
	r = cris_bound_w(-1, 0xffff);
	cris_tst_cc(0, 0, 0, 0);
	if (r != 0xffff)
		err();

	cris_tst_cc_init();
	r = cris_bound_w(2, 0xffff);
	cris_tst_cc(0, 0, 0, 0);
	if (r != 2)
		err();

	cris_tst_cc_init();
	r = cris_bound_w(0xfedaffff, 0xffff);
	cris_tst_cc(0, 0, 0, 0);
	if (r != 0xffff)
		err();

	cris_tst_cc_init();
	r = cris_bound_w(0x78134452, 0xf789);
	cris_tst_cc(0, 0, 0, 0);
	if (r != 0xf789)
		err();

	cris_tst_cc_init();
	r = cris_bound_b(-1, 2);
	cris_tst_cc(0, 0, 0, 0);
	if (r != 2)
		err();

	cris_tst_cc_init();
	r = cris_bound_b(2, 0xff);
	cris_tst_cc(0, 0, 0, 0);
	if (r != 2)
		err();

	cris_tst_cc_init();
	r = cris_bound_b(-1, 0xff);
	cris_tst_cc(0, 0, 0, 0);
	if (r != 0xff)
		err();

	cris_tst_cc_init();
	r = cris_bound_b(0xff, 0xff);
	cris_tst_cc(0, 0, 0, 0);
	if (r != 0xff)
		err();

	cris_tst_cc_init();
	r = cris_bound_b(0xfeda49ff, 0xff);
	cris_tst_cc(0, 0, 0, 0);
	if (r != 0xff)
		err();

	cris_tst_cc_init();
	r = cris_bound_b(0x78134452, 0x89);
	cris_tst_cc(0, 0, 0, 0);
	if (r != 0x89)
		err();

	cris_tst_cc_init();
	r = cris_bound_w(0x78134452, 0);
	cris_tst_cc(0, 1, 0, 0);
	if (r != 0)
		err();

	cris_tst_cc_init();
	r = cris_bound_b(0xffff, -1);
	cris_tst_cc(0, 0, 0, 0);
	if (r != 0xff)
		err();

	pass();
	return 0;
}
