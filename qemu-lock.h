/*
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

/* Locking primitives.  Most of this code should be redundant -
   system emulation doesn't need/use locking, NPTL userspace uses
   pthread mutexes, and non-NPTL userspace isn't threadsafe anyway.
   In either case a spinlock is probably the wrong kind of lock.
   Spinlocks are only good if you know annother CPU has the lock and is
   likely to release it soon.  In environments where you have more threads
   than physical CPUs (the extreme case being a single CPU host) a spinlock
   simply wastes CPU until the OS decides to preempt it.  */
#if defined(CONFIG_USE_NPTL)

#include <pthread.h>
#define spin_lock pthread_mutex_lock
#define spin_unlock pthread_mutex_unlock
#define spinlock_t pthread_mutex_t
#define SPIN_LOCK_UNLOCKED PTHREAD_MUTEX_INITIALIZER

#else

#if defined(__hppa__)

typedef int spinlock_t[4];

#define SPIN_LOCK_UNLOCKED { 1, 1, 1, 1 }

static inline void resetlock (spinlock_t *p)
{
    (*p)[0] = (*p)[1] = (*p)[2] = (*p)[3] = 1;
}

#else

typedef int spinlock_t;

#define SPIN_LOCK_UNLOCKED 0

static inline void resetlock (spinlock_t *p)
{
    *p = SPIN_LOCK_UNLOCKED;
}

#endif

#if defined(_ARCH_PPC)
static inline int testandset (int *p)
{
    int ret;
    __asm__ __volatile__ (
                          "      lwarx %0,0,%1\n"
                          "      xor. %0,%3,%0\n"
                          "      bne $+12\n"
                          "      stwcx. %2,0,%1\n"
                          "      bne- $-16\n"
                          : "=&r" (ret)
                          : "r" (p), "r" (1), "r" (0)
                          : "cr0", "memory");
    return ret;
}
#elif defined(__i386__)
static inline int testandset (int *p)
{
    long int readval = 0;

    __asm__ __volatile__ ("lock; cmpxchgl %2, %0"
                          : "+m" (*p), "+a" (readval)
                          : "r" (1)
                          : "cc");
    return readval;
}
#elif defined(__x86_64__)
static inline int testandset (int *p)
{
    long int readval = 0;

    __asm__ __volatile__ ("lock; cmpxchgl %2, %0"
                          : "+m" (*p), "+a" (readval)
                          : "r" (1)
                          : "cc");
    return readval;
}
#elif defined(__s390__)
static inline int testandset (int *p)
{
    int ret;

    __asm__ __volatile__ ("0: cs    %0,%1,0(%2)\n"
			  "   jl    0b"
			  : "=&d" (ret)
			  : "r" (1), "a" (p), "0" (*p)
			  : "cc", "memory" );
    return ret;
}
#elif defined(__alpha__)
static inline int testandset (int *p)
{
    int ret;
    unsigned long one;

    __asm__ __volatile__ ("0:	mov 1,%2\n"
			  "	ldl_l %0,%1\n"
			  "	stl_c %2,%1\n"
			  "	beq %2,1f\n"
			  ".subsection 2\n"
			  "1:	br 0b\n"
			  ".previous"
			  : "=r" (ret), "=m" (*p), "=r" (one)
			  : "m" (*p));
    return ret;
}
#elif defined(__sparc__)
static inline int testandset (int *p)
{
	int ret;

	__asm__ __volatile__("ldstub	[%1], %0"
			     : "=r" (ret)
			     : "r" (p)
			     : "memory");

	return (ret ? 1 : 0);
}
#elif defined(__arm__)
static inline int testandset (int *spinlock)
{
    register unsigned int ret;
    __asm__ __volatile__("swp %0, %1, [%2]"
                         : "=r"(ret)
                         : "0"(1), "r"(spinlock));

    return ret;
}
#elif defined(__mc68000)
static inline int testandset (int *p)
{
    char ret;
    __asm__ __volatile__("tas %1; sne %0"
                         : "=r" (ret)
                         : "m" (p)
                         : "cc","memory");
    return ret;
}
#elif defined(__hppa__)

/* Because malloc only guarantees 8-byte alignment for malloc'd data,
   and GCC only guarantees 8-byte alignment for stack locals, we can't
   be assured of 16-byte alignment for atomic lock data even if we
   specify "__attribute ((aligned(16)))" in the type declaration.  So,
   we use a struct containing an array of four ints for the atomic lock
   type and dynamically select the 16-byte aligned int from the array
   for the semaphore.  */
#define __PA_LDCW_ALIGNMENT 16
static inline void *ldcw_align (void *p) {
    unsigned long a = (unsigned long)p;
    a = (a + __PA_LDCW_ALIGNMENT - 1) & ~(__PA_LDCW_ALIGNMENT - 1);
    return (void *)a;
}

static inline int testandset (spinlock_t *p)
{
    unsigned int ret;
    p = ldcw_align(p);
    __asm__ __volatile__("ldcw 0(%1),%0"
                         : "=r" (ret)
                         : "r" (p)
                         : "memory" );
    return !ret;
}

#elif defined(__ia64)

#include <ia64intrin.h>

static inline int testandset (int *p)
{
    return __sync_lock_test_and_set (p, 1);
}
#elif defined(__mips__)
static inline int testandset (int *p)
{
    int ret;

    __asm__ __volatile__ (
	"	.set push		\n"
	"	.set noat		\n"
	"	.set mips2		\n"
	"1:	li	$1, 1		\n"
	"	ll	%0, %1		\n"
	"	sc	$1, %1		\n"
	"	beqz	$1, 1b		\n"
	"	.set pop		"
	: "=r" (ret), "+R" (*p)
	:
	: "memory");

    return ret;
}
#else
#error unimplemented CPU support
#endif

#if defined(CONFIG_USER_ONLY)
static inline void spin_lock(spinlock_t *lock)
{
    while (testandset(lock));
}

static inline void spin_unlock(spinlock_t *lock)
{
    resetlock(lock);
}
#else
static inline void spin_lock(spinlock_t *lock)
{
}

static inline void spin_unlock(spinlock_t *lock)
{
}
#endif

#endif
