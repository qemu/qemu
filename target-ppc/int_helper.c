/*
 *  PowerPC integer and vector emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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
#include "qemu/host-utils.h"
#include "helper.h"

#include "helper_regs.h"
/*****************************************************************************/
/* Fixed point operations helpers */
#if defined(TARGET_PPC64)

uint64_t helper_mulldo(CPUPPCState *env, uint64_t arg1, uint64_t arg2)
{
    int64_t th;
    uint64_t tl;

    muls64(&tl, (uint64_t *)&th, arg1, arg2);
    /* If th != 0 && th != -1, then we had an overflow */
    if (likely((uint64_t)(th + 1) <= 1)) {
        env->ov = 0;
    } else {
        env->so = env->ov = 1;
    }
    return (int64_t)tl;
}
#endif

target_ulong helper_cntlzw(target_ulong t)
{
    return clz32(t);
}

#if defined(TARGET_PPC64)
target_ulong helper_cntlzd(target_ulong t)
{
    return clz64(t);
}
#endif

target_ulong helper_cmpb(target_ulong rs, target_ulong rb)
{
    target_ulong mask = 0xff;
    target_ulong ra = 0;
    int i;

    for (i = 0; i < sizeof(target_ulong); i++) {
        if ((rs & mask) == (rb & mask)) {
            ra |= mask;
        }
        mask <<= 8;
    }
    return ra;
}

/* shift right arithmetic helper */
target_ulong helper_sraw(CPUPPCState *env, target_ulong value,
                         target_ulong shift)
{
    int32_t ret;

    if (likely(!(shift & 0x20))) {
        if (likely((uint32_t)shift != 0)) {
            shift &= 0x1f;
            ret = (int32_t)value >> shift;
            if (likely(ret >= 0 || (value & ((1 << shift) - 1)) == 0)) {
                env->ca = 0;
            } else {
                env->ca = 1;
            }
        } else {
            ret = (int32_t)value;
            env->ca = 0;
        }
    } else {
        ret = (int32_t)value >> 31;
        env->ca = (ret != 0);
    }
    return (target_long)ret;
}

#if defined(TARGET_PPC64)
target_ulong helper_srad(CPUPPCState *env, target_ulong value,
                         target_ulong shift)
{
    int64_t ret;

    if (likely(!(shift & 0x40))) {
        if (likely((uint64_t)shift != 0)) {
            shift &= 0x3f;
            ret = (int64_t)value >> shift;
            if (likely(ret >= 0 || (value & ((1 << shift) - 1)) == 0)) {
                env->ca = 0;
            } else {
                env->ca = 1;
            }
        } else {
            ret = (int64_t)value;
            env->ca = 0;
        }
    } else {
        ret = (int64_t)value >> 63;
        env->ca = (ret != 0);
    }
    return ret;
}
#endif

#if defined(TARGET_PPC64)
target_ulong helper_popcntb(target_ulong val)
{
    val = (val & 0x5555555555555555ULL) + ((val >>  1) &
                                           0x5555555555555555ULL);
    val = (val & 0x3333333333333333ULL) + ((val >>  2) &
                                           0x3333333333333333ULL);
    val = (val & 0x0f0f0f0f0f0f0f0fULL) + ((val >>  4) &
                                           0x0f0f0f0f0f0f0f0fULL);
    return val;
}

target_ulong helper_popcntw(target_ulong val)
{
    val = (val & 0x5555555555555555ULL) + ((val >>  1) &
                                           0x5555555555555555ULL);
    val = (val & 0x3333333333333333ULL) + ((val >>  2) &
                                           0x3333333333333333ULL);
    val = (val & 0x0f0f0f0f0f0f0f0fULL) + ((val >>  4) &
                                           0x0f0f0f0f0f0f0f0fULL);
    val = (val & 0x00ff00ff00ff00ffULL) + ((val >>  8) &
                                           0x00ff00ff00ff00ffULL);
    val = (val & 0x0000ffff0000ffffULL) + ((val >> 16) &
                                           0x0000ffff0000ffffULL);
    return val;
}

target_ulong helper_popcntd(target_ulong val)
{
    return ctpop64(val);
}
#else
target_ulong helper_popcntb(target_ulong val)
{
    val = (val & 0x55555555) + ((val >>  1) & 0x55555555);
    val = (val & 0x33333333) + ((val >>  2) & 0x33333333);
    val = (val & 0x0f0f0f0f) + ((val >>  4) & 0x0f0f0f0f);
    return val;
}

target_ulong helper_popcntw(target_ulong val)
{
    val = (val & 0x55555555) + ((val >>  1) & 0x55555555);
    val = (val & 0x33333333) + ((val >>  2) & 0x33333333);
    val = (val & 0x0f0f0f0f) + ((val >>  4) & 0x0f0f0f0f);
    val = (val & 0x00ff00ff) + ((val >>  8) & 0x00ff00ff);
    val = (val & 0x0000ffff) + ((val >> 16) & 0x0000ffff);
    return val;
}
#endif

/*****************************************************************************/
/* PowerPC 601 specific instructions (POWER bridge) */
target_ulong helper_div(CPUPPCState *env, target_ulong arg1, target_ulong arg2)
{
    uint64_t tmp = (uint64_t)arg1 << 32 | env->spr[SPR_MQ];

    if (((int32_t)tmp == INT32_MIN && (int32_t)arg2 == (int32_t)-1) ||
        (int32_t)arg2 == 0) {
        env->spr[SPR_MQ] = 0;
        return INT32_MIN;
    } else {
        env->spr[SPR_MQ] = tmp % arg2;
        return  tmp / (int32_t)arg2;
    }
}

target_ulong helper_divo(CPUPPCState *env, target_ulong arg1,
                         target_ulong arg2)
{
    uint64_t tmp = (uint64_t)arg1 << 32 | env->spr[SPR_MQ];

    if (((int32_t)tmp == INT32_MIN && (int32_t)arg2 == (int32_t)-1) ||
        (int32_t)arg2 == 0) {
        env->so = env->ov = 1;
        env->spr[SPR_MQ] = 0;
        return INT32_MIN;
    } else {
        env->spr[SPR_MQ] = tmp % arg2;
        tmp /= (int32_t)arg2;
        if ((int32_t)tmp != tmp) {
            env->so = env->ov = 1;
        } else {
            env->ov = 0;
        }
        return tmp;
    }
}

