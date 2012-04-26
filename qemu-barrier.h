#ifndef __QEMU_BARRIER_H
#define __QEMU_BARRIER_H 1

/* Compiler barrier */
#define barrier()   asm volatile("" ::: "memory")

#if defined(__i386__)

/*
 * Because of the strongly ordered x86 storage model, wmb() and rmb() are nops
 * on x86(well, a compiler barrier only).  Well, at least as long as
 * qemu doesn't do accesses to write-combining memory or non-temporal
 * load/stores from C code.
 */
#define smp_wmb()   barrier()
#define smp_rmb()   barrier()
/*
 * We use GCC builtin if it's available, as that can use
 * mfence on 32 bit as well, e.g. if built with -march=pentium-m.
 * However, on i386, there seem to be known bugs as recently as 4.3.
 * */
#if defined(__GNUC__) && __GNUC__ >= 4 && __GNUC_MINOR__ >= 4
#define smp_mb() __sync_synchronize()
#else
#define smp_mb() asm volatile("lock; addl $0,0(%%esp) " ::: "memory")
#endif

#elif defined(__x86_64__)

#define smp_wmb()   barrier()
#define smp_rmb()   barrier()
#define smp_mb() asm volatile("mfence" ::: "memory")

#elif defined(_ARCH_PPC)

/*
 * We use an eieio() for wmb() on powerpc.  This assumes we don't
 * need to order cacheable and non-cacheable stores with respect to
 * each other
 */
#define smp_wmb()   asm volatile("eieio" ::: "memory")

#if defined(__powerpc64__)
#define smp_rmb()   asm volatile("lwsync" ::: "memory")
#else
#define smp_rmb()   asm volatile("sync" ::: "memory")
#endif

#define smp_mb()   asm volatile("sync" ::: "memory")

#else

/*
 * For (host) platforms we don't have explicit barrier definitions
 * for, we use the gcc __sync_synchronize() primitive to generate a
 * full barrier.  This should be safe on all platforms, though it may
 * be overkill for wmb() and rmb().
 */
#define smp_wmb()   __sync_synchronize()
#define smp_mb()   __sync_synchronize()
#define smp_rmb()   __sync_synchronize()

#endif

#endif
