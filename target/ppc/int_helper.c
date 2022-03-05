/*
 *  PowerPC integer and vector emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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
#include "internal.h"
#include "qemu/host-utils.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "exec/helper-proto.h"
#include "crypto/aes.h"
#include "fpu/softfloat.h"
#include "qapi/error.h"
#include "qemu/guest-random.h"
#include "tcg/tcg-gvec-desc.h"

#include "helper_regs.h"
/*****************************************************************************/
/* Fixed point operations helpers */

static inline void helper_update_ov_legacy(CPUPPCState *env, int ov)
{
    if (unlikely(ov)) {
        env->so = env->ov = 1;
    } else {
        env->ov = 0;
    }
}

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
        helper_update_ov_legacy(env, overflow);
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
        helper_update_ov_legacy(env, overflow);
    }

    return (target_ulong)rt;
}

#if defined(TARGET_PPC64)

uint64_t helper_divdeu(CPUPPCState *env, uint64_t ra, uint64_t rb, uint32_t oe)
{
    uint64_t rt = 0;
    int overflow = 0;

    if (unlikely(rb == 0 || ra >= rb)) {
        overflow = 1;
        rt = 0; /* Undefined */
    } else {
        divu128(&rt, &ra, rb);
    }

    if (oe) {
        helper_update_ov_legacy(env, overflow);
    }

    return rt;
}

uint64_t helper_divde(CPUPPCState *env, uint64_t rau, uint64_t rbu, uint32_t oe)
{
    uint64_t rt = 0;
    int64_t ra = (int64_t)rau;
    int64_t rb = (int64_t)rbu;
    int overflow = 0;

    if (unlikely(rb == 0 || uabs64(ra) >= uabs64(rb))) {
        overflow = 1;
        rt = 0; /* Undefined */
    } else {
        divs128(&rt, &ra, rb);
    }

    if (oe) {
        helper_update_ov_legacy(env, overflow);
    }

    return rt;
}

#endif


#if defined(TARGET_PPC64)
/* if x = 0xab, returns 0xababababababababa */
#define pattern(x) (((x) & 0xff) * (~(target_ulong)0 / 0xff))

/*
 * subtract 1 from each byte, and with inverse, check if MSB is set at each
 * byte.
 * i.e. ((0x00 - 0x01) & ~(0x00)) & 0x80
 *      (0xFF & 0xFF) & 0x80 = 0x80 (zero found)
 */
#define haszero(v) (((v) - pattern(0x01)) & ~(v) & pattern(0x80))

/* When you XOR the pattern and there is a match, that byte will be zero */
#define hasvalue(x, n)  (haszero((x) ^ pattern(n)))

uint32_t helper_cmpeqb(target_ulong ra, target_ulong rb)
{
    return hasvalue(rb, ra) ? CRF_GT : 0;
}

#undef pattern
#undef haszero
#undef hasvalue

/*
 * Return a random number.
 */
uint64_t helper_darn32(void)
{
    Error *err = NULL;
    uint32_t ret;

    if (qemu_guest_getrandom(&ret, sizeof(ret), &err) < 0) {
        qemu_log_mask(LOG_UNIMP, "darn: Crypto failure: %s",
                      error_get_pretty(err));
        error_free(err);
        return -1;
    }

    return ret;
}

uint64_t helper_darn64(void)
{
    Error *err = NULL;
    uint64_t ret;

    if (qemu_guest_getrandom(&ret, sizeof(ret), &err) < 0) {
        qemu_log_mask(LOG_UNIMP, "darn: Crypto failure: %s",
                      error_get_pretty(err));
        error_free(err);
        return -1;
    }

    return ret;
}

uint64_t helper_bpermd(uint64_t rs, uint64_t rb)
{
    int i;
    uint64_t ra = 0;

    for (i = 0; i < 8; i++) {
        int index = (rs >> (i * 8)) & 0xFF;
        if (index < 64) {
            if (rb & PPC_BIT(index)) {
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
                env->ca32 = env->ca = 0;
            } else {
                env->ca32 = env->ca = 1;
            }
        } else {
            ret = (int32_t)value;
            env->ca32 = env->ca = 0;
        }
    } else {
        ret = (int32_t)value >> 31;
        env->ca32 = env->ca = (ret != 0);
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
            if (likely(ret >= 0 || (value & ((1ULL << shift) - 1)) == 0)) {
                env->ca32 = env->ca = 0;
            } else {
                env->ca32 = env->ca = 1;
            }
        } else {
            ret = (int64_t)value;
            env->ca32 = env->ca = 0;
        }
    } else {
        ret = (int64_t)value >> 63;
        env->ca32 = env->ca = (ret != 0);
    }
    return ret;
}
#endif

#if defined(TARGET_PPC64)
target_ulong helper_popcntb(target_ulong val)
{
    /* Note that we don't fold past bytes */
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
    /* Note that we don't fold past words.  */
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
#else
target_ulong helper_popcntb(target_ulong val)
{
    /* Note that we don't fold past bytes */
    val = (val & 0x55555555) + ((val >>  1) & 0x55555555);
    val = (val & 0x33333333) + ((val >>  2) & 0x33333333);
    val = (val & 0x0f0f0f0f) + ((val >>  4) & 0x0f0f0f0f);
    return val;
}
#endif

uint64_t helper_CFUGED(uint64_t src, uint64_t mask)
{
    /*
     * Instead of processing the mask bit-by-bit from the most significant to
     * the least significant bit, as described in PowerISA, we'll handle it in
     * blocks of 'n' zeros/ones from LSB to MSB. To avoid the decision to use
     * ctz or cto, we negate the mask at the end of the loop.
     */
    target_ulong m, left = 0, right = 0;
    unsigned int n, i = 64;
    bool bit = false; /* tracks if we are processing zeros or ones */

    if (mask == 0 || mask == -1) {
        return src;
    }

    /* Processes the mask in blocks, from LSB to MSB */
    while (i) {
        /* Find how many bits we should take */
        n = ctz64(mask);
        if (n > i) {
            n = i;
        }

        /*
         * Extracts 'n' trailing bits of src and put them on the leading 'n'
         * bits of 'right' or 'left', pushing down the previously extracted
         * values.
         */
        m = (1ll << n) - 1;
        if (bit) {
            right = ror64(right | (src & m), n);
        } else {
            left = ror64(left | (src & m), n);
        }

        /*
         * Discards the processed bits from 'src' and 'mask'. Note that we are
         * removing 'n' trailing zeros from 'mask', but the logical shift will
         * add 'n' leading zeros back, so the population count of 'mask' is kept
         * the same.
         */
        src >>= n;
        mask >>= n;
        i -= n;
        bit = !bit;
        mask = ~mask;
    }

    /*
     * At the end, right was ror'ed ctpop(mask) times. To put it back in place,
     * we'll shift it more 64-ctpop(mask) times.
     */
    if (bit) {
        n = ctpop64(mask);
    } else {
        n = 64 - ctpop64(mask);
    }

    return left | (right >> n);
}

uint64_t helper_PDEPD(uint64_t src, uint64_t mask)
{
    int i, o;
    uint64_t result = 0;

    if (mask == -1) {
        return src;
    }

    for (i = 0; mask != 0; i++) {
        o = ctz64(mask);
        mask &= mask - 1;
        result |= ((src >> i) & 1) << o;
    }

    return result;
}

uint64_t helper_PEXTD(uint64_t src, uint64_t mask)
{
    int i, o;
    uint64_t result = 0;

    if (mask == -1) {
        return src;
    }

    for (o = 0; mask != 0; o++) {
        i = ctz64(mask);
        mask &= mask - 1;
        result |= ((src >> i) & 1) << o;
    }

    return result;
}

/*****************************************************************************/
/* Altivec extension helpers */
#if defined(HOST_WORDS_BIGENDIAN)
#define VECTOR_FOR_INORDER_I(index, element)                    \
    for (index = 0; index < ARRAY_SIZE(r->element); index++)
#else
#define VECTOR_FOR_INORDER_I(index, element)                    \
    for (index = ARRAY_SIZE(r->element) - 1; index >= 0; index--)
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

void helper_mtvscr(CPUPPCState *env, uint32_t vscr)
{
    ppc_store_vscr(env, vscr);
}

uint32_t helper_mfvscr(CPUPPCState *env)
{
    return ppc_get_vscr(env);
}

static inline void set_vscr_sat(CPUPPCState *env)
{
    /* The choice of non-zero value is arbitrary.  */
    env->vscr_sat.u32[0] = 1;
}

void helper_vaddcuw(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(r->u32); i++) {
        r->u32[i] = ~a->u32[i] < b->u32[i];
    }
}

/* vprtybw */
void helper_vprtybw(ppc_avr_t *r, ppc_avr_t *b)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(r->u32); i++) {
        uint64_t res = b->u32[i] ^ (b->u32[i] >> 16);
        res ^= res >> 8;
        r->u32[i] = res & 1;
    }
}

/* vprtybd */
void helper_vprtybd(ppc_avr_t *r, ppc_avr_t *b)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(r->u64); i++) {
        uint64_t res = b->u64[i] ^ (b->u64[i] >> 32);
        res ^= res >> 16;
        res ^= res >> 8;
        r->u64[i] = res & 1;
    }
}