target_ulong helper_divs(CPUPPCState *env, target_ulong arg1,
                         target_ulong arg2)
{
    if (((int32_t)arg1 == INT32_MIN && (int32_t)arg2 == (int32_t)-1) ||
        (int32_t)arg2 == 0) {
        env->spr[SPR_MQ] = 0;
        return INT32_MIN;
    } else {
        env->spr[SPR_MQ] = (int32_t)arg1 % (int32_t)arg2;
        return (int32_t)arg1 / (int32_t)arg2;
    }
}

target_ulong helper_divso(CPUPPCState *env, target_ulong arg1,
                          target_ulong arg2)
{
    if (((int32_t)arg1 == INT32_MIN && (int32_t)arg2 == (int32_t)-1) ||
        (int32_t)arg2 == 0) {
        env->so = env->ov = 1;
        env->spr[SPR_MQ] = 0;
        return INT32_MIN;
    } else {
        env->ov = 0;
        env->spr[SPR_MQ] = (int32_t)arg1 % (int32_t)arg2;
        return (int32_t)arg1 / (int32_t)arg2;
    }
}

/*****************************************************************************/
/* 602 specific instructions */
/* mfrom is the most crazy instruction ever seen, imho ! */
/* Real implementation uses a ROM table. Do the same */
/* Extremely decomposed:
 *                      -arg / 256
 * return 256 * log10(10           + 1.0) + 0.5
 */
#if !defined(CONFIG_USER_ONLY)
target_ulong helper_602_mfrom(target_ulong arg)
{
    if (likely(arg < 602)) {
#include "mfrom_table.c"
        return mfrom_ROM_table[arg];
    } else {
        return 0;
    }
}
#endif

/*****************************************************************************/
/* Altivec extension helpers */
#if defined(HOST_WORDS_BIGENDIAN)
#define HI_IDX 0
#define LO_IDX 1
#else
#define HI_IDX 1
#define LO_IDX 0
#endif

#if defined(HOST_WORDS_BIGENDIAN)
#define VECTOR_FOR_INORDER_I(index, element)                    \
    for (index = 0; index < ARRAY_SIZE(r->element); index++)
#else
#define VECTOR_FOR_INORDER_I(index, element)                    \
    for (index = ARRAY_SIZE(r->element)-1; index >= 0; index--)
#endif

/* Saturating arithmetic helpers.  */
#define SATCVT(from, to, from_type, to_type, min, max)          \
    static inline to_type cvt##from##to(from_type x, int *sat)  \
    {                                                           \
        to_type r;                                              \
                                                                \
        if (x < (from_type)min) {                               \
            r = min;                                            \
            *sat = 1;                                           \
        } else if (x > (from_type)max) {                        \
            r = max;                                            \
            *sat = 1;                                           \
        } else {                                                \
            r = x;                                              \
        }                                                       \
        return r;                                               \
    }
#define SATCVTU(from, to, from_type, to_type, min, max)         \
    static inline to_type cvt##from##to(from_type x, int *sat)  \
    {                                                           \
        to_type r;                                              \
                                                                \
        if (x > (from_type)max) {                               \
            r = max;                                            \
            *sat = 1;                                           \
        } else {                                                \
            r = x;                                              \
        }                                                       \
        return r;                                               \
    }
SATCVT(sh, sb, int16_t, int8_t, INT8_MIN, INT8_MAX)
SATCVT(sw, sh, int32_t, int16_t, INT16_MIN, INT16_MAX)
SATCVT(sd, sw, int64_t, int32_t, INT32_MIN, INT32_MAX)

SATCVTU(uh, ub, uint16_t, uint8_t, 0, UINT8_MAX)
SATCVTU(uw, uh, uint32_t, uint16_t, 0, UINT16_MAX)
SATCVTU(ud, uw, uint64_t, uint32_t, 0, UINT32_MAX)
SATCVT(sh, ub, int16_t, uint8_t, 0, UINT8_MAX)
SATCVT(sw, uh, int32_t, uint16_t, 0, UINT16_MAX)
SATCVT(sd, uw, int64_t, uint32_t, 0, UINT32_MAX)
#undef SATCVT
#undef SATCVTU

void helper_lvsl(ppc_avr_t *r, target_ulong sh)
{
    int i, j = (sh & 0xf);

    VECTOR_FOR_INORDER_I(i, u8) {
        r->u8[i] = j++;
    }
}

void helper_lvsr(ppc_avr_t *r, target_ulong sh)
{
    int i, j = 0x10 - (sh & 0xf);

    VECTOR_FOR_INORDER_I(i, u8) {
        r->u8[i] = j++;
    }
}

void helper_mtvscr(CPUPPCState *env, ppc_avr_t *r)
{
#if defined(HOST_WORDS_BIGENDIAN)
    env->vscr = r->u32[3];
#else
    env->vscr = r->u32[0];
#endif
    set_flush_to_zero(vscr_nj, &env->vec_status);
}

void helper_vaddcuw(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(r->u32); i++) {
        r->u32[i] = ~a->u32[i] < b->u32[i];
    }
}

#define VARITH_DO(name, op, element)                                    \
    void helper_v##name(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)       \
    {                                                                   \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->element); i++) {                  \
            r->element[i] = a->element[i] op b->element[i];             \
        }                                                               \
    }
#define VARITH(suffix, element)                 \
    VARITH_DO(add##suffix, +, element)          \
    VARITH_DO(sub##suffix, -, element)
VARITH(ubm, u8)
VARITH(uhm, u16)
VARITH(uwm, u32)
#undef VARITH_DO
#undef VARITH

#define VARITHFP(suffix, func)                                          \
    void helper_v##suffix(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a, \
                          ppc_avr_t *b)                                 \
    {                                                                   \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->f); i++) {                        \
            r->f[i] = func(a->f[i], b->f[i], &env->vec_status);         \
        }                                                               \
    }
