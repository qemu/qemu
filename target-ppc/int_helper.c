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
#include "exec/helper-proto.h"

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

target_ulong helper_divweu(CPUPPCState *env, target_ulong ra, target_ulong rb,
                           uint32_t oe)
{
    uint64_t rt = 0;
    int overflow = 0;

    uint64_t dividend = (uint64_t)ra << 32;
    uint64_t divisor = (uint32_t)rb;

    if (unlikely(divisor == 0)) {
        overflow = 1;
    } else {
        rt = dividend / divisor;
        overflow = rt > UINT32_MAX;
    }

    if (unlikely(overflow)) {
        rt = 0; /* Undefined */
    }

    if (oe) {
        if (unlikely(overflow)) {
            env->so = env->ov = 1;
        } else {
            env->ov = 0;
        }
    }

    return (target_ulong)rt;
}

target_ulong helper_divwe(CPUPPCState *env, target_ulong ra, target_ulong rb,
                          uint32_t oe)
{
    int64_t rt = 0;
    int overflow = 0;

    int64_t dividend = (int64_t)ra << 32;
    int64_t divisor = (int64_t)((int32_t)rb);

    if (unlikely((divisor == 0) ||
                 ((divisor == -1ull) && (dividend == INT64_MIN)))) {
        overflow = 1;
    } else {
        rt = dividend / divisor;
        overflow = rt != (int32_t)rt;
    }

    if (unlikely(overflow)) {
        rt = 0; /* Undefined */
    }

    if (oe) {
        if (unlikely(overflow)) {
            env->so = env->ov = 1;
        } else {
            env->ov = 0;
        }
    }

    return (target_ulong)rt;
}

#if defined(TARGET_PPC64)

uint64_t helper_divdeu(CPUPPCState *env, uint64_t ra, uint64_t rb, uint32_t oe)
{
    uint64_t rt = 0;
    int overflow = 0;

    overflow = divu128(&rt, &ra, rb);

    if (unlikely(overflow)) {
        rt = 0; /* Undefined */
    }

    if (oe) {
        if (unlikely(overflow)) {
            env->so = env->ov = 1;
        } else {
            env->ov = 0;
        }
    }

    return rt;
}

