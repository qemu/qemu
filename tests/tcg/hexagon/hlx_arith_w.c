/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hlx_test.h"

#define WORD_A           1000
#define WORD_B           2000
#define NEAR_INT32_MAX   0x7FFFFF00
#define NEAR_INT32_MIN   ((int32_t)0x80000100)
#define NEAR_UINT32_MAX  0xFFFFFF00u
#define SAT_STEP_W       256
#define HALF_A           100
#define HALF_B           200
#define PAIRED_A_LO      ((int16_t)300)
#define PAIRED_A_HI      ((int16_t)500)
#define PAIRED_B_LO      ((int16_t)700)
#define PAIRED_B_HI      ((int16_t)900)
#define PAIRED_SAT_A     ((int16_t)0x7F00)
#define PAIRED_SAT_B     ((int16_t)1)

static uint32_t *pw_a_lo;
static uint32_t *pw_a_hi;
static uint32_t *pw_b_lo;
static uint32_t *pw_b_hi;
static uint32_t *pw_out_lo;
static uint32_t *pw_out_hi;
static uint32_t expected_w[HLX_HALF_ELEMS];

static uint16_t *buf_a;
static uint16_t *buf_b;


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

/* Helper: pack two halfwords into a word */
static inline uint32_t pack_h(uint16_t lo, uint16_t hi)
{
    return ((uint32_t)hi << 16) | lo;
}


