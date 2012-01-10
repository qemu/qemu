#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "sys.h"

static inline int cris_lz(int x)
{
	int r;
	asm ("lz\t%1, %0\n" : "=r" (r) : "r" (x));
	return r;
}

void check_lz(void)
{
	int i;

	if (cris_lz(0) != 32)
		err();
	if (cris_lz(1) != 31)
		err();
	if (cris_lz(2) != 30)
		err();
	if (cris_lz(4) != 29)
		err();
	if (cris_lz(8) != 28)
		err();

	/* try all positions with a single bit.  */
	for (i = 1; i < 32; i++) {
		if (cris_lz(1 << (i-1)) != (32 - i))
			err();
	}

	/* try all positions with all bits.  */
	for (i = 1; i < 32; i++) {
		/* split up this computation to clarify it.  */
		uint32_t val;
		val = (unsigned int)-1 >> (32 - i);
		if (cris_lz(val) != (32 - i))
			err();
	}
}

int main(void)
{
	check_lz();
	pass();
	exit(0);
}
