/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hlx_test.h"

#define SIGNED_A         ((int16_t)-150)
#define SIGNED_B         ((int16_t)200)
#define UNSIGNED_A       300
#define UNSIGNED_B       100

#define SIGNED_W_A       (-50000)
#define SIGNED_W_B       30000
#define UNSIGNED_W_A     100000u
#define UNSIGNED_W_B     70000u

#define PAIRED_A_LO      ((int16_t)(-150))
#define PAIRED_A_HI      ((int16_t)200)
#define PAIRED_B_LO      ((int16_t)100)
#define PAIRED_B_HI      ((int16_t)(-50))

static uint16_t *buf_a;
static uint16_t *buf_b;
static uint16_t *buf_out;
static uint16_t expected[HLX_HALF_ELEMS];

static uint32_t *pw_a_lo;
static uint32_t *pw_a_hi;
static uint32_t *pw_b_lo;
static uint32_t *pw_b_hi;
static uint32_t *pw_out_lo;
static uint32_t *pw_out_hi;
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


static void test_max_h(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, SIGNED_A);
    fill_h(buf_b, SIGNED_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_max_LhLh(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)(SIGNED_A > SIGNED_B ? SIGNED_A : SIGNED_B);
    }
    check_h("LX_max_h", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_min_h(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, SIGNED_A);
    fill_h(buf_b, SIGNED_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_min_LhLh(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)(SIGNED_A < SIGNED_B ? SIGNED_A : SIGNED_B);
    }
    check_h("LX_min_h", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_max_uh(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, (int16_t)UNSIGNED_A);
    fill_h(buf_b, (int16_t)UNSIGNED_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_max_LuhLuh(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = UNSIGNED_A > UNSIGNED_B ? UNSIGNED_A : UNSIGNED_B;
    }
    check_h("LX_max_uh", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_min_uh(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, (int16_t)UNSIGNED_A);
    fill_h(buf_b, (int16_t)UNSIGNED_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_min_LuhLuh(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = UNSIGNED_A < UNSIGNED_B ? UNSIGNED_A : UNSIGNED_B;
    }
    check_h("LX_min_uh", buf_out, expected, HLX_HALF_ELEMS);
}


static void test_max_w(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    /* Test max(A, B) */
    fill_w(pw_a_lo, pw_a_hi, SIGNED_W_A);
    fill_w(pw_b_lo, pw_b_hi, SIGNED_W_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_max_LWwLWw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)(SIGNED_W_A > SIGNED_W_B ?
                                   SIGNED_W_A : SIGNED_W_B);
    }
    check_wp("LX_max_w (a,b)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);

    /* Test max(B, A) */
    fill_w(pw_a_lo, pw_a_hi, SIGNED_W_B);
    fill_w(pw_b_lo, pw_b_hi, SIGNED_W_A);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_max_LWwLWw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)(SIGNED_W_B > SIGNED_W_A ?
                                   SIGNED_W_B : SIGNED_W_A);
    }
    check_wp("LX_max_w (b,a)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_min_w(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    /* Test min(A, B) */
    fill_w(pw_a_lo, pw_a_hi, SIGNED_W_A);
    fill_w(pw_b_lo, pw_b_hi, SIGNED_W_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_min_LWwLWw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)(SIGNED_W_A < SIGNED_W_B ?
                                   SIGNED_W_A : SIGNED_W_B);
    }
    check_wp("LX_min_w (a,b)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);

    /* Test min(B, A) */
    fill_w(pw_a_lo, pw_a_hi, SIGNED_W_B);
    fill_w(pw_b_lo, pw_b_hi, SIGNED_W_A);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_min_LWwLWw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)(SIGNED_W_B < SIGNED_W_A ?
                                   SIGNED_W_B : SIGNED_W_A);
    }
    check_wp("LX_min_w (b,a)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_max_uw(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    /* Test max(A, B) */
    fill_w(pw_a_lo, pw_a_hi, (int32_t)UNSIGNED_W_A);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)UNSIGNED_W_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuw_max_LWuwLWuw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = UNSIGNED_W_A > UNSIGNED_W_B ?
                        UNSIGNED_W_A : UNSIGNED_W_B;
    }
    check_wp("LX_max_uw (a,b)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);

    /* Test max(B, A) */
    fill_w(pw_a_lo, pw_a_hi, (int32_t)UNSIGNED_W_B);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)UNSIGNED_W_A);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuw_max_LWuwLWuw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = UNSIGNED_W_B > UNSIGNED_W_A ?
                        UNSIGNED_W_B : UNSIGNED_W_A;
    }
    check_wp("LX_max_uw (b,a)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_min_uw(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    /* Test min(A, B) */
    fill_w(pw_a_lo, pw_a_hi, (int32_t)UNSIGNED_W_A);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)UNSIGNED_W_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuw_min_LWuwLWuw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = UNSIGNED_W_A < UNSIGNED_W_B ?
                        UNSIGNED_W_A : UNSIGNED_W_B;
    }
    check_wp("LX_min_uw (a,b)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);

    /* Test min(B, A) */
    fill_w(pw_a_lo, pw_a_hi, (int32_t)UNSIGNED_W_B);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)UNSIGNED_W_A);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuw_min_LWuwLWuw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = UNSIGNED_W_B < UNSIGNED_W_A ?
                        UNSIGNED_W_B : UNSIGNED_W_A;
    }
    check_wp("LX_min_uw (b,a)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}


static void test_max_hp(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    uint32_t exp_packed;
    uint32_t packed_a = pack_h((uint16_t)PAIRED_A_LO, (uint16_t)PAIRED_A_HI);
    uint32_t packed_b = pack_h((uint16_t)PAIRED_B_LO, (uint16_t)PAIRED_B_HI);
    fill_w(pw_a_lo, pw_a_hi, (int32_t)packed_a);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)packed_b);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWh_max_LWhLWh(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    exp_packed = pack_h(
        (uint16_t)(PAIRED_A_LO > PAIRED_B_LO ? PAIRED_A_LO : PAIRED_B_LO),
        (uint16_t)(PAIRED_A_HI > PAIRED_B_HI ? PAIRED_A_HI : PAIRED_B_HI));
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp_packed;
    }
    check_wp("LX_max_hp", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_min_hp(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    uint32_t exp_packed;
    uint32_t packed_a = pack_h((uint16_t)PAIRED_A_LO, (uint16_t)PAIRED_A_HI);
    uint32_t packed_b = pack_h((uint16_t)PAIRED_B_LO, (uint16_t)PAIRED_B_HI);
    fill_w(pw_a_lo, pw_a_hi, (int32_t)packed_a);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)packed_b);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWh_min_LWhLWh(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    exp_packed = pack_h(
        (uint16_t)(PAIRED_A_LO < PAIRED_B_LO ? PAIRED_A_LO : PAIRED_B_LO),
        (uint16_t)(PAIRED_A_HI < PAIRED_B_HI ? PAIRED_A_HI : PAIRED_B_HI));
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp_packed;
    }
    check_wp("LX_min_hp", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_max_uhp(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    uint16_t a_lo;
    uint16_t b_lo;
    uint16_t a_hi;
    uint16_t b_hi;
    uint32_t exp_packed;
    uint32_t packed_a = pack_h((uint16_t)PAIRED_A_LO, (uint16_t)PAIRED_A_HI);
    uint32_t packed_b = pack_h((uint16_t)PAIRED_B_LO, (uint16_t)PAIRED_B_HI);
    fill_w(pw_a_lo, pw_a_hi, (int32_t)packed_a);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)packed_b);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuh_max_LWuhLWuh(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    a_lo = (uint16_t)PAIRED_A_LO;

    b_lo = (uint16_t)PAIRED_B_LO;
    a_hi = (uint16_t)PAIRED_A_HI;
    b_hi = (uint16_t)PAIRED_B_HI;
    exp_packed = pack_h(a_lo > b_lo ? a_lo : b_lo,
                         a_hi > b_hi ? a_hi : b_hi);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp_packed;
    }
    check_wp("LX_max_uhp", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_min_uhp(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    uint16_t a_lo;
    uint16_t b_lo;
    uint16_t a_hi;
    uint16_t b_hi;
    uint32_t exp_packed;
    uint32_t packed_a = pack_h((uint16_t)PAIRED_A_LO, (uint16_t)PAIRED_A_HI);
    uint32_t packed_b = pack_h((uint16_t)PAIRED_B_LO, (uint16_t)PAIRED_B_HI);
    fill_w(pw_a_lo, pw_a_hi, (int32_t)packed_a);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)packed_b);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuh_min_LWuhLWuh(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    a_lo = (uint16_t)PAIRED_A_LO;

    b_lo = (uint16_t)PAIRED_B_LO;
    a_hi = (uint16_t)PAIRED_A_HI;
    b_hi = (uint16_t)PAIRED_B_HI;
    exp_packed = pack_h(a_lo < b_lo ? a_lo : b_lo,
                         a_hi < b_hi ? a_hi : b_hi);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp_packed;
    }
    check_wp("LX_min_uhp", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}