uint64_t helper_divde(CPUPPCState *env, uint64_t rau, uint64_t rbu, uint32_t oe)
{
    int64_t rt = 0;
    int64_t ra = (int64_t)rau;
    int64_t rb = (int64_t)rbu;
    int overflow = divs128(&rt, &ra, rb);

    if (unlikely(overflow)) {
        rt = 0; /* Undefined */
    }

    if (oe) {

        if (unlikely(overflow)) {
            env->so = env->ov = 1;
        } else {
            env->ov = 0;
        }
    }

    return rt;
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

#if defined(TARGET_PPC64)

uint64_t helper_bpermd(uint64_t rs, uint64_t rb)
{
    int i;
    uint64_t ra = 0;

    for (i = 0; i < 8; i++) {
        int index = (rs >> (i*8)) & 0xFF;
        if (index < 64) {
            if (rb & (1ull << (63-index))) {
                ra |= 1 << i;
            }
        }
    }
    return ra;
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
VARITH(udm, u64)
VARITH_DO(muluwm, *, u32)
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
        uint64_t ones = (uint64_t)-1;                                   \
        uint64_t all = ones;                                            \
        uint64_t none = 0;                                              \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->element); i++) {                  \
            uint64_t result = (a->element[i] compare b->element[i] ?    \
                               ones : 0x0);                             \
            switch (sizeof(a->element[0])) {                            \
            case 8:                                                     \
                r->u64[i] = result;                                     \
                break;                                                  \
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
VCMP(equd, ==, u64)
VCMP(gtub, >, u8)
VCMP(gtuh, >, u16)
VCMP(gtuw, >, u32)
VCMP(gtud, >, u64)
VCMP(gtsb, >, s8)
VCMP(gtsh, >, s16)
VCMP(gtsw, >, s32)
VCMP(gtsd, >, s64)
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
VMINMAX(sd, s64)
VMINMAX(ub, u8)
VMINMAX(uh, u16)
VMINMAX(uw, u32)
VMINMAX(ud, u64)
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

#define VMUL_DO(name, mul_element, prod_element, cast, evenp)           \
    void helper_v##name(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)       \
    {                                                                   \
        int i;                                                          \
                                                                        \
        VECTOR_FOR_INORDER_I(i, prod_element) {                         \
            if (evenp) {                                                \
                r->prod_element[i] =                                    \
                    (cast)a->mul_element[i * 2 + HI_IDX] *              \
                    (cast)b->mul_element[i * 2 + HI_IDX];               \
            } else {                                                    \
                r->prod_element[i] =                                    \
                    (cast)a->mul_element[i * 2 + LO_IDX] *              \
                    (cast)b->mul_element[i * 2 + LO_IDX];               \
            }                                                           \
        }                                                               \
    }
#define VMUL(suffix, mul_element, prod_element, cast)            \
    VMUL_DO(mule##suffix, mul_element, prod_element, cast, 1)    \
    VMUL_DO(mulo##suffix, mul_element, prod_element, cast, 0)
VMUL(sb, s8, s16, int16_t)
VMUL(sh, s16, s32, int32_t)
VMUL(sw, s32, s64, int64_t)
VMUL(ub, u8, u16, uint16_t)
VMUL(uh, u16, u32, uint32_t)
VMUL(uw, u32, u64, uint64_t)
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
#define VBPERMQ_INDEX(avr, i) ((avr)->u8[(i)])
#define VBPERMQ_DW(index) (((index) & 0x40) != 0)
#else
#define VBPERMQ_INDEX(avr, i) ((avr)->u8[15-(i)])
#define VBPERMQ_DW(index) (((index) & 0x40) == 0)
#endif

void helper_vbpermq(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int i;
    uint64_t perm = 0;

    VECTOR_FOR_INORDER_I(i, u8) {
        int index = VBPERMQ_INDEX(b, i);

        if (index < 128) {
            uint64_t mask = (1ull << (63-(index & 0x3F)));
            if (a->u64[VBPERMQ_DW(index)] & mask) {
                perm |= (0x8000 >> i);
            }
        }
    }

    r->u64[HI_IDX] = perm;
    r->u64[LO_IDX] = 0;
}

#undef VBPERMQ_INDEX
#undef VBPERMQ_DW

static const uint64_t VGBBD_MASKS[256] = {
    0x0000000000000000ull, /* 00 */
    0x0000000000000080ull, /* 01 */
    0x0000000000008000ull, /* 02 */
    0x0000000000008080ull, /* 03 */
    0x0000000000800000ull, /* 04 */
    0x0000000000800080ull, /* 05 */
    0x0000000000808000ull, /* 06 */
    0x0000000000808080ull, /* 07 */
    0x0000000080000000ull, /* 08 */
    0x0000000080000080ull, /* 09 */
    0x0000000080008000ull, /* 0A */
    0x0000000080008080ull, /* 0B */
    0x0000000080800000ull, /* 0C */
    0x0000000080800080ull, /* 0D */
    0x0000000080808000ull, /* 0E */
    0x0000000080808080ull, /* 0F */
    0x0000008000000000ull, /* 10 */
    0x0000008000000080ull, /* 11 */
    0x0000008000008000ull, /* 12 */
    0x0000008000008080ull, /* 13 */
    0x0000008000800000ull, /* 14 */
    0x0000008000800080ull, /* 15 */
    0x0000008000808000ull, /* 16 */
    0x0000008000808080ull, /* 17 */
    0x0000008080000000ull, /* 18 */
    0x0000008080000080ull, /* 19 */
    0x0000008080008000ull, /* 1A */
    0x0000008080008080ull, /* 1B */
    0x0000008080800000ull, /* 1C */
    0x0000008080800080ull, /* 1D */
    0x0000008080808000ull, /* 1E */
    0x0000008080808080ull, /* 1F */
    0x0000800000000000ull, /* 20 */
    0x0000800000000080ull, /* 21 */
    0x0000800000008000ull, /* 22 */
    0x0000800000008080ull, /* 23 */
    0x0000800000800000ull, /* 24 */
    0x0000800000800080ull, /* 25 */
    0x0000800000808000ull, /* 26 */
    0x0000800000808080ull, /* 27 */
    0x0000800080000000ull, /* 28 */
    0x0000800080000080ull, /* 29 */
    0x0000800080008000ull, /* 2A */
    0x0000800080008080ull, /* 2B */
    0x0000800080800000ull, /* 2C */
    0x0000800080800080ull, /* 2D */
    0x0000800080808000ull, /* 2E */
    0x0000800080808080ull, /* 2F */
    0x0000808000000000ull, /* 30 */
    0x0000808000000080ull, /* 31 */
    0x0000808000008000ull, /* 32 */
    0x0000808000008080ull, /* 33 */
    0x0000808000800000ull, /* 34 */
    0x0000808000800080ull, /* 35 */
    0x0000808000808000ull, /* 36 */
    0x0000808000808080ull, /* 37 */
    0x0000808080000000ull, /* 38 */
    0x0000808080000080ull, /* 39 */
    0x0000808080008000ull, /* 3A */
    0x0000808080008080ull, /* 3B */
    0x0000808080800000ull, /* 3C */
    0x0000808080800080ull, /* 3D */
    0x0000808080808000ull, /* 3E */
    0x0000808080808080ull, /* 3F */
    0x0080000000000000ull, /* 40 */
    0x0080000000000080ull, /* 41 */
    0x0080000000008000ull, /* 42 */
    0x0080000000008080ull, /* 43 */
    0x0080000000800000ull, /* 44 */
    0x0080000000800080ull, /* 45 */
    0x0080000000808000ull, /* 46 */
    0x0080000000808080ull, /* 47 */
    0x0080000080000000ull, /* 48 */
    0x0080000080000080ull, /* 49 */
    0x0080000080008000ull, /* 4A */
    0x0080000080008080ull, /* 4B */
    0x0080000080800000ull, /* 4C */
    0x0080000080800080ull, /* 4D */
    0x0080000080808000ull, /* 4E */
    0x0080000080808080ull, /* 4F */
    0x0080008000000000ull, /* 50 */
    0x0080008000000080ull, /* 51 */
    0x0080008000008000ull, /* 52 */
    0x0080008000008080ull, /* 53 */
    0x0080008000800000ull, /* 54 */
    0x0080008000800080ull, /* 55 */
    0x0080008000808000ull, /* 56 */
    0x0080008000808080ull, /* 57 */
    0x0080008080000000ull, /* 58 */
    0x0080008080000080ull, /* 59 */
    0x0080008080008000ull, /* 5A */
    0x0080008080008080ull, /* 5B */
    0x0080008080800000ull, /* 5C */
    0x0080008080800080ull, /* 5D */
    0x0080008080808000ull, /* 5E */
    0x0080008080808080ull, /* 5F */
    0x0080800000000000ull, /* 60 */
    0x0080800000000080ull, /* 61 */
    0x0080800000008000ull, /* 62 */
    0x0080800000008080ull, /* 63 */
    0x0080800000800000ull, /* 64 */
    0x0080800000800080ull, /* 65 */
    0x0080800000808000ull, /* 66 */
    0x0080800000808080ull, /* 67 */
    0x0080800080000000ull, /* 68 */
    0x0080800080000080ull, /* 69 */
    0x0080800080008000ull, /* 6A */
    0x0080800080008080ull, /* 6B */
    0x0080800080800000ull, /* 6C */
    0x0080800080800080ull, /* 6D */
    0x0080800080808000ull, /* 6E */
    0x0080800080808080ull, /* 6F */
    0x0080808000000000ull, /* 70 */
    0x0080808000000080ull, /* 71 */
    0x0080808000008000ull, /* 72 */
    0x0080808000008080ull, /* 73 */
    0x0080808000800000ull, /* 74 */
    0x0080808000800080ull, /* 75 */
    0x0080808000808000ull, /* 76 */
    0x0080808000808080ull, /* 77 */
    0x0080808080000000ull, /* 78 */
    0x0080808080000080ull, /* 79 */
    0x0080808080008000ull, /* 7A */
    0x0080808080008080ull, /* 7B */
    0x0080808080800000ull, /* 7C */
    0x0080808080800080ull, /* 7D */
    0x0080808080808000ull, /* 7E */
    0x0080808080808080ull, /* 7F */
    0x8000000000000000ull, /* 80 */
    0x8000000000000080ull, /* 81 */
    0x8000000000008000ull, /* 82 */
    0x8000000000008080ull, /* 83 */
    0x8000000000800000ull, /* 84 */
    0x8000000000800080ull, /* 85 */
    0x8000000000808000ull, /* 86 */
    0x8000000000808080ull, /* 87 */
    0x8000000080000000ull, /* 88 */
    0x8000000080000080ull, /* 89 */
    0x8000000080008000ull, /* 8A */
    0x8000000080008080ull, /* 8B */
    0x8000000080800000ull, /* 8C */
    0x8000000080800080ull, /* 8D */
    0x8000000080808000ull, /* 8E */
    0x8000000080808080ull, /* 8F */
    0x8000008000000000ull, /* 90 */
    0x8000008000000080ull, /* 91 */
    0x8000008000008000ull, /* 92 */
    0x8000008000008080ull, /* 93 */
    0x8000008000800000ull, /* 94 */
    0x8000008000800080ull, /* 95 */
    0x8000008000808000ull, /* 96 */
    0x8000008000808080ull, /* 97 */
    0x8000008080000000ull, /* 98 */
    0x8000008080000080ull, /* 99 */
    0x8000008080008000ull, /* 9A */
    0x8000008080008080ull, /* 9B */
    0x8000008080800000ull, /* 9C */
    0x8000008080800080ull, /* 9D */
    0x8000008080808000ull, /* 9E */
    0x8000008080808080ull, /* 9F */
    0x8000800000000000ull, /* A0 */
    0x8000800000000080ull, /* A1 */
    0x8000800000008000ull, /* A2 */
    0x8000800000008080ull, /* A3 */
    0x8000800000800000ull, /* A4 */
    0x8000800000800080ull, /* A5 */
    0x8000800000808000ull, /* A6 */
    0x8000800000808080ull, /* A7 */
    0x8000800080000000ull, /* A8 */
    0x8000800080000080ull, /* A9 */
    0x8000800080008000ull, /* AA */
    0x8000800080008080ull, /* AB */
    0x8000800080800000ull, /* AC */
    0x8000800080800080ull, /* AD */
    0x8000800080808000ull, /* AE */
    0x8000800080808080ull, /* AF */
    0x8000808000000000ull, /* B0 */
    0x8000808000000080ull, /* B1 */
    0x8000808000008000ull, /* B2 */
    0x8000808000008080ull, /* B3 */
    0x8000808000800000ull, /* B4 */
    0x8000808000800080ull, /* B5 */
    0x8000808000808000ull, /* B6 */
    0x8000808000808080ull, /* B7 */
    0x8000808080000000ull, /* B8 */
    0x8000808080000080ull, /* B9 */
    0x8000808080008000ull, /* BA */
    0x8000808080008080ull, /* BB */
    0x8000808080800000ull, /* BC */
    0x8000808080800080ull, /* BD */
    0x8000808080808000ull, /* BE */
    0x8000808080808080ull, /* BF */
    0x8080000000000000ull, /* C0 */
    0x8080000000000080ull, /* C1 */
    0x8080000000008000ull, /* C2 */
    0x8080000000008080ull, /* C3 */
    0x8080000000800000ull, /* C4 */
    0x8080000000800080ull, /* C5 */
    0x8080000000808000ull, /* C6 */
    0x8080000000808080ull, /* C7 */
    0x8080000080000000ull, /* C8 */
    0x8080000080000080ull, /* C9 */
    0x8080000080008000ull, /* CA */
    0x8080000080008080ull, /* CB */
    0x8080000080800000ull, /* CC */
    0x8080000080800080ull, /* CD */
    0x8080000080808000ull, /* CE */
    0x8080000080808080ull, /* CF */
    0x8080008000000000ull, /* D0 */
    0x8080008000000080ull, /* D1 */
    0x8080008000008000ull, /* D2 */
    0x8080008000008080ull, /* D3 */
    0x8080008000800000ull, /* D4 */
    0x8080008000800080ull, /* D5 */
    0x8080008000808000ull, /* D6 */
    0x8080008000808080ull, /* D7 */
    0x8080008080000000ull, /* D8 */
    0x8080008080000080ull, /* D9 */
    0x8080008080008000ull, /* DA */
    0x8080008080008080ull, /* DB */
    0x8080008080800000ull, /* DC */
    0x8080008080800080ull, /* DD */
    0x8080008080808000ull, /* DE */
    0x8080008080808080ull, /* DF */
    0x8080800000000000ull, /* E0 */
    0x8080800000000080ull, /* E1 */
    0x8080800000008000ull, /* E2 */
    0x8080800000008080ull, /* E3 */
    0x8080800000800000ull, /* E4 */
    0x8080800000800080ull, /* E5 */
    0x8080800000808000ull, /* E6 */
    0x8080800000808080ull, /* E7 */
    0x8080800080000000ull, /* E8 */
    0x8080800080000080ull, /* E9 */
    0x8080800080008000ull, /* EA */
    0x8080800080008080ull, /* EB */
    0x8080800080800000ull, /* EC */
    0x8080800080800080ull, /* ED */
    0x8080800080808000ull, /* EE */
    0x8080800080808080ull, /* EF */
    0x8080808000000000ull, /* F0 */
    0x8080808000000080ull, /* F1 */
    0x8080808000008000ull, /* F2 */
    0x8080808000008080ull, /* F3 */
    0x8080808000800000ull, /* F4 */
    0x8080808000800080ull, /* F5 */
    0x8080808000808000ull, /* F6 */
    0x8080808000808080ull, /* F7 */
    0x8080808080000000ull, /* F8 */
    0x8080808080000080ull, /* F9 */
    0x8080808080008000ull, /* FA */
    0x8080808080008080ull, /* FB */
    0x8080808080800000ull, /* FC */
    0x8080808080800080ull, /* FD */
    0x8080808080808000ull, /* FE */
    0x8080808080808080ull, /* FF */
};

void helper_vgbbd(ppc_avr_t *r, ppc_avr_t *b)
{
    int i;
    uint64_t t[2] = { 0, 0 };

    VECTOR_FOR_INORDER_I(i, u8) {
#if defined(HOST_WORDS_BIGENDIAN)
        t[i>>3] |= VGBBD_MASKS[b->u8[i]] >> (i & 7);
#else
        t[i>>3] |= VGBBD_MASKS[b->u8[i]] >> (7-(i & 7));
#endif
    }

    r->u64[0] = t[0];
    r->u64[1] = t[1];
}

#define PMSUM(name, srcfld, trgfld, trgtyp)                   \
void helper_##name(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)  \
{                                                             \
    int i, j;                                                 \
    trgtyp prod[sizeof(ppc_avr_t)/sizeof(a->srcfld[0])];      \
                                                              \
    VECTOR_FOR_INORDER_I(i, srcfld) {                         \
        prod[i] = 0;                                          \
        for (j = 0; j < sizeof(a->srcfld[0]) * 8; j++) {      \
            if (a->srcfld[i] & (1ull<<j)) {                   \
                prod[i] ^= ((trgtyp)b->srcfld[i] << j);       \
            }                                                 \
        }                                                     \
    }                                                         \
                                                              \
    VECTOR_FOR_INORDER_I(i, trgfld) {                         \
        r->trgfld[i] = prod[2*i] ^ prod[2*i+1];               \
    }                                                         \
}

PMSUM(vpmsumb, u8, u16, uint16_t)
PMSUM(vpmsumh, u16, u32, uint32_t)
PMSUM(vpmsumw, u32, u64, uint64_t)

void helper_vpmsumd(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{

#ifdef CONFIG_INT128
    int i, j;
    __uint128_t prod[2];

    VECTOR_FOR_INORDER_I(i, u64) {
        prod[i] = 0;
        for (j = 0; j < 64; j++) {
            if (a->u64[i] & (1ull<<j)) {
                prod[i] ^= (((__uint128_t)b->u64[i]) << j);
            }
        }
    }

    r->u128 = prod[0] ^ prod[1];

#else
    int i, j;
    ppc_avr_t prod[2];

    VECTOR_FOR_INORDER_I(i, u64) {
        prod[i].u64[LO_IDX] = prod[i].u64[HI_IDX] = 0;
        for (j = 0; j < 64; j++) {
            if (a->u64[i] & (1ull<<j)) {
                ppc_avr_t bshift;
                if (j == 0) {
                    bshift.u64[HI_IDX] = 0;
                    bshift.u64[LO_IDX] = b->u64[i];
                } else {
                    bshift.u64[HI_IDX] = b->u64[i] >> (64-j);
                    bshift.u64[LO_IDX] = b->u64[i] << j;
                }
                prod[i].u64[LO_IDX] ^= bshift.u64[LO_IDX];
                prod[i].u64[HI_IDX] ^= bshift.u64[HI_IDX];
            }
        }
    }

    r->u64[LO_IDX] = prod[0].u64[LO_IDX] ^ prod[1].u64[LO_IDX];
    r->u64[HI_IDX] = prod[0].u64[HI_IDX] ^ prod[1].u64[HI_IDX];
#endif
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
VPK(sdss, s64, s32, cvtsdsw, 1)
VPK(sdus, s64, u32, cvtsduw, 1)
VPK(uhus, u16, u8, cvtuhub, 1)
VPK(uwus, u32, u16, cvtuwuh, 1)
VPK(udus, u64, u32, cvtuduw, 1)
VPK(uhum, u16, u8, I, 0)
VPK(uwum, u32, u16, I, 0)
VPK(udum, u64, u32, I, 0)
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

#define VROTATE(suffix, element, mask)                                  \
    void helper_vrl##suffix(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)   \
    {                                                                   \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->element); i++) {                  \
            unsigned int shift = b->element[i] & mask;                  \
            r->element[i] = (a->element[i] << shift) |                  \
                (a->element[i] >> (sizeof(a->element[0]) * 8 - shift)); \
        }                                                               \
    }
VROTATE(b, u8, 0x7)
VROTATE(h, u16, 0xF)
VROTATE(w, u32, 0x1F)
VROTATE(d, u64, 0x3F)
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

#define VSL(suffix, element, mask)                                      \
    void helper_vsl##suffix(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)   \
    {                                                                   \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->element); i++) {                  \
            unsigned int shift = b->element[i] & mask;                  \
                                                                        \
            r->element[i] = a->element[i] << shift;                     \
        }                                                               \
    }
VSL(b, u8, 0x7)
VSL(h, u16, 0x0F)
VSL(w, u32, 0x1F)
VSL(d, u64, 0x3F)
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

#define VSR(suffix, element, mask)                                      \
    void helper_vsr##suffix(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)   \
    {                                                                   \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->element); i++) {                  \
            unsigned int shift = b->element[i] & mask;                  \
            r->element[i] = a->element[i] >> shift;                     \
        }                                                               \
    }
