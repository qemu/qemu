/*
 *  AArch64 specific helpers
 *
 *  Copyright (c) 2013 Alexander Graf <agraf@suse.de>
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/gdbstub.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"
#include "qemu/log.h"
#include "sysemu/sysemu.h"
#include "qemu/bitops.h"
#include "internals.h"
#include "qemu/crc32c.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "qemu/int128.h"
#include "qemu/atomic128.h"
#include "tcg.h"
#include "fpu/softfloat.h"
#include <zlib.h> /* For crc32 */

/* C2.4.7 Multiply and divide */
/* special cases for 0 and LLONG_MIN are mandated by the standard */
uint64_t HELPER(udiv64)(uint64_t num, uint64_t den)
{
    if (den == 0) {
        return 0;
    }
    return num / den;
}

int64_t HELPER(sdiv64)(int64_t num, int64_t den)
{
    if (den == 0) {
        return 0;
    }
    if (num == LLONG_MIN && den == -1) {
        return LLONG_MIN;
    }
    return num / den;
}

uint64_t HELPER(rbit64)(uint64_t x)
{
    return revbit64(x);
}

/* Convert a softfloat float_relation_ (as returned by
 * the float*_compare functions) to the correct ARM
 * NZCV flag state.
 */
static inline uint32_t float_rel_to_flags(int res)
{
    uint64_t flags;
    switch (res) {
    case float_relation_equal:
        flags = PSTATE_Z | PSTATE_C;
        break;
    case float_relation_less:
        flags = PSTATE_N;
        break;
    case float_relation_greater:
        flags = PSTATE_C;
        break;
    case float_relation_unordered:
    default:
        flags = PSTATE_C | PSTATE_V;
        break;
    }
    return flags;
}

uint64_t HELPER(vfp_cmph_a64)(uint32_t x, uint32_t y, void *fp_status)
{
    return float_rel_to_flags(float16_compare_quiet(x, y, fp_status));
}

uint64_t HELPER(vfp_cmpeh_a64)(uint32_t x, uint32_t y, void *fp_status)
{
    return float_rel_to_flags(float16_compare(x, y, fp_status));
}

uint64_t HELPER(vfp_cmps_a64)(float32 x, float32 y, void *fp_status)
{
    return float_rel_to_flags(float32_compare_quiet(x, y, fp_status));
}

uint64_t HELPER(vfp_cmpes_a64)(float32 x, float32 y, void *fp_status)
{
    return float_rel_to_flags(float32_compare(x, y, fp_status));
}

uint64_t HELPER(vfp_cmpd_a64)(float64 x, float64 y, void *fp_status)
{
    return float_rel_to_flags(float64_compare_quiet(x, y, fp_status));
}

uint64_t HELPER(vfp_cmped_a64)(float64 x, float64 y, void *fp_status)
{
    return float_rel_to_flags(float64_compare(x, y, fp_status));
}

float32 HELPER(vfp_mulxs)(float32 a, float32 b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float32_squash_input_denormal(a, fpst);
    b = float32_squash_input_denormal(b, fpst);

    if ((float32_is_zero(a) && float32_is_infinity(b)) ||
        (float32_is_infinity(a) && float32_is_zero(b))) {
        /* 2.0 with the sign bit set to sign(A) XOR sign(B) */
        return make_float32((1U << 30) |
                            ((float32_val(a) ^ float32_val(b)) & (1U << 31)));
    }
    return float32_mul(a, b, fpst);
}

float64 HELPER(vfp_mulxd)(float64 a, float64 b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float64_squash_input_denormal(a, fpst);
    b = float64_squash_input_denormal(b, fpst);

    if ((float64_is_zero(a) && float64_is_infinity(b)) ||
        (float64_is_infinity(a) && float64_is_zero(b))) {
        /* 2.0 with the sign bit set to sign(A) XOR sign(B) */
        return make_float64((1ULL << 62) |
                            ((float64_val(a) ^ float64_val(b)) & (1ULL << 63)));
    }
    return float64_mul(a, b, fpst);
}

