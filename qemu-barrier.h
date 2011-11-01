#ifndef __QEMU_BARRIER_H
#define __QEMU_BARRIER_H 1

/* Compiler barrier */
#define barrier()   asm volatile("" ::: "memory")

#if defined(__i386__) || defined(__x86_64__)

/*
 * Because of the strongly ordered x86 storage model, wmb() is a nop
 * on x86(well, a compiler barrier only).  Well, at least as long as
 * qemu doesn't do accesses to write-combining memory or non-temporal
 * load/stores from C code.
 */
#define smp_wmb()   barrier()

#elif defined(_ARCH_PPC)

/*
 * We use an eieio() for a wmb() on powerpc.  This assumes we don't
 * need to order cacheable and non-cacheable stores with respect to
 * each other
 */
#define smp_wmb()   asm volatile("eieio" ::: "memory")

#else

/*
 * For (host) platforms we don't have explicit barrier definitions
 * for, we use the gcc __sync_synchronize() primitive to generate a
 * full barrier.  This should be safe on all platforms, though it may
 * be overkill.
 */
#define smp_wmb()   __sync_synchronize()

#endif

#endif
