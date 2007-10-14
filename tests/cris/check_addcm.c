#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "sys.h"
#include "crisutils.h"

/* need to avoid acr as source here.  */
extern inline int cris_addc_m(int a, const int *b) {
	asm volatile ("addc [%1], %0\n" : "+r" (a) : "r" (b));
	return a;
}

/* 'b' is a crisv32 constrain to avoid postinc with $acr.  */
extern inline int cris_addc_pi_m(int a, int **b) {
	asm volatile ("addc [%1+], %0\n" : "+r" (a), "+b" (*b));
	return a;
}

#define verify_addc_m(a, b, res, n, z, v, c)  \
{                                           \
	int r;                              \
	r = cris_addc_m((a), (b));            \
	cris_tst_cc((n), (z), (v), (c));    \
	if (r != (res))                     \
		err();                      \
}

#define verify_addc_pi_m(a, b, res, n, z, v, c)  \
{                                           \
	int r;                              \
	r = cris_addc_pi_m((a), (b));            \
	cris_tst_cc((n), (z), (v), (c));    \
	if (r != (res))                     \
		err();                      \
}

int x[] = { 0, 0, 2, -1, 0xffff, -1, 0x5432f789};

int main(void)
{
	int *p = (void *)&x[0];
#if 1
	cris_tst_cc_init();
	asm volatile ("clearf cz");
	verify_addc_m(0, p, 0, 0, 0, 0, 0);

	cris_tst_cc_init();
	asm volatile ("setf z");
	verify_addc_m(0, p, 0, 0, 1, 0, 0);

	cris_tst_cc_init();
	asm volatile ("setf c");
	verify_addc_m(0, p, 1, 0, 0, 0, 0);

	cris_tst_cc_init();
	asm volatile ("clearf c");
	verify_addc_pi_m(0, &p, 0, 0, 1, 0, 0);

	p = &x[1];
	cris_tst_cc_init();
	asm volatile ("setf c");
	verify_addc_pi_m(0, &p, 1, 0, 0, 0, 0);

	if (p != &x[2])
		err();

	cris_tst_cc_init();
	asm volatile ("clearf c");
	verify_addc_pi_m(-1, &p, 1, 0, 0, 0, 1);

	if (p != &x[3])
		err();
#endif
	p = &x[3];
	/* TODO: investigate why this one fails.  */
	cris_tst_cc_init();
	asm volatile ("setf c");
	verify_addc_m(2, p, 2, 0, 0, 0, 1);
	p += 4;

	pass();
	return 0;
}
