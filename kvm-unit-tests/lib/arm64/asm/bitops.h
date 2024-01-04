#ifndef _ASMARM64_BITOPS_H_
#define _ASMARM64_BITOPS_H_
/*
 * Adapated from
 *   include/linux/bitops.h
 *   arch/arm64/lib/bitops.S
 *
 * Copyright (C) 2015, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */

#define BITS_PER_LONG	64
#define BIT(nr)		(1UL << (nr))
#define BIT_MASK(nr)	(1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)	((nr) / BITS_PER_LONG)

#define ATOMIC_BITOP(insn, mask, word)				\
({								\
	unsigned long tmp1, tmp2;				\
	asm volatile(						\
	"1:	ldxr	%0, [%2]\n"				\
		insn"	%0, %0, %3\n"				\
	"	stxr	%w1, %0, [%2]\n"			\
	"	cbnz	%w1, 1b\n"				\
	: "=&r" (tmp1), "=&r" (tmp2)				\
	: "r" (word), "r" (mask)				\
	: "cc");						\
})

#define ATOMIC_TESTOP(insn, mask, word, old)			\
({								\
	unsigned long tmp1, tmp2;				\
	asm volatile(						\
	"1:	ldxr	%0, [%3]\n"				\
	"	and	%1, %0, %4\n"				\
		insn"	%0, %0, %4\n"				\
	"	stlxr	%w2, %0, [%3]\n"			\
	"	cbnz	%w2, 1b\n"				\
	: "=&r" (tmp1), "=&r" (old), "=&r" (tmp2)		\
	: "r" (word), "r" (mask)				\
	: "cc");						\
})

extern void set_bit(int nr, volatile unsigned long *addr);
extern void clear_bit(int nr, volatile unsigned long *addr);
extern int test_bit(int nr, const volatile unsigned long *addr);
extern int test_and_set_bit(int nr, volatile unsigned long *addr);
extern int test_and_clear_bit(int nr, volatile unsigned long *addr);

#endif /* _ASMARM64_BITOPS_H_ */