uint64_t HELPER(simd_tbl)(CPUARMState *env, uint64_t result, uint64_t indices,
                          uint32_t rn, uint32_t numregs)
{
    /* Helper function for SIMD TBL and TBX. We have to do the table
     * lookup part for the 64 bits worth of indices we're passed in.
     * result is the initial results vector (either zeroes for TBL
     * or some guest values for TBX), rn the register number where
     * the table starts, and numregs the number of registers in the table.
     * We return the results of the lookups.
     */
    int shift;

    for (shift = 0; shift < 64; shift += 8) {
        int index = extract64(indices, shift, 8);
        if (index < 16 * numregs) {
            /* Convert index (a byte offset into the virtual table
             * which is a series of 128-bit vectors concatenated)
             * into the correct register element plus a bit offset
             * into that element, bearing in mind that the table
             * can wrap around from V31 to V0.
             */
            int elt = (rn * 2 + (index >> 3)) % 64;
            int bitidx = (index & 7) * 8;
            uint64_t *q = aa64_vfp_qreg(env, elt >> 1);
            uint64_t val = extract64(q[elt & 1], bitidx, 8);

            result = deposit64(result, shift, 8, val);
        }
    }
    return result;
}

/* 64bit/double versions of the neon float compare functions */
uint64_t HELPER(neon_ceq_f64)(float64 a, float64 b, void *fpstp)
{
    float_status *fpst = fpstp;
    return -float64_eq_quiet(a, b, fpst);
}

uint64_t HELPER(neon_cge_f64)(float64 a, float64 b, void *fpstp)
{
    float_status *fpst = fpstp;
    return -float64_le(b, a, fpst);
}

uint64_t HELPER(neon_cgt_f64)(float64 a, float64 b, void *fpstp)
{
    float_status *fpst = fpstp;
    return -float64_lt(b, a, fpst);
}

/* Reciprocal step and sqrt step. Note that unlike the A32/T32
 * versions, these do a fully fused multiply-add or
 * multiply-add-and-halve.
 */
#define float16_two make_float16(0x4000)
#define float16_three make_float16(0x4200)
#define float16_one_point_five make_float16(0x3e00)

#define float32_two make_float32(0x40000000)
#define float32_three make_float32(0x40400000)
#define float32_one_point_five make_float32(0x3fc00000)

#define float64_two make_float64(0x4000000000000000ULL)
#define float64_three make_float64(0x4008000000000000ULL)
#define float64_one_point_five make_float64(0x3FF8000000000000ULL)

uint32_t HELPER(recpsf_f16)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float16_squash_input_denormal(a, fpst);
    b = float16_squash_input_denormal(b, fpst);

    a = float16_chs(a);
    if ((float16_is_infinity(a) && float16_is_zero(b)) ||
        (float16_is_infinity(b) && float16_is_zero(a))) {
        return float16_two;
    }
    return float16_muladd(a, b, float16_two, 0, fpst);
}

float32 HELPER(recpsf_f32)(float32 a, float32 b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float32_squash_input_denormal(a, fpst);
    b = float32_squash_input_denormal(b, fpst);

    a = float32_chs(a);
    if ((float32_is_infinity(a) && float32_is_zero(b)) ||
        (float32_is_infinity(b) && float32_is_zero(a))) {
        return float32_two;
    }
    return float32_muladd(a, b, float32_two, 0, fpst);
}

float64 HELPER(recpsf_f64)(float64 a, float64 b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float64_squash_input_denormal(a, fpst);
    b = float64_squash_input_denormal(b, fpst);

    a = float64_chs(a);
    if ((float64_is_infinity(a) && float64_is_zero(b)) ||
        (float64_is_infinity(b) && float64_is_zero(a))) {
        return float64_two;
    }
    return float64_muladd(a, b, float64_two, 0, fpst);
}

uint32_t HELPER(rsqrtsf_f16)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float16_squash_input_denormal(a, fpst);
    b = float16_squash_input_denormal(b, fpst);

    a = float16_chs(a);
    if ((float16_is_infinity(a) && float16_is_zero(b)) ||
        (float16_is_infinity(b) && float16_is_zero(a))) {
        return float16_one_point_five;
    }
    return float16_muladd(a, b, float16_three, float_muladd_halve_result, fpst);
}

float32 HELPER(rsqrtsf_f32)(float32 a, float32 b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float32_squash_input_denormal(a, fpst);
    b = float32_squash_input_denormal(b, fpst);

    a = float32_chs(a);
    if ((float32_is_infinity(a) && float32_is_zero(b)) ||
        (float32_is_infinity(b) && float32_is_zero(a))) {
        return float32_one_point_five;
    }
    return float32_muladd(a, b, float32_three, float_muladd_halve_result, fpst);
}