/* vprtybq */
void helper_vprtybq(ppc_avr_t *r, ppc_avr_t *b)
{
    uint64_t res = b->u64[0] ^ b->u64[1];
    res ^= res >> 32;
    res ^= res >> 16;
    res ^= res >> 8;
    r->VsrD(1) = res & 1;
    r->VsrD(0) = 0;
}

#define VARITHFP(suffix, func)                                          \
    void helper_v##suffix(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a, \
                          ppc_avr_t *b)                                 \
    {                                                                   \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->f32); i++) {                      \
            r->f32[i] = func(a->f32[i], b->f32[i], &env->vec_status);   \
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
        for (i = 0; i < ARRAY_SIZE(r->f32); i++) {                      \
            r->f32[i] = float32_muladd(a->f32[i], c->f32[i], b->f32[i], \
                                       type, &env->vec_status);         \
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
    void helper_v##name(ppc_avr_t *r, ppc_avr_t *vscr_sat,              \
                        ppc_avr_t *a, ppc_avr_t *b, uint32_t desc)      \
    {                                                                   \
        int sat = 0;                                                    \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->element); i++) {                  \
            VARITHSAT_CASE(optype, op, cvt, element);                   \
        }                                                               \
        if (sat) {                                                      \
            vscr_sat->u32[0] = 1;                                       \
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

#define VABSDU_DO(name, element)                                        \
void helper_v##name(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)           \
{                                                                       \
    int i;                                                              \
                                                                        \
    for (i = 0; i < ARRAY_SIZE(r->element); i++) {                      \
        r->element[i] = (a->element[i] > b->element[i]) ?               \
            (a->element[i] - b->element[i]) :                           \
            (b->element[i] - a->element[i]);                            \
    }                                                                   \
}

/*
 * VABSDU - Vector absolute difference unsigned
 *   name    - instruction mnemonic suffix (b: byte, h: halfword, w: word)
 *   element - element type to access from vector
 */
#define VABSDU(type, element)                   \
    VABSDU_DO(absdu##type, element)
VABSDU(b, u8)
VABSDU(h, u16)
VABSDU(w, u32)
#undef VABSDU_DO
#undef VABSDU

#define VCF(suffix, cvt, element)                                       \
    void helper_vcf##suffix(CPUPPCState *env, ppc_avr_t *r,             \
                            ppc_avr_t *b, uint32_t uim)                 \
    {                                                                   \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->f32); i++) {                      \
            float32 t = cvt(b->element[i], &env->vec_status);           \
            r->f32[i] = float32_scalbn(t, -uim, &env->vec_status);      \
        }                                                               \
    }
VCF(ux, uint32_to_float32, u32)
VCF(sx, int32_to_float32, s32)
#undef VCF

#define VCMPNEZ(NAME, ELEM) \
void helper_##NAME(ppc_vsr_t *t, ppc_vsr_t *a, ppc_vsr_t *b, uint32_t desc) \
{                                                                           \
    for (int i = 0; i < ARRAY_SIZE(t->ELEM); i++) {                         \
        t->ELEM[i] = ((a->ELEM[i] == 0) || (b->ELEM[i] == 0) ||             \
                      (a->ELEM[i] != b->ELEM[i])) ? -1 : 0;                 \
    }                                                                       \
}
VCMPNEZ(VCMPNEZB, u8)
VCMPNEZ(VCMPNEZH, u16)
VCMPNEZ(VCMPNEZW, u32)
#undef VCMPNEZ

#define VCMPFP_DO(suffix, compare, order, record)                       \
    void helper_vcmp##suffix(CPUPPCState *env, ppc_avr_t *r,            \
                             ppc_avr_t *a, ppc_avr_t *b)                \
    {                                                                   \
        uint32_t ones = (uint32_t)-1;                                   \
        uint32_t all = ones;                                            \
        uint32_t none = 0;                                              \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->f32); i++) {                      \
            uint32_t result;                                            \
            FloatRelation rel =                                         \
                float32_compare_quiet(a->f32[i], b->f32[i],             \
                                      &env->vec_status);                \
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

    for (i = 0; i < ARRAY_SIZE(r->f32); i++) {
        FloatRelation le_rel = float32_compare_quiet(a->f32[i], b->f32[i],
                                                     &env->vec_status);
        if (le_rel == float_relation_unordered) {
            r->u32[i] = 0xc0000000;
            all_in = 1;
        } else {
            float32 bneg = float32_chs(b->f32[i]);
            FloatRelation ge_rel = float32_compare_quiet(a->f32[i], bneg,
                                                         &env->vec_status);
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
        for (i = 0; i < ARRAY_SIZE(r->f32); i++) {                      \
            if (float32_is_any_nan(b->f32[i])) {                        \
                r->element[i] = 0;                                      \
            } else {                                                    \
                float64 t = float32_to_float64(b->f32[i], &s);          \
                int64_t j;                                              \
                                                                        \
                t = float64_scalbn(t, uim, &s);                         \
                j = float64_to_int64(t, &s);                            \
                r->element[i] = satcvt(j, &sat);                        \
            }                                                           \
        }                                                               \
        if (sat) {                                                      \
            set_vscr_sat(env);                                          \
        }                                                               \
    }
VCT(uxs, cvtsduw, u32)
VCT(sxs, cvtsdsw, s32)
#undef VCT

target_ulong helper_vclzlsbb(ppc_avr_t *r)
{
    target_ulong count = 0;
    int i;
    for (i = 0; i < ARRAY_SIZE(r->u8); i++) {
        if (r->VsrB(i) & 0x01) {
            break;
        }
        count++;
    }
    return count;
}

target_ulong helper_vctzlsbb(ppc_avr_t *r)
{
    target_ulong count = 0;
    int i;
    for (i = ARRAY_SIZE(r->u8) - 1; i >= 0; i--) {
        if (r->VsrB(i) & 0x01) {
            break;
        }
        count++;
    }
    return count;
}

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
        set_vscr_sat(env);
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
        set_vscr_sat(env);
    }
}

void helper_vmladduhm(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, ppc_avr_t *c)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(r->s16); i++) {
        int32_t prod = a->s16[i] * b->s16[i];
        r->s16[i] = (int16_t) (prod + c->s16[i]);
    }
}

#define VMRG_DO(name, element, access, ofs)                                  \
    void helper_v##name(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)            \
    {                                                                        \
        ppc_avr_t result;                                                    \
        int i, half = ARRAY_SIZE(r->element) / 2;                            \
                                                                             \
        for (i = 0; i < half; i++) {                                         \
            result.access(i * 2 + 0) = a->access(i + ofs);                   \
            result.access(i * 2 + 1) = b->access(i + ofs);                   \
        }                                                                    \
        *r = result;                                                         \
    }

#define VMRG(suffix, element, access)          \
    VMRG_DO(mrgl##suffix, element, access, half)   \
    VMRG_DO(mrgh##suffix, element, access, 0)
VMRG(b, u8, VsrB)
VMRG(h, u16, VsrH)
VMRG(w, u32, VsrW)
#undef VMRG_DO
#undef VMRG

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
        set_vscr_sat(env);
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
        set_vscr_sat(env);
    }
}

#define VMUL_DO_EVN(name, mul_element, mul_access, prod_access, cast)   \
    void helper_V##name(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)       \
    {                                                                   \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->mul_element); i += 2) {           \
            r->prod_access(i >> 1) = (cast)a->mul_access(i) *           \
                                     (cast)b->mul_access(i);            \
        }                                                               \
    }

#define VMUL_DO_ODD(name, mul_element, mul_access, prod_access, cast)   \
    void helper_V##name(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)       \
    {                                                                   \
        int i;                                                          \
                                                                        \
        for (i = 0; i < ARRAY_SIZE(r->mul_element); i += 2) {           \
            r->prod_access(i >> 1) = (cast)a->mul_access(i + 1) *       \
                                     (cast)b->mul_access(i + 1);        \
        }                                                               \
    }