VSR(ab, s8, 0x7)
VSR(ah, s16, 0xF)
VSR(aw, s32, 0x1F)
VSR(ad, s64, 0x3F)
VSR(b, u8, 0x7)
VSR(h, u16, 0xF)
VSR(w, u32, 0x1F)
VSR(d, u64, 0x3F)
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
VUPK(hsw, s64, s32, UPKHI)
VUPK(lsb, s16, s8, UPKLO)
VUPK(lsh, s32, s16, UPKLO)
VUPK(lsw, s64, s32, UPKLO)
#undef VUPK
#undef UPKHI
#undef UPKLO

#define VGENERIC_DO(name, element)                                      \
    void helper_v##name(ppc_avr_t *r, ppc_avr_t *b)                     \
    {                                                                   \
        int i;                                                          \
                                                                        \
        VECTOR_FOR_INORDER_I(i, element) {                              \
            r->element[i] = name(b->element[i]);                        \
        }                                                               \
    }

#define clzb(v) ((v) ? clz32((uint32_t)(v) << 24) : 8)
#define clzh(v) ((v) ? clz32((uint32_t)(v) << 16) : 16)
#define clzw(v) clz32((v))
#define clzd(v) clz64((v))

VGENERIC_DO(clzb, u8)
VGENERIC_DO(clzh, u16)
VGENERIC_DO(clzw, u32)
VGENERIC_DO(clzd, u64)

