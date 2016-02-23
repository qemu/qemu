/*
 * Simple interface for atomic operations.
 *
 * Copyright (C) 2013 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * See docs/atomics.txt for discussion about the guarantees each
 * atomic primitive is meant to provide.
 */

#ifndef __QEMU_ATOMIC_H
#define __QEMU_ATOMIC_H 1



/* Compiler barrier */
#define barrier()   ({ asm volatile("" ::: "memory"); (void)0; })

#ifdef __ATOMIC_RELAXED
/* For C11 atomic ops */

/* Manual memory barriers
 *
 *__atomic_thread_fence does not include a compiler barrier; instead,
 * the barrier is part of __atomic_load/__atomic_store's "volatile-like"
 * semantics. If smp_wmb() is a no-op, absence of the barrier means that
 * the compiler is free to reorder stores on each side of the barrier.
 * Add one here, and similarly in smp_rmb() and smp_read_barrier_depends().
 */

#define smp_mb()    ({ barrier(); __atomic_thread_fence(__ATOMIC_SEQ_CST); barrier(); })
#define smp_wmb()   ({ barrier(); __atomic_thread_fence(__ATOMIC_RELEASE); barrier(); })
#define smp_rmb()   ({ barrier(); __atomic_thread_fence(__ATOMIC_ACQUIRE); barrier(); })

#define smp_read_barrier_depends() ({ barrier(); __atomic_thread_fence(__ATOMIC_CONSUME); barrier(); })

/* Weak atomic operations prevent the compiler moving other
 * loads/stores past the atomic operation load/store. However there is
 * no explicit memory barrier for the processor.
 */
#define atomic_read(ptr)                          \
    ({                                            \
    typeof(*ptr) _val;                            \
     __atomic_load(ptr, &_val, __ATOMIC_RELAXED); \
    _val;                                         \
    })

#define atomic_set(ptr, i)  do {                  \
    typeof(*ptr) _val = (i);                      \
    __atomic_store(ptr, &_val, __ATOMIC_RELAXED); \
} while(0)

/* Atomic RCU operations imply weak memory barriers */

#define atomic_rcu_read(ptr)                      \
    ({                                            \
    typeof(*ptr) _val;                            \
     __atomic_load(ptr, &_val, __ATOMIC_CONSUME); \
    _val;                                         \
    })

#define atomic_rcu_set(ptr, i)  do {                    \
    typeof(*ptr) _val = (i);                            \
    __atomic_store(ptr, &_val, __ATOMIC_RELEASE);       \
} while(0)

/* atomic_mb_read/set semantics map Java volatile variables. They are
 * less expensive on some platforms (notably POWER & ARMv7) than fully
 * sequentially consistent operations.
 *
 * As long as they are used as paired operations they are safe to
 * use. See docs/atomic.txt for more discussion.
 */

#if defined(_ARCH_PPC)
#define atomic_mb_read(ptr)                             \
    ({                                                  \
    typeof(*ptr) _val;                                  \
     __atomic_load(ptr, &_val, __ATOMIC_RELAXED);       \
     smp_rmb();                                         \
    _val;                                               \
    })

#define atomic_mb_set(ptr, i)  do {                     \
    typeof(*ptr) _val = (i);                            \
    smp_wmb();                                          \
    __atomic_store(ptr, &_val, __ATOMIC_RELAXED);       \
    smp_mb();                                           \
} while(0)
#else
#define atomic_mb_read(ptr)                       \
    ({                                            \
    typeof(*ptr) _val;                            \
     __atomic_load(ptr, &_val, __ATOMIC_SEQ_CST); \
    _val;                                         \
    })

#define atomic_mb_set(ptr, i)  do {               \
    typeof(*ptr) _val = (i);                      \
    __atomic_store(ptr, &_val, __ATOMIC_SEQ_CST); \
} while(0)
#endif


/* All the remaining operations are fully sequentially consistent */

#define atomic_xchg(ptr, i)    ({                           \
    typeof(*ptr) _new = (i), _old;                          \
    __atomic_exchange(ptr, &_new, &_old, __ATOMIC_SEQ_CST); \
    _old;                                                   \
})

/* Returns the eventual value, failed or not */
#define atomic_cmpxchg(ptr, old, new)                                   \
    ({                                                                  \
    typeof(*ptr) _old = (old), _new = (new);                            \
    __atomic_compare_exchange(ptr, &_old, &_new, false,                 \
                              __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);      \
    _old;                                                               \
    })

