/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Tests for the HVX IEEE floating point instructions introduced in v79.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <hexagon_types.h>
#include <hvx_hexagon_protos.h>

int err;
#include "hvx_misc.h"
#include "hex_test.h"

/* vcvt2 interleaves elements between result vectors and source vectors. */
#define DEF_TEST_VCVT2_WIDEN(FROM, MUL, ADD) \
    static void test_vcvt2_hf_##FROM(void) \
    { \
        HVX_VectorPair *hvx_output = (HVX_VectorPair *)&output[0]; \
        HVX_Vector buffer; \
        memset(expect, 0, sizeof(expect)); \
        for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) { \
            ((MMVector *)&buffer)->FROM[i] = i * MUL + ADD; \
            expect[i % 2].hf[2 * (i / 4) + ((i % 4) / 2)] = \
                raw_hf((_Float16)(((MMVector *)&buffer)->FROM[i])); \
        } \
        *hvx_output = Q6_Whf_vcvt2_V##FROM(buffer); \
        check_output_hf(__LINE__, 2); \
    }

DEF_TEST_VCVT2_WIDEN(ub, 3, 1)
DEF_TEST_VCVT2_WIDEN(b, 5, 7)

static const struct {
    uint16_t hf;
    uint8_t ub;
    int8_t b;
} cvt2_narrow_cases[] = {
    { HF_QNaN, UINT8_MAX, INT8_MAX },
    { HF_SNaN, UINT8_MAX, INT8_MAX },
    { HF_QNaN_neg, UINT8_MAX, INT8_MAX },
    { HF_INF, UINT8_MAX, INT8_MAX },
    { HF_INF_neg, 0, INT8_MIN },
    { HF_zero, 0, 0 },
    { HF_zero_neg, 0, 0 },
    { HF_neg_two, 0, -2 },
    { HF_one, 1, 1 },
    { 0x3e00 /* 1.5 */, 2, 2 },
    { 0x4100 /* 2.5 */, 2, 2 },
    { 0x4300 /* 3.5 */, 4, 4 },
    { 0x5640 /* 100 */, 100, 100 },
    { 0x5a40 /* 200 */, 200, INT8_MAX },
    { 0x5c00 /* 256 */, UINT8_MAX, INT8_MAX },
    { 0xd640 /* -100 */, 0, -100 },
    { 0xda40 /* -200 */, 0, INT8_MIN },
};
#define NUM_CVT2_NARROW_CASES ARRAY_SIZE(cvt2_narrow_cases)

#define DEF_TEST_VCVT2_NARROW(TO) \
    static void test_vcvt2_##TO##_hf(void) \
    { \
        HVX_Vector *hvx_output = (HVX_Vector *)&output[0]; \
        HVX_Vector buffers[2]; \
        memset(expect, 0, sizeof(expect)); \
        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 2; j++) { \
            for (int s = 0; s < 2; s++) { \
                int c = (2 * j + s) % NUM_CVT2_NARROW_CASES; \
                ((MMVector *)&buffers[s])->hf[j] = cvt2_narrow_cases[c].hf; \
                expect[0].TO[4 * (j / 2) + 2 * (j % 2) + s] = \
                    cvt2_narrow_cases[c].TO; \
            } \
        } \
        *hvx_output = Q6_V##TO##_vcvt2_VhfVhf(buffers[0], buffers[1]); \
        check_output_##TO(__LINE__, 1); \
    }

DEF_TEST_VCVT2_NARROW(ub)
DEF_TEST_VCVT2_NARROW(b)

/* FP8 tests cover edge cases and hash exhaustive input sweeps. */
#define F8_NAN 0x80
#define HF_NAN 0x7fff

/* Widening FP8 instructions use vcvt2-style result interleaving. */
#define WIDEN_VEC(I) ((I) % 2)
#define WIDEN_IDX(I) (2 * ((I) / 4) + (((I) % 4) / 2))

#define HASH_INIT 0xcbf29ce484222325ULL

