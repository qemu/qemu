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
 * See docs/devel/atomics.txt for discussion about the guarantees each
 * atomic primitive is meant to provide.
 */

#ifndef QEMU_ATOMIC_H
#define QEMU_ATOMIC_H

/* Compiler barrier */
#define barrier()   ({ asm volatile("" ::: "memory"); (void)0; })

/* The variable that receives the old value of an atomically-accessed
 * variable must be non-qualified, because atomic builtins return values
 * through a pointer-type argument as in __atomic_load(&var, &old, MODEL).
 *
 * This macro has to handle types smaller than int manually, because of
 * implicit promotion.  int and larger types, as well as pointers, can be
 * converted to a non-qualified type just by applying a binary operator.
 */
#define typeof_strip_qual(expr)                                                    \
  typeof(                                                                          \
    __builtin_choose_expr(                                                         \
      __builtin_types_compatible_p(typeof(expr), bool) ||                          \
        __builtin_types_compatible_p(typeof(expr), const bool) ||                  \
        __builtin_types_compatible_p(typeof(expr), volatile bool) ||               \
        __builtin_types_compatible_p(typeof(expr), const volatile bool),           \
        (bool)1,                                                                   \
    __builtin_choose_expr(                                                         \
      __builtin_types_compatible_p(typeof(expr), signed char) ||                   \
        __builtin_types_compatible_p(typeof(expr), const signed char) ||           \
        __builtin_types_compatible_p(typeof(expr), volatile signed char) ||        \
        __builtin_types_compatible_p(typeof(expr), const volatile signed char),    \
        (signed char)1,                                                            \
    __builtin_choose_expr(                                                         \
      __builtin_types_compatible_p(typeof(expr), unsigned char) ||                 \
        __builtin_types_compatible_p(typeof(expr), const unsigned char) ||         \
        __builtin_types_compatible_p(typeof(expr), volatile unsigned char) ||      \
        __builtin_types_compatible_p(typeof(expr), const volatile unsigned char),  \
        (unsigned char)1,                                                          \
    __builtin_choose_expr(                                                         \
      __builtin_types_compatible_p(typeof(expr), signed short) ||                  \
        __builtin_types_compatible_p(typeof(expr), const signed short) ||          \
        __builtin_types_compatible_p(typeof(expr), volatile signed short) ||       \
        __builtin_types_compatible_p(typeof(expr), const volatile signed short),   \
        (signed short)1,                                                           \
    __builtin_choose_expr(                                                         \
      __builtin_types_compatible_p(typeof(expr), unsigned short) ||                \
        __builtin_types_compatible_p(typeof(expr), const unsigned short) ||        \
        __builtin_types_compatible_p(typeof(expr), volatile unsigned short) ||     \
        __builtin_types_compatible_p(typeof(expr), const volatile unsigned short), \
        (unsigned short)1,                                                         \
      (expr)+0))))))

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

#define smp_mb()                     ({ barrier(); __atomic_thread_fence(__ATOMIC_SEQ_CST); })
#define smp_mb_release()             ({ barrier(); __atomic_thread_fence(__ATOMIC_RELEASE); })
#define smp_mb_acquire()             ({ barrier(); __atomic_thread_fence(__ATOMIC_ACQUIRE); })

/* Most compilers currently treat consume and acquire the same, but really
 * no processors except Alpha need a barrier here.  Leave it in if
 * using Thread Sanitizer to avoid warnings, otherwise optimize it away.
 */
#if defined(__SANITIZE_THREAD__)
#define smp_read_barrier_depends()   ({ barrier(); __atomic_thread_fence(__ATOMIC_CONSUME); })
#elif defined(__alpha__)
#define smp_read_barrier_depends()   asm volatile("mb":::"memory")
#else
#define smp_read_barrier_depends()   barrier()
#endif

/*
 * A signal barrier forces all pending local memory ops to be observed before
 * a SIGSEGV is delivered to the *same* thread.  In practice this is exactly
 * the same as barrier(), but since we have the correct builtin, use it.
 */
#define signal_barrier()    __atomic_signal_fence(__ATOMIC_SEQ_CST)

/* Sanity check that the size of an atomic operation isn't "overly large".
 * Despite the fact that e.g. i686 has 64-bit atomic operations, we do not
 * want to use them because we ought not need them, and this lets us do a
 * bit of sanity checking that other 32-bit hosts might build.
 *
 * That said, we have a problem on 64-bit ILP32 hosts in that in order to
 * sync with TCG_OVERSIZED_GUEST, this must match TCG_TARGET_REG_BITS.
 * We'd prefer not want to pull in everything else TCG related, so handle
 * those few cases by hand.
 *
 * Note that x32 is fully detected with __x86_64__ + _ILP32, and that for
 * Sparc we always force the use of sparcv9 in configure. MIPS n32 (ILP32) &
 * n64 (LP64) ABIs are both detected using __mips64.
 */