VARITHFP(addfp, float32_add)
VARITHFP(subfp, float32_sub)
VARITHFP(minfp, float32_min)
VARITHFP(maxfp, float32_max)
#undef VARITHFP

#define VARITHFPFMA(suffix, type)                                       \
    void helper_v##suffix(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a, \
                           ppc_avr_t *b, ppc_avr_t *c)                  \
    {                                                                   \
        int i;                                                          \
        for (i = 0; i < ARRAY_SIZE(r->f); i++) {                        \
            r->f[i] = float32_muladd(a->f[i], c->f[i], b->f[i],         \
                                     type, &env->vec_status);           \
        }                                                               \
    }
VARITHFPFMA(maddfp, 0);
VARITHFPFMA(nmsubfp, float_muladd_negate_result | float_muladd_negate_c);
#undef VARITHFPFMA

#define VARITHSAT_CASE(type, op, cvt, element)                          \
    {                                                                   \
        type result = (type)a->element[i] op (type)b->element[i];       \
        r->element[i] = cvt(result, &sat);                              \
    }

#define VARITHSAT_DO(name, op, optype, cvt, element)                    \
    void helper_v##name(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a,   \
                        ppc_avr_t *b)                                   \
    {                                                                   \
        int sat = 0;                                                    \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->element); i++) {                  \
            switch (sizeof(r->element[0])) {                            \
            case 1:                                                     \
                VARITHSAT_CASE(optype, op, cvt, element);               \
                break;                                                  \
            case 2:                                                     \
                VARITHSAT_CASE(optype, op, cvt, element);               \
                break;                                                  \
            case 4:                                                     \
                VARITHSAT_CASE(optype, op, cvt, element);               \
                break;                                                  \
            }                                                           \
        }                                                               \
        if (sat) {                                                      \
            env->vscr |= (1 << VSCR_SAT);                               \
        }                                                               \
    }
#define VARITHSAT_SIGNED(suffix, element, optype, cvt)          \
    VARITHSAT_DO(adds##suffix##s, +, optype, cvt, element)      \
    VARITHSAT_DO(subs##suffix##s, -, optype, cvt, element)
#define VARITHSAT_UNSIGNED(suffix, element, optype, cvt)        \
    VARITHSAT_DO(addu##suffix##s, +, optype, cvt, element)      \
    VARITHSAT_DO(subu##suffix##s, -, optype, cvt, element)
VARITHSAT_SIGNED(b, s8, int16_t, cvtshsb)
VARITHSAT_SIGNED(h, s16, int32_t, cvtswsh)
VARITHSAT_SIGNED(w, s32, int64_t, cvtsdsw)
VARITHSAT_UNSIGNED(b, u8, uint16_t, cvtshub)
VARITHSAT_UNSIGNED(h, u16, uint32_t, cvtswuh)
VARITHSAT_UNSIGNED(w, u32, uint64_t, cvtsduw)
#undef VARITHSAT_CASE
#undef VARITHSAT_DO
#undef VARITHSAT_SIGNED
#undef VARITHSAT_UNSIGNED

#define VAVG_DO(name, element, etype)                                   \
    void helper_v##name(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)       \
    {                                                                   \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->element); i++) {                  \
            etype x = (etype)a->element[i] + (etype)b->element[i] + 1;  \
            r->element[i] = x >> 1;                                     \
        }                                                               \
    }

#define VAVG(type, signed_element, signed_type, unsigned_element,       \
             unsigned_type)                                             \
    VAVG_DO(avgs##type, signed_element, signed_type)                    \
    VAVG_DO(avgu##type, unsigned_element, unsigned_type)
VAVG(b, s8, int16_t, u8, uint16_t)
VAVG(h, s16, int32_t, u16, uint32_t)
VAVG(w, s32, int64_t, u32, uint64_t)
#undef VAVG_DO
#undef VAVG

#define VCF(suffix, cvt, element)                                       \
    void helper_vcf##suffix(CPUPPCState *env, ppc_avr_t *r,             \
                            ppc_avr_t *b, uint32_t uim)                 \
    {                                                                   \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->f); i++) {                        \
            float32 t = cvt(b->element[i], &env->vec_status);           \
            r->f[i] = float32_scalbn(t, -uim, &env->vec_status);        \
        }                                                               \
    }
VCF(ux, uint32_to_float32, u32)
VCF(sx, int32_to_float32, s32)
#undef VCF

#define VCMP_DO(suffix, compare, element, record)                       \
    void helper_vcmp##suffix(CPUPPCState *env, ppc_avr_t *r,            \
                             ppc_avr_t *a, ppc_avr_t *b)                \
    {                                                                   \
        uint32_t ones = (uint32_t)-1;                                   \
        uint32_t all = ones;                                            \
        uint32_t none = 0;                                              \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->element); i++) {                  \
            uint32_t result = (a->element[i] compare b->element[i] ?    \
                               ones : 0x0);                             \
            switch (sizeof(a->element[0])) {                            \
            case 4:                                                     \
                r->u32[i] = result;                                     \
                break;                                                  \
            case 2:                                                     \
                r->u16[i] = result;                                     \
                break;                                                  \
            case 1:                                                     \
                r->u8[i] = result;                                      \
                break;                                                  \
            }                                                           \
            all &= result;                                              \
            none |= result;                                             \
        }                                                               \
        if (record) {                                                   \
            env->crf[6] = ((all != 0) << 3) | ((none == 0) << 1);       \
        }                                                               \
    }
