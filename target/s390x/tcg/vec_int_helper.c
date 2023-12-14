/*
 * QEMU TCG support -- s390x vector integer instruction support
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
#include "cpu.h"
#include "vec.h"
#include "exec/helper-proto.h"
#include "tcg/tcg-gvec-desc.h"
#include "crypto/clmul.h"

static bool s390_vec_is_zero(const S390Vector *v)
{
    return !v->doubleword[0] && !v->doubleword[1];
}

static void s390_vec_and(S390Vector *res, const S390Vector *a,
                         const S390Vector *b)
{
    res->doubleword[0] = a->doubleword[0] & b->doubleword[0];
    res->doubleword[1] = a->doubleword[1] & b->doubleword[1];
}

static bool s390_vec_equal(const S390Vector *a, const S390Vector *b)
{
    return a->doubleword[0] == b->doubleword[0] &&
           a->doubleword[1] == b->doubleword[1];
}

static void s390_vec_shl(S390Vector *d, const S390Vector *a, uint64_t count)
{
    uint64_t tmp;

    g_assert(count < 128);
    if (count == 0) {
        d->doubleword[0] = a->doubleword[0];
        d->doubleword[1] = a->doubleword[1];
    } else if (count == 64) {
        d->doubleword[0] = a->doubleword[1];
        d->doubleword[1] = 0;
    } else if (count < 64) {
        tmp = extract64(a->doubleword[1], 64 - count, count);
        d->doubleword[1] = a->doubleword[1] << count;
        d->doubleword[0] = (a->doubleword[0] << count) | tmp;
    } else {
        d->doubleword[0] = a->doubleword[1] << (count - 64);
        d->doubleword[1] = 0;
    }
}

static void s390_vec_sar(S390Vector *d, const S390Vector *a, uint64_t count)
{
    uint64_t tmp;

    if (count == 0) {
        d->doubleword[0] = a->doubleword[0];
        d->doubleword[1] = a->doubleword[1];
    } else if (count == 64) {
        tmp = (int64_t)a->doubleword[0] >> 63;
        d->doubleword[1] = a->doubleword[0];
        d->doubleword[0] = tmp;
    } else if (count < 64) {
        tmp = a->doubleword[1] >> count;
        d->doubleword[1] = deposit64(tmp, 64 - count, count, a->doubleword[0]);
        d->doubleword[0] = (int64_t)a->doubleword[0] >> count;
    } else {
        tmp = (int64_t)a->doubleword[0] >> 63;
        d->doubleword[1] = (int64_t)a->doubleword[0] >> (count - 64);
        d->doubleword[0] = tmp;
    }
}

static void s390_vec_shr(S390Vector *d, const S390Vector *a, uint64_t count)
{
    uint64_t tmp;

    g_assert(count < 128);
    if (count == 0) {
        d->doubleword[0] = a->doubleword[0];
        d->doubleword[1] = a->doubleword[1];
    } else if (count == 64) {
        d->doubleword[1] = a->doubleword[0];
        d->doubleword[0] = 0;
    } else if (count < 64) {
        tmp = a->doubleword[1] >> count;
        d->doubleword[1] = deposit64(tmp, 64 - count, count, a->doubleword[0]);
        d->doubleword[0] = a->doubleword[0] >> count;
    } else {
        d->doubleword[1] = a->doubleword[0] >> (count - 64);
        d->doubleword[0] = 0;
    }
}
#define DEF_VAVG(BITS)                                                         \
void HELPER(gvec_vavg##BITS)(void *v1, const void *v2, const void *v3,         \
                             uint32_t desc)                                    \
{                                                                              \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const int32_t a = (int##BITS##_t)s390_vec_read_element##BITS(v2, i);   \
        const int32_t b = (int##BITS##_t)s390_vec_read_element##BITS(v3, i);   \
                                                                               \
        s390_vec_write_element##BITS(v1, i, (a + b + 1) >> 1);                 \
    }                                                                          \
}
DEF_VAVG(8)
DEF_VAVG(16)

#define DEF_VAVGL(BITS)                                                        \
void HELPER(gvec_vavgl##BITS)(void *v1, const void *v2, const void *v3,        \
                              uint32_t desc)                                   \
{                                                                              \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const uint##BITS##_t a = s390_vec_read_element##BITS(v2, i);           \
        const uint##BITS##_t b = s390_vec_read_element##BITS(v3, i);           \
                                                                               \
        s390_vec_write_element##BITS(v1, i, (a + b + 1) >> 1);                 \
    }                                                                          \
}
DEF_VAVGL(8)
DEF_VAVGL(16)

#define DEF_VCLZ(BITS)                                                         \
void HELPER(gvec_vclz##BITS)(void *v1, const void *v2, uint32_t desc)          \
{                                                                              \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const uint##BITS##_t a = s390_vec_read_element##BITS(v2, i);           \
                                                                               \
        s390_vec_write_element##BITS(v1, i, clz32(a) - 32 + BITS);             \
    }                                                                          \
}
DEF_VCLZ(8)
DEF_VCLZ(16)

#define DEF_VCTZ(BITS)                                                         \
void HELPER(gvec_vctz##BITS)(void *v1, const void *v2, uint32_t desc)          \
{                                                                              \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const uint##BITS##_t a = s390_vec_read_element##BITS(v2, i);           \
                                                                               \
        s390_vec_write_element##BITS(v1, i, a ? ctz32(a) : BITS);              \
    }                                                                          \
}
DEF_VCTZ(8)
DEF_VCTZ(16)

/* like binary multiplication, but XOR instead of addition */

