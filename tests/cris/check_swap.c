#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "sys.h"
#include "crisutils.h"

#define N 8
#define W 4
#define B 2
#define R 1

extern inline int cris_swap(const int mode, int x)
{
	switch (mode)
	{
		case N: asm ("swapn\t%0\n" : "+r" (x) : "0" (x)); break;
		case W: asm ("swapw\t%0\n" : "+r" (x) : "0" (x)); break;
		case B: asm ("swapb\t%0\n" : "+r" (x) : "0" (x)); break;
		case R: asm ("swapr\t%0\n" : "+r" (x) : "0" (x)); break;
		case B|R: asm ("swapbr\t%0\n" : "+r" (x) : "0" (x)); break;
		case W|R: asm ("swapwr\t%0\n" : "+r" (x) : "0" (x)); break;
		case W|B: asm ("swapwb\t%0\n" : "+r" (x) : "0" (x)); break;
		case W|B|R: asm ("swapwbr\t%0\n" : "+r" (x) : "0" (x)); break;
		case N|R: asm ("swapnr\t%0\n" : "+r" (x) : "0" (x)); break;
		case N|B: asm ("swapnb\t%0\n" : "+r" (x) : "0" (x)); break;
		case N|B|R: asm ("swapnbr\t%0\n" : "+r" (x) : "0" (x)); break;
		case N|W: asm ("swapnw\t%0\n" : "+r" (x) : "0" (x)); break;
		default:
			err();
			break;
	}
	return x;
}

/* Made this a macro to be able to pick up the location of the errors.  */
#define verify_swap(mode, val, expected, n, z)          \
do {                                                    \
        int r;                                          \
        cris_tst_cc_init();                             \
	r = cris_swap(mode, val);                       \
        cris_tst_mov_cc(n, z);                          \
	if (r != expected)                              \
		err();                                  \
} while(0);

void check_swap(void)
{
	/* Some of these numbers are borrowed from GDB's cris sim
	   testsuite.  */
	if (cris_swap(N, 0) != 0xffffffff)
		err();
	if (cris_swap(W, 0x12345678) != 0x56781234)
		err();
	if (cris_swap(B, 0x12345678) != 0x34127856)
		err();

	verify_swap(R, 0x78134452, 0x1ec8224a, 0, 0);
	verify_swap(B, 0x78134452, 0x13785244, 0, 0);
	verify_swap(B|R, 0x78134452, 0xc81e4a22, 1, 0);
	verify_swap(W, 0x78134452, 0x44527813, 0, 0);
	verify_swap(W|R, 0x78134452, 0x224a1ec8, 0, 0);
	verify_swap(W|B|R, 0x78134452, 0x4a22c81e, 0, 0);
	verify_swap(N, 0x78134452, 0x87ecbbad, 1, 0);
	verify_swap(N|R, 0x78134452, 0xe137ddb5, 1, 0);
	verify_swap(N|B, 0x78134452, 0xec87adbb, 1, 0);
	verify_swap(N|B|R, 0x78134452, 0x37e1b5dd, 0, 0);
	verify_swap(N|W, 0x78134452, 0xbbad87ec, 1, 0);
	verify_swap(N|B|R, 0xffffffff, 0, 0, 1);
}

int main(void)
{
	check_swap();
	pass();
	return 0;
}