#define VCMP(suffix, compare, element)          \
    VCMP_DO(suffix, compare, element, 0)        \
    VCMP_DO(suffix##_dot, compare, element, 1)
VCMP(equb, ==, u8)
VCMP(equh, ==, u16)
VCMP(equw, ==, u32)
VCMP(gtub, >, u8)
VCMP(gtuh, >, u16)
VCMP(gtuw, >, u32)
VCMP(gtsb, >, s8)
VCMP(gtsh, >, s16)
VCMP(gtsw, >, s32)
#undef VCMP_DO
#undef VCMP

#define VCMPFP_DO(suffix, compare, order, record)                       \
    void helper_vcmp##suffix(CPUPPCState *env, ppc_avr_t *r,            \
                             ppc_avr_t *a, ppc_avr_t *b)                \
    {                                                                   \
        uint32_t ones = (uint32_t)-1;                                   \
        uint32_t all = ones;                                            \
        uint32_t none = 0;                                              \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->f); i++) {                        \
            uint32_t result;                                            \
            int rel = float32_compare_quiet(a->f[i], b->f[i],           \
                                            &env->vec_status);          \
            if (rel == float_relation_unordered) {                      \
                result = 0;                                             \
            } else if (rel compare order) {                             \
                result = ones;                                          \
            } else {                                                    \
                result = 0;                                             \
            }                                                           \
            r->u32[i] = result;                                         \
            all &= result;                                              \
            none |= result;                                             \
        }                                                               \
        if (record) {                                                   \
            env->crf[6] = ((all != 0) << 3) | ((none == 0) << 1);       \
        }                                                               \
    }
#define VCMPFP(suffix, compare, order)          \
    VCMPFP_DO(suffix, compare, order, 0)        \
    VCMPFP_DO(suffix##_dot, compare, order, 1)
VCMPFP(eqfp, ==, float_relation_equal)
VCMPFP(gefp, !=, float_relation_less)
VCMPFP(gtfp, ==, float_relation_greater)
#undef VCMPFP_DO
#undef VCMPFP

static inline void vcmpbfp_internal(CPUPPCState *env, ppc_avr_t *r,
                                    ppc_avr_t *a, ppc_avr_t *b, int record)
{
    int i;
    int all_in = 0;

    for (i = 0; i < ARRAY_SIZE(r->f); i++) {
        int le_rel = float32_compare_quiet(a->f[i], b->f[i], &env->vec_status);
        if (le_rel == float_relation_unordered) {
            r->u32[i] = 0xc0000000;
            /* ALL_IN does not need to be updated here.  */
        } else {
            float32 bneg = float32_chs(b->f[i]);
            int ge_rel = float32_compare_quiet(a->f[i], bneg, &env->vec_status);
            int le = le_rel != float_relation_greater;
            int ge = ge_rel != float_relation_less;

            r->u32[i] = ((!le) << 31) | ((!ge) << 30);
            all_in |= (!le | !ge);
        }
    }
    if (record) {
        env->crf[6] = (all_in == 0) << 1;
    }
}

void helper_vcmpbfp(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    vcmpbfp_internal(env, r, a, b, 0);
}

void helper_vcmpbfp_dot(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a,
                        ppc_avr_t *b)
{
    vcmpbfp_internal(env, r, a, b, 1);
}

#define VCT(suffix, satcvt, element)                                    \
    void helper_vct##suffix(CPUPPCState *env, ppc_avr_t *r,             \
                            ppc_avr_t *b, uint32_t uim)                 \
    {                                                                   \
        int i;                                                          \
        int sat = 0;                                                    \
        float_status s = env->vec_status;                               \
                                                                        \
        set_float_rounding_mode(float_round_to_zero, &s);               \
        for (i = 0; i < ARRAY_SIZE(r->f); i++) {                        \
            if (float32_is_any_nan(b->f[i])) {                          \
                r->element[i] = 0;                                      \
            } else {                                                    \
                float64 t = float32_to_float64(b->f[i], &s);            \
                int64_t j;                                              \
                                                                        \
                t = float64_scalbn(t, uim, &s);                         \
                j = float64_to_int64(t, &s);                            \
                r->element[i] = satcvt(j, &sat);                        \
            }                                                           \
        }                                                               \
        if (sat) {                                                      \
            env->vscr |= (1 << VSCR_SAT);                               \
        }                                                               \
    }
VCT(uxs, cvtsduw, u32)
VCT(sxs, cvtsdsw, s32)
#undef VCT

void helper_vmhaddshs(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a,
                      ppc_avr_t *b, ppc_avr_t *c)
{
    int sat = 0;
    int i;

    for (i = 0; i < ARRAY_SIZE(r->s16); i++) {
        int32_t prod = a->s16[i] * b->s16[i];
        int32_t t = (int32_t)c->s16[i] + (prod >> 15);

        r->s16[i] = cvtswsh(t, &sat);
    }

    if (sat) {
        env->vscr |= (1 << VSCR_SAT);
    }
}

void helper_vmhraddshs(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a,
                       ppc_avr_t *b, ppc_avr_t *c)
{
    int sat = 0;
    int i;

    for (i = 0; i < ARRAY_SIZE(r->s16); i++) {
        int32_t prod = a->s16[i] * b->s16[i] + 0x00004000;
        int32_t t = (int32_t)c->s16[i] + (prod >> 15);
        r->s16[i] = cvtswsh(t, &sat);
    }

    if (sat) {
        env->vscr |= (1 << VSCR_SAT);
    }
}

#define VMINMAX_DO(name, compare, element)                              \
    void helper_v##name(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)       \
    {                                                                   \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->element); i++) {                  \
            if (a->element[i] compare b->element[i]) {                  \
                r->element[i] = b->element[i];                          \
            } else {                                                    \
                r->element[i] = a->element[i];                          \
            }                                                           \
        }                                                               \
    }
#define VMINMAX(suffix, element)                \
    VMINMAX_DO(min##suffix, >, element)         \
    VMINMAX_DO(max##suffix, <, element)
VMINMAX(sb, s8)
VMINMAX(sh, s16)
VMINMAX(sw, s32)
VMINMAX(ub, u8)
VMINMAX(uh, u16)
VMINMAX(uw, u32)
#undef VMINMAX_DO
#undef VMINMAX

void helper_vmladduhm(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, ppc_avr_t *c)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(r->s16); i++) {
        int32_t prod = a->s16[i] * b->s16[i];
        r->s16[i] = (int16_t) (prod + c->s16[i]);
    }
}

