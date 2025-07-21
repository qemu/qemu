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
#include "qemu/int128.h"
#include "crypto/clmul.h"
#include "vec_internal.h"

/*
 * Data for expanding active predicate bits to bytes, for byte elements.
 *
 *  for (i = 0; i < 256; ++i) {
 *      unsigned long m = 0;
 *      for (j = 0; j < 8; j++) {
 *          if ((i >> j) & 1) {
 *              m |= 0xfful << (j << 3);
 *          }
 *      }
 *      printf("0x%016lx,\n", m);
 *  }
 */
const uint64_t expand_pred_b_data[256] = {
    0x0000000000000000, 0x00000000000000ff, 0x000000000000ff00,
    0x000000000000ffff, 0x0000000000ff0000, 0x0000000000ff00ff,
    0x0000000000ffff00, 0x0000000000ffffff, 0x00000000ff000000,
    0x00000000ff0000ff, 0x00000000ff00ff00, 0x00000000ff00ffff,
    0x00000000ffff0000, 0x00000000ffff00ff, 0x00000000ffffff00,
    0x00000000ffffffff, 0x000000ff00000000, 0x000000ff000000ff,
    0x000000ff0000ff00, 0x000000ff0000ffff, 0x000000ff00ff0000,
    0x000000ff00ff00ff, 0x000000ff00ffff00, 0x000000ff00ffffff,
    0x000000ffff000000, 0x000000ffff0000ff, 0x000000ffff00ff00,
    0x000000ffff00ffff, 0x000000ffffff0000, 0x000000ffffff00ff,
    0x000000ffffffff00, 0x000000ffffffffff, 0x0000ff0000000000,
    0x0000ff00000000ff, 0x0000ff000000ff00, 0x0000ff000000ffff,
    0x0000ff0000ff0000, 0x0000ff0000ff00ff, 0x0000ff0000ffff00,
    0x0000ff0000ffffff, 0x0000ff00ff000000, 0x0000ff00ff0000ff,
    0x0000ff00ff00ff00, 0x0000ff00ff00ffff, 0x0000ff00ffff0000,
    0x0000ff00ffff00ff, 0x0000ff00ffffff00, 0x0000ff00ffffffff,
    0x0000ffff00000000, 0x0000ffff000000ff, 0x0000ffff0000ff00,
    0x0000ffff0000ffff, 0x0000ffff00ff0000, 0x0000ffff00ff00ff,
    0x0000ffff00ffff00, 0x0000ffff00ffffff, 0x0000ffffff000000,
    0x0000ffffff0000ff, 0x0000ffffff00ff00, 0x0000ffffff00ffff,
    0x0000ffffffff0000, 0x0000ffffffff00ff, 0x0000ffffffffff00,
    0x0000ffffffffffff, 0x00ff000000000000, 0x00ff0000000000ff,
    0x00ff00000000ff00, 0x00ff00000000ffff, 0x00ff000000ff0000,
    0x00ff000000ff00ff, 0x00ff000000ffff00, 0x00ff000000ffffff,
    0x00ff0000ff000000, 0x00ff0000ff0000ff, 0x00ff0000ff00ff00,
    0x00ff0000ff00ffff, 0x00ff0000ffff0000, 0x00ff0000ffff00ff,
    0x00ff0000ffffff00, 0x00ff0000ffffffff, 0x00ff00ff00000000,
    0x00ff00ff000000ff, 0x00ff00ff0000ff00, 0x00ff00ff0000ffff,
    0x00ff00ff00ff0000, 0x00ff00ff00ff00ff, 0x00ff00ff00ffff00,
    0x00ff00ff00ffffff, 0x00ff00ffff000000, 0x00ff00ffff0000ff,
    0x00ff00ffff00ff00, 0x00ff00ffff00ffff, 0x00ff00ffffff0000,
    0x00ff00ffffff00ff, 0x00ff00ffffffff00, 0x00ff00ffffffffff,
    0x00ffff0000000000, 0x00ffff00000000ff, 0x00ffff000000ff00,
    0x00ffff000000ffff, 0x00ffff0000ff0000, 0x00ffff0000ff00ff,
    0x00ffff0000ffff00, 0x00ffff0000ffffff, 0x00ffff00ff000000,
    0x00ffff00ff0000ff, 0x00ffff00ff00ff00, 0x00ffff00ff00ffff,
    0x00ffff00ffff0000, 0x00ffff00ffff00ff, 0x00ffff00ffffff00,
    0x00ffff00ffffffff, 0x00ffffff00000000, 0x00ffffff000000ff,
    0x00ffffff0000ff00, 0x00ffffff0000ffff, 0x00ffffff00ff0000,
    0x00ffffff00ff00ff, 0x00ffffff00ffff00, 0x00ffffff00ffffff,
    0x00ffffffff000000, 0x00ffffffff0000ff, 0x00ffffffff00ff00,
    0x00ffffffff00ffff, 0x00ffffffffff0000, 0x00ffffffffff00ff,
    0x00ffffffffffff00, 0x00ffffffffffffff, 0xff00000000000000,
    0xff000000000000ff, 0xff0000000000ff00, 0xff0000000000ffff,
    0xff00000000ff0000, 0xff00000000ff00ff, 0xff00000000ffff00,
    0xff00000000ffffff, 0xff000000ff000000, 0xff000000ff0000ff,
    0xff000000ff00ff00, 0xff000000ff00ffff, 0xff000000ffff0000,
    0xff000000ffff00ff, 0xff000000ffffff00, 0xff000000ffffffff,
    0xff0000ff00000000, 0xff0000ff000000ff, 0xff0000ff0000ff00,
    0xff0000ff0000ffff, 0xff0000ff00ff0000, 0xff0000ff00ff00ff,
    0xff0000ff00ffff00, 0xff0000ff00ffffff, 0xff0000ffff000000,
    0xff0000ffff0000ff, 0xff0000ffff00ff00, 0xff0000ffff00ffff,
    0xff0000ffffff0000, 0xff0000ffffff00ff, 0xff0000ffffffff00,
    0xff0000ffffffffff, 0xff00ff0000000000, 0xff00ff00000000ff,
    0xff00ff000000ff00, 0xff00ff000000ffff, 0xff00ff0000ff0000,
    0xff00ff0000ff00ff, 0xff00ff0000ffff00, 0xff00ff0000ffffff,
    0xff00ff00ff000000, 0xff00ff00ff0000ff, 0xff00ff00ff00ff00,
    0xff00ff00ff00ffff, 0xff00ff00ffff0000, 0xff00ff00ffff00ff,
    0xff00ff00ffffff00, 0xff00ff00ffffffff, 0xff00ffff00000000,
    0xff00ffff000000ff, 0xff00ffff0000ff00, 0xff00ffff0000ffff,
    0xff00ffff00ff0000, 0xff00ffff00ff00ff, 0xff00ffff00ffff00,
    0xff00ffff00ffffff, 0xff00ffffff000000, 0xff00ffffff0000ff,
    0xff00ffffff00ff00, 0xff00ffffff00ffff, 0xff00ffffffff0000,
    0xff00ffffffff00ff, 0xff00ffffffffff00, 0xff00ffffffffffff,
    0xffff000000000000, 0xffff0000000000ff, 0xffff00000000ff00,
    0xffff00000000ffff, 0xffff000000ff0000, 0xffff000000ff00ff,
    0xffff000000ffff00, 0xffff000000ffffff, 0xffff0000ff000000,
    0xffff0000ff0000ff, 0xffff0000ff00ff00, 0xffff0000ff00ffff,
    0xffff0000ffff0000, 0xffff0000ffff00ff, 0xffff0000ffffff00,
    0xffff0000ffffffff, 0xffff00ff00000000, 0xffff00ff000000ff,
    0xffff00ff0000ff00, 0xffff00ff0000ffff, 0xffff00ff00ff0000,
    0xffff00ff00ff00ff, 0xffff00ff00ffff00, 0xffff00ff00ffffff,
    0xffff00ffff000000, 0xffff00ffff0000ff, 0xffff00ffff00ff00,
    0xffff00ffff00ffff, 0xffff00ffffff0000, 0xffff00ffffff00ff,
    0xffff00ffffffff00, 0xffff00ffffffffff, 0xffffff0000000000,
    0xffffff00000000ff, 0xffffff000000ff00, 0xffffff000000ffff,
    0xffffff0000ff0000, 0xffffff0000ff00ff, 0xffffff0000ffff00,
    0xffffff0000ffffff, 0xffffff00ff000000, 0xffffff00ff0000ff,
    0xffffff00ff00ff00, 0xffffff00ff00ffff, 0xffffff00ffff0000,
    0xffffff00ffff00ff, 0xffffff00ffffff00, 0xffffff00ffffffff,
    0xffffffff00000000, 0xffffffff000000ff, 0xffffffff0000ff00,
    0xffffffff0000ffff, 0xffffffff00ff0000, 0xffffffff00ff00ff,
    0xffffffff00ffff00, 0xffffffff00ffffff, 0xffffffffff000000,
    0xffffffffff0000ff, 0xffffffffff00ff00, 0xffffffffff00ffff,
    0xffffffffffff0000, 0xffffffffffff00ff, 0xffffffffffffff00,
    0xffffffffffffffff,
};

/*
 * Similarly for half-word elements.
 *  for (i = 0; i < 256; ++i) {
 *      unsigned long m = 0;
 *      if (i & 0xaa) {
 *          continue;
 *      }
 *      for (j = 0; j < 8; j += 2) {
 *          if ((i >> j) & 1) {
 *              m |= 0xfffful << (j << 3);
 *          }
 *      }
 *      printf("[0x%x] = 0x%016lx,\n", i, m);
 *  }
 */
const uint64_t expand_pred_h_data[0x55 + 1] = {
    [0x01] = 0x000000000000ffff, [0x04] = 0x00000000ffff0000,
    [0x05] = 0x00000000ffffffff, [0x10] = 0x0000ffff00000000,
    [0x11] = 0x0000ffff0000ffff, [0x14] = 0x0000ffffffff0000,
    [0x15] = 0x0000ffffffffffff, [0x40] = 0xffff000000000000,
    [0x41] = 0xffff00000000ffff, [0x44] = 0xffff0000ffff0000,
    [0x45] = 0xffff0000ffffffff, [0x50] = 0xffffffff00000000,
    [0x51] = 0xffffffff0000ffff, [0x54] = 0xffffffffffff0000,
    [0x55] = 0xffffffffffffffff,
};

/* Signed saturating rounding doubling multiply-accumulate high half, 8-bit */
int8_t do_sqrdmlah_b(int8_t src1, int8_t src2, int8_t src3,
                     bool neg, bool round)
{
    /*
     * Simplify:
     * = ((a3 << 8) + ((e1 * e2) << 1) + (round << 7)) >> 8
     * = ((a3 << 7) + (e1 * e2) + (round << 6)) >> 7
     */
    int32_t ret = (int32_t)src1 * src2;
    if (neg) {
        ret = -ret;
    }
    ret += ((int32_t)src3 << 7) + (round << 6);
    ret >>= 7;

    if (ret != (int8_t)ret) {
        ret = (ret < 0 ? INT8_MIN : INT8_MAX);
    }
    return ret;
}

void HELPER(sve2_sqrdmlah_b)(void *vd, void *vn, void *vm,
                             void *va, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int8_t *d = vd, *n = vn, *m = vm, *a = va;

    for (i = 0; i < opr_sz; ++i) {
        d[i] = do_sqrdmlah_b(n[i], m[i], a[i], false, true);
    }
}

void HELPER(sve2_sqrdmlsh_b)(void *vd, void *vn, void *vm,
                             void *va, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int8_t *d = vd, *n = vn, *m = vm, *a = va;

    for (i = 0; i < opr_sz; ++i) {
        d[i] = do_sqrdmlah_b(n[i], m[i], a[i], true, true);
    }
}

void HELPER(sve2_sqdmulh_b)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int8_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz; ++i) {
        d[i] = do_sqrdmlah_b(n[i], m[i], 0, false, false);
    }
}

void HELPER(sve2_sqrdmulh_b)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int8_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz; ++i) {
        d[i] = do_sqrdmlah_b(n[i], m[i], 0, false, true);
    }
}