/*
 * There is no carry across the two doublewords, so their order does
 * not matter.  Nor is there partial overlap between registers.
 */
static inline uint64_t do_gfma8(uint64_t n, uint64_t m, uint64_t a)
{
    return clmul_8x4_even(n, m) ^ clmul_8x4_odd(n, m) ^ a;
}

void HELPER(gvec_vgfm8)(void *v1, const void *v2, const void *v3, uint32_t d)
{
    uint64_t *q1 = v1;
    const uint64_t *q2 = v2, *q3 = v3;

    q1[0] = do_gfma8(q2[0], q3[0], 0);
    q1[1] = do_gfma8(q2[1], q3[1], 0);
}

void HELPER(gvec_vgfma8)(void *v1, const void *v2, const void *v3,
                         const void *v4, uint32_t desc)
{
    uint64_t *q1 = v1;
    const uint64_t *q2 = v2, *q3 = v3, *q4 = v4;

    q1[0] = do_gfma8(q2[0], q3[0], q4[0]);
    q1[1] = do_gfma8(q2[1], q3[1], q4[1]);
}

static inline uint64_t do_gfma16(uint64_t n, uint64_t m, uint64_t a)
{
    return clmul_16x2_even(n, m) ^ clmul_16x2_odd(n, m) ^ a;
}

void HELPER(gvec_vgfm16)(void *v1, const void *v2, const void *v3, uint32_t d)
{
    uint64_t *q1 = v1;
    const uint64_t *q2 = v2, *q3 = v3;

    q1[0] = do_gfma16(q2[0], q3[0], 0);
    q1[1] = do_gfma16(q2[1], q3[1], 0);
}

void HELPER(gvec_vgfma16)(void *v1, const void *v2, const void *v3,
                         const void *v4, uint32_t d)
{
    uint64_t *q1 = v1;
    const uint64_t *q2 = v2, *q3 = v3, *q4 = v4;

    q1[0] = do_gfma16(q2[0], q3[0], q4[0]);
    q1[1] = do_gfma16(q2[1], q3[1], q4[1]);
}

static inline uint64_t do_gfma32(uint64_t n, uint64_t m, uint64_t a)
{
    return clmul_32(n, m) ^ clmul_32(n >> 32, m >> 32) ^ a;
}

void HELPER(gvec_vgfm32)(void *v1, const void *v2, const void *v3, uint32_t d)
{
    uint64_t *q1 = v1;
    const uint64_t *q2 = v2, *q3 = v3;

    q1[0] = do_gfma32(q2[0], q3[0], 0);
    q1[1] = do_gfma32(q2[1], q3[1], 0);
}