static void test_add_w(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    fill_w(pw_a_lo, pw_a_hi, WORD_A);
    fill_w(pw_b_lo, pw_b_hi, WORD_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_add_LWwLWw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)((int32_t)WORD_A + (int32_t)WORD_B);
    }
    check_wp("LX_add_w", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_add_w_sat(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    fill_w(pw_a_lo, pw_a_hi, NEAR_INT32_MAX);
    fill_w(pw_b_lo, pw_b_hi, SAT_STEP_W);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_add_LWwLWw_sat(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)sat_w((int64_t)NEAR_INT32_MAX +
                                        (int64_t)SAT_STEP_W);
    }
    check_wp("LX_add_w_sat", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_add_uw_sat(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    fill_w(pw_a_lo, pw_a_hi, (int32_t)NEAR_UINT32_MAX);
    fill_w(pw_b_lo, pw_b_hi, SAT_STEP_W);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuw_add_LWuwLWuw_sat(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        int64_t sum = (int64_t)(uint64_t)NEAR_UINT32_MAX +
                      (int64_t)(uint64_t)SAT_STEP_W;
        expected_w[j] = sat_uw(sum);
    }
    check_wp("LX_add_uw_sat", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_add_uw_w_sat(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    fill_w(pw_a_lo, pw_a_hi, WORD_A);
    fill_w(pw_b_lo, pw_b_hi, WORD_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuw_add_LWwLWw_sat(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        int64_t sum = (int64_t)(int32_t)WORD_A +
                      (int64_t)(int32_t)WORD_B;
        expected_w[j] = sat_uw(sum);
    }
    check_wp("LX_add_uw_w_sat", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_sub_w(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    fill_w(pw_a_lo, pw_a_hi, WORD_A);
    fill_w(pw_b_lo, pw_b_hi, WORD_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_sub_LWwLWw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)((int32_t)WORD_A - (int32_t)WORD_B);
    }
    check_wp("LX_sub_w", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_sub_w_sat(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    fill_w(pw_a_lo, pw_a_hi, NEAR_INT32_MIN);
    fill_w(pw_b_lo, pw_b_hi, SAT_STEP_W);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_sub_LWwLWw_sat(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)sat_w((int64_t)NEAR_INT32_MIN -
                                        (int64_t)SAT_STEP_W);
    }
    check_wp("LX_sub_w_sat", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_sub_uw_sat(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    fill_w(pw_a_lo, pw_a_hi, WORD_A);
    fill_w(pw_b_lo, pw_b_hi, WORD_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuw_sub_LWuwLWuw_sat(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        int64_t diff = (int64_t)(uint64_t)(uint32_t)WORD_A -
                       (int64_t)(uint64_t)(uint32_t)WORD_B;
        expected_w[j] = sat_uw(diff);
    }
    check_wp("LX_sub_uw_sat", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_sub_uw_w_sat(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    fill_w(pw_a_lo, pw_a_hi, WORD_A);
    fill_w(pw_b_lo, pw_b_hi, WORD_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuw_sub_LWwLWw_sat(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        int64_t diff = (int64_t)(int32_t)WORD_A -
                       (int64_t)(int32_t)WORD_B;
        expected_w[j] = sat_uw(diff);
    }
    check_wp("LX_sub_uw_w_sat", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}


static void test_add_wwh(void)
{
    HLX_VectorPair pa;
    HLX_Vector la;
    HLX_VectorPair pc;

    fill_w(pw_a_lo, pw_a_hi, WORD_A);
    fill_h(buf_a, HALF_A);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    la = Q6_L_tmp_mem_A(buf_a);
    pc = Q6_LWw_add_LWwLh(pa, la);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)((int32_t)WORD_A + (int32_t)HALF_A);
    }
    check_wp("LX_add_wwh", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_add_wwuh(void)
{
    HLX_VectorPair pa;
    HLX_Vector la;
    HLX_VectorPair pc;

    fill_w(pw_a_lo, pw_a_hi, WORD_A);
    fill_h(buf_a, HALF_A);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    la = Q6_L_tmp_mem_A(buf_a);
    pc = Q6_LWw_add_LWwLuh(pa, la);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)((int32_t)WORD_A +
                                   (int32_t)(uint32_t)(uint16_t)HALF_A);
    }
    check_wp("LX_add_wwuh", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_sub_wwh(void)
{
    HLX_VectorPair pa;
    HLX_Vector la;
    HLX_VectorPair pc;

    fill_w(pw_a_lo, pw_a_hi, WORD_A);
    fill_h(buf_a, HALF_A);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    la = Q6_L_tmp_mem_A(buf_a);
    pc = Q6_LWw_sub_LWwLh(pa, la);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)((int32_t)WORD_A - (int32_t)HALF_A);
    }
    check_wp("LX_sub_wwh", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_sub_wwuh(void)
{
    HLX_VectorPair pa;
    HLX_Vector la;
    HLX_VectorPair pc;

    fill_w(pw_a_lo, pw_a_hi, WORD_A);
    fill_h(buf_a, HALF_A);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    la = Q6_L_tmp_mem_A(buf_a);
    pc = Q6_LWw_sub_LWwLuh(pa, la);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)((int32_t)WORD_A -
                                   (int32_t)(uint32_t)(uint16_t)HALF_A);
    }
    check_wp("LX_sub_wwuh", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}


static void test_add_wuh(void)
{
    HLX_Vector la;
    HLX_Vector lb;
    HLX_VectorPair pc;

    fill_h(buf_a, HALF_A);
    fill_h(buf_b, HALF_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    pc = Q6_LWw_add_LuhLuh(la, lb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)(uint16_t)HALF_A +
                        (uint32_t)(uint16_t)HALF_B;
    }
    check_wp("LX_add_wuh", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_sub_wuh(void)
{
    HLX_Vector la;
    HLX_Vector lb;
    HLX_VectorPair pc;

    fill_h(buf_a, HALF_B);
    fill_h(buf_b, HALF_A);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    pc = Q6_LWw_sub_LuhLuh(la, lb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)((int32_t)(uint32_t)(uint16_t)HALF_B -
                                   (int32_t)(uint32_t)(uint16_t)HALF_A);
    }
    check_wp("LX_sub_wuh", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}


static void test_add_hp(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    uint16_t elo;
    uint16_t ehi;

    fill_w(pw_a_lo, pw_a_hi,
           (int32_t)pack_h((uint16_t)PAIRED_A_LO,
                           (uint16_t)PAIRED_A_HI));
    fill_w(pw_b_lo, pw_b_hi,
           (int32_t)pack_h((uint16_t)PAIRED_B_LO,
                           (uint16_t)PAIRED_B_HI));

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWh_add_LWhLWh(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    elo = (uint16_t)((int16_t)PAIRED_A_LO + (int16_t)PAIRED_B_LO);
    ehi = (uint16_t)((int16_t)PAIRED_A_HI + (int16_t)PAIRED_B_HI);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = pack_h(elo, ehi);
    }
    check_wp("LX_add_hp", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_add_hp_sat(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    uint16_t elo;
    uint16_t ehi;

    fill_w(pw_a_lo, pw_a_hi,
           (int32_t)pack_h((uint16_t)PAIRED_SAT_A,
                           (uint16_t)PAIRED_SAT_A));
    fill_w(pw_b_lo, pw_b_hi,
           (int32_t)pack_h((uint16_t)PAIRED_SAT_B,
                           (uint16_t)PAIRED_SAT_B));

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWh_add_LWhLWh_sat(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    elo = (uint16_t)sat_h((int32_t)PAIRED_SAT_A
                           + (int32_t)PAIRED_SAT_B);
    ehi = (uint16_t)sat_h((int32_t)PAIRED_SAT_A
                           + (int32_t)PAIRED_SAT_B);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = pack_h(elo, ehi);
    }
    check_wp("LX_add_hp_sat", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_add_uhp_sat(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    uint16_t elo;
    uint16_t ehi;

    fill_w(pw_a_lo, pw_a_hi,
           (int32_t)pack_h((uint16_t)PAIRED_SAT_A,
                           (uint16_t)PAIRED_SAT_A));
    fill_w(pw_b_lo, pw_b_hi,
           (int32_t)pack_h((uint16_t)PAIRED_SAT_B,
                           (uint16_t)PAIRED_SAT_B));

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuh_add_LWuhLWuh_sat(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    elo = sat_uh((int32_t)(uint16_t)PAIRED_SAT_A +
                 (int32_t)(uint16_t)PAIRED_SAT_B);
    ehi = sat_uh((int32_t)(uint16_t)PAIRED_SAT_A +
                 (int32_t)(uint16_t)PAIRED_SAT_B);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = pack_h(elo, ehi);
    }
    check_wp("LX_add_uhp_sat", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_add_uhp_h_sat(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    uint16_t elo;
    uint16_t ehi;

    fill_w(pw_a_lo, pw_a_hi,
           (int32_t)pack_h((uint16_t)PAIRED_A_LO,
                           (uint16_t)PAIRED_A_HI));
    fill_w(pw_b_lo, pw_b_hi,
           (int32_t)pack_h((uint16_t)PAIRED_B_LO,
                           (uint16_t)PAIRED_B_HI));

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuh_add_LWhLWh_sat(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    elo = sat_uh((int32_t)PAIRED_A_LO + (int32_t)PAIRED_B_LO);
    ehi = sat_uh((int32_t)PAIRED_A_HI + (int32_t)PAIRED_B_HI);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = pack_h(elo, ehi);
    }
    check_wp("LX_add_uhp_h_sat", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_sub_hp(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    uint16_t elo;
    uint16_t ehi;

    fill_w(pw_a_lo, pw_a_hi,
           (int32_t)pack_h((uint16_t)PAIRED_A_LO,
                           (uint16_t)PAIRED_A_HI));
    fill_w(pw_b_lo, pw_b_hi,
           (int32_t)pack_h((uint16_t)PAIRED_B_LO,
                           (uint16_t)PAIRED_B_HI));

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWh_sub_LWhLWh(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    elo = (uint16_t)((int16_t)PAIRED_A_LO - (int16_t)PAIRED_B_LO);
    ehi = (uint16_t)((int16_t)PAIRED_A_HI - (int16_t)PAIRED_B_HI);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = pack_h(elo, ehi);
    }
    check_wp("LX_sub_hp", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_sub_hp_sat(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    uint16_t elo;
    uint16_t ehi;

    fill_w(pw_a_lo, pw_a_hi,
           (int32_t)pack_h((uint16_t)PAIRED_SAT_A,
                           (uint16_t)PAIRED_SAT_A));
    fill_w(pw_b_lo, pw_b_hi,
           (int32_t)pack_h((uint16_t)PAIRED_SAT_B,
                           (uint16_t)PAIRED_SAT_B));

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWh_sub_LWhLWh_sat(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    elo = (uint16_t)sat_h((int32_t)PAIRED_SAT_A
                           - (int32_t)PAIRED_SAT_B);
    ehi = (uint16_t)sat_h((int32_t)PAIRED_SAT_A
                           - (int32_t)PAIRED_SAT_B);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = pack_h(elo, ehi);
    }
    check_wp("LX_sub_hp_sat", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_sub_uhp_sat(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    uint16_t elo;
    uint16_t ehi;

    fill_w(pw_a_lo, pw_a_hi,
           (int32_t)pack_h((uint16_t)PAIRED_SAT_A,
                           (uint16_t)PAIRED_SAT_A));
    fill_w(pw_b_lo, pw_b_hi,
           (int32_t)pack_h((uint16_t)PAIRED_SAT_B,
                           (uint16_t)PAIRED_SAT_B));

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuh_sub_LWuhLWuh_sat(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    elo = sat_uh((int32_t)(uint16_t)PAIRED_SAT_A -
                 (int32_t)(uint16_t)PAIRED_SAT_B);
    ehi = sat_uh((int32_t)(uint16_t)PAIRED_SAT_A -
                 (int32_t)(uint16_t)PAIRED_SAT_B);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = pack_h(elo, ehi);
    }
    check_wp("LX_sub_uhp_sat", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_sub_uhp_h_sat(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    uint16_t elo;
    uint16_t ehi;

    fill_w(pw_a_lo, pw_a_hi,
           (int32_t)pack_h((uint16_t)PAIRED_A_LO,
                           (uint16_t)PAIRED_A_HI));
    fill_w(pw_b_lo, pw_b_hi,
           (int32_t)pack_h((uint16_t)PAIRED_B_LO,
                           (uint16_t)PAIRED_B_HI));

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuh_sub_LWhLWh_sat(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    elo = sat_uh((int32_t)PAIRED_A_LO - (int32_t)PAIRED_B_LO);
    ehi = sat_uh((int32_t)PAIRED_A_HI - (int32_t)PAIRED_B_HI);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = pack_h(elo, ehi);
    }
    check_wp("LX_sub_uhp_h_sat", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

int main(void)
{
    init_vtcm();
    enable_hlx();

    pw_a_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_a_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_b_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_b_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_out_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_out_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    buf_a = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_b = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    init_hlx_vsp();

    /* Pair-pair word operations */
    test_add_w();
    test_add_w_sat();
    test_add_uw_sat();
    test_add_uw_w_sat();
    test_sub_w();
    test_sub_w_sat();
    test_sub_uw_sat();
    test_sub_uw_w_sat();

    /* Mixed word+half operations */
    test_add_wwh();
    test_add_wwuh();
    test_sub_wwh();
    test_sub_wwuh();

    /* Widening operations */
    test_add_wuh();
    test_sub_wuh();

    /* Paired halfword operations */
    test_add_hp();
    test_add_hp_sat();
    test_add_uhp_sat();
    test_add_uhp_h_sat();
    test_sub_hp();
    test_sub_hp_sat();
    test_sub_uhp_sat();
    test_sub_uhp_h_sat();

    puts(err ? "FAIL" : "PASS");
    return err;
}