static void hash_bytes(uint64_t *hash, const void *buf, size_t len)
{
    const uint8_t *p = buf;

    for (size_t i = 0; i < len; i++) {
        *hash = (*hash ^ p[i]) * 0x100000001b3ULL;
    }
}

static void check_hash(int line, uint64_t hash, uint64_t golden)
{
    if (hash != golden) {
        printf("ERROR at line %d: hash 0x%016" PRIx64 " != 0x%016" PRIx64 "\n",
               line, hash, golden);
        err++;
    }
}

static const struct {
    uint8_t a, b;
    uint16_t add, sub, mpy;
    uint8_t min, max;
} f8_cases[] = {
    /* NaN inputs */
    { 0x80, 0x40, HF_NAN, HF_NAN, HF_NAN, F8_NAN, F8_NAN },
    { 0x40, 0x80, HF_NAN, HF_NAN, HF_NAN, F8_NAN, F8_NAN },
    { 0x80, 0x80, HF_NAN, HF_NAN, HF_NAN, F8_NAN, F8_NAN },
    /* Zero */
    { 0x00, 0x00, 0x0000, 0x0000, 0x0000, 0x00, 0x00 },
    { 0x00, 0x40, 0x3c00, 0xbc00, 0x0000, 0x00, 0x40 },
    /* Exact cancellation */
    { 0x40, 0x40, 0x4000, 0x0000, 0x3c00, 0x40, 0x40 },
    { 0x40, 0xc0, 0x0000, 0x4000, 0xbc00, 0xc0, 0x40 },
    /* Largest magnitude */
    { 0x7f, 0x7f, 0x5f80, 0x0000, 0x7b08, 0x7f, 0x7f },
    { 0x7f, 0xff, 0x0000, 0x5f80, 0xfb08, 0xff, 0x7f },
    { 0xff, 0xff, 0xdf80, 0x0000, 0x7b08, 0xff, 0xff },
    /* Subnormal boundaries */
    { 0x01, 0x01, 0x1800, 0x0000, 0x0010, 0x01, 0x01 },
    { 0x01, 0x40, 0x3c01, 0xbbfe, 0x1400, 0x01, 0x40 },
    { 0x07, 0x08, 0x2380, 0x9400, 0x0380, 0x07, 0x08 },
    { 0x44, 0x01, 0x3e01, 0x3dff, 0x1600, 0x01, 0x44 },
};
#define NUM_F8_CASES ARRAY_SIZE(f8_cases)

#define DEF_TEST_F8_BINARY_HF(NAME, INTRIN, FIELD, GOLDEN) \
    static void test_##NAME(void) \
    { \
        HVX_VectorPair *hvx_output = (HVX_VectorPair *)&output[0]; \
        uint64_t hash = HASH_INIT; \
        HVX_Vector a, b; \
        for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) { \
            int c = i % NUM_F8_CASES; \
            ((MMVector *)&a)->ub[i] = f8_cases[c].a; \
            ((MMVector *)&b)->ub[i] = f8_cases[c].b; \
            expect[WIDEN_VEC(i)].hf[WIDEN_IDX(i)] = f8_cases[c].FIELD; \
        } \
        *hvx_output = INTRIN(a, b); \
        check_output_hf(__LINE__, 2); \
        /* All FP8 operand pairs. */ \
        for (int blk = 0; blk < 512; blk++) { \
            for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) { \
                int pair = blk * MAX_VEC_SIZE_BYTES + i; \
                ((MMVector *)&a)->ub[i] = pair & 0xff; \
                ((MMVector *)&b)->ub[i] = pair >> 8; \
            } \
            *hvx_output = INTRIN(a, b); \
            hash_bytes(&hash, hvx_output, sizeof(*hvx_output)); \
        } \
        check_hash(__LINE__, hash, GOLDEN); \
    }