#define VMUL(suffix, mul_element, mul_access, prod_access, cast)       \
    VMUL_DO_EVN(MULE##suffix, mul_element, mul_access, prod_access, cast)  \
    VMUL_DO_ODD(MULO##suffix, mul_element, mul_access, prod_access, cast)
VMUL(SB, s8, VsrSB, VsrSH, int16_t)
VMUL(SH, s16, VsrSH, VsrSW, int32_t)
VMUL(SW, s32, VsrSW, VsrSD, int64_t)
VMUL(UB, u8, VsrB, VsrH, uint16_t)
VMUL(UH, u16, VsrH, VsrW, uint32_t)
VMUL(UW, u32, VsrW, VsrD, uint64_t)
#undef VMUL_DO_EVN
#undef VMUL_DO_ODD
#undef VMUL

void helper_XXPERMX(ppc_vsr_t *t, ppc_vsr_t *s0, ppc_vsr_t *s1, ppc_vsr_t *pcv,
                    target_ulong uim)
{
    int i, idx;
    ppc_vsr_t tmp = { .u64 = {0, 0} };

    for (i = 0; i < ARRAY_SIZE(t->u8); i++) {
        if ((pcv->VsrB(i) >> 5) == uim) {
            idx = pcv->VsrB(i) & 0x1f;
            if (idx < ARRAY_SIZE(t->u8)) {
                tmp.VsrB(i) = s0->VsrB(idx);
            } else {
                tmp.VsrB(i) = s1->VsrB(idx - ARRAY_SIZE(t->u8));
            }
        }
    }

    *t = tmp;
}

void helper_VPERM(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, ppc_avr_t *c)
{
    ppc_avr_t result;
    int i;

    for (i = 0; i < ARRAY_SIZE(r->u8); i++) {
        int s = c->VsrB(i) & 0x1f;
        int index = s & 0xf;

        if (s & 0x10) {
            result.VsrB(i) = b->VsrB(index);
        } else {
            result.VsrB(i) = a->VsrB(index);
        }
    }
    *r = result;
}

void helper_VPERMR(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, ppc_avr_t *c)
{
    ppc_avr_t result;
    int i;

    for (i = 0; i < ARRAY_SIZE(r->u8); i++) {
        int s = c->VsrB(i) & 0x1f;
        int index = 15 - (s & 0xf);

        if (s & 0x10) {
            result.VsrB(i) = a->VsrB(index);
        } else {
            result.VsrB(i) = b->VsrB(index);
        }
    }
    *r = result;
}

#define XXGENPCV_BE_EXP(NAME, SZ) \
void glue(helper_, glue(NAME, _be_exp))(ppc_vsr_t *t, ppc_vsr_t *b) \
{                                                                   \
    ppc_vsr_t tmp;                                                  \
                                                                    \
    /* Initialize tmp with the result of an all-zeros mask */       \
    tmp.VsrD(0) = 0x1011121314151617;                               \
    tmp.VsrD(1) = 0x18191A1B1C1D1E1F;                               \
                                                                    \
    /* Iterate over the most significant byte of each element */    \
    for (int i = 0, j = 0; i < ARRAY_SIZE(b->u8); i += SZ) {        \
        if (b->VsrB(i) & 0x80) {                                    \
            /* Update each byte of the element */                   \
            for (int k = 0; k < SZ; k++) {                          \
                tmp.VsrB(i + k) = j + k;                            \
            }                                                       \
            j += SZ;                                                \
        }                                                           \
    }                                                               \
                                                                    \
    *t = tmp;                                                       \
}

#define XXGENPCV_BE_COMP(NAME, SZ) \
void glue(helper_, glue(NAME, _be_comp))(ppc_vsr_t *t, ppc_vsr_t *b)\
{                                                                   \
    ppc_vsr_t tmp = { .u64 = { 0, 0 } };                            \
                                                                    \
    /* Iterate over the most significant byte of each element */    \
    for (int i = 0, j = 0; i < ARRAY_SIZE(b->u8); i += SZ) {        \
        if (b->VsrB(i) & 0x80) {                                    \
            /* Update each byte of the element */                   \
            for (int k = 0; k < SZ; k++) {                          \
                tmp.VsrB(j + k) = i + k;                            \
            }                                                       \
            j += SZ;                                                \
        }                                                           \
    }                                                               \
                                                                    \
    *t = tmp;                                                       \
}

#define XXGENPCV_LE_EXP(NAME, SZ) \
void glue(helper_, glue(NAME, _le_exp))(ppc_vsr_t *t, ppc_vsr_t *b) \
{                                                                   \
    ppc_vsr_t tmp;                                                  \
                                                                    \
    /* Initialize tmp with the result of an all-zeros mask */       \
    tmp.VsrD(0) = 0x1F1E1D1C1B1A1918;                               \
    tmp.VsrD(1) = 0x1716151413121110;                               \
                                                                    \
    /* Iterate over the most significant byte of each element */    \
    for (int i = 0, j = 0; i < ARRAY_SIZE(b->u8); i += SZ) {        \
        /* Reverse indexing of "i" */                               \
        const int idx = ARRAY_SIZE(b->u8) - i - SZ;                 \
        if (b->VsrB(idx) & 0x80) {                                  \
            /* Update each byte of the element */                   \
            for (int k = 0, rk = SZ - 1; k < SZ; k++, rk--) {       \
                tmp.VsrB(idx + rk) = j + k;                         \
            }                                                       \
            j += SZ;                                                \
        }                                                           \
    }                                                               \
                                                                    \
    *t = tmp;                                                       \
}

#define XXGENPCV_LE_COMP(NAME, SZ) \
void glue(helper_, glue(NAME, _le_comp))(ppc_vsr_t *t, ppc_vsr_t *b)\
{                                                                   \
    ppc_vsr_t tmp = { .u64 = { 0, 0 } };                            \
                                                                    \
    /* Iterate over the most significant byte of each element */    \
    for (int i = 0, j = 0; i < ARRAY_SIZE(b->u8); i += SZ) {        \
        if (b->VsrB(ARRAY_SIZE(b->u8) - i - SZ) & 0x80) {           \
            /* Update each byte of the element */                   \
            for (int k = 0, rk = SZ - 1; k < SZ; k++, rk--) {       \
                /* Reverse indexing of "j" */                       \
                const int idx = ARRAY_SIZE(b->u8) - j - SZ;         \
                tmp.VsrB(idx + rk) = i + k;                         \
            }                                                       \
            j += SZ;                                                \
        }                                                           \
    }                                                               \
                                                                    \
    *t = tmp;                                                       \
}

#define XXGENPCV(NAME, SZ) \
    XXGENPCV_BE_EXP(NAME, SZ)  \
    XXGENPCV_BE_COMP(NAME, SZ) \
    XXGENPCV_LE_EXP(NAME, SZ)  \
    XXGENPCV_LE_COMP(NAME, SZ) \

XXGENPCV(XXGENPCVBM, 1)
XXGENPCV(XXGENPCVHM, 2)
XXGENPCV(XXGENPCVWM, 4)
XXGENPCV(XXGENPCVDM, 8)

#undef XXGENPCV_BE_EXP
#undef XXGENPCV_BE_COMP
#undef XXGENPCV_LE_EXP
#undef XXGENPCV_LE_COMP
#undef XXGENPCV

#if defined(HOST_WORDS_BIGENDIAN)
#define VBPERMQ_INDEX(avr, i) ((avr)->u8[(i)])
#define VBPERMD_INDEX(i) (i)
#define VBPERMQ_DW(index) (((index) & 0x40) != 0)
#define EXTRACT_BIT(avr, i, index) (extract64((avr)->u64[i], index, 1))
#else
#define VBPERMQ_INDEX(avr, i) ((avr)->u8[15 - (i)])
#define VBPERMD_INDEX(i) (1 - i)
#define VBPERMQ_DW(index) (((index) & 0x40) == 0)
#define EXTRACT_BIT(avr, i, index) \
        (extract64((avr)->u64[1 - i], 63 - index, 1))
#endif

void helper_vbpermd(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int i, j;
    ppc_avr_t result = { .u64 = { 0, 0 } };
    VECTOR_FOR_INORDER_I(i, u64) {
        for (j = 0; j < 8; j++) {
            int index = VBPERMQ_INDEX(b, (i * 8) + j);
            if (index < 64 && EXTRACT_BIT(a, i, index)) {
                result.u64[VBPERMD_INDEX(i)] |= (0x80 >> j);
            }
        }
    }
    *r = result;
}

void helper_vbpermq(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int i;
    uint64_t perm = 0;

    VECTOR_FOR_INORDER_I(i, u8) {
        int index = VBPERMQ_INDEX(b, i);

        if (index < 128) {
            uint64_t mask = (1ull << (63 - (index & 0x3F)));
            if (a->u64[VBPERMQ_DW(index)] & mask) {
                perm |= (0x8000 >> i);
            }
        }
    }

    r->VsrD(0) = perm;
    r->VsrD(1) = 0;
}

#undef VBPERMQ_INDEX
#undef VBPERMQ_DW

#define PMSUM(name, srcfld, trgfld, trgtyp)                   \
void helper_##name(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)  \
{                                                             \
    int i, j;                                                 \
    trgtyp prod[sizeof(ppc_avr_t) / sizeof(a->srcfld[0])];    \
                                                              \
    VECTOR_FOR_INORDER_I(i, srcfld) {                         \
        prod[i] = 0;                                          \
        for (j = 0; j < sizeof(a->srcfld[0]) * 8; j++) {      \
            if (a->srcfld[i] & (1ull << j)) {                 \
                prod[i] ^= ((trgtyp)b->srcfld[i] << j);       \
            }                                                 \
        }                                                     \
    }                                                         \
                                                              \
    VECTOR_FOR_INORDER_I(i, trgfld) {                         \
        r->trgfld[i] = prod[2 * i] ^ prod[2 * i + 1];         \
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
            if (a->u64[i] & (1ull << j)) {
                prod[i] ^= (((__uint128_t)b->u64[i]) << j);
            }
        }
    }

    r->u128 = prod[0] ^ prod[1];

#else
    int i, j;
    ppc_avr_t prod[2];

    VECTOR_FOR_INORDER_I(i, u64) {
        prod[i].VsrD(1) = prod[i].VsrD(0) = 0;
        for (j = 0; j < 64; j++) {
            if (a->u64[i] & (1ull << j)) {
                ppc_avr_t bshift;
                if (j == 0) {
                    bshift.VsrD(0) = 0;
                    bshift.VsrD(1) = b->u64[i];
                } else {
                    bshift.VsrD(0) = b->u64[i] >> (64 - j);
                    bshift.VsrD(1) = b->u64[i] << j;
                }
                prod[i].VsrD(1) ^= bshift.VsrD(1);
                prod[i].VsrD(0) ^= bshift.VsrD(0);
            }
        }
    }

    r->VsrD(1) = prod[0].VsrD(1) ^ prod[1].VsrD(1);
    r->VsrD(0) = prod[0].VsrD(0) ^ prod[1].VsrD(0);
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

            result.u16[4 * i + j] = (((e >> 9) & 0xfc00) |
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
            result.to[i + ARRAY_SIZE(r->from)] = cvt(a1->from[i], &sat);\
        }                                                               \
        *r = result;                                                    \
        if (dosat && sat) {                                             \
            set_vscr_sat(env);                                          \
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

    for (i = 0; i < ARRAY_SIZE(r->f32); i++) {
        r->f32[i] = float32_div(float32_one, b->f32[i], &env->vec_status);
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
        for (i = 0; i < ARRAY_SIZE(r->f32); i++) {              \
            r->f32[i] = float32_round_to_int (b->f32[i], &s);   \
        }                                                       \
    }
VRFI(n, float_round_nearest_even)
VRFI(m, float_round_down)
VRFI(p, float_round_up)
VRFI(z, float_round_to_zero)
#undef VRFI

void helper_vrsqrtefp(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *b)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(r->f32); i++) {
        float32 t = float32_sqrt(b->f32[i], &env->vec_status);

        r->f32[i] = float32_div(float32_one, t, &env->vec_status);
    }
}

#define VRLMI(name, size, element, insert)                                  \
void helper_##name(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, uint32_t desc) \
{                                                                           \
    int i;                                                                  \
    for (i = 0; i < ARRAY_SIZE(r->element); i++) {                          \
        uint##size##_t src1 = a->element[i];                                \
        uint##size##_t src2 = b->element[i];                                \
        uint##size##_t src3 = r->element[i];                                \
        uint##size##_t begin, end, shift, mask, rot_val;                    \
                                                                            \
        shift = extract##size(src2, 0, 6);                                  \
        end   = extract##size(src2, 8, 6);                                  \
        begin = extract##size(src2, 16, 6);                                 \
        rot_val = rol##size(src1, shift);                                   \
        mask = mask_u##size(begin, end);                                    \
        if (insert) {                                                       \
            r->element[i] = (rot_val & mask) | (src3 & ~mask);              \
        } else {                                                            \
            r->element[i] = (rot_val & mask);                               \
        }                                                                   \
    }                                                                       \
}

VRLMI(VRLDMI, 64, u64, 1);
VRLMI(VRLWMI, 32, u32, 1);
VRLMI(VRLDNM, 64, u64, 0);
VRLMI(VRLWNM, 32, u32, 0);

void helper_vexptefp(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *b)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(r->f32); i++) {
        r->f32[i] = float32_exp2(b->f32[i], &env->vec_status);
    }
}