#if defined(__x86_64__) || defined(__sparc__) || defined(__mips64)
# define ATOMIC_REG_SIZE  8
#else
# define ATOMIC_REG_SIZE  sizeof(void *)
#endif

/* Weak atomic operations prevent the compiler moving other
 * loads/stores past the atomic operation load/store. However there is
 * no explicit memory barrier for the processor.
 *
 * The C11 memory model says that variables that are accessed from
 * different threads should at least be done with __ATOMIC_RELAXED
 * primitives or the result is undefined. Generally this has little to
 * no effect on the generated code but not using the atomic primitives
 * will get flagged by sanitizers as a violation.
 */
#define qatomic_read__nocheck(ptr) \
    __atomic_load_n(ptr, __ATOMIC_RELAXED)

#define qatomic_read(ptr)                              \
    ({                                                 \
    QEMU_BUILD_BUG_ON(sizeof(*ptr) > ATOMIC_REG_SIZE); \
    qatomic_read__nocheck(ptr);                        \
    })

#define qatomic_set__nocheck(ptr, i) \
    __atomic_store_n(ptr, i, __ATOMIC_RELAXED)

#define qatomic_set(ptr, i)  do {                      \
    QEMU_BUILD_BUG_ON(sizeof(*ptr) > ATOMIC_REG_SIZE); \
    qatomic_set__nocheck(ptr, i);                      \
} while(0)

/* See above: most compilers currently treat consume and acquire the
 * same, but this slows down qatomic_rcu_read unnecessarily.
 */
#ifdef __SANITIZE_THREAD__
#define qatomic_rcu_read__nocheck(ptr, valptr)           \
    __atomic_load(ptr, valptr, __ATOMIC_CONSUME);
#else
#define qatomic_rcu_read__nocheck(ptr, valptr)           \
    __atomic_load(ptr, valptr, __ATOMIC_RELAXED);        \
    smp_read_barrier_depends();
#endif

#define qatomic_rcu_read(ptr)                          \
    ({                                                 \
    QEMU_BUILD_BUG_ON(sizeof(*ptr) > ATOMIC_REG_SIZE); \
    typeof_strip_qual(*ptr) _val;                      \
    qatomic_rcu_read__nocheck(ptr, &_val);             \
    _val;                                              \
    })

#define qatomic_rcu_set(ptr, i) do {                   \
    QEMU_BUILD_BUG_ON(sizeof(*ptr) > ATOMIC_REG_SIZE); \
    __atomic_store_n(ptr, i, __ATOMIC_RELEASE);        \
} while(0)

#define qatomic_load_acquire(ptr)                       \
    ({                                                  \
    QEMU_BUILD_BUG_ON(sizeof(*ptr) > ATOMIC_REG_SIZE);  \
    typeof_strip_qual(*ptr) _val;                       \
    __atomic_load(ptr, &_val, __ATOMIC_ACQUIRE);        \
    _val;                                               \
    })

#define qatomic_store_release(ptr, i)  do {             \
    QEMU_BUILD_BUG_ON(sizeof(*ptr) > ATOMIC_REG_SIZE);  \
    __atomic_store_n(ptr, i, __ATOMIC_RELEASE);         \
} while(0)


/* All the remaining operations are fully sequentially consistent */

#define qatomic_xchg__nocheck(ptr, i)    ({                 \
    __atomic_exchange_n(ptr, (i), __ATOMIC_SEQ_CST);        \
})

#define qatomic_xchg(ptr, i)    ({                          \
    QEMU_BUILD_BUG_ON(sizeof(*ptr) > ATOMIC_REG_SIZE);      \
    qatomic_xchg__nocheck(ptr, i);                          \
})

/* Returns the eventual value, failed or not */
#define qatomic_cmpxchg__nocheck(ptr, old, new)    ({                   \
    typeof_strip_qual(*ptr) _old = (old);                               \
    (void)__atomic_compare_exchange_n(ptr, &_old, new, false,           \
                              __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);      \
    _old;                                                               \
})

#define qatomic_cmpxchg(ptr, old, new)    ({                            \
    QEMU_BUILD_BUG_ON(sizeof(*ptr) > ATOMIC_REG_SIZE);                  \
    qatomic_cmpxchg__nocheck(ptr, old, new);                            \
})

