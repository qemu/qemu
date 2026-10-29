/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hlx_test.h"

/* Broadcast constants for uniform buffer init */
#define VAL_A       ((int16_t)0x1234)
#define VAL_B       ((int16_t)0x5678)
#define VAL_C       ((int16_t)-300)       /* 0xFED4 -- negative for mux test */
#define VAL_W       ((int32_t)0x12345678)

static uint16_t *buf_a;
static uint16_t *buf_b;
static uint16_t *buf_c;
static uint16_t *buf_out;
static uint16_t expected[HLX_HALF_ELEMS];

static uint32_t *pw_out_lo;
static uint32_t *pw_out_hi;
static uint32_t *pw_a_lo;
static uint32_t *pw_a_hi;
static uint32_t expected_w[HLX_HALF_ELEMS];

static inline uint32_t pack_h(uint16_t lo, uint16_t hi)
{
    return ((uint32_t)hi << 16) | lo;
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


static void test_assign(void)
{
    HLX_Vector la;

    fill_h(buf_a, VAL_A);

    la = Q6_L_tmp_mem_A(buf_a);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_equals_Lh(la));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)VAL_A;
    }
    check_h("LX_assign", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_assign_w(void)
{
    HLX_Vector l_lo;
    HLX_Vector l_hi;
    HLX_VectorPair pp;

    fill_w(pw_a_lo, pw_a_hi, VAL_W);

    l_lo = Q6_L_tmp_mem_A(pw_a_lo);
    l_hi = Q6_L_tmp_mem_A(pw_a_hi);
    pp = Q6_LW_combine_LL(l_hi, l_lo);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pp));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pp));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)VAL_W;
    }
    check_wp("LX_assign_w", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_assign_r(void)
{
    int16_t scalar = 0x1234;
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_equals_Rh(scalar));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)scalar;
    }
    check_h("LX_assign_r", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_assign_s(void)
{
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_equals_Is(-5));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)(int16_t)(-5);
    }
    check_h("LX_assign_s", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_assign_u(void)
{
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_equals_I(17));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = 17;
    }
    check_h("LX_assign_u", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_assign_rw(void)
{
    int32_t scalar = 0x12345678;
    HLX_VectorPair wp = Q6_LWw_equals_Rw(scalar);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(wp));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(wp));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)scalar;
    }
    check_wp("LX_assign_rw", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}


static void test_combine_w(void)
{
    HLX_Vector la;
    HLX_Vector lb;
    HLX_VectorPair pp;
    uint32_t exp_val;

    fill_h(buf_a, VAL_A);
    fill_h(buf_b, VAL_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    pp = Q6_LW_combine_LL(la, lb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pp));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pp));

    /* combine(Lu, Lv): l[i].h[1] = Lu.h[i], l[i].h[0] = Lv.h[i] */
    exp_val = pack_h((uint16_t)VAL_B, (uint16_t)VAL_A);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp_val;
    }
    check_wp("LX_combine_w", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}


static void test_mux_h(void)
{
    HLX_Vector la;
    HLX_Vector lc;
    HLX_Vector lb;
    HLX_Vector res;

    fill_h(buf_a, VAL_A);
    fill_h(buf_b, VAL_B);
    fill_h(buf_c, VAL_C);

    /* mux(Lx, Lu, Lv): Lx.h < 0 ? Lu : Lv */
    la = Q6_L_tmp_mem_A(buf_a);
    lc = Q6_L_tmp_mem_A(buf_c);
    lb = Q6_L_tmp_mem_A(buf_b);
    res = Q6_Lh_mux_LhLhLh(lb, la, lc);
    Q6_mem_Ku16_new_AL(buf_out, res);

    /* VAL_C < 0, so mux selects VAL_A for every element */
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)VAL_A;
    }
    check_h("LX_mux_h", buf_out, expected, HLX_HALF_ELEMS);
}


