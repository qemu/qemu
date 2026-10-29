/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hlx_test.h"

/* Named constants */
#define ACC_H            1000
#define SRC_H            0x1234
#define SHAMT_H          4
#define ACC_W            50000
#define SRC_W            0x00123400
#define SHAMT_W          8

static uint16_t *buf_src;
static uint16_t *buf_shamt;
static uint16_t *buf_acc;
static uint16_t *buf_out;
static uint16_t expected[HLX_HALF_ELEMS];

static uint32_t *pw_src_lo;
static uint32_t *pw_src_hi;
static uint32_t *pw_acc_lo;
static uint32_t *pw_acc_hi;
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


static void test_shra_h_uh(void)
{
    HLX_Vector lacc;
    HLX_Vector lsrc;
    HLX_Vector lshamt;
    uint16_t exp;

    fill_h(buf_acc, ACC_H);
    fill_h(buf_src, (int16_t)SRC_H);
    fill_h(buf_shamt, (int16_t)SHAMT_H);

    lacc = Q6_L_tmp_mem_A(buf_acc);
    lsrc = Q6_L_tmp_mem_A(buf_src);
    lshamt = Q6_L_tmp_mem_A(buf_shamt);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_shracc_LuhLh(lacc, lsrc, lshamt));

    exp = 0x050b;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shra_h_uh", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_shrar_h_uh(void)
{
    int16_t scalar;
    HLX_Vector lacc;
    HLX_Vector lsrc;
    uint16_t exp;

    fill_h(buf_acc, ACC_H);
    fill_h(buf_src, (int16_t)SRC_H);
    scalar = -SHAMT_H;

    lacc = Q6_L_tmp_mem_A(buf_acc);
    lsrc = Q6_L_tmp_mem_A(buf_src);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_shracc_LuhRh(lacc, lsrc, scalar));

    exp = 0x2728;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shrar_h_uh", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_shra_h_h(void)
{
    HLX_Vector lacc;
    HLX_Vector lsrc;
    HLX_Vector lshamt;
    uint16_t exp;

    fill_h(buf_acc, ACC_H);
    fill_h(buf_src, (int16_t)SRC_H);
    fill_h(buf_shamt, (int16_t)SHAMT_H);

    lacc = Q6_L_tmp_mem_A(buf_acc);
    lsrc = Q6_L_tmp_mem_A(buf_src);
    lshamt = Q6_L_tmp_mem_A(buf_shamt);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_shracc_LhLh(lacc, lsrc, lshamt));

    exp = 0x050b;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shra_h_h", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_shrar_h_h(void)
{
    int16_t scalar;
    HLX_Vector lacc;
    HLX_Vector lsrc;
    uint16_t exp;

    fill_h(buf_acc, ACC_H);
    fill_h(buf_src, (int16_t)SRC_H);
    scalar = -SHAMT_H;

    lacc = Q6_L_tmp_mem_A(buf_acc);
    lsrc = Q6_L_tmp_mem_A(buf_src);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_shracc_LhRh(lacc, lsrc, scalar));

    exp = 0x2728;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp;
    }
    check_h("LX_shrar_h_h", buf_out, expected, HLX_HALF_ELEMS);
}


static void test_shra_w_uw(void)
{
    HLX_VectorPair acc;
    HLX_VectorPair src;
    HLX_Vector lshamt;
    HLX_VectorPair result;
    uint32_t exp;

    fill_w(pw_acc_lo, pw_acc_hi, ACC_W);
    fill_w(pw_src_lo, pw_src_hi, (int32_t)SRC_W);
    fill_h(buf_shamt, (int16_t)SHAMT_W);

    acc = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_acc_hi),
                           Q6_L_tmp_mem_A(pw_acc_lo));
    src = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                           Q6_L_tmp_mem_A(pw_src_lo));
    lshamt = Q6_L_tmp_mem_A(buf_shamt);
    result = Q6_LWw_shracc_LWuwLh(acc, src, lshamt);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    exp = 0x0000d584;
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp;
    }
    check_wp("LX_shra_w_uw", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_shrar_w_uw(void)
{
    int16_t scalar;
    HLX_VectorPair acc;
    HLX_VectorPair src;
    HLX_VectorPair result;
    uint32_t exp;

    fill_w(pw_acc_lo, pw_acc_hi, ACC_W);
    fill_w(pw_src_lo, pw_src_hi, (int32_t)SRC_W);
    scalar = -SHAMT_W;

    acc = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_acc_hi),
                           Q6_L_tmp_mem_A(pw_acc_lo));
    src = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                           Q6_L_tmp_mem_A(pw_src_lo));
    result = Q6_LWw_shracc_LWuwRh(acc, src, scalar);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    exp = 0x1234c350;
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp;
    }
    check_wp("LX_shrar_w_uw", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_shra_w_w(void)
{
    HLX_VectorPair acc;
    HLX_VectorPair src;
    HLX_Vector lshamt;
    HLX_VectorPair result;
    uint32_t exp;

    fill_w(pw_acc_lo, pw_acc_hi, ACC_W);
    fill_w(pw_src_lo, pw_src_hi, (int32_t)SRC_W);
    fill_h(buf_shamt, (int16_t)SHAMT_W);

    acc = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_acc_hi),
                           Q6_L_tmp_mem_A(pw_acc_lo));
    src = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                           Q6_L_tmp_mem_A(pw_src_lo));
    lshamt = Q6_L_tmp_mem_A(buf_shamt);
    result = Q6_LWw_shracc_LWwLh(acc, src, lshamt);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    exp = 0x0000d584;
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp;
    }
    check_wp("LX_shra_w_w", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_shrar_w_w(void)
{
    int16_t scalar;
    HLX_VectorPair acc;
    HLX_VectorPair src;
    HLX_VectorPair result;
    uint32_t exp;

    fill_w(pw_acc_lo, pw_acc_hi, ACC_W);
    fill_w(pw_src_lo, pw_src_hi, (int32_t)SRC_W);
    scalar = -SHAMT_W;

    acc = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_acc_hi),
                           Q6_L_tmp_mem_A(pw_acc_lo));
    src = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_src_hi),
                           Q6_L_tmp_mem_A(pw_src_lo));
    result = Q6_LWw_shracc_LWwRh(acc, src, scalar);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(result));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(result));

    exp = 0x1234c350;
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp;
    }
    check_wp("LX_shrar_w_w", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

int main(void)
{
    init_vtcm();
    enable_hlx();

    buf_src = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_shamt = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_acc = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_out = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    pw_src_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_src_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_acc_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_acc_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_out_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_out_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    init_hlx_vsp();

    /* Halfword shift-accumulate */
    test_shra_h_uh();
    test_shrar_h_uh();
    test_shra_h_h();
    test_shrar_h_h();

    /* Word shift-accumulate */
    test_shra_w_uw();
    test_shrar_w_uw();
    test_shra_w_w();
    test_shrar_w_w();

    puts(err ? "FAIL" : "PASS");
    return err;
}