float64 HELPER(rsqrtsf_f64)(float64 a, float64 b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float64_squash_input_denormal(a, fpst);
    b = float64_squash_input_denormal(b, fpst);

    a = float64_chs(a);
    if ((float64_is_infinity(a) && float64_is_zero(b)) ||
        (float64_is_infinity(b) && float64_is_zero(a))) {
        return float64_one_point_five;
    }
    return float64_muladd(a, b, float64_three, float_muladd_halve_result, fpst);
}

/* Pairwise long add: add pairs of adjacent elements into
 * double-width elements in the result (eg _s8 is an 8x8->16 op)
 */
uint64_t HELPER(neon_addlp_s8)(uint64_t a)
{
    uint64_t nsignmask = 0x0080008000800080ULL;
    uint64_t wsignmask = 0x8000800080008000ULL;
    uint64_t elementmask = 0x00ff00ff00ff00ffULL;
    uint64_t tmp1, tmp2;
    uint64_t res, signres;

    /* Extract odd elements, sign extend each to a 16 bit field */
    tmp1 = a & elementmask;
    tmp1 ^= nsignmask;
    tmp1 |= wsignmask;
    tmp1 = (tmp1 - nsignmask) ^ wsignmask;
    /* Ditto for the even elements */
    tmp2 = (a >> 8) & elementmask;
    tmp2 ^= nsignmask;
    tmp2 |= wsignmask;
    tmp2 = (tmp2 - nsignmask) ^ wsignmask;

    /* calculate the result by summing bits 0..14, 16..22, etc,
     * and then adjusting the sign bits 15, 23, etc manually.
     * This ensures the addition can't overflow the 16 bit field.
     */
    signres = (tmp1 ^ tmp2) & wsignmask;
    res = (tmp1 & ~wsignmask) + (tmp2 & ~wsignmask);
    res ^= signres;

    return res;
}

uint64_t HELPER(neon_addlp_u8)(uint64_t a)
{
    uint64_t tmp;

    tmp = a & 0x00ff00ff00ff00ffULL;
    tmp += (a >> 8) & 0x00ff00ff00ff00ffULL;
    return tmp;
}

uint64_t HELPER(neon_addlp_s16)(uint64_t a)
{
    int32_t reslo, reshi;

    reslo = (int32_t)(int16_t)a + (int32_t)(int16_t)(a >> 16);
    reshi = (int32_t)(int16_t)(a >> 32) + (int32_t)(int16_t)(a >> 48);

    return (uint32_t)reslo | (((uint64_t)reshi) << 32);
}

uint64_t HELPER(neon_addlp_u16)(uint64_t a)
{
    uint64_t tmp;

    tmp = a & 0x0000ffff0000ffffULL;
    tmp += (a >> 16) & 0x0000ffff0000ffffULL;
    return tmp;
}

/* Floating-point reciprocal exponent - see FPRecpX in ARM ARM */
uint32_t HELPER(frecpx_f16)(uint32_t a, void *fpstp)
{
    float_status *fpst = fpstp;
    uint16_t val16, sbit;
    int16_t exp;

    if (float16_is_any_nan(a)) {
        float16 nan = a;
        if (float16_is_signaling_nan(a, fpst)) {
            float_raise(float_flag_invalid, fpst);
            nan = float16_silence_nan(a, fpst);
        }
        if (fpst->default_nan_mode) {
            nan = float16_default_nan(fpst);
        }
        return nan;
    }

    a = float16_squash_input_denormal(a, fpst);

    val16 = float16_val(a);
    sbit = 0x8000 & val16;
    exp = extract32(val16, 10, 5);

    if (exp == 0) {
        return make_float16(deposit32(sbit, 10, 5, 0x1e));
    } else {
        return make_float16(deposit32(sbit, 10, 5, ~exp));
    }
}

float32 HELPER(frecpx_f32)(float32 a, void *fpstp)
{
    float_status *fpst = fpstp;
    uint32_t val32, sbit;
    int32_t exp;

    if (float32_is_any_nan(a)) {
        float32 nan = a;
        if (float32_is_signaling_nan(a, fpst)) {
            float_raise(float_flag_invalid, fpst);
            nan = float32_silence_nan(a, fpst);
        }
        if (fpst->default_nan_mode) {
            nan = float32_default_nan(fpst);
        }
        return nan;
    }

    a = float32_squash_input_denormal(a, fpst);

    val32 = float32_val(a);
    sbit = 0x80000000ULL & val32;
    exp = extract32(val32, 23, 8);

    if (exp == 0) {
        return make_float32(sbit | (0xfe << 23));
    } else {
        return make_float32(sbit | (~exp & 0xff) << 23);
    }
}

