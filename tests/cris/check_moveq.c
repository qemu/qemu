#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "sys.h"
#include "crisutils.h"

#define cris_moveq(dst, src) \
               asm volatile ("moveq %1, %0\n" : "=r" (dst) : "i" (src));



int main(void)
{
	int t;

	cris_tst_cc_init();
	asm volatile ("setf\tzvnc\n");
	cris_moveq(t, 10);
	cris_tst_cc(1, 1, 1, 1);
	if (t != 10)
		err();

	/* make sure moveq doesnt clobber the zflag.  */
	cris_tst_cc_init();
	asm volatile ("setf vnc\n");
	asm volatile ("clearf z\n");
	cris_moveq(t, 0);
	cris_tst_cc(1, 0, 1, 1);
	if (t != 0)
		err();

	/* make sure moveq doesnt clobber the nflag.
	   Also check large immediates  */
	cris_tst_cc_init();
	asm volatile ("setf zvc\n");
	asm volatile ("clearf n\n");
	cris_moveq(t, -31);
	cris_tst_cc(0, 1, 1, 1);
	if (t != -31)
		err();

	cris_tst_cc_init();
	asm volatile ("setf nzvc\n");
	cris_moveq(t, 31);
	cris_tst_cc(1, 1, 1, 1);
	if (t != 31)
		err();

	pass();
	return 0;
}