#undef clzb
#undef clzh
#undef clzw
#undef clzd

#define popcntb(v) ctpop8(v)
#define popcnth(v) ctpop16(v)
#define popcntw(v) ctpop32(v)
#define popcntd(v) ctpop64(v)

VGENERIC_DO(popcntb, u8)
VGENERIC_DO(popcnth, u16)
VGENERIC_DO(popcntw, u32)
VGENERIC_DO(popcntd, u64)

#undef popcntb
#undef popcnth
#undef popcntw
#undef popcntd

#undef VGENERIC_DO

#if defined(HOST_WORDS_BIGENDIAN)
#define QW_ONE { .u64 = { 0, 1 } }
#else
#define QW_ONE { .u64 = { 1, 0 } }
#endif

#ifndef CONFIG_INT128

static inline void avr_qw_not(ppc_avr_t *t, ppc_avr_t a)
{
    t->u64[0] = ~a.u64[0];
    t->u64[1] = ~a.u64[1];
}

static int avr_qw_cmpu(ppc_avr_t a, ppc_avr_t b)
{
    if (a.u64[HI_IDX] < b.u64[HI_IDX]) {
        return -1;
    } else if (a.u64[HI_IDX] > b.u64[HI_IDX]) {
        return 1;
    } else if (a.u64[LO_IDX] < b.u64[LO_IDX]) {
        return -1;
    } else if (a.u64[LO_IDX] > b.u64[LO_IDX]) {
        return 1;
    } else {
        return 0;
    }
}

static void avr_qw_add(ppc_avr_t *t, ppc_avr_t a, ppc_avr_t b)
{
    t->u64[LO_IDX] = a.u64[LO_IDX] + b.u64[LO_IDX];
    t->u64[HI_IDX] = a.u64[HI_IDX] + b.u64[HI_IDX] +
                     (~a.u64[LO_IDX] < b.u64[LO_IDX]);
}

static int avr_qw_addc(ppc_avr_t *t, ppc_avr_t a, ppc_avr_t b)
{
    ppc_avr_t not_a;
    t->u64[LO_IDX] = a.u64[LO_IDX] + b.u64[LO_IDX];
    t->u64[HI_IDX] = a.u64[HI_IDX] + b.u64[HI_IDX] +
                     (~a.u64[LO_IDX] < b.u64[LO_IDX]);
    avr_qw_not(&not_a, a);
    return avr_qw_cmpu(not_a, b) < 0;
}

#endif

void helper_vadduqm(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
#ifdef CONFIG_INT128
    r->u128 = a->u128 + b->u128;
#else
    avr_qw_add(r, *a, *b);
#endif
}

void helper_vaddeuqm(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, ppc_avr_t *c)
{
#ifdef CONFIG_INT128
    r->u128 = a->u128 + b->u128 + (c->u128 & 1);
#else

    if (c->u64[LO_IDX] & 1) {
        ppc_avr_t tmp;

        tmp.u64[HI_IDX] = 0;
        tmp.u64[LO_IDX] = c->u64[LO_IDX] & 1;
        avr_qw_add(&tmp, *a, tmp);
        avr_qw_add(r, tmp, *b);
    } else {
        avr_qw_add(r, *a, *b);
    }
#endif
}

void helper_vaddcuq(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
#ifdef CONFIG_INT128
    r->u128 = (~a->u128 < b->u128);
#else
    ppc_avr_t not_a;

    avr_qw_not(&not_a, *a);

    r->u64[HI_IDX] = 0;
    r->u64[LO_IDX] = (avr_qw_cmpu(not_a, *b) < 0);
#endif
}