float64 HELPER(frecpx_f64)(float64 a, void *fpstp)
{
    float_status *fpst = fpstp;
    uint64_t val64, sbit;
    int64_t exp;

    if (float64_is_any_nan(a)) {
        float64 nan = a;
        if (float64_is_signaling_nan(a, fpst)) {
            float_raise(float_flag_invalid, fpst);
            nan = float64_silence_nan(a, fpst);
        }
        if (fpst->default_nan_mode) {
            nan = float64_default_nan(fpst);
        }
        return nan;
    }

    a = float64_squash_input_denormal(a, fpst);

    val64 = float64_val(a);
    sbit = 0x8000000000000000ULL & val64;
    exp = extract64(float64_val(a), 52, 11);

    if (exp == 0) {
        return make_float64(sbit | (0x7feULL << 52));
    } else {
        return make_float64(sbit | (~exp & 0x7ffULL) << 52);
    }
}

float32 HELPER(fcvtx_f64_to_f32)(float64 a, CPUARMState *env)
{
    /* Von Neumann rounding is implemented by using round-to-zero
     * and then setting the LSB of the result if Inexact was raised.
     */
    float32 r;
    float_status *fpst = &env->vfp.fp_status;
    float_status tstat = *fpst;
    int exflags;

    set_float_rounding_mode(float_round_to_zero, &tstat);
    set_float_exception_flags(0, &tstat);
    r = float64_to_float32(a, &tstat);
    exflags = get_float_exception_flags(&tstat);
    if (exflags & float_flag_inexact) {
        r = make_float32(float32_val(r) | 1);
    }
    exflags |= get_float_exception_flags(fpst);
    set_float_exception_flags(exflags, fpst);
    return r;
}

/* 64-bit versions of the CRC helpers. Note that although the operation
 * (and the prototypes of crc32c() and crc32() mean that only the bottom
 * 32 bits of the accumulator and result are used, we pass and return
 * uint64_t for convenience of the generated code. Unlike the 32-bit
 * instruction set versions, val may genuinely have 64 bits of data in it.
 * The upper bytes of val (above the number specified by 'bytes') must have
 * been zeroed out by the caller.
 */
uint64_t HELPER(crc32_64)(uint64_t acc, uint64_t val, uint32_t bytes)
{
    uint8_t buf[8];

    stq_le_p(buf, val);

    /* zlib crc32 converts the accumulator and output to one's complement.  */
    return crc32(acc ^ 0xffffffff, buf, bytes) ^ 0xffffffff;
}

uint64_t HELPER(crc32c_64)(uint64_t acc, uint64_t val, uint32_t bytes)
{
    uint8_t buf[8];

    stq_le_p(buf, val);

    /* Linux crc32c converts the output to one's complement.  */
    return crc32c(acc, buf, bytes) ^ 0xffffffff;
}

uint64_t HELPER(paired_cmpxchg64_le)(CPUARMState *env, uint64_t addr,
                                     uint64_t new_lo, uint64_t new_hi)
{
    Int128 cmpv = int128_make128(env->exclusive_val, env->exclusive_high);
    Int128 newv = int128_make128(new_lo, new_hi);
    Int128 oldv;
    uintptr_t ra = GETPC();
    uint64_t o0, o1;
    bool success;

#ifdef CONFIG_USER_ONLY
    /* ??? Enforce alignment.  */
    uint64_t *haddr = g2h(addr);

    helper_retaddr = ra;
    o0 = ldq_le_p(haddr + 0);
    o1 = ldq_le_p(haddr + 1);
    oldv = int128_make128(o0, o1);

    success = int128_eq(oldv, cmpv);
    if (success) {
        stq_le_p(haddr + 0, int128_getlo(newv));
        stq_le_p(haddr + 1, int128_gethi(newv));
    }
    helper_retaddr = 0;
#else
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOpIdx oi0 = make_memop_idx(MO_LEQ | MO_ALIGN_16, mem_idx);
    TCGMemOpIdx oi1 = make_memop_idx(MO_LEQ, mem_idx);

    o0 = helper_le_ldq_mmu(env, addr + 0, oi0, ra);
    o1 = helper_le_ldq_mmu(env, addr + 8, oi1, ra);
    oldv = int128_make128(o0, o1);

    success = int128_eq(oldv, cmpv);
    if (success) {
        helper_le_stq_mmu(env, addr + 0, int128_getlo(newv), oi1, ra);
        helper_le_stq_mmu(env, addr + 8, int128_gethi(newv), oi1, ra);
    }
#endif

    return !success;
}

