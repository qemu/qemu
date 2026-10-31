/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hlx_test.h"

/* Basic add/sub operands (no overflow) */
#define ARITH_A             100
#define ARITH_B             200

/* Signed positive saturation: NEAR_MAX + SAT_STEP overflows INT16_MAX */
#define NEAR_INT16_MAX      32000
#define SAT_STEP            1000

/* Signed negative saturation: NEAR_MIN - SAT_STEP underflows INT16_MIN */
#define NEAR_INT16_MIN      (-32000)

/* Unsigned saturation: NEAR_UMAX + SAT_STEP overflows UINT16_MAX */
#define NEAR_UINT16_MAX     ((int16_t)65000u)

static uint16_t *buf_a;
static uint16_t *buf_b;
static uint16_t *buf_out;
static uint16_t expected[HLX_HALF_ELEMS];

static void fill_h(uint16_t *buf, int16_t val)
{
    Q6_mem_Ku16_new_AL(buf, Q6_Lh_equals_Rh(val));
}

static void test_add_h(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, ARITH_A);
    fill_h(buf_b, ARITH_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_add_LhLh(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = ARITH_A + ARITH_B;
    }
    check_h("LX_add_h", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_add_h_sat(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, NEAR_INT16_MAX);
    fill_h(buf_b, SAT_STEP);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_add_LhLh_sat(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)sat_h((int32_t)NEAR_INT16_MAX + SAT_STEP);
    }
    check_h("LX_add_h_sat", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_add_uh_sat(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, NEAR_UINT16_MAX);
    fill_h(buf_b, SAT_STEP);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_add_LuhLuh_sat(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = sat_uh((int32_t)65000u + SAT_STEP);
    }
    check_h("LX_add_uh_sat", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_add_uhh_sat(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, ARITH_A);
    fill_h(buf_b, ARITH_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_add_LhLh_sat(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = sat_uh((int32_t)ARITH_A + ARITH_B);
    }
    check_h("LX_add_uhh_sat", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_sub_h(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, ARITH_A + ARITH_B);
    fill_h(buf_b, ARITH_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_sub_LhLh(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = ARITH_A;
    }
    check_h("LX_sub_h", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_sub_h_sat(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, NEAR_INT16_MIN);
    fill_h(buf_b, SAT_STEP);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_sub_LhLh_sat(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)sat_h((int32_t)NEAR_INT16_MIN - SAT_STEP);
    }
    check_h("LX_sub_h_sat", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_sub_uh_sat(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, ARITH_A);
    fill_h(buf_b, ARITH_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_sub_LuhLuh_sat(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = sat_uh((int32_t)ARITH_A - ARITH_B);
    }
    check_h("LX_sub_uh_sat", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_sub_uhh_sat(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, -ARITH_A);
    fill_h(buf_b, ARITH_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_sub_LhLh_sat(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = sat_uh((int32_t)-ARITH_A - ARITH_B);
    }
    check_h("LX_sub_uhh_sat", buf_out, expected, HLX_HALF_ELEMS);
}

int main(void)
{
    init_vtcm();
    enable_hlx();

    buf_a = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_b = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_out = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    init_hlx_vsp();

    test_add_h();
    test_add_h_sat();
    test_add_uh_sat();
    test_add_uhh_sat();
    test_sub_h();
    test_sub_h_sat();
    test_sub_uh_sat();
    test_sub_uhh_sat();

    puts(err ? "FAIL" : "PASS");
    return err;
}
