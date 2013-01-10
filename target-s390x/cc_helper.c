/*
 *  S/390 condition code helper routines
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2009 Alexander Graf
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "helper.h"

/* #define DEBUG_HELPER */
#ifdef DEBUG_HELPER
#define HELPER_LOG(x...) qemu_log(x)
#else
#define HELPER_LOG(x...)
#endif

static inline uint32_t cc_calc_ltgt_32(CPUS390XState *env, int32_t src,
                                       int32_t dst)
{
    if (src == dst) {
        return 0;
    } else if (src < dst) {
        return 1;
    } else {
        return 2;
    }
}

static inline uint32_t cc_calc_ltgt0_32(CPUS390XState *env, int32_t dst)
{
    return cc_calc_ltgt_32(env, dst, 0);
}

static inline uint32_t cc_calc_ltgt_64(CPUS390XState *env, int64_t src,
                                       int64_t dst)
{
    if (src == dst) {
        return 0;
    } else if (src < dst) {
        return 1;
    } else {
        return 2;
    }
}

static inline uint32_t cc_calc_ltgt0_64(CPUS390XState *env, int64_t dst)
{
    return cc_calc_ltgt_64(env, dst, 0);
}

static inline uint32_t cc_calc_ltugtu_32(CPUS390XState *env, uint32_t src,
                                         uint32_t dst)
{
    if (src == dst) {
        return 0;
    } else if (src < dst) {
        return 1;
    } else {
        return 2;
    }
}

static inline uint32_t cc_calc_ltugtu_64(CPUS390XState *env, uint64_t src,
                                         uint64_t dst)
{
    if (src == dst) {
        return 0;
    } else if (src < dst) {
        return 1;
    } else {
        return 2;
    }
}

static inline uint32_t cc_calc_tm_32(CPUS390XState *env, uint32_t val,
                                     uint32_t mask)
{
    uint16_t r = val & mask;

    HELPER_LOG("%s: val 0x%x mask 0x%x\n", __func__, val, mask);
    if (r == 0 || mask == 0) {
        return 0;
    } else if (r == mask) {
        return 3;
    } else {
        return 1;
    }
}

/* set condition code for test under mask */
static inline uint32_t cc_calc_tm_64(CPUS390XState *env, uint64_t val,
                                     uint32_t mask)
{
    uint16_t r = val & mask;

    HELPER_LOG("%s: val 0x%lx mask 0x%x r 0x%x\n", __func__, val, mask, r);
    if (r == 0 || mask == 0) {
        return 0;
    } else if (r == mask) {
        return 3;
    } else {
        while (!(mask & 0x8000)) {
            mask <<= 1;
            val <<= 1;
        }
        if (val & 0x8000) {
            return 2;
        } else {
            return 1;
        }
    }
}

static inline uint32_t cc_calc_nz(CPUS390XState *env, uint64_t dst)
{
    return !!dst;
}

static inline uint32_t cc_calc_add_64(CPUS390XState *env, int64_t a1,
                                      int64_t a2, int64_t ar)
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

static inline uint32_t cc_calc_addu_64(CPUS390XState *env, uint64_t a1,
                                       uint64_t a2, uint64_t ar)
{
    if (ar == 0) {
        if (a1) {
            return 2;
        } else {
            return 0;
        }
    } else {
        if (ar < a1 || ar < a2) {
            return 3;
        } else {
            return 1;
        }
    }
}

static inline uint32_t cc_calc_sub_64(CPUS390XState *env, int64_t a1,
                                      int64_t a2, int64_t ar)
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

static inline uint32_t cc_calc_subu_64(CPUS390XState *env, uint64_t a1,
                                       uint64_t a2, uint64_t ar)
{
    if (ar == 0) {
        return 2;
    } else {
        if (a2 > a1) {
            return 1;
        } else {
            return 3;
        }
    }
}

static inline uint32_t cc_calc_abs_64(CPUS390XState *env, int64_t dst)
{
    if ((uint64_t)dst == 0x8000000000000000ULL) {
        return 3;
    } else if (dst) {
        return 1;
    } else {
        return 0;
    }
}

static inline uint32_t cc_calc_nabs_64(CPUS390XState *env, int64_t dst)
{
    return !!dst;
}

static inline uint32_t cc_calc_comp_64(CPUS390XState *env, int64_t dst)
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


static inline uint32_t cc_calc_add_32(CPUS390XState *env, int32_t a1,
                                      int32_t a2, int32_t ar)
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

static inline uint32_t cc_calc_addu_32(CPUS390XState *env, uint32_t a1,
                                       uint32_t a2, uint32_t ar)
{
    if (ar == 0) {
        if (a1) {
            return 2;
        } else {
            return 0;
        }
    } else {
        if (ar < a1 || ar < a2) {
            return 3;
        } else {
            return 1;
        }
    }
}

static inline uint32_t cc_calc_sub_32(CPUS390XState *env, int32_t a1,
                                      int32_t a2, int32_t ar)
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

static inline uint32_t cc_calc_subu_32(CPUS390XState *env, uint32_t a1,
                                       uint32_t a2, uint32_t ar)
{
    if (ar == 0) {
        return 2;
    } else {
        if (a2 > a1) {
            return 1;
        } else {
            return 3;
        }
    }
}

static inline uint32_t cc_calc_abs_32(CPUS390XState *env, int32_t dst)
{
    if ((uint32_t)dst == 0x80000000UL) {
        return 3;
    } else if (dst) {
        return 1;
    } else {
        return 0;
    }
}

static inline uint32_t cc_calc_nabs_32(CPUS390XState *env, int32_t dst)
{
    return !!dst;
}