#define VMRG_DO(name, element, highp)                                   \
    void helper_v##name(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)       \
    {                                                                   \
        ppc_avr_t result;                                               \
        int i;                                                          \
        size_t n_elems = ARRAY_SIZE(r->element);                        \
                                                                        \
        for (i = 0; i < n_elems / 2; i++) {                             \
            if (highp) {                                                \
                result.element[i*2+HI_IDX] = a->element[i];             \
                result.element[i*2+LO_IDX] = b->element[i];             \
            } else {                                                    \
                result.element[n_elems - i * 2 - (1 + HI_IDX)] =        \
                    b->element[n_elems - i - 1];                        \
                result.element[n_elems - i * 2 - (1 + LO_IDX)] =        \
                    a->element[n_elems - i - 1];                        \
            }                                                           \
        }                                                               \
        *r = result;                                                    \
    }
#if defined(HOST_WORDS_BIGENDIAN)
#define MRGHI 0
#define MRGLO 1
#else
#define MRGHI 1
#define MRGLO 0
#endif
#define VMRG(suffix, element)                   \
    VMRG_DO(mrgl##suffix, element, MRGHI)       \
    VMRG_DO(mrgh##suffix, element, MRGLO)
VMRG(b, u8)
VMRG(h, u16)
VMRG(w, u32)
#undef VMRG_DO
#undef VMRG
#undef MRGHI
#undef MRGLO

void helper_vmsummbm(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a,
                     ppc_avr_t *b, ppc_avr_t *c)
{
    int32_t prod[16];
    int i;

    for (i = 0; i < ARRAY_SIZE(r->s8); i++) {
        prod[i] = (int32_t)a->s8[i] * b->u8[i];
    }

    VECTOR_FOR_INORDER_I(i, s32) {
        r->s32[i] = c->s32[i] + prod[4 * i] + prod[4 * i + 1] +
            prod[4 * i + 2] + prod[4 * i + 3];
    }
}

void helper_vmsumshm(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a,
                     ppc_avr_t *b, ppc_avr_t *c)
{
    int32_t prod[8];
    int i;

    for (i = 0; i < ARRAY_SIZE(r->s16); i++) {
        prod[i] = a->s16[i] * b->s16[i];
    }

    VECTOR_FOR_INORDER_I(i, s32) {
        r->s32[i] = c->s32[i] + prod[2 * i] + prod[2 * i + 1];
    }
}

void helper_vmsumshs(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a,
                     ppc_avr_t *b, ppc_avr_t *c)
{
    int32_t prod[8];
    int i;
    int sat = 0;

    for (i = 0; i < ARRAY_SIZE(r->s16); i++) {
        prod[i] = (int32_t)a->s16[i] * b->s16[i];
    }

    VECTOR_FOR_INORDER_I(i, s32) {
        int64_t t = (int64_t)c->s32[i] + prod[2 * i] + prod[2 * i + 1];

        r->u32[i] = cvtsdsw(t, &sat);
    }

    if (sat) {
        env->vscr |= (1 << VSCR_SAT);
    }
}

void helper_vmsumubm(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a,
                     ppc_avr_t *b, ppc_avr_t *c)
{
    uint16_t prod[16];
    int i;

    for (i = 0; i < ARRAY_SIZE(r->u8); i++) {
        prod[i] = a->u8[i] * b->u8[i];
    }

    VECTOR_FOR_INORDER_I(i, u32) {
        r->u32[i] = c->u32[i] + prod[4 * i] + prod[4 * i + 1] +
            prod[4 * i + 2] + prod[4 * i + 3];
    }
}

void helper_vmsumuhm(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a,
                     ppc_avr_t *b, ppc_avr_t *c)
{
    uint32_t prod[8];
    int i;

    for (i = 0; i < ARRAY_SIZE(r->u16); i++) {
        prod[i] = a->u16[i] * b->u16[i];
    }

    VECTOR_FOR_INORDER_I(i, u32) {
        r->u32[i] = c->u32[i] + prod[2 * i] + prod[2 * i + 1];
    }
}

void helper_vmsumuhs(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a,
                     ppc_avr_t *b, ppc_avr_t *c)
{
    uint32_t prod[8];
    int i;
    int sat = 0;

    for (i = 0; i < ARRAY_SIZE(r->u16); i++) {
        prod[i] = a->u16[i] * b->u16[i];
    }

    VECTOR_FOR_INORDER_I(i, s32) {
        uint64_t t = (uint64_t)c->u32[i] + prod[2 * i] + prod[2 * i + 1];

        r->u32[i] = cvtuduw(t, &sat);
    }

    if (sat) {
        env->vscr |= (1 << VSCR_SAT);
    }
}

#define VMUL_DO(name, mul_element, prod_element, evenp)                 \
    void helper_v##name(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)       \
    {                                                                   \
        int i;                                                          \
                                                                        \
        VECTOR_FOR_INORDER_I(i, prod_element) {                         \
            if (evenp) {                                                \
                r->prod_element[i] = a->mul_element[i * 2 + HI_IDX] *   \
                    b->mul_element[i * 2 + HI_IDX];                     \
            } else {                                                    \
                r->prod_element[i] = a->mul_element[i * 2 + LO_IDX] *   \
                    b->mul_element[i * 2 + LO_IDX];                     \
            }                                                           \
        }                                                               \
    }
#define VMUL(suffix, mul_element, prod_element)         \
    VMUL_DO(mule##suffix, mul_element, prod_element, 1) \
    VMUL_DO(mulo##suffix, mul_element, prod_element, 0)
VMUL(sb, s8, s16)
VMUL(sh, s16, s32)
VMUL(ub, u8, u16)
VMUL(uh, u16, u32)
#undef VMUL_DO
#undef VMUL

void helper_vperm(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b,
                  ppc_avr_t *c)
{
    ppc_avr_t result;
    int i;

    VECTOR_FOR_INORDER_I(i, u8) {
        int s = c->u8[i] & 0x1f;
#if defined(HOST_WORDS_BIGENDIAN)
        int index = s & 0xf;
#else
        int index = 15 - (s & 0xf);
#endif

        if (s & 0x10) {
            result.u8[i] = b->u8[index];
        } else {
            result.u8[i] = a->u8[index];
        }
    }
    *r = result;
}

#if defined(HOST_WORDS_BIGENDIAN)
#define PKBIG 1
#else
#define PKBIG 0
#endif
void helper_vpkpx(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int i, j;
    ppc_avr_t result;
#if defined(HOST_WORDS_BIGENDIAN)
    const ppc_avr_t *x[2] = { a, b };
#else
    const ppc_avr_t *x[2] = { b, a };
#endif

    VECTOR_FOR_INORDER_I(i, u64) {
        VECTOR_FOR_INORDER_I(j, u32) {
            uint32_t e = x[i]->u32[j];

            result.u16[4*i+j] = (((e >> 9) & 0xfc00) |
                                 ((e >> 6) & 0x3e0) |
                                 ((e >> 3) & 0x1f));
        }
    }
    *r = result;
}

#define VPK(suffix, from, to, cvt, dosat)                               \
    void helper_vpk##suffix(CPUPPCState *env, ppc_avr_t *r,             \
                            ppc_avr_t *a, ppc_avr_t *b)                 \
    {                                                                   \
        int i;                                                          \
        int sat = 0;                                                    \
        ppc_avr_t result;                                               \
        ppc_avr_t *a0 = PKBIG ? a : b;                                  \
        ppc_avr_t *a1 = PKBIG ? b : a;                                  \
                                                                        \
        VECTOR_FOR_INORDER_I(i, from) {                                 \
            result.to[i] = cvt(a0->from[i], &sat);                      \
            result.to[i+ARRAY_SIZE(r->from)] = cvt(a1->from[i], &sat);  \
        }                                                               \
        *r = result;                                                    \
        if (dosat && sat) {                                             \
            env->vscr |= (1 << VSCR_SAT);                               \
        }                                                               \
    }
#define I(x, y) (x)
VPK(shss, s16, s8, cvtshsb, 1)
VPK(shus, s16, u8, cvtshub, 1)
VPK(swss, s32, s16, cvtswsh, 1)
VPK(swus, s32, u16, cvtswuh, 1)
VPK(uhus, u16, u8, cvtuhub, 1)
VPK(uwus, u32, u16, cvtuwuh, 1)
VPK(uhum, u16, u8, I, 0)
VPK(uwum, u32, u16, I, 0)
#undef I
#undef VPK
#undef PKBIG

void helper_vrefp(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *b)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(r->f); i++) {
        r->f[i] = float32_div(float32_one, b->f[i], &env->vec_status);
    }
}

#define VRFI(suffix, rounding)                                  \
    void helper_vrfi##suffix(CPUPPCState *env, ppc_avr_t *r,    \
                             ppc_avr_t *b)                      \
    {                                                           \
        int i;                                                  \
        float_status s = env->vec_status;                       \
                                                                \
        set_float_rounding_mode(rounding, &s);                  \
        for (i = 0; i < ARRAY_SIZE(r->f); i++) {                \
            r->f[i] = float32_round_to_int (b->f[i], &s);       \
        }                                                       \
    }
