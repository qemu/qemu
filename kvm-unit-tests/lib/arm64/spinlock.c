/*
 * spinlocks
 *
 * Copyright (C) 2015, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <asm/spinlock.h>
#include <asm/barrier.h>
#include <asm/mmu.h>

void spin_lock(struct spinlock *lock)
{
	u32 val, fail;

	if (!mmu_enabled()) {
		lock->v = 1;
		smp_mb();
		return;
	}

	do {
		asm volatile(
		"1:	ldaxr	%w0, [%2]\n"
		"	cbnz	%w0, 1b\n"
		"	mov	%0, #1\n"
		"	stxr	%w1, %w0, [%2]\n"
		: "=&r" (val), "=&r" (fail)
		: "r" (&lock->v)
		: "cc" );
	} while (fail);
	smp_mb();
}

void spin_unlock(struct spinlock *lock)
{
	smp_mb();
	if (mmu_enabled())
		asm volatile("stlrh wzr, [%0]" :: "r" (&lock->v));
	else
		lock->v = 0;
}