void helper_vaddecuq(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, ppc_avr_t *c)
{
#ifdef CONFIG_INT128
    int carry_out = (~a->u128 < b->u128);
    if (!carry_out && (c->u128 & 1)) {
        carry_out = ((a->u128 + b->u128 + 1) == 0) &&
                    ((a->u128 != 0) || (b->u128 != 0));
    }
    r->u128 = carry_out;
#else

    int carry_in = c->u64[LO_IDX] & 1;
    int carry_out = 0;
    ppc_avr_t tmp;

    carry_out = avr_qw_addc(&tmp, *a, *b);

    if (!carry_out && carry_in) {
        ppc_avr_t one = QW_ONE;
        carry_out = avr_qw_addc(&tmp, tmp, one);
    }
    r->u64[HI_IDX] = 0;
    r->u64[LO_IDX] = carry_out;
#endif
}

void helper_vsubuqm(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
#ifdef CONFIG_INT128
    r->u128 = a->u128 - b->u128;
#else
    ppc_avr_t tmp;
    ppc_avr_t one = QW_ONE;

    avr_qw_not(&tmp, *b);
    avr_qw_add(&tmp, *a, tmp);
    avr_qw_add(r, tmp, one);
#endif
}

void helper_vsubeuqm(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, ppc_avr_t *c)
{
#ifdef CONFIG_INT128
    r->u128 = a->u128 + ~b->u128 + (c->u128 & 1);
#else
    ppc_avr_t tmp, sum;

    avr_qw_not(&tmp, *b);
    avr_qw_add(&sum, *a, tmp);

    tmp.u64[HI_IDX] = 0;
    tmp.u64[LO_IDX] = c->u64[LO_IDX] & 1;
    avr_qw_add(r, sum, tmp);
#endif
}

void helper_vsubcuq(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
#ifdef CONFIG_INT128
    r->u128 = (~a->u128 < ~b->u128) ||
                 (a->u128 + ~b->u128 == (__uint128_t)-1);
#else
    int carry = (avr_qw_cmpu(*a, *b) > 0);
    if (!carry) {
        ppc_avr_t tmp;
        avr_qw_not(&tmp, *b);
        avr_qw_add(&tmp, *a, tmp);
        carry = ((tmp.s64[HI_IDX] == -1ull) && (tmp.s64[LO_IDX] == -1ull));
    }
    r->u64[HI_IDX] = 0;
    r->u64[LO_IDX] = carry;
#endif
}

void helper_vsubecuq(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, ppc_avr_t *c)
{
#ifdef CONFIG_INT128
    r->u128 =
        (~a->u128 < ~b->u128) ||
        ((c->u128 & 1) && (a->u128 + ~b->u128 == (__uint128_t)-1));
#else
    int carry_in = c->u64[LO_IDX] & 1;
    int carry_out = (avr_qw_cmpu(*a, *b) > 0);
    if (!carry_out && carry_in) {
        ppc_avr_t tmp;
        avr_qw_not(&tmp, *b);
        avr_qw_add(&tmp, *a, tmp);
        carry_out = ((tmp.u64[HI_IDX] == -1ull) && (tmp.u64[LO_IDX] == -1ull));
    }

    r->u64[HI_IDX] = 0;
    r->u64[LO_IDX] = carry_out;
#endif
}

#define BCD_PLUS_PREF_1 0xC
#define BCD_PLUS_PREF_2 0xF
#define BCD_PLUS_ALT_1  0xA
#define BCD_NEG_PREF    0xD
#define BCD_NEG_ALT     0xB
#define BCD_PLUS_ALT_2  0xE

#if defined(HOST_WORDS_BIGENDIAN)
#define BCD_DIG_BYTE(n) (15 - (n/2))
#else
#define BCD_DIG_BYTE(n) (n/2)
#endif

static int bcd_get_sgn(ppc_avr_t *bcd)
{
    switch (bcd->u8[BCD_DIG_BYTE(0)] & 0xF) {
    case BCD_PLUS_PREF_1:
    case BCD_PLUS_PREF_2:
    case BCD_PLUS_ALT_1:
    case BCD_PLUS_ALT_2:
    {
        return 1;
    }

    case BCD_NEG_PREF:
    case BCD_NEG_ALT:
    {
        return -1;
    }

    default:
    {
        return 0;
    }
    }
}

static int bcd_preferred_sgn(int sgn, int ps)
{
    if (sgn >= 0) {
        return (ps == 0) ? BCD_PLUS_PREF_1 : BCD_PLUS_PREF_2;
    } else {
        return BCD_NEG_PREF;
    }
}

static uint8_t bcd_get_digit(ppc_avr_t *bcd, int n, int *invalid)
{
    uint8_t result;
    if (n & 1) {
        result = bcd->u8[BCD_DIG_BYTE(n)] >> 4;
    } else {
       result = bcd->u8[BCD_DIG_BYTE(n)] & 0xF;
    }

    if (unlikely(result > 9)) {
        *invalid = true;
    }
    return result;
}

static void bcd_put_digit(ppc_avr_t *bcd, uint8_t digit, int n)
{
    if (n & 1) {
        bcd->u8[BCD_DIG_BYTE(n)] &= 0x0F;
        bcd->u8[BCD_DIG_BYTE(n)] |= (digit<<4);
    } else {
        bcd->u8[BCD_DIG_BYTE(n)] &= 0xF0;
        bcd->u8[BCD_DIG_BYTE(n)] |= digit;
    }
}

static int bcd_cmp_mag(ppc_avr_t *a, ppc_avr_t *b)
{
    int i;
    int invalid = 0;
    for (i = 31; i > 0; i--) {
        uint8_t dig_a = bcd_get_digit(a, i, &invalid);
        uint8_t dig_b = bcd_get_digit(b, i, &invalid);
        if (unlikely(invalid)) {
            return 0; /* doesn't matter */
        } else if (dig_a > dig_b) {
            return 1;
        } else if (dig_a < dig_b) {
            return -1;
        }
    }

    return 0;
}

static int bcd_add_mag(ppc_avr_t *t, ppc_avr_t *a, ppc_avr_t *b, int *invalid,
                       int *overflow)
{
    int carry = 0;
    int i;
    int is_zero = 1;
    for (i = 1; i <= 31; i++) {
        uint8_t digit = bcd_get_digit(a, i, invalid) +
                        bcd_get_digit(b, i, invalid) + carry;
        is_zero &= (digit == 0);
        if (digit > 9) {
            carry = 1;
            digit -= 10;
        } else {
            carry = 0;
        }

        bcd_put_digit(t, digit, i);

        if (unlikely(*invalid)) {
            return -1;
        }
    }

    *overflow = carry;
    return is_zero;
}

static int bcd_sub_mag(ppc_avr_t *t, ppc_avr_t *a, ppc_avr_t *b, int *invalid,
                       int *overflow)
{
    int carry = 0;
    int i;
    int is_zero = 1;
    for (i = 1; i <= 31; i++) {
        uint8_t digit = bcd_get_digit(a, i, invalid) -
                        bcd_get_digit(b, i, invalid) + carry;
        is_zero &= (digit == 0);
        if (digit & 0x80) {
            carry = -1;
            digit += 10;
        } else {
            carry = 0;
        }

        bcd_put_digit(t, digit, i);

        if (unlikely(*invalid)) {
            return -1;
        }
    }

    *overflow = carry;
    return is_zero;
}