VRFI(n, float_round_nearest_even)
VRFI(m, float_round_down)
VRFI(p, float_round_up)
VRFI(z, float_round_to_zero)
#undef VRFI

#define VROTATE(suffix, element)                                        \
    void helper_vrl##suffix(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)   \
    {                                                                   \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->element); i++) {                  \
            unsigned int mask = ((1 <<                                  \
                                  (3 + (sizeof(a->element[0]) >> 1)))   \
                                 - 1);                                  \
            unsigned int shift = b->element[i] & mask;                  \
            r->element[i] = (a->element[i] << shift) |                  \
                (a->element[i] >> (sizeof(a->element[0]) * 8 - shift)); \
        }                                                               \
    }
VROTATE(b, u8)
VROTATE(h, u16)
VROTATE(w, u32)
#undef VROTATE

void helper_vrsqrtefp(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *b)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(r->f); i++) {
        float32 t = float32_sqrt(b->f[i], &env->vec_status);

        r->f[i] = float32_div(float32_one, t, &env->vec_status);
    }
}

void helper_vsel(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b,
                 ppc_avr_t *c)
{
    r->u64[0] = (a->u64[0] & ~c->u64[0]) | (b->u64[0] & c->u64[0]);
    r->u64[1] = (a->u64[1] & ~c->u64[1]) | (b->u64[1] & c->u64[1]);
}

void helper_vexptefp(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *b)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(r->f); i++) {
        r->f[i] = float32_exp2(b->f[i], &env->vec_status);
    }
}

void helper_vlogefp(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *b)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(r->f); i++) {
        r->f[i] = float32_log2(b->f[i], &env->vec_status);
    }
}

#if defined(HOST_WORDS_BIGENDIAN)
#define LEFT 0
#define RIGHT 1
#else
#define LEFT 1
#define RIGHT 0
#endif
/* The specification says that the results are undefined if all of the
 * shift counts are not identical.  We check to make sure that they are
 * to conform to what real hardware appears to do.  */
#define VSHIFT(suffix, leftp)                                           \
    void helper_vs##suffix(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)    \
    {                                                                   \
        int shift = b->u8[LO_IDX*15] & 0x7;                             \
        int doit = 1;                                                   \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->u8); i++) {                       \
            doit = doit && ((b->u8[i] & 0x7) == shift);                 \
        }                                                               \
        if (doit) {                                                     \
            if (shift == 0) {                                           \
                *r = *a;                                                \
            } else if (leftp) {                                         \
                uint64_t carry = a->u64[LO_IDX] >> (64 - shift);        \
                                                                        \
                r->u64[HI_IDX] = (a->u64[HI_IDX] << shift) | carry;     \
                r->u64[LO_IDX] = a->u64[LO_IDX] << shift;               \
            } else {                                                    \
                uint64_t carry = a->u64[HI_IDX] << (64 - shift);        \
                                                                        \
                r->u64[LO_IDX] = (a->u64[LO_IDX] >> shift) | carry;     \
                r->u64[HI_IDX] = a->u64[HI_IDX] >> shift;               \
            }                                                           \
        }                                                               \
    }