uint64_t HELPER(paired_cmpxchg64_le_parallel)(CPUARMState *env, uint64_t addr,
                                              uint64_t new_lo, uint64_t new_hi)
{
    Int128 oldv, cmpv, newv;
    uintptr_t ra = GETPC();
    bool success;
    int mem_idx;
    TCGMemOpIdx oi;

    assert(HAVE_CMPXCHG128);

    mem_idx = cpu_mmu_index(env, false);
    oi = make_memop_idx(MO_LEQ | MO_ALIGN_16, mem_idx);

    cmpv = int128_make128(env->exclusive_val, env->exclusive_high);
    newv = int128_make128(new_lo, new_hi);
    oldv = helper_atomic_cmpxchgo_le_mmu(env, addr, cmpv, newv, oi, ra);

    success = int128_eq(oldv, cmpv);
    return !success;
}

uint64_t HELPER(paired_cmpxchg64_be)(CPUARMState *env, uint64_t addr,
                                     uint64_t new_lo, uint64_t new_hi)
{
    /*
     * High and low need to be switched here because this is not actually a
     * 128bit store but two doublewords stored consecutively
     */
    Int128 cmpv = int128_make128(env->exclusive_val, env->exclusive_high);
    Int128 newv = int128_make128(new_lo, new_hi);
    Int128 oldv;
    uintptr_t ra = GETPC();
    uint64_t o0, o1;
    bool success;

#ifdef CONFIG_USER_ONLY
    /* ??? Enforce alignment.  */
    uint64_t *haddr = g2h(addr);

    helper_retaddr = ra;
    o1 = ldq_be_p(haddr + 0);
    o0 = ldq_be_p(haddr + 1);
    oldv = int128_make128(o0, o1);

    success = int128_eq(oldv, cmpv);
    if (success) {
        stq_be_p(haddr + 0, int128_gethi(newv));
        stq_be_p(haddr + 1, int128_getlo(newv));
    }
    helper_retaddr = 0;
#else
    int mem_idx = cpu_mmu_index(env, false);
    TCGMemOpIdx oi0 = make_memop_idx(MO_BEQ | MO_ALIGN_16, mem_idx);
    TCGMemOpIdx oi1 = make_memop_idx(MO_BEQ, mem_idx);

    o1 = helper_be_ldq_mmu(env, addr + 0, oi0, ra);
    o0 = helper_be_ldq_mmu(env, addr + 8, oi1, ra);
    oldv = int128_make128(o0, o1);

    success = int128_eq(oldv, cmpv);
    if (success) {
        helper_be_stq_mmu(env, addr + 0, int128_gethi(newv), oi1, ra);
        helper_be_stq_mmu(env, addr + 8, int128_getlo(newv), oi1, ra);
    }
#endif

    return !success;
}

uint64_t HELPER(paired_cmpxchg64_be_parallel)(CPUARMState *env, uint64_t addr,
                                              uint64_t new_lo, uint64_t new_hi)
{
    Int128 oldv, cmpv, newv;
    uintptr_t ra = GETPC();
    bool success;
    int mem_idx;
    TCGMemOpIdx oi;

    assert(HAVE_CMPXCHG128);

    mem_idx = cpu_mmu_index(env, false);
    oi = make_memop_idx(MO_BEQ | MO_ALIGN_16, mem_idx);

    /*
     * High and low need to be switched here because this is not actually a
     * 128bit store but two doublewords stored consecutively
     */
    cmpv = int128_make128(env->exclusive_high, env->exclusive_val);
    newv = int128_make128(new_hi, new_lo);
    oldv = helper_atomic_cmpxchgo_be_mmu(env, addr, cmpv, newv, oi, ra);

    success = int128_eq(oldv, cmpv);
    return !success;
}

