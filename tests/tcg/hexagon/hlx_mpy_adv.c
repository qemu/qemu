/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hlx_test.h"

/* Operand values for multiplies */
#define ADV_A_UH        100
#define ADV_B_UH        200
#define ADV_A_SH        ((int16_t)-50)
#define ADV_SCALAR_UH   42
#define ADV_SCALAR_SUH  50

/*
 * Accumulator word value whose lo/hi halves are tested separately.
 * 0x00300500 => lo half = 0x0500 = 1280, hi half = 0x0030 = 48
 */
#define ACC_W_VAL       0x00300500
#define ACC_W_LO        (ACC_W_VAL & 0xFFFF)           /* 1280 */
#define ACC_W_HI        ((ACC_W_VAL >> 16) & 0xFFFF)   /* 48   */

/* Bias for complex-mpy tests */
#define BIAS_VAL        1000
#define BIAS_SCALAR      12345
#define BIAS_SCALAR_NEG  (-5000)

static uint16_t *buf_a;
static uint16_t *buf_b;

static uint32_t *pw_acc_lo;
static uint32_t *pw_acc_hi;
static uint32_t *pw_out_lo;
static uint32_t *pw_out_hi;
static uint32_t *pw_bias_lo;
static uint32_t *pw_bias_hi;
static uint32_t expected_w[HLX_HALF_ELEMS];

static inline uint32_t ref_mpy_uu(uint16_t a, uint16_t b)
{
    return (uint32_t)a * (uint32_t)b;
}

static inline int32_t ref_mpy_su(int16_t a, uint16_t b)
{
    return (int32_t)a * (int32_t)(uint32_t)b;
}

static void fill_h(uint16_t *buf, int16_t val)
{
    Q6_mem_Ku16_new_AL(buf, Q6_Lh_equals_Rh(val));
}

static void fill_w(uint32_t *buf_lo, uint32_t *buf_hi, int32_t val)
{
    HLX_VectorPair wp = Q6_LWw_equals_Rw(val);
    Q6_mem_Ku16_new_AL((uint16_t *)buf_lo, Q6_L_lo_LW(wp));
    Q6_mem_Ku16_new_AL((uint16_t *)buf_hi, Q6_L_hi_LW(wp));
}


static void test_mpa_wuh_lo(void)
{
    HLX_Vector la;
    HLX_Vector lb;
    HLX_VectorPair acc;
    HLX_VectorPair result;
    uint32_t exp;

    fill_h(buf_a, ADV_A_UH);
    fill_h(buf_b, ADV_B_UH);
    fill_w(pw_acc_lo, pw_acc_hi, ACC_W_VAL);

    /*
     * Lxx.uw = add(mpy(Lu.uh, Lv.uh), lo(Lxx).uh)
     * Q6_LWuw_add_mpy_lo_LuhLuhLWuh(Lu, Lv, Lxx)
     */
    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    acc = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_acc_hi),
                           Q6_L_tmp_mem_A(pw_acc_lo));
    result = Q6_LWuw_add_mpy_lo_LuhLuhLWuh(la, lb, acc);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    exp = ref_mpy_uu(ADV_A_UH, ADV_B_UH) + ACC_W_LO;
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp;
    }
    check_wp("LX_mpa_wuh_lo", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_mpar_wuh_lo(void)
{
    HLX_Vector la;
    HLX_VectorPair acc;
    HLX_VectorPair result;
    uint32_t exp;

    fill_h(buf_a, ADV_A_UH);
    fill_w(pw_acc_lo, pw_acc_hi, ACC_W_VAL);

    la = Q6_L_tmp_mem_A(buf_a);
    acc = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_acc_hi),
                           Q6_L_tmp_mem_A(pw_acc_lo));
    result = Q6_LWuw_add_mpy_lo_LuhRuhLWuh(la, ADV_SCALAR_UH,
                                           acc);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    exp = ref_mpy_uu(ADV_A_UH, ADV_SCALAR_UH) + ACC_W_LO;
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp;
    }
    check_wp("LX_mpar_wuh_lo", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_mpa_whuh_lo(void)
{
    HLX_Vector la;
    HLX_Vector lb;
    HLX_VectorPair acc;
    HLX_VectorPair result;
    uint32_t exp;

    fill_h(buf_a, ADV_A_SH);
    fill_h(buf_b, ADV_B_UH);
    fill_w(pw_acc_lo, pw_acc_hi, ACC_W_VAL);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    acc = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_acc_hi),
                           Q6_L_tmp_mem_A(pw_acc_lo));
    result = Q6_LWw_add_mpy_lo_LhLuhLWuh(la, lb, acc);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    exp = (uint32_t)(ref_mpy_su(ADV_A_SH, ADV_B_UH) +
          (int32_t)ACC_W_LO);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp;
    }
    check_wp("LX_mpa_whuh_lo", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_mpar_whuh_lo(void)
{
    HLX_Vector la;
    HLX_VectorPair acc;
    HLX_VectorPair result;
    uint32_t exp;

    fill_h(buf_a, ADV_A_SH);
    fill_w(pw_acc_lo, pw_acc_hi, ACC_W_VAL);

    la = Q6_L_tmp_mem_A(buf_a);
    acc = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_acc_hi),
                           Q6_L_tmp_mem_A(pw_acc_lo));
    result = Q6_LWw_add_mpy_lo_LhRuhLWuh(la, ADV_SCALAR_SUH,
                                         acc);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    exp = (uint32_t)(ref_mpy_su(ADV_A_SH, ADV_SCALAR_SUH) +
          (int32_t)ACC_W_LO);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp;
    }
    check_wp("LX_mpar_whuh_lo", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}