void helper_vlogefp(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *b)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(r->f32); i++) {
        r->f32[i] = float32_log2(b->f32[i], &env->vec_status);
    }
}

#define VEXTU_X_DO(name, size, left)                            \
target_ulong glue(helper_, name)(target_ulong a, ppc_avr_t *b)  \
{                                                               \
    int index = (a & 0xf) * 8;                                  \
    if (left) {                                                 \
        index = 128 - index - size;                             \
    }                                                           \
    return int128_getlo(int128_rshift(b->s128, index)) &        \
        MAKE_64BIT_MASK(0, size);                               \
}
VEXTU_X_DO(vextublx,  8, 1)
VEXTU_X_DO(vextuhlx, 16, 1)
VEXTU_X_DO(vextuwlx, 32, 1)
VEXTU_X_DO(vextubrx,  8, 0)
VEXTU_X_DO(vextuhrx, 16, 0)
VEXTU_X_DO(vextuwrx, 32, 0)
#undef VEXTU_X_DO

void helper_vslv(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int i;
    unsigned int shift, bytes, size;

    size = ARRAY_SIZE(r->u8);
    for (i = 0; i < size; i++) {
        shift = b->VsrB(i) & 0x7;             /* extract shift value */
        bytes = (a->VsrB(i) << 8) +           /* extract adjacent bytes */
            (((i + 1) < size) ? a->VsrB(i + 1) : 0);
        r->VsrB(i) = (bytes << shift) >> 8;   /* shift and store result */
    }
}

void helper_vsrv(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int i;
    unsigned int shift, bytes;

    /*
     * Use reverse order, as destination and source register can be
     * same. Its being modified in place saving temporary, reverse
     * order will guarantee that computed result is not fed back.
     */
    for (i = ARRAY_SIZE(r->u8) - 1; i >= 0; i--) {
        shift = b->VsrB(i) & 0x7;               /* extract shift value */
        bytes = ((i ? a->VsrB(i - 1) : 0) << 8) + a->VsrB(i);
                                                /* extract adjacent bytes */
        r->VsrB(i) = (bytes >> shift) & 0xFF;   /* shift and store result */
    }
}

void helper_vsldoi(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, uint32_t shift)
{
    int sh = shift & 0xf;
    int i;
    ppc_avr_t result;

    for (i = 0; i < ARRAY_SIZE(r->u8); i++) {
        int index = sh + i;
        if (index > 0xf) {
            result.VsrB(i) = b->VsrB(index - 0x10);
        } else {
            result.VsrB(i) = a->VsrB(index);
        }
    }
    *r = result;
}

void helper_vslo(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int sh = (b->VsrB(0xf) >> 3) & 0xf;

#if defined(HOST_WORDS_BIGENDIAN)
    memmove(&r->u8[0], &a->u8[sh], 16 - sh);
    memset(&r->u8[16 - sh], 0, sh);
#else
    memmove(&r->u8[sh], &a->u8[0], 16 - sh);
    memset(&r->u8[0], 0, sh);
#endif
}

#if defined(HOST_WORDS_BIGENDIAN)
#define ELEM_ADDR(VEC, IDX, SIZE) (&(VEC)->u8[IDX])
#else
#define ELEM_ADDR(VEC, IDX, SIZE) (&(VEC)->u8[15 - (IDX)] - (SIZE) + 1)
#endif