uint32_t helper_bcdadd(ppc_avr_t *r,  ppc_avr_t *a, ppc_avr_t *b, uint32_t ps)
{

    int sgna = bcd_get_sgn(a);
    int sgnb = bcd_get_sgn(b);
    int invalid = (sgna == 0) || (sgnb == 0);
    int overflow = 0;
    int zero = 0;
    uint32_t cr = 0;
    ppc_avr_t result = { .u64 = { 0, 0 } };

    if (!invalid) {
        if (sgna == sgnb) {
            result.u8[BCD_DIG_BYTE(0)] = bcd_preferred_sgn(sgna, ps);
            zero = bcd_add_mag(&result, a, b, &invalid, &overflow);
            cr = (sgna > 0) ? 4 : 8;
        } else if (bcd_cmp_mag(a, b) > 0) {
            result.u8[BCD_DIG_BYTE(0)] = bcd_preferred_sgn(sgna, ps);
            zero = bcd_sub_mag(&result, a, b, &invalid, &overflow);
            cr = (sgna > 0) ? 4 : 8;
        } else {
            result.u8[BCD_DIG_BYTE(0)] = bcd_preferred_sgn(sgnb, ps);
            zero = bcd_sub_mag(&result, b, a, &invalid, &overflow);
            cr = (sgnb > 0) ? 4 : 8;
        }
    }

    if (unlikely(invalid)) {
        result.u64[HI_IDX] = result.u64[LO_IDX] = -1;
        cr = 1;
    } else if (overflow) {
        cr |= 1;
    } else if (zero) {
        cr = 2;
    }

    *r = result;

    return cr;
}

uint32_t helper_bcdsub(ppc_avr_t *r,  ppc_avr_t *a, ppc_avr_t *b, uint32_t ps)
{
    ppc_avr_t bcopy = *b;
    int sgnb = bcd_get_sgn(b);
    if (sgnb < 0) {
        bcd_put_digit(&bcopy, BCD_PLUS_PREF_1, 0);
    } else if (sgnb > 0) {
        bcd_put_digit(&bcopy, BCD_NEG_PREF, 0);
    }
    /* else invalid ... defer to bcdadd code for proper handling */

    return helper_bcdadd(r, a, &bcopy, ps);
}

static uint8_t SBOX[256] = {
0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5,
0x30, 0x01, 0x67, 0x2B, 0xFE, 0xD7, 0xAB, 0x76,
0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0,
0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0,
0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F, 0xF7, 0xCC,
0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A,
0x07, 0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75,
0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0,
0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84,
0x53, 0xD1, 0x00, 0xED, 0x20, 0xFC, 0xB1, 0x5B,
0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85,
0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8,
0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5,
0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2,
0xCD, 0x0C, 0x13, 0xEC, 0x5F, 0x97, 0x44, 0x17,
0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88,
0x46, 0xEE, 0xB8, 0x14, 0xDE, 0x5E, 0x0B, 0xDB,
0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C,
0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79,
0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9,
0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6,
0xE8, 0xDD, 0x74, 0x1F, 0x4B, 0xBD, 0x8B, 0x8A,
0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E,
0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E,
0xE1, 0xF8, 0x98, 0x11, 0x69, 0xD9, 0x8E, 0x94,
0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68,
0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16,
};

static void SubBytes(ppc_avr_t *r, ppc_avr_t *a)
{
    int i;
    VECTOR_FOR_INORDER_I(i, u8) {
        r->u8[i] = SBOX[a->u8[i]];
    }
}

static uint8_t InvSBOX[256] = {
0x52, 0x09, 0x6A, 0xD5, 0x30, 0x36, 0xA5, 0x38,
0xBF, 0x40, 0xA3, 0x9E, 0x81, 0xF3, 0xD7, 0xFB,
0x7C, 0xE3, 0x39, 0x82, 0x9B, 0x2F, 0xFF, 0x87,
0x34, 0x8E, 0x43, 0x44, 0xC4, 0xDE, 0xE9, 0xCB,
0x54, 0x7B, 0x94, 0x32, 0xA6, 0xC2, 0x23, 0x3D,
0xEE, 0x4C, 0x95, 0x0B, 0x42, 0xFA, 0xC3, 0x4E,
0x08, 0x2E, 0xA1, 0x66, 0x28, 0xD9, 0x24, 0xB2,
0x76, 0x5B, 0xA2, 0x49, 0x6D, 0x8B, 0xD1, 0x25,
0x72, 0xF8, 0xF6, 0x64, 0x86, 0x68, 0x98, 0x16,
0xD4, 0xA4, 0x5C, 0xCC, 0x5D, 0x65, 0xB6, 0x92,
0x6C, 0x70, 0x48, 0x50, 0xFD, 0xED, 0xB9, 0xDA,
0x5E, 0x15, 0x46, 0x57, 0xA7, 0x8D, 0x9D, 0x84,
0x90, 0xD8, 0xAB, 0x00, 0x8C, 0xBC, 0xD3, 0x0A,
0xF7, 0xE4, 0x58, 0x05, 0xB8, 0xB3, 0x45, 0x06,
0xD0, 0x2C, 0x1E, 0x8F, 0xCA, 0x3F, 0x0F, 0x02,
0xC1, 0xAF, 0xBD, 0x03, 0x01, 0x13, 0x8A, 0x6B,
0x3A, 0x91, 0x11, 0x41, 0x4F, 0x67, 0xDC, 0xEA,
0x97, 0xF2, 0xCF, 0xCE, 0xF0, 0xB4, 0xE6, 0x73,
0x96, 0xAC, 0x74, 0x22, 0xE7, 0xAD, 0x35, 0x85,
0xE2, 0xF9, 0x37, 0xE8, 0x1C, 0x75, 0xDF, 0x6E,
0x47, 0xF1, 0x1A, 0x71, 0x1D, 0x29, 0xC5, 0x89,
0x6F, 0xB7, 0x62, 0x0E, 0xAA, 0x18, 0xBE, 0x1B,
0xFC, 0x56, 0x3E, 0x4B, 0xC6, 0xD2, 0x79, 0x20,
0x9A, 0xDB, 0xC0, 0xFE, 0x78, 0xCD, 0x5A, 0xF4,
0x1F, 0xDD, 0xA8, 0x33, 0x88, 0x07, 0xC7, 0x31,
0xB1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xEC, 0x5F,
0x60, 0x51, 0x7F, 0xA9, 0x19, 0xB5, 0x4A, 0x0D,
0x2D, 0xE5, 0x7A, 0x9F, 0x93, 0xC9, 0x9C, 0xEF,
0xA0, 0xE0, 0x3B, 0x4D, 0xAE, 0x2A, 0xF5, 0xB0,
0xC8, 0xEB, 0xBB, 0x3C, 0x83, 0x53, 0x99, 0x61,
0x17, 0x2B, 0x04, 0x7E, 0xBA, 0x77, 0xD6, 0x26,
0xE1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0C, 0x7D,
};

static void InvSubBytes(ppc_avr_t *r, ppc_avr_t *a)
{
    int i;
    VECTOR_FOR_INORDER_I(i, u8) {
        r->u8[i] = InvSBOX[a->u8[i]];
    }
}