/* Provide shorter names for GCC atomic builtins, return old value */
#define qatomic_fetch_inc(ptr)  __atomic_fetch_add(ptr, 1, __ATOMIC_SEQ_CST)
#define qatomic_fetch_dec(ptr)  __atomic_fetch_sub(ptr, 1, __ATOMIC_SEQ_CST)

#define qatomic_fetch_add(ptr, n) __atomic_fetch_add(ptr, n, __ATOMIC_SEQ_CST)
#define qatomic_fetch_sub(ptr, n) __atomic_fetch_sub(ptr, n, __ATOMIC_SEQ_CST)
#define qatomic_fetch_and(ptr, n) __atomic_fetch_and(ptr, n, __ATOMIC_SEQ_CST)
#define qatomic_fetch_or(ptr, n)  __atomic_fetch_or(ptr, n, __ATOMIC_SEQ_CST)
#define qatomic_fetch_xor(ptr, n) __atomic_fetch_xor(ptr, n, __ATOMIC_SEQ_CST)

#define qatomic_inc_fetch(ptr)    __atomic_add_fetch(ptr, 1, __ATOMIC_SEQ_CST)
#define qatomic_dec_fetch(ptr)    __atomic_sub_fetch(ptr, 1, __ATOMIC_SEQ_CST)
#define qatomic_add_fetch(ptr, n) __atomic_add_fetch(ptr, n, __ATOMIC_SEQ_CST)
#define qatomic_sub_fetch(ptr, n) __atomic_sub_fetch(ptr, n, __ATOMIC_SEQ_CST)
#define qatomic_and_fetch(ptr, n) __atomic_and_fetch(ptr, n, __ATOMIC_SEQ_CST)
#define qatomic_or_fetch(ptr, n)  __atomic_or_fetch(ptr, n, __ATOMIC_SEQ_CST)
#define qatomic_xor_fetch(ptr, n) __atomic_xor_fetch(ptr, n, __ATOMIC_SEQ_CST)

/* And even shorter names that return void.  */
#define qatomic_inc(ptr) \
    ((void) __atomic_fetch_add(ptr, 1, __ATOMIC_SEQ_CST))
#define qatomic_dec(ptr) \
    ((void) __atomic_fetch_sub(ptr, 1, __ATOMIC_SEQ_CST))
#define qatomic_add(ptr, n) \
    ((void) __atomic_fetch_add(ptr, n, __ATOMIC_SEQ_CST))
#define qatomic_sub(ptr, n) \
    ((void) __atomic_fetch_sub(ptr, n, __ATOMIC_SEQ_CST))
#define qatomic_and(ptr, n) \
    ((void) __atomic_fetch_and(ptr, n, __ATOMIC_SEQ_CST))
#define qatomic_or(ptr, n) \
    ((void) __atomic_fetch_or(ptr, n, __ATOMIC_SEQ_CST))
#define qatomic_xor(ptr, n) \
    ((void) __atomic_fetch_xor(ptr, n, __ATOMIC_SEQ_CST))

#else /* __ATOMIC_RELAXED */

#ifdef __alpha__
#define smp_read_barrier_depends()   asm volatile("mb":::"memory")
#endif

#if defined(__i386__) || defined(__x86_64__) || defined(__s390x__)

/*
 * Because of the strongly ordered storage model, wmb() and rmb() are nops
 * here (a compiler barrier only).  QEMU doesn't do accesses to write-combining
 * qemu memory or non-temporal load/stores from C code.
 */
#define smp_mb_release()   barrier()
#define smp_mb_acquire()   barrier()

/*
 * __sync_lock_test_and_set() is documented to be an acquire barrier only,
 * but it is a full barrier at the hardware level.  Add a compiler barrier
 * to make it a full barrier also at the compiler level.
 */
#define qatomic_xchg(ptr, i)    (barrier(), __sync_lock_test_and_set(ptr, i))

#elif defined(_ARCH_PPC)

/*
 * We use an eieio() for wmb() on powerpc.  This assumes we don't
 * need to order cacheable and non-cacheable stores with respect to
 * each other.
 *
 * smp_mb has the same problem as on x86 for not-very-new GCC
 * (http://patchwork.ozlabs.org/patch/126184/, Nov 2011).
 */
