/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hlx_test.h"

/* Expected values are from hexagon-sim -mv85qa_1. */
#define SRC_UH           0x5678
#define SRC_SH           ((int16_t)-1234)
#define SHAMT_H          4
#define SRC_UW           0x12345678u
#define SRC_SW           ((int32_t)-12345678)
#define SHAMT_W          8
#define NARROW_SRC_W     0x00ABCD00
#define NARROW_SHAMT     8

static uint16_t *buf_src;
static uint16_t *buf_shamt;
static uint16_t *buf_out;
static uint16_t expected[HLX_HALF_ELEMS];

static uint32_t *pw_src_lo;
static uint32_t *pw_src_hi;
static uint32_t *pw_out_lo;
static uint32_t *pw_out_hi;
static uint32_t expected_w[HLX_HALF_ELEMS];

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


static void test_shr_uh_uh(void)
{
    HLX_Vector la;
    HLX_Vector lshamt;
    uint16_t exp;

    fill_h(buf_src, (int16_t)SRC_UH);
    fill_h(buf_shamt, (int16_t)SHAMT_H);

    la = Q6_L_tmp_mem_A(buf_src);
    lshamt = Q6_L_tmp_mem_A(buf_shamt);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_shr_LuhLh(la, lshamt));

    exp = 0x0567;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shr_uh_uh", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_shrr_uh_uh(void)
{
    int16_t scalar;
    HLX_Vector la;
    uint16_t exp;

    fill_h(buf_src, (int16_t)SRC_UH);
    scalar = -SHAMT_H;

    la = Q6_L_tmp_mem_A(buf_src);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_shr_LuhRh(la, scalar));

    exp = 0x6780;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shrr_uh_uh", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_shr_h(void)
{
    HLX_Vector la;
    HLX_Vector lshamt;
    uint16_t exp;

    fill_h(buf_src, SRC_SH);
    fill_h(buf_shamt, (int16_t)SHAMT_H);

    la = Q6_L_tmp_mem_A(buf_src);
    lshamt = Q6_L_tmp_mem_A(buf_shamt);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_shr_LhLh(la, lshamt));

    exp = 0xffb2;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shr_h", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_shrr_h(void)
{
    int16_t scalar;
    HLX_Vector la;
    uint16_t exp;

    fill_h(buf_src, SRC_SH);
    scalar = -SHAMT_H;

    la = Q6_L_tmp_mem_A(buf_src);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_shr_LhRh(la, scalar));

    exp = 0xb2e0;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shrr_h", buf_out, expected, HLX_HALF_ELEMS);
}