void HELPER(gvec_vgfma32)(void *v1, const void *v2, const void *v3,
                         const void *v4, uint32_t d)
{
    uint64_t *q1 = v1;
    const uint64_t *q2 = v2, *q3 = v3, *q4 = v4;

    q1[0] = do_gfma32(q2[0], q3[0], q4[0]);
    q1[1] = do_gfma32(q2[1], q3[1], q4[1]);
}

void HELPER(gvec_vgfm64)(void *v1, const void *v2, const void *v3,
                         uint32_t desc)
{
    uint64_t *q1 = v1;
    const uint64_t *q2 = v2, *q3 = v3;
    Int128 r;

    r = int128_xor(clmul_64(q2[0], q3[0]), clmul_64(q2[1], q3[1]));
    q1[0] = int128_gethi(r);
    q1[1] = int128_getlo(r);
}

void HELPER(gvec_vgfma64)(void *v1, const void *v2, const void *v3,
                          const void *v4, uint32_t desc)
{
    uint64_t *q1 = v1;
    const uint64_t *q2 = v2, *q3 = v3, *q4 = v4;
    Int128 r;

    r = int128_xor(clmul_64(q2[0], q3[0]), clmul_64(q2[1], q3[1]));
    q1[0] = q4[0] ^ int128_gethi(r);
    q1[1] = q4[1] ^ int128_getlo(r);
}

#define DEF_VMAL(BITS)                                                         \
void HELPER(gvec_vmal##BITS)(void *v1, const void *v2, const void *v3,         \
                             const void *v4, uint32_t desc)                    \
{                                                                              \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const uint##BITS##_t a = s390_vec_read_element##BITS(v2, i);           \
        const uint##BITS##_t b = s390_vec_read_element##BITS(v3, i);           \
        const uint##BITS##_t c = s390_vec_read_element##BITS(v4, i);           \
                                                                               \
        s390_vec_write_element##BITS(v1, i, a * b + c);                        \
    }                                                                          \
}
DEF_VMAL(8)
DEF_VMAL(16)