#define smp_wmb()          ({ asm volatile("eieio" ::: "memory"); (void)0; })
#if defined(__powerpc64__)
#define smp_mb_release()   ({ asm volatile("lwsync" ::: "memory"); (void)0; })
#define smp_mb_acquire()   ({ asm volatile("lwsync" ::: "memory"); (void)0; })
#else
#define smp_mb_release()   ({ asm volatile("sync" ::: "memory"); (void)0; })
#define smp_mb_acquire()   ({ asm volatile("sync" ::: "memory"); (void)0; })
#endif
#define smp_mb()           ({ asm volatile("sync" ::: "memory"); (void)0; })

#endif /* _ARCH_PPC */

/*
 * For (host) platforms we don't have explicit barrier definitions
 * for, we use the gcc __sync_synchronize() primitive to generate a
 * full barrier.  This should be safe on all platforms, though it may
 * be overkill for smp_mb_acquire() and smp_mb_release().
 */
#ifndef smp_mb
#define smp_mb()           __sync_synchronize()
#endif

#ifndef smp_mb_acquire
#define smp_mb_acquire()   __sync_synchronize()
#endif

#ifndef smp_mb_release
#define smp_mb_release()   __sync_synchronize()
#endif

#ifndef smp_read_barrier_depends
#define smp_read_barrier_depends()   barrier()
#endif

#ifndef signal_barrier
#define signal_barrier()    barrier()
#endif

/* These will only be atomic if the processor does the fetch or store
 * in a single issue memory operation
 */
#define qatomic_read__nocheck(p)   (*(__typeof__(*(p)) volatile*) (p))
#define qatomic_set__nocheck(p, i) ((*(__typeof__(*(p)) volatile*) (p)) = (i))

#define qatomic_read(ptr)       qatomic_read__nocheck(ptr)
#define qatomic_set(ptr, i)     qatomic_set__nocheck(ptr,i)

/**
 * qatomic_rcu_read - reads a RCU-protected pointer to a local variable
 * into a RCU read-side critical section. The pointer can later be safely
 * dereferenced within the critical section.
 *
 * This ensures that the pointer copy is invariant thorough the whole critical
 * section.
 *
 * Inserts memory barriers on architectures that require them (currently only
 * Alpha) and documents which pointers are protected by RCU.
 *
 * qatomic_rcu_read also includes a compiler barrier to ensure that
 * value-speculative optimizations (e.g. VSS: Value Speculation
 * Scheduling) does not perform the data read before the pointer read
 * by speculating the value of the pointer.
 *
 * Should match qatomic_rcu_set(), qatomic_xchg(), qatomic_cmpxchg().
 */
#define qatomic_rcu_read(ptr)    ({               \
    typeof(*ptr) _val = qatomic_read(ptr);        \
    smp_read_barrier_depends();                   \
    _val;                                         \
})

/**
 * qatomic_rcu_set - assigns (publicizes) a pointer to a new data structure
 * meant to be read by RCU read-side critical sections.
 *
 * Documents which pointers will be dereferenced by RCU read-side critical
 * sections and adds the required memory barriers on architectures requiring
 * them. It also makes sure the compiler does not reorder code initializing the
 * data structure before its publication.
 *
 * Should match qatomic_rcu_read().
 */
#define qatomic_rcu_set(ptr, i)  do {             \
    smp_wmb();                                    \
    qatomic_set(ptr, i);                          \
} while (0)

#define qatomic_load_acquire(ptr)    ({     \
    typeof(*ptr) _val = qatomic_read(ptr);  \
    smp_mb_acquire();                       \
    _val;                                   \
})

#define qatomic_store_release(ptr, i)  do { \
    smp_mb_release();                       \
    qatomic_set(ptr, i);                    \
} while (0)

#ifndef qatomic_xchg
#if defined(__clang__)
#define qatomic_xchg(ptr, i)    __sync_swap(ptr, i)
#else
/* __sync_lock_test_and_set() is documented to be an acquire barrier only.  */
#define qatomic_xchg(ptr, i)    (smp_mb(), __sync_lock_test_and_set(ptr, i))
#endif
#endif
#define qatomic_xchg__nocheck  qatomic_xchg

/* Provide shorter names for GCC atomic builtins.  */
#define qatomic_fetch_inc(ptr)  __sync_fetch_and_add(ptr, 1)
#define qatomic_fetch_dec(ptr)  __sync_fetch_and_add(ptr, -1)

#define qatomic_fetch_add(ptr, n) __sync_fetch_and_add(ptr, n)
#define qatomic_fetch_sub(ptr, n) __sync_fetch_and_sub(ptr, n)
#define qatomic_fetch_and(ptr, n) __sync_fetch_and_and(ptr, n)
#define qatomic_fetch_or(ptr, n) __sync_fetch_and_or(ptr, n)
#define qatomic_fetch_xor(ptr, n) __sync_fetch_and_xor(ptr, n)

