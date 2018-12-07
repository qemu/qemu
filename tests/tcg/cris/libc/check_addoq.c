#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "sys.h"
#include "crisutils.h"

/* this would be better to do in asm, it's an orgy in GCC inline asm now.  */

/* ACR will be clobbered.  */
#define cris_addoq(o, v) \
	asm volatile ("addoq\t%1, %0, $acr\n" : : "r" (v), "i" (o) : "acr");


int main(void)
{
	int x[3] = {0x55aa77ff, 0xccff2244, 0x88ccee19};
	int *p, *t = x + 1;

	cris_tst_cc_init();
	asm volatile ("setf\tzvnc\n");
	cris_addoq(0, t);
	cris_tst_cc(1, 1, 1, 1);
	asm volatile ("move.d\t$acr, %0\n" : "=r" (p));
	if (*p != 0xccff2244)
		err();

	cris_tst_cc_init();
	asm volatile ("setf\tzvnc\n");
	cris_addoq(4, t);
	cris_tst_cc(0, 0, 0, 0);
	asm volatile ("move.d\t$acr, %0\n" : "=r" (p));
	if (*p != 0x88ccee19)
		err();

	cris_tst_cc_init();
	asm volatile ("clearf\tzvnc\n");
	cris_addoq(-8, t + 1);
	cris_tst_cc(0, 0, 0, 0);
	asm volatile ("move.d\t$acr, %0\n" : "=r" (p));
	if (*p != 0x55aa77ff)
		err();
	pass();
	return 0;
}