static void test_shr_uw_uw(void)
{
    HLX_VectorPair pa;
    HLX_Vector lshamt;
    HLX_VectorPair result;

    fill_w(pw_src_lo, pw_src_hi, (int32_t)SRC_UW);
    fill_h(buf_shamt, (int16_t)SHAMT_W);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    lshamt = Q6_L_tmp_mem_A(buf_shamt);
    result = Q6_LWuw_shr_LWuwLh(pa, lshamt);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = 0x00123456u;
    }
    check_wp("LX_shr_uw_uw", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_shrr_uw_uw(void)
{
    int16_t scalar;
    HLX_VectorPair pa;
    HLX_VectorPair result;

    fill_w(pw_src_lo, pw_src_hi, (int32_t)SRC_UW);
    scalar = -SHAMT_W;

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    result = Q6_LWuw_shr_LWuwRh(pa, scalar);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = 0x34567800u;
    }
    check_wp("LX_shrr_uw_uw", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_shr_w_w(void)
{
    HLX_VectorPair pa;
    HLX_Vector lshamt;
    HLX_VectorPair result;

    fill_w(pw_src_lo, pw_src_hi, SRC_SW);
    fill_h(buf_shamt, (int16_t)SHAMT_W);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    lshamt = Q6_L_tmp_mem_A(buf_shamt);
    result = Q6_LWw_shr_LWwLh(pa, lshamt);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = 0xffff439eu;
    }
    check_wp("LX_shr_w_w", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_shrr_w_w(void)
{
    int16_t scalar;
    HLX_VectorPair pa;
    HLX_VectorPair result;

    fill_w(pw_src_lo, pw_src_hi, SRC_SW);
    scalar = -SHAMT_W;

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    result = Q6_LWw_shr_LWwRh(pa, scalar);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = 0x439eb200u;
    }
    check_wp("LX_shrr_w_w", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}


static void test_shr_h_w_uh(void)
{
    HLX_VectorPair pa;
    HLX_Vector lshamt;
    uint16_t exp;

    fill_w(pw_src_lo, pw_src_hi, (int32_t)NARROW_SRC_W);
    fill_h(buf_shamt, (int16_t)NARROW_SHAMT);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    lshamt = Q6_L_tmp_mem_A(buf_shamt);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_shr_LWwLuh(pa, lshamt));

    exp = (uint16_t)((int32_t)NARROW_SRC_W >> (NARROW_SHAMT & 15));
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shr_h_w_uh", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_shrr_h_w_uh(void)
{
    uint16_t scalar;
    HLX_VectorPair pa;
    uint16_t exp;

    fill_w(pw_src_lo, pw_src_hi, (int32_t)NARROW_SRC_W);
    scalar = NARROW_SHAMT;

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_shr_LWwRuh(pa, scalar));

    exp = (uint16_t)((int32_t)NARROW_SRC_W >> (scalar & 15));
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shrr_h_w_uh", buf_out, expected, HLX_HALF_ELEMS);
}


static void test_shr_h_w_sat(void)
{
    HLX_VectorPair pa;
    HLX_Vector lshamt;
    uint16_t exp;

    fill_w(pw_src_lo, pw_src_hi, SRC_SW);
    fill_h(buf_shamt, (int16_t)SHAMT_W);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    lshamt = Q6_L_tmp_mem_A(buf_shamt);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_shr_LWwLh_sat(pa, lshamt));

    exp = 0x8000;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shr_h_w_sat", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_shrr_h_w_sat(void)
{
    int16_t scalar;
    HLX_VectorPair pa;
    uint16_t exp;

    fill_w(pw_src_lo, pw_src_hi, SRC_SW);
    scalar = -SHAMT_H;

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_shr_LWwRh_sat(pa, scalar));

    exp = 0x8000;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shrr_h_w_sat", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_shr_h_w_rnd_sat(void)
{
    HLX_VectorPair pa;
    HLX_Vector lshamt;
    uint16_t exp;

    fill_w(pw_src_lo, pw_src_hi, SRC_SW);
    fill_h(buf_shamt, (int16_t)SHAMT_W);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    lshamt = Q6_L_tmp_mem_A(buf_shamt);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_shr_LWwLh_rnd_sat(pa, lshamt));

    exp = 0x8000;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shr_h_w_rnd_sat", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_shrr_h_w_rnd_sat(void)
{
    int16_t scalar;
    HLX_VectorPair pa;
    uint16_t exp;

    fill_w(pw_src_lo, pw_src_hi, SRC_SW);
    scalar = -SHAMT_W;

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_shr_LWwRh_rnd_sat(pa, scalar));

    exp = 0x8000;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shrr_h_w_rnd_sat", buf_out, expected, HLX_HALF_ELEMS);
}


static void test_shr_uh_uw_sat(void)
{
    HLX_VectorPair pa;
    HLX_Vector lshamt;
    uint16_t exp;

    fill_w(pw_src_lo, pw_src_hi, (int32_t)SRC_UW);
    fill_h(buf_shamt, (int16_t)SHAMT_W);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    lshamt = Q6_L_tmp_mem_A(buf_shamt);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_shr_LWuwLh_sat(pa, lshamt));

    exp = 0xffff;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shr_uh_uw_sat", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_shrr_uh_uw_sat(void)
{
    int16_t scalar;
    HLX_VectorPair pa;
    uint16_t exp;

    fill_w(pw_src_lo, pw_src_hi, (int32_t)SRC_UW);
    scalar = -SHAMT_W;

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_shr_LWuwRh_sat(pa, scalar));

    exp = 0xffff;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shrr_uh_uw_sat", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_shr_uh_uw_rnd_sat(void)
{
    HLX_VectorPair pa;
    HLX_Vector lshamt;
    uint16_t exp;

    fill_w(pw_src_lo, pw_src_hi, (int32_t)SRC_UW);
    fill_h(buf_shamt, (int16_t)SHAMT_W);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    lshamt = Q6_L_tmp_mem_A(buf_shamt);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_shr_LWuwLh_rnd_sat(pa, lshamt));

    exp = 0xffff;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shr_uh_uw_rnd_sat", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_shrr_uh_uw_rnd_sat(void)
{
    int16_t scalar;
    HLX_VectorPair pa;
    uint16_t exp;

    fill_w(pw_src_lo, pw_src_hi, (int32_t)SRC_UW);
    scalar = -SHAMT_W;

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_shr_LWuwRh_rnd_sat(pa, scalar));

    exp = 0xffff;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shrr_uh_uw_rnd_sat", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_shr_uh_w_sat(void)
{
    HLX_VectorPair pa;
    HLX_Vector lshamt;
    uint16_t exp;

    fill_w(pw_src_lo, pw_src_hi, SRC_SW);
    fill_h(buf_shamt, (int16_t)SHAMT_W);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    lshamt = Q6_L_tmp_mem_A(buf_shamt);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_shr_LWwLh_sat(pa, lshamt));

    exp = 0x0000;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shr_uh_w_sat", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_shrr_uh_w_sat(void)
{
    int16_t scalar;
    HLX_VectorPair pa;
    uint16_t exp;

    fill_w(pw_src_lo, pw_src_hi, SRC_SW);
    scalar = -SHAMT_W;

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_shr_LWwRh_sat(pa, scalar));

    exp = 0x0000;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shrr_uh_w_sat", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_shr_uh_w_rnd_sat(void)
{
    HLX_VectorPair pa;
    HLX_Vector lshamt;
    uint16_t exp;

    fill_w(pw_src_lo, pw_src_hi, SRC_SW);
    fill_h(buf_shamt, (int16_t)SHAMT_W);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    lshamt = Q6_L_tmp_mem_A(buf_shamt);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_shr_LWwLh_rnd_sat(pa, lshamt));

    exp = 0x0000;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shr_uh_w_rnd_sat", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_shrr_uh_w_rnd_sat(void)
{
    int16_t scalar;
    HLX_VectorPair pa;
    uint16_t exp;

    fill_w(pw_src_lo, pw_src_hi, SRC_SW);
    scalar = -SHAMT_W;

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                          Q6_L_tmp_mem_A(pw_src_lo));
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_shr_LWwRh_rnd_sat(pa, scalar));

    exp = 0x0000;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shrr_uh_w_rnd_sat", buf_out, expected, HLX_HALF_ELEMS);
}

int main(void)
{
    init_vtcm();
    enable_hlx();

    buf_src = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_shamt = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_out = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    pw_src_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_src_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_out_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_out_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    init_hlx_vsp();

    /* Basic halfword shifts */
    test_shr_uh_uh();
    test_shrr_uh_uh();
    test_shr_h();
    test_shrr_h();

    /* Word pair shifts */
    test_shr_uw_uw();
    test_shrr_uw_uw();
    test_shr_w_w();
    test_shrr_w_w();

    /* Narrowing shifts */
    test_shr_h_w_uh();
    test_shrr_h_w_uh();

    /* Narrowing shifts with saturation */
    test_shr_h_w_sat();
    test_shrr_h_w_sat();
    test_shr_h_w_rnd_sat();
    test_shrr_h_w_rnd_sat();

    /* Unsigned narrowing with saturation */
    test_shr_uh_uw_sat();
    test_shrr_uh_uw_sat();
    test_shr_uh_uw_rnd_sat();
    test_shrr_uh_uw_rnd_sat();
    test_shr_uh_w_sat();
    test_shrr_uh_w_sat();
    test_shr_uh_w_rnd_sat();
    test_shrr_uh_w_rnd_sat();

    puts(err ? "FAIL" : "PASS");
    return err;
}