#define DEF_VMAH(BITS)                                                         \
void HELPER(gvec_vmah##BITS)(void *v1, const void *v2, const void *v3,         \
                             const void *v4, uint32_t desc)                    \
{                                                                              \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const int32_t a = (int##BITS##_t)s390_vec_read_element##BITS(v2, i);   \
        const int32_t b = (int##BITS##_t)s390_vec_read_element##BITS(v3, i);   \
        const int32_t c = (int##BITS##_t)s390_vec_read_element##BITS(v4, i);   \
                                                                               \
        s390_vec_write_element##BITS(v1, i, (a * b + c) >> BITS);              \
    }                                                                          \
}
DEF_VMAH(8)
DEF_VMAH(16)

#define DEF_VMALH(BITS)                                                        \
void HELPER(gvec_vmalh##BITS)(void *v1, const void *v2, const void *v3,        \
                              const void *v4, uint32_t desc)                   \
{                                                                              \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const uint##BITS##_t a = s390_vec_read_element##BITS(v2, i);           \
        const uint##BITS##_t b = s390_vec_read_element##BITS(v3, i);           \
        const uint##BITS##_t c = s390_vec_read_element##BITS(v4, i);           \
                                                                               \
        s390_vec_write_element##BITS(v1, i, (a * b + c) >> BITS);              \
    }                                                                          \
}
DEF_VMALH(8)
DEF_VMALH(16)

#define DEF_VMAE(BITS, TBITS)                                                  \
void HELPER(gvec_vmae##BITS)(void *v1, const void *v2, const void *v3,         \
                             const void *v4, uint32_t desc)                    \
{                                                                              \
    int i, j;                                                                  \
                                                                               \
    for (i = 0, j = 0; i < (128 / TBITS); i++, j += 2) {                       \
        int##TBITS##_t a = (int##BITS##_t)s390_vec_read_element##BITS(v2, j);  \
        int##TBITS##_t b = (int##BITS##_t)s390_vec_read_element##BITS(v3, j);  \
        int##TBITS##_t c = s390_vec_read_element##TBITS(v4, i);                \
                                                                               \
        s390_vec_write_element##TBITS(v1, i, a * b + c);                       \
    }                                                                          \
}
DEF_VMAE(8, 16)
DEF_VMAE(16, 32)
DEF_VMAE(32, 64)

#define DEF_VMALE(BITS, TBITS)                                                 \
void HELPER(gvec_vmale##BITS)(void *v1, const void *v2, const void *v3,        \
                              const void *v4, uint32_t desc)                   \
{                                                                              \
    int i, j;                                                                  \
                                                                               \
    for (i = 0, j = 0; i < (128 / TBITS); i++, j += 2) {                       \
        uint##TBITS##_t a = s390_vec_read_element##BITS(v2, j);                \
        uint##TBITS##_t b = s390_vec_read_element##BITS(v3, j);                \
        uint##TBITS##_t c = s390_vec_read_element##TBITS(v4, i);               \
                                                                               \
        s390_vec_write_element##TBITS(v1, i, a * b + c);                       \
    }                                                                          \
}
DEF_VMALE(8, 16)
DEF_VMALE(16, 32)
DEF_VMALE(32, 64)

#define DEF_VMAO(BITS, TBITS)                                                  \
void HELPER(gvec_vmao##BITS)(void *v1, const void *v2, const void *v3,         \
                             const void *v4, uint32_t desc)                    \
{                                                                              \
    int i, j;                                                                  \
                                                                               \
    for (i = 0, j = 1; i < (128 / TBITS); i++, j += 2) {                       \
        int##TBITS##_t a = (int##BITS##_t)s390_vec_read_element##BITS(v2, j);  \
        int##TBITS##_t b = (int##BITS##_t)s390_vec_read_element##BITS(v3, j);  \
        int##TBITS##_t c = s390_vec_read_element##TBITS(v4, i);                \
                                                                               \
        s390_vec_write_element##TBITS(v1, i, a * b + c);                       \
    }                                                                          \
}
DEF_VMAO(8, 16)
DEF_VMAO(16, 32)
DEF_VMAO(32, 64)

#define DEF_VMALO(BITS, TBITS)                                                 \
void HELPER(gvec_vmalo##BITS)(void *v1, const void *v2, const void *v3,        \
                              const void *v4, uint32_t desc)                   \
{                                                                              \
    int i, j;                                                                  \
                                                                               \
    for (i = 0, j = 1; i < (128 / TBITS); i++, j += 2) {                       \
        uint##TBITS##_t a = s390_vec_read_element##BITS(v2, j);                \
        uint##TBITS##_t b = s390_vec_read_element##BITS(v3, j);                \
        uint##TBITS##_t c = s390_vec_read_element##TBITS(v4, i);               \
                                                                               \
        s390_vec_write_element##TBITS(v1, i, a * b + c);                       \
    }                                                                          \
}
DEF_VMALO(8, 16)
DEF_VMALO(16, 32)
DEF_VMALO(32, 64)

#define DEF_VMH(BITS)                                                          \
void HELPER(gvec_vmh##BITS)(void *v1, const void *v2, const void *v3,          \
                            uint32_t desc)                                     \
{                                                                              \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const int32_t a = (int##BITS##_t)s390_vec_read_element##BITS(v2, i);   \
        const int32_t b = (int##BITS##_t)s390_vec_read_element##BITS(v3, i);   \
                                                                               \
        s390_vec_write_element##BITS(v1, i, (a * b) >> BITS);                  \
    }                                                                          \
}
DEF_VMH(8)
DEF_VMH(16)

#define DEF_VMLH(BITS)                                                         \
void HELPER(gvec_vmlh##BITS)(void *v1, const void *v2, const void *v3,         \
                             uint32_t desc)                                    \
{                                                                              \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const uint##BITS##_t a = s390_vec_read_element##BITS(v2, i);           \
        const uint##BITS##_t b = s390_vec_read_element##BITS(v3, i);           \
                                                                               \
        s390_vec_write_element##BITS(v1, i, (a * b) >> BITS);                  \
    }                                                                          \
}
DEF_VMLH(8)
DEF_VMLH(16)

#define DEF_VME(BITS, TBITS)                                                   \
void HELPER(gvec_vme##BITS)(void *v1, const void *v2, const void *v3,          \
                            uint32_t desc)                                     \
{                                                                              \
    int i, j;                                                                  \
                                                                               \
    for (i = 0, j = 0; i < (128 / TBITS); i++, j += 2) {                       \
        int##TBITS##_t a = (int##BITS##_t)s390_vec_read_element##BITS(v2, j);  \
        int##TBITS##_t b = (int##BITS##_t)s390_vec_read_element##BITS(v3, j);  \
                                                                               \
        s390_vec_write_element##TBITS(v1, i, a * b);                           \
    }                                                                          \
}
DEF_VME(8, 16)
DEF_VME(16, 32)
DEF_VME(32, 64)

#define DEF_VMLE(BITS, TBITS)                                                  \
void HELPER(gvec_vmle##BITS)(void *v1, const void *v2, const void *v3,         \
                             uint32_t desc)                                    \
{                                                                              \
    int i, j;                                                                  \
                                                                               \
    for (i = 0, j = 0; i < (128 / TBITS); i++, j += 2) {                       \
        const uint##TBITS##_t a = s390_vec_read_element##BITS(v2, j);          \
        const uint##TBITS##_t b = s390_vec_read_element##BITS(v3, j);          \
                                                                               \
        s390_vec_write_element##TBITS(v1, i, a * b);                           \
    }                                                                          \
}
DEF_VMLE(8, 16)
DEF_VMLE(16, 32)
DEF_VMLE(32, 64)

#define DEF_VMO(BITS, TBITS)                                                   \
void HELPER(gvec_vmo##BITS)(void *v1, const void *v2, const void *v3,          \
                            uint32_t desc)                                     \
{                                                                              \
    int i, j;                                                                  \
                                                                               \
    for (i = 0, j = 1; i < (128 / TBITS); i++, j += 2) {                       \
        int##TBITS##_t a = (int##BITS##_t)s390_vec_read_element##BITS(v2, j);  \
        int##TBITS##_t b = (int##BITS##_t)s390_vec_read_element##BITS(v3, j);  \
                                                                               \
        s390_vec_write_element##TBITS(v1, i, a * b);                           \
    }                                                                          \
}
DEF_VMO(8, 16)
DEF_VMO(16, 32)
DEF_VMO(32, 64)

#define DEF_VMLO(BITS, TBITS)                                                  \
void HELPER(gvec_vmlo##BITS)(void *v1, const void *v2, const void *v3,         \
                             uint32_t desc)                                    \
{                                                                              \
    int i, j;                                                                  \
                                                                               \
    for (i = 0, j = 1; i < (128 / TBITS); i++, j += 2) {                       \
        const uint##TBITS##_t a = s390_vec_read_element##BITS(v2, j);          \
        const uint##TBITS##_t b = s390_vec_read_element##BITS(v3, j);          \
                                                                               \
        s390_vec_write_element##TBITS(v1, i, a * b);                           \
    }                                                                          \
}
DEF_VMLO(8, 16)
DEF_VMLO(16, 32)
DEF_VMLO(32, 64)

#define DEF_VPOPCT(BITS)                                                       \
void HELPER(gvec_vpopct##BITS)(void *v1, const void *v2, uint32_t desc)        \
{                                                                              \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const uint##BITS##_t a = s390_vec_read_element##BITS(v2, i);           \
                                                                               \
        s390_vec_write_element##BITS(v1, i, ctpop32(a));                       \
    }                                                                          \
}
DEF_VPOPCT(8)
DEF_VPOPCT(16)

#define DEF_VERIM(BITS)                                                        \
void HELPER(gvec_verim##BITS)(void *v1, const void *v2, const void *v3,        \
                              uint32_t desc)                                   \
{                                                                              \
    const uint8_t count = simd_data(desc);                                     \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const uint##BITS##_t a = s390_vec_read_element##BITS(v1, i);           \
        const uint##BITS##_t b = s390_vec_read_element##BITS(v2, i);           \
        const uint##BITS##_t mask = s390_vec_read_element##BITS(v3, i);        \
        const uint##BITS##_t d = (a & ~mask) | (rol##BITS(b, count) & mask);   \
                                                                               \
        s390_vec_write_element##BITS(v1, i, d);                                \
    }                                                                          \
}
DEF_VERIM(8)
DEF_VERIM(16)