static void test_mpa_wuh_hi(void)
{
    HLX_Vector la;
    HLX_Vector lb;
    HLX_VectorPair acc;
    HLX_VectorPair result;
    uint32_t exp;

    fill_h(buf_a, ADV_A_UH);
    fill_h(buf_b, ADV_B_UH);
    fill_w(pw_acc_lo, pw_acc_hi, ACC_W_VAL);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    acc = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_acc_hi),
                           Q6_L_tmp_mem_A(pw_acc_lo));
    result = Q6_LWuw_add_mpy_hi_LuhLuhLWuh(la, lb, acc);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    exp = ref_mpy_uu(ADV_A_UH, ADV_B_UH) + ACC_W_HI;
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp;
    }
    check_wp("LX_mpa_wuh_hi", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_mpar_wuh_hi(void)
{
    HLX_Vector la;
    HLX_VectorPair acc;
    HLX_VectorPair result;
    uint32_t exp;

    fill_h(buf_a, ADV_A_UH);
    fill_w(pw_acc_lo, pw_acc_hi, ACC_W_VAL);

    la = Q6_L_tmp_mem_A(buf_a);
    acc = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_acc_hi),
                           Q6_L_tmp_mem_A(pw_acc_lo));
    result = Q6_LWuw_add_mpy_hi_LuhRuhLWuh(la, ADV_SCALAR_UH,
                                           acc);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    exp = ref_mpy_uu(ADV_A_UH, ADV_SCALAR_UH) + ACC_W_HI;
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp;
    }
    check_wp("LX_mpar_wuh_hi", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_mpa_wh_hi(void)
{
    HLX_Vector la;
    HLX_Vector lb;
    HLX_VectorPair acc;
    HLX_VectorPair result;
    uint32_t exp;

    fill_h(buf_a, ADV_A_SH);
    fill_h(buf_b, ADV_B_UH);
    fill_w(pw_acc_lo, pw_acc_hi, ACC_W_VAL);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    acc = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_acc_hi),
                           Q6_L_tmp_mem_A(pw_acc_lo));
    result = Q6_LWuw_add_mpy_hi_LhLuhLWuh(la, lb, acc);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    exp = (uint32_t)(ref_mpy_su(ADV_A_SH, ADV_B_UH) +
          (int32_t)ACC_W_HI);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp;
    }
    check_wp("LX_mpa_wh_hi", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_mpar_wh_hi(void)
{
    HLX_Vector la;
    HLX_VectorPair acc;
    HLX_VectorPair result;
    uint32_t exp;

    fill_h(buf_a, ADV_A_SH);
    fill_w(pw_acc_lo, pw_acc_hi, ACC_W_VAL);

    la = Q6_L_tmp_mem_A(buf_a);
    acc = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_acc_hi),
                           Q6_L_tmp_mem_A(pw_acc_lo));
    result = Q6_LWuw_add_mpy_hi_LhRuhLWuh(la, ADV_SCALAR_SUH,
                                          acc);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    exp = (uint32_t)(ref_mpy_su(ADV_A_SH, ADV_SCALAR_SUH) +
          (int32_t)ACC_W_HI);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp;
    }
    check_wp("LX_mpar_wh_hi", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}


