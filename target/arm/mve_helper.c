/*
 * M-profile MVE Operations
 *
 * Copyright (c) 2021 Linaro, Ltd.
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
#include "internals.h"
#include "vec_internal.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"

static uint16_t mve_element_mask(CPUARMState *env)
{
    /*
     * Return the mask of which elements in the MVE vector should be
     * updated. This is a combination of multiple things:
     *  (1) by default, we update every lane in the vector
     *  (2) VPT predication stores its state in the VPR register;
     *  (3) low-overhead-branch tail predication will mask out part
     *      the vector on the final iteration of the loop
     *  (4) if EPSR.ECI is set then we must execute only some beats
     *      of the insn
     * We combine all these into a 16-bit result with the same semantics
     * as VPR.P0: 0 to mask the lane, 1 if it is active.
     * 8-bit vector ops will look at all bits of the result;
     * 16-bit ops will look at bits 0, 2, 4, ...;
     * 32-bit ops will look at bits 0, 4, 8 and 12.
     * Compare pseudocode GetCurInstrBeat(), though that only returns
     * the 4-bit slice of the mask corresponding to a single beat.
     */
    uint16_t mask = FIELD_EX32(env->v7m.vpr, V7M_VPR, P0);

    if (!(env->v7m.vpr & R_V7M_VPR_MASK01_MASK)) {
        mask |= 0xff;
    }
    if (!(env->v7m.vpr & R_V7M_VPR_MASK23_MASK)) {
        mask |= 0xff00;
    }

    if (env->v7m.ltpsize < 4 &&
        env->regs[14] <= (1 << (4 - env->v7m.ltpsize))) {
        /*
         * Tail predication active, and this is the last loop iteration.
         * The element size is (1 << ltpsize), and we only want to process
         * loopcount elements, so we want to retain the least significant
         * (loopcount * esize) predicate bits and zero out bits above that.
         */
        int masklen = env->regs[14] << env->v7m.ltpsize;
        assert(masklen <= 16);
        mask &= MAKE_64BIT_MASK(0, masklen);
    }

    if ((env->condexec_bits & 0xf) == 0) {
        /*
         * ECI bits indicate which beats are already executed;
         * we handle this by effectively predicating them out.
         */
        int eci = env->condexec_bits >> 4;
        switch (eci) {
        case ECI_NONE:
            break;
        case ECI_A0:
            mask &= 0xfff0;
            break;
        case ECI_A0A1:
            mask &= 0xff00;
            break;
        case ECI_A0A1A2:
        case ECI_A0A1A2B0:
            mask &= 0xf000;
            break;
        default:
            g_assert_not_reached();
        }
    }

    return mask;
}

static void mve_advance_vpt(CPUARMState *env)
{
    /* Advance the VPT and ECI state if necessary */
    uint32_t vpr = env->v7m.vpr;
    unsigned mask01, mask23;

    if ((env->condexec_bits & 0xf) == 0) {
        env->condexec_bits = (env->condexec_bits == (ECI_A0A1A2B0 << 4)) ?
            (ECI_A0 << 4) : (ECI_NONE << 4);
    }

    if (!(vpr & (R_V7M_VPR_MASK01_MASK | R_V7M_VPR_MASK23_MASK))) {
        /* VPT not enabled, nothing to do */
        return;
    }

    mask01 = FIELD_EX32(vpr, V7M_VPR, MASK01);
    mask23 = FIELD_EX32(vpr, V7M_VPR, MASK23);
    if (mask01 > 8) {
        /* high bit set, but not 0b1000: invert the relevant half of P0 */
        vpr ^= 0xff;
    }
    if (mask23 > 8) {
        /* high bit set, but not 0b1000: invert the relevant half of P0 */
        vpr ^= 0xff00;
    }
    vpr = FIELD_DP32(vpr, V7M_VPR, MASK01, mask01 << 1);
    vpr = FIELD_DP32(vpr, V7M_VPR, MASK23, mask23 << 1);
    env->v7m.vpr = vpr;
}