static inline uint32_t cc_calc_comp_32(CPUS390XState *env, int32_t dst)
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
static inline uint32_t cc_calc_icm_32(CPUS390XState *env, uint32_t mask,
                                      uint32_t val)
{
    uint32_t cc;

    HELPER_LOG("%s: mask 0x%x val %d\n", __func__, mask, val);
    if (mask == 0xf) {
        if (!val) {
            return 0;
        } else if (val & 0x80000000) {
            return 1;
        } else {
            return 2;
        }
    }

    if (!val || !mask) {
        cc = 0;
    } else {
        while (mask != 1) {
            mask >>= 1;
            val >>= 8;
        }
        if (val & 0x80) {
            cc = 1;
        } else {
            cc = 2;
        }
    }
    return cc;
}

static inline uint32_t cc_calc_slag(CPUS390XState *env, uint64_t src,
                                    uint64_t shift)
{
    uint64_t mask = ((1ULL << shift) - 1ULL) << (64 - shift);
    uint64_t match, r;

    /* check if the sign bit stays the same */
    if (src & (1ULL << 63)) {
        match = mask;
    } else {
        match = 0;
    }

    if ((src & mask) != match) {
        /* overflow */
        return 3;
    }

    r = ((src << shift) & ((1ULL << 63) - 1)) | (src & (1ULL << 63));

    if ((int64_t)r == 0) {
        return 0;
    } else if ((int64_t)r < 0) {
        return 1;
    }

    return 2;
}


static inline uint32_t do_calc_cc(CPUS390XState *env, uint32_t cc_op,
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
        r = cc_calc_ltgt0_32(env, dst);
        break;
    case CC_OP_LTGT0_64:
        r =  cc_calc_ltgt0_64(env, dst);
        break;
    case CC_OP_LTGT_32:
        r =  cc_calc_ltgt_32(env, src, dst);
        break;
    case CC_OP_LTGT_64:
        r =  cc_calc_ltgt_64(env, src, dst);
        break;
    case CC_OP_LTUGTU_32:
        r =  cc_calc_ltugtu_32(env, src, dst);
        break;
    case CC_OP_LTUGTU_64:
        r =  cc_calc_ltugtu_64(env, src, dst);
        break;
    case CC_OP_TM_32:
        r =  cc_calc_tm_32(env, src, dst);
        break;
    case CC_OP_TM_64:
        r =  cc_calc_tm_64(env, src, dst);
        break;
    case CC_OP_NZ:
        r =  cc_calc_nz(env, dst);
        break;
    case CC_OP_ADD_64:
        r =  cc_calc_add_64(env, src, dst, vr);
        break;
    case CC_OP_ADDU_64:
        r =  cc_calc_addu_64(env, src, dst, vr);
        break;
    case CC_OP_SUB_64:
        r =  cc_calc_sub_64(env, src, dst, vr);
        break;
    case CC_OP_SUBU_64:
        r =  cc_calc_subu_64(env, src, dst, vr);
        break;
    case CC_OP_ABS_64:
        r =  cc_calc_abs_64(env, dst);
        break;
    case CC_OP_NABS_64:
        r =  cc_calc_nabs_64(env, dst);
        break;
    case CC_OP_COMP_64:
        r =  cc_calc_comp_64(env, dst);
        break;

    case CC_OP_ADD_32:
        r =  cc_calc_add_32(env, src, dst, vr);
        break;
    case CC_OP_ADDU_32:
        r =  cc_calc_addu_32(env, src, dst, vr);
        break;
    case CC_OP_SUB_32:
        r =  cc_calc_sub_32(env, src, dst, vr);
        break;
    case CC_OP_SUBU_32:
        r =  cc_calc_subu_32(env, src, dst, vr);
        break;
    case CC_OP_ABS_32:
        r =  cc_calc_abs_64(env, dst);
        break;
    case CC_OP_NABS_32:
        r =  cc_calc_nabs_64(env, dst);
        break;
    case CC_OP_COMP_32:
        r =  cc_calc_comp_32(env, dst);
        break;

    case CC_OP_ICM:
        r =  cc_calc_icm_32(env, src, dst);
        break;
    case CC_OP_SLAG:
        r =  cc_calc_slag(env, src, dst);
        break;

    case CC_OP_LTGT_F32:
        r = set_cc_f32(env, src, dst);
        break;
    case CC_OP_LTGT_F64:
        r = set_cc_f64(env, src, dst);
        break;
    case CC_OP_NZ_F32:
        r = set_cc_nz_f32(dst);
        break;
    case CC_OP_NZ_F64:
        r = set_cc_nz_f64(dst);
        break;

    default:
        cpu_abort(env, "Unknown CC operation: %s\n", cc_name(cc_op));
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

/* insert psw mask and condition code into r1 */
void HELPER(ipm)(CPUS390XState *env, uint32_t cc, uint32_t r1)
{
    uint64_t r = env->regs[r1];

    r &= 0xffffffff00ffffffULL;
    r |= (cc << 28) | ((env->psw.mask >> 40) & 0xf);
    env->regs[r1] = r;
    HELPER_LOG("%s: cc %d psw.mask 0x%lx r1 0x%lx\n", __func__,
               cc, env->psw.mask, r);
}

#ifndef CONFIG_USER_ONLY
void HELPER(load_psw)(CPUS390XState *env, uint64_t mask, uint64_t addr)
{
    load_psw(env, mask, addr);
    cpu_loop_exit(env);
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
        qemu_log("unknown sacf mode: %" PRIx64 "\n", a1);
        program_interrupt(env, PGM_SPECIFICATION, 2);
        break;
    }
}
#endif
