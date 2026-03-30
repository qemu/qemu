/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * guest <-> host helpers.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 */

#ifndef USER_GUEST_HOST_H
#define USER_GUEST_HOST_H

#include "exec/vaddr.h"
#include "user/guest-base.h"
#include "accel/tcg/cpu-ops.h"

/*
 * If non-zero, the guest virtual address space is a contiguous subset
 * of the host virtual address space, i.e. '-R reserved_va' is in effect
 * either from the command-line or by default.  The value is the last
 * byte of the guest address space e.g. UINT32_MAX.
 *
 * If zero, the host and guest virtual address spaces are intermingled.
 */
extern unsigned long reserved_va;

/*
 * The last byte of the guest address space.
 * If reserved_va is non-zero, guest_addr_max matches.
 * If reserved_va is zero, guest_addr_max equals the full guest space.
 */
extern unsigned long guest_addr_max;

/*
 * These functions take the guest virtual address as a vaddr,
 * and are suitable for use from target-independent code.
 */

static inline vaddr cpu_untagged_addr_vaddr(CPUState *cs, vaddr x)
{
    const TCGCPUOps *tcg_ops = cs->cc->tcg_ops;
    if (tcg_ops->untagged_addr) {
        return tcg_ops->untagged_addr(cs, x);
    }
    return x;
}

/* All direct uses of g2h and h2g need to go away for usermode softmmu.  */
static inline void *g2h_untagged_vaddr(vaddr x)
{
    return (void *)((uintptr_t)(x) + guest_base);
}

static inline void *g2h_vaddr(CPUState *cs, vaddr x)
{
    return g2h_untagged_vaddr(cpu_untagged_addr_vaddr(cs, x));
}

static inline bool guest_addr_valid_untagged_vaddr(vaddr x)
{
    return x <= guest_addr_max;
}

static inline bool guest_range_valid_untagged_vaddr(vaddr start, vaddr len)
{
    return len - 1 <= guest_addr_max && start <= guest_addr_max - len + 1;
}

#define h2g_valid(x) \
    ((uintptr_t)(x) - guest_base <= guest_addr_max)

#define h2g_nocheck(x) ({ \
    uintptr_t __ret = (uintptr_t)(x) - guest_base; \
    (vaddr)__ret; \
})

#define h2g(x) ({ \
    /* Check if given address fits target address space */ \
    assert(h2g_valid(x)); \
    h2g_nocheck(x); \
})

#ifdef COMPILING_PER_TARGET

/*
 * These functions take the guest virtual address as an abi_ptr.  This
 * is an important difference from a vaddr for the common case where
 * the address is a syscall argument in a variable of type abi_long,
 * which may be smaller than the vaddr type. If you pass an address in
 * an abi_long to these functions then the value will be converted to
 * an unsigned type and then zero extended to give the vaddr. If you
 * use the g2h_vaddr() and similar functions which take an argument of
 * type vaddr, then the value will be sign-extended, giving the wrong
 * answer for addresses above the 2GB mark on 32-bit guests.
 *
 * Providing these functions with their traditional QEMU semantics is
 * less bug-prone than requiring many callsites to remember to cast
 * their abi_long variable to an abi_ptr before calling.
 */

static inline void *g2h(CPUState *cs, abi_ptr x)
{
    return g2h_vaddr(cs, x);
}

static inline void *g2h_untagged(abi_ptr x)
{
    return g2h_untagged_vaddr(x);
}

static inline bool guest_addr_valid_untagged(abi_ptr x)
{
    return guest_addr_valid_untagged_vaddr(x);
}

static inline bool guest_range_valid_untagged(abi_ptr start, abi_ptr len)
{
    return guest_range_valid_untagged_vaddr(start, len);
}

static inline abi_ptr cpu_untagged_addr(CPUState *cs, abi_ptr x)
{
    return cpu_untagged_addr_vaddr(cs, x);
}

#endif

#endif