static void test_mpa_uwuhuw_si(void)
{
    HLX_Vector la;
    HLX_VectorPair acc;
    HLX_VectorPair bias;
    HLX_VectorPair result;
    uint32_t mpy;
    uint32_t exp;

    fill_h(buf_a, ADV_A_UH);
    fill_w(pw_acc_lo, pw_acc_hi, ACC_W_VAL);
    fill_w(pw_bias_lo, pw_bias_hi, BIAS_VAL);

    /*
     * Lxx.uw = add(mpy(Lu.uh, hi(Lxx).uh), Lvv.w)
     * fMPY3216UU(A,B) = (uint32_t)((uint64_t)A * (uint32_t)B)
     * Q6_LWuw_add_mpy_hi_LuhLWuhLWw(Lu, Lxx, Lvv)
     */
    la = Q6_L_tmp_mem_A(buf_a);
    acc = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_acc_hi),
                           Q6_L_tmp_mem_A(pw_acc_lo));
    bias = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_bias_hi),
                            Q6_L_tmp_mem_A(pw_bias_lo));
    result = Q6_LWuw_add_mpy_hi_LuhLWuhLWw(la, acc, bias);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    mpy = (uint32_t)((uint64_t)ACC_W_HI * (uint32_t)ADV_A_UH);
    exp = (uint32_t)BIAS_VAL + mpy;
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp;
    }
    check_wp("LX_mpa_uwuhuw_si", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_mpar_uwuhuw_si(void)
{
    HLX_Vector la;
    HLX_VectorPair acc;
    HLX_VectorPair result;
    uint32_t mpy;
    uint32_t exp;

    fill_h(buf_a, ADV_A_UH);
    fill_w(pw_acc_lo, pw_acc_hi, ACC_W_VAL);

    la = Q6_L_tmp_mem_A(buf_a);
    acc = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_acc_hi),
                           Q6_L_tmp_mem_A(pw_acc_lo));
    result = Q6_LWuw_add_mpy_hi_LuhLWuhRw(la, acc,
                                          BIAS_SCALAR);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    mpy = (uint32_t)((uint64_t)ACC_W_HI * (uint32_t)ADV_A_UH);
    exp = (uint32_t)BIAS_SCALAR + mpy;
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp;
    }
    check_wp("LX_mpar_uwuhuw_si", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_mpa_wuhw_si(void)
{
    HLX_Vector la;
    HLX_VectorPair acc;
    HLX_VectorPair bias;
    HLX_VectorPair result;
    int16_t hi_half;
    int32_t mpy;
    uint32_t exp;

    fill_h(buf_a, ADV_A_UH);
    fill_w(pw_acc_lo, pw_acc_hi, ACC_W_VAL);
    fill_w(pw_bias_lo, pw_bias_hi, BIAS_VAL);

    /*
     * Lxx.w = add(mpy(Lu.uh, hi(Lxx).h), Lvv.w)
     * fMPY3216SU(A,B) = (int32_t)(fSE32_64(A) * fZXTN(16,64,B))
     * Here A=hi(Lxx).h (signed), B=Lu.uh (unsigned)
     * Q6_LWw_add_mpy_hi_LuhLWhLWw(Lu, Lxx, Lvv)
     */
    la = Q6_L_tmp_mem_A(buf_a);
    acc = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_acc_hi),
                           Q6_L_tmp_mem_A(pw_acc_lo));
    bias = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_bias_hi),
                            Q6_L_tmp_mem_A(pw_bias_lo));
    result = Q6_LWw_add_mpy_hi_LuhLWhLWw(la, acc, bias);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    hi_half = (int16_t)ACC_W_HI;
    mpy = (int32_t)((int64_t)hi_half * (uint32_t)ADV_A_UH);
    exp = (uint32_t)((int32_t)BIAS_VAL + mpy);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp;
    }
    check_wp("LX_mpa_wuhw_si", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_mpar_wuhw_si(void)
{
    HLX_Vector la;
    HLX_VectorPair acc;
    HLX_VectorPair result;
    int16_t hi_half;
    int32_t mpy;
    uint32_t exp;

    fill_h(buf_a, ADV_A_UH);
    fill_w(pw_acc_lo, pw_acc_hi, ACC_W_VAL);

    la = Q6_L_tmp_mem_A(buf_a);
    acc = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_acc_hi),
                           Q6_L_tmp_mem_A(pw_acc_lo));
    result = Q6_LWw_add_mpy_hi_LuhLWhRw(la, acc,
                                        BIAS_SCALAR_NEG);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    hi_half = (int16_t)ACC_W_HI;
    mpy = (int32_t)((int64_t)hi_half * (uint32_t)ADV_A_UH);
    exp = (uint32_t)(BIAS_SCALAR_NEG + mpy);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp;
    }
    check_wp("LX_mpar_wuhw_si", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

int main(void)
{
    init_vtcm();
    enable_hlx();

    buf_a = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_b = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    pw_acc_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_acc_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_out_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_out_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_bias_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_bias_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    init_hlx_vsp();

    /* mpy + lo(Lxx) */
    test_mpa_wuh_lo();
    test_mpar_wuh_lo();
    test_mpa_whuh_lo();
    test_mpar_whuh_lo();

    /* mpy + hi(Lxx) */
    test_mpa_wuh_hi();
    test_mpar_wuh_hi();
    test_mpa_wh_hi();
    test_mpar_wh_hi();

    /* Complex mpy */
    test_mpa_uwuhuw_si();
    test_mpar_uwuhuw_si();
    test_mpa_wuhw_si();
    test_mpar_wuhw_si();

    puts(err ? "FAIL" : "PASS");
    return err;
}