/* Writes back the old data into Rs.  */
void HELPER(casp_le_parallel)(CPUARMState *env, uint32_t rs, uint64_t addr,
                              uint64_t new_lo, uint64_t new_hi)
{
    Int128 oldv, cmpv, newv;
    uintptr_t ra = GETPC();
    int mem_idx;
    TCGMemOpIdx oi;

    assert(HAVE_CMPXCHG128);

    mem_idx = cpu_mmu_index(env, false);
    oi = make_memop_idx(MO_LEQ | MO_ALIGN_16, mem_idx);

    cmpv = int128_make128(env->xregs[rs], env->xregs[rs + 1]);
    newv = int128_make128(new_lo, new_hi);
    oldv = helper_atomic_cmpxchgo_le_mmu(env, addr, cmpv, newv, oi, ra);

    env->xregs[rs] = int128_getlo(oldv);
    env->xregs[rs + 1] = int128_gethi(oldv);
}

void HELPER(casp_be_parallel)(CPUARMState *env, uint32_t rs, uint64_t addr,
                              uint64_t new_hi, uint64_t new_lo)
{
    Int128 oldv, cmpv, newv;
    uintptr_t ra = GETPC();
    int mem_idx;
    TCGMemOpIdx oi;

    assert(HAVE_CMPXCHG128);

    mem_idx = cpu_mmu_index(env, false);
    oi = make_memop_idx(MO_LEQ | MO_ALIGN_16, mem_idx);

    cmpv = int128_make128(env->xregs[rs + 1], env->xregs[rs]);
    newv = int128_make128(new_lo, new_hi);
    oldv = helper_atomic_cmpxchgo_be_mmu(env, addr, cmpv, newv, oi, ra);

    env->xregs[rs + 1] = int128_getlo(oldv);
    env->xregs[rs] = int128_gethi(oldv);
}

/*
 * AdvSIMD half-precision
 */

#define ADVSIMD_HELPER(name, suffix) HELPER(glue(glue(advsimd_, name), suffix))

#define ADVSIMD_HALFOP(name) \
uint32_t ADVSIMD_HELPER(name, h)(uint32_t a, uint32_t b, void *fpstp) \
{ \
    float_status *fpst = fpstp; \
    return float16_ ## name(a, b, fpst);    \
}

ADVSIMD_HALFOP(add)
ADVSIMD_HALFOP(sub)
ADVSIMD_HALFOP(mul)
ADVSIMD_HALFOP(div)
ADVSIMD_HALFOP(min)
ADVSIMD_HALFOP(max)
ADVSIMD_HALFOP(minnum)
ADVSIMD_HALFOP(maxnum)

#define ADVSIMD_TWOHALFOP(name)                                         \
uint32_t ADVSIMD_HELPER(name, 2h)(uint32_t two_a, uint32_t two_b, void *fpstp) \
{ \
    float16  a1, a2, b1, b2;                        \
    uint32_t r1, r2;                                \
    float_status *fpst = fpstp;                     \
    a1 = extract32(two_a, 0, 16);                   \
    a2 = extract32(two_a, 16, 16);                  \
    b1 = extract32(two_b, 0, 16);                   \
    b2 = extract32(two_b, 16, 16);                  \
    r1 = float16_ ## name(a1, b1, fpst);            \
    r2 = float16_ ## name(a2, b2, fpst);            \
    return deposit32(r1, 16, 16, r2);               \
}

ADVSIMD_TWOHALFOP(add)
ADVSIMD_TWOHALFOP(sub)
ADVSIMD_TWOHALFOP(mul)
ADVSIMD_TWOHALFOP(div)
ADVSIMD_TWOHALFOP(min)
ADVSIMD_TWOHALFOP(max)
ADVSIMD_TWOHALFOP(minnum)
ADVSIMD_TWOHALFOP(maxnum)

/* Data processing - scalar floating-point and advanced SIMD */
static float16 float16_mulx(float16 a, float16 b, void *fpstp)
{
    float_status *fpst = fpstp;

    a = float16_squash_input_denormal(a, fpst);
    b = float16_squash_input_denormal(b, fpst);

    if ((float16_is_zero(a) && float16_is_infinity(b)) ||
        (float16_is_infinity(a) && float16_is_zero(b))) {
        /* 2.0 with the sign bit set to sign(A) XOR sign(B) */
        return make_float16((1U << 14) |
                            ((float16_val(a) ^ float16_val(b)) & (1U << 15)));
    }
    return float16_mul(a, b, fpst);
}

ADVSIMD_HALFOP(mulx)
ADVSIMD_TWOHALFOP(mulx)

