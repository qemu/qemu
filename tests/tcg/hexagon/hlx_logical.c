/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hlx_test.h"

static uint16_t *buf_a;
static uint16_t *buf_b;
static uint16_t *buf_out;
static uint16_t expected[HLX_HALF_ELEMS];

static uint16_t *pair_a_lo;
static uint16_t *pair_a_hi;
static uint16_t *pair_b_lo;
static uint16_t *pair_b_hi;
static uint16_t *pair_out_lo;
static uint16_t *pair_out_hi;

#define VAL_A     ((int16_t)0xAAAA)
#define VAL_B     ((int16_t)0x5555)
#define VAL_PA_LO ((int16_t)0xFF00)
#define VAL_PA_HI ((int16_t)0x00FF)
#define VAL_PB_LO ((int16_t)0x0F0F)
#define VAL_PB_HI ((int16_t)0xF0F0)

static void init_logical(void)
{
    Q6_mem_Ku16_new_AL(buf_a, Q6_Lh_equals_Rh(VAL_A));
    Q6_mem_Ku16_new_AL(buf_b, Q6_Lh_equals_Rh(VAL_B));
    Q6_mem_Ku16_new_AL(pair_a_lo, Q6_Lh_equals_Rh(VAL_PA_LO));
    Q6_mem_Ku16_new_AL(pair_a_hi, Q6_Lh_equals_Rh(VAL_PA_HI));
    Q6_mem_Ku16_new_AL(pair_b_lo, Q6_Lh_equals_Rh(VAL_PB_LO));
    Q6_mem_Ku16_new_AL(pair_b_hi, Q6_Lh_equals_Rh(VAL_PB_HI));
}


static void test_and(void)
{
    HLX_Vector la = Q6_L_tmp_mem_A(buf_a);
    HLX_Vector lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_L_and_LL(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)VAL_A & (uint16_t)VAL_B;
    }
    check_h("LX_and", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_or(void)
{
    HLX_Vector la = Q6_L_tmp_mem_A(buf_a);
    HLX_Vector lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_L_or_LL(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)VAL_A | (uint16_t)VAL_B;
    }
    check_h("LX_or", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_xor(void)
{
    HLX_Vector la = Q6_L_tmp_mem_A(buf_a);
    HLX_Vector lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_L_xor_LL(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)VAL_A ^ (uint16_t)VAL_B;
    }
    check_h("LX_xor", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_not(void)
{
    HLX_Vector la = Q6_L_tmp_mem_A(buf_a);
    Q6_mem_Ku16_new_AL(buf_out, Q6_L_not_L(la));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)~(uint16_t)VAL_A;
    }
    check_h("LX_not", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_andn(void)
{
    HLX_Vector la = Q6_L_tmp_mem_A(buf_a);
    HLX_Vector lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_L_andn_LL(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)VAL_A & ~(uint16_t)VAL_B;
    }
    check_h("LX_andn", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_orn(void)
{
    HLX_Vector la = Q6_L_tmp_mem_A(buf_a);
    HLX_Vector lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_L_orn_LL(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)VAL_A | ~(uint16_t)VAL_B;
    }
    check_h("LX_orn", buf_out, expected, HLX_HALF_ELEMS);
}


static void test_and_w(void)
{
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    HLX_VectorPair pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pair_a_hi),
                                         Q6_L_tmp_mem_A(pair_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pair_b_hi),
                          Q6_L_tmp_mem_A(pair_b_lo));
    pc = Q6_LW_and_LWLW(pa, pb);
    Q6_mem_Ku16_new_AL(pair_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL(pair_out_hi, Q6_L_hi_LW(pc));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)VAL_PA_LO & (uint16_t)VAL_PB_LO;
    }
    check_h("LX_and_w (lo)", pair_out_lo, expected, HLX_HALF_ELEMS);
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)VAL_PA_HI & (uint16_t)VAL_PB_HI;
    }
    check_h("LX_and_w (hi)", pair_out_hi, expected, HLX_HALF_ELEMS);
}