#define DEF_TEST_F8_BINARY_UB(NAME, INTRIN, FIELD, GOLDEN) \
    static void test_##NAME(void) \
    { \
        HVX_Vector *hvx_output = (HVX_Vector *)&output[0]; \
        uint64_t hash = HASH_INIT; \
        HVX_Vector a, b; \
        for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) { \
            int c = i % NUM_F8_CASES; \
            ((MMVector *)&a)->ub[i] = f8_cases[c].a; \
            ((MMVector *)&b)->ub[i] = f8_cases[c].b; \
            expect[0].ub[i] = f8_cases[c].FIELD; \
        } \
        *hvx_output = INTRIN(a, b); \
        check_output_ub(__LINE__, 1); \
        for (int blk = 0; blk < 512; blk++) { \
            for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) { \
                int pair = blk * MAX_VEC_SIZE_BYTES + i; \
                ((MMVector *)&a)->ub[i] = pair & 0xff; \
                ((MMVector *)&b)->ub[i] = pair >> 8; \
            } \
            *hvx_output = INTRIN(a, b); \
            hash_bytes(&hash, hvx_output, sizeof(*hvx_output)); \
        } \
        check_hash(__LINE__, hash, GOLDEN); \
    }

DEF_TEST_F8_BINARY_HF(vadd_hf_f8, Q6_Whf_vadd_VV, add, 0x2e57ea6fa0a5770bULL)
DEF_TEST_F8_BINARY_HF(vsub_hf_f8, Q6_Whf_vsub_VV, sub, 0xe5fcfee5b66a9333ULL)
DEF_TEST_F8_BINARY_HF(vmpy_hf_f8, Q6_Whf_vmpy_VV, mpy, 0x9edf5d41219d76dfULL)
DEF_TEST_F8_BINARY_UB(vfmin_f8, Q6_V_vfmin_VV, min, 0x9d394d7796c56725ULL)
DEF_TEST_F8_BINARY_UB(vfmax_f8, Q6_V_vfmax_VV, max, 0xd4e8b1560e8feea5ULL)

static const struct {
    uint8_t a, b;
    uint16_t acc, result;
} f8_acc_cases[] = {
    { 0x40, 0x40, 0x0000, 0x3c00 },
    { 0x40, 0x40, 0x3c00, 0x4000 },
    { 0x40, 0x40, 0xbc00, 0x0000 },
    { 0x40, 0xc0, 0x3c00, 0x0000 },
    { 0x7f, 0x7f, 0x7bff, 0x7c00 },
    { 0x7f, 0xff, 0xfbff, 0xfc00 },
    { 0x01, 0x01, 0x0001, 0x0011 },
    /* NaN and infinity */
    { 0x80, 0x40, 0x3c00, HF_NAN },
    { 0x40, 0x40, 0x7e00, HF_NAN },
    { 0x40, 0x40, 0x7c00, 0x7c00 },
};
#define NUM_F8_ACC_CASES ARRAY_SIZE(f8_acc_cases)

static const uint16_t acc_values[] = {
    0x0000, 0x8000, 0x3c00, 0xbc00, 0x6800, 0x0001, 0x7bff, 0xfbff, 0x7c00,
    0xfc00, 0x7e00, 0x7d00, 0x1234, 0xa5c3,
};

static void test_vmpy_hf_f8_acc(void)
{
    HVX_VectorPair *hvx_output = (HVX_VectorPair *)&output[0];
    uint64_t hash = HASH_INIT;
    HVX_VectorPair acc;
    HVX_Vector a, b;

    for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) {
        int c = i % NUM_F8_ACC_CASES;

        ((MMVector *)&a)->ub[i] = f8_acc_cases[c].a;
        ((MMVector *)&b)->ub[i] = f8_acc_cases[c].b;
        ((MMVector *)&acc)->hf[WIDEN_VEC(i) * 64 + WIDEN_IDX(i)] =
            f8_acc_cases[c].acc;
        expect[WIDEN_VEC(i)].hf[WIDEN_IDX(i)] = f8_acc_cases[c].result;
    }
    *hvx_output = Q6_Whf_vmpyacc_WhfVV(acc, a, b);
    check_output_hf(__LINE__, 2);

    /* Test fixed and near-canceling accumulators. */
    for (int n = 0; n <= ARRAY_SIZE(acc_values); n++) {
        for (int blk = 0; blk < 512; blk++) {
            for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) {
                int pair = blk * MAX_VEC_SIZE_BYTES + i;

                ((MMVector *)&a)->ub[i] = pair & 0xff;
                ((MMVector *)&b)->ub[i] = pair >> 8;
            }
            if (n == ARRAY_SIZE(acc_values)) {
                acc = Q6_Whf_vmpy_VV(a, b);
                for (int k = 0; k < MAX_VEC_SIZE_BYTES; k++) {
                    ((MMVector *)&acc)->hf[k] ^= 0x8000 ^ (k & 3);
                }
            } else {
                for (int k = 0; k < MAX_VEC_SIZE_BYTES; k++) {
                    ((MMVector *)&acc)->hf[k] = acc_values[n];
                }
            }
            *hvx_output = Q6_Whf_vmpyacc_WhfVV(acc, a, b);
            hash_bytes(&hash, hvx_output, sizeof(*hvx_output));
        }
    }
    check_hash(__LINE__, hash, 0x85def62d6d02aea8ULL);
}