static void test_conv_uw_uh(void)
{
    HLX_Vector la;
    HLX_VectorPair pp;
    uint32_t exp_val;

    fill_h(buf_a, VAL_A);

    la = Q6_L_tmp_mem_A(buf_a);
    pp = Q6_LWuw_zxt_Luh(la);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pp));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pp));

    exp_val = (uint32_t)(uint16_t)VAL_A;
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp_val;
    }
    check_wp("LX_conv_uw_uh", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_conv_w_h(void)
{
    HLX_Vector la;
    HLX_VectorPair pp;
    uint32_t exp_val;

    fill_h(buf_a, VAL_A);

    la = Q6_L_tmp_mem_A(buf_a);
    pp = Q6_LWw_sxt_Lh(la);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pp));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pp));

    exp_val = (uint32_t)(int32_t)VAL_A;
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp_val;
    }
    check_wp("LX_conv_w_h", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_conv_b_h_hi(void)
{
    HLX_Vector la;
    HLX_Vector lb;
    uint8_t hi_a;
    uint8_t hi_b;
    uint16_t exp_val;

    fill_h(buf_a, VAL_A);
    fill_h(buf_b, VAL_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lb_packhi_LhLh(la, lb));

    /* packhi: Ld.b[2i+1] = Lu.b[2i+1], Ld.b[2i] = Lv.b[2i+1] */
    hi_a = ((uint16_t)VAL_A >> 8) & 0xFF;
    hi_b = ((uint16_t)VAL_B >> 8) & 0xFF;
    exp_val = ((uint16_t)hi_a << 8) | hi_b;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp_val;
    }
    check_h("LX_conv_b_h_hi", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_conv_b_h_lo(void)
{
    HLX_Vector la;
    HLX_Vector lb;
    uint8_t lo_a;
    uint8_t lo_b;
    uint16_t exp_val;

    fill_h(buf_a, VAL_A);
    fill_h(buf_b, VAL_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lb_packlo_LhLh(la, lb));

    /* packlo: Ld.b[2i+1] = Lu.b[2i], Ld.b[2i] = Lv.b[2i] */
    lo_a = (uint16_t)VAL_A & 0xFF;
    lo_b = (uint16_t)VAL_B & 0xFF;
    exp_val = ((uint16_t)lo_a << 8) | lo_b;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp_val;
    }
    check_h("LX_conv_b_h_lo", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_conv_h_b_hi(void)
{
    HLX_Vector la;
    HLX_VectorPair pp;
    uint8_t hi_byte;
    uint8_t lo_byte;
    uint32_t exp_val;

    fill_h(buf_a, VAL_A);

    la = Q6_L_tmp_mem_A(buf_a);
    pp = Q6_LWh_unpackhi_Lb(la);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pp));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pp));

    /* unpackhi: b[3] = Lu.b[2i+1], b[1] = Lu.b[2i], b[0]=b[2]=0 */
    hi_byte = ((uint16_t)VAL_A >> 8) & 0xFF;
    lo_byte = (uint16_t)VAL_A & 0xFF;
    exp_val = ((uint32_t)hi_byte << 24) | ((uint32_t)lo_byte << 8);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp_val;
    }
    check_wp("LX_conv_h_b_hi", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_conv_h_b_lo(void)
{
    HLX_Vector la;
    HLX_VectorPair pp;
    uint8_t hi_byte;
    uint8_t lo_byte;
    uint32_t exp_val;

    fill_h(buf_a, VAL_A);

    la = Q6_L_tmp_mem_A(buf_a);
    pp = Q6_LWh_unpacklo_Lb(la);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pp));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pp));

    /* unpacklo: b[2] = Lu.b[2i+1], b[0] = Lu.b[2i] */
    hi_byte = ((uint16_t)VAL_A >> 8) & 0xFF;
    lo_byte = (uint16_t)VAL_A & 0xFF;
    exp_val = ((uint32_t)hi_byte << 16) | lo_byte;
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp_val;
    }
    check_wp("LX_conv_h_b_lo", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

int main(void)
{
    init_vtcm();
    enable_hlx();

    buf_a = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_b = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_c = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_out = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    pw_out_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_out_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_a_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_a_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    init_hlx_vsp();

    /* Assign operations */
    test_assign();
    test_assign_w();
    test_assign_r();
    test_assign_s();
    test_assign_u();
    test_assign_rw();

    /* Combine and mux */
    test_combine_w();
    test_mux_h();

    /* Conversions */
    test_conv_uw_uh();
    test_conv_w_h();
    test_conv_b_h_hi();
    test_conv_b_h_lo();
    test_conv_h_b_hi();
    test_conv_h_b_lo();

    puts(err ? "FAIL" : "PASS");
    return err;
}
