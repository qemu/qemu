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
 * See docs/devel/atomics.rst for discussion about the guarantees each
 * atomic primitive is meant to provide.
 */

#ifndef QEMU_ATOMIC_H
#define QEMU_ATOMIC_H

#include "compiler.h"

/* Compiler barrier */
#define barrier()   ({ asm volatile("" ::: "memory"); (void)0; })

#ifndef __ATOMIC_RELAXED
#error "Expecting C11 atomic ops"
#endif

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
#ifdef QEMU_SANITIZE_THREAD
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
    qemu_build_assert(sizeof(*ptr) <= ATOMIC_REG_SIZE); \
    qatomic_read__nocheck(ptr);                        \
    })

#define qatomic_set__nocheck(ptr, i) \
    __atomic_store_n(ptr, i, __ATOMIC_RELAXED)

#define qatomic_set(ptr, i)  do {                      \
    qemu_build_assert(sizeof(*ptr) <= ATOMIC_REG_SIZE); \
    qatomic_set__nocheck(ptr, i);                      \
} while(0)

/* See above: most compilers currently treat consume and acquire the
 * same, but this slows down qatomic_rcu_read unnecessarily.
 */
#ifdef QEMU_SANITIZE_THREAD
#define qatomic_rcu_read__nocheck(ptr, valptr)           \
    __atomic_load(ptr, valptr, __ATOMIC_CONSUME);
#else
#define qatomic_rcu_read__nocheck(ptr, valptr)           \
    __atomic_load(ptr, valptr, __ATOMIC_RELAXED);        \
    smp_read_barrier_depends();
#endif

/*
 * Preprocessor sorcery ahead: use a different identifier for the
 * local variable in each expansion, so we can nest macro calls
 * without shadowing variables.
 */
#define qatomic_rcu_read_internal(ptr, _val)            \
    ({                                                  \
    qemu_build_assert(sizeof(*ptr) <= ATOMIC_REG_SIZE); \
    typeof_strip_qual(*ptr) _val;                       \
    qatomic_rcu_read__nocheck(ptr, &_val);              \
    _val;                                               \
    })
#define qatomic_rcu_read(ptr) \
    qatomic_rcu_read_internal((ptr), MAKE_IDENTFIER(_val))

#define qatomic_rcu_set(ptr, i) do {                   \
    qemu_build_assert(sizeof(*ptr) <= ATOMIC_REG_SIZE); \
    __atomic_store_n(ptr, i, __ATOMIC_RELEASE);        \
} while(0)

#define qatomic_load_acquire(ptr)                       \
    ({                                                  \
    qemu_build_assert(sizeof(*ptr) <= ATOMIC_REG_SIZE); \
    typeof_strip_qual(*ptr) _val;                       \
    __atomic_load(ptr, &_val, __ATOMIC_ACQUIRE);        \
    _val;                                               \
    })

#define qatomic_store_release(ptr, i)  do {             \
    qemu_build_assert(sizeof(*ptr) <= ATOMIC_REG_SIZE); \
    __atomic_store_n(ptr, i, __ATOMIC_RELEASE);         \
} while(0)


/* All the remaining operations are fully sequentially consistent */

#define qatomic_xchg__nocheck(ptr, i)    ({                 \
    __atomic_exchange_n(ptr, (i), __ATOMIC_SEQ_CST);        \
})

#define qatomic_xchg(ptr, i)    ({                          \
    qemu_build_assert(sizeof(*ptr) <= ATOMIC_REG_SIZE);     \
    qatomic_xchg__nocheck(ptr, i);                          \
})

/* Returns the old value of '*ptr' (whether the cmpxchg failed or not) */
#define qatomic_cmpxchg__nocheck(ptr, old, new)    ({                   \
    typeof_strip_qual(*ptr) _old = (old);                               \
    (void)__atomic_compare_exchange_n(ptr, &_old, new, false,           \
                              __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);      \
    _old;                                                               \
})

#define qatomic_cmpxchg(ptr, old, new)    ({                            \
    qemu_build_assert(sizeof(*ptr) <= ATOMIC_REG_SIZE);                 \
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

#define smp_wmb()   smp_mb_release()
#define smp_rmb()   smp_mb_acquire()

/*
 * SEQ_CST is weaker than the older __sync_* builtins and Linux
 * kernel read-modify-write atomics.  Provide a macro to obtain
 * the same semantics.
 */
#if !defined(QEMU_SANITIZE_THREAD) && \
    (defined(__i386__) || defined(__x86_64__) || defined(__s390x__))
# define smp_mb__before_rmw() signal_barrier()
# define smp_mb__after_rmw() signal_barrier()
#else
# define smp_mb__before_rmw() smp_mb()
# define smp_mb__after_rmw() smp_mb()
#endif

/*
 * On some architectures, qatomic_set_mb is more efficient than a store
 * plus a fence.
 */

#if !defined(QEMU_SANITIZE_THREAD) && \
    (defined(__i386__) || defined(__x86_64__) || defined(__s390x__))
# define qatomic_set_mb(ptr, i) \
    ({ (void)qatomic_xchg(ptr, i); smp_mb__after_rmw(); })
#else
# define qatomic_set_mb(ptr, i) \
   ({ qatomic_store_release(ptr, i); smp_mb(); })
#endif

#define qatomic_fetch_inc_nonzero(ptr) ({                               \
    typeof_strip_qual(*ptr) _oldn = qatomic_read(ptr);                  \
    while (_oldn && qatomic_cmpxchg(ptr, _oldn, _oldn + 1) != _oldn) {  \
        _oldn = qatomic_read(ptr);                                      \
    }                                                                   \
    _oldn;                                                              \
})

/*
 * Abstractions to access atomically (i.e. "once") i64/u64 variables.
 *
 * The i386 abi is odd in that by default members are only aligned to
 * 4 bytes, which means that 8-byte types can wind up mis-aligned.
 * Clang will then warn about this, and emit a call into libatomic.
 *
 * Use of these types in structures when they will be used with atomic
 * operations can avoid this.
 */
typedef int64_t aligned_int64_t __attribute__((aligned(8)));
typedef uint64_t aligned_uint64_t __attribute__((aligned(8)));

#ifdef CONFIG_ATOMIC64
/* Use __nocheck because sizeof(void *) might be < sizeof(u64) */
#define qatomic_read_i64(P) \
    _Generic(*(P), int64_t: qatomic_read__nocheck(P))
#define qatomic_read_u64(P) \
    _Generic(*(P), uint64_t: qatomic_read__nocheck(P))
#define qatomic_set_i64(P, V) \
    _Generic(*(P), int64_t: qatomic_set__nocheck(P, V))
#define qatomic_set_u64(P, V) \
    _Generic(*(P), uint64_t: qatomic_set__nocheck(P, V))

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
