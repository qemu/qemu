/*
 * QEMU TCG support -- s390x vector floating point instruction support
 *
 * Copyright (C) 2019 Red Hat Inc
 *
 * Authors:
 *   David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "internal.h"
#include "vec.h"
#include "tcg_s390x.h"
#include "tcg/tcg-gvec-desc.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"

#define VIC_INVALID         0x1
#define VIC_DIVBYZERO       0x2
#define VIC_OVERFLOW        0x3
#define VIC_UNDERFLOW       0x4
#define VIC_INEXACT         0x5

/* returns the VEX. If the VEX is 0, there is no trap */
static uint8_t check_ieee_exc(CPUS390XState *env, uint8_t enr, bool XxC,
                              uint8_t *vec_exc)
{
    uint8_t vece_exc = 0, trap_exc;
    unsigned qemu_exc;

    /* Retrieve and clear the softfloat exceptions */
    qemu_exc = env->fpu_status.float_exception_flags;
    if (qemu_exc == 0) {
        return 0;
    }
    env->fpu_status.float_exception_flags = 0;

    vece_exc = s390_softfloat_exc_to_ieee(qemu_exc);

    /* Add them to the vector-wide s390x exception bits */
    *vec_exc |= vece_exc;

    /* Check for traps and construct the VXC */
    trap_exc = vece_exc & env->fpc >> 24;
    if (trap_exc) {
        if (trap_exc & S390_IEEE_MASK_INVALID) {
            return enr << 4 | VIC_INVALID;
        } else if (trap_exc & S390_IEEE_MASK_DIVBYZERO) {
            return enr << 4 | VIC_DIVBYZERO;
        } else if (trap_exc & S390_IEEE_MASK_OVERFLOW) {
            return enr << 4 | VIC_OVERFLOW;
        } else if (trap_exc & S390_IEEE_MASK_UNDERFLOW) {
            return enr << 4 | VIC_UNDERFLOW;
        } else if (!XxC) {
            g_assert(trap_exc & S390_IEEE_MASK_INEXACT);
            /* inexact has lowest priority on traps */
            return enr << 4 | VIC_INEXACT;
        }
    }
    return 0;
}

static void handle_ieee_exc(CPUS390XState *env, uint8_t vxc, uint8_t vec_exc,
                            uintptr_t retaddr)
{
    if (vxc) {
        /* on traps, the fpc flags are not updated, instruction is suppressed */
        tcg_s390_vector_exception(env, vxc, retaddr);
    }
    if (vec_exc) {
        /* indicate exceptions for all elements combined */
        env->fpc |= vec_exc << 16;
    }
}

typedef uint64_t (*vop64_3_fn)(uint64_t a, uint64_t b, float_status *s);
static void vop64_3(S390Vector *v1, const S390Vector *v2, const S390Vector *v3,
                    CPUS390XState *env, bool s, vop64_3_fn fn,
                    uintptr_t retaddr)
{
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i;

    for (i = 0; i < 2; i++) {
        const uint64_t a = s390_vec_read_element64(v2, i);
        const uint64_t b = s390_vec_read_element64(v3, i);

        s390_vec_write_element64(&tmp, i, fn(a, b, &env->fpu_status));
        vxc = check_ieee_exc(env, i, false, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    handle_ieee_exc(env, vxc, vec_exc, retaddr);
    *v1 = tmp;
}

static uint64_t vfa64(uint64_t a, uint64_t b, float_status *s)
{
    return float64_add(a, b, s);
}

void HELPER(gvec_vfa64)(void *v1, const void *v2, const void *v3,
                        CPUS390XState *env, uint32_t desc)
{
    vop64_3(v1, v2, v3, env, false, vfa64, GETPC());
}

void HELPER(gvec_vfa64s)(void *v1, const void *v2, const void *v3,
                         CPUS390XState *env, uint32_t desc)
{
    vop64_3(v1, v2, v3, env, true, vfa64, GETPC());
}