static void test_or_w(void)
{
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    HLX_VectorPair pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pair_a_hi),
                                         Q6_L_tmp_mem_A(pair_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pair_b_hi),
                          Q6_L_tmp_mem_A(pair_b_lo));
    pc = Q6_LW_or_LWLW(pa, pb);
    Q6_mem_Ku16_new_AL(pair_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL(pair_out_hi, Q6_L_hi_LW(pc));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)VAL_PA_LO | (uint16_t)VAL_PB_LO;
    }
    check_h("LX_or_w (lo)", pair_out_lo, expected, HLX_HALF_ELEMS);
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)VAL_PA_HI | (uint16_t)VAL_PB_HI;
    }
    check_h("LX_or_w (hi)", pair_out_hi, expected, HLX_HALF_ELEMS);
}

static void test_xor_w(void)
{
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    HLX_VectorPair pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pair_a_hi),
                                         Q6_L_tmp_mem_A(pair_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pair_b_hi),
                          Q6_L_tmp_mem_A(pair_b_lo));
    pc = Q6_LW_xor_LWLW(pa, pb);
    Q6_mem_Ku16_new_AL(pair_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL(pair_out_hi, Q6_L_hi_LW(pc));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)VAL_PA_LO ^ (uint16_t)VAL_PB_LO;
    }
    check_h("LX_xor_w (lo)", pair_out_lo, expected, HLX_HALF_ELEMS);
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)VAL_PA_HI ^ (uint16_t)VAL_PB_HI;
    }
    check_h("LX_xor_w (hi)", pair_out_hi, expected, HLX_HALF_ELEMS);
}

static void test_not_w(void)
{
    HLX_VectorPair pc;
    HLX_VectorPair pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pair_a_hi),
                                         Q6_L_tmp_mem_A(pair_a_lo));
    pc = Q6_LW_not_LW(pa);
    Q6_mem_Ku16_new_AL(pair_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL(pair_out_hi, Q6_L_hi_LW(pc));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)~(uint16_t)VAL_PA_LO;
    }
    check_h("LX_not_w (lo)", pair_out_lo, expected, HLX_HALF_ELEMS);
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = ~(uint16_t)VAL_PA_HI;
    }
    check_h("LX_not_w (hi)", pair_out_hi, expected, HLX_HALF_ELEMS);
}

static void test_andn_w(void)
{
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    HLX_VectorPair pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pair_a_hi),
                                         Q6_L_tmp_mem_A(pair_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pair_b_hi),
                          Q6_L_tmp_mem_A(pair_b_lo));
    pc = Q6_LW_andn_LWLW(pa, pb);
    Q6_mem_Ku16_new_AL(pair_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL(pair_out_hi, Q6_L_hi_LW(pc));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)VAL_PA_LO & ~(uint16_t)VAL_PB_LO;
    }
    check_h("LX_andn_w (lo)", pair_out_lo, expected, HLX_HALF_ELEMS);
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)VAL_PA_HI & ~(uint16_t)VAL_PB_HI;
    }
    check_h("LX_andn_w (hi)", pair_out_hi, expected, HLX_HALF_ELEMS);
}

static void test_orn_w(void)
{
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    HLX_VectorPair pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pair_a_hi),
                                         Q6_L_tmp_mem_A(pair_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pair_b_hi),
                          Q6_L_tmp_mem_A(pair_b_lo));
    pc = Q6_LW_orn_LWLW(pa, pb);
    Q6_mem_Ku16_new_AL(pair_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL(pair_out_hi, Q6_L_hi_LW(pc));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)VAL_PA_LO | ~(uint16_t)VAL_PB_LO;
    }
    check_h("LX_orn_w (lo)", pair_out_lo, expected, HLX_HALF_ELEMS);
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)((uint16_t)VAL_PA_HI | ~(uint16_t)VAL_PB_HI);
    }
    check_h("LX_orn_w (hi)", pair_out_hi, expected, HLX_HALF_ELEMS);
}

int main(void)
{
    init_vtcm();
    enable_hlx();

    buf_a = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_b = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_out = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    pair_a_lo = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    pair_a_hi = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    pair_b_lo = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    pair_b_hi = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    pair_out_lo = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    pair_out_hi = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    init_hlx_vsp();

    init_logical();

    /* Single-register operations */
    test_and();
    test_or();
    test_xor();
    test_not();
    test_andn();
    test_orn();

    /* Word-pair operations */
    test_and_w();
    test_or_w();
    test_xor_w();
    test_not_w();
    test_andn_w();
    test_orn_w();

    puts(err ? "FAIL" : "PASS");
    return err;
}
