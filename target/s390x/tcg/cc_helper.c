/*
 *  S/390 condition code helper routines
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2009 Alexander Graf
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "s390x-internal.h"
#include "tcg_s390x.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"

/* #define DEBUG_HELPER */
#ifdef DEBUG_HELPER
#define HELPER_LOG(x...) qemu_log(x)
#else
#define HELPER_LOG(x...)
#endif

static uint32_t cc_calc_ltgt_32(int32_t src, int32_t dst)
{
    if (src == dst) {
        return 0;
    } else if (src < dst) {
        return 1;
    } else {
        return 2;
    }
}

static uint32_t cc_calc_ltgt0_32(int32_t dst)
{
    return cc_calc_ltgt_32(dst, 0);
}

static uint32_t cc_calc_ltgt_64(int64_t src, int64_t dst)
{
    if (src == dst) {
        return 0;
    } else if (src < dst) {
        return 1;
    } else {
        return 2;
    }
}

static uint32_t cc_calc_ltgt0_64(int64_t dst)
{
    return cc_calc_ltgt_64(dst, 0);
}

static uint32_t cc_calc_ltugtu_32(uint32_t src, uint32_t dst)
{
    if (src == dst) {
        return 0;
    } else if (src < dst) {
        return 1;
    } else {
        return 2;
    }
}

static uint32_t cc_calc_ltugtu_64(uint64_t src, uint64_t dst)
{
    if (src == dst) {
        return 0;
    } else if (src < dst) {
        return 1;
    } else {
        return 2;
    }
}

static uint32_t cc_calc_tm_32(uint32_t val, uint32_t mask)
{
    uint32_t r = val & mask;

    if (r == 0) {
        return 0;
    } else if (r == mask) {
        return 3;
    } else {
        return 1;
    }
}

static uint32_t cc_calc_tm_64(uint64_t val, uint64_t mask)
{
    uint64_t r = val & mask;

    if (r == 0) {
        return 0;
    } else if (r == mask) {
        return 3;
    } else {
        int top = clz64(mask);
        if ((int64_t)(val << top) < 0) {
            return 2;
        } else {
            return 1;
        }
    }
}

static uint32_t cc_calc_nz(uint64_t dst)
{
    return !!dst;
}

static uint32_t cc_calc_addu(uint64_t carry_out, uint64_t result)
{
    g_assert(carry_out <= 1);
    return (result != 0) + 2 * carry_out;
}

static uint32_t cc_calc_subu(uint64_t borrow_out, uint64_t result)
{
    return cc_calc_addu(borrow_out + 1, result);
}

static uint32_t cc_calc_add_64(int64_t a1, int64_t a2, int64_t ar)
{
    if ((a1 > 0 && a2 > 0 && ar < 0) || (a1 < 0 && a2 < 0 && ar > 0)) {
        return 3; /* overflow */
    } else {
        if (ar < 0) {
            return 1;
        } else if (ar > 0) {
            return 2;
        } else {
            return 0;
        }
    }
}

static uint32_t cc_calc_sub_64(int64_t a1, int64_t a2, int64_t ar)
{
    if ((a1 > 0 && a2 < 0 && ar < 0) || (a1 < 0 && a2 > 0 && ar > 0)) {
        return 3; /* overflow */
    } else {
        if (ar < 0) {
            return 1;
        } else if (ar > 0) {
            return 2;
        } else {
            return 0;
        }
    }
}

static uint32_t cc_calc_abs_64(int64_t dst)
{
    if ((uint64_t)dst == 0x8000000000000000ULL) {
        return 3;
    } else if (dst) {
        return 2;
    } else {
        return 0;
    }
}

static uint32_t cc_calc_nabs_64(int64_t dst)
{
    return !!dst;
}

static uint32_t cc_calc_comp_64(int64_t dst)
{
    if ((uint64_t)dst == 0x8000000000000000ULL) {
        return 3;
    } else if (dst < 0) {
        return 1;
    } else if (dst > 0) {
        return 2;
    } else {
        return 0;
    }
}


static uint32_t cc_calc_add_32(int32_t a1, int32_t a2, int32_t ar)
{
    if ((a1 > 0 && a2 > 0 && ar < 0) || (a1 < 0 && a2 < 0 && ar > 0)) {
        return 3; /* overflow */
    } else {
        if (ar < 0) {
            return 1;
        } else if (ar > 0) {
            return 2;
        } else {
            return 0;
        }
    }
}

