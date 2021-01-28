/*
 * ARM AdvSIMD / SVE Vector Operations
 *
 * Copyright (c) 2018 Linaro
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
#include "exec/helper-proto.h"
#include "tcg/tcg-gvec-desc.h"
#include "fpu/softfloat.h"
#include "vec_internal.h"

/* Note that vector data is stored in host-endian 64-bit chunks,
   so addressing units smaller than that needs a host-endian fixup.  */
#ifdef HOST_WORDS_BIGENDIAN
#define H1(x)  ((x) ^ 7)
#define H2(x)  ((x) ^ 3)
#define H4(x)  ((x) ^ 1)
#else
#define H1(x)  (x)
#define H2(x)  (x)
#define H4(x)  (x)
#endif

/* Signed saturating rounding doubling multiply-accumulate high half, 16-bit */
static int16_t do_sqrdmlah_h(int16_t src1, int16_t src2, int16_t src3,
                             bool neg, bool round, uint32_t *sat)
{
    /*
     * Simplify:
     * = ((a3 << 16) + ((e1 * e2) << 1) + (1 << 15)) >> 16
     * = ((a3 << 15) + (e1 * e2) + (1 << 14)) >> 15
     */
    int32_t ret = (int32_t)src1 * src2;
    if (neg) {
        ret = -ret;
    }
    ret += ((int32_t)src3 << 15) + (round << 14);
    ret >>= 15;

    if (ret != (int16_t)ret) {
        *sat = 1;
        ret = (ret < 0 ? INT16_MIN : INT16_MAX);
    }
    return ret;
}

uint32_t HELPER(neon_qrdmlah_s16)(CPUARMState *env, uint32_t src1,
                                  uint32_t src2, uint32_t src3)
{
    uint32_t *sat = &env->vfp.qc[0];
    uint16_t e1 = do_sqrdmlah_h(src1, src2, src3, false, true, sat);
    uint16_t e2 = do_sqrdmlah_h(src1 >> 16, src2 >> 16, src3 >> 16,
                                false, true, sat);
    return deposit32(e1, 16, 16, e2);
}

