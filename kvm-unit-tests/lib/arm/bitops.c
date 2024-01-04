/*
 * Adapated from
 *   include/asm-generic/bitops/atomic.h
 *
 * Copyright (C) 2015, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <asm/bitops.h>
#include <asm/barrier.h>
#include <asm/mmu.h>

void set_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *word = addr + BIT_WORD(nr);
	unsigned long mask = BIT_MASK(nr);

	if (mmu_enabled())
		ATOMIC_BITOP("orr", mask, word);
	else
		*word |= mask;
	smp_mb();
}

void clear_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *word = addr + BIT_WORD(nr);
	unsigned long mask = BIT_MASK(nr);

	if (mmu_enabled())
		ATOMIC_BITOP("bic", mask, word);
	else
		*word &= ~mask;
	smp_mb();
}

int test_bit(int nr, const volatile unsigned long *addr)
{
	const volatile unsigned long *word = addr + BIT_WORD(nr);
	unsigned long mask = BIT_MASK(nr);

	return (*word & mask) != 0;
}

int test_and_set_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *word = addr + BIT_WORD(nr);
	unsigned long mask = BIT_MASK(nr);
	unsigned long old;

	smp_mb();

	if (mmu_enabled()) {
		ATOMIC_TESTOP("orr", mask, word, old);
	} else {
		old = *word;
		*word = old | mask;
	}
	smp_mb();

	return (old & mask) != 0;
}

int test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	volatile unsigned long *word = addr + BIT_WORD(nr);
	unsigned long mask = BIT_MASK(nr);
	unsigned long old;

	smp_mb();

	if (mmu_enabled()) {
		ATOMIC_TESTOP("bic", mask, word, old);
	} else {
		old = *word;
		*word = old & ~mask;
	}
	smp_mb();

	return (old & mask) != 0;
}
