#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "sys.h"
#include "crisutils.h"

static inline void cris_ftag_i(unsigned int x)
{
	register unsigned int v asm("$r10") = x;
	asm ("ftagi\t[%0]\n" : : "r" (v) );
}
static inline void cris_ftag_d(unsigned int x)
{
	register unsigned int v asm("$r10") = x;
	asm ("ftagd\t[%0]\n" : : "r" (v) );
}
static inline void cris_fidx_i(unsigned int x)
{
	register unsigned int v asm("$r10") = x;
	asm ("fidxi\t[%0]\n" : : "r" (v) );
}
static inline void cris_fidx_d(unsigned int x)
{
	register unsigned int v asm("$r10") = x;
	asm ("fidxd\t[%0]\n" : : "r" (v) );
}


int main(void)
{
	cris_ftag_i(0);
	cris_ftag_d(0);
	cris_fidx_i(0);
	cris_fidx_d(0);
	pass();
	return 0;
}