static void test_avg_h(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, SIGNED_A);
    fill_h(buf_b, SIGNED_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_avg_LhLh(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        int32_t sum = (int32_t)SIGNED_A + (int32_t)SIGNED_B;
        expected[i] = (uint16_t)(int16_t)(sum >> 1);
    }
    check_h("LX_avg_h", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_avg_h_rnd(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, SIGNED_A);
    fill_h(buf_b, SIGNED_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_avg_LhLh_rnd(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        int32_t sum = (int32_t)SIGNED_A + (int32_t)SIGNED_B + 1;
        expected[i] = (uint16_t)(int16_t)(sum >> 1);
    }
    check_h("LX_avg_h_rnd", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_navg_h(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, SIGNED_A);
    fill_h(buf_b, SIGNED_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_navg_LhLh(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        int32_t diff = (int32_t)SIGNED_A - (int32_t)SIGNED_B;
        expected[i] = (uint16_t)(int16_t)(diff >> 1);
    }
    check_h("LX_navg_h", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_avg_uh(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, (int16_t)UNSIGNED_A);
    fill_h(buf_b, (int16_t)UNSIGNED_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_avg_LuhLuh(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        uint32_t sum = (uint32_t)UNSIGNED_A + (uint32_t)UNSIGNED_B;
        expected[i] = (uint16_t)(sum >> 1);
    }
    check_h("LX_avg_uh", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_avg_uh_rnd(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, (int16_t)UNSIGNED_A);
    fill_h(buf_b, (int16_t)UNSIGNED_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_avg_LuhLuh_rnd(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        uint32_t sum = (uint32_t)UNSIGNED_A + (uint32_t)UNSIGNED_B + 1;
        expected[i] = (uint16_t)(sum >> 1);
    }
    check_h("LX_avg_uh_rnd", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_navg_uh(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, (int16_t)UNSIGNED_A);
    fill_h(buf_b, (int16_t)UNSIGNED_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_navg_LuhLuh(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        int32_t diff = (int32_t)(uint32_t)UNSIGNED_A -
                       (int32_t)(uint32_t)UNSIGNED_B;
        expected[i] = (uint16_t)(int16_t)(diff >> 1);
    }
    check_h("LX_navg_uh", buf_out, expected, HLX_HALF_ELEMS);
}


static void test_avg_w(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    fill_w(pw_a_lo, pw_a_hi, SIGNED_W_A);
    fill_w(pw_b_lo, pw_b_hi, SIGNED_W_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_avg_LWwLWw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        int64_t sum = (int64_t)SIGNED_W_A + (int64_t)SIGNED_W_B;
        expected_w[j] = (uint32_t)(int32_t)(sum >> 1);
    }
    check_wp("LX_avg_w", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_avg_w_rnd(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    fill_w(pw_a_lo, pw_a_hi, SIGNED_W_A);
    fill_w(pw_b_lo, pw_b_hi, SIGNED_W_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_avg_LWwLWw_rnd(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        int64_t sum = (int64_t)SIGNED_W_A + (int64_t)SIGNED_W_B + 1;
        expected_w[j] = (uint32_t)(int32_t)(sum >> 1);
    }
    check_wp("LX_avg_w_rnd", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_navg_w(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    /* Test navg(A, B) = (A - B) / 2 */
    fill_w(pw_a_lo, pw_a_hi, SIGNED_W_A);
    fill_w(pw_b_lo, pw_b_hi, SIGNED_W_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_navg_LWwLWw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        int64_t diff = (int64_t)SIGNED_W_A - (int64_t)SIGNED_W_B;
        expected_w[j] = (uint32_t)(int32_t)(diff >> 1);
    }
    check_wp("LX_navg_w (a-b)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);

    /* Test navg(B, A) = (B - A) / 2 */
    fill_w(pw_a_lo, pw_a_hi, SIGNED_W_B);
    fill_w(pw_b_lo, pw_b_hi, SIGNED_W_A);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_navg_LWwLWw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        int64_t diff = (int64_t)SIGNED_W_B - (int64_t)SIGNED_W_A;
        expected_w[j] = (uint32_t)(int32_t)(diff >> 1);
    }
    check_wp("LX_navg_w (b-a)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_avg_uw(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    fill_w(pw_a_lo, pw_a_hi, (int32_t)UNSIGNED_W_A);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)UNSIGNED_W_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuw_avg_LWuwLWuw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        uint64_t sum = (uint64_t)UNSIGNED_W_A + (uint64_t)UNSIGNED_W_B;
        expected_w[j] = (uint32_t)(sum >> 1);
    }
    check_wp("LX_avg_uw", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_avg_uw_rnd(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    fill_w(pw_a_lo, pw_a_hi, (int32_t)UNSIGNED_W_A);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)UNSIGNED_W_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuw_avg_LWuwLWuw_rnd(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        uint64_t sum = (uint64_t)UNSIGNED_W_A + (uint64_t)UNSIGNED_W_B + 1;
        expected_w[j] = (uint32_t)(sum >> 1);
    }
    check_wp("LX_avg_uw_rnd", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_navg_uw(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    /* Test navg(A, B) = (A - B) / 2 */
    fill_w(pw_a_lo, pw_a_hi, (int32_t)UNSIGNED_W_A);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)UNSIGNED_W_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_navg_LWuwLWuw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        int64_t diff = (int64_t)(uint64_t)UNSIGNED_W_A -
                       (int64_t)(uint64_t)UNSIGNED_W_B;
        expected_w[j] = (uint32_t)(int32_t)(diff >> 1);
    }
    check_wp("LX_navg_uw (a-b)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);

    /* Test navg(B, A) = (B - A) / 2 */
    fill_w(pw_a_lo, pw_a_hi, (int32_t)UNSIGNED_W_B);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)UNSIGNED_W_A);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_navg_LWuwLWuw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        int64_t diff = (int64_t)(uint64_t)UNSIGNED_W_B -
                       (int64_t)(uint64_t)UNSIGNED_W_A;
        expected_w[j] = (uint32_t)(int32_t)(diff >> 1);
    }
    check_wp("LX_navg_uw (b-a)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

int main(void)
{
    init_vtcm();
    enable_hlx();

    buf_a = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_b = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_out = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    pw_a_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_a_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_b_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_b_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_out_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_out_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    init_hlx_vsp();

    /* Single-register min/max */
    test_max_h();
    test_min_h();
    test_max_uh();
    test_min_uh();

    /* Pair min/max (word) */
    test_max_w();
    test_min_w();
    test_max_uw();
    test_min_uw();

    /* Paired halfword min/max */
    test_max_hp();
    test_min_hp();
    test_max_uhp();
    test_min_uhp();

    /* Single-register average */
    test_avg_h();
    test_avg_h_rnd();
    test_navg_h();
    test_avg_uh();
    test_avg_uh_rnd();
    test_navg_uh();

    /* Pair average (word) */
    test_avg_w();
    test_avg_w_rnd();
    test_navg_w();
    test_avg_uw();
    test_avg_uw_rnd();
    test_navg_uw();

    puts(err ? "FAIL" : "PASS");
    return err;
}