/* Provide shorter names for GCC atomic builtins, return old value */
#define atomic_fetch_inc(ptr)  __atomic_fetch_add(ptr, 1, __ATOMIC_SEQ_CST)
#define atomic_fetch_dec(ptr)  __atomic_fetch_sub(ptr, 1, __ATOMIC_SEQ_CST)
#define atomic_fetch_add(ptr, n) __atomic_fetch_add(ptr, n, __ATOMIC_SEQ_CST)
#define atomic_fetch_sub(ptr, n) __atomic_fetch_sub(ptr, n, __ATOMIC_SEQ_CST)
#define atomic_fetch_and(ptr, n) __atomic_fetch_and(ptr, n, __ATOMIC_SEQ_CST)
#define atomic_fetch_or(ptr, n)  __atomic_fetch_or(ptr, n, __ATOMIC_SEQ_CST)

/* And even shorter names that return void.  */
#define atomic_inc(ptr)    ((void) __atomic_fetch_add(ptr, 1, __ATOMIC_SEQ_CST))
#define atomic_dec(ptr)    ((void) __atomic_fetch_sub(ptr, 1, __ATOMIC_SEQ_CST))
#define atomic_add(ptr, n) ((void) __atomic_fetch_add(ptr, n, __ATOMIC_SEQ_CST))
#define atomic_sub(ptr, n) ((void) __atomic_fetch_sub(ptr, n, __ATOMIC_SEQ_CST))
#define atomic_and(ptr, n) ((void) __atomic_fetch_and(ptr, n, __ATOMIC_SEQ_CST))
#define atomic_or(ptr, n)  ((void) __atomic_fetch_or(ptr, n, __ATOMIC_SEQ_CST))

#else /* __ATOMIC_RELAXED */

/*
 * We use GCC builtin if it's available, as that can use mfence on
 * 32-bit as well, e.g. if built with -march=pentium-m. However, on
 * i386 the spec is buggy, and the implementation followed it until
 * 4.3 (http://gcc.gnu.org/bugzilla/show_bug.cgi?id=36793).
 */
#if defined(__i386__) || defined(__x86_64__)
#if !QEMU_GNUC_PREREQ(4, 4)
#if defined __x86_64__
#define smp_mb()    ({ asm volatile("mfence" ::: "memory"); (void)0; })
#else
#define smp_mb()    ({ asm volatile("lock; addl $0,0(%%esp) " ::: "memory"); (void)0; })
#endif
#endif
#endif


#ifdef __alpha__
#define smp_read_barrier_depends()   asm volatile("mb":::"memory")
#endif

#if defined(__i386__) || defined(__x86_64__) || defined(__s390x__)

/*
 * Because of the strongly ordered storage model, wmb() and rmb() are nops
 * here (a compiler barrier only).  QEMU doesn't do accesses to write-combining
 * qemu memory or non-temporal load/stores from C code.
 */
#define smp_wmb()   barrier()
#define smp_rmb()   barrier()

/*
 * __sync_lock_test_and_set() is documented to be an acquire barrier only,
 * but it is a full barrier at the hardware level.  Add a compiler barrier
 * to make it a full barrier also at the compiler level.
 */
#define atomic_xchg(ptr, i)    (barrier(), __sync_lock_test_and_set(ptr, i))

/*
 * Load/store with Java volatile semantics.
 */
#define atomic_mb_set(ptr, i)  ((void)atomic_xchg(ptr, i))

#elif defined(_ARCH_PPC)

/*
 * We use an eieio() for wmb() on powerpc.  This assumes we don't
 * need to order cacheable and non-cacheable stores with respect to
 * each other.
 *
 * smp_mb has the same problem as on x86 for not-very-new GCC
 * (http://patchwork.ozlabs.org/patch/126184/, Nov 2011).
 */
#define smp_wmb()   ({ asm volatile("eieio" ::: "memory"); (void)0; })
#if defined(__powerpc64__)
#define smp_rmb()   ({ asm volatile("lwsync" ::: "memory"); (void)0; })
#else
#define smp_rmb()   ({ asm volatile("sync" ::: "memory"); (void)0; })
#endif
#define smp_mb()    ({ asm volatile("sync" ::: "memory"); (void)0; })

#endif /* _ARCH_PPC */

/*
 * For (host) platforms we don't have explicit barrier definitions
 * for, we use the gcc __sync_synchronize() primitive to generate a
 * full barrier.  This should be safe on all platforms, though it may
 * be overkill for smp_wmb() and smp_rmb().
 */
#ifndef smp_mb
#define smp_mb()    __sync_synchronize()
#endif

#ifndef smp_wmb
#define smp_wmb()   __sync_synchronize()
#endif

#ifndef smp_rmb
#define smp_rmb()   __sync_synchronize()
#endif

#ifndef smp_read_barrier_depends
#define smp_read_barrier_depends()   barrier()
#endif

/* These will only be atomic if the processor does the fetch or store
 * in a single issue memory operation
 */
#define atomic_read(ptr)       (*(__typeof__(*ptr) volatile*) (ptr))
#define atomic_set(ptr, i)     ((*(__typeof__(*ptr) volatile*) (ptr)) = (i))