static uint32_t cc_calc_sub_32(int32_t a1, int32_t a2, int32_t ar)
{
    if ((a1 > 0 && a2 < 0 && ar < 0) || (a1 < 0 && a2 > 0 && ar > 0)) {
        return 3; /* overflow */
    } else {
        if (ar < 0) {
            return 1;
        } else if (ar > 0) {
            return 2;
        } else {
            return 0;
        }
    }
}

static uint32_t cc_calc_abs_32(int32_t dst)
{
    if ((uint32_t)dst == 0x80000000UL) {
        return 3;
    } else if (dst) {
        return 2;
    } else {
        return 0;
    }
}

static uint32_t cc_calc_nabs_32(int32_t dst)
{
    return !!dst;
}

static uint32_t cc_calc_comp_32(int32_t dst)
{
    if ((uint32_t)dst == 0x80000000UL) {
        return 3;
    } else if (dst < 0) {
        return 1;
    } else if (dst > 0) {
        return 2;
    } else {
        return 0;
    }
}

/* calculate condition code for insert character under mask insn */
static uint32_t cc_calc_icm(uint64_t mask, uint64_t val)
{
    if ((val & mask) == 0) {
        return 0;
    } else {
        int top = clz64(mask);
        if ((int64_t)(val << top) < 0) {
            return 1;
        } else {
            return 2;
        }
    }
}

static uint32_t cc_calc_sla_32(uint32_t src, int shift)
{
    uint32_t mask = ((1U << shift) - 1U) << (32 - shift);
    uint32_t sign = 1U << 31;
    uint32_t match;
    int32_t r;

    /* Check if the sign bit stays the same.  */
    if (src & sign) {
        match = mask;
    } else {
        match = 0;
    }
    if ((src & mask) != match) {
        /* Overflow.  */
        return 3;
    }

    r = ((src << shift) & ~sign) | (src & sign);
    if (r == 0) {
        return 0;
    } else if (r < 0) {
        return 1;
    }
    return 2;
}

static uint32_t cc_calc_sla_64(uint64_t src, int shift)
{
    uint64_t mask = ((1ULL << shift) - 1ULL) << (64 - shift);
    uint64_t sign = 1ULL << 63;
    uint64_t match;
    int64_t r;

    /* Check if the sign bit stays the same.  */
    if (src & sign) {
        match = mask;
    } else {
        match = 0;
    }
    if ((src & mask) != match) {
        /* Overflow.  */
        return 3;
    }

    r = ((src << shift) & ~sign) | (src & sign);
    if (r == 0) {
        return 0;
    } else if (r < 0) {
        return 1;
    }
    return 2;
}

static uint32_t cc_calc_flogr(uint64_t dst)
{
    return dst ? 2 : 0;
}

static uint32_t cc_calc_lcbb(uint64_t dst)
{
    return dst == 16 ? 0 : 3;
}

static uint32_t cc_calc_vc(uint64_t low, uint64_t high)
{
    if (high == -1ull && low == -1ull) {
        /* all elements match */
        return 0;
    } else if (high == 0 && low == 0) {
        /* no elements match */
        return 3;
    } else {
        /* some elements but not all match */
        return 1;
    }
}

static uint32_t cc_calc_muls_32(int64_t res)
{
    const int64_t tmp = res >> 31;

    if (!res) {
        return 0;
    } else if (tmp && tmp != -1) {
        return 3;
    } else if (res < 0) {
        return 1;
    }
    return 2;
}

static uint64_t cc_calc_muls_64(int64_t res_high, uint64_t res_low)
{
    if (!res_high && !res_low) {
        return 0;
    } else if (res_high + (res_low >> 63) != 0) {
        return 3;
    } else if (res_high < 0) {
        return 1;
    }
    return 2;
}