VSHIFT(l, LEFT)
VSHIFT(r, RIGHT)
#undef VSHIFT
#undef LEFT
#undef RIGHT

#define VSL(suffix, element)                                            \
    void helper_vsl##suffix(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)   \
    {                                                                   \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->element); i++) {                  \
            unsigned int mask = ((1 <<                                  \
                                  (3 + (sizeof(a->element[0]) >> 1)))   \
                                 - 1);                                  \
            unsigned int shift = b->element[i] & mask;                  \
                                                                        \
            r->element[i] = a->element[i] << shift;                     \
        }                                                               \
    }
VSL(b, u8)
VSL(h, u16)
VSL(w, u32)
#undef VSL

void helper_vsldoi(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, uint32_t shift)
{
    int sh = shift & 0xf;
    int i;
    ppc_avr_t result;

#if defined(HOST_WORDS_BIGENDIAN)
    for (i = 0; i < ARRAY_SIZE(r->u8); i++) {
        int index = sh + i;
        if (index > 0xf) {
            result.u8[i] = b->u8[index - 0x10];
        } else {
            result.u8[i] = a->u8[index];
        }
    }
#else
    for (i = 0; i < ARRAY_SIZE(r->u8); i++) {
        int index = (16 - sh) + i;
        if (index > 0xf) {
            result.u8[i] = a->u8[index - 0x10];
        } else {
            result.u8[i] = b->u8[index];
        }
    }
#endif
    *r = result;
}

void helper_vslo(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int sh = (b->u8[LO_IDX*0xf] >> 3) & 0xf;

#if defined(HOST_WORDS_BIGENDIAN)
    memmove(&r->u8[0], &a->u8[sh], 16 - sh);
    memset(&r->u8[16-sh], 0, sh);
#else
    memmove(&r->u8[sh], &a->u8[0], 16 - sh);
    memset(&r->u8[0], 0, sh);
#endif
}

/* Experimental testing shows that hardware masks the immediate.  */
#define _SPLAT_MASKED(element) (splat & (ARRAY_SIZE(r->element) - 1))
#if defined(HOST_WORDS_BIGENDIAN)
#define SPLAT_ELEMENT(element) _SPLAT_MASKED(element)
#else
#define SPLAT_ELEMENT(element)                                  \
    (ARRAY_SIZE(r->element) - 1 - _SPLAT_MASKED(element))
#endif
#define VSPLT(suffix, element)                                          \
    void helper_vsplt##suffix(ppc_avr_t *r, ppc_avr_t *b, uint32_t splat) \
    {                                                                   \
        uint32_t s = b->element[SPLAT_ELEMENT(element)];                \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->element); i++) {                  \
            r->element[i] = s;                                          \
        }                                                               \
    }
VSPLT(b, u8)
VSPLT(h, u16)
VSPLT(w, u32)
#undef VSPLT
#undef SPLAT_ELEMENT
#undef _SPLAT_MASKED

#define VSPLTI(suffix, element, splat_type)                     \
    void helper_vspltis##suffix(ppc_avr_t *r, uint32_t splat)   \
    {                                                           \
        splat_type x = (int8_t)(splat << 3) >> 3;               \
        int i;                                                  \
                                                                \
        for (i = 0; i < ARRAY_SIZE(r->element); i++) {          \
            r->element[i] = x;                                  \
        }                                                       \
    }
VSPLTI(b, s8, int8_t)
VSPLTI(h, s16, int16_t)
VSPLTI(w, s32, int32_t)
#undef VSPLTI

#define VSR(suffix, element)                                            \
    void helper_vsr##suffix(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)   \
    {                                                                   \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->element); i++) {                  \
            unsigned int mask = ((1 <<                                  \
                                  (3 + (sizeof(a->element[0]) >> 1)))   \
                                 - 1);                                  \
            unsigned int shift = b->element[i] & mask;                  \
                                                                        \
            r->element[i] = a->element[i] >> shift;                     \
        }                                                               \
    }
VSR(ab, s8)
VSR(ah, s16)
VSR(aw, s32)
VSR(b, u8)
VSR(h, u16)
VSR(w, u32)
#undef VSR

void helper_vsro(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int sh = (b->u8[LO_IDX * 0xf] >> 3) & 0xf;

#if defined(HOST_WORDS_BIGENDIAN)
    memmove(&r->u8[sh], &a->u8[0], 16 - sh);
    memset(&r->u8[0], 0, sh);
#else
    memmove(&r->u8[0], &a->u8[sh], 16 - sh);
    memset(&r->u8[16 - sh], 0, sh);
#endif
}

void helper_vsubcuw(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(r->u32); i++) {
        r->u32[i] = a->u32[i] >= b->u32[i];
    }
}

void helper_vsumsws(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int64_t t;
    int i, upper;
    ppc_avr_t result;
    int sat = 0;

#if defined(HOST_WORDS_BIGENDIAN)
    upper = ARRAY_SIZE(r->s32)-1;
#else
    upper = 0;
#endif
    t = (int64_t)b->s32[upper];
    for (i = 0; i < ARRAY_SIZE(r->s32); i++) {
        t += a->s32[i];
        result.s32[i] = 0;
    }
    result.s32[upper] = cvtsdsw(t, &sat);
    *r = result;

    if (sat) {
        env->vscr |= (1 << VSCR_SAT);
    }
}

void helper_vsum2sws(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int i, j, upper;
    ppc_avr_t result;
    int sat = 0;

#if defined(HOST_WORDS_BIGENDIAN)
    upper = 1;
#else
    upper = 0;
#endif
    for (i = 0; i < ARRAY_SIZE(r->u64); i++) {
        int64_t t = (int64_t)b->s32[upper + i * 2];

        result.u64[i] = 0;
        for (j = 0; j < ARRAY_SIZE(r->u64); j++) {
            t += a->s32[2 * i + j];
        }
        result.s32[upper + i * 2] = cvtsdsw(t, &sat);
    }

    *r = result;
    if (sat) {
        env->vscr |= (1 << VSCR_SAT);
    }
}