void HELPER(gvec_vsl)(void *v1, const void *v2, uint64_t count,
                      uint32_t desc)
{
    s390_vec_shl(v1, v2, count);
}

void HELPER(gvec_vsl_ve2)(void *v1, const void *v2, const void *v3,
                          uint32_t desc)
{
    S390Vector tmp;
    uint32_t sh, e0, e1 = 0;
    int i;

    for (i = 15; i >= 0; --i, e1 = e0) {
        e0 = s390_vec_read_element8(v2, i);
        sh = s390_vec_read_element8(v3, i) & 7;

        s390_vec_write_element8(&tmp, i, rol32(e0 | (e1 << 24), sh));
    }

    *(S390Vector *)v1 = tmp;
}

void HELPER(gvec_vsra)(void *v1, const void *v2, uint64_t count,
                       uint32_t desc)
{
    s390_vec_sar(v1, v2, count);
}

void HELPER(gvec_vsra_ve2)(void *v1, const void *v2, const void *v3,
                           uint32_t desc)
{
    S390Vector tmp;
    uint32_t sh, e0, e1 = 0;
    int i = 0;

    /* Byte 0 is special only. */
    e0 = (int32_t)(int8_t)s390_vec_read_element8(v2, i);
    sh = s390_vec_read_element8(v3, i) & 7;
    s390_vec_write_element8(&tmp, i, e0 >> sh);

    e1 = e0;
    for (i = 1; i < 16; ++i, e1 = e0) {
        e0 = s390_vec_read_element8(v2, i);
        sh = s390_vec_read_element8(v3, i) & 7;
        s390_vec_write_element8(&tmp, i, (e0 | e1 << 8) >> sh);
    }

    *(S390Vector *)v1 = tmp;
}