static uint32_t do_calc_cc(CPUS390XState *env, uint32_t cc_op,
                                  uint64_t src, uint64_t dst, uint64_t vr)
{
    uint32_t r = 0;

    switch (cc_op) {
    case CC_OP_CONST0:
    case CC_OP_CONST1:
    case CC_OP_CONST2:
    case CC_OP_CONST3:
        /* cc_op value _is_ cc */
        r = cc_op;
        break;
    case CC_OP_LTGT0_32:
        r = cc_calc_ltgt0_32(dst);
        break;
    case CC_OP_LTGT0_64:
        r =  cc_calc_ltgt0_64(dst);
        break;
    case CC_OP_LTGT_32:
        r =  cc_calc_ltgt_32(src, dst);
        break;
    case CC_OP_LTGT_64:
        r =  cc_calc_ltgt_64(src, dst);
        break;
    case CC_OP_LTUGTU_32:
        r =  cc_calc_ltugtu_32(src, dst);
        break;
    case CC_OP_LTUGTU_64:
        r =  cc_calc_ltugtu_64(src, dst);
        break;
    case CC_OP_TM_32:
        r =  cc_calc_tm_32(src, dst);
        break;
    case CC_OP_TM_64:
        r =  cc_calc_tm_64(src, dst);
        break;
    case CC_OP_NZ:
        r =  cc_calc_nz(dst);
        break;
    case CC_OP_ADDU:
        r = cc_calc_addu(src, dst);
        break;
    case CC_OP_SUBU:
        r = cc_calc_subu(src, dst);
        break;
    case CC_OP_ADD_64:
        r =  cc_calc_add_64(src, dst, vr);
        break;
    case CC_OP_SUB_64:
        r =  cc_calc_sub_64(src, dst, vr);
        break;
    case CC_OP_ABS_64:
        r =  cc_calc_abs_64(dst);
        break;
    case CC_OP_NABS_64:
        r =  cc_calc_nabs_64(dst);
        break;
    case CC_OP_COMP_64:
        r =  cc_calc_comp_64(dst);
        break;
    case CC_OP_MULS_64:
        r = cc_calc_muls_64(src, dst);
        break;

    case CC_OP_ADD_32:
        r =  cc_calc_add_32(src, dst, vr);
        break;
    case CC_OP_SUB_32:
        r =  cc_calc_sub_32(src, dst, vr);
        break;
    case CC_OP_ABS_32:
        r =  cc_calc_abs_32(dst);
        break;
    case CC_OP_NABS_32:
        r =  cc_calc_nabs_32(dst);
        break;
    case CC_OP_COMP_32:
        r =  cc_calc_comp_32(dst);
        break;
    case CC_OP_MULS_32:
        r = cc_calc_muls_32(dst);
        break;

    case CC_OP_ICM:
        r =  cc_calc_icm(src, dst);
        break;
    case CC_OP_SLA_32:
        r =  cc_calc_sla_32(src, dst);
        break;
    case CC_OP_SLA_64:
        r =  cc_calc_sla_64(src, dst);
        break;
    case CC_OP_FLOGR:
        r = cc_calc_flogr(dst);
        break;
    case CC_OP_LCBB:
        r = cc_calc_lcbb(dst);
        break;
    case CC_OP_VC:
        r = cc_calc_vc(src, dst);
        break;

    case CC_OP_NZ_F32:
        r = set_cc_nz_f32(dst);
        break;
    case CC_OP_NZ_F64:
        r = set_cc_nz_f64(dst);
        break;
    case CC_OP_NZ_F128:
        r = set_cc_nz_f128(make_float128(src, dst));
        break;

    default:
        cpu_abort(env_cpu(env), "Unknown CC operation: %s\n", cc_name(cc_op));
    }

    HELPER_LOG("%s: %15s 0x%016lx 0x%016lx 0x%016lx = %d\n", __func__,
               cc_name(cc_op), src, dst, vr, r);
    return r;
}

uint32_t calc_cc(CPUS390XState *env, uint32_t cc_op, uint64_t src, uint64_t dst,
                 uint64_t vr)
{
    return do_calc_cc(env, cc_op, src, dst, vr);
}

uint32_t HELPER(calc_cc)(CPUS390XState *env, uint32_t cc_op, uint64_t src,
                         uint64_t dst, uint64_t vr)
{
    return do_calc_cc(env, cc_op, src, dst, vr);
}

#ifndef CONFIG_USER_ONLY
void HELPER(load_psw)(CPUS390XState *env, uint64_t mask, uint64_t addr)
{
    s390_cpu_set_psw(env, mask, addr);
    cpu_loop_exit(env_cpu(env));
}

void HELPER(sacf)(CPUS390XState *env, uint64_t a1)
{
    HELPER_LOG("%s: %16" PRIx64 "\n", __func__, a1);

    switch (a1 & 0xf00) {
    case 0x000:
        env->psw.mask &= ~PSW_MASK_ASC;
        env->psw.mask |= PSW_ASC_PRIMARY;
        break;
    case 0x100:
        env->psw.mask &= ~PSW_MASK_ASC;
        env->psw.mask |= PSW_ASC_SECONDARY;
        break;
    case 0x300:
        env->psw.mask &= ~PSW_MASK_ASC;
        env->psw.mask |= PSW_ASC_HOME;
        break;
    default:
        HELPER_LOG("unknown sacf mode: %" PRIx64 "\n", a1);
        tcg_s390_program_interrupt(env, PGM_SPECIFICATION, GETPC());
    }
}
#endif
