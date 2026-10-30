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

/*
 * The vcvt2 instructions differ from vcvt in how elements are interleaved
 * between the vectors of a pair or the sources:
 *
 *    Vdd.v[0].hf[2*i]   = OP(Vu.FROM[4*i]);
 *    Vdd.v[0].hf[2*i+1] = OP(Vu.FROM[4*i+2]);
 *    Vdd.v[1].hf[2*i]   = OP(Vu.FROM[4*i+1]);
 *    Vdd.v[1].hf[2*i+1] = OP(Vu.FROM[4*i+3]);
 *
 *    Vd.TO[4*i]   = OP(Vu.hf[2*i]);
 *    Vd.TO[4*i+1] = OP(Vv.hf[2*i]);
 *    Vd.TO[4*i+2] = OP(Vu.hf[2*i+1]);
 *    Vd.TO[4*i+3] = OP(Vv.hf[2*i+1]);
 */
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

int main(void)
{
    test_vcvt2_hf_ub();
    test_vcvt2_hf_b();
    test_vcvt2_ub_hf();
    test_vcvt2_b_hf();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