void HELPER(gvec_vsrl)(void *v1, const void *v2, uint64_t count,
                       uint32_t desc)
{
    s390_vec_shr(v1, v2, count);
}

void HELPER(gvec_vsrl_ve2)(void *v1, const void *v2, const void *v3,
                           uint32_t desc)
{
    S390Vector tmp;
    uint32_t sh, e0, e1 = 0;

    for (int i = 0; i < 16; ++i, e1 = e0) {
        e0 = s390_vec_read_element8(v2, i);
        sh = s390_vec_read_element8(v3, i) & 7;

        s390_vec_write_element8(&tmp, i, (e0 | (e1 << 8)) >> sh);
    }

    *(S390Vector *)v1 = tmp;
}

#define DEF_VSCBI(BITS)                                                        \
void HELPER(gvec_vscbi##BITS)(void *v1, const void *v2, const void *v3,        \
                              uint32_t desc)                                   \
{                                                                              \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const uint##BITS##_t a = s390_vec_read_element##BITS(v2, i);           \
        const uint##BITS##_t b = s390_vec_read_element##BITS(v3, i);           \
                                                                               \
        s390_vec_write_element##BITS(v1, i, a >= b);                           \
    }                                                                          \
}
DEF_VSCBI(8)
DEF_VSCBI(16)

void HELPER(gvec_vtm)(void *v1, const void *v2, CPUS390XState *env,
                      uint32_t desc)
{
    S390Vector tmp;

    s390_vec_and(&tmp, v1, v2);
    if (s390_vec_is_zero(&tmp)) {
        /* Selected bits all zeros; or all mask bits zero */
        env->cc_op = 0;
    } else if (s390_vec_equal(&tmp, v2)) {
        /* Selected bits all ones */
        env->cc_op = 3;
    } else {
        /* Selected bits a mix of zeros and ones */
        env->cc_op = 1;
    }
}