#define qatomic_inc_fetch(ptr)  __sync_add_and_fetch(ptr, 1)
#define qatomic_dec_fetch(ptr)  __sync_add_and_fetch(ptr, -1)
#define qatomic_add_fetch(ptr, n) __sync_add_and_fetch(ptr, n)
#define qatomic_sub_fetch(ptr, n) __sync_sub_and_fetch(ptr, n)
#define qatomic_and_fetch(ptr, n) __sync_and_and_fetch(ptr, n)
#define qatomic_or_fetch(ptr, n) __sync_or_and_fetch(ptr, n)
#define qatomic_xor_fetch(ptr, n) __sync_xor_and_fetch(ptr, n)

#define qatomic_cmpxchg(ptr, old, new) \
    __sync_val_compare_and_swap(ptr, old, new)
#define qatomic_cmpxchg__nocheck(ptr, old, new)  qatomic_cmpxchg(ptr, old, new)

/* And even shorter names that return void.  */
#define qatomic_inc(ptr)        ((void) __sync_fetch_and_add(ptr, 1))
#define qatomic_dec(ptr)        ((void) __sync_fetch_and_add(ptr, -1))
#define qatomic_add(ptr, n)     ((void) __sync_fetch_and_add(ptr, n))
#define qatomic_sub(ptr, n)     ((void) __sync_fetch_and_sub(ptr, n))
#define qatomic_and(ptr, n)     ((void) __sync_fetch_and_and(ptr, n))
#define qatomic_or(ptr, n)      ((void) __sync_fetch_and_or(ptr, n))
#define qatomic_xor(ptr, n)     ((void) __sync_fetch_and_xor(ptr, n))

#endif /* __ATOMIC_RELAXED */

#ifndef smp_wmb
#define smp_wmb()   smp_mb_release()
#endif
#ifndef smp_rmb
#define smp_rmb()   smp_mb_acquire()
#endif

/* This is more efficient than a store plus a fence.  */
#if !defined(__SANITIZE_THREAD__)
#if defined(__i386__) || defined(__x86_64__) || defined(__s390x__)
#define qatomic_mb_set(ptr, i)  ((void)qatomic_xchg(ptr, i))
#endif
#endif

/* qatomic_mb_read/set semantics map Java volatile variables. They are
 * less expensive on some platforms (notably POWER) than fully
 * sequentially consistent operations.
 *
 * As long as they are used as paired operations they are safe to
 * use. See docs/devel/atomics.txt for more discussion.
 */

#ifndef qatomic_mb_read
#define qatomic_mb_read(ptr)                             \
    qatomic_load_acquire(ptr)
#endif

#ifndef qatomic_mb_set
#define qatomic_mb_set(ptr, i)  do {                    \
    qatomic_store_release(ptr, i);                      \
    smp_mb();                                           \
} while(0)
#endif

#define qatomic_fetch_inc_nonzero(ptr) ({                               \
    typeof_strip_qual(*ptr) _oldn = qatomic_read(ptr);                  \
    while (_oldn && qatomic_cmpxchg(ptr, _oldn, _oldn + 1) != _oldn) {  \
        _oldn = qatomic_read(ptr);                                      \
    }                                                                   \
    _oldn;                                                              \
})

/* Abstractions to access atomically (i.e. "once") i64/u64 variables */
#ifdef CONFIG_ATOMIC64
static inline int64_t qatomic_read_i64(const int64_t *ptr)
{
    /* use __nocheck because sizeof(void *) might be < sizeof(u64) */
    return qatomic_read__nocheck(ptr);
}

static inline uint64_t qatomic_read_u64(const uint64_t *ptr)
{
    return qatomic_read__nocheck(ptr);
}

static inline void qatomic_set_i64(int64_t *ptr, int64_t val)
{
    qatomic_set__nocheck(ptr, val);
}

static inline void qatomic_set_u64(uint64_t *ptr, uint64_t val)
{
    qatomic_set__nocheck(ptr, val);
}

static inline void qatomic64_init(void)
{
}
#else /* !CONFIG_ATOMIC64 */
int64_t  qatomic_read_i64(const int64_t *ptr);
uint64_t qatomic_read_u64(const uint64_t *ptr);
void qatomic_set_i64(int64_t *ptr, int64_t val);
void qatomic_set_u64(uint64_t *ptr, uint64_t val);
void qatomic64_init(void);
#endif /* !CONFIG_ATOMIC64 */

#endif /* QEMU_ATOMIC_H */