#define VINSX(SUFFIX, TYPE) \
void glue(glue(helper_VINS, SUFFIX), LX)(CPUPPCState *env, ppc_avr_t *t,       \
                                         uint64_t val, target_ulong index)     \
{                                                                              \
    const int maxidx = ARRAY_SIZE(t->u8) - sizeof(TYPE);                       \
    target_long idx = index;                                                   \
                                                                               \
    if (idx < 0 || idx > maxidx) {                                             \
        idx =  idx < 0 ? sizeof(TYPE) - idx : idx;                             \
        qemu_log_mask(LOG_GUEST_ERROR,                                         \
            "Invalid index for Vector Insert Element after 0x" TARGET_FMT_lx   \
            ", RA = " TARGET_FMT_ld " > %d\n", env->nip, idx, maxidx);         \
    } else {                                                                   \
        TYPE src = val;                                                        \
        memcpy(ELEM_ADDR(t, idx, sizeof(TYPE)), &src, sizeof(TYPE));           \
    }                                                                          \
}
VINSX(B, uint8_t)
VINSX(H, uint16_t)
VINSX(W, uint32_t)
VINSX(D, uint64_t)
#undef ELEM_ADDR
#undef VINSX
#if defined(HOST_WORDS_BIGENDIAN)
#define VEXTDVLX(NAME, SIZE) \
void helper_##NAME(CPUPPCState *env, ppc_avr_t *t, ppc_avr_t *a, ppc_avr_t *b, \
                   target_ulong index)                                         \
{                                                                              \
    const target_long idx = index;                                             \
    ppc_avr_t tmp[2] = { *a, *b };                                             \
    memset(t, 0, sizeof(*t));                                                  \
    if (idx >= 0 && idx + SIZE <= sizeof(tmp)) {                               \
        memcpy(&t->u8[ARRAY_SIZE(t->u8) / 2 - SIZE], (void *)tmp + idx, SIZE); \
    } else {                                                                   \
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid index for " #NAME " after 0x"  \
                      TARGET_FMT_lx ", RC = " TARGET_FMT_ld " > %d\n",         \
                      env->nip, idx < 0 ? SIZE - idx : idx, 32 - SIZE);        \
    }                                                                          \
}
#else
#define VEXTDVLX(NAME, SIZE) \
void helper_##NAME(CPUPPCState *env, ppc_avr_t *t, ppc_avr_t *a, ppc_avr_t *b, \
                   target_ulong index)                                         \
{                                                                              \
    const target_long idx = index;                                             \
    ppc_avr_t tmp[2] = { *b, *a };                                             \
    memset(t, 0, sizeof(*t));                                                  \
    if (idx >= 0 && idx + SIZE <= sizeof(tmp)) {                               \
        memcpy(&t->u8[ARRAY_SIZE(t->u8) / 2],                                  \
               (void *)tmp + sizeof(tmp) - SIZE - idx, SIZE);                  \
    } else {                                                                   \
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid index for " #NAME " after 0x"  \
                      TARGET_FMT_lx ", RC = " TARGET_FMT_ld " > %d\n",         \
                      env->nip, idx < 0 ? SIZE - idx : idx, 32 - SIZE);        \
    }                                                                          \
}
#endif
VEXTDVLX(VEXTDUBVLX, 1)
VEXTDVLX(VEXTDUHVLX, 2)
VEXTDVLX(VEXTDUWVLX, 4)
VEXTDVLX(VEXTDDVLX, 8)
#undef VEXTDVLX
#if defined(HOST_WORDS_BIGENDIAN)
#define VEXTRACT(suffix, element)                                            \
    void helper_vextract##suffix(ppc_avr_t *r, ppc_avr_t *b, uint32_t index) \
    {                                                                        \
        uint32_t es = sizeof(r->element[0]);                                 \
        memmove(&r->u8[8 - es], &b->u8[index], es);                          \
        memset(&r->u8[8], 0, 8);                                             \
        memset(&r->u8[0], 0, 8 - es);                                        \
    }
#else
#define VEXTRACT(suffix, element)                                            \
    void helper_vextract##suffix(ppc_avr_t *r, ppc_avr_t *b, uint32_t index) \
    {                                                                        \
        uint32_t es = sizeof(r->element[0]);                                 \
        uint32_t s = (16 - index) - es;                                      \
        memmove(&r->u8[8], &b->u8[s], es);                                   \
        memset(&r->u8[0], 0, 8);                                             \
        memset(&r->u8[8 + es], 0, 8 - es);                                   \
    }
#endif
VEXTRACT(ub, u8)
VEXTRACT(uh, u16)
VEXTRACT(uw, u32)
VEXTRACT(d, u64)
#undef VEXTRACT

#define VSTRI(NAME, ELEM, NUM_ELEMS, LEFT) \
uint32_t helper_##NAME(ppc_avr_t *t, ppc_avr_t *b) \
{                                                   \
    int i, idx, crf = 0;                            \
                                                    \
    for (i = 0; i < NUM_ELEMS; i++) {               \
        idx = LEFT ? i : NUM_ELEMS - i - 1;         \
        if (b->Vsr##ELEM(idx)) {                    \
            t->Vsr##ELEM(idx) = b->Vsr##ELEM(idx);  \
        } else {                                    \
            crf = 0b0010;                           \
            break;                                  \
        }                                           \
    }                                               \
                                                    \
    for (; i < NUM_ELEMS; i++) {                    \
        idx = LEFT ? i : NUM_ELEMS - i - 1;         \
        t->Vsr##ELEM(idx) = 0;                      \
    }                                               \
                                                    \
    return crf;                                     \
}
VSTRI(VSTRIBL, B, 16, true)
VSTRI(VSTRIBR, B, 16, false)
VSTRI(VSTRIHL, H, 8, true)
VSTRI(VSTRIHR, H, 8, false)
#undef VSTRI

void helper_xxextractuw(CPUPPCState *env, ppc_vsr_t *xt,
                        ppc_vsr_t *xb, uint32_t index)
{
    ppc_vsr_t t = { };
    size_t es = sizeof(uint32_t);
    uint32_t ext_index;
    int i;

    ext_index = index;
    for (i = 0; i < es; i++, ext_index++) {
        t.VsrB(8 - es + i) = xb->VsrB(ext_index % 16);
    }

    *xt = t;
}

void helper_xxinsertw(CPUPPCState *env, ppc_vsr_t *xt,
                      ppc_vsr_t *xb, uint32_t index)
{
    ppc_vsr_t t = *xt;
    size_t es = sizeof(uint32_t);
    int ins_index, i = 0;

    ins_index = index;
    for (i = 0; i < es && ins_index < 16; i++, ins_index++) {
        t.VsrB(ins_index) = xb->VsrB(8 - es + i);
    }

    *xt = t;
}

void helper_XXEVAL(ppc_avr_t *t, ppc_avr_t *a, ppc_avr_t *b, ppc_avr_t *c,
                   uint32_t desc)
{
    /*
     * Instead of processing imm bit-by-bit, we'll skip the computation of
     * conjunctions whose corresponding bit is unset.
     */
    int bit, imm = simd_data(desc);
    Int128 conj, disj = int128_zero();

    /* Iterate over set bits from the least to the most significant bit */
    while (imm) {
        /*
         * Get the next bit to be processed with ctz64. Invert the result of
         * ctz64 to match the indexing used by PowerISA.
         */
        bit = 7 - ctzl(imm);
        if (bit & 0x4) {
            conj = a->s128;
        } else {
            conj = int128_not(a->s128);
        }
        if (bit & 0x2) {
            conj = int128_and(conj, b->s128);
        } else {
            conj = int128_and(conj, int128_not(b->s128));
        }
        if (bit & 0x1) {
            conj = int128_and(conj, c->s128);
        } else {
            conj = int128_and(conj, int128_not(c->s128));
        }
        disj = int128_or(disj, conj);

        /* Unset the least significant bit that is set */
        imm &= imm - 1;
    }

    t->s128 = disj;
}

#define XXBLEND(name, sz) \
void glue(helper_XXBLENDV, name)(ppc_avr_t *t, ppc_avr_t *a, ppc_avr_t *b,  \
                                 ppc_avr_t *c, uint32_t desc)               \
{                                                                           \
    for (int i = 0; i < ARRAY_SIZE(t->glue(u, sz)); i++) {                  \
        t->glue(u, sz)[i] = (c->glue(s, sz)[i] >> (sz - 1)) ?               \
            b->glue(u, sz)[i] : a->glue(u, sz)[i];                          \
    }                                                                       \
}
XXBLEND(B, 8)
XXBLEND(H, 16)
XXBLEND(W, 32)
XXBLEND(D, 64)
#undef XXBLEND

#define VNEG(name, element)                                         \
void helper_##name(ppc_avr_t *r, ppc_avr_t *b)                      \
{                                                                   \
    int i;                                                          \
    for (i = 0; i < ARRAY_SIZE(r->element); i++) {                  \
        r->element[i] = -b->element[i];                             \
    }                                                               \
}
VNEG(vnegw, s32)
VNEG(vnegd, s64)
#undef VNEG

void helper_vsro(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int sh = (b->VsrB(0xf) >> 3) & 0xf;

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

    upper = ARRAY_SIZE(r->s32) - 1;
    t = (int64_t)b->VsrSW(upper);
    for (i = 0; i < ARRAY_SIZE(r->s32); i++) {
        t += a->VsrSW(i);
        result.VsrSW(i) = 0;
    }
    result.VsrSW(upper) = cvtsdsw(t, &sat);
    *r = result;

    if (sat) {
        set_vscr_sat(env);
    }
}

void helper_vsum2sws(CPUPPCState *env, ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    int i, j, upper;
    ppc_avr_t result;
    int sat = 0;

    upper = 1;
    for (i = 0; i < ARRAY_SIZE(r->u64); i++) {
        int64_t t = (int64_t)b->VsrSW(upper + i * 2);

        result.VsrD(i) = 0;
        for (j = 0; j < ARRAY_SIZE(r->u64); j++) {
            t += a->VsrSW(2 * i + j);
        }
        result.VsrSW(upper + i * 2) = cvtsdsw(t, &sat);
    }

    *r = result;
    if (sat) {
        set_vscr_sat(env);
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
        set_vscr_sat(env);
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
        set_vscr_sat(env);
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
        set_vscr_sat(env);
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
            uint16_t e = b->u16[hi ? i : i + 4];                        \
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
        for (i = 0; i < ARRAY_SIZE(r->element); i++) {                  \
            r->element[i] = name(b->element[i]);                        \
        }                                                               \
    }

#define clzb(v) ((v) ? clz32((uint32_t)(v) << 24) : 8)
#define clzh(v) ((v) ? clz32((uint32_t)(v) << 16) : 16)

VGENERIC_DO(clzb, u8)
VGENERIC_DO(clzh, u16)

#undef clzb
#undef clzh

#define ctzb(v) ((v) ? ctz32(v) : 8)
#define ctzh(v) ((v) ? ctz32(v) : 16)
#define ctzw(v) ctz32((v))
#define ctzd(v) ctz64((v))

VGENERIC_DO(ctzb, u8)
VGENERIC_DO(ctzh, u16)
VGENERIC_DO(ctzw, u32)
VGENERIC_DO(ctzd, u64)

#undef ctzb
#undef ctzh
#undef ctzw
#undef ctzd

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
    if (a.VsrD(0) < b.VsrD(0)) {
        return -1;
    } else if (a.VsrD(0) > b.VsrD(0)) {
        return 1;
    } else if (a.VsrD(1) < b.VsrD(1)) {
        return -1;
    } else if (a.VsrD(1) > b.VsrD(1)) {
        return 1;
    } else {
        return 0;
    }
}

static void avr_qw_add(ppc_avr_t *t, ppc_avr_t a, ppc_avr_t b)
{
    t->VsrD(1) = a.VsrD(1) + b.VsrD(1);
    t->VsrD(0) = a.VsrD(0) + b.VsrD(0) +
                     (~a.VsrD(1) < b.VsrD(1));
}

static int avr_qw_addc(ppc_avr_t *t, ppc_avr_t a, ppc_avr_t b)
{
    ppc_avr_t not_a;
    t->VsrD(1) = a.VsrD(1) + b.VsrD(1);
    t->VsrD(0) = a.VsrD(0) + b.VsrD(0) +
                     (~a.VsrD(1) < b.VsrD(1));
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

    if (c->VsrD(1) & 1) {
        ppc_avr_t tmp;

        tmp.VsrD(0) = 0;
        tmp.VsrD(1) = c->VsrD(1) & 1;
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

    r->VsrD(0) = 0;
    r->VsrD(1) = (avr_qw_cmpu(not_a, *b) < 0);
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

    int carry_in = c->VsrD(1) & 1;
    int carry_out = 0;
    ppc_avr_t tmp;

    carry_out = avr_qw_addc(&tmp, *a, *b);

    if (!carry_out && carry_in) {
        ppc_avr_t one = QW_ONE;
        carry_out = avr_qw_addc(&tmp, tmp, one);
    }
    r->VsrD(0) = 0;
    r->VsrD(1) = carry_out;
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

    tmp.VsrD(0) = 0;
    tmp.VsrD(1) = c->VsrD(1) & 1;
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
        carry = ((tmp.VsrSD(0) == -1ull) && (tmp.VsrSD(1) == -1ull));
    }
    r->VsrD(0) = 0;
    r->VsrD(1) = carry;
#endif
}

void helper_vsubecuq(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, ppc_avr_t *c)
{
#ifdef CONFIG_INT128
    r->u128 =
        (~a->u128 < ~b->u128) ||
        ((c->u128 & 1) && (a->u128 + ~b->u128 == (__uint128_t)-1));
#else
    int carry_in = c->VsrD(1) & 1;
    int carry_out = (avr_qw_cmpu(*a, *b) > 0);
    if (!carry_out && carry_in) {
        ppc_avr_t tmp;
        avr_qw_not(&tmp, *b);
        avr_qw_add(&tmp, *a, tmp);
        carry_out = ((tmp.VsrD(0) == -1ull) && (tmp.VsrD(1) == -1ull));
    }

    r->VsrD(0) = 0;
    r->VsrD(1) = carry_out;
#endif
}

#define BCD_PLUS_PREF_1 0xC
#define BCD_PLUS_PREF_2 0xF
#define BCD_PLUS_ALT_1  0xA
#define BCD_NEG_PREF    0xD
#define BCD_NEG_ALT     0xB
#define BCD_PLUS_ALT_2  0xE
#define NATIONAL_PLUS   0x2B
#define NATIONAL_NEG    0x2D

#define BCD_DIG_BYTE(n) (15 - ((n) / 2))

static int bcd_get_sgn(ppc_avr_t *bcd)
{
    switch (bcd->VsrB(BCD_DIG_BYTE(0)) & 0xF) {
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
        result = bcd->VsrB(BCD_DIG_BYTE(n)) >> 4;
    } else {
       result = bcd->VsrB(BCD_DIG_BYTE(n)) & 0xF;
    }

    if (unlikely(result > 9)) {
        *invalid = true;
    }
    return result;
}

static void bcd_put_digit(ppc_avr_t *bcd, uint8_t digit, int n)
{
    if (n & 1) {
        bcd->VsrB(BCD_DIG_BYTE(n)) &= 0x0F;
        bcd->VsrB(BCD_DIG_BYTE(n)) |= (digit << 4);
    } else {
        bcd->VsrB(BCD_DIG_BYTE(n)) &= 0xF0;
        bcd->VsrB(BCD_DIG_BYTE(n)) |= digit;
    }
}

static bool bcd_is_valid(ppc_avr_t *bcd)
{
    int i;
    int invalid = 0;

    if (bcd_get_sgn(bcd) == 0) {
        return false;
    }

    for (i = 1; i < 32; i++) {
        bcd_get_digit(bcd, i, &invalid);
        if (unlikely(invalid)) {
            return false;
        }
    }
    return true;
}

static int bcd_cmp_zero(ppc_avr_t *bcd)
{
    if (bcd->VsrD(0) == 0 && (bcd->VsrD(1) >> 4) == 0) {
        return CRF_EQ;
    } else {
        return (bcd_get_sgn(bcd) == 1) ? CRF_GT : CRF_LT;
    }
}

static uint16_t get_national_digit(ppc_avr_t *reg, int n)
{
    return reg->VsrH(7 - n);
}

static void set_national_digit(ppc_avr_t *reg, uint8_t val, int n)
{
    reg->VsrH(7 - n) = val;
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
    }

    *overflow = carry;
    return is_zero;
}

static void bcd_sub_mag(ppc_avr_t *t, ppc_avr_t *a, ppc_avr_t *b, int *invalid,
                       int *overflow)
{
    int carry = 0;
    int i;

    for (i = 1; i <= 31; i++) {
        uint8_t digit = bcd_get_digit(a, i, invalid) -
                        bcd_get_digit(b, i, invalid) + carry;
        if (digit & 0x80) {
            carry = -1;
            digit += 10;
        } else {
            carry = 0;
        }

        bcd_put_digit(t, digit, i);
    }

    *overflow = carry;
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
            result.VsrB(BCD_DIG_BYTE(0)) = bcd_preferred_sgn(sgna, ps);
            zero = bcd_add_mag(&result, a, b, &invalid, &overflow);
            cr = (sgna > 0) ? CRF_GT : CRF_LT;
        } else {
            int magnitude = bcd_cmp_mag(a, b);
            if (magnitude > 0) {
                result.VsrB(BCD_DIG_BYTE(0)) = bcd_preferred_sgn(sgna, ps);
                bcd_sub_mag(&result, a, b, &invalid, &overflow);
                cr = (sgna > 0) ? CRF_GT : CRF_LT;
            } else if (magnitude < 0) {
                result.VsrB(BCD_DIG_BYTE(0)) = bcd_preferred_sgn(sgnb, ps);
                bcd_sub_mag(&result, b, a, &invalid, &overflow);
                cr = (sgnb > 0) ? CRF_GT : CRF_LT;
            } else {
                result.VsrB(BCD_DIG_BYTE(0)) = bcd_preferred_sgn(0, ps);
                cr = CRF_EQ;
            }
        }
    }

    if (unlikely(invalid)) {
        result.VsrD(0) = result.VsrD(1) = -1;
        cr = CRF_SO;
    } else if (overflow) {
        cr |= CRF_SO;
    } else if (zero) {
        cr |= CRF_EQ;
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

uint32_t helper_bcdcfn(ppc_avr_t *r, ppc_avr_t *b, uint32_t ps)
{
    int i;
    int cr = 0;
    uint16_t national = 0;
    uint16_t sgnb = get_national_digit(b, 0);
    ppc_avr_t ret = { .u64 = { 0, 0 } };
    int invalid = (sgnb != NATIONAL_PLUS && sgnb != NATIONAL_NEG);

    for (i = 1; i < 8; i++) {
        national = get_national_digit(b, i);
        if (unlikely(national < 0x30 || national > 0x39)) {
            invalid = 1;
            break;
        }

        bcd_put_digit(&ret, national & 0xf, i);
    }

    if (sgnb == NATIONAL_PLUS) {
        bcd_put_digit(&ret, (ps == 0) ? BCD_PLUS_PREF_1 : BCD_PLUS_PREF_2, 0);
    } else {
        bcd_put_digit(&ret, BCD_NEG_PREF, 0);
    }

    cr = bcd_cmp_zero(&ret);

    if (unlikely(invalid)) {
        cr = CRF_SO;
    }

    *r = ret;

    return cr;
}

uint32_t helper_bcdctn(ppc_avr_t *r, ppc_avr_t *b, uint32_t ps)
{
    int i;
    int cr = 0;
    int sgnb = bcd_get_sgn(b);
    int invalid = (sgnb == 0);
    ppc_avr_t ret = { .u64 = { 0, 0 } };

    int ox_flag = (b->VsrD(0) != 0) || ((b->VsrD(1) >> 32) != 0);

    for (i = 1; i < 8; i++) {
        set_national_digit(&ret, 0x30 + bcd_get_digit(b, i, &invalid), i);

        if (unlikely(invalid)) {
            break;
        }
    }
    set_national_digit(&ret, (sgnb == -1) ? NATIONAL_NEG : NATIONAL_PLUS, 0);

    cr = bcd_cmp_zero(b);

    if (ox_flag) {
        cr |= CRF_SO;
    }

    if (unlikely(invalid)) {
        cr = CRF_SO;
    }

    *r = ret;

    return cr;
}

uint32_t helper_bcdcfz(ppc_avr_t *r, ppc_avr_t *b, uint32_t ps)
{
    int i;
    int cr = 0;
    int invalid = 0;
    int zone_digit = 0;
    int zone_lead = ps ? 0xF : 0x3;
    int digit = 0;
    ppc_avr_t ret = { .u64 = { 0, 0 } };
    int sgnb = b->VsrB(BCD_DIG_BYTE(0)) >> 4;

    if (unlikely((sgnb < 0xA) && ps)) {
        invalid = 1;
    }

    for (i = 0; i < 16; i++) {
        zone_digit = i ? b->VsrB(BCD_DIG_BYTE(i * 2)) >> 4 : zone_lead;
        digit = b->VsrB(BCD_DIG_BYTE(i * 2)) & 0xF;
        if (unlikely(zone_digit != zone_lead || digit > 0x9)) {
            invalid = 1;
            break;
        }

        bcd_put_digit(&ret, digit, i + 1);
    }

    if ((ps && (sgnb == 0xB || sgnb == 0xD)) ||
            (!ps && (sgnb & 0x4))) {
        bcd_put_digit(&ret, BCD_NEG_PREF, 0);
    } else {
        bcd_put_digit(&ret, BCD_PLUS_PREF_1, 0);
    }

    cr = bcd_cmp_zero(&ret);

    if (unlikely(invalid)) {
        cr = CRF_SO;
    }

    *r = ret;

    return cr;
}

uint32_t helper_bcdctz(ppc_avr_t *r, ppc_avr_t *b, uint32_t ps)
{
    int i;
    int cr = 0;
    uint8_t digit = 0;
    int sgnb = bcd_get_sgn(b);
    int zone_lead = (ps) ? 0xF0 : 0x30;
    int invalid = (sgnb == 0);
    ppc_avr_t ret = { .u64 = { 0, 0 } };

    int ox_flag = ((b->VsrD(0) >> 4) != 0);

    for (i = 0; i < 16; i++) {
        digit = bcd_get_digit(b, i + 1, &invalid);

        if (unlikely(invalid)) {
            break;
        }

        ret.VsrB(BCD_DIG_BYTE(i * 2)) = zone_lead + digit;
    }

    if (ps) {
        bcd_put_digit(&ret, (sgnb == 1) ? 0xC : 0xD, 1);
    } else {
        bcd_put_digit(&ret, (sgnb == 1) ? 0x3 : 0x7, 1);
    }

    cr = bcd_cmp_zero(b);

    if (ox_flag) {
        cr |= CRF_SO;
    }

    if (unlikely(invalid)) {
        cr = CRF_SO;
    }

    *r = ret;

    return cr;
}

/**
 * Compare 2 128-bit unsigned integers, passed in as unsigned 64-bit pairs
 *
 * Returns:
 * > 0 if ahi|alo > bhi|blo,
 * 0 if ahi|alo == bhi|blo,
 * < 0 if ahi|alo < bhi|blo
 */
static inline int ucmp128(uint64_t alo, uint64_t ahi,
                          uint64_t blo, uint64_t bhi)
{
    return (ahi == bhi) ?
        (alo > blo ? 1 : (alo == blo ? 0 : -1)) :
        (ahi > bhi ? 1 : -1);
}

uint32_t helper_bcdcfsq(ppc_avr_t *r, ppc_avr_t *b, uint32_t ps)
{
    int i;
    int cr;
    uint64_t lo_value;
    uint64_t hi_value;
    uint64_t rem;
    ppc_avr_t ret = { .u64 = { 0, 0 } };

    if (b->VsrSD(0) < 0) {
        lo_value = -b->VsrSD(1);
        hi_value = ~b->VsrD(0) + !lo_value;
        bcd_put_digit(&ret, 0xD, 0);

        cr = CRF_LT;
    } else {
        lo_value = b->VsrD(1);
        hi_value = b->VsrD(0);
        bcd_put_digit(&ret, bcd_preferred_sgn(0, ps), 0);

        if (hi_value == 0 && lo_value == 0) {
            cr = CRF_EQ;
        } else {
            cr = CRF_GT;
        }
    }

    /*
     * Check src limits: abs(src) <= 10^31 - 1
     *
     * 10^31 - 1 = 0x0000007e37be2022 c0914b267fffffff
     */
    if (ucmp128(lo_value, hi_value,
                0xc0914b267fffffffULL, 0x7e37be2022ULL) > 0) {
        cr |= CRF_SO;

        /*
         * According to the ISA, if src wouldn't fit in the destination
         * register, the result is undefined.
         * In that case, we leave r unchanged.
         */
    } else {
        rem = divu128(&lo_value, &hi_value, 1000000000000000ULL);

        for (i = 1; i < 16; rem /= 10, i++) {
            bcd_put_digit(&ret, rem % 10, i);
        }

        for (; i < 32; lo_value /= 10, i++) {
            bcd_put_digit(&ret, lo_value % 10, i);
        }

        *r = ret;
    }

    return cr;
}

uint32_t helper_bcdctsq(ppc_avr_t *r, ppc_avr_t *b, uint32_t ps)
{
    uint8_t i;
    int cr;
    uint64_t carry;
    uint64_t unused;
    uint64_t lo_value;
    uint64_t hi_value = 0;
    int sgnb = bcd_get_sgn(b);
    int invalid = (sgnb == 0);

    lo_value = bcd_get_digit(b, 31, &invalid);
    for (i = 30; i > 0; i--) {
        mulu64(&lo_value, &carry, lo_value, 10ULL);
        mulu64(&hi_value, &unused, hi_value, 10ULL);
        lo_value += bcd_get_digit(b, i, &invalid);
        hi_value += carry;

        if (unlikely(invalid)) {
            break;
        }
    }

    if (sgnb == -1) {
        r->VsrSD(1) = -lo_value;
        r->VsrSD(0) = ~hi_value + !r->VsrSD(1);
    } else {
        r->VsrSD(1) = lo_value;
        r->VsrSD(0) = hi_value;
    }

    cr = bcd_cmp_zero(b);

    if (unlikely(invalid)) {
        cr = CRF_SO;
    }

    return cr;
}

uint32_t helper_bcdcpsgn(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, uint32_t ps)
{
    int i;
    int invalid = 0;

    if (bcd_get_sgn(a) == 0 || bcd_get_sgn(b) == 0) {
        return CRF_SO;
    }

    *r = *a;
    bcd_put_digit(r, b->VsrB(BCD_DIG_BYTE(0)) & 0xF, 0);

    for (i = 1; i < 32; i++) {
        bcd_get_digit(a, i, &invalid);
        bcd_get_digit(b, i, &invalid);
        if (unlikely(invalid)) {
            return CRF_SO;
        }
    }

    return bcd_cmp_zero(r);
}

uint32_t helper_bcdsetsgn(ppc_avr_t *r, ppc_avr_t *b, uint32_t ps)
{
    int sgnb = bcd_get_sgn(b);

    *r = *b;
    bcd_put_digit(r, bcd_preferred_sgn(sgnb, ps), 0);

    if (bcd_is_valid(b) == false) {
        return CRF_SO;
    }

    return bcd_cmp_zero(r);
}

uint32_t helper_bcds(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, uint32_t ps)
{
    int cr;
    int i = a->VsrSB(7);
    bool ox_flag = false;
    int sgnb = bcd_get_sgn(b);
    ppc_avr_t ret = *b;
    ret.VsrD(1) &= ~0xf;

    if (bcd_is_valid(b) == false) {
        return CRF_SO;
    }

    if (unlikely(i > 31)) {
        i = 31;
    } else if (unlikely(i < -31)) {
        i = -31;
    }

    if (i > 0) {
        ulshift(&ret.VsrD(1), &ret.VsrD(0), i * 4, &ox_flag);
    } else {
        urshift(&ret.VsrD(1), &ret.VsrD(0), -i * 4);
    }
    bcd_put_digit(&ret, bcd_preferred_sgn(sgnb, ps), 0);

    *r = ret;

    cr = bcd_cmp_zero(r);
    if (ox_flag) {
        cr |= CRF_SO;
    }

    return cr;
}

uint32_t helper_bcdus(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, uint32_t ps)
{
    int cr;
    int i;
    int invalid = 0;
    bool ox_flag = false;
    ppc_avr_t ret = *b;

    for (i = 0; i < 32; i++) {
        bcd_get_digit(b, i, &invalid);

        if (unlikely(invalid)) {
            return CRF_SO;
        }
    }

    i = a->VsrSB(7);
    if (i >= 32) {
        ox_flag = true;
        ret.VsrD(1) = ret.VsrD(0) = 0;
    } else if (i <= -32) {
        ret.VsrD(1) = ret.VsrD(0) = 0;
    } else if (i > 0) {
        ulshift(&ret.VsrD(1), &ret.VsrD(0), i * 4, &ox_flag);
    } else {
        urshift(&ret.VsrD(1), &ret.VsrD(0), -i * 4);
    }
    *r = ret;

    cr = bcd_cmp_zero(r);
    if (ox_flag) {
        cr |= CRF_SO;
    }

    return cr;
}

uint32_t helper_bcdsr(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, uint32_t ps)
{
    int cr;
    int unused = 0;
    int invalid = 0;
    bool ox_flag = false;
    int sgnb = bcd_get_sgn(b);
    ppc_avr_t ret = *b;
    ret.VsrD(1) &= ~0xf;

    int i = a->VsrSB(7);
    ppc_avr_t bcd_one;

    bcd_one.VsrD(0) = 0;
    bcd_one.VsrD(1) = 0x10;

    if (bcd_is_valid(b) == false) {
        return CRF_SO;
    }

    if (unlikely(i > 31)) {
        i = 31;
    } else if (unlikely(i < -31)) {
        i = -31;
    }

    if (i > 0) {
        ulshift(&ret.VsrD(1), &ret.VsrD(0), i * 4, &ox_flag);
    } else {
        urshift(&ret.VsrD(1), &ret.VsrD(0), -i * 4);

        if (bcd_get_digit(&ret, 0, &invalid) >= 5) {
            bcd_add_mag(&ret, &ret, &bcd_one, &invalid, &unused);
        }
    }
    bcd_put_digit(&ret, bcd_preferred_sgn(sgnb, ps), 0);

    cr = bcd_cmp_zero(&ret);
    if (ox_flag) {
        cr |= CRF_SO;
    }
    *r = ret;

    return cr;
}

uint32_t helper_bcdtrunc(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, uint32_t ps)
{
    uint64_t mask;
    uint32_t ox_flag = 0;
    int i = a->VsrSH(3) + 1;
    ppc_avr_t ret = *b;

    if (bcd_is_valid(b) == false) {
        return CRF_SO;
    }

    if (i > 16 && i < 32) {
        mask = (uint64_t)-1 >> (128 - i * 4);
        if (ret.VsrD(0) & ~mask) {
            ox_flag = CRF_SO;
        }

        ret.VsrD(0) &= mask;
    } else if (i >= 0 && i <= 16) {
        mask = (uint64_t)-1 >> (64 - i * 4);
        if (ret.VsrD(0) || (ret.VsrD(1) & ~mask)) {
            ox_flag = CRF_SO;
        }

        ret.VsrD(1) &= mask;
        ret.VsrD(0) = 0;
    }
    bcd_put_digit(&ret, bcd_preferred_sgn(bcd_get_sgn(b), ps), 0);
    *r = ret;

    return bcd_cmp_zero(&ret) | ox_flag;
}

uint32_t helper_bcdutrunc(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b, uint32_t ps)
{
    int i;
    uint64_t mask;
    uint32_t ox_flag = 0;
    int invalid = 0;
    ppc_avr_t ret = *b;

    for (i = 0; i < 32; i++) {
        bcd_get_digit(b, i, &invalid);

        if (unlikely(invalid)) {
            return CRF_SO;
        }
    }

    i = a->VsrSH(3);
    if (i > 16 && i < 33) {
        mask = (uint64_t)-1 >> (128 - i * 4);
        if (ret.VsrD(0) & ~mask) {
            ox_flag = CRF_SO;
        }

        ret.VsrD(0) &= mask;
    } else if (i > 0 && i <= 16) {
        mask = (uint64_t)-1 >> (64 - i * 4);
        if (ret.VsrD(0) || (ret.VsrD(1) & ~mask)) {
            ox_flag = CRF_SO;
        }

        ret.VsrD(1) &= mask;
        ret.VsrD(0) = 0;
    } else if (i == 0) {
        if (ret.VsrD(0) || ret.VsrD(1)) {
            ox_flag = CRF_SO;
        }
        ret.VsrD(0) = ret.VsrD(1) = 0;
    }

    *r = ret;
    if (r->VsrD(0) == 0 && r->VsrD(1) == 0) {
        return ox_flag | CRF_EQ;
    }

    return ox_flag | CRF_GT;
}

void helper_vsbox(ppc_avr_t *r, ppc_avr_t *a)
{
    int i;
    VECTOR_FOR_INORDER_I(i, u8) {
        r->u8[i] = AES_sbox[a->u8[i]];
    }
}

void helper_vcipher(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    ppc_avr_t result;
    int i;

    VECTOR_FOR_INORDER_I(i, u32) {
        result.VsrW(i) = b->VsrW(i) ^
            (AES_Te0[a->VsrB(AES_shifts[4 * i + 0])] ^
             AES_Te1[a->VsrB(AES_shifts[4 * i + 1])] ^
             AES_Te2[a->VsrB(AES_shifts[4 * i + 2])] ^
             AES_Te3[a->VsrB(AES_shifts[4 * i + 3])]);
    }
    *r = result;
}

void helper_vcipherlast(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    ppc_avr_t result;
    int i;

    VECTOR_FOR_INORDER_I(i, u8) {
        result.VsrB(i) = b->VsrB(i) ^ (AES_sbox[a->VsrB(AES_shifts[i])]);
    }
    *r = result;
}

void helper_vncipher(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    /* This differs from what is written in ISA V2.07.  The RTL is */
    /* incorrect and will be fixed in V2.07B.                      */
    int i;
    ppc_avr_t tmp;

    VECTOR_FOR_INORDER_I(i, u8) {
        tmp.VsrB(i) = b->VsrB(i) ^ AES_isbox[a->VsrB(AES_ishifts[i])];
    }

    VECTOR_FOR_INORDER_I(i, u32) {
        r->VsrW(i) =
            AES_imc[tmp.VsrB(4 * i + 0)][0] ^
            AES_imc[tmp.VsrB(4 * i + 1)][1] ^
            AES_imc[tmp.VsrB(4 * i + 2)][2] ^
            AES_imc[tmp.VsrB(4 * i + 3)][3];
    }
}

void helper_vncipherlast(ppc_avr_t *r, ppc_avr_t *a, ppc_avr_t *b)
{
    ppc_avr_t result;
    int i;

    VECTOR_FOR_INORDER_I(i, u8) {
        result.VsrB(i) = b->VsrB(i) ^ (AES_isbox[a->VsrB(AES_ishifts[i])]);
    }
    *r = result;
}

void helper_vshasigmaw(ppc_avr_t *r,  ppc_avr_t *a, uint32_t st_six)
{
    int st = (st_six & 0x10) != 0;
    int six = st_six & 0xF;
    int i;

    for (i = 0; i < ARRAY_SIZE(r->u32); i++) {
        if (st == 0) {
            if ((six & (0x8 >> i)) == 0) {
                r->VsrW(i) = ror32(a->VsrW(i), 7) ^
                             ror32(a->VsrW(i), 18) ^
                             (a->VsrW(i) >> 3);
            } else { /* six.bit[i] == 1 */
                r->VsrW(i) = ror32(a->VsrW(i), 17) ^
                             ror32(a->VsrW(i), 19) ^
                             (a->VsrW(i) >> 10);
            }
        } else { /* st == 1 */
            if ((six & (0x8 >> i)) == 0) {
                r->VsrW(i) = ror32(a->VsrW(i), 2) ^
                             ror32(a->VsrW(i), 13) ^
                             ror32(a->VsrW(i), 22);
            } else { /* six.bit[i] == 1 */
                r->VsrW(i) = ror32(a->VsrW(i), 6) ^
                             ror32(a->VsrW(i), 11) ^
                             ror32(a->VsrW(i), 25);
            }
        }
    }
}

void helper_vshasigmad(ppc_avr_t *r,  ppc_avr_t *a, uint32_t st_six)
{
    int st = (st_six & 0x10) != 0;
    int six = st_six & 0xF;
    int i;

    for (i = 0; i < ARRAY_SIZE(r->u64); i++) {
        if (st == 0) {
            if ((six & (0x8 >> (2 * i))) == 0) {
                r->VsrD(i) = ror64(a->VsrD(i), 1) ^
                             ror64(a->VsrD(i), 8) ^
                             (a->VsrD(i) >> 7);
            } else { /* six.bit[2*i] == 1 */
                r->VsrD(i) = ror64(a->VsrD(i), 19) ^
                             ror64(a->VsrD(i), 61) ^
                             (a->VsrD(i) >> 6);
            }
        } else { /* st == 1 */
            if ((six & (0x8 >> (2 * i))) == 0) {
                r->VsrD(i) = ror64(a->VsrD(i), 28) ^
                             ror64(a->VsrD(i), 34) ^
                             ror64(a->VsrD(i), 39);
            } else { /* six.bit[2*i] == 1 */
                r->VsrD(i) = ror64(a->VsrD(i), 14) ^
                             ror64(a->VsrD(i), 18) ^
                             ror64(a->VsrD(i), 41);
            }
        }
    }
}

void helper_vpermxor(ppc_avr_t *r,  ppc_avr_t *a, ppc_avr_t *b, ppc_avr_t *c)
{
    ppc_avr_t result;
    int i;

    for (i = 0; i < ARRAY_SIZE(r->u8); i++) {
        int indexA = c->VsrB(i) >> 4;
        int indexB = c->VsrB(i) & 0xF;

        result.VsrB(i) = a->VsrB(indexA) ^ b->VsrB(indexB);
    }
    *r = result;
}

#undef VECTOR_FOR_INORDER_I

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
    i = 8;
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
