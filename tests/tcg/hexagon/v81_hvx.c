/*
 *  Test the HVX instructions that are new in v81:
 *   - V6_veqhf/V6_veqsf (and the _and/_or/_xor predicate-accumulate forms)
 *   - V6_valign4
 *   - V6_vconv_h_hf_rnd
 *
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <hexagon_types.h>
#include <hvx_hexagon_protos.h>

int err;
#include "hvx_misc.h"
#include "hex_test.h"

#define MAX_TESTS_hf (MAX_VEC_SIZE_BYTES / 2)
#define MAX_TESTS_sf (MAX_VEC_SIZE_BYTES / 4)

#define TRUE_MASK_sf 0xffffffff
#define TRUE_MASK_hf 0xffff

static const char *comparisons[MAX_TESTS_sf][2];
static HVX_Vector *hvx_output = (HVX_Vector *)&output[0];
static HVX_Vector buffers[2], true_vec, false_vec;
static int exp_index;

#define ADD_TEST_CMP(TYPE, VAL1, VAL2, EXP) do { \
    assert(exp_index < MAX_TESTS_##TYPE); \
    ((MMVector *)&buffers[0])->TYPE[exp_index] = VAL1; \
    ((MMVector *)&buffers[1])->TYPE[exp_index] = VAL2; \
    expect[0].TYPE[exp_index] = EXP ? TRUE_MASK_##TYPE : 0; \
    comparisons[exp_index][0] = #VAL1; \
    comparisons[exp_index][1] = #VAL2; \
    exp_index++; \
} while (0)

#define TEST_CMP_EQ(TYPE, VAL1, VAL2, EXP) do { \
    ADD_TEST_CMP(TYPE, VAL1, VAL2, EXP); \
    ADD_TEST_CMP(TYPE, VAL2, VAL1, EXP); \
} while (0)

#define PREP_TEST() do { \
    memset(&buffers, 0, sizeof(buffers)); \
    memset(expect, 0, sizeof(expect)); \
    exp_index = 0; \
} while (0)

#define CHECK(TYPE, TYPESZ) do { \
    HVX_VectorPred pred = \
        Q6_Q_vcmp_eq_V##TYPE##V##TYPE(buffers[0], buffers[1]); \
    *hvx_output = Q6_V_vmux_QVV(pred, true_vec, false_vec); \
    for (int j = 0; j < exp_index; j++) { \
        if (output[0].TYPE[j] != expect[0].TYPE[j]) { \
            printf("ERROR: expected %s %s %s\n", comparisons[j][0], \
                   (expect[0].TYPE[j] != 0 ? "==" : "!="), comparisons[j][1]); \
            err++; \
        } \
    } \
} while (0)

static void test_cmp_eq_sf(void)
{
    PREP_TEST();
    TEST_CMP_EQ(sf, raw_sf(2.2),  raw_sf(2.2),  true);
    TEST_CMP_EQ(sf, raw_sf(2.2),  raw_sf(2.1),  false);
    TEST_CMP_EQ(sf, SF_zero,      SF_zero_neg,  true);
    CHECK(sf, 4);

    /* NaNs never compare equal, even to themselves */
    PREP_TEST();
    TEST_CMP_EQ(sf, SF_QNaN,      SF_QNaN,      false);
    TEST_CMP_EQ(sf, SF_SNaN,      SF_SNaN,      false);
    TEST_CMP_EQ(sf, SF_QNaN,      SF_SNaN,      false);
    TEST_CMP_EQ(sf, SF_QNaN,      SF_one,       false);
    TEST_CMP_EQ(sf, SF_INF,       SF_QNaN,      false);
    CHECK(sf, 4);

    /* Infinities of like sign compare equal */
    PREP_TEST();
    TEST_CMP_EQ(sf, SF_INF,       SF_INF,       true);
    TEST_CMP_EQ(sf, SF_INF_neg,   SF_INF_neg,   true);
    TEST_CMP_EQ(sf, SF_INF,       SF_INF_neg,   false);
    CHECK(sf, 4);
}