static const struct {
    uint8_t f8;
    uint16_t hf;
} cvt_hf_f8_cases[] = {
    { 0x00, 0x0000 },
    { 0x01, 0x1400 },
    { 0x07, 0x1f00 },
    { 0x08, 0x2000 },
    { 0x40, 0x3c00 },
    { 0x7f, 0x5b80 },
    { 0x80, HF_NAN },
    { 0xff, 0xdb80 },
};
#define NUM_CVT_HF_F8_CASES ARRAY_SIZE(cvt_hf_f8_cases)

static void test_vcvt_hf_f8(void)
{
    HVX_VectorPair *hvx_output = (HVX_VectorPair *)&output[0];
    uint64_t hash = HASH_INIT;
    HVX_Vector a;

    for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) {
        int c = i % NUM_CVT_HF_F8_CASES;

        ((MMVector *)&a)->ub[i] = cvt_hf_f8_cases[c].f8;
        expect[WIDEN_VEC(i)].hf[WIDEN_IDX(i)] = cvt_hf_f8_cases[c].hf;
    }
    *hvx_output = Q6_Whf_vcvt_V(a);
    check_output_hf(__LINE__, 2);

    /* All FP8 values. */
    for (int blk = 0; blk < 2; blk++) {
        for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) {
            ((MMVector *)&a)->ub[i] = blk * MAX_VEC_SIZE_BYTES + i;
        }
        *hvx_output = Q6_Whf_vcvt_V(a);
        hash_bytes(&hash, hvx_output, sizeof(*hvx_output));
    }
    check_hash(__LINE__, hash, 0xf09689ef20d70fafULL);
}

#define DEF_TEST_F8_UNARY(NAME, INTRIN, REF) \
    static void test_##NAME(void) \
    { \
        HVX_Vector *hvx_output = (HVX_Vector *)&output[0]; \
        HVX_Vector a; \
        for (int blk = 0; blk < 2; blk++) { \
            for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) { \
                int f8 = blk * MAX_VEC_SIZE_BYTES + i; \
                ((MMVector *)&a)->ub[i] = f8; \
                expect[0].ub[i] = REF(f8); \
            } \
            *hvx_output = INTRIN(a); \
            check_output_ub(__LINE__, 1); \
        } \
    }

static uint8_t ref_neg(uint8_t f8)
{
    return f8 == 0 || f8 == F8_NAN ? f8 : f8 ^ 0x80;
}

static uint8_t ref_abs(uint8_t f8)
{
    return f8 == F8_NAN ? f8 : f8 & 0x7f;
}

DEF_TEST_F8_UNARY(vfneg_f8, Q6_V_vfneg_V, ref_neg)
DEF_TEST_F8_UNARY(vabs_f8, Q6_V_vabs_V, ref_abs)

/* Keep the conversion after the USR.FPSAT update. */
static void set_fpsat(bool sat)
{
    uint32_t usr;

    asm volatile("%[usr] = usr" : [usr] "=r"(usr));
    usr = sat ? usr | (1 << 24) : usr & ~(1 << 24);
    asm volatile("usr = %[usr]" : : [usr] "r"(usr));
}

