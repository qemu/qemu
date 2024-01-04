#include <libcflat.h>
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
		"1:	ldrex	%0, [%2]\n"
		"	teq	%0, #0\n"
		"	bne	1b\n"
		"	mov	%0, #1\n"
		"	strex	%1, %0, [%2]\n"
		: "=&r" (val), "=&r" (fail)
		: "r" (&lock->v)
		: "cc" );
	} while (fail);

	smp_mb();
}

void spin_unlock(struct spinlock *lock)
{
	smp_mb();
	lock->v = 0;
}