static void test_cmp_eq_hf(void)
{
    PREP_TEST();
    TEST_CMP_EQ(hf, raw_hf((_Float16)2.2),  raw_hf((_Float16)2.2),  true);
    TEST_CMP_EQ(hf, raw_hf((_Float16)2.2),  raw_hf((_Float16)2.1),  false);
    TEST_CMP_EQ(hf, (uint16_t)0,             (uint16_t)0x8000,      true);
    CHECK(hf, 2);

    /* NaNs never compare equal, even to themselves */
    PREP_TEST();
    TEST_CMP_EQ(hf, HF_QNaN,      HF_QNaN,      false);
    TEST_CMP_EQ(hf, HF_SNaN,      HF_SNaN,      false);
    TEST_CMP_EQ(hf, HF_QNaN,      HF_SNaN,      false);
    TEST_CMP_EQ(hf, HF_QNaN,      HF_one,       false);
    TEST_CMP_EQ(hf, HF_INF,       HF_QNaN,      false);
    CHECK(hf, 2);

    /* Infinities of like sign compare equal */
    PREP_TEST();
    TEST_CMP_EQ(hf, HF_INF,       HF_INF,       true);
    TEST_CMP_EQ(hf, HF_INF_neg,   HF_INF_neg,   true);
    TEST_CMP_EQ(hf, HF_INF,       HF_INF_neg,   false);
    CHECK(hf, 2);
}

static void check_byte_pred(HVX_VectorPred pred, int byte_idx, uint8_t exp_mask,
                            int line)
{
    /*
     * Note: ((uint8_t *)&pred)[N] returns the expanded value of bit N:
     * 0xFF if bit is set, 0x00 if clear.
     */
    for (int i = 0; i < 8; i++) {
        int idx = byte_idx * 8 + i;
        int val = ((uint8_t *)&pred)[idx];
        int exp = (exp_mask >> i) & 1 ? 0xff : 0x00;
        if (exp != val) {
            printf("ERROR line %d: pred bit %d is 0x%x, should be 0x%x\n",
                   line, idx, val, exp);
            err++;
        }
    }
}

#define CHECK_BYTE_PRED(PRED, BYTE, EXP) \
    check_byte_pred(PRED, BYTE, EXP, __LINE__)

static void test_cmp_eq_variants(void)
{
    HVX_VectorPred pred;

    /*
     * Setup: comparison result will have bits 4-7 set (0xF0 in pred byte 0)
     * - sf[0]: SF_zero == SF_one    = false -> bits 0-3 = 0
     * - sf[1]: SF_one  == SF_one    = true  -> bits 4-7 = 1
     */
    PREP_TEST();
    ADD_TEST_CMP(sf, SF_zero, SF_one, false);
    ADD_TEST_CMP(sf, SF_one,  SF_one, true);

    /* equal and: 0xF0 & 0xF0 = 0xF0 */
    memset(&pred, 0xF0, sizeof(pred));
    pred = Q6_Q_vcmp_eqand_QVsfVsf(pred, buffers[0], buffers[1]);
    CHECK_BYTE_PRED(pred, 0, 0xF0);

    /* equal or: 0x0F | 0xF0 = 0xFF */
    memset(&pred, 0x0F, sizeof(pred));
    pred = Q6_Q_vcmp_eqor_QVsfVsf(pred, buffers[0], buffers[1]);
    CHECK_BYTE_PRED(pred, 0, 0xFF);

    /* equal xor: 0xFF ^ 0xF0 = 0x0F */
    memset(&pred, 0xFF, sizeof(pred));
    pred = Q6_Q_vcmp_eqxacc_QVsfVsf(pred, buffers[0], buffers[1]);
    CHECK_BYTE_PRED(pred, 0, 0x0F);
}