/* fused multiply-accumulate */
uint32_t HELPER(advsimd_muladdh)(uint32_t a, uint32_t b, uint32_t c,
                                 void *fpstp)
{
    float_status *fpst = fpstp;
    return float16_muladd(a, b, c, 0, fpst);
}

uint32_t HELPER(advsimd_muladd2h)(uint32_t two_a, uint32_t two_b,
                                  uint32_t two_c, void *fpstp)
{
    float_status *fpst = fpstp;
    float16  a1, a2, b1, b2, c1, c2;
    uint32_t r1, r2;
    a1 = extract32(two_a, 0, 16);
    a2 = extract32(two_a, 16, 16);
    b1 = extract32(two_b, 0, 16);
    b2 = extract32(two_b, 16, 16);
    c1 = extract32(two_c, 0, 16);
    c2 = extract32(two_c, 16, 16);
    r1 = float16_muladd(a1, b1, c1, 0, fpst);
    r2 = float16_muladd(a2, b2, c2, 0, fpst);
    return deposit32(r1, 16, 16, r2);
}

/*
 * Floating point comparisons produce an integer result. Softfloat
 * routines return float_relation types which we convert to the 0/-1
 * Neon requires.
 */

#define ADVSIMD_CMPRES(test) (test) ? 0xffff : 0

uint32_t HELPER(advsimd_ceq_f16)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;
    int compare = float16_compare_quiet(a, b, fpst);
    return ADVSIMD_CMPRES(compare == float_relation_equal);
}

uint32_t HELPER(advsimd_cge_f16)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;
    int compare = float16_compare(a, b, fpst);
    return ADVSIMD_CMPRES(compare == float_relation_greater ||
                          compare == float_relation_equal);
}

uint32_t HELPER(advsimd_cgt_f16)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;
    int compare = float16_compare(a, b, fpst);
    return ADVSIMD_CMPRES(compare == float_relation_greater);
}

uint32_t HELPER(advsimd_acge_f16)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;
    float16 f0 = float16_abs(a);
    float16 f1 = float16_abs(b);
    int compare = float16_compare(f0, f1, fpst);
    return ADVSIMD_CMPRES(compare == float_relation_greater ||
                          compare == float_relation_equal);
}

uint32_t HELPER(advsimd_acgt_f16)(uint32_t a, uint32_t b, void *fpstp)
{
    float_status *fpst = fpstp;
    float16 f0 = float16_abs(a);
    float16 f1 = float16_abs(b);
    int compare = float16_compare(f0, f1, fpst);
    return ADVSIMD_CMPRES(compare == float_relation_greater);
}

/* round to integral */
uint32_t HELPER(advsimd_rinth_exact)(uint32_t x, void *fp_status)
{
    return float16_round_to_int(x, fp_status);
}

uint32_t HELPER(advsimd_rinth)(uint32_t x, void *fp_status)
{
    int old_flags = get_float_exception_flags(fp_status), new_flags;
    float16 ret;

    ret = float16_round_to_int(x, fp_status);

    /* Suppress any inexact exceptions the conversion produced */
    if (!(old_flags & float_flag_inexact)) {
        new_flags = get_float_exception_flags(fp_status);
        set_float_exception_flags(new_flags & ~float_flag_inexact, fp_status);
    }

    return ret;
}

/*
 * Half-precision floating point conversion functions
 *
 * There are a multitude of conversion functions with various
 * different rounding modes. This is dealt with by the calling code
 * setting the mode appropriately before calling the helper.
 */

uint32_t HELPER(advsimd_f16tosinth)(uint32_t a, void *fpstp)
{
    float_status *fpst = fpstp;

    /* Invalid if we are passed a NaN */
    if (float16_is_any_nan(a)) {
        float_raise(float_flag_invalid, fpst);
        return 0;
    }
    return float16_to_int16(a, fpst);
}

uint32_t HELPER(advsimd_f16touinth)(uint32_t a, void *fpstp)
{
    float_status *fpst = fpstp;

    /* Invalid if we are passed a NaN */
    if (float16_is_any_nan(a)) {
        float_raise(float_flag_invalid, fpst);
        return 0;
    }
    return float16_to_uint16(a, fpst);
}

/*
 * Square Root and Reciprocal square root
 */

uint32_t HELPER(sqrt_f16)(uint32_t a, void *fpstp)
{
    float_status *s = fpstp;

    return float16_sqrt(a, s);
}