#define DO_VLDR(OP, MSIZE, LDTYPE, ESIZE, TYPE)                         \
    void HELPER(mve_##OP)(CPUARMState *env, void *vd, uint32_t addr)    \
    {                                                                   \
        TYPE *d = vd;                                                   \
        uint16_t mask = mve_element_mask(env);                          \
        unsigned b, e;                                                  \
        /*                                                              \
         * R_SXTM allows the dest reg to become UNKNOWN for abandoned   \
         * beats so we don't care if we update part of the dest and     \
         * then take an exception.                                      \
         */                                                             \
        for (b = 0, e = 0; b < 16; b += ESIZE, e++) {                   \
            if (mask & (1 << b)) {                                      \
                d[H##ESIZE(e)] = cpu_##LDTYPE##_data_ra(env, addr, GETPC()); \
            }                                                           \
            addr += MSIZE;                                              \
        }                                                               \
        mve_advance_vpt(env);                                           \
    }

#define DO_VSTR(OP, MSIZE, STTYPE, ESIZE, TYPE)                         \
    void HELPER(mve_##OP)(CPUARMState *env, void *vd, uint32_t addr)    \
    {                                                                   \
        TYPE *d = vd;                                                   \
        uint16_t mask = mve_element_mask(env);                          \
        unsigned b, e;                                                  \
        for (b = 0, e = 0; b < 16; b += ESIZE, e++) {                   \
            if (mask & (1 << b)) {                                      \
                cpu_##STTYPE##_data_ra(env, addr, d[H##ESIZE(e)], GETPC()); \
            }                                                           \
            addr += MSIZE;                                              \
        }                                                               \
        mve_advance_vpt(env);                                           \
    }

DO_VLDR(vldrb, 1, ldub, 1, uint8_t)
DO_VLDR(vldrh, 2, lduw, 2, uint16_t)
DO_VLDR(vldrw, 4, ldl, 4, uint32_t)

DO_VSTR(vstrb, 1, stb, 1, uint8_t)
DO_VSTR(vstrh, 2, stw, 2, uint16_t)
DO_VSTR(vstrw, 4, stl, 4, uint32_t)

DO_VLDR(vldrb_sh, 1, ldsb, 2, int16_t)
DO_VLDR(vldrb_sw, 1, ldsb, 4, int32_t)
DO_VLDR(vldrb_uh, 1, ldub, 2, uint16_t)
DO_VLDR(vldrb_uw, 1, ldub, 4, uint32_t)
DO_VLDR(vldrh_sw, 2, ldsw, 4, int32_t)
DO_VLDR(vldrh_uw, 2, lduw, 4, uint32_t)

DO_VSTR(vstrb_h, 1, stb, 2, int16_t)
DO_VSTR(vstrb_w, 1, stb, 4, int32_t)
DO_VSTR(vstrh_w, 2, stw, 4, int32_t)

#undef DO_VLDR
#undef DO_VSTR

/*
 * The mergemask(D, R, M) macro performs the operation "*D = R" but
 * storing only the bytes which correspond to 1 bits in M,
 * leaving other bytes in *D unchanged. We use _Generic
 * to select the correct implementation based on the type of D.
 */

static void mergemask_ub(uint8_t *d, uint8_t r, uint16_t mask)
{
    if (mask & 1) {
        *d = r;
    }
}

static void mergemask_sb(int8_t *d, int8_t r, uint16_t mask)
{
    mergemask_ub((uint8_t *)d, r, mask);
}

static void mergemask_uh(uint16_t *d, uint16_t r, uint16_t mask)
{
    uint16_t bmask = expand_pred_b_data[mask & 3];
    *d = (*d & ~bmask) | (r & bmask);
}

static void mergemask_sh(int16_t *d, int16_t r, uint16_t mask)
{
    mergemask_uh((uint16_t *)d, r, mask);
}

static void mergemask_uw(uint32_t *d, uint32_t r, uint16_t mask)
{
    uint32_t bmask = expand_pred_b_data[mask & 0xf];
    *d = (*d & ~bmask) | (r & bmask);
}

static void mergemask_sw(int32_t *d, int32_t r, uint16_t mask)
{
    mergemask_uw((uint32_t *)d, r, mask);
}

static void mergemask_uq(uint64_t *d, uint64_t r, uint16_t mask)
{
    uint64_t bmask = expand_pred_b_data[mask & 0xff];
    *d = (*d & ~bmask) | (r & bmask);
}

static void mergemask_sq(int64_t *d, int64_t r, uint16_t mask)
{
    mergemask_uq((uint64_t *)d, r, mask);
}

#define mergemask(D, R, M)                      \
    _Generic(D,                                 \
             uint8_t *: mergemask_ub,           \
             int8_t *:  mergemask_sb,           \
             uint16_t *: mergemask_uh,          \
             int16_t *:  mergemask_sh,          \
             uint32_t *: mergemask_uw,          \
             int32_t *:  mergemask_sw,          \
             uint64_t *: mergemask_uq,          \
             int64_t *:  mergemask_sq)(D, R, M)

#define DO_1OP(OP, ESIZE, TYPE, FN)                                     \
    void HELPER(mve_##OP)(CPUARMState *env, void *vd, void *vm)         \
    {                                                                   \
        TYPE *d = vd, *m = vm;                                          \
        uint16_t mask = mve_element_mask(env);                          \
        unsigned e;                                                     \
        for (e = 0; e < 16 / ESIZE; e++, mask >>= ESIZE) {              \
            mergemask(&d[H##ESIZE(e)], FN(m[H##ESIZE(e)]), mask);       \
        }                                                               \
        mve_advance_vpt(env);                                           \
    }

#define DO_CLZ_B(N)   (clz32(N) - 24)
#define DO_CLZ_H(N)   (clz32(N) - 16)

DO_1OP(vclzb, 1, uint8_t, DO_CLZ_B)
DO_1OP(vclzh, 2, uint16_t, DO_CLZ_H)
DO_1OP(vclzw, 4, uint32_t, clz32)