/* Signed saturating rounding doubling multiply-accumulate high half, 16-bit */
int16_t do_sqrdmlah_h(int16_t src1, int16_t src2, int16_t src3,
                      bool neg, bool round, uint32_t *sat)
{
    /* Simplify similarly to do_sqrdmlah_b above.  */
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

void HELPER(neon_sqdmulh_idx_h)(void *vd, void *vn, void *vm,
                                void *vq, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    int idx = simd_data(desc);
    int16_t *d = vd, *n = vn, *m = (int16_t *)vm + H2(idx);
    intptr_t elements = opr_sz / 2;
    intptr_t eltspersegment = MIN(16 / 2, elements);

    for (i = 0; i < elements; i += 16 / 2) {
        int16_t mm = m[i];
        for (j = 0; j < eltspersegment; ++j) {
            d[i + j] = do_sqrdmlah_h(n[i + j], mm, 0, false, false, vq);
        }
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(neon_sqrdmulh_idx_h)(void *vd, void *vn, void *vm,
                                 void *vq, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    int idx = simd_data(desc);
    int16_t *d = vd, *n = vn, *m = (int16_t *)vm + H2(idx);
    intptr_t elements = opr_sz / 2;
    intptr_t eltspersegment = MIN(16 / 2, elements);

    for (i = 0; i < elements; i += 16 / 2) {
        int16_t mm = m[i];
        for (j = 0; j < eltspersegment; ++j) {
            d[i + j] = do_sqrdmlah_h(n[i + j], mm, 0, false, true, vq);
        }
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(neon_sqrdmlah_idx_h)(void *vd, void *vn, void *vm,
                                 void *vq, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    int idx = simd_data(desc);
    int16_t *d = vd, *n = vn, *m = (int16_t *)vm + H2(idx);
    intptr_t elements = opr_sz / 2;
    intptr_t eltspersegment = MIN(16 / 2, elements);

    for (i = 0; i < elements; i += 16 / 2) {
        int16_t mm = m[i];
        for (j = 0; j < eltspersegment; ++j) {
            d[i + j] = do_sqrdmlah_h(n[i + j], mm, d[i + j], false, true, vq);
        }
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(neon_sqrdmlsh_idx_h)(void *vd, void *vn, void *vm,
                                 void *vq, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    int idx = simd_data(desc);
    int16_t *d = vd, *n = vn, *m = (int16_t *)vm + H2(idx);
    intptr_t elements = opr_sz / 2;
    intptr_t eltspersegment = MIN(16 / 2, elements);

    for (i = 0; i < elements; i += 16 / 2) {
        int16_t mm = m[i];
        for (j = 0; j < eltspersegment; ++j) {
            d[i + j] = do_sqrdmlah_h(n[i + j], mm, d[i + j], true, true, vq);
        }
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(sve2_sqrdmlah_h)(void *vd, void *vn, void *vm,
                             void *va, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int16_t *d = vd, *n = vn, *m = vm, *a = va;
    uint32_t discard;

    for (i = 0; i < opr_sz / 2; ++i) {
        d[i] = do_sqrdmlah_h(n[i], m[i], a[i], false, true, &discard);
    }
}

void HELPER(sve2_sqrdmlsh_h)(void *vd, void *vn, void *vm,
                             void *va, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int16_t *d = vd, *n = vn, *m = vm, *a = va;
    uint32_t discard;

    for (i = 0; i < opr_sz / 2; ++i) {
        d[i] = do_sqrdmlah_h(n[i], m[i], a[i], true, true, &discard);
    }
}

void HELPER(sve2_sqdmulh_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int16_t *d = vd, *n = vn, *m = vm;
    uint32_t discard;

    for (i = 0; i < opr_sz / 2; ++i) {
        d[i] = do_sqrdmlah_h(n[i], m[i], 0, false, false, &discard);
    }
}

void HELPER(sve2_sqrdmulh_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int16_t *d = vd, *n = vn, *m = vm;
    uint32_t discard;

    for (i = 0; i < opr_sz / 2; ++i) {
        d[i] = do_sqrdmlah_h(n[i], m[i], 0, false, true, &discard);
    }
}

void HELPER(sve2_sqdmulh_idx_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    int idx = simd_data(desc);
    int16_t *d = vd, *n = vn, *m = (int16_t *)vm + H2(idx);
    uint32_t discard;

    for (i = 0; i < opr_sz / 2; i += 16 / 2) {
        int16_t mm = m[i];
        for (j = 0; j < 16 / 2; ++j) {
            d[i + j] = do_sqrdmlah_h(n[i + j], mm, 0, false, false, &discard);
        }
    }
}

void HELPER(sve2_sqrdmulh_idx_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    int idx = simd_data(desc);
    int16_t *d = vd, *n = vn, *m = (int16_t *)vm + H2(idx);
    uint32_t discard;

    for (i = 0; i < opr_sz / 2; i += 16 / 2) {
        int16_t mm = m[i];
        for (j = 0; j < 16 / 2; ++j) {
            d[i + j] = do_sqrdmlah_h(n[i + j], mm, 0, false, true, &discard);
        }
    }
}

/* Signed saturating rounding doubling multiply-accumulate high half, 32-bit */
int32_t do_sqrdmlah_s(int32_t src1, int32_t src2, int32_t src3,
                      bool neg, bool round, uint32_t *sat)
{
    /* Simplify similarly to do_sqrdmlah_b above.  */
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

void HELPER(neon_sqdmulh_idx_s)(void *vd, void *vn, void *vm,
                                void *vq, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    int idx = simd_data(desc);
    int32_t *d = vd, *n = vn, *m = (int32_t *)vm + H4(idx);
    intptr_t elements = opr_sz / 4;
    intptr_t eltspersegment = MIN(16 / 4, elements);

    for (i = 0; i < elements; i += 16 / 4) {
        int32_t mm = m[i];
        for (j = 0; j < eltspersegment; ++j) {
            d[i + j] = do_sqrdmlah_s(n[i + j], mm, 0, false, false, vq);
        }
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(neon_sqrdmulh_idx_s)(void *vd, void *vn, void *vm,
                                 void *vq, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    int idx = simd_data(desc);
    int32_t *d = vd, *n = vn, *m = (int32_t *)vm + H4(idx);
    intptr_t elements = opr_sz / 4;
    intptr_t eltspersegment = MIN(16 / 4, elements);

    for (i = 0; i < elements; i += 16 / 4) {
        int32_t mm = m[i];
        for (j = 0; j < eltspersegment; ++j) {
            d[i + j] = do_sqrdmlah_s(n[i + j], mm, 0, false, true, vq);
        }
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(neon_sqrdmlah_idx_s)(void *vd, void *vn, void *vm,
                                 void *vq, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    int idx = simd_data(desc);
    int32_t *d = vd, *n = vn, *m = (int32_t *)vm + H4(idx);
    intptr_t elements = opr_sz / 4;
    intptr_t eltspersegment = MIN(16 / 4, elements);

    for (i = 0; i < elements; i += 16 / 4) {
        int32_t mm = m[i];
        for (j = 0; j < eltspersegment; ++j) {
            d[i + j] = do_sqrdmlah_s(n[i + j], mm, d[i + j], false, true, vq);
        }
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(neon_sqrdmlsh_idx_s)(void *vd, void *vn, void *vm,
                                 void *vq, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    int idx = simd_data(desc);
    int32_t *d = vd, *n = vn, *m = (int32_t *)vm + H4(idx);
    intptr_t elements = opr_sz / 4;
    intptr_t eltspersegment = MIN(16 / 4, elements);

    for (i = 0; i < elements; i += 16 / 4) {
        int32_t mm = m[i];
        for (j = 0; j < eltspersegment; ++j) {
            d[i + j] = do_sqrdmlah_s(n[i + j], mm, d[i + j], true, true, vq);
        }
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(sve2_sqrdmlah_s)(void *vd, void *vn, void *vm,
                             void *va, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int32_t *d = vd, *n = vn, *m = vm, *a = va;
    uint32_t discard;

    for (i = 0; i < opr_sz / 4; ++i) {
        d[i] = do_sqrdmlah_s(n[i], m[i], a[i], false, true, &discard);
    }
}

void HELPER(sve2_sqrdmlsh_s)(void *vd, void *vn, void *vm,
                             void *va, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int32_t *d = vd, *n = vn, *m = vm, *a = va;
    uint32_t discard;

    for (i = 0; i < opr_sz / 4; ++i) {
        d[i] = do_sqrdmlah_s(n[i], m[i], a[i], true, true, &discard);
    }
}

void HELPER(sve2_sqdmulh_s)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int32_t *d = vd, *n = vn, *m = vm;
    uint32_t discard;

    for (i = 0; i < opr_sz / 4; ++i) {
        d[i] = do_sqrdmlah_s(n[i], m[i], 0, false, false, &discard);
    }
}

void HELPER(sve2_sqrdmulh_s)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int32_t *d = vd, *n = vn, *m = vm;
    uint32_t discard;

    for (i = 0; i < opr_sz / 4; ++i) {
        d[i] = do_sqrdmlah_s(n[i], m[i], 0, false, true, &discard);
    }
}

void HELPER(sve2_sqdmulh_idx_s)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    int idx = simd_data(desc);
    int32_t *d = vd, *n = vn, *m = (int32_t *)vm + H4(idx);
    uint32_t discard;

    for (i = 0; i < opr_sz / 4; i += 16 / 4) {
        int32_t mm = m[i];
        for (j = 0; j < 16 / 4; ++j) {
            d[i + j] = do_sqrdmlah_s(n[i + j], mm, 0, false, false, &discard);
        }
    }
}

void HELPER(sve2_sqrdmulh_idx_s)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    int idx = simd_data(desc);
    int32_t *d = vd, *n = vn, *m = (int32_t *)vm + H4(idx);
    uint32_t discard;

    for (i = 0; i < opr_sz / 4; i += 16 / 4) {
        int32_t mm = m[i];
        for (j = 0; j < 16 / 4; ++j) {
            d[i + j] = do_sqrdmlah_s(n[i + j], mm, 0, false, true, &discard);
        }
    }
}

/* Signed saturating rounding doubling multiply-accumulate high half, 64-bit */
static int64_t do_sat128_d(Int128 r)
{
    int64_t ls = int128_getlo(r);
    int64_t hs = int128_gethi(r);

    if (unlikely(hs != (ls >> 63))) {
        return hs < 0 ? INT64_MIN : INT64_MAX;
    }
    return ls;
}

int64_t do_sqrdmlah_d(int64_t n, int64_t m, int64_t a, bool neg, bool round)
{
    uint64_t l, h;
    Int128 r, t;

    /* As in do_sqrdmlah_b, but with 128-bit arithmetic. */
    muls64(&l, &h, m, n);
    r = int128_make128(l, h);
    if (neg) {
        r = int128_neg(r);
    }
    if (a) {
        t = int128_exts64(a);
        t = int128_lshift(t, 63);
        r = int128_add(r, t);
    }
    if (round) {
        t = int128_exts64(1ll << 62);
        r = int128_add(r, t);
    }
    r = int128_rshift(r, 63);

    return do_sat128_d(r);
}

void HELPER(sve2_sqrdmlah_d)(void *vd, void *vn, void *vm,
                             void *va, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int64_t *d = vd, *n = vn, *m = vm, *a = va;

    for (i = 0; i < opr_sz / 8; ++i) {
        d[i] = do_sqrdmlah_d(n[i], m[i], a[i], false, true);
    }
}

void HELPER(sve2_sqrdmlsh_d)(void *vd, void *vn, void *vm,
                             void *va, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int64_t *d = vd, *n = vn, *m = vm, *a = va;

    for (i = 0; i < opr_sz / 8; ++i) {
        d[i] = do_sqrdmlah_d(n[i], m[i], a[i], true, true);
    }
}

void HELPER(sve2_sqdmulh_d)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int64_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 8; ++i) {
        d[i] = do_sqrdmlah_d(n[i], m[i], 0, false, false);
    }
}

void HELPER(sve2_sqrdmulh_d)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int64_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 8; ++i) {
        d[i] = do_sqrdmlah_d(n[i], m[i], 0, false, true);
    }
}

void HELPER(sve2_sqdmulh_idx_d)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    int idx = simd_data(desc);
    int64_t *d = vd, *n = vn, *m = (int64_t *)vm + idx;

    for (i = 0; i < opr_sz / 8; i += 16 / 8) {
        int64_t mm = m[i];
        for (j = 0; j < 16 / 8; ++j) {
            d[i + j] = do_sqrdmlah_d(n[i + j], mm, 0, false, false);
        }
    }
}

void HELPER(sve2_sqrdmulh_idx_d)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    int idx = simd_data(desc);
    int64_t *d = vd, *n = vn, *m = (int64_t *)vm + idx;

    for (i = 0; i < opr_sz / 8; i += 16 / 8) {
        int64_t mm = m[i];
        for (j = 0; j < 16 / 8; ++j) {
            d[i + j] = do_sqrdmlah_d(n[i + j], mm, 0, false, true);
        }
    }
}

/* Integer 8 and 16-bit dot-product.
 *
 * Note that for the loops herein, host endianness does not matter
 * with respect to the ordering of data within the quad-width lanes.
 * All elements are treated equally, no matter where they are.
 */

#define DO_DOT(NAME, TYPED, TYPEN, TYPEM) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *va, uint32_t desc)  \
{                                                                         \
    intptr_t i, opr_sz = simd_oprsz(desc);                                \
    TYPED *d = vd, *a = va;                                               \
    TYPEN *n = vn;                                                        \
    TYPEM *m = vm;                                                        \
    for (i = 0; i < opr_sz / sizeof(TYPED); ++i) {                        \
        d[i] = (a[i] +                                                    \
                (TYPED)n[i * 4 + 0] * m[i * 4 + 0] +                      \
                (TYPED)n[i * 4 + 1] * m[i * 4 + 1] +                      \
                (TYPED)n[i * 4 + 2] * m[i * 4 + 2] +                      \
                (TYPED)n[i * 4 + 3] * m[i * 4 + 3]);                      \
    }                                                                     \
    clear_tail(d, opr_sz, simd_maxsz(desc));                              \
}

DO_DOT(gvec_sdot_4b, int32_t, int8_t, int8_t)
DO_DOT(gvec_udot_4b, uint32_t, uint8_t, uint8_t)
DO_DOT(gvec_usdot_4b, uint32_t, uint8_t, int8_t)
DO_DOT(gvec_sdot_4h, int64_t, int16_t, int16_t)
DO_DOT(gvec_udot_4h, uint64_t, uint16_t, uint16_t)

#define DO_DOT_IDX(NAME, TYPED, TYPEN, TYPEM, HD) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *va, uint32_t desc)  \
{                                                                         \
    intptr_t i = 0, opr_sz = simd_oprsz(desc);                            \
    intptr_t opr_sz_n = opr_sz / sizeof(TYPED);                           \
    /*                                                                    \
     * Special case: opr_sz == 8 from AA64/AA32 advsimd means the         \
     * first iteration might not be a full 16 byte segment. But           \
     * for vector lengths beyond that this must be SVE and we know        \
     * opr_sz is a multiple of 16, so we need not clamp segend            \
     * to opr_sz_n when we advance it at the end of the loop.             \
     */                                                                   \
    intptr_t segend = MIN(16 / sizeof(TYPED), opr_sz_n);                  \
    intptr_t index = simd_data(desc);                                     \
    TYPED *d = vd, *a = va;                                               \
    TYPEN *n = vn;                                                        \
    TYPEM *m_indexed = (TYPEM *)vm + HD(index) * 4;                       \
    do {                                                                  \
        TYPED m0 = m_indexed[i * 4 + 0];                                  \
        TYPED m1 = m_indexed[i * 4 + 1];                                  \
        TYPED m2 = m_indexed[i * 4 + 2];                                  \
        TYPED m3 = m_indexed[i * 4 + 3];                                  \
        do {                                                              \
            d[i] = (a[i] +                                                \
                    n[i * 4 + 0] * m0 +                                   \
                    n[i * 4 + 1] * m1 +                                   \
                    n[i * 4 + 2] * m2 +                                   \
                    n[i * 4 + 3] * m3);                                   \
        } while (++i < segend);                                           \
        segend = i + (16 / sizeof(TYPED));                                \
    } while (i < opr_sz_n);                                               \
    clear_tail(d, opr_sz, simd_maxsz(desc));                              \
}

DO_DOT_IDX(gvec_sdot_idx_4b, int32_t, int8_t, int8_t, H4)
DO_DOT_IDX(gvec_udot_idx_4b, uint32_t, uint8_t, uint8_t, H4)
DO_DOT_IDX(gvec_sudot_idx_4b, int32_t, int8_t, uint8_t, H4)
DO_DOT_IDX(gvec_usdot_idx_4b, int32_t, uint8_t, int8_t, H4)
DO_DOT_IDX(gvec_sdot_idx_4h, int64_t, int16_t, int16_t, H8)
DO_DOT_IDX(gvec_udot_idx_4h, uint64_t, uint16_t, uint16_t, H8)

#undef DO_DOT
#undef DO_DOT_IDX

/* Similar for 2-way dot product */
#define DO_DOT(NAME, TYPED, TYPEN, TYPEM) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *va, uint32_t desc)  \
{                                                                         \
    intptr_t i, opr_sz = simd_oprsz(desc);                                \
    TYPED *d = vd, *a = va;                                               \
    TYPEN *n = vn;                                                        \
    TYPEM *m = vm;                                                        \
    for (i = 0; i < opr_sz / sizeof(TYPED); ++i) {                        \
        d[i] = (a[i] +                                                    \
                (TYPED)n[i * 2 + 0] * m[i * 2 + 0] +                      \
                (TYPED)n[i * 2 + 1] * m[i * 2 + 1]);                      \
    }                                                                     \
    clear_tail(d, opr_sz, simd_maxsz(desc));                              \
}

#define DO_DOT_IDX(NAME, TYPED, TYPEN, TYPEM, HD) \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *va, uint32_t desc)  \
{                                                                         \
    intptr_t i = 0, opr_sz = simd_oprsz(desc);                            \
    intptr_t opr_sz_n = opr_sz / sizeof(TYPED);                           \
    intptr_t segend = MIN(16 / sizeof(TYPED), opr_sz_n);                  \
    intptr_t index = simd_data(desc);                                     \
    TYPED *d = vd, *a = va;                                               \
    TYPEN *n = vn;                                                        \
    TYPEM *m_indexed = (TYPEM *)vm + HD(index) * 2;                       \
    do {                                                                  \
        TYPED m0 = m_indexed[i * 2 + 0];                                  \
        TYPED m1 = m_indexed[i * 2 + 1];                                  \
        do {                                                              \
            d[i] = (a[i] +                                                \
                    n[i * 2 + 0] * m0 +                                   \
                    n[i * 2 + 1] * m1);                                   \
        } while (++i < segend);                                           \
        segend = i + (16 / sizeof(TYPED));                                \
    } while (i < opr_sz_n);                                               \
    clear_tail(d, opr_sz, simd_maxsz(desc));                              \
}

DO_DOT(gvec_sdot_2h, int32_t, int16_t, int16_t)
DO_DOT(gvec_udot_2h, uint32_t, uint16_t, uint16_t)

DO_DOT_IDX(gvec_sdot_idx_2h, int32_t, int16_t, int16_t, H4)
DO_DOT_IDX(gvec_udot_idx_2h, uint32_t, uint16_t, uint16_t, H4)

#undef DO_DOT
#undef DO_DOT_IDX

void HELPER(gvec_fcaddh)(void *vd, void *vn, void *vm,
                         float_status *fpst, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    float16 *d = vd;
    float16 *n = vn;
    float16 *m = vm;
    bool rot = extract32(desc, SIMD_DATA_SHIFT, 1);
    bool fpcr_ah = extract64(desc, SIMD_DATA_SHIFT + 1, 1);
    uintptr_t i;

    for (i = 0; i < opr_sz / 2; i += 2) {
        float16 e0 = n[H2(i)];
        float16 e1 = m[H2(i + 1)];
        float16 e2 = n[H2(i + 1)];
        float16 e3 = m[H2(i)];

        if (rot) {
            e3 = float16_maybe_ah_chs(e3, fpcr_ah);
        } else {
            e1 = float16_maybe_ah_chs(e1, fpcr_ah);
        }

        d[H2(i)] = float16_add(e0, e1, fpst);
        d[H2(i + 1)] = float16_add(e2, e3, fpst);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_fcadds)(void *vd, void *vn, void *vm,
                         float_status *fpst, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    float32 *d = vd;
    float32 *n = vn;
    float32 *m = vm;
    bool rot = extract32(desc, SIMD_DATA_SHIFT, 1);
    bool fpcr_ah = extract64(desc, SIMD_DATA_SHIFT + 1, 1);
    uintptr_t i;

    for (i = 0; i < opr_sz / 4; i += 2) {
        float32 e0 = n[H4(i)];
        float32 e1 = m[H4(i + 1)];
        float32 e2 = n[H4(i + 1)];
        float32 e3 = m[H4(i)];

        if (rot) {
            e3 = float32_maybe_ah_chs(e3, fpcr_ah);
        } else {
            e1 = float32_maybe_ah_chs(e1, fpcr_ah);
        }

        d[H4(i)] = float32_add(e0, e1, fpst);
        d[H4(i + 1)] = float32_add(e2, e3, fpst);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_fcaddd)(void *vd, void *vn, void *vm,
                         float_status *fpst, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    float64 *d = vd;
    float64 *n = vn;
    float64 *m = vm;
    bool rot = extract32(desc, SIMD_DATA_SHIFT, 1);
    bool fpcr_ah = extract64(desc, SIMD_DATA_SHIFT + 1, 1);
    uintptr_t i;

    for (i = 0; i < opr_sz / 8; i += 2) {
        float64 e0 = n[i];
        float64 e1 = m[i + 1];
        float64 e2 = n[i + 1];
        float64 e3 = m[i];

        if (rot) {
            e3 = float64_maybe_ah_chs(e3, fpcr_ah);
        } else {
            e1 = float64_maybe_ah_chs(e1, fpcr_ah);
        }

        d[i] = float64_add(e0, e1, fpst);
        d[i + 1] = float64_add(e2, e3, fpst);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_fcmlah)(void *vd, void *vn, void *vm, void *va,
                         float_status *fpst, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    float16 *d = vd, *n = vn, *m = vm, *a = va;
    intptr_t flip = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint32_t fpcr_ah = extract32(desc, SIMD_DATA_SHIFT + 2, 1);
    uint32_t negf_imag = extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    uint32_t negf_real = flip ^ negf_imag;
    float16 negx_imag, negx_real;
    uintptr_t i;

    /* With AH=0, use negx; with AH=1 use negf. */
    negx_real = (negf_real & ~fpcr_ah) << 15;
    negx_imag = (negf_imag & ~fpcr_ah) << 15;
    negf_real = (negf_real & fpcr_ah ? float_muladd_negate_product : 0);
    negf_imag = (negf_imag & fpcr_ah ? float_muladd_negate_product : 0);

    for (i = 0; i < opr_sz / 2; i += 2) {
        float16 e2 = n[H2(i + flip)];
        float16 e1 = m[H2(i + flip)] ^ negx_real;
        float16 e4 = e2;
        float16 e3 = m[H2(i + 1 - flip)] ^ negx_imag;

        d[H2(i)] = float16_muladd(e2, e1, a[H2(i)], negf_real, fpst);
        d[H2(i + 1)] = float16_muladd(e4, e3, a[H2(i + 1)], negf_imag, fpst);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_fcmlah_idx)(void *vd, void *vn, void *vm, void *va,
                             float_status *fpst, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    float16 *d = vd, *n = vn, *m = vm, *a = va;
    intptr_t flip = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint32_t negf_imag = extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    intptr_t index = extract32(desc, SIMD_DATA_SHIFT + 2, 2);
    uint32_t fpcr_ah = extract32(desc, SIMD_DATA_SHIFT + 4, 1);
    uint32_t negf_real = flip ^ negf_imag;
    intptr_t elements = opr_sz / sizeof(float16);
    intptr_t eltspersegment = MIN(16 / sizeof(float16), elements);
    float16 negx_imag, negx_real;
    intptr_t i, j;

    /* With AH=0, use negx; with AH=1 use negf. */
    negx_real = (negf_real & ~fpcr_ah) << 15;
    negx_imag = (negf_imag & ~fpcr_ah) << 15;
    negf_real = (negf_real & fpcr_ah ? float_muladd_negate_product : 0);
    negf_imag = (negf_imag & fpcr_ah ? float_muladd_negate_product : 0);

    for (i = 0; i < elements; i += eltspersegment) {
        float16 mr = m[H2(i + 2 * index + 0)];
        float16 mi = m[H2(i + 2 * index + 1)];
        float16 e1 = negx_real ^ (flip ? mi : mr);
        float16 e3 = negx_imag ^ (flip ? mr : mi);

        for (j = i; j < i + eltspersegment; j += 2) {
            float16 e2 = n[H2(j + flip)];
            float16 e4 = e2;

            d[H2(j)] = float16_muladd(e2, e1, a[H2(j)], negf_real, fpst);
            d[H2(j + 1)] = float16_muladd(e4, e3, a[H2(j + 1)], negf_imag, fpst);
        }
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_fcmlas)(void *vd, void *vn, void *vm, void *va,
                         float_status *fpst, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    float32 *d = vd, *n = vn, *m = vm, *a = va;
    intptr_t flip = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint32_t fpcr_ah = extract32(desc, SIMD_DATA_SHIFT + 2, 1);
    uint32_t negf_imag = extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    uint32_t negf_real = flip ^ negf_imag;
    float32 negx_imag, negx_real;
    uintptr_t i;

    /* With AH=0, use negx; with AH=1 use negf. */
    negx_real = (negf_real & ~fpcr_ah) << 31;
    negx_imag = (negf_imag & ~fpcr_ah) << 31;
    negf_real = (negf_real & fpcr_ah ? float_muladd_negate_product : 0);
    negf_imag = (negf_imag & fpcr_ah ? float_muladd_negate_product : 0);

    for (i = 0; i < opr_sz / 4; i += 2) {
        float32 e2 = n[H4(i + flip)];
        float32 e1 = m[H4(i + flip)] ^ negx_real;
        float32 e4 = e2;
        float32 e3 = m[H4(i + 1 - flip)] ^ negx_imag;

        d[H4(i)] = float32_muladd(e2, e1, a[H4(i)], negf_real, fpst);
        d[H4(i + 1)] = float32_muladd(e4, e3, a[H4(i + 1)], negf_imag, fpst);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_fcmlas_idx)(void *vd, void *vn, void *vm, void *va,
                             float_status *fpst, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    float32 *d = vd, *n = vn, *m = vm, *a = va;
    intptr_t flip = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint32_t negf_imag = extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    intptr_t index = extract32(desc, SIMD_DATA_SHIFT + 2, 2);
    uint32_t fpcr_ah = extract32(desc, SIMD_DATA_SHIFT + 4, 1);
    uint32_t negf_real = flip ^ negf_imag;
    intptr_t elements = opr_sz / sizeof(float32);
    intptr_t eltspersegment = MIN(16 / sizeof(float32), elements);
    float32 negx_imag, negx_real;
    intptr_t i, j;

    /* With AH=0, use negx; with AH=1 use negf. */
    negx_real = (negf_real & ~fpcr_ah) << 31;
    negx_imag = (negf_imag & ~fpcr_ah) << 31;
    negf_real = (negf_real & fpcr_ah ? float_muladd_negate_product : 0);
    negf_imag = (negf_imag & fpcr_ah ? float_muladd_negate_product : 0);

    for (i = 0; i < elements; i += eltspersegment) {
        float32 mr = m[H4(i + 2 * index + 0)];
        float32 mi = m[H4(i + 2 * index + 1)];
        float32 e1 = negx_real ^ (flip ? mi : mr);
        float32 e3 = negx_imag ^ (flip ? mr : mi);

        for (j = i; j < i + eltspersegment; j += 2) {
            float32 e2 = n[H4(j + flip)];
            float32 e4 = e2;

            d[H4(j)] = float32_muladd(e2, e1, a[H4(j)], negf_real, fpst);
            d[H4(j + 1)] = float32_muladd(e4, e3, a[H4(j + 1)], negf_imag, fpst);
        }
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_fcmlad)(void *vd, void *vn, void *vm, void *va,
                         float_status *fpst, uint32_t desc)
{
    uintptr_t opr_sz = simd_oprsz(desc);
    float64 *d = vd, *n = vn, *m = vm, *a = va;
    intptr_t flip = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint32_t fpcr_ah = extract32(desc, SIMD_DATA_SHIFT + 2, 1);
    uint32_t negf_imag = extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    uint32_t negf_real = flip ^ negf_imag;
    float64 negx_real, negx_imag;
    uintptr_t i;

    /* With AH=0, use negx; with AH=1 use negf. */
    negx_real = (uint64_t)(negf_real & ~fpcr_ah) << 63;
    negx_imag = (uint64_t)(negf_imag & ~fpcr_ah) << 63;
    negf_real = (negf_real & fpcr_ah ? float_muladd_negate_product : 0);
    negf_imag = (negf_imag & fpcr_ah ? float_muladd_negate_product : 0);

    for (i = 0; i < opr_sz / 8; i += 2) {
        float64 e2 = n[i + flip];
        float64 e1 = m[i + flip] ^ negx_real;
        float64 e4 = e2;
        float64 e3 = m[i + 1 - flip] ^ negx_imag;

        d[i] = float64_muladd(e2, e1, a[i], negf_real, fpst);
        d[i + 1] = float64_muladd(e4, e3, a[i + 1], negf_imag, fpst);
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

static uint64_t float64_ceq(float64 op1, float64 op2, float_status *stat)
{
    return -float64_eq_quiet(op1, op2, stat);
}

static uint16_t float16_cge(float16 op1, float16 op2, float_status *stat)
{
    return -float16_le(op2, op1, stat);
}

static uint32_t float32_cge(float32 op1, float32 op2, float_status *stat)
{
    return -float32_le(op2, op1, stat);
}

static uint64_t float64_cge(float64 op1, float64 op2, float_status *stat)
{
    return -float64_le(op2, op1, stat);
}

static uint16_t float16_cgt(float16 op1, float16 op2, float_status *stat)
{
    return -float16_lt(op2, op1, stat);
}

static uint32_t float32_cgt(float32 op1, float32 op2, float_status *stat)
{
    return -float32_lt(op2, op1, stat);
}

static uint64_t float64_cgt(float64 op1, float64 op2, float_status *stat)
{
    return -float64_lt(op2, op1, stat);
}

static uint16_t float16_acge(float16 op1, float16 op2, float_status *stat)
{
    return -float16_le(float16_abs(op2), float16_abs(op1), stat);
}

static uint32_t float32_acge(float32 op1, float32 op2, float_status *stat)
{
    return -float32_le(float32_abs(op2), float32_abs(op1), stat);
}

static uint64_t float64_acge(float64 op1, float64 op2, float_status *stat)
{
    return -float64_le(float64_abs(op2), float64_abs(op1), stat);
}

static uint16_t float16_acgt(float16 op1, float16 op2, float_status *stat)
{
    return -float16_lt(float16_abs(op2), float16_abs(op1), stat);
}

static uint32_t float32_acgt(float32 op1, float32 op2, float_status *stat)
{
    return -float32_lt(float32_abs(op2), float32_abs(op1), stat);
}

static uint64_t float64_acgt(float64 op1, float64 op2, float_status *stat)
{
    return -float64_lt(float64_abs(op2), float64_abs(op1), stat);
}

static int16_t vfp_tosszh(float16 x, float_status *fpst)
{
    if (float16_is_any_nan(x)) {
        float_raise(float_flag_invalid, fpst);
        return 0;
    }
    return float16_to_int16_round_to_zero(x, fpst);
}

static uint16_t vfp_touszh(float16 x, float_status *fpst)
{
    if (float16_is_any_nan(x)) {
        float_raise(float_flag_invalid, fpst);
        return 0;
    }
    return float16_to_uint16_round_to_zero(x, fpst);
}

#define DO_2OP(NAME, FUNC, TYPE) \
void HELPER(NAME)(void *vd, void *vn, float_status *stat, uint32_t desc)  \
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
DO_2OP(gvec_frecpe_rpres_s, helper_recpe_rpres_f32, float32)
DO_2OP(gvec_frecpe_d, helper_recpe_f64, float64)

DO_2OP(gvec_frsqrte_h, helper_rsqrte_f16, float16)
DO_2OP(gvec_frsqrte_s, helper_rsqrte_f32, float32)
DO_2OP(gvec_frsqrte_rpres_s, helper_rsqrte_rpres_f32, float32)
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
    WRAP_CMP0_##DIRN(FN, CMPOP, float64)                \
    DO_2OP(gvec_f##FN##0_h, float16_##FN##0, float16)   \
    DO_2OP(gvec_f##FN##0_s, float32_##FN##0, float32)   \
    DO_2OP(gvec_f##FN##0_d, float64_##FN##0, float64)

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

static float64 float64_abd(float64 op1, float64 op2, float_status *stat)
{
    return float64_abs(float64_sub(op1, op2, stat));
}

/* ABD when FPCR.AH = 1: avoid flipping sign bit of a NaN result */
static float16 float16_ah_abd(float16 op1, float16 op2, float_status *stat)
{
    float16 r = float16_sub(op1, op2, stat);
    return float16_is_any_nan(r) ? r : float16_abs(r);
}

static float32 float32_ah_abd(float32 op1, float32 op2, float_status *stat)
{
    float32 r = float32_sub(op1, op2, stat);
    return float32_is_any_nan(r) ? r : float32_abs(r);
}

static float64 float64_ah_abd(float64 op1, float64 op2, float_status *stat)
{
    float64 r = float64_sub(op1, op2, stat);
    return float64_is_any_nan(r) ? r : float64_abs(r);
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
void HELPER(NAME)(void *vd, void *vn, void *vm,                            \
                  float_status *stat, uint32_t desc)                       \
{                                                                          \
    intptr_t i, oprsz = simd_oprsz(desc);                                  \
    TYPE *d = vd, *n = vn, *m = vm;                                        \
    for (i = 0; i < oprsz / sizeof(TYPE); i++) {                           \
        d[i] = FUNC(n[i], m[i], stat);                                     \
    }                                                                      \
    clear_tail(d, oprsz, simd_maxsz(desc));                                \
}

DO_3OP(gvec_fadd_b16, bfloat16_add, float16)
DO_3OP(gvec_fadd_h, float16_add, float16)
DO_3OP(gvec_fadd_s, float32_add, float32)
DO_3OP(gvec_fadd_d, float64_add, float64)
DO_3OP(gvec_bfadd, bfloat16_add, bfloat16)

DO_3OP(gvec_fsub_b16, bfloat16_sub, float16)
DO_3OP(gvec_fsub_h, float16_sub, float16)
DO_3OP(gvec_fsub_s, float32_sub, float32)
DO_3OP(gvec_fsub_d, float64_sub, float64)
DO_3OP(gvec_bfsub, bfloat16_sub, bfloat16)

DO_3OP(gvec_fmul_b16, bfloat16_mul, float16)
DO_3OP(gvec_fmul_h, float16_mul, float16)
DO_3OP(gvec_fmul_s, float32_mul, float32)
DO_3OP(gvec_fmul_d, float64_mul, float64)

DO_3OP(gvec_ftsmul_h, float16_ftsmul, float16)
DO_3OP(gvec_ftsmul_s, float32_ftsmul, float32)
DO_3OP(gvec_ftsmul_d, float64_ftsmul, float64)

DO_3OP(gvec_fabd_h, float16_abd, float16)
DO_3OP(gvec_fabd_s, float32_abd, float32)
DO_3OP(gvec_fabd_d, float64_abd, float64)

DO_3OP(gvec_ah_fabd_h, float16_ah_abd, float16)
DO_3OP(gvec_ah_fabd_s, float32_ah_abd, float32)
DO_3OP(gvec_ah_fabd_d, float64_ah_abd, float64)

DO_3OP(gvec_fceq_h, float16_ceq, float16)
DO_3OP(gvec_fceq_s, float32_ceq, float32)
DO_3OP(gvec_fceq_d, float64_ceq, float64)

DO_3OP(gvec_fcge_h, float16_cge, float16)
DO_3OP(gvec_fcge_s, float32_cge, float32)
DO_3OP(gvec_fcge_d, float64_cge, float64)

DO_3OP(gvec_fcgt_h, float16_cgt, float16)
DO_3OP(gvec_fcgt_s, float32_cgt, float32)
DO_3OP(gvec_fcgt_d, float64_cgt, float64)

DO_3OP(gvec_facge_h, float16_acge, float16)
DO_3OP(gvec_facge_s, float32_acge, float32)
DO_3OP(gvec_facge_d, float64_acge, float64)

DO_3OP(gvec_facgt_h, float16_acgt, float16)
DO_3OP(gvec_facgt_s, float32_acgt, float32)
DO_3OP(gvec_facgt_d, float64_acgt, float64)

DO_3OP(gvec_fmax_h, float16_max, float16)
DO_3OP(gvec_fmax_s, float32_max, float32)
DO_3OP(gvec_fmax_d, float64_max, float64)

DO_3OP(gvec_fmin_h, float16_min, float16)
DO_3OP(gvec_fmin_s, float32_min, float32)
DO_3OP(gvec_fmin_d, float64_min, float64)

DO_3OP(gvec_fmaxnum_h, float16_maxnum, float16)
DO_3OP(gvec_fmaxnum_s, float32_maxnum, float32)
DO_3OP(gvec_fmaxnum_d, float64_maxnum, float64)

DO_3OP(gvec_fminnum_h, float16_minnum, float16)
DO_3OP(gvec_fminnum_s, float32_minnum, float32)
DO_3OP(gvec_fminnum_d, float64_minnum, float64)

DO_3OP(gvec_recps_nf_h, float16_recps_nf, float16)
DO_3OP(gvec_recps_nf_s, float32_recps_nf, float32)

DO_3OP(gvec_rsqrts_nf_h, float16_rsqrts_nf, float16)
DO_3OP(gvec_rsqrts_nf_s, float32_rsqrts_nf, float32)

#ifdef TARGET_AARCH64
DO_3OP(gvec_fdiv_h, float16_div, float16)
DO_3OP(gvec_fdiv_s, float32_div, float32)
DO_3OP(gvec_fdiv_d, float64_div, float64)

DO_3OP(gvec_fmulx_h, helper_advsimd_mulxh, float16)
DO_3OP(gvec_fmulx_s, helper_vfp_mulxs, float32)
DO_3OP(gvec_fmulx_d, helper_vfp_mulxd, float64)

DO_3OP(gvec_recps_h, helper_recpsf_f16, float16)
DO_3OP(gvec_recps_s, helper_recpsf_f32, float32)
DO_3OP(gvec_recps_d, helper_recpsf_f64, float64)

DO_3OP(gvec_rsqrts_h, helper_rsqrtsf_f16, float16)
DO_3OP(gvec_rsqrts_s, helper_rsqrtsf_f32, float32)
DO_3OP(gvec_rsqrts_d, helper_rsqrtsf_f64, float64)

DO_3OP(gvec_ah_recps_h, helper_recpsf_ah_f16, float16)
DO_3OP(gvec_ah_recps_s, helper_recpsf_ah_f32, float32)
DO_3OP(gvec_ah_recps_d, helper_recpsf_ah_f64, float64)

DO_3OP(gvec_ah_rsqrts_h, helper_rsqrtsf_ah_f16, float16)
DO_3OP(gvec_ah_rsqrts_s, helper_rsqrtsf_ah_f32, float32)
DO_3OP(gvec_ah_rsqrts_d, helper_rsqrtsf_ah_f64, float64)

DO_3OP(gvec_ah_fmax_h, helper_vfp_ah_maxh, float16)
DO_3OP(gvec_ah_fmax_s, helper_vfp_ah_maxs, float32)
DO_3OP(gvec_ah_fmax_d, helper_vfp_ah_maxd, float64)

DO_3OP(gvec_ah_fmin_h, helper_vfp_ah_minh, float16)
DO_3OP(gvec_ah_fmin_s, helper_vfp_ah_mins, float32)
DO_3OP(gvec_ah_fmin_d, helper_vfp_ah_mind, float64)

DO_3OP(gvec_fmax_b16, bfloat16_max, bfloat16)
DO_3OP(gvec_fmin_b16, bfloat16_min, bfloat16)
DO_3OP(gvec_fmaxnum_b16, bfloat16_maxnum, bfloat16)
DO_3OP(gvec_fminnum_b16, bfloat16_minnum, bfloat16)
DO_3OP(gvec_ah_fmax_b16, helper_sme2_ah_fmax_b16, bfloat16)
DO_3OP(gvec_ah_fmin_b16, helper_sme2_ah_fmin_b16, bfloat16)

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

static bfloat16 bfloat16_muladd_f(bfloat16 dest, bfloat16 op1, bfloat16 op2,
                                  float_status *stat)
{
    return bfloat16_muladd(op1, op2, dest, 0, stat);
}

static float32 float32_muladd_f(float32 dest, float32 op1, float32 op2,
                                 float_status *stat)
{
    return float32_muladd(op1, op2, dest, 0, stat);
}

static float64 float64_muladd_f(float64 dest, float64 op1, float64 op2,
                                 float_status *stat)
{
    return float64_muladd(op1, op2, dest, 0, stat);
}

static float16 float16_mulsub_f(float16 dest, float16 op1, float16 op2,
                                 float_status *stat)
{
    return float16_muladd(float16_chs(op1), op2, dest, 0, stat);
}

static bfloat16 bfloat16_mulsub_f(bfloat16 dest, bfloat16 op1, bfloat16 op2,
                                  float_status *stat)
{
    return bfloat16_muladd(bfloat16_chs(op1), op2, dest, 0, stat);
}

static float32 float32_mulsub_f(float32 dest, float32 op1, float32 op2,
                                 float_status *stat)
{
    return float32_muladd(float32_chs(op1), op2, dest, 0, stat);
}

static float64 float64_mulsub_f(float64 dest, float64 op1, float64 op2,
                                 float_status *stat)
{
    return float64_muladd(float64_chs(op1), op2, dest, 0, stat);
}

static float16 float16_ah_mulsub_f(float16 dest, float16 op1, float16 op2,
                                 float_status *stat)
{
    return float16_muladd(op1, op2, dest, float_muladd_negate_product, stat);
}

static bfloat16 bfloat16_ah_mulsub_f(bfloat16 dest, bfloat16 op1, bfloat16 op2,
                                     float_status *stat)
{
    return bfloat16_muladd(op1, op2, dest, float_muladd_negate_product, stat);
}

static float32 float32_ah_mulsub_f(float32 dest, float32 op1, float32 op2,
                                 float_status *stat)
{
    return float32_muladd(op1, op2, dest, float_muladd_negate_product, stat);
}

static float64 float64_ah_mulsub_f(float64 dest, float64 op1, float64 op2,
                                 float_status *stat)
{
    return float64_muladd(op1, op2, dest, float_muladd_negate_product, stat);
}

#define DO_MULADD(NAME, FUNC, TYPE)                                        \
void HELPER(NAME)(void *vd, void *vn, void *vm,                            \
                  float_status *stat, uint32_t desc)                       \
{                                                                          \
    intptr_t i, oprsz = simd_oprsz(desc);                                  \
    TYPE *d = vd, *n = vn, *m = vm;                                        \
    for (i = 0; i < oprsz / sizeof(TYPE); i++) {                           \
        d[i] = FUNC(d[i], n[i], m[i], stat);                               \
    }                                                                      \
    clear_tail(d, oprsz, simd_maxsz(desc));                                \
}

DO_MULADD(gvec_fmla_nf_h, float16_muladd_nf, float16)
DO_MULADD(gvec_fmla_nf_s, float32_muladd_nf, float32)

DO_MULADD(gvec_fmls_nf_h, float16_mulsub_nf, float16)
DO_MULADD(gvec_fmls_nf_s, float32_mulsub_nf, float32)

DO_MULADD(gvec_vfma_h, float16_muladd_f, float16)
DO_MULADD(gvec_vfma_s, float32_muladd_f, float32)
DO_MULADD(gvec_vfma_d, float64_muladd_f, float64)
DO_MULADD(gvec_bfmla, bfloat16_muladd_f, bfloat16)

DO_MULADD(gvec_vfms_h, float16_mulsub_f, float16)
DO_MULADD(gvec_vfms_s, float32_mulsub_f, float32)
DO_MULADD(gvec_vfms_d, float64_mulsub_f, float64)
DO_MULADD(gvec_bfmls, bfloat16_mulsub_f, bfloat16)

DO_MULADD(gvec_ah_vfms_h, float16_ah_mulsub_f, float16)
DO_MULADD(gvec_ah_vfms_s, float32_ah_mulsub_f, float32)
DO_MULADD(gvec_ah_vfms_d, float64_ah_mulsub_f, float64)
DO_MULADD(gvec_ah_bfmls, bfloat16_ah_mulsub_f, bfloat16)

#undef DO_MULADD

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
DO_MUL_IDX(gvec_mul_idx_d, uint64_t, H8)

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
DO_MLA_IDX(gvec_mla_idx_d, uint64_t, +, H8)

DO_MLA_IDX(gvec_mls_idx_h, uint16_t, -, H2)
DO_MLA_IDX(gvec_mls_idx_s, uint32_t, -, H4)
DO_MLA_IDX(gvec_mls_idx_d, uint64_t, -, H8)

#undef DO_MLA_IDX

#define DO_FMUL_IDX(NAME, ADD, MUL, TYPE, H)                               \
void HELPER(NAME)(void *vd, void *vn, void *vm,                            \
                  float_status *stat, uint32_t desc)                       \
{                                                                          \
    intptr_t i, j, oprsz = simd_oprsz(desc);                               \
    intptr_t segment = MIN(16, oprsz) / sizeof(TYPE);                      \
    intptr_t idx = simd_data(desc);                                        \
    TYPE *d = vd, *n = vn, *m = vm;                                        \
    for (i = 0; i < oprsz / sizeof(TYPE); i += segment) {                  \
        TYPE mm = m[H(i + idx)];                                           \
        for (j = 0; j < segment; j++) {                                    \
            d[i + j] = ADD(d[i + j], MUL(n[i + j], mm, stat), stat);       \
        }                                                                  \
    }                                                                      \
    clear_tail(d, oprsz, simd_maxsz(desc));                                \
}

#define nop(N, M, S) (M)

DO_FMUL_IDX(gvec_fmul_idx_b16, nop, bfloat16_mul, float16, H2)
DO_FMUL_IDX(gvec_fmul_idx_h, nop, float16_mul, float16, H2)
DO_FMUL_IDX(gvec_fmul_idx_s, nop, float32_mul, float32, H4)
DO_FMUL_IDX(gvec_fmul_idx_d, nop, float64_mul, float64, H8)

#ifdef TARGET_AARCH64

DO_FMUL_IDX(gvec_fmulx_idx_h, nop, helper_advsimd_mulxh, float16, H2)
DO_FMUL_IDX(gvec_fmulx_idx_s, nop, helper_vfp_mulxs, float32, H4)
DO_FMUL_IDX(gvec_fmulx_idx_d, nop, helper_vfp_mulxd, float64, H8)

#endif

#undef nop

/*
 * Non-fused multiply-accumulate operations, for Neon. NB that unlike
 * the fused ops below they assume accumulate both from and into Vd.
 */
DO_FMUL_IDX(gvec_fmla_nf_idx_h, float16_add, float16_mul, float16, H2)
DO_FMUL_IDX(gvec_fmla_nf_idx_s, float32_add, float32_mul, float32, H4)
DO_FMUL_IDX(gvec_fmls_nf_idx_h, float16_sub, float16_mul, float16, H2)
DO_FMUL_IDX(gvec_fmls_nf_idx_s, float32_sub, float32_mul, float32, H4)

#undef DO_FMUL_IDX

#define DO_FMLA_IDX(NAME, TYPE, H, NEGX, NEGF)                             \
void HELPER(NAME)(void *vd, void *vn, void *vm, void *va,                  \
                  float_status *stat, uint32_t desc)                       \
{                                                                          \
    intptr_t i, j, oprsz = simd_oprsz(desc);                               \
    intptr_t segment = MIN(16, oprsz) / sizeof(TYPE);                      \
    intptr_t idx = simd_data(desc);                                        \
    TYPE *d = vd, *n = vn, *m = vm, *a = va;                               \
    for (i = 0; i < oprsz / sizeof(TYPE); i += segment) {                  \
        TYPE mm = m[H(i + idx)];                                           \
        for (j = 0; j < segment; j++) {                                    \
            d[i + j] = TYPE##_muladd(n[i + j] ^ NEGX, mm,                  \
                                     a[i + j], NEGF, stat);                \
        }                                                                  \
    }                                                                      \
    clear_tail(d, oprsz, simd_maxsz(desc));                                \
}

DO_FMLA_IDX(gvec_fmla_idx_h, float16, H2, 0, 0)
DO_FMLA_IDX(gvec_fmla_idx_s, float32, H4, 0, 0)
DO_FMLA_IDX(gvec_fmla_idx_d, float64, H8, 0, 0)
DO_FMLA_IDX(gvec_bfmla_idx, bfloat16, H2, 0, 0)

DO_FMLA_IDX(gvec_fmls_idx_h, float16, H2, INT16_MIN, 0)
DO_FMLA_IDX(gvec_fmls_idx_s, float32, H4, INT32_MIN, 0)
DO_FMLA_IDX(gvec_fmls_idx_d, float64, H8, INT64_MIN, 0)
DO_FMLA_IDX(gvec_bfmls_idx, bfloat16, H2, INT16_MIN, 0)

DO_FMLA_IDX(gvec_ah_fmls_idx_h, float16, H2, 0, float_muladd_negate_product)
DO_FMLA_IDX(gvec_ah_fmls_idx_s, float32, H4, 0, float_muladd_negate_product)
DO_FMLA_IDX(gvec_ah_fmls_idx_d, float64, H8, 0, float_muladd_negate_product)
DO_FMLA_IDX(gvec_ah_bfmls_idx, bfloat16, H2, 0, float_muladd_negate_product)

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

DO_SAT(gvec_usqadd_b, int, uint8_t, int8_t, +, 0, UINT8_MAX)
DO_SAT(gvec_usqadd_h, int, uint16_t, int16_t, +, 0, UINT16_MAX)
DO_SAT(gvec_usqadd_s, int64_t, uint32_t, int32_t, +, 0, UINT32_MAX)

DO_SAT(gvec_suqadd_b, int, int8_t, uint8_t, +, INT8_MIN, INT8_MAX)
DO_SAT(gvec_suqadd_h, int, int16_t, uint16_t, +, INT16_MIN, INT16_MAX)
DO_SAT(gvec_suqadd_s, int64_t, int32_t, uint32_t, +, INT32_MIN, INT32_MAX)

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

void HELPER(gvec_usqadd_d)(void *vd, void *vq, void *vn,
                           void *vm, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);
    uint64_t *d = vd, *n = vn, *m = vm;
    bool q = false;

    for (i = 0; i < oprsz / 8; i++) {
        uint64_t nn = n[i];
        int64_t mm = m[i];
        uint64_t dd = nn + mm;

        if (mm < 0) {
            if (nn < (uint64_t)-mm) {
                dd = 0;
                q = true;
            }
        } else {
            if (dd < nn) {
                dd = UINT64_MAX;
                q = true;
            }
        }
        d[i] = dd;
    }
    if (q) {
        uint32_t *qc = vq;
        qc[0] = 1;
    }
    clear_tail(d, oprsz, simd_maxsz(desc));
}

void HELPER(gvec_suqadd_d)(void *vd, void *vq, void *vn,
                           void *vm, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);
    uint64_t *d = vd, *n = vn, *m = vm;
    bool q = false;

    for (i = 0; i < oprsz / 8; i++) {
        int64_t nn = n[i];
        uint64_t mm = m[i];
        int64_t dd = nn + mm;

        if (mm > (uint64_t)(INT64_MAX - nn)) {
            dd = INT64_MAX;
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

static void do_fmlal(float32 *d, void *vn, void *vm,
                     CPUARMState *env, uint32_t desc,
                     ARMFPStatusFlavour fpst_idx,
                     uint64_t negx, int negf)
{
    float_status *fpst = &env->vfp.fp_status[fpst_idx];
    bool fz16 = env->vfp.fpcr & FPCR_FZ16;
    intptr_t i, oprsz = simd_oprsz(desc);
    int is_2 = extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    int is_q = oprsz == 16;
    uint64_t n_4, m_4;

    /*
     * Pre-load all of the f16 data, avoiding overlap issues.
     * Negate all inputs for AH=0 FMLSL at once.
     */
    n_4 = load4_f16(vn, is_q, is_2) ^ negx;
    m_4 = load4_f16(vm, is_q, is_2);

    for (i = 0; i < oprsz / 4; i++) {
        float32 n_1 = float16_to_float32_by_bits(n_4 >> (i * 16), fz16);
        float32 m_1 = float16_to_float32_by_bits(m_4 >> (i * 16), fz16);
        d[H4(i)] = float32_muladd(n_1, m_1, d[H4(i)], negf, fpst);
    }
    clear_tail(d, oprsz, simd_maxsz(desc));
}

void HELPER(gvec_fmlal_a32)(void *vd, void *vn, void *vm,
                            CPUARMState *env, uint32_t desc)
{
    bool is_s = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint64_t negx = is_s ? 0x8000800080008000ull : 0;

    do_fmlal(vd, vn, vm, env, desc, FPST_STD, negx, 0);
}

void HELPER(gvec_fmlal_a64)(void *vd, void *vn, void *vm,
                            CPUARMState *env, uint32_t desc)
{
    bool is_s = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint64_t negx = 0;
    int negf = 0;

    if (is_s) {
        if (env->vfp.fpcr & FPCR_AH) {
            negf = float_muladd_negate_product;
        } else {
            negx = 0x8000800080008000ull;
        }
    }
    do_fmlal(vd, vn, vm, env, desc, FPST_A64, negx, negf);
}

void HELPER(sve2_fmlal_zzzw_s)(void *vd, void *vn, void *vm, void *va,
                               CPUARMState *env, uint32_t desc)
{
    intptr_t i, oprsz = simd_oprsz(desc);
    bool is_s = extract32(desc, SIMD_DATA_SHIFT, 1);
    intptr_t sel = extract32(desc, SIMD_DATA_SHIFT + 1, 1) * sizeof(float16);
    bool za = extract32(desc, SIMD_DATA_SHIFT + 2, 1);
    float_status *status = &env->vfp.fp_status[za ? FPST_ZA : FPST_A64];
    bool fz16 = env->vfp.fpcr & FPCR_FZ16;
    int negx = 0, negf = 0;

    if (is_s) {
        if (env->vfp.fpcr & FPCR_AH) {
            negf = float_muladd_negate_product;
        } else {
            negx = 0x8000;
        }
    }

    for (i = 0; i < oprsz; i += sizeof(float32)) {
        float16 nn_16 = *(float16 *)(vn + H1_2(i + sel)) ^ negx;
        float16 mm_16 = *(float16 *)(vm + H1_2(i + sel));
        float32 nn = float16_to_float32_by_bits(nn_16, fz16);
        float32 mm = float16_to_float32_by_bits(mm_16, fz16);
        float32 aa = *(float32 *)(va + H1_4(i));

        *(float32 *)(vd + H1_4(i)) = float32_muladd(nn, mm, aa, negf, status);
    }
}

static void do_fmlal_idx(float32 *d, void *vn, void *vm,
                         CPUARMState *env, uint32_t desc,
                         ARMFPStatusFlavour fpst_idx,
                         uint64_t negx, int negf)
{
    float_status *fpst = &env->vfp.fp_status[fpst_idx];
    bool fz16 = env->vfp.fpcr & FPCR_FZ16;
    intptr_t i, oprsz = simd_oprsz(desc);
    int is_2 = extract32(desc, SIMD_DATA_SHIFT + 1, 1);
    int index = extract32(desc, SIMD_DATA_SHIFT + 2, 3);
    int is_q = oprsz == 16;
    uint64_t n_4;
    float32 m_1;

    /*
     * Pre-load all of the f16 data, avoiding overlap issues.
     * Negate all inputs for AH=0 FMLSL at once.
     */
    n_4 = load4_f16(vn, is_q, is_2) ^ negx;
    m_1 = float16_to_float32_by_bits(((float16 *)vm)[H2(index)], fz16);

    for (i = 0; i < oprsz / 4; i++) {
        float32 n_1 = float16_to_float32_by_bits(n_4 >> (i * 16), fz16);
        d[H4(i)] = float32_muladd(n_1, m_1, d[H4(i)], negf, fpst);
    }
    clear_tail(d, oprsz, simd_maxsz(desc));
}

void HELPER(gvec_fmlal_idx_a32)(void *vd, void *vn, void *vm,
                                CPUARMState *env, uint32_t desc)
{
    bool is_s = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint64_t negx = is_s ? 0x8000800080008000ull : 0;

    do_fmlal_idx(vd, vn, vm, env, desc, FPST_STD, negx, 0);
}

void HELPER(gvec_fmlal_idx_a64)(void *vd, void *vn, void *vm,
                                CPUARMState *env, uint32_t desc)
{
    bool is_s = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint64_t negx = 0;
    int negf = 0;

    if (is_s) {
        if (env->vfp.fpcr & FPCR_AH) {
            negf = float_muladd_negate_product;
        } else {
            negx = 0x8000800080008000ull;
        }
    }
    do_fmlal_idx(vd, vn, vm, env, desc, FPST_A64, negx, negf);
}

void HELPER(sve2_fmlal_zzxw_s)(void *vd, void *vn, void *vm, void *va,
                               CPUARMState *env, uint32_t desc)
{
    intptr_t i, j, oprsz = simd_oprsz(desc);
    bool is_s = extract32(desc, SIMD_DATA_SHIFT, 1);
    intptr_t sel = extract32(desc, SIMD_DATA_SHIFT + 1, 1) * sizeof(float16);
    bool za = extract32(desc, SIMD_DATA_SHIFT + 2, 1);
    intptr_t idx = extract32(desc, SIMD_DATA_SHIFT + 3, 3) * sizeof(float16);
    float_status *status = &env->vfp.fp_status[za ? FPST_ZA : FPST_A64];
    bool fz16 = env->vfp.fpcr & FPCR_FZ16;
    int negx = 0, negf = 0;

    if (is_s) {
        if (env->vfp.fpcr & FPCR_AH) {
            negf = float_muladd_negate_product;
        } else {
            negx = 0x8000;
        }
    }
    for (i = 0; i < oprsz; i += 16) {
        float16 mm_16 = *(float16 *)(vm + i + idx);
        float32 mm = float16_to_float32_by_bits(mm_16, fz16);

        for (j = 0; j < 16; j += sizeof(float32)) {
            float16 nn_16 = *(float16 *)(vn + H1_2(i + j + sel)) ^ negx;
            float32 nn = float16_to_float32_by_bits(nn_16, fz16);
            float32 aa = *(float32 *)(va + H1_4(i + j));

            *(float32 *)(vd + H1_4(i + j)) =
                float32_muladd(nn, mm, aa, negf, status);
        }
    }
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
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint64_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 8; ++i) {
        d[i] = clmul_8x8_low(n[i], m[i]);
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
    intptr_t i, opr_sz = simd_oprsz(desc);
    intptr_t hi = simd_data(desc);
    uint64_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 8; i += 2) {
        Int128 r = clmul_64(n[i + hi], m[i + hi]);
        d[i] = int128_getlo(r);
        d[i + 1] = int128_gethi(r);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(neon_pmull_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    int hi = simd_data(desc);
    uint64_t *d = vd, *n = vn, *m = vm;
    uint64_t nn = n[hi], mm = m[hi];

    d[0] = clmul_8x4_packed(nn, mm);
    nn >>= 32;
    mm >>= 32;
    d[1] = clmul_8x4_packed(nn, mm);

    clear_tail(d, 16, simd_maxsz(desc));
}

#ifdef TARGET_AARCH64
void HELPER(sve2_pmull_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    int shift = simd_data(desc) * 8;
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint64_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 8; ++i) {
        d[i] = clmul_8x4_even(n[i] >> shift, m[i] >> shift);
    }
}

void HELPER(sve2_pmull_d)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t sel = H4(simd_data(desc));
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint32_t *n = vn, *m = vm;
    uint64_t *d = vd;

    for (i = 0; i < opr_sz / 8; ++i) {
        d[i] = clmul_32(n[2 * i + sel], m[2 * i + sel]);
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

#define DO_3OP_PAIR(NAME, FUNC, TYPE, H) \
void HELPER(NAME)(void *vd, void *vn, void *vm,                            \
                  float_status *stat, uint32_t desc)                       \
{                                                                          \
    ARMVectorReg scratch;                                                  \
    intptr_t oprsz = simd_oprsz(desc);                                     \
    intptr_t half = oprsz / sizeof(TYPE) / 2;                              \
    TYPE *d = vd, *n = vn, *m = vm;                                        \
    if (unlikely(d == m)) {                                                \
        m = memcpy(&scratch, m, oprsz);                                    \
    }                                                                      \
    for (intptr_t i = 0; i < half; ++i) {                                  \
        d[H(i)] = FUNC(n[H(i * 2)], n[H(i * 2 + 1)], stat);                \
    }                                                                      \
    for (intptr_t i = 0; i < half; ++i) {                                  \
        d[H(i + half)] = FUNC(m[H(i * 2)], m[H(i * 2 + 1)], stat);         \
    }                                                                      \
    clear_tail(d, oprsz, simd_maxsz(desc));                                \
}

DO_3OP_PAIR(gvec_faddp_h, float16_add, float16, H2)
DO_3OP_PAIR(gvec_faddp_s, float32_add, float32, H4)
DO_3OP_PAIR(gvec_faddp_d, float64_add, float64, )

DO_3OP_PAIR(gvec_fmaxp_h, float16_max, float16, H2)
DO_3OP_PAIR(gvec_fmaxp_s, float32_max, float32, H4)
DO_3OP_PAIR(gvec_fmaxp_d, float64_max, float64, )

DO_3OP_PAIR(gvec_fminp_h, float16_min, float16, H2)
DO_3OP_PAIR(gvec_fminp_s, float32_min, float32, H4)
DO_3OP_PAIR(gvec_fminp_d, float64_min, float64, )

DO_3OP_PAIR(gvec_fmaxnump_h, float16_maxnum, float16, H2)
DO_3OP_PAIR(gvec_fmaxnump_s, float32_maxnum, float32, H4)
DO_3OP_PAIR(gvec_fmaxnump_d, float64_maxnum, float64, )

DO_3OP_PAIR(gvec_fminnump_h, float16_minnum, float16, H2)
DO_3OP_PAIR(gvec_fminnump_s, float32_minnum, float32, H4)
DO_3OP_PAIR(gvec_fminnump_d, float64_minnum, float64, )

#ifdef TARGET_AARCH64
DO_3OP_PAIR(gvec_ah_fmaxp_h, helper_vfp_ah_maxh, float16, H2)
DO_3OP_PAIR(gvec_ah_fmaxp_s, helper_vfp_ah_maxs, float32, H4)
DO_3OP_PAIR(gvec_ah_fmaxp_d, helper_vfp_ah_maxd, float64, )

DO_3OP_PAIR(gvec_ah_fminp_h, helper_vfp_ah_minh, float16, H2)
DO_3OP_PAIR(gvec_ah_fminp_s, helper_vfp_ah_mins, float32, H4)
DO_3OP_PAIR(gvec_ah_fminp_d, helper_vfp_ah_mind, float64, )
#endif

#undef DO_3OP_PAIR

#define DO_3OP_PAIR(NAME, FUNC, TYPE, H) \
void HELPER(NAME)(void *vd, void *vn, void *vm, uint32_t desc)  \
{                                                               \
    ARMVectorReg scratch;                                       \
    intptr_t oprsz = simd_oprsz(desc);                          \
    intptr_t half = oprsz / sizeof(TYPE) / 2;                   \
    TYPE *d = vd, *n = vn, *m = vm;                             \
    if (unlikely(d == m)) {                                     \
        m = memcpy(&scratch, m, oprsz);                         \
    }                                                           \
    for (intptr_t i = 0; i < half; ++i) {                       \
        d[H(i)] = FUNC(n[H(i * 2)], n[H(i * 2 + 1)]);           \
    }                                                           \
    for (intptr_t i = 0; i < half; ++i) {                       \
        d[H(i + half)] = FUNC(m[H(i * 2)], m[H(i * 2 + 1)]);    \
    }                                                           \
    clear_tail(d, oprsz, simd_maxsz(desc));                     \
}

#define ADD(A, B) (A + B)
DO_3OP_PAIR(gvec_addp_b, ADD, uint8_t, H1)
DO_3OP_PAIR(gvec_addp_h, ADD, uint16_t, H2)
DO_3OP_PAIR(gvec_addp_s, ADD, uint32_t, H4)
DO_3OP_PAIR(gvec_addp_d, ADD, uint64_t, )
#undef  ADD

DO_3OP_PAIR(gvec_smaxp_b, MAX, int8_t, H1)
DO_3OP_PAIR(gvec_smaxp_h, MAX, int16_t, H2)
DO_3OP_PAIR(gvec_smaxp_s, MAX, int32_t, H4)

DO_3OP_PAIR(gvec_umaxp_b, MAX, uint8_t, H1)
DO_3OP_PAIR(gvec_umaxp_h, MAX, uint16_t, H2)
DO_3OP_PAIR(gvec_umaxp_s, MAX, uint32_t, H4)

DO_3OP_PAIR(gvec_sminp_b, MIN, int8_t, H1)
DO_3OP_PAIR(gvec_sminp_h, MIN, int16_t, H2)
DO_3OP_PAIR(gvec_sminp_s, MIN, int32_t, H4)

DO_3OP_PAIR(gvec_uminp_b, MIN, uint8_t, H1)
DO_3OP_PAIR(gvec_uminp_h, MIN, uint16_t, H2)
DO_3OP_PAIR(gvec_uminp_s, MIN, uint32_t, H4)

#undef DO_3OP_PAIR

#define DO_VCVT_FIXED(NAME, FUNC, TYPE)                                 \
    void HELPER(NAME)(void *vd, void *vn, float_status *stat, uint32_t desc) \
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

DO_VCVT_FIXED(gvec_vcvt_sd, helper_vfp_sqtod, uint64_t)
DO_VCVT_FIXED(gvec_vcvt_ud, helper_vfp_uqtod, uint64_t)
DO_VCVT_FIXED(gvec_vcvt_sf, helper_vfp_sltos, uint32_t)
DO_VCVT_FIXED(gvec_vcvt_uf, helper_vfp_ultos, uint32_t)
DO_VCVT_FIXED(gvec_vcvt_sh, helper_vfp_shtoh, uint16_t)
DO_VCVT_FIXED(gvec_vcvt_uh, helper_vfp_uhtoh, uint16_t)

DO_VCVT_FIXED(gvec_vcvt_rz_ds, helper_vfp_tosqd_round_to_zero, uint64_t)
DO_VCVT_FIXED(gvec_vcvt_rz_du, helper_vfp_touqd_round_to_zero, uint64_t)
DO_VCVT_FIXED(gvec_vcvt_rz_fs, helper_vfp_tosls_round_to_zero, uint32_t)
DO_VCVT_FIXED(gvec_vcvt_rz_fu, helper_vfp_touls_round_to_zero, uint32_t)
DO_VCVT_FIXED(gvec_vcvt_rz_hs, helper_vfp_toshh_round_to_zero, uint16_t)
DO_VCVT_FIXED(gvec_vcvt_rz_hu, helper_vfp_touhh_round_to_zero, uint16_t)

#undef DO_VCVT_FIXED

#define DO_VCVT_RMODE(NAME, FUNC, TYPE)                                 \
    void HELPER(NAME)(void *vd, void *vn, float_status *fpst, uint32_t desc) \
    {                                                                   \
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

DO_VCVT_RMODE(gvec_vcvt_rm_sd, helper_vfp_tosqd, uint64_t)
DO_VCVT_RMODE(gvec_vcvt_rm_ud, helper_vfp_touqd, uint64_t)
DO_VCVT_RMODE(gvec_vcvt_rm_ss, helper_vfp_tosls, uint32_t)
DO_VCVT_RMODE(gvec_vcvt_rm_us, helper_vfp_touls, uint32_t)
DO_VCVT_RMODE(gvec_vcvt_rm_sh, helper_vfp_toshh, uint16_t)
DO_VCVT_RMODE(gvec_vcvt_rm_uh, helper_vfp_touhh, uint16_t)

#undef DO_VCVT_RMODE

#define DO_VRINT_RMODE(NAME, FUNC, TYPE)                                \
    void HELPER(NAME)(void *vd, void *vn, float_status *fpst, uint32_t desc) \
    {                                                                   \
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

#ifdef TARGET_AARCH64
void HELPER(simd_tblx)(void *vd, void *vm, CPUARMState *env, uint32_t desc)
{
    const uint8_t *indices = vm;
    size_t oprsz = simd_oprsz(desc);
    uint32_t rn = extract32(desc, SIMD_DATA_SHIFT, 5);
    bool is_tbx = extract32(desc, SIMD_DATA_SHIFT + 5, 1);
    uint32_t table_len = desc >> (SIMD_DATA_SHIFT + 6);
    union {
        uint8_t b[16];
        uint64_t d[2];
    } result;

    /*
     * We must construct the final result in a temp, lest the output
     * overlaps the input table.  For TBL, begin with zero; for TBX,
     * begin with the original register contents.  Note that we always
     * copy 16 bytes here to avoid an extra branch; clearing the high
     * bits of the register for oprsz == 8 is handled below.
     */
    if (is_tbx) {
        memcpy(&result, vd, 16);
    } else {
        memset(&result, 0, 16);
    }

    for (size_t i = 0; i < oprsz; ++i) {
        uint32_t index = indices[H1(i)];

        if (index < table_len) {
            /*
             * Convert index (a byte offset into the virtual table
             * which is a series of 128-bit vectors concatenated)
             * into the correct register element, bearing in mind
             * that the table can wrap around from V31 to V0.
             */
            const uint8_t *table = (const uint8_t *)
                aa64_vfp_qreg(env, (rn + (index >> 4)) % 32);
            result.b[H1(i)] = table[H1(index % 16)];
        }
    }

    memcpy(vd, &result, 16);
    clear_tail(vd, oprsz, simd_maxsz(desc));
}
#endif

/*
 * NxN -> N highpart multiply
 *
 * TODO: expose this as a generic vector operation.
 */

void HELPER(gvec_smulh_b)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int8_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz; ++i) {
        d[i] = ((int32_t)n[i] * m[i]) >> 8;
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_smulh_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int16_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 2; ++i) {
        d[i] = ((int32_t)n[i] * m[i]) >> 16;
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_smulh_s)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    int32_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 4; ++i) {
        d[i] = ((int64_t)n[i] * m[i]) >> 32;
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_smulh_d)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint64_t *d = vd, *n = vn, *m = vm;
    uint64_t discard;

    for (i = 0; i < opr_sz / 8; ++i) {
        muls64(&discard, &d[i], n[i], m[i]);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_umulh_b)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint8_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz; ++i) {
        d[i] = ((uint32_t)n[i] * m[i]) >> 8;
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_umulh_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint16_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 2; ++i) {
        d[i] = ((uint32_t)n[i] * m[i]) >> 16;
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_umulh_s)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint32_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 4; ++i) {
        d[i] = ((uint64_t)n[i] * m[i]) >> 32;
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_umulh_d)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint64_t *d = vd, *n = vn, *m = vm;
    uint64_t discard;

    for (i = 0; i < opr_sz / 8; ++i) {
        mulu64(&discard, &d[i], n[i], m[i]);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_xar_d)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc) / 8;
    int shr = simd_data(desc);
    uint64_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz; ++i) {
        d[i] = ror64(n[i] ^ m[i], shr);
    }
    clear_tail(d, opr_sz * 8, simd_maxsz(desc));
}

/*
 * Integer matrix-multiply accumulate
 */

static uint32_t do_smmla_b(uint32_t sum, void *vn, void *vm)
{
    int8_t *n = vn, *m = vm;

    for (intptr_t k = 0; k < 8; ++k) {
        sum += n[H1(k)] * m[H1(k)];
    }
    return sum;
}

static uint32_t do_ummla_b(uint32_t sum, void *vn, void *vm)
{
    uint8_t *n = vn, *m = vm;

    for (intptr_t k = 0; k < 8; ++k) {
        sum += n[H1(k)] * m[H1(k)];
    }
    return sum;
}

static uint32_t do_usmmla_b(uint32_t sum, void *vn, void *vm)
{
    uint8_t *n = vn;
    int8_t *m = vm;

    for (intptr_t k = 0; k < 8; ++k) {
        sum += n[H1(k)] * m[H1(k)];
    }
    return sum;
}

static void do_mmla_b(void *vd, void *vn, void *vm, void *va, uint32_t desc,
                      uint32_t (*inner_loop)(uint32_t, void *, void *))
{
    intptr_t seg, opr_sz = simd_oprsz(desc);

    for (seg = 0; seg < opr_sz; seg += 16) {
        uint32_t *d = vd + seg;
        uint32_t *a = va + seg;
        uint32_t sum0, sum1, sum2, sum3;

        /*
         * Process the entire segment at once, writing back the
         * results only after we've consumed all of the inputs.
         *
         * Key to indices by column:
         *          i   j                  i             j
         */
        sum0 = a[H4(0 + 0)];
        sum0 = inner_loop(sum0, vn + seg + 0, vm + seg + 0);
        sum1 = a[H4(0 + 1)];
        sum1 = inner_loop(sum1, vn + seg + 0, vm + seg + 8);
        sum2 = a[H4(2 + 0)];
        sum2 = inner_loop(sum2, vn + seg + 8, vm + seg + 0);
        sum3 = a[H4(2 + 1)];
        sum3 = inner_loop(sum3, vn + seg + 8, vm + seg + 8);

        d[H4(0)] = sum0;
        d[H4(1)] = sum1;
        d[H4(2)] = sum2;
        d[H4(3)] = sum3;
    }
    clear_tail(vd, opr_sz, simd_maxsz(desc));
}

#define DO_MMLA_B(NAME, INNER) \
    void HELPER(NAME)(void *vd, void *vn, void *vm, void *va, uint32_t desc) \
    { do_mmla_b(vd, vn, vm, va, desc, INNER); }

DO_MMLA_B(gvec_smmla_b, do_smmla_b)
DO_MMLA_B(gvec_ummla_b, do_ummla_b)
DO_MMLA_B(gvec_usmmla_b, do_usmmla_b)

/*
 * BFloat16 Dot Product
 */

bool is_ebf(CPUARMState *env, float_status *statusp, float_status *oddstatusp)
{
    /*
     * For BFDOT, BFMMLA, etc, the behaviour depends on FPCR.EBF.
     * For EBF = 0, we ignore the FPCR bits which determine rounding
     * mode and denormal-flushing, and we do unfused multiplies and
     * additions with intermediate rounding of all products and sums.
     * For EBF = 1, we honour FPCR rounding mode and denormal-flushing bits,
     * and we perform a fused two-way sum-of-products without intermediate
     * rounding of the products.
     * In either case, we don't set fp exception flags.
     *
     * EBF is AArch64 only, so even if it's set in the FPCR it has
     * no effect on AArch32 instructions.
     */
    bool ebf = is_a64(env) && env->vfp.fpcr & FPCR_EBF;

    *statusp = env->vfp.fp_status[is_a64(env) ? FPST_A64 : FPST_A32];
    set_default_nan_mode(true, statusp);

    if (ebf) {
        /* EBF=1 needs to do a step with round-to-odd semantics */
        *oddstatusp = *statusp;
        set_float_rounding_mode(float_round_to_odd, oddstatusp);
    } else {
        set_flush_to_zero(true, statusp);
        set_flush_inputs_to_zero(true, statusp);
        set_float_rounding_mode(float_round_to_odd_inf, statusp);
    }
    return ebf;
}

float32 bfdotadd(float32 sum, uint32_t e1, uint32_t e2, float_status *fpst)
{
    float32 t1, t2;

    /*
     * Extract each BFloat16 from the element pair, and shift
     * them such that they become float32.
     */
    t1 = float32_mul(e1 << 16, e2 << 16, fpst);
    t2 = float32_mul(e1 & 0xffff0000u, e2 & 0xffff0000u, fpst);
    t1 = float32_add(t1, t2, fpst);
    t1 = float32_add(sum, t1, fpst);

    return t1;
}

float32 bfdotadd_ebf(float32 sum, uint32_t e1, uint32_t e2,
                     float_status *fpst, float_status *fpst_odd)
{
    float32 s1r = e1 << 16;
    float32 s1c = e1 & 0xffff0000u;
    float32 s2r = e2 << 16;
    float32 s2c = e2 & 0xffff0000u;
    float32 t32;

    /* C.f. FPProcessNaNs4 */
    if (float32_is_any_nan(s1r) || float32_is_any_nan(s1c) ||
        float32_is_any_nan(s2r) || float32_is_any_nan(s2c)) {
        if (float32_is_signaling_nan(s1r, fpst)) {
            t32 = s1r;
        } else if (float32_is_signaling_nan(s1c, fpst)) {
            t32 = s1c;
        } else if (float32_is_signaling_nan(s2r, fpst)) {
            t32 = s2r;
        } else if (float32_is_signaling_nan(s2c, fpst)) {
            t32 = s2c;
        } else if (float32_is_any_nan(s1r)) {
            t32 = s1r;
        } else if (float32_is_any_nan(s1c)) {
            t32 = s1c;
        } else if (float32_is_any_nan(s2r)) {
            t32 = s2r;
        } else {
            t32 = s2c;
        }
        /*
         * FPConvertNaN(FPProcessNaN(t32)) will be done as part
         * of the final addition below.
         */
    } else {
        /*
         * Compare f16_dotadd() in sme_helper.c, but here we have
         * bfloat16 inputs. In particular that means that we do not
         * want the FPCR.FZ16 flush semantics, so we use the normal
         * float_status for the input handling here.
         */
        float64 e1r = float32_to_float64(s1r, fpst);
        float64 e1c = float32_to_float64(s1c, fpst);
        float64 e2r = float32_to_float64(s2r, fpst);
        float64 e2c = float32_to_float64(s2c, fpst);
        float64 t64;

        /*
         * The ARM pseudocode function FPDot performs both multiplies
         * and the add with a single rounding operation.  Emulate this
         * by performing the first multiply in round-to-odd, then doing
         * the second multiply as fused multiply-add, and rounding to
         * float32 all in one step.
         */
        t64 = float64_mul(e1r, e2r, fpst_odd);
        t64 = float64r32_muladd(e1c, e2c, t64, 0, fpst);

        /* This conversion is exact, because we've already rounded. */
        t32 = float64_to_float32(t64, fpst);
    }

    /* The final accumulation step is not fused. */
    return float32_add(sum, t32, fpst);
}

void HELPER(gvec_bfdot)(void *vd, void *vn, void *vm, void *va,
                        CPUARMState *env, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    float32 *d = vd, *a = va;
    uint32_t *n = vn, *m = vm;
    float_status fpst, fpst_odd;

    if (is_ebf(env, &fpst, &fpst_odd)) {
        for (i = 0; i < opr_sz / 4; ++i) {
            d[i] = bfdotadd_ebf(a[i], n[i], m[i], &fpst, &fpst_odd);
        }
    } else {
        for (i = 0; i < opr_sz / 4; ++i) {
            d[i] = bfdotadd(a[i], n[i], m[i], &fpst);
        }
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_bfdot_idx)(void *vd, void *vn, void *vm,
                            void *va, CPUARMState *env, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    intptr_t index = simd_data(desc);
    intptr_t elements = opr_sz / 4;
    intptr_t eltspersegment = MIN(16 / 4, elements);
    float32 *d = vd, *a = va;
    uint32_t *n = vn, *m = vm;
    float_status fpst, fpst_odd;

    if (is_ebf(env, &fpst, &fpst_odd)) {
        for (i = 0; i < elements; i += eltspersegment) {
            uint32_t m_idx = m[i + H4(index)];

            for (j = i; j < i + eltspersegment; j++) {
                d[j] = bfdotadd_ebf(a[j], n[j], m_idx, &fpst, &fpst_odd);
            }
        }
    } else {
        for (i = 0; i < elements; i += eltspersegment) {
            uint32_t m_idx = m[i + H4(index)];

            for (j = i; j < i + eltspersegment; j++) {
                d[j] = bfdotadd(a[j], n[j], m_idx, &fpst);
            }
        }
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(sme2_bfvdot_idx)(void *vd, void *vn, void *vm,
                             void *va, CPUARMState *env, uint32_t desc)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    intptr_t idx = extract32(desc, SIMD_DATA_SHIFT, 2);
    intptr_t sel = extract32(desc, SIMD_DATA_SHIFT + 2, 1);
    intptr_t elements = opr_sz / 4;
    intptr_t eltspersegment = MIN(16 / 4, elements);
    float32 *d = vd, *a = va;
    uint16_t *n0 = vn;
    uint16_t *n1 = vn + sizeof(ARMVectorReg);
    uint32_t *m = vm;
    float_status fpst, fpst_odd;

    if (is_ebf(env, &fpst, &fpst_odd)) {
        for (i = 0; i < elements; i += eltspersegment) {
            uint32_t m_idx = m[i + H4(idx)];

            for (j = 0; j < eltspersegment; j++) {
                uint32_t nn = (n0[H2(2 * (i + j) + sel)])
                            | (n1[H2(2 * (i + j) + sel)] << 16);
                d[i + H4(j)] = bfdotadd_ebf(a[i + H4(j)], nn, m_idx,
                                            &fpst, &fpst_odd);
            }
        }
    } else {
        for (i = 0; i < elements; i += eltspersegment) {
            uint32_t m_idx = m[i + H4(idx)];

            for (j = 0; j < eltspersegment; j++) {
                uint32_t nn = (n0[H2(2 * (i + j) + sel)])
                            | (n1[H2(2 * (i + j) + sel)] << 16);
                d[i + H4(j)] = bfdotadd(a[i + H4(j)], nn, m_idx, &fpst);
            }
        }
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_bfmmla)(void *vd, void *vn, void *vm, void *va,
                         CPUARMState *env, uint32_t desc)
{
    intptr_t s, opr_sz = simd_oprsz(desc);
    float32 *d = vd, *a = va;
    uint32_t *n = vn, *m = vm;
    float_status fpst, fpst_odd;

    if (is_ebf(env, &fpst, &fpst_odd)) {
        for (s = 0; s < opr_sz / 4; s += 4) {
            float32 sum00, sum01, sum10, sum11;

            /*
             * Process the entire segment at once, writing back the
             * results only after we've consumed all of the inputs.
             *
             * Key to indices by column:
             *               i   j               i   k             j   k
             */
            sum00 = a[s + H4(0 + 0)];
            sum00 = bfdotadd_ebf(sum00, n[s + H4(0 + 0)], m[s + H4(0 + 0)], &fpst, &fpst_odd);
            sum00 = bfdotadd_ebf(sum00, n[s + H4(0 + 1)], m[s + H4(0 + 1)], &fpst, &fpst_odd);

            sum01 = a[s + H4(0 + 1)];
            sum01 = bfdotadd_ebf(sum01, n[s + H4(0 + 0)], m[s + H4(2 + 0)], &fpst, &fpst_odd);
            sum01 = bfdotadd_ebf(sum01, n[s + H4(0 + 1)], m[s + H4(2 + 1)], &fpst, &fpst_odd);

            sum10 = a[s + H4(2 + 0)];
            sum10 = bfdotadd_ebf(sum10, n[s + H4(2 + 0)], m[s + H4(0 + 0)], &fpst, &fpst_odd);
            sum10 = bfdotadd_ebf(sum10, n[s + H4(2 + 1)], m[s + H4(0 + 1)], &fpst, &fpst_odd);

            sum11 = a[s + H4(2 + 1)];
            sum11 = bfdotadd_ebf(sum11, n[s + H4(2 + 0)], m[s + H4(2 + 0)], &fpst, &fpst_odd);
            sum11 = bfdotadd_ebf(sum11, n[s + H4(2 + 1)], m[s + H4(2 + 1)], &fpst, &fpst_odd);

            d[s + H4(0 + 0)] = sum00;
            d[s + H4(0 + 1)] = sum01;
            d[s + H4(2 + 0)] = sum10;
            d[s + H4(2 + 1)] = sum11;
        }
    } else {
        for (s = 0; s < opr_sz / 4; s += 4) {
            float32 sum00, sum01, sum10, sum11;

            /*
             * Process the entire segment at once, writing back the
             * results only after we've consumed all of the inputs.
             *
             * Key to indices by column:
             *               i   j           i   k             j   k
             */
            sum00 = a[s + H4(0 + 0)];
            sum00 = bfdotadd(sum00, n[s + H4(0 + 0)], m[s + H4(0 + 0)], &fpst);
            sum00 = bfdotadd(sum00, n[s + H4(0 + 1)], m[s + H4(0 + 1)], &fpst);

            sum01 = a[s + H4(0 + 1)];
            sum01 = bfdotadd(sum01, n[s + H4(0 + 0)], m[s + H4(2 + 0)], &fpst);
            sum01 = bfdotadd(sum01, n[s + H4(0 + 1)], m[s + H4(2 + 1)], &fpst);

            sum10 = a[s + H4(2 + 0)];
            sum10 = bfdotadd(sum10, n[s + H4(2 + 0)], m[s + H4(0 + 0)], &fpst);
            sum10 = bfdotadd(sum10, n[s + H4(2 + 1)], m[s + H4(0 + 1)], &fpst);

            sum11 = a[s + H4(2 + 1)];
            sum11 = bfdotadd(sum11, n[s + H4(2 + 0)], m[s + H4(2 + 0)], &fpst);
            sum11 = bfdotadd(sum11, n[s + H4(2 + 1)], m[s + H4(2 + 1)], &fpst);

            d[s + H4(0 + 0)] = sum00;
            d[s + H4(0 + 1)] = sum01;
            d[s + H4(2 + 0)] = sum10;
            d[s + H4(2 + 1)] = sum11;
        }
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

static void do_bfmlal(float32 *d, bfloat16 *n, bfloat16 *m, float32 *a,
                      float_status *stat, uint32_t desc, int negx, int negf)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    intptr_t sel = extract32(desc, SIMD_DATA_SHIFT, 1);

    for (i = 0; i < opr_sz / 4; ++i) {
        float32 nn = (negx ^ n[H2(i * 2 + sel)]) << 16;
        float32 mm = m[H2(i * 2 + sel)] << 16;
        d[H4(i)] = float32_muladd(nn, mm, a[H4(i)], negf, stat);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_bfmlal)(void *vd, void *vn, void *vm, void *va,
                         float_status *stat, uint32_t desc)
{
    do_bfmlal(vd, vn, vm, va, stat, desc, 0, 0);
}

void HELPER(gvec_bfmlsl)(void *vd, void *vn, void *vm, void *va,
                         float_status *stat, uint32_t desc)
{
    do_bfmlal(vd, vn, vm, va, stat, desc, 0x8000, 0);
}

void HELPER(gvec_ah_bfmlsl)(void *vd, void *vn, void *vm, void *va,
                            float_status *stat, uint32_t desc)
{
    do_bfmlal(vd, vn, vm, va, stat, desc, 0, float_muladd_negate_product);
}

static void do_bfmlal_idx(float32 *d, bfloat16 *n, bfloat16 *m, float32 *a,
                          float_status *stat, uint32_t desc, int negx, int negf)
{
    intptr_t i, j, opr_sz = simd_oprsz(desc);
    intptr_t sel = extract32(desc, SIMD_DATA_SHIFT, 1);
    intptr_t index = extract32(desc, SIMD_DATA_SHIFT + 1, 3);
    intptr_t elements = opr_sz / 4;
    intptr_t eltspersegment = MIN(16 / 4, elements);

    for (i = 0; i < elements; i += eltspersegment) {
        float32 m_idx = m[H2(2 * i + index)] << 16;

        for (j = i; j < i + eltspersegment; j++) {
            float32 n_j = (negx ^ n[H2(2 * j + sel)]) << 16;
            d[H4(j)] = float32_muladd(n_j, m_idx, a[H4(j)], negf, stat);
        }
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_bfmlal_idx)(void *vd, void *vn, void *vm, void *va,
                             float_status *stat, uint32_t desc)
{
    do_bfmlal_idx(vd, vn, vm, va, stat, desc, 0, 0);
}

void HELPER(gvec_bfmlsl_idx)(void *vd, void *vn, void *vm, void *va,
                             float_status *stat, uint32_t desc)
{
    do_bfmlal_idx(vd, vn, vm, va, stat, desc, 0x8000, 0);
}

void HELPER(gvec_ah_bfmlsl_idx)(void *vd, void *vn, void *vm, void *va,
                                float_status *stat, uint32_t desc)
{
    do_bfmlal_idx(vd, vn, vm, va, stat, desc, 0, float_muladd_negate_product);
}

#define DO_CLAMP(NAME, TYPE) \
void HELPER(NAME)(void *d, void *n, void *m, void *a, uint32_t desc)    \
{                                                                       \
    intptr_t i, opr_sz = simd_oprsz(desc);                              \
    for (i = 0; i < opr_sz; i += sizeof(TYPE)) {                        \
        TYPE aa = *(TYPE *)(a + i);                                     \
        TYPE nn = *(TYPE *)(n + i);                                     \
        TYPE mm = *(TYPE *)(m + i);                                     \
        TYPE dd = MIN(MAX(aa, nn), mm);                                 \
        *(TYPE *)(d + i) = dd;                                          \
    }                                                                   \
    clear_tail(d, opr_sz, simd_maxsz(desc));                            \
}

DO_CLAMP(gvec_sclamp_b, int8_t)
DO_CLAMP(gvec_sclamp_h, int16_t)
DO_CLAMP(gvec_sclamp_s, int32_t)
DO_CLAMP(gvec_sclamp_d, int64_t)

DO_CLAMP(gvec_uclamp_b, uint8_t)
DO_CLAMP(gvec_uclamp_h, uint16_t)
DO_CLAMP(gvec_uclamp_s, uint32_t)
DO_CLAMP(gvec_uclamp_d, uint64_t)

/* Bit count in each 8-bit word. */
void HELPER(gvec_cnt_b)(void *vd, void *vn, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint8_t *d = vd, *n = vn;

    for (i = 0; i < opr_sz; ++i) {
        d[i] = ctpop8(n[i]);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

/* Reverse bits in each 8 bit word */
void HELPER(gvec_rbit_b)(void *vd, void *vn, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint64_t *d = vd, *n = vn;

    for (i = 0; i < opr_sz / 8; ++i) {
        d[i] = revbit64(bswap64(n[i]));
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_urecpe_s)(void *vd, void *vn, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint32_t *d = vd, *n = vn;

    for (i = 0; i < opr_sz / 4; ++i) {
        d[i] = helper_recpe_u32(n[i]);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

void HELPER(gvec_ursqrte_s)(void *vd, void *vn, uint32_t desc)
{
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint32_t *d = vd, *n = vn;

    for (i = 0; i < opr_sz / 4; ++i) {
        d[i] = helper_rsqrte_u32(n[i]);
    }
    clear_tail(d, opr_sz, simd_maxsz(desc));
}

static inline void do_lut_b(void *zd, uint64_t *indexes, uint64_t *table,
                            unsigned elements, unsigned segbase,
                            unsigned dstride, unsigned isize,
                            unsigned tsize, unsigned nreg)
{
    for (unsigned r = 0; r < nreg; ++r) {
        uint8_t *dst = zd + dstride * r;
        unsigned base = segbase + r * elements;

        for (unsigned e = 0; e < elements; ++e) {
            unsigned index = extractn(indexes, (base + e) * isize, isize);
            dst[H1(e)] = extractn(table, index * tsize, 8);
        }
    }
}

static inline void do_lut_h(void *zd, uint64_t *indexes, uint64_t *table,
                            unsigned elements, unsigned segbase,
                            unsigned dstride, unsigned isize,
                            unsigned tsize, unsigned nreg)
{
    for (unsigned r = 0; r < nreg; ++r) {
        uint16_t *dst = zd + dstride * r;
        unsigned base = segbase + r * elements;

        for (unsigned e = 0; e < elements; ++e) {
            unsigned index = extractn(indexes, (base + e) * isize, isize);
            dst[H2(e)] = extractn(table, index * tsize, 16);
        }
    }
}

static inline void do_lut_s(void *zd, uint64_t *indexes, uint32_t *table,
                            unsigned elements, unsigned segbase,
                            unsigned dstride, unsigned isize,
                            unsigned tsize, unsigned nreg)
{
    for (unsigned r = 0; r < nreg; ++r) {
        uint32_t *dst = zd + dstride * r;
        unsigned base = segbase + r * elements;

        for (unsigned e = 0; e < elements; ++e) {
            unsigned index = extractn(indexes, (base + e) * isize, isize);
            dst[H4(e)] = table[H4(index)];
        }
    }
}

#define DO_SME2_LUT(ISIZE, NREG, SUFF, ESIZE) \
void helper_sme2_luti##ISIZE##_##NREG##SUFF                             \
    (void *zd, void *zn, CPUARMState *env, uint32_t desc)               \
{                                                                       \
    unsigned vl = simd_oprsz(desc);                                     \
    unsigned strided = extract32(desc, SIMD_DATA_SHIFT, 1);             \
    unsigned idx = extract32(desc, SIMD_DATA_SHIFT + 1, 4);             \
    unsigned elements = vl / ESIZE;                                     \
    unsigned dstride = (!strided ? 1 : NREG == 4 ? 4 : 8);              \
    unsigned segments = (ESIZE * 8) / (ISIZE * NREG);                   \
    unsigned segment = idx & (segments - 1);                            \
    ARMVectorReg indexes;                                               \
    memcpy(&indexes, zn, vl);                                           \
    do_lut_##SUFF(zd, indexes.d, (void *)env->za_state.zt0, elements,   \
                  segment * NREG * elements,                            \
                  dstride * sizeof(ARMVectorReg), ISIZE, 32, NREG);     \
}

DO_SME2_LUT(2,1,b, 1)
DO_SME2_LUT(2,1,h, 2)
DO_SME2_LUT(2,1,s, 4)
DO_SME2_LUT(2,2,b, 1)
DO_SME2_LUT(2,2,h, 2)
DO_SME2_LUT(2,2,s, 4)
DO_SME2_LUT(2,4,b, 1)
DO_SME2_LUT(2,4,h, 2)
DO_SME2_LUT(2,4,s, 4)

DO_SME2_LUT(4,1,b, 1)
DO_SME2_LUT(4,1,h, 2)
DO_SME2_LUT(4,1,s, 4)
DO_SME2_LUT(4,2,b, 1)
DO_SME2_LUT(4,2,h, 2)
DO_SME2_LUT(4,2,s, 4)
DO_SME2_LUT(4,4,h, 2)
DO_SME2_LUT(4,4,s, 4)

#undef DO_SME2_LUT