static void test_valign4(void)
{
    HVX_Vector vu = Q6_V_vsplat_R(0x11111111);
    HVX_Vector vv = Q6_V_vsplat_R(0x22222222);

    static const uint32_t expected[4] = {
        0x11111111, /* Rt & 0x3 == 0: no shift */
        0x11111122, /* Rt & 0x3 == 1: shift by  8 */
        0x11112222, /* Rt & 0x3 == 2: shift by 16 */
        0x11222222, /* Rt & 0x3 == 3: shift by 24 */
    };

    for (int rt = 0; rt < 4; rt++) {
        HVX_Vector result = Q6_V_valign4_VVR(vu, vv, rt);
        MMVector *r = (MMVector *)&result;
        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            check(__LINE__, rt, j, r->uw[j], expected[rt]);
        }
    }
}

static void test_vconv_h_hf_rnd(void)
{
    /*
     * V6_vconv_h_hf_rnd always rounds to nearest-even, regardless of
     * USR.FPRND.  Verified against hexagon-sim: all four FPRND settings
     * (nearest-even, toward-zero, toward -inf, toward +inf) produce
     * identical results for this instruction.
     */
    uint32_t usr;
    HVX_Vector input = Q6_V_vsplat_R(
        ((uint32_t)raw_hf((_Float16)1.5) << 16) | raw_hf((_Float16)(-1.5)));
    HVX_Vector result = Q6_Vh_equals_Vhf_rnd(input);
    MMVector *r = (MMVector *)&result;

    for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
        check(__LINE__, 0, 2 * j,     r->h[2 * j],     -2);
        check(__LINE__, 0, 2 * j + 1, r->h[2 * j + 1],  2);
    }

    asm("%0 = usr" : "=r"(usr));
    usr = (usr & ~(3u << 22)) | (1u << 22); /* round toward zero */
    asm("usr = %0" : : "r"(usr) : "usr");
    result = Q6_Vh_equals_Vhf_rnd(input);
    r = (MMVector *)&result;
    for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
        check(__LINE__, 0, 2 * j,     r->h[2 * j],     -2);
        check(__LINE__, 0, 2 * j + 1, r->h[2 * j + 1],  2);
    }

    usr = (usr & ~(3u << 22)) | (2u << 22); /* round toward -infinity */
    asm("usr = %0" : : "r"(usr) : "usr");
    result = Q6_Vh_equals_Vhf_rnd(input);
    r = (MMVector *)&result;
    for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
        check(__LINE__, 0, 2 * j,     r->h[2 * j],     -2);
        check(__LINE__, 0, 2 * j + 1, r->h[2 * j + 1],  2);
    }

    usr = (usr & ~(3u << 22)) | (3u << 22); /* round toward +infinity */
    asm("usr = %0" : : "r"(usr) : "usr");
    result = Q6_Vh_equals_Vhf_rnd(input);
    r = (MMVector *)&result;
    for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
        check(__LINE__, 0, 2 * j,     r->h[2 * j],     -2);
        check(__LINE__, 0, 2 * j + 1, r->h[2 * j + 1],  2);
    }

    input = Q6_V_vsplat_R(((uint32_t)0x7e00 << 16) | 0xfe00);
    result = Q6_Vh_equals_Vhf_rnd(input);
    r = (MMVector *)&result;
    for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
        check(__LINE__, 0, 2 * j,     r->h[2 * j],     INT16_MIN);
        check(__LINE__, 0, 2 * j + 1, r->h[2 * j + 1], INT16_MAX);
    }
}

int main(void)
{
    memset(&true_vec, 0xff, sizeof(true_vec));
    memset(&false_vec, 0, sizeof(false_vec));

    test_cmp_eq_sf();
    test_cmp_eq_hf();
    test_cmp_eq_variants();
    test_valign4();
    test_vconv_h_hf_rnd();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
