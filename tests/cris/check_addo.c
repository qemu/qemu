#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "sys.h"
#include "crisutils.h"

/* this would be better to do in asm, it's an orgy in GCC inline asm now.  */

#define cris_addo_b(o, v) \
	asm volatile ("addo.b\t[%0], %1, $acr\n" : : "r" (o), "r" (v) : "acr");
#define cris_addo_w(o, v) \
	asm volatile ("addo.w\t[%0], %1, $acr\n" : : "r" (o), "r" (v) : "acr");
#define cris_addo_d(o, v) \
	asm volatile ("addo.d\t[%0], %1, $acr\n" : : "r" (o), "r" (v) : "acr");
#define cris_addo_pi_b(o, v) \
	asm volatile ("addo.b\t[%0+], %1, $acr\n" \
                         : "+b" (o): "r" (v) : "acr");
#define cris_addo_pi_w(o, v) \
	asm volatile ("addo.w\t[%0+], %1, $acr\n" \
                         : "+b" (o): "r" (v) : "acr");
#define cris_addo_pi_d(o, v) \
	asm volatile ("addo.d\t[%0+], %1, $acr\n" \
                         : "+b" (o): "r" (v) : "acr");

struct {
	uint32_t v1;
	uint16_t v2;
	uint32_t v3;
	uint8_t v4;
	uint8_t v5;
	uint16_t v6;
	uint32_t v7;
} y = {
	32769,
	-1,
	5,
	3, -4,
	2,
	-76789887
};

static int x[3] = {0x55aa77ff, 0xccff2244, 0x88ccee19};

int main(void)
{
	int *r;
	unsigned char *t, *p;

	/* Note, this test-case will trig an unaligned access, partly
	   to x[0] and to [x1].  */
	t = (unsigned char *)x;
	t -= 32768;
	p = (unsigned char *) &y.v1;
	mb(); /* dont reorder anything beyond here.  */
	cris_tst_cc_init();
	asm volatile ("setf\tzvnc\n");
	cris_addo_pi_d(p, t);
	cris_tst_cc(1, 1, 1, 1);
	asm volatile ("move.d\t$acr, %0\n" : "=r" (r));
	if (*r != 0x4455aa77)
		err();


	t += 32770;
	mb(); /* dont reorder anything beyond here.  */
	cris_tst_cc_init();
	asm volatile ("setf\tzvnc\n");
	cris_addo_pi_w(p, t);
	cris_tst_cc(1, 1, 1, 1);
	asm volatile ("move.d\t$acr, %0\n" : "=r" (r));
	if (*r != 0x4455aa77)
		err();

	mb(); /* dont reorder anything beyond here.  */
	cris_tst_cc_init();
	asm volatile ("setf\tzvnc\n");
	cris_addo_d(p, r);
	cris_tst_cc(1, 1, 1, 1);
	p += 4;
	asm volatile ("move.d\t$acr, %0\n" : "=r" (r));
	if (*r != 0xee19ccff)
		err();

	mb(); /* dont reorder anything beyond here.  */
	cris_tst_cc_init();
	asm volatile ("setf\tzvnc\n");
	cris_addo_pi_b(p, t);
	cris_tst_cc(1, 1, 1, 1);
	asm volatile ("move.d\t$acr, %0\n" : "=r" (r));
	if (*(uint16_t*)r != 0xff22)
		err();

	mb(); /* dont reorder anything beyond here.  */
	cris_tst_cc_init();
	asm volatile ("setf\tzvnc\n");
	cris_addo_b(p, r);
	cris_tst_cc(1, 1, 1, 1);
	p += 1;
	asm volatile ("move.d\t$acr, %0\n" : "=r" (r));
	if (*r != 0x4455aa77)
		err();

	mb(); /* dont reorder anything beyond here.  */
	cris_tst_cc_init();
	asm volatile ("setf\tzvnc\n");
	cris_addo_w(p, r);
	cris_tst_cc(1, 1, 1, 1);
	p += 2;
	asm volatile ("move.d\t$acr, %0\n" : "=r" (r));
	if (*r != 0xff224455)
		err();

	mb(); /* dont reorder anything beyond here.  */
	cris_tst_cc_init();
	asm volatile ("setf\tzvnc\n");
	cris_addo_pi_d(p, t);
	cris_tst_cc(1, 1, 1, 1);
	asm volatile ("move.d\t$acr, %0\n" : "=r" (r));
	r = (void*)(((char *)r) + 76789885);
	if (*r != 0x55aa77ff)
		err();

	pass();
	return 0;
}