static uint8_t ROTL8(uint8_t x, int n)
{
    return (x << n) | (x >> (8-n));
}

static inline int BIT8(uint8_t x, int n)
{
    return (x & (0x80 >> n)) != 0;
}

static uint8_t GFx02(uint8_t x)
{
    return ROTL8(x, 1) ^ (BIT8(x, 0) ? 0x1A : 0);
}

static uint8_t GFx03(uint8_t x)
{
    return x ^ ROTL8(x, 1) ^ (BIT8(x, 0) ? 0x1A : 0);
}

static uint8_t GFx09(uint8_t x)
{
    uint8_t term2 = ROTL8(x, 3);
    uint8_t term3 = (BIT8(x, 0) ? 0x68 : 0) | (BIT8(x, 1) ? 0x14 : 0) |
                    (BIT8(x, 2) ? 0x02 : 0);
    uint8_t term4 = (BIT8(x, 1) ? 0x20 : 0) | (BIT8(x, 2) ? 0x18 : 0);
    return x ^ term2 ^ term3 ^ term4;
}

static uint8_t GFx0B(uint8_t x)
{
    uint8_t term2 = ROTL8(x, 1);
    uint8_t term3 = (x << 3) | (BIT8(x, 0) ? 0x06 : 0) |
                    (BIT8(x, 2) ? 0x01 : 0);
    uint8_t term4 = (BIT8(x, 0) ? 0x70 : 0) | (BIT8(x, 1) ? 0x06 : 0) |
                    (BIT8(x, 2) ? 0x08 : 0);
    uint8_t term5 = (BIT8(x, 1) ? 0x30 : 0) | (BIT8(x, 2) ? 0x02 : 0);
    uint8_t term6 = BIT8(x, 2) ? 0x10 : 0;
    return x ^ term2 ^ term3 ^ term4 ^ term5 ^ term6;
}

static uint8_t GFx0D(uint8_t x)
{
    uint8_t term2 = ROTL8(x, 2);
    uint8_t term3 = (x << 3) | (BIT8(x, 1) ? 0x04 : 0) |
                    (BIT8(x, 2) ? 0x03 : 0);
    uint8_t term4 = (BIT8(x, 0) ? 0x58 : 0) | (BIT8(x, 1) ? 0x20 : 0);
    uint8_t term5 = (BIT8(x, 1) ? 0x08 : 0) | (BIT8(x, 2) ? 0x10 : 0);
    uint8_t term6 = BIT8(x, 2) ? 0x08 : 0;
    return x ^ term2 ^ term3 ^ term4 ^ term5 ^ term6;
}

static uint8_t GFx0E(uint8_t x)
{
    uint8_t term1 = ROTL8(x, 1);
    uint8_t term2 = (x << 2) | (BIT8(x, 2) ? 0x02 : 0) |
                    (BIT8(x, 1) ? 0x01 : 0);
    uint8_t term3 = (x << 3) | (BIT8(x, 1) ? 0x04 : 0) |
                    (BIT8(x, 2) ? 0x01 : 0);
    uint8_t term4 = (BIT8(x, 0) ? 0x40 : 0) | (BIT8(x, 1) ? 0x28 : 0) |
                    (BIT8(x, 2) ? 0x10 : 0);
    uint8_t term5 = (BIT8(x, 2) ? 0x08 : 0);
    return term1 ^ term2 ^ term3 ^ term4 ^ term5;
}

#if defined(HOST_WORDS_BIGENDIAN)
#define MCB(x, i, b) ((x)->u8[(i)*4 + (b)])
#else
#define MCB(x, i, b) ((x)->u8[15 - ((i)*4 + (b))])
#endif

static void MixColumns(ppc_avr_t *r, ppc_avr_t *x)
{
    int i;
    for (i = 0; i < 4; i++) {
        MCB(r, i, 0) = GFx02(MCB(x, i, 0)) ^ GFx03(MCB(x, i, 1)) ^
                       MCB(x, i, 2) ^ MCB(x, i, 3);
        MCB(r, i, 1) = MCB(x, i, 0) ^ GFx02(MCB(x, i, 1)) ^
                       GFx03(MCB(x, i, 2)) ^ MCB(x, i, 3);
        MCB(r, i, 2) = MCB(x, i, 0) ^ MCB(x, i, 1) ^
                       GFx02(MCB(x, i, 2)) ^ GFx03(MCB(x, i, 3));
        MCB(r, i, 3) = GFx03(MCB(x, i, 0)) ^ MCB(x, i, 1) ^
                       MCB(x, i, 2) ^ GFx02(MCB(x, i, 3));
    }
}

static void InvMixColumns(ppc_avr_t *r, ppc_avr_t *x)
{
    int i;
    for (i = 0; i < 4; i++) {
        MCB(r, i, 0) = GFx0E(MCB(x, i, 0)) ^ GFx0B(MCB(x, i, 1)) ^
                       GFx0D(MCB(x, i, 2)) ^ GFx09(MCB(x, i, 3));
        MCB(r, i, 1) = GFx09(MCB(x, i, 0)) ^ GFx0E(MCB(x, i, 1)) ^
                       GFx0B(MCB(x, i, 2)) ^ GFx0D(MCB(x, i, 3));
        MCB(r, i, 2) = GFx0D(MCB(x, i, 0)) ^ GFx09(MCB(x, i, 1)) ^
                       GFx0E(MCB(x, i, 2)) ^ GFx0B(MCB(x, i, 3));
        MCB(r, i, 3) = GFx0B(MCB(x, i, 0)) ^ GFx0D(MCB(x, i, 1)) ^
                       GFx09(MCB(x, i, 2)) ^ GFx0E(MCB(x, i, 3));
    }
}

static void ShiftRows(ppc_avr_t *r, ppc_avr_t *x)
{
    MCB(r, 0, 0) = MCB(x, 0, 0);
    MCB(r, 1, 0) = MCB(x, 1, 0);
    MCB(r, 2, 0) = MCB(x, 2, 0);
    MCB(r, 3, 0) = MCB(x, 3, 0);

    MCB(r, 0, 1) = MCB(x, 1, 1);
    MCB(r, 1, 1) = MCB(x, 2, 1);
    MCB(r, 2, 1) = MCB(x, 3, 1);
    MCB(r, 3, 1) = MCB(x, 0, 1);

    MCB(r, 0, 2) = MCB(x, 2, 2);
    MCB(r, 1, 2) = MCB(x, 3, 2);
    MCB(r, 2, 2) = MCB(x, 0, 2);
    MCB(r, 3, 2) = MCB(x, 1, 2);

    MCB(r, 0, 3) = MCB(x, 3, 3);
    MCB(r, 1, 3) = MCB(x, 0, 3);
    MCB(r, 2, 3) = MCB(x, 1, 3);
    MCB(r, 3, 3) = MCB(x, 2, 3);
}