static const struct {
    uint16_t hf;
    uint8_t f8, f8_sat;
} cvt_f8_hf_cases[] = {
    /* NaN, infinity, and zero */
    { 0x7e00, F8_NAN, F8_NAN },
    { 0x7d00, F8_NAN, F8_NAN },
    { 0xfe00, F8_NAN, F8_NAN },
    { 0x7c00, F8_NAN, 0x7f },
    { 0xfc00, F8_NAN, 0xff },
    { 0x0000, 0x00, 0x00 },
    { 0x8000, 0x00, 0x00 },
    { 0x3c00, 0x40, 0x40 },
    { 0xbc00, 0xc0, 0xc0 },
    /* Largest finite values and overflow */
    { 0x5b80, 0x7f, 0x7f },
    { 0xdb80, 0xff, 0xff },
    { 0x5ba0, 0x7f, 0x7f },
    { 0x5b40, 0x7e, 0x7e },
    { 0x5bc0, F8_NAN, 0x7f },
    { 0x5c00, F8_NAN, 0x7f },
    /* Subnormal rounding */
    { 0x1400, 0x01, 0x01 },
    { 0x1000, 0x00, 0x00 },
    { 0x1600, 0x02, 0x02 },
    { 0x1800, 0x02, 0x02 },
    { 0x1200, 0x01, 0x01 },
    { 0x0001, 0x00, 0x00 },
    { 0x8001, 0x00, 0x00 },
};
#define NUM_CVT_F8_HF_CASES ARRAY_SIZE(cvt_f8_hf_cases)

static void test_vcvt_f8_hf(bool sat)
{
    HVX_Vector *hvx_output = (HVX_Vector *)&output[0];
    HVX_Vector buffers[2];
    /* Prevent source loads from moving before set_fpsat(). */
    volatile HVX_Vector *vbuffers = buffers;
    uint64_t hash = HASH_INIT;

    for (int j = 0; j < MAX_VEC_SIZE_BYTES / 2; j++) {
        for (int s = 0; s < 2; s++) {
            int c = (2 * j + s) % NUM_CVT_F8_HF_CASES;

            ((MMVector *)&buffers[s])->hf[j] = cvt_f8_hf_cases[c].hf;
            expect[0].ub[4 * (j / 2) + 2 * (j % 2) + s] =
                sat ? cvt_f8_hf_cases[c].f8_sat : cvt_f8_hf_cases[c].f8;
        }
    }
    set_fpsat(sat);
    *hvx_output = Q6_V_vcvt_VhfVhf(vbuffers[0], vbuffers[1]);
    set_fpsat(false);
    check_output_ub(__LINE__, 1);

    /* All half-precision values in alternating sources. */
    for (int blk = 0; blk < 1024; blk++) {
        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 2; j++) {
            for (int s = 0; s < 2; s++) {
                uint16_t h = (blk * 64 + j) * (s ? 40503 : 1) + (s ? 12345 : 0);

                ((MMVector *)&buffers[s])->hf[j] = h;
            }
        }
        set_fpsat(sat);
        *hvx_output = Q6_V_vcvt_VhfVhf(vbuffers[0], vbuffers[1]);
        set_fpsat(false);
        hash_bytes(&hash, hvx_output, sizeof(*hvx_output));
    }
    check_hash(__LINE__, hash,
               sat ? 0x711e375027f85c31ULL : 0xc90f8f22ad443a9dULL);
}

int main(void)
{
    test_vcvt2_hf_ub();
    test_vcvt2_hf_b();
    test_vcvt2_ub_hf();
    test_vcvt2_b_hf();

    test_vcvt_hf_f8();
    test_vcvt_f8_hf(false);
    test_vcvt_f8_hf(true);
    test_vadd_hf_f8();
    test_vsub_hf_f8();
    test_vmpy_hf_f8();
    test_vmpy_hf_f8_acc();
    test_vfneg_f8();
    test_vabs_f8();
    test_vfmin_f8();
    test_vfmax_f8();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