void helper_vsum4sbs(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int i, j;
    int sat = 0;

    for (i = 0; i < ARRAY_SIZE(r->s32); i++) {
        int64_t t = (int64_t)b->s32[i];

        for (j = 0; j < ARRAY_SIZE(r->s32); j++) {
            t += a->s8[4 * i + j];
        }
        r->s32[i] = cvtsdsw(t, &sat);
    }

    if (sat) {
        env->vscr |= (1 << VSCR_SAT);
    }
}

void helper_vsum4shs(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int sat = 0;
    int i;

    for (i = 0; i < ARRAY_SIZE(r->s32); i++) {
        int64_t t = (int64_t)b->s32[i];

        t += a->s16[2 * i] + a->s16[2 * i + 1];
        r->s32[i] = cvtsdsw(t, &sat);
    }

    if (sat) {
        env->vscr |= (1 << VSCR_SAT);
    }
}

void helper_vsum4ubs(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int i, j;
    int sat = 0;

    for (i = 0; i < ARRAY_SIZE(r->u32); i++) {
        uint64_t t = (uint64_t)b->u32[i];

        for (j = 0; j < ARRAY_SIZE(r->u32); j++) {
            t += a->u8[4 * i + j];
        }
        r->u32[i] = cvtuduw(t, &sat);
    }

    if (sat) {
        env->vscr |= (1 << VSCR_SAT);
    }
}

#if defined(HOST_WORDS_BIGENDIAN)
#define UPKHI 1
#define UPKLO 0
#else
#define UPKHI 0
#define UPKLO 1
#endif
#define VUPKPX(suffix, hi)                                              \
    void helper_vupk##suffix(ppc_avr_t *r, ppc_avr_t *b)                \
    {                                                                   \
        int i;                                                          \
        ppc_avr_t result;                                               \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->u32); i++) {                      \
            uint16_t e = b->u16[hi ? i : i+4];                          \
            uint8_t a = (e >> 15) ? 0xff : 0;                           \
            uint8_t r = (e >> 10) & 0x1f;                               \
            uint8_t g = (e >> 5) & 0x1f;                                \
            uint8_t b = e & 0x1f;                                       \
                                                                        \
            result.u32[i] = (a << 24) | (r << 16) | (g << 8) | b;       \
        }                                                               \
        *r = result;                                                    \
    }
VUPKPX(lpx, UPKLO)
VUPKPX(hpx, UPKHI)
#undef VUPKPX

#define VUPK(suffix, unpacked, packee, hi)                              \
    void helper_vupk##suffix(ppc_avr_t *r, ppc_avr_t *b)                \
    {                                                                   \
        int i;                                                          \
        ppc_avr_t result;                                               \
                                                                        \
        if (hi) {                                                       \
            for (i = 0; i < ARRAY_SIZE(r->unpacked); i++) {             \
                result.unpacked[i] = b->packee[i];                      \
            }                                                           \
        } else {                                                        \
            for (i = ARRAY_SIZE(r->unpacked); i < ARRAY_SIZE(r->packee); \
                 i++) {                                                 \
                result.unpacked[i - ARRAY_SIZE(r->unpacked)] = b->packee[i]; \
            }                                                           \
        }                                                               \
        *r = result;                                                    \
    }
VUPK(hsb, s16, s8, UPKHI)
VUPK(hsh, s32, s16, UPKHI)
VUPK(lsb, s16, s8, UPKLO)
VUPK(lsh, s32, s16, UPKLO)
#undef VUPK
#undef UPKHI
#undef UPKLO

#undef VECTOR_FOR_INORDER_I
#undef HI_IDX
#undef LO_IDX

/*****************************************************************************/
/* SPE extension helpers */
/* Use a table to make this quicker */
static const uint8_t hbrev[16] = {
    0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE,
    0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF,
};

static inline uint8_t byte_reverse(uint8_t val)
{
    return hbrev[val >> 4] | (hbrev[val & 0xF] << 4);
}

static inline uint32_t word_reverse(uint32_t val)
{
    return byte_reverse(val >> 24) | (byte_reverse(val >> 16) << 8) |
        (byte_reverse(val >> 8) << 16) | (byte_reverse(val) << 24);
}

#define MASKBITS 16 /* Random value - to be fixed (implementation dependent) */
target_ulong helper_brinc(target_ulong arg1, target_ulong arg2)
{
    uint32_t a, b, d, mask;

    mask = UINT32_MAX >> (32 - MASKBITS);
    a = arg1 & mask;
    b = arg2 & mask;
    d = word_reverse(1 + word_reverse(a | ~b));
    return (arg1 & ~mask) | (d & b);
}

uint32_t helper_cntlsw32(uint32_t val)
{
    if (val & 0x80000000) {
        return clz32(~val);
    } else {
        return clz32(val);
    }
}

uint32_t helper_cntlzw32(uint32_t val)
{
    return clz32(val);
}

/* 440 specific */
target_ulong helper_dlmzb(CPUPPCState *env, target_ulong high,
                          target_ulong low, uint32_t update_Rc)
{
    target_ulong mask;
    int i;

    i = 1;
    for (mask = 0xFF000000; mask != 0; mask = mask >> 8) {
        if ((high & mask) == 0) {
            if (update_Rc) {
                env->crf[0] = 0x4;
            }
            goto done;
        }
        i++;
    }
    for (mask = 0xFF000000; mask != 0; mask = mask >> 8) {
        if ((low & mask) == 0) {
            if (update_Rc) {
                env->crf[0] = 0x8;
            }
            goto done;
        }
        i++;
    }
    if (update_Rc) {
        env->crf[0] = 0x2;
    }
 done:
    env->xer = (env->xer & ~0x7F) | i;
    if (update_Rc) {
        env->crf[0] |= xer_so;
    }
    return i;
}