void HELPER(gvec_qrdmlah_s16)(void *vd, void *vn, void *vm,
                              void *vq, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    int16_t *d = vd;
    int16_t *n = vn;
    int16_t *m = vm;
    uintptr_t i;

    for (i = 0; i < opr_sz / 2; ++i) {
        d[i] = do_sqrdmlah_h(n[i], m[i], d[i], false, true, vq);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

uint32_t HELPER(neon_qrdmlsh_s16)(CPUARMState *env, uint32_t src1,
                                  uint32_t src2, uint32_t src3)
{
    uint32_t *sat = &env->vfp.qc[0];
    uint16_t e1 = do_sqrdmlah_h(src1, src2, src3, true, true, sat);
    uint16_t e2 = do_sqrdmlah_h(src1 >> 16, src2 >> 16, src3 >> 16,
                                true, true, sat);
    return deposit32(e1, 16, 16, e2);
}

void HELPER(gvec_qrdmlsh_s16)(void *vd, void *vn, void *vm,
                              void *vq, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    int16_t *d = vd;
    int16_t *n = vn;
    int16_t *m = vm;
    uintptr_t i;

    for (i = 0; i < opr_sz / 2; ++i) {
        d[i] = do_sqrdmlah_h(n[i], m[i], d[i], true, true, vq);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(neon_sqdmulh_h)(void *vd, void *vn, void *vm,
                            void *vq, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int16_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 2; ++i) {
        d[i] = do_sqrdmlah_h(n[i], m[i], 0, false, false, vq);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(neon_sqrdmulh_h)(void *vd, void *vn, void *vm,
                             void *vq, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int16_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 2; ++i) {
        d[i] = do_sqrdmlah_h(n[i], m[i], 0, false, true, vq);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

/* Signed saturating rounding doubling multiply-accumulate high half, 32-bit */
static int32_t do_sqrdmlah_s(int32_t src1, int32_t src2, int32_t src3,
                             bool neg, bool round, uint32_t *sat)
{
    /* Simplify similarly to int_qrdmlah_s16 above.  */
    int64_t ret = (int64_t)src1 * src2;
    if (neg) {
        ret = -ret;
    }
    ret += ((int64_t)src3 << 31) + (round << 30);
    ret >>= 31;

    if (ret != (int32_t)ret) {
        *sat = 1;
        ret = (ret < 0 ? INT32_MIN : INT32_MAX);
    }
    return ret;
}

uint32_t HELPER(neon_qrdmlah_s32)(CPUARMState *env, int32_t src1,
                                  int32_t src2, int32_t src3)
{
    uint32_t *sat = &env->vfp.qc[0];
    return do_sqrdmlah_s(src1, src2, src3, false, true, sat);
}

void HELPER(gvec_qrdmlah_s32)(void *vd, void *vn, void *vm,
                              void *vq, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    int32_t *d = vd;
    int32_t *n = vn;
    int32_t *m = vm;
    uintptr_t i;

    for (i = 0; i < opr_sz / 4; ++i) {
        d[i] = do_sqrdmlah_s(n[i], m[i], d[i], false, true, vq);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

uint32_t HELPER(neon_qrdmlsh_s32)(CPUARMState *env, int32_t src1,
                                  int32_t src2, int32_t src3)
{
    uint32_t *sat = &env->vfp.qc[0];
    return do_sqrdmlah_s(src1, src2, src3, true, true, sat);
}

void HELPER(gvec_qrdmlsh_s32)(void *vd, void *vn, void *vm,
                              void *vq, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    int32_t *d = vd;
    int32_t *n = vn;
    int32_t *m = vm;
    uintptr_t i;

    for (i = 0; i < opr_sz / 4; ++i) {
        d[i] = do_sqrdmlah_s(n[i], m[i], d[i], true, true, vq);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(neon_sqdmulh_s)(void *vd, void *vn, void *vm,
                            void *vq, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int32_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 4; ++i) {
        d[i] = do_sqrdmlah_s(n[i], m[i], 0, false, false, vq);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(neon_sqrdmulh_s)(void *vd, void *vn, void *vm,
                             void *vq, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int32_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 4; ++i) {
        d[i] = do_sqrdmlah_s(n[i], m[i], 0, false, true, vq);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

/* Integer 8 and 16-bit dot-product.
 *
 * Note that for the loops herein, host endianness does not matter
 * with respect to the ordering of data within the 64-bit lanes.
 * All elements are treated equally, no matter where they are.
 */

void HELPER(gvec_sdot_b)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint32_t *d = vd;
    int8_t *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 4; ++i) {
        d[i] += n[i * 4 + 0] * m[i * 4 + 0]
              + n[i * 4 + 1] * m[i * 4 + 1]
              + n[i * 4 + 2] * m[i * 4 + 2]
              + n[i * 4 + 3] * m[i * 4 + 3];
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_udot_b)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint32_t *d = vd;
    uint8_t *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 4; ++i) {
        d[i] += n[i * 4 + 0] * m[i * 4 + 0]
              + n[i * 4 + 1] * m[i * 4 + 1]
              + n[i * 4 + 2] * m[i * 4 + 2]
              + n[i * 4 + 3] * m[i * 4 + 3];
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_sdot_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint64_t *d = vd;
    int16_t *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 8; ++i) {
        d[i] += (int64_t)n[i * 4 + 0] * m[i * 4 + 0]
              + (int64_t)n[i * 4 + 1] * m[i * 4 + 1]
              + (int64_t)n[i * 4 + 2] * m[i * 4 + 2]
              + (int64_t)n[i * 4 + 3] * m[i * 4 + 3];
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_udot_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint64_t *d = vd;
    uint16_t *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 8; ++i) {
        d[i] += (uint64_t)n[i * 4 + 0] * m[i * 4 + 0]
              + (uint64_t)n[i * 4 + 1] * m[i * 4 + 1]
              + (uint64_t)n[i * 4 + 2] * m[i * 4 + 2]
              + (uint64_t)n[i * 4 + 3] * m[i * 4 + 3];
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_sdot_idx_b)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, segend, opr_sz = simd_oprsz(desc), opr_sz_4 = opr_sz / 4;
    intptr_t index = simd_data(desc);
    uint32_t *d = vd;
    int8_t *n = vn;
    int8_t *m_indexed = (int8_t *)vm + H4(index) * 4;

    /* Notice the special case of opr_sz == 8, from aa64/aa32 advsimd.
     * Otherwise opr_sz is a multiple of 16.
     */
    segend = MIN(4, opr_sz_4);
    i = 0;
    do {
        int8_t m0 = m_indexed[i * 4 + 0];
        int8_t m1 = m_indexed[i * 4 + 1];
        int8_t m2 = m_indexed[i * 4 + 2];
        int8_t m3 = m_indexed[i * 4 + 3];

        do {
            d[i] += n[i * 4 + 0] * m0
                  + n[i * 4 + 1] * m1
                  + n[i * 4 + 2] * m2
                  + n[i * 4 + 3] * m3;
        } while (++i < segend);
        segend = i + 4;
    } while (i < opr_sz_4);

    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_udot_idx_b)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, segend, opr_sz = simd_oprsz(desc), opr_sz_4 = opr_sz / 4;
    intptr_t index = simd_data(desc);
    uint32_t *d = vd;
    uint8_t *n = vn;
    uint8_t *m_indexed = (uint8_t *)vm + H4(index) * 4;

    /* Notice the special case of opr_sz == 8, from aa64/aa32 advsimd.
     * Otherwise opr_sz is a multiple of 16.
     */
    segend = MIN(4, opr_sz_4);
    i = 0;
    do {
        uint8_t m0 = m_indexed[i * 4 + 0];
        uint8_t m1 = m_indexed[i * 4 + 1];
        uint8_t m2 = m_indexed[i * 4 + 2];
        uint8_t m3 = m_indexed[i * 4 + 3];

        do {
            d[i] += n[i * 4 + 0] * m0
                  + n[i * 4 + 1] * m1
                  + n[i * 4 + 2] * m2
                  + n[i * 4 + 3] * m3;
        } while (++i < segend);
        segend = i + 4;
    } while (i < opr_sz_4);

    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_sdot_idx_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc), opr_sz_8 = opr_sz / 8;
    intptr_t index = simd_data(desc);
    uint64_t *d = vd;
    int16_t *n = vn;
    int16_t *m_indexed = (int16_t *)vm + index * 4;

    /* This is supported by SVE only, so opr_sz is always a multiple of 16.
     * Process the entire segment all at once, writing back the results
     * only after we've consumed all of the inputs.
     */
    for (i = 0; i < opr_sz_8 ; i += 2) {
        uint64_t d0, d1;

        d0  = n[i * 4 + 0] * (int64_t)m_indexed[i * 4 + 0];
        d0 += n[i * 4 + 1] * (int64_t)m_indexed[i * 4 + 1];
        d0 += n[i * 4 + 2] * (int64_t)m_indexed[i * 4 + 2];
        d0 += n[i * 4 + 3] * (int64_t)m_indexed[i * 4 + 3];
        d1  = n[i * 4 + 4] * (int64_t)m_indexed[i * 4 + 0];
        d1 += n[i * 4 + 5] * (int64_t)m_indexed[i * 4 + 1];
        d1 += n[i * 4 + 6] * (int64_t)m_indexed[i * 4 + 2];
        d1 += n[i * 4 + 7] * (int64_t)m_indexed[i * 4 + 3];

        d[i + 0] += d0;
        d[i + 1] += d1;
    }

    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_udot_idx_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc), opr_sz_8 = opr_sz / 8;
    intptr_t index = simd_data(desc);
    uint64_t *d = vd;
    uint16_t *n = vn;
    uint16_t *m_indexed = (uint16_t *)vm + index * 4;

    /* This is supported by SVE only, so opr_sz is always a multiple of 16.
     * Process the entire segment all at once, writing back the results
     * only after we've consumed all of the inputs.
     */
    for (i = 0; i < opr_sz_8 ; i += 2) {
        uint64_t d0, d1;

        d0  = n[i * 4 + 0] * (uint64_t)m_indexed[i * 4 + 0];
        d0 += n[i * 4 + 1] * (uint64_t)m_indexed[i * 4 + 1];
        d0 += n[i * 4 + 2] * (uint64_t)m_indexed[i * 4 + 2];
        d0 += n[i * 4 + 3] * (uint64_t)m_indexed[i * 4 + 3];
        d1  = n[i * 4 + 4] * (uint64_t)m_indexed[i * 4 + 0];
        d1 += n[i * 4 + 5] * (uint64_t)m_indexed[i * 4 + 1];
        d1 += n[i * 4 + 6] * (uint64_t)m_indexed[i * 4 + 2];
        d1 += n[i * 4 + 7] * (uint64_t)m_indexed[i * 4 + 3];

        d[i + 0] += d0;
        d[i + 1] += d1;
    }

    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_fcaddh)(void *vd, void *vn, void *vm,
                         void *vfpst, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    float16 *d = vd;
    float16 *n = vn;
    float16 *m = vm;
    float_status *fpst = vfpst;
    uint32_t neg_real = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint32_t neg_imag = neg_real ^ 1;
    uintptr_t i;

    /* Shift boolean to the sign bit so we can xor to negate.  */
    neg_real <<= 15;
    neg_imag <<= 15;

    for (i = 0; i < opr_sz / 2; i += 2) {
        float16 e0 = n[H2(i)];
        float16 e1 = m[H2(i + 1)] ^ neg_imag;
        float16 e2 = n[H2(i + 1)];
        float16 e3 = m[H2(i)] ^ neg_real;

        d[H2(i)] = float16_add(e0, e1, fpst);
        d[H2(i + 1)] = float16_add(e2, e3, fpst);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_fcadds)(void *vd, void *vn, void *vm,
                         void *vfpst, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    float32 *d = vd;
    float32 *n = vn;
    float32 *m = vm;
    float_status *fpst = vfpst;
    uint32_t neg_real = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint32_t neg_imag = neg_real ^ 1;
    uintptr_t i;

    /* Shift boolean to the sign bit so we can xor to negate.  */
    neg_real <<= 31;
    neg_imag <<= 31;

    for (i = 0; i < opr_sz / 4; i += 2) {
        float32 e0 = n[H4(i)];
        float32 e1 = m[H4(i + 1)] ^ neg_imag;
        float32 e2 = n[H4(i + 1)];
        float32 e3 = m[H4(i)] ^ neg_real;

        d[H4(i)] = float32_add(e0, e1, fpst);
        d[H4(i + 1)] = float32_add(e2, e3, fpst);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_fcaddd)(void *vd, void *vn, void *vm,
                         void *vfpst, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    float64 *d = vd;
    float64 *n = vn;
    float64 *m = vm;
    float_status *fpst = vfpst;
    uint64_t neg_real = extract64(desc, SIMD_DATA_SHIFT, 1);
    uint64_t neg_imag = neg_real ^ 1;
    uintptr_t i;

    /* Shift boolean to the sign bit so we can xor to negate.  */
    neg_real <<= 63;
    neg_imag <<= 63;

    for (i = 0; i < opr_sz / 8; i += 2) {
        float64 e0 = n[i];
        float64 e1 = m[i + 1] ^ neg_imag;
        float64 e2 = n[i + 1];
        float64 e3 = m[i] ^ neg_real;

        d[i] = float64_add(e0, e1, fpst);
        d[i + 1] = float64_add(e2, e3, fpst);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_fcmlah)(void *vd, void *vn, void *vm,
                         void *vfpst, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    float16 *d = vd;
    float16 *n = vn;
    float16 *m = vm;
    float_status *fpst = vfpst;
    intptr_t flip = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint32_t neg_imag = extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    uint32_t neg_real = flip ^ neg_imag;
    uintptr_t i;

    /* Shift boolean to the sign bit so we can xor to negate.  */
    neg_real <<= 15;
    neg_imag <<= 15;

    for (i = 0; i < opr_sz / 2; i += 2) {
        float16 e2 = n[H2(i + flip)];
        float16 e1 = m[H2(i + flip)] ^ neg_real;
        float16 e4 = e2;
        float16 e3 = m[H2(i + 1 - flip)] ^ neg_imag;

        d[H2(i)] = float16_muladd(e2, e1, d[H2(i)], 0, fpst);
        d[H2(i + 1)] = float16_muladd(e4, e3, d[H2(i + 1)], 0, fpst);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_fcmlah_idx)(void *vd, void *vn, void *vm,
                             void *vfpst, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    float16 *d = vd;
    float16 *n = vn;
    float16 *m = vm;
    float_status *fpst = vfpst;
    intptr_t flip = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint32_t neg_imag = extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    intptr_t index = extract32(desc, SIMD_DATA_SHIFT + 2, 2);
    uint32_t neg_real = flip ^ neg_imag;
    intptr_t elements = opr_sz / sizeof(float16);
    intptr_t eltspersegment = 16 / sizeof(float16);
    intptr_t i, j;

    /* Shift boolean to the sign bit so we can xor to negate.  */
    neg_real <<= 15;
    neg_imag <<= 15;

    for (i = 0; i < elements; i += eltspersegment) {
        float16 mr = m[H2(i + 2 * index + 0)];
        float16 mi = m[H2(i + 2 * index + 1)];
        float16 e1 = neg_real ^ (flip ? mi : mr);
        float16 e3 = neg_imag ^ (flip ? mr : mi);

        for (j = i; j < i + eltspersegment; j += 2) {
            float16 e2 = n[H2(j + flip)];
            float16 e4 = e2;

            d[H2(j)] = float16_muladd(e2, e1, d[H2(j)], 0, fpst);
            d[H2(j + 1)] = float16_muladd(e4, e3, d[H2(j + 1)], 0, fpst);
        }
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_fcmlas)(void *vd, void *vn, void *vm,
                         void *vfpst, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    float32 *d = vd;
    float32 *n = vn;
    float32 *m = vm;
    float_status *fpst = vfpst;
    intptr_t flip = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint32_t neg_imag = extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    uint32_t neg_real = flip ^ neg_imag;
    uintptr_t i;

    /* Shift boolean to the sign bit so we can xor to negate.  */
    neg_real <<= 31;
    neg_imag <<= 31;

    for (i = 0; i < opr_sz / 4; i += 2) {
        float32 e2 = n[H4(i + flip)];
        float32 e1 = m[H4(i + flip)] ^ neg_real;
        float32 e4 = e2;
        float32 e3 = m[H4(i + 1 - flip)] ^ neg_imag;

        d[H4(i)] = float32_muladd(e2, e1, d[H4(i)], 0, fpst);
        d[H4(i + 1)] = float32_muladd(e4, e3, d[H4(i + 1)], 0, fpst);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_fcmlas_idx)(void *vd, void *vn, void *vm,
                             void *vfpst, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    float32 *d = vd;
    float32 *n = vn;
    float32 *m = vm;
    float_status *fpst = vfpst;
    intptr_t flip = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint32_t neg_imag = extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    intptr_t index = extract32(desc, SIMD_DATA_SHIFT + 2, 2);
    uint32_t neg_real = flip ^ neg_imag;
    intptr_t elements = opr_sz / sizeof(float32);
    intptr_t eltspersegment = 16 / sizeof(float32);
    intptr_t i, j;

    /* Shift boolean to the sign bit so we can xor to negate.  */
    neg_real <<= 31;
    neg_imag <<= 31;

    for (i = 0; i < elements; i += eltspersegment) {
        float32 mr = m[H4(i + 2 * index + 0)];
        float32 mi = m[H4(i + 2 * index + 1)];
        float32 e1 = neg_real ^ (flip ? mi : mr);
        float32 e3 = neg_imag ^ (flip ? mr : mi);

        for (j = i; j < i + eltspersegment; j += 2) {
            float32 e2 = n[H4(j + flip)];
            float32 e4 = e2;

            d[H4(j)] = float32_muladd(e2, e1, d[H4(j)], 0, fpst);
            d[H4(j + 1)] = float32_muladd(e4, e3, d[H4(j + 1)], 0, fpst);
        }
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_fcmlad)(void *vd, void *vn, void *vm,
                         void *vfpst, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    float64 *d = vd;
    float64 *n = vn;
    float64 *m = vm;
    float_status *fpst = vfpst;
    intptr_t flip = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint64_t neg_imag = extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    uint64_t neg_real = flip ^ neg_imag;
    uintptr_t i;

    /* Shift boolean to the sign bit so we can xor to negate.  */
    neg_real <<= 63;
    neg_imag <<= 63;

    for (i = 0; i < opr_sz / 8; i += 2) {
        float64 e2 = n[i + flip];
        float64 e1 = m[i + flip] ^ neg_real;
        float64 e4 = e2;
        float64 e3 = m[i + 1 - flip] ^ neg_imag;

        d[i] = float64_muladd(e2, e1, d[i], 0, fpst);
        d[i + 1] = float64_muladd(e4, e3, d[i + 1], 0, fpst);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

/*
 * Floating point comparisons producing an integer result (all 1s or all 0s).
 * Note that EQ doesn't signal InvalidOp for QNaNs but GE and GT do.
 * Softfloat routines return 0/1, which we convert to the 0/-1 Neon requires.
 */
static uint16_t float16_ceq(float16 op1, float16 op2, float_status *stat)
{
    return -float16_eq_quiet(op1, op2, stat);
}

static uint32_t float32_ceq(float32 op1, float32 op2, float_status *stat)
{
    return -float32_eq_quiet(op1, op2, stat);
}

static uint16_t float16_cge(float16 op1, float16 op2, float_status *stat)
{
    return -float16_le(op2, op1, stat);
}

static uint32_t float32_cge(float32 op1, float32 op2, float_status *stat)
{
    return -float32_le(op2, op1, stat);
}

static uint16_t float16_cgt(float16 op1, float16 op2, float_status *stat)
{
    return -float16_lt(op2, op1, stat);
}

static uint32_t float32_cgt(float32 op1, float32 op2, float_status *stat)
{
    return -float32_lt(op2, op1, stat);
}

static uint16_t float16_acge(float16 op1, float16 op2, float_status *stat)
{
    return -float16_le(float16_abs(op2), float16_abs(op1), stat);
}

static uint32_t float32_acge(float32 op1, float32 op2, float_status *stat)
{
    return -float32_le(float32_abs(op2), float32_abs(op1), stat);
}

static uint16_t float16_acgt(float16 op1, float16 op2, float_status *stat)
{
    return -float16_lt(float16_abs(op2), float16_abs(op1), stat);
}

static uint32_t float32_acgt(float32 op1, float32 op2, float_status *stat)
{
    return -float32_lt(float32_abs(op2), float32_abs(op1), stat);
}

static int16_t vfp_tosszh(float16 x, void *fpstp)
{
    float_status *fpst = fpstp;
    if (float16_is_any_nan(x)) {
        float_raise(float_flag_invalid, fpst);
        return 0;
    }
    return float16_to_int16_round_to_zero(x, fpst);
}

static uint16_t vfp_touszh(float16 x, void *fpstp)
{
    float_status *fpst = fpstp;
    if (float16_is_any_nan(x)) {
        float_raise(float_flag_invalid, fpst);
        return 0;
    }
    return float16_to_uint16_round_to_zero(x, fpst);
}

#define DO_2OP(NAME, FUNC, TYPE) \
void HELPER(NAME)(void *vd, void *vn, void *stat, uint32_t desc)  \
{                                                                 \
    intptr_t i, oprsz = simd_oprsz(desc);                         \
    TYPE *d = vd, *n = vn;                                        \
    for (i = 0; i < oprsz / sizeof(TYPE); i++) {                  \
        d[i] = FUNC(n[i], stat);                                  \
    }                                                             \
    clear_tail(d, oprsz, simd_maxsz(desc));                       \
}

DO_2OP(gvec_frecpe_h, helper_recpe_f16, float16)
DO_2OP(gvec_frecpe_s, helper_recpe_f32, float32)
DO_2OP(gvec_frecpe_d, helper_recpe_f64, float64)

DO_2OP(gvec_frsqrte_h, helper_rsqrte_f16, float16)
DO_2OP(gvec_frsqrte_s, helper_rsqrte_f32, float32)
DO_2OP(gvec_frsqrte_d, helper_rsqrte_f64, float64)

DO_2OP(gvec_vrintx_h, float16_round_to_int, float16)
DO_2OP(gvec_vrintx_s, float32_round_to_int, float32)

DO_2OP(gvec_sitos, helper_vfp_sitos, int32_t)
DO_2OP(gvec_uitos, helper_vfp_uitos, uint32_t)
DO_2OP(gvec_tosizs, helper_vfp_tosizs, float32)
DO_2OP(gvec_touizs, helper_vfp_touizs, float32)
DO_2OP(gvec_sstoh, int16_to_float16, int16_t)
DO_2OP(gvec_ustoh, uint16_to_float16, uint16_t)
DO_2OP(gvec_tosszh, vfp_tosszh, float16)
DO_2OP(gvec_touszh, vfp_touszh, float16)

#define WRAP_CMP0_FWD(FN, CMPOP, TYPE)                          \
    static TYPE TYPE##_##FN##0(TYPE op, float_status *stat)     \
    {                                                           \
        return TYPE##_##CMPOP(op, TYPE##_zero, stat);           \
    }

#define WRAP_CMP0_REV(FN, CMPOP, TYPE)                          \
    static TYPE TYPE##_##FN##0(TYPE op, float_status *stat)    \
    {                                                           \
        return TYPE##_##CMPOP(TYPE##_zero, op, stat);           \
    }

#define DO_2OP_CMP0(FN, CMPOP, DIRN)                    \
    WRAP_CMP0_##DIRN(FN, CMPOP, float16)                \
    WRAP_CMP0_##DIRN(FN, CMPOP, float32)                \
    DO_2OP(gvec_f##FN##0_h, float16_##FN##0, float16)   \
    DO_2OP(gvec_f##FN##0_s, float32_##FN##0, float32)

DO_2OP_CMP0(cgt, cgt, FWD)
DO_2OP_CMP0(cge, cge, FWD)
DO_2OP_CMP0(ceq, ceq, FWD)
DO_2OP_CMP0(clt, cgt, REV)
DO_2OP_CMP0(cle, cge, REV)

#undef DO_2OP
#undef DO_2OP_CMP0

/* Floating-point trigonometric starting value.
 * See the ARM ARM pseudocode function FPTrigSMul.
 */
static float16 float16_ftsmul(float16 op1, uint16_t op2, float_status *stat)
{
    float16 result = float16_mul(op1, op1, stat);
    if (!float16_is_any_nan(result)) {
        result = float16_set_sign(result, op2 & 1);
    }
    return result;
}

static float32 float32_ftsmul(float32 op1, uint32_t op2, float_status *stat)
{
    float32 result = float32_mul(op1, op1, stat);
    if (!float32_is_any_nan(result)) {
        result = float32_set_sign(result, op2 & 1);
    }
    return result;
}

static float64 float64_ftsmul(float64 op1, uint64_t op2, float_status *stat)
{
    float64 result = float64_mul(op1, op1, stat);
    if (!float64_is_any_nan(result)) {
        result = float64_set_sign(result, op2 & 1);
    }
    return result;
}

static float16 float16_abd(float16 op1, float16 op2, float_status *stat)
{
    return float16_abs(float16_sub(op1, op2, stat));
}

static float32 float32_abd(float32 op1, float32 op2, float_status *stat)
{
    return float32_abs(float32_sub(op1, op2, stat));
}

/*
 * Reciprocal step. These are the AArch32 version which uses a
 * non-fused multiply-and-subtract.
 */
static float16 float16_recps_nf(float16 op1, float16 op2, float_status *stat)
{
    op1 = float16_squash_input_denormal(op1, stat);
    op2 = float16_squash_input_denormal(op2, stat);

    if ((float16_is_infinity(op1) && float16_is_zero(op2)) ||
        (float16_is_infinity(op2) && float16_is_zero(op1))) {
        return float16_two;
    }
    return float16_sub(float16_two, float16_mul(op1, op2, stat), stat);
}

static float32 float32_recps_nf(float32 op1, float32 op2, float_status *stat)
{
    op1 = float32_squash_input_denormal(op1, stat);
    op2 = float32_squash_input_denormal(op2, stat);

    if ((float32_is_infinity(op1) && float32_is_zero(op2)) ||
        (float32_is_infinity(op2) && float32_is_zero(op1))) {
        return float32_two;
    }
    return float32_sub(float32_two, float32_mul(op1, op2, stat), stat);
}

/* Reciprocal square-root step. AArch32 non-fused semantics. */
static float16 float16_rsqrts_nf(float16 op1, float16 op2, float_status *stat)
{
    op1 = float16_squash_input_denormal(op1, stat);
    op2 = float16_squash_input_denormal(op2, stat);

    if ((float16_is_infinity(op1) && float16_is_zero(op2)) ||
        (float16_is_infinity(op2) && float16_is_zero(op1))) {
        return float16_one_point_five;
    }
    op1 = float16_sub(float16_three, float16_mul(op1, op2, stat), stat);
    return float16_div(op1, float16_two, stat);
}

static float32 float32_rsqrts_nf(float32 op1, float32 op2, float_status *stat)
{
    op1 = float32_squash_input_denormal(op1, stat);
    op2 = float32_squash_input_denormal(op2, stat);

    if ((float32_is_infinity(op1) && float32_is_zero(op2)) ||
        (float32_is_infinity(op2) && float32_is_zero(op1))) {
        return float32_one_point_five;
    }
    op1 = float32_sub(float32_three, float32_mul(op1, op2, stat), stat);
    return float32_div(op1, float32_two, stat);
}

#define DO_3OP(NAME, FUNC, TYPE) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *stat, uint32_t desc) \
{                                                                          \
    intptr_t i, oprsz = simd_oprsz(desc);                                  \
    TYPE *d = vd, *n = vn, *m = vm;                                        \
    for (i = 0; i < oprsz / sizeof(TYPE); i++) {                           \
        d[i] = FUNC(n[i], m[i], stat);                                     \
    }                                                                      \
    clear_tail(d, oprsz, simd_maxsz(desc));                                \
}

DO_3OP(gvec_fadd_h, float16_add, float16)
DO_3OP(gvec_fadd_s, float32_add, float32)
DO_3OP(gvec_fadd_d, float64_add, float64)

DO_3OP(gvec_fsub_h, float16_sub, float16)
DO_3OP(gvec_fsub_s, float32_sub, float32)
DO_3OP(gvec_fsub_d, float64_sub, float64)

DO_3OP(gvec_fmul_h, float16_mul, float16)
DO_3OP(gvec_fmul_s, float32_mul, float32)
DO_3OP(gvec_fmul_d, float64_mul, float64)

DO_3OP(gvec_ftsmul_h, float16_ftsmul, float16)
DO_3OP(gvec_ftsmul_s, float32_ftsmul, float32)
DO_3OP(gvec_ftsmul_d, float64_ftsmul, float64)

DO_3OP(gvec_fabd_h, float16_abd, float16)
DO_3OP(gvec_fabd_s, float32_abd, float32)

DO_3OP(gvec_fceq_h, float16_ceq, float16)
DO_3OP(gvec_fceq_s, float32_ceq, float32)

DO_3OP(gvec_fcge_h, float16_cge, float16)
DO_3OP(gvec_fcge_s, float32_cge, float32)

DO_3OP(gvec_fcgt_h, float16_cgt, float16)
DO_3OP(gvec_fcgt_s, float32_cgt, float32)

DO_3OP(gvec_facge_h, float16_acge, float16)
DO_3OP(gvec_facge_s, float32_acge, float32)

DO_3OP(gvec_facgt_h, float16_acgt, float16)
DO_3OP(gvec_facgt_s, float32_acgt, float32)

DO_3OP(gvec_fmax_h, float16_max, float16)
DO_3OP(gvec_fmax_s, float32_max, float32)

DO_3OP(gvec_fmin_h, float16_min, float16)
DO_3OP(gvec_fmin_s, float32_min, float32)

DO_3OP(gvec_fmaxnum_h, float16_maxnum, float16)
DO_3OP(gvec_fmaxnum_s, float32_maxnum, float32)

DO_3OP(gvec_fminnum_h, float16_minnum, float16)
DO_3OP(gvec_fminnum_s, float32_minnum, float32)

DO_3OP(gvec_recps_nf_h, float16_recps_nf, float16)
DO_3OP(gvec_recps_nf_s, float32_recps_nf, float32)

DO_3OP(gvec_rsqrts_nf_h, float16_rsqrts_nf, float16)
DO_3OP(gvec_rsqrts_nf_s, float32_rsqrts_nf, float32)

#ifdef TARGET_AARCH64

DO_3OP(gvec_recps_h, helper_recpsf_f16, float16)
DO_3OP(gvec_recps_s, helper_recpsf_f32, float32)
DO_3OP(gvec_recps_d, helper_recpsf_f64, float64)

DO_3OP(gvec_rsqrts_h, helper_rsqrtsf_f16, float16)
DO_3OP(gvec_rsqrts_s, helper_rsqrtsf_f32, float32)
DO_3OP(gvec_rsqrts_d, helper_rsqrtsf_f64, float64)

#endif
#undef DO_3OP

/* Non-fused multiply-add (unlike float16_muladd etc, which are fused) */
static float16 float16_muladd_nf(float16 dest, float16 op1, float16 op2,
                                 float_status *stat)
{
    return float16_add(dest, float16_mul(op1, op2, stat), stat);
}

static float32 float32_muladd_nf(float32 dest, float32 op1, float32 op2,
                                 float_status *stat)
{
    return float32_add(dest, float32_mul(op1, op2, stat), stat);
}

static float16 float16_mulsub_nf(float16 dest, float16 op1, float16 op2,
                                 float_status *stat)
{
    return float16_sub(dest, float16_mul(op1, op2, stat), stat);
}

static float32 float32_mulsub_nf(float32 dest, float32 op1, float32 op2,
                                 float_status *stat)
{
    return float32_sub(dest, float32_mul(op1, op2, stat), stat);
}

/* Fused versions; these have the semantics Neon VFMA/VFMS want */
static float16 float16_muladd_f(float16 dest, float16 op1, float16 op2,
                                float_status *stat)
{
    return float16_muladd(op1, op2, dest, 0, stat);
}

static float32 float32_muladd_f(float32 dest, float32 op1, float32 op2,
                                 float_status *stat)
{
    return float32_muladd(op1, op2, dest, 0, stat);
}

static float16 float16_mulsub_f(float16 dest, float16 op1, float16 op2,
                                 float_status *stat)
{
    return float16_muladd(float16_chs(op1), op2, dest, 0, stat);
}

static float32 float32_mulsub_f(float32 dest, float32 op1, float32 op2,
                                 float_status *stat)
{
    return float32_muladd(float32_chs(op1), op2, dest, 0, stat);
}

#define DO_MULADD(NAME, FUNC, TYPE)                                     \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *stat, uint32_t desc) \
{                                                                          \
    intptr_t i, oprsz = simd_oprsz(desc);                                  \
    TYPE *d = vd, *n = vn, *m = vm;                                        \
    for (i = 0; i < oprsz / sizeof(TYPE); i++) {                           \
        d[i] = FUNC(d[i], n[i], m[i], stat);                               \
    }                                                                      \
    clear_tail(d, oprsz, simd_maxsz(desc));                                \
}

DO_MULADD(gvec_fmla_h, float16_muladd_nf, float16)
DO_MULADD(gvec_fmla_s, float32_muladd_nf, float32)

DO_MULADD(gvec_fmls_h, float16_mulsub_nf, float16)
DO_MULADD(gvec_fmls_s, float32_mulsub_nf, float32)

DO_MULADD(gvec_vfma_h, float16_muladd_f, float16)
DO_MULADD(gvec_vfma_s, float32_muladd_f, float32)

DO_MULADD(gvec_vfms_h, float16_mulsub_f, float16)
DO_MULADD(gvec_vfms_s, float32_mulsub_f, float32)

/* For the indexed ops, SVE applies the index per 128-bit vector segment.
 * For AdvSIMD, there is of course only one such vector segment.
 */

#define DO_MUL_IDX(NAME, TYPE, H) \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc) \
{                                                                          \
    intptr_t i, j, oprsz = simd_oprsz(desc);                               \
    intptr_t segment = MIN(16, oprsz) / sizeof(TYPE);                      \
    intptr_t idx = simd_data(desc);                                        \
    TYPE *d = vd, *n = vn, *m = vm;                                        \
    for (i = 0; i < oprsz / sizeof(TYPE); i += segment) {                  \
        TYPE mm = m[H(i + idx)];                                           \
        for (j = 0; j < segment; j++) {                                    \
            d[i + j] = n[i + j] * mm;                                      \
        }                                                                  \
    }                                                                      \
    clear_tail(d, oprsz, simd_maxsz(desc));                                \
}

DO_MUL_IDX(gvec_mul_idx_h, uint16_t, H2)
DO_MUL_IDX(gvec_mul_idx_s, uint32_t, H4)
DO_MUL_IDX(gvec_mul_idx_d, uint64_t, )

#undef DO_MUL_IDX

#define DO_MLA_IDX(NAME, TYPE, OP, H) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *va, uint32_t desc)   \
{                                                                          \
    intptr_t i, j, oprsz = simd_oprsz(desc);                               \
    intptr_t segment = MIN(16, oprsz) / sizeof(TYPE);                      \
    intptr_t idx = simd_data(desc);                                        \
    TYPE *d = vd, *n = vn, *m = vm, *a = va;                               \
    for (i = 0; i < oprsz / sizeof(TYPE); i += segment) {                  \
        TYPE mm = m[H(i + idx)];                                           \
        for (j = 0; j < segment; j++) {                                    \
            d[i + j] = a[i + j] OP n[i + j] * mm;                          \
        }                                                                  \
    }                                                                      \
    clear_tail(d, oprsz, simd_maxsz(desc));                                \
}

DO_MLA_IDX(gvec_mla_idx_h, uint16_t, +, H2)
DO_MLA_IDX(gvec_mla_idx_s, uint32_t, +, H4)
DO_MLA_IDX(gvec_mla_idx_d, uint64_t, +,   )

DO_MLA_IDX(gvec_mls_idx_h, uint16_t, -, H2)
DO_MLA_IDX(gvec_mls_idx_s, uint32_t, -, H4)
DO_MLA_IDX(gvec_mls_idx_d, uint64_t, -,   )

#undef DO_MLA_IDX

#define DO_FMUL_IDX(NAME, ADD, TYPE, H)                                    \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *stat, uint32_t desc) \
{                                                                          \
    intptr_t i, j, oprsz = simd_oprsz(desc);                               \
    intptr_t segment = MIN(16, oprsz) / sizeof(TYPE);                      \
    intptr_t idx = simd_data(desc);                                        \
    TYPE *d = vd, *n = vn, *m = vm;                                        \
    for (i = 0; i < oprsz / sizeof(TYPE); i += segment) {                  \
        TYPE mm = m[H(i + idx)];                                           \
        for (j = 0; j < segment; j++) {                                    \
            d[i + j] = TYPE##_##ADD(d[i + j],                              \
                                    TYPE##_mul(n[i + j], mm, stat), stat); \
        }                                                                  \
    }                                                                      \
    clear_tail(d, oprsz, simd_maxsz(desc));                                \
}

#define float16_nop(N, M, S) (M)
#define float32_nop(N, M, S) (M)
#define float64_nop(N, M, S) (M)

DO_FMUL_IDX(gvec_fmul_idx_h, nop, float16, H2)
DO_FMUL_IDX(gvec_fmul_idx_s, nop, float32, H4)
DO_FMUL_IDX(gvec_fmul_idx_d, nop, float64, )

/*
 * Non-fused multiply-accumulate operations, for Neon. NB that unlike
 * the fused ops below they assume accumulate both from and into Vd.
 */
DO_FMUL_IDX(gvec_fmla_nf_idx_h, add, float16, H2)
DO_FMUL_IDX(gvec_fmla_nf_idx_s, add, float32, H4)
DO_FMUL_IDX(gvec_fmls_nf_idx_h, sub, float16, H2)
DO_FMUL_IDX(gvec_fmls_nf_idx_s, sub, float32, H4)

#undef float16_nop
#undef float32_nop
#undef float64_nop
#undef DO_FMUL_IDX

#define DO_FMLA_IDX(NAME, TYPE, H)                                         \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *va,                  \
                  void *stat, uint32_t desc)                               \
{                                                                          \
    intptr_t i, j, oprsz = simd_oprsz(desc);                               \
    intptr_t segment = MIN(16, oprsz) / sizeof(TYPE);                      \
    TYPE op1_neg = extract32(desc, SIMD_DATA_SHIFT, 1);                    \
    intptr_t idx = desc >> (SIMD_DATA_SHIFT + 1);                          \
    TYPE *d = vd, *n = vn, *m = vm, *a = va;                               \
    op1_neg <<= (8 * sizeof(TYPE) - 1);                                    \
    for (i = 0; i < oprsz / sizeof(TYPE); i += segment) {                  \
        TYPE mm = m[H(i + idx)];                                           \
        for (j = 0; j < segment; j++) {                                    \
            d[i + j] = TYPE##_muladd(n[i + j] ^ op1_neg,                   \
                                     mm, a[i + j], 0, stat);               \
        }                                                                  \
    }                                                                      \
    clear_tail(d, oprsz, simd_maxsz(desc));                                \
}

DO_FMLA_IDX(gvec_fmla_idx_h, float16, H2)
DO_FMLA_IDX(gvec_fmla_idx_s, float32, H4)
DO_FMLA_IDX(gvec_fmla_idx_d, float64, )

#undef DO_FMLA_IDX

#define DO_SAT(NAME, WTYPE, TYPEN, TYPEM, OP, MIN, MAX) \
void HELPER(NAME)(void *vd, void *vq, void *vn, void *vm, uint32_t desc)   \
{                                                                          \
    intptr_t i, oprsz = simd_oprsz(desc);                                  \
    TYPEN *d = vd, *n = vn; TYPEM *m = vm;                                 \
    bool q = false;                                                        \
    for (i = 0; i < oprsz / sizeof(TYPEN); i++) {                          \
        WTYPE dd = (WTYPE)n[i] OP m[i];                                    \
        if (dd < MIN) {                                                    \
            dd = MIN;                                                      \
            q = true;                                                      \
        } else if (dd > MAX) {                                             \
            dd = MAX;                                                      \
            q = true;                                                      \
        }                                                                  \
        d[i] = dd;                                                         \
    }                                                                      \
    if (q) {                                                               \
        uint32_t *qc = vq;                                                 \
        qc[0] = 1;                                                         \
    }                                                                      \
    clear_tail(d, oprsz, simd_maxsz(desc));                                \
}

DO_SAT(gvec_uqadd_b, int, uint8_t, uint8_t, +, 0, UINT8_MAX)
DO_SAT(gvec_uqadd_h, int, uint16_t, uint16_t, +, 0, UINT16_MAX)
DO_SAT(gvec_uqadd_s, int64_t, uint32_t, uint32_t, +, 0, UINT32_MAX)

DO_SAT(gvec_sqadd_b, int, int8_t, int8_t, +, INT8_MIN, INT8_MAX)
DO_SAT(gvec_sqadd_h, int, int16_t, int16_t, +, INT16_MIN, INT16_MAX)
DO_SAT(gvec_sqadd_s, int64_t, int32_t, int32_t, +, INT32_MIN, INT32_MAX)

DO_SAT(gvec_uqsub_b, int, uint8_t, uint8_t, -, 0, UINT8_MAX)
DO_SAT(gvec_uqsub_h, int, uint16_t, uint16_t, -, 0, UINT16_MAX)
DO_SAT(gvec_uqsub_s, int64_t, uint32_t, uint32_t, -, 0, UINT32_MAX)

DO_SAT(gvec_sqsub_b, int, int8_t, int8_t, -, INT8_MIN, INT8_MAX)
DO_SAT(gvec_sqsub_h, int, int16_t, int16_t, -, INT16_MIN, INT16_MAX)
DO_SAT(gvec_sqsub_s, int64_t, int32_t, int32_t, -, INT32_MIN, INT32_MAX)

#undef DO_SAT

void HELPER(gvec_uqadd_d)(void *vd, void *vq, void *vn,
                          void *vm, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);
    uint64_t *d = vd, *n = vn, *m = vm;
    bool q = false;

    for (i = 0; i < oprsz / 8; i++) {
        uint64_t nn = n[i], mm = m[i], dd = nn + mm;
        if (dd < nn) {
            dd = UINT64_MAX;
            q = true;
        }
        d[i] = dd;
    }
    if (q) {
        uint32_t *qc = vq;
        qc[0] = 1;
    }
    clear_tail(d, oprsz, simd_maxsz(desc));
}

void HELPER(gvec_uqsub_d)(void *vd, void *vq, void *vn,
                          void *vm, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);
    uint64_t *d = vd, *n = vn, *m = vm;
    bool q = false;

    for (i = 0; i < oprsz / 8; i++) {
        uint64_t nn = n[i], mm = m[i], dd = nn - mm;
        if (nn < mm) {
            dd = 0;
            q = true;
        }
        d[i] = dd;
    }
    if (q) {
        uint32_t *qc = vq;
        qc[0] = 1;
    }
    clear_tail(d, oprsz, simd_maxsz(desc));
}

void HELPER(gvec_sqadd_d)(void *vd, void *vq, void *vn,
                          void *vm, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);
    int64_t *d = vd, *n = vn, *m = vm;
    bool q = false;

    for (i = 0; i < oprsz / 8; i++) {
        int64_t nn = n[i], mm = m[i], dd = nn + mm;
        if (((dd ^ nn) & ~(nn ^ mm)) & INT64_MIN) {
            dd = (nn >> 63) ^ ~INT64_MIN;
            q = true;
        }
        d[i] = dd;
    }
    if (q) {
        uint32_t *qc = vq;
        qc[0] = 1;
    }
    clear_tail(d, oprsz, simd_maxsz(desc));
}

void HELPER(gvec_sqsub_d)(void *vd, void *vq, void *vn,
                          void *vm, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);
    int64_t *d = vd, *n = vn, *m = vm;
    bool q = false;

    for (i = 0; i < oprsz / 8; i++) {
        int64_t nn = n[i], mm = m[i], dd = nn - mm;
        if (((dd ^ nn) & (nn ^ mm)) & INT64_MIN) {
            dd = (nn >> 63) ^ ~INT64_MIN;
            q = true;
        }
        d[i] = dd;
    }
    if (q) {
        uint32_t *qc = vq;
        qc[0] = 1;
    }
    clear_tail(d, oprsz, simd_maxsz(desc));
}


#define DO_SRA(NAME, TYPE)                              \
void HELPER(NAME)(void *vd, void *vn, uint32_t desc)    \
{                                                       \
    intptr_t i, oprsz = simd_oprsz(desc);               \
    int shift = simd_data(desc);                        \
    TYPE *d = vd, *n = vn;                              \
    for (i = 0; i < oprsz / sizeof(TYPE); i++) {        \
        d[i] += n[i] >> shift;                          \
    }                                                   \
    clear_tail(d, oprsz, simd_maxsz(desc));             \
}

DO_SRA(gvec_ssra_b, int8_t)
DO_SRA(gvec_ssra_h, int16_t)
DO_SRA(gvec_ssra_s, int32_t)
DO_SRA(gvec_ssra_d, int64_t)

DO_SRA(gvec_usra_b, uint8_t)
DO_SRA(gvec_usra_h, uint16_t)
DO_SRA(gvec_usra_s, uint32_t)
DO_SRA(gvec_usra_d, uint64_t)

#undef DO_SRA

#define DO_RSHR(NAME, TYPE)                             \
void HELPER(NAME)(void *vd, void *vn, uint32_t desc)    \
{                                                       \
    intptr_t i, oprsz = simd_oprsz(desc);               \
    int shift = simd_data(desc);                        \
    TYPE *d = vd, *n = vn;                              \
    for (i = 0; i < oprsz / sizeof(TYPE); i++) {        \
        TYPE tmp = n[i] >> (shift - 1);                 \
        d[i] = (tmp >> 1) + (tmp & 1);                  \
    }                                                   \
    clear_tail(d, oprsz, simd_maxsz(desc));             \
}

DO_RSHR(gvec_srshr_b, int8_t)
DO_RSHR(gvec_srshr_h, int16_t)
DO_RSHR(gvec_srshr_s, int32_t)
DO_RSHR(gvec_srshr_d, int64_t)

DO_RSHR(gvec_urshr_b, uint8_t)
DO_RSHR(gvec_urshr_h, uint16_t)
DO_RSHR(gvec_urshr_s, uint32_t)
DO_RSHR(gvec_urshr_d, uint64_t)

#undef DO_RSHR

#define DO_RSRA(NAME, TYPE)                             \
void HELPER(NAME)(void *vd, void *vn, uint32_t desc)    \
{                                                       \
    intptr_t i, oprsz = simd_oprsz(desc);               \
    int shift = simd_data(desc);                        \
    TYPE *d = vd, *n = vn;                              \
    for (i = 0; i < oprsz / sizeof(TYPE); i++) {        \
        TYPE tmp = n[i] >> (shift - 1);                 \
        d[i] += (tmp >> 1) + (tmp & 1);                 \
    }                                                   \
    clear_tail(d, oprsz, simd_maxsz(desc));             \
}

DO_RSRA(gvec_srsra_b, int8_t)
DO_RSRA(gvec_srsra_h, int16_t)
DO_RSRA(gvec_srsra_s, int32_t)
DO_RSRA(gvec_srsra_d, int64_t)

DO_RSRA(gvec_ursra_b, uint8_t)
DO_RSRA(gvec_ursra_h, uint16_t)
DO_RSRA(gvec_ursra_s, uint32_t)
DO_RSRA(gvec_ursra_d, uint64_t)

#undef DO_RSRA

#define DO_SRI(NAME, TYPE)                              \
void HELPER(NAME)(void *vd, void *vn, uint32_t desc)    \
{                                                       \
    intptr_t i, oprsz = simd_oprsz(desc);               \
    int shift = simd_data(desc);                        \
    TYPE *d = vd, *n = vn;                              \
    for (i = 0; i < oprsz / sizeof(TYPE); i++) {        \
        d[i] = deposit64(d[i], 0, sizeof(TYPE) * 8 - shift, n[i] >> shift); \
    }                                                   \
    clear_tail(d, oprsz, simd_maxsz(desc));             \
}

DO_SRI(gvec_sri_b, uint8_t)
DO_SRI(gvec_sri_h, uint16_t)
DO_SRI(gvec_sri_s, uint32_t)
DO_SRI(gvec_sri_d, uint64_t)

#undef DO_SRI

#define DO_SLI(NAME, TYPE)                              \
void HELPER(NAME)(void *vd, void *vn, uint32_t desc)    \
{                                                       \
    intptr_t i, oprsz = simd_oprsz(desc);               \
    int shift = simd_data(desc);                        \
    TYPE *d = vd, *n = vn;                              \
    for (i = 0; i < oprsz / sizeof(TYPE); i++) {        \
        d[i] = deposit64(d[i], shift, sizeof(TYPE) * 8 - shift, n[i]); \
    }                                                   \
    clear_tail(d, oprsz, simd_maxsz(desc));             \
}

DO_SLI(gvec_sli_b, uint8_t)
DO_SLI(gvec_sli_h, uint16_t)
DO_SLI(gvec_sli_s, uint32_t)
DO_SLI(gvec_sli_d, uint64_t)

#undef DO_SLI

/*
 * Convert float16 to float32, raising no exceptions and
 * preserving exceptional values, including SNaN.
 * This is effectively an unpack+repack operation.
 */
static float32 float16_to_float32_by_bits(uint32_t f16, bool fz16)
{
    const int f16_bias = 15;
    const int f32_bias = 127;
    uint32_t sign = extract32(f16, 15, 1);
    uint32_t exp = extract32(f16, 10, 5);
    uint32_t frac = extract32(f16, 0, 10);

    if (exp == 0x1f) {
        /* Inf or NaN */
        exp = 0xff;
    } else if (exp == 0) {
        /* Zero or denormal.  */
        if (frac != 0) {
            if (fz16) {
                frac = 0;
            } else {
                /*
                 * Denormal; these are all normal float32.
                 * Shift the fraction so that the msb is at bit 11,
                 * then remove bit 11 as the implicit bit of the
                 * normalized float32.  Note that we still go through
                 * the shift for normal numbers below, to put the
                 * float32 fraction at the right place.
                 */
                int shift = clz32(frac) - 21;
                frac = (frac << shift) & 0x3ff;
                exp = f32_bias - f16_bias - shift + 1;
            }
        }
    } else {
        /* Normal number; adjust the bias.  */
        exp += f32_bias - f16_bias;
    }
    sign <<= 31;
    exp <<= 23;
    frac <<= 23 - 10;

    return sign | exp | frac;
}

static uint64_t load4_f16(uint64_t *ptr, int is_q, int is_2)
{
    /*
     * Branchless load of u32[0], u64[0], u32[1], or u64[1].
     * Load the 2nd qword iff is_q & is_2.
     * Shift to the 2nd dword iff !is_q & is_2.
     * For !is_q & !is_2, the upper bits of the result are garbage.
     */
    return ptr[is_q & is_2] >> ((is_2 & ~is_q) << 5);
}

/*
 * Note that FMLAL requires oprsz == 8 or oprsz == 16,
 * as there is not yet SVE versions that might use blocking.
 */

static void do_fmlal(float32 *d, void *vn, void *vm, float_status *fpst,
                     uint32_t desc, bool fz16)
{
    intptr_t i, oprsz = simd_oprsz(desc);
    int is_s = extract32(desc, SIMD_DATA_SHIFT, 1);
    int is_2 = extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    int is_q = oprsz == 16;
    uint64_t n_4, m_4;

    /* Pre-load all of the f16 data, avoiding overlap issues.  */
    n_4 = load4_f16(vn, is_q, is_2);
    m_4 = load4_f16(vm, is_q, is_2);

    /* Negate all inputs for FMLSL at once.  */
    if (is_s) {
        n_4 ^= 0x8000800080008000ull;
    }

    for (i = 0; i < oprsz / 4; i++) {
        float32 n_1 = float16_to_float32_by_bits(n_4 >> (i * 16), fz16);
        float32 m_1 = float16_to_float32_by_bits(m_4 >> (i * 16), fz16);
        d[H4(i)] = float32_muladd(n_1, m_1, d[H4(i)], 0, fpst);
    }
    clear_tail(d, oprsz, simd_maxsz(desc));
}

void HELPER(gvec_fmlal_a32)(void *vd, void *vn, void *vm,
                            void *venv, uint32_t desc)
{
    CPUARMState *env = venv;
    do_fmlal(vd, vn, vm, &env->vfp.standard_fp_status, desc,
             get_flush_inputs_to_zero(&env->vfp.fp_status_f16));
}

void HELPER(gvec_fmlal_a64)(void *vd, void *vn, void *vm,
                            void *venv, uint32_t desc)
{
    CPUARMState *env = venv;
    do_fmlal(vd, vn, vm, &env->vfp.fp_status, desc,
             get_flush_inputs_to_zero(&env->vfp.fp_status_f16));
}

static void do_fmlal_idx(float32 *d, void *vn, void *vm, float_status *fpst,
                         uint32_t desc, bool fz16)
{
    intptr_t i, oprsz = simd_oprsz(desc);
    int is_s = extract32(desc, SIMD_DATA_SHIFT, 1);
    int is_2 = extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    int index = extract32(desc, SIMD_DATA_SHIFT + 2, 3);
    int is_q = oprsz == 16;
    uint64_t n_4;
    float32 m_1;

    /* Pre-load all of the f16 data, avoiding overlap issues.  */
    n_4 = load4_f16(vn, is_q, is_2);

    /* Negate all inputs for FMLSL at once.  */
    if (is_s) {
        n_4 ^= 0x8000800080008000ull;
    }

    m_1 = float16_to_float32_by_bits(((float16 *)vm)[H2(index)], fz16);

    for (i = 0; i < oprsz / 4; i++) {
        float32 n_1 = float16_to_float32_by_bits(n_4 >> (i * 16), fz16);
        d[H4(i)] = float32_muladd(n_1, m_1, d[H4(i)], 0, fpst);
    }
    clear_tail(d, oprsz, simd_maxsz(desc));
}

void HELPER(gvec_fmlal_idx_a32)(void *vd, void *vn, void *vm,
                                void *venv, uint32_t desc)
{
    CPUARMState *env = venv;
    do_fmlal_idx(vd, vn, vm, &env->vfp.standard_fp_status, desc,
                 get_flush_inputs_to_zero(&env->vfp.fp_status_f16));
}

void HELPER(gvec_fmlal_idx_a64)(void *vd, void *vn, void *vm,
                                void *venv, uint32_t desc)
{
    CPUARMState *env = venv;
    do_fmlal_idx(vd, vn, vm, &env->vfp.fp_status, desc,
                 get_flush_inputs_to_zero(&env->vfp.fp_status_f16));
}

void HELPER(gvec_sshl_b)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int8_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz; ++i) {
        int8_t mm = m[i];
        int8_t nn = n[i];
        int8_t res = 0;
        if (mm >= 0) {
            if (mm < 8) {
                res = nn << mm;
            }
        } else {
            res = nn >> (mm > -8 ? -mm : 7);
        }
        d[i] = res;
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_sshl_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int16_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 2; ++i) {
        int8_t mm = m[i];   /* only 8 bits of shift are significant */
        int16_t nn = n[i];
        int16_t res = 0;
        if (mm >= 0) {
            if (mm < 16) {
                res = nn << mm;
            }
        } else {
            res = nn >> (mm > -16 ? -mm : 15);
        }
        d[i] = res;
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_ushl_b)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint8_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz; ++i) {
        int8_t mm = m[i];
        uint8_t nn = n[i];
        uint8_t res = 0;
        if (mm >= 0) {
            if (mm < 8) {
                res = nn << mm;
            }
        } else {
            if (mm > -8) {
                res = nn >> -mm;
            }
        }
        d[i] = res;
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_ushl_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint16_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 2; ++i) {
        int8_t mm = m[i];   /* only 8 bits of shift are significant */
        uint16_t nn = n[i];
        uint16_t res = 0;
        if (mm >= 0) {
            if (mm < 16) {
                res = nn << mm;
            }
        } else {
            if (mm > -16) {
                res = nn >> -mm;
            }
        }
        d[i] = res;
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

/*
 * 8x8->8 polynomial multiply.
 *
 * Polynomial multiplication is like integer multiplication except the
 * partial products are XORed, not added.
 *
 * TODO: expose this as a generic vector operation, as it is a common
 * crypto building block.
 */
void HELPER(gvec_pmul_b)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    uint64_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 8; ++i) {
        uint64_t nn = n[i];
        uint64_t mm = m[i];
        uint64_t rr = 0;

        for (j = 0; j < 8; ++j) {
            uint64_t mask = (nn & 0x0101010101010101ull) * 0xff;
            rr ^= mm & mask;
            mm = (mm << 1) & 0xfefefefefefefefeull;
            nn >>= 1;
        }
        d[i] = rr;
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

/*
 * 64x64->128 polynomial multiply.
 * Because of the lanes are not accessed in strict columns,
 * this probably cannot be turned into a generic helper.
 */
void HELPER(gvec_pmull_q)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    intptr_t hi = simd_data(desc);
    uint64_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 8; i += 2) {
        uint64_t nn = n[i + hi];
        uint64_t mm = m[i + hi];
        uint64_t rhi = 0;
        uint64_t rlo = 0;

        /* Bit 0 can only influence the low 64-bit result.  */
        if (nn & 1) {
            rlo = mm;
        }

        for (j = 1; j < 64; ++j) {
            uint64_t mask = -((nn >> j) & 1);
            rlo ^= (mm << j) & mask;
            rhi ^= (mm >> (64 - j)) & mask;
        }
        d[i] = rlo;
        d[i + 1] = rhi;
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

/*
 * 8x8->16 polynomial multiply.
 *
 * The byte inputs are expanded to (or extracted from) half-words.
 * Note that neon and sve2 get the inputs from different positions.
 * This allows 4 bytes to be processed in parallel with uint64_t.
 */

static uint64_t expand_byte_to_half(uint64_t x)
{
    return  (x & 0x000000ff)
         | ((x & 0x0000ff00) << 8)
         | ((x & 0x00ff0000) << 16)
         | ((x & 0xff000000) << 24);
}

static uint64_t pmull_h(uint64_t op1, uint64_t op2)
{
    uint64_t result = 0;
    int i;

    for (i = 0; i < 8; ++i) {
        uint64_t mask = (op1 & 0x0001000100010001ull) * 0xffff;
        result ^= op2 & mask;
        op1 >>= 1;
        op2 <<= 1;
    }
    return result;
}

void HELPER(neon_pmull_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    int hi = simd_data(desc);
    uint64_t *d = vd, *n = vn, *m = vm;
    uint64_t nn = n[hi], mm = m[hi];

    d[0] = pmull_h(expand_byte_to_half(nn), expand_byte_to_half(mm));
    nn >>= 32;
    mm >>= 32;
    d[1] = pmull_h(expand_byte_to_half(nn), expand_byte_to_half(mm));

    clear_tail(d, 16, simd_maxsz(desc));
}

#ifdef TARGET_AARCH64
void HELPER(sve2_pmull_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    int shift = simd_data(desc) * 8;
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint64_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 8; ++i) {
        uint64_t nn = (n[i] >> shift) & 0x00ff00ff00ff00ffull;
        uint64_t mm = (m[i] >> shift) & 0x00ff00ff00ff00ffull;

        d[i] = pmull_h(nn, mm);
    }
}
#endif

#define DO_CMP0(NAME, TYPE, OP)                         \
void HELPER(NAME)(void *vd, void *vn, uint32_t desc)    \
{                                                       \
    intptr_t i, opr_sz = simd_oprsz(desc);              \
    for (i = 0; i < opr_sz; i += sizeof(TYPE)) {        \
        TYPE nn = *(TYPE *)(vn + i);                    \
        *(TYPE *)(vd + i) = -(nn OP 0);                 \
    }                                                   \
    clear_tail(vd, opr_sz, simd_maxsz(desc));           \
}

DO_CMP0(gvec_ceq0_b, int8_t, ==)
DO_CMP0(gvec_clt0_b, int8_t, <)
DO_CMP0(gvec_cle0_b, int8_t, <=)
DO_CMP0(gvec_cgt0_b, int8_t, >)
DO_CMP0(gvec_cge0_b, int8_t, >=)

DO_CMP0(gvec_ceq0_h, int16_t, ==)
DO_CMP0(gvec_clt0_h, int16_t, <)
DO_CMP0(gvec_cle0_h, int16_t, <=)
DO_CMP0(gvec_cgt0_h, int16_t, >)
DO_CMP0(gvec_cge0_h, int16_t, >=)

#undef DO_CMP0

#define DO_ABD(NAME, TYPE)                                      \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc)  \
{                                                               \
    intptr_t i, opr_sz = simd_oprsz(desc);                      \
    TYPE *d = vd, *n = vn, *m = vm;                             \
                                                                \
    for (i = 0; i < opr_sz / sizeof(TYPE); ++i) {               \
        d[i] = n[i] < m[i] ? m[i] - n[i] : n[i] - m[i];         \
    }                                                           \
    clear_tail(d, opr_sz, simd_maxsz(desc));                    \
}

DO_ABD(gvec_sabd_b, int8_t)
DO_ABD(gvec_sabd_h, int16_t)
DO_ABD(gvec_sabd_s, int32_t)
DO_ABD(gvec_sabd_d, int64_t)

DO_ABD(gvec_uabd_b, uint8_t)
DO_ABD(gvec_uabd_h, uint16_t)
DO_ABD(gvec_uabd_s, uint32_t)
DO_ABD(gvec_uabd_d, uint64_t)

#undef DO_ABD

#define DO_ABA(NAME, TYPE)                                      \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc)  \
{                                                               \
    intptr_t i, opr_sz = simd_oprsz(desc);                      \
    TYPE *d = vd, *n = vn, *m = vm;                             \
                                                                \
    for (i = 0; i < opr_sz / sizeof(TYPE); ++i) {               \
        d[i] += n[i] < m[i] ? m[i] - n[i] : n[i] - m[i];        \
    }                                                           \
    clear_tail(d, opr_sz, simd_maxsz(desc));                    \
}

DO_ABA(gvec_saba_b, int8_t)
DO_ABA(gvec_saba_h, int16_t)
DO_ABA(gvec_saba_s, int32_t)
DO_ABA(gvec_saba_d, int64_t)

DO_ABA(gvec_uaba_b, uint8_t)
DO_ABA(gvec_uaba_h, uint16_t)
DO_ABA(gvec_uaba_s, uint32_t)
DO_ABA(gvec_uaba_d, uint64_t)

#undef DO_ABA

#define DO_NEON_PAIRWISE(NAME, OP)                                      \
    void HELPER(NAME##s)(void *vd, void *vn, void *vm,                  \
                         void *stat, uint32_t oprsz)                    \
    {                                                                   \
        float_status *fpst = stat;                                      \
        float32 *d = vd;                                                \
        float32 *n = vn;                                                \
        float32 *m = vm;                                                \
        float32 r0, r1;                                                 \
                                                                        \
        /* Read all inputs before writing outputs in case vm == vd */   \
        r0 = float32_##OP(n[H4(0)], n[H4(1)], fpst);                    \
        r1 = float32_##OP(m[H4(0)], m[H4(1)], fpst);                    \
                                                                        \
        d[H4(0)] = r0;                                                  \
        d[H4(1)] = r1;                                                  \
    }                                                                   \
                                                                        \
    void HELPER(NAME##h)(void *vd, void *vn, void *vm,                  \
                         void *stat, uint32_t oprsz)                    \
    {                                                                   \
        float_status *fpst = stat;                                      \
        float16 *d = vd;                                                \
        float16 *n = vn;                                                \
        float16 *m = vm;                                                \
        float16 r0, r1, r2, r3;                                         \
                                                                        \
        /* Read all inputs before writing outputs in case vm == vd */   \
        r0 = float16_##OP(n[H2(0)], n[H2(1)], fpst);                    \
        r1 = float16_##OP(n[H2(2)], n[H2(3)], fpst);                    \
        r2 = float16_##OP(m[H2(0)], m[H2(1)], fpst);                    \
        r3 = float16_##OP(m[H2(2)], m[H2(3)], fpst);                    \
                                                                        \
        d[H2(0)] = r0;                                                  \
        d[H2(1)] = r1;                                                  \
        d[H2(2)] = r2;                                                  \
        d[H2(3)] = r3;                                                  \
    }

DO_NEON_PAIRWISE(neon_padd, add)
DO_NEON_PAIRWISE(neon_pmax, max)
DO_NEON_PAIRWISE(neon_pmin, min)

#undef DO_NEON_PAIRWISE

#define DO_VCVT_FIXED(NAME, FUNC, TYPE)                                 \
    void HELPER(NAME)(void *vd, void *vn, void *stat, uint32_t desc)    \
    {                                                                   \
        intptr_t i, oprsz = simd_oprsz(desc);                           \
        int shift = simd_data(desc);                                    \
        TYPE *d = vd, *n = vn;                                          \
        float_status *fpst = stat;                                      \
        for (i = 0; i < oprsz / sizeof(TYPE); i++) {                    \
            d[i] = FUNC(n[i], shift, fpst);                             \
        }                                                               \
        clear_tail(d, oprsz, simd_maxsz(desc));                         \
    }

DO_VCVT_FIXED(gvec_vcvt_sf, helper_vfp_sltos, uint32_t)
DO_VCVT_FIXED(gvec_vcvt_uf, helper_vfp_ultos, uint32_t)
DO_VCVT_FIXED(gvec_vcvt_fs, helper_vfp_tosls_round_to_zero, uint32_t)
DO_VCVT_FIXED(gvec_vcvt_fu, helper_vfp_touls_round_to_zero, uint32_t)
DO_VCVT_FIXED(gvec_vcvt_sh, helper_vfp_shtoh, uint16_t)
DO_VCVT_FIXED(gvec_vcvt_uh, helper_vfp_uhtoh, uint16_t)
DO_VCVT_FIXED(gvec_vcvt_hs, helper_vfp_toshh_round_to_zero, uint16_t)
DO_VCVT_FIXED(gvec_vcvt_hu, helper_vfp_touhh_round_to_zero, uint16_t)

#undef DO_VCVT_FIXED

#define DO_VCVT_RMODE(NAME, FUNC, TYPE)                                 \
    void HELPER(NAME)(void *vd, void *vn, void *stat, uint32_t desc)    \
    {                                                                   \
        float_status *fpst = stat;                                      \
        intptr_t i, oprsz = simd_oprsz(desc);                           \
        uint32_t rmode = simd_data(desc);                               \
        uint32_t prev_rmode = get_float_rounding_mode(fpst);            \
        TYPE *d = vd, *n = vn;                                          \
        set_float_rounding_mode(rmode, fpst);                           \
        for (i = 0; i < oprsz / sizeof(TYPE); i++) {                    \
            d[i] = FUNC(n[i], 0, fpst);                                 \
        }                                                               \
        set_float_rounding_mode(prev_rmode, fpst);                      \
        clear_tail(d, oprsz, simd_maxsz(desc));                         \
    }

DO_VCVT_RMODE(gvec_vcvt_rm_ss, helper_vfp_tosls, uint32_t)
DO_VCVT_RMODE(gvec_vcvt_rm_us, helper_vfp_touls, uint32_t)
DO_VCVT_RMODE(gvec_vcvt_rm_sh, helper_vfp_toshh, uint16_t)
DO_VCVT_RMODE(gvec_vcvt_rm_uh, helper_vfp_touhh, uint16_t)

#undef DO_VCVT_RMODE

#define DO_VRINT_RMODE(NAME, FUNC, TYPE)                                \
    void HELPER(NAME)(void *vd, void *vn, void *stat, uint32_t desc)    \
    {                                                                   \
        float_status *fpst = stat;                                      \
        intptr_t i, oprsz = simd_oprsz(desc);                           \
        uint32_t rmode = simd_data(desc);                               \
        uint32_t prev_rmode = get_float_rounding_mode(fpst);            \
        TYPE *d = vd, *n = vn;                                          \
        set_float_rounding_mode(rmode, fpst);                           \
        for (i = 0; i < oprsz / sizeof(TYPE); i++) {                    \
            d[i] = FUNC(n[i], fpst);                                    \
        }                                                               \
        set_float_rounding_mode(prev_rmode, fpst);                      \
        clear_tail(d, oprsz, simd_maxsz(desc));                         \
    }

DO_VRINT_RMODE(gvec_vrint_rm_h, helper_rinth, uint16_t)
DO_VRINT_RMODE(gvec_vrint_rm_s, helper_rints, uint32_t)

#undef DO_VRINT_RMODE