static void InvShiftRows(ppc_avr_t *r, ppc_avr_t *x)
{
    MCB(r, 0, 0) = MCB(x, 0, 0);
    MCB(r, 1, 0) = MCB(x, 1, 0);
    MCB(r, 2, 0) = MCB(x, 2, 0);
    MCB(r, 3, 0) = MCB(x, 3, 0);

    MCB(r, 0, 1) = MCB(x, 3, 1);
    MCB(r, 1, 1) = MCB(x, 0, 1);
    MCB(r, 2, 1) = MCB(x, 1, 1);
    MCB(r, 3, 1) = MCB(x, 2, 1);

    MCB(r, 0, 2) = MCB(x, 2, 2);
    MCB(r, 1, 2) = MCB(x, 3, 2);
    MCB(r, 2, 2) = MCB(x, 0, 2);
    MCB(r, 3, 2) = MCB(x, 1, 2);

    MCB(r, 0, 3) = MCB(x, 1, 3);
    MCB(r, 1, 3) = MCB(x, 2, 3);
    MCB(r, 2, 3) = MCB(x, 3, 3);
    MCB(r, 3, 3) = MCB(x, 0, 3);
}

#undef MCB

void helper_vsbox(ppc_avr_t *r, ppc_avr_t *a)
{
    SubBytes(r, a);
}

void helper_vcipher(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    ppc_avr_t vtemp1, vtemp2, vtemp3;
    SubBytes(&vtemp1, a);
    ShiftRows(&vtemp2, &vtemp1);
    MixColumns(&vtemp3, &vtemp2);
    r->u64[0] = vtemp3.u64[0] ^ b->u64[0];
    r->u64[1] = vtemp3.u64[1] ^ b->u64[1];
}

void helper_vcipherlast(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    ppc_avr_t vtemp1, vtemp2;
    SubBytes(&vtemp1, a);
    ShiftRows(&vtemp2, &vtemp1);
    r->u64[0] = vtemp2.u64[0] ^ b->u64[0];
    r->u64[1] = vtemp2.u64[1] ^ b->u64[1];
}

void helper_vncipher(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    /* This differs from what is written in ISA V2.07.  The RTL is */
    /* incorrect and will be fixed in V2.07B.                      */
    ppc_avr_t vtemp1, vtemp2, vtemp3;
    InvShiftRows(&vtemp1, a);
    InvSubBytes(&vtemp2, &vtemp1);
    vtemp3.u64[0] = vtemp2.u64[0] ^ b->u64[0];
    vtemp3.u64[1] = vtemp2.u64[1] ^ b->u64[1];
    InvMixColumns(r, &vtemp3);
}

void helper_vncipherlast(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    ppc_avr_t vtemp1, vtemp2;
    InvShiftRows(&vtemp1, a);
    InvSubBytes(&vtemp2, &vtemp1);
    r->u64[0] = vtemp2.u64[0] ^ b->u64[0];
    r->u64[1] = vtemp2.u64[1] ^ b->u64[1];
}

#define ROTRu32(v, n) (((v) >> (n)) | ((v) << (32-n)))
#if defined(HOST_WORDS_BIGENDIAN)
#define EL_IDX(i) (i)
#else
#define EL_IDX(i) (3 - (i))
#endif

void helper_vshasigmaw(ppc_avr_t *r,  ppc_avr_t *a, uint32_t st_six)
{
    int st = (st_six & 0x10) != 0;
    int six = st_six & 0xF;
    int i;

    VECTOR_FOR_INORDER_I(i, u32) {
        if (st == 0) {
            if ((six & (0x8 >> i)) == 0) {
                r->u32[EL_IDX(i)] = ROTRu32(a->u32[EL_IDX(i)], 7) ^
                                    ROTRu32(a->u32[EL_IDX(i)], 18) ^
                                    (a->u32[EL_IDX(i)] >> 3);
            } else { /* six.bit[i] == 1 */
                r->u32[EL_IDX(i)] = ROTRu32(a->u32[EL_IDX(i)], 17) ^
                                    ROTRu32(a->u32[EL_IDX(i)], 19) ^
                                    (a->u32[EL_IDX(i)] >> 10);
            }
        } else { /* st == 1 */
            if ((six & (0x8 >> i)) == 0) {
                r->u32[EL_IDX(i)] = ROTRu32(a->u32[EL_IDX(i)], 2) ^
                                    ROTRu32(a->u32[EL_IDX(i)], 13) ^
                                    ROTRu32(a->u32[EL_IDX(i)], 22);
            } else { /* six.bit[i] == 1 */
                r->u32[EL_IDX(i)] = ROTRu32(a->u32[EL_IDX(i)], 6) ^
                                    ROTRu32(a->u32[EL_IDX(i)], 11) ^
                                    ROTRu32(a->u32[EL_IDX(i)], 25);
            }
        }
    }
}

#undef ROTRu32
#undef EL_IDX

#define ROTRu64(v, n) (((v) >> (n)) | ((v) << (64-n)))
#if defined(HOST_WORDS_BIGENDIAN)
#define EL_IDX(i) (i)
#else
#define EL_IDX(i) (1 - (i))
#endif

void helper_vshasigmad(ppc_avr_t *r,  ppc_avr_t *a, uint32_t st_six)
{
    int st = (st_six & 0x10) != 0;
    int six = st_six & 0xF;
    int i;

    VECTOR_FOR_INORDER_I(i, u64) {
        if (st == 0) {
            if ((six & (0x8 >> (2*i))) == 0) {
                r->u64[EL_IDX(i)] = ROTRu64(a->u64[EL_IDX(i)], 1) ^
                                    ROTRu64(a->u64[EL_IDX(i)], 8) ^
                                    (a->u64[EL_IDX(i)] >> 7);
            } else { /* six.bit[2*i] == 1 */
                r->u64[EL_IDX(i)] = ROTRu64(a->u64[EL_IDX(i)], 19) ^
                                    ROTRu64(a->u64[EL_IDX(i)], 61) ^
                                    (a->u64[EL_IDX(i)] >> 6);
            }
        } else { /* st == 1 */
            if ((six & (0x8 >> (2*i))) == 0) {
                r->u64[EL_IDX(i)] = ROTRu64(a->u64[EL_IDX(i)], 28) ^
                                    ROTRu64(a->u64[EL_IDX(i)], 34) ^
                                    ROTRu64(a->u64[EL_IDX(i)], 39);
            } else { /* six.bit[2*i] == 1 */
                r->u64[EL_IDX(i)] = ROTRu64(a->u64[EL_IDX(i)], 14) ^
                                    ROTRu64(a->u64[EL_IDX(i)], 18) ^
                                    ROTRu64(a->u64[EL_IDX(i)], 41);
            }
        }
    }
}

#undef ROTRu64
#undef EL_IDX

void helper_vpermxor(ppc_avr_t *r,  ppc_avr_t *a, ppc_avr_t *b, ppc_avr_t *c)
{
    int i;
    VECTOR_FOR_INORDER_I(i, u8) {
        int indexA = c->u8[i] >> 4;
        int indexB = c->u8[i] & 0xF;
#if defined(HOST_WORDS_BIGENDIAN)
        r->u8[i] = a->u8[indexA] ^ b->u8[indexB];
#else
        r->u8[i] = a->u8[15-indexA] ^ b->u8[15-indexB];
#endif
    }
}

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