/**
 * atomic_rcu_read - reads a RCU-protected pointer to a local variable
 * into a RCU read-side critical section. The pointer can later be safely
 * dereferenced within the critical section.
 *
 * This ensures that the pointer copy is invariant thorough the whole critical
 * section.
 *
 * Inserts memory barriers on architectures that require them (currently only
 * Alpha) and documents which pointers are protected by RCU.
 *
 * atomic_rcu_read also includes a compiler barrier to ensure that
 * value-speculative optimizations (e.g. VSS: Value Speculation
 * Scheduling) does not perform the data read before the pointer read
 * by speculating the value of the pointer.
 *
 * Should match atomic_rcu_set(), atomic_xchg(), atomic_cmpxchg().
 */
#define atomic_rcu_read(ptr)    ({                \
    typeof(*ptr) _val = atomic_read(ptr);         \
    smp_read_barrier_depends();                   \
    _val;                                         \
})

/**
 * atomic_rcu_set - assigns (publicizes) a pointer to a new data structure
 * meant to be read by RCU read-side critical sections.
 *
 * Documents which pointers will be dereferenced by RCU read-side critical
 * sections and adds the required memory barriers on architectures requiring
 * them. It also makes sure the compiler does not reorder code initializing the
 * data structure before its publication.
 *
 * Should match atomic_rcu_read().
 */
#define atomic_rcu_set(ptr, i)  do {              \
    smp_wmb();                                    \
    atomic_set(ptr, i);                           \
} while (0)

/* These have the same semantics as Java volatile variables.
 * See http://gee.cs.oswego.edu/dl/jmm/cookbook.html:
 * "1. Issue a StoreStore barrier (wmb) before each volatile store."
 *  2. Issue a StoreLoad barrier after each volatile store.
 *     Note that you could instead issue one before each volatile load, but
 *     this would be slower for typical programs using volatiles in which
 *     reads greatly outnumber writes. Alternatively, if available, you
 *     can implement volatile store as an atomic instruction (for example
 *     XCHG on x86) and omit the barrier. This may be more efficient if
 *     atomic instructions are cheaper than StoreLoad barriers.
 *  3. Issue LoadLoad and LoadStore barriers after each volatile load."
 *
 * If you prefer to think in terms of "pairing" of memory barriers,
 * an atomic_mb_read pairs with an atomic_mb_set.
 *
 * And for the few ia64 lovers that exist, an atomic_mb_read is a ld.acq,
 * while an atomic_mb_set is a st.rel followed by a memory barrier.
 *
 * These are a bit weaker than __atomic_load/store with __ATOMIC_SEQ_CST
 * (see docs/atomics.txt), and I'm not sure that __ATOMIC_ACQ_REL is enough.
 * Just always use the barriers manually by the rules above.
 */
#define atomic_mb_read(ptr)    ({           \
    typeof(*ptr) _val = atomic_read(ptr);   \
    smp_rmb();                              \
    _val;                                   \
})

#ifndef atomic_mb_set
#define atomic_mb_set(ptr, i)  do {         \
    smp_wmb();                              \
    atomic_set(ptr, i);                     \
    smp_mb();                               \
} while (0)
#endif

#ifndef atomic_xchg
#if defined(__clang__)
#define atomic_xchg(ptr, i)    __sync_swap(ptr, i)
#else
/* __sync_lock_test_and_set() is documented to be an acquire barrier only.  */
#define atomic_xchg(ptr, i)    (smp_mb(), __sync_lock_test_and_set(ptr, i))
#endif
#endif

/* Provide shorter names for GCC atomic builtins.  */
#define atomic_fetch_inc(ptr)  __sync_fetch_and_add(ptr, 1)
#define atomic_fetch_dec(ptr)  __sync_fetch_and_add(ptr, -1)
#define atomic_fetch_add       __sync_fetch_and_add
#define atomic_fetch_sub       __sync_fetch_and_sub
#define atomic_fetch_and       __sync_fetch_and_and
#define atomic_fetch_or        __sync_fetch_and_or
#define atomic_cmpxchg         __sync_val_compare_and_swap

/* And even shorter names that return void.  */
#define atomic_inc(ptr)        ((void) __sync_fetch_and_add(ptr, 1))
#define atomic_dec(ptr)        ((void) __sync_fetch_and_add(ptr, -1))
#define atomic_add(ptr, n)     ((void) __sync_fetch_and_add(ptr, n))
#define atomic_sub(ptr, n)     ((void) __sync_fetch_and_sub(ptr, n))
#define atomic_and(ptr, n)     ((void) __sync_fetch_and_and(ptr, n))
#define atomic_or(ptr, n)      ((void) __sync_fetch_and_or(ptr, n))

#endif /* __ATOMIC_RELAXED */
#endif /* __QEMU_ATOMIC_H */
