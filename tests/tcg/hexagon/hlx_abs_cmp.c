/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hlx_test.h"

#define SIGNED_VAL_A     ((int16_t)-150)
#define SIGNED_VAL_B     ((int16_t)200)
#define UNSIGNED_VAL_A   300
#define UNSIGNED_VAL_B   100

#define SIGNED_W_A       (-50000)
#define SIGNED_W_B       30000
#define UNSIGNED_W_A     100000u
#define UNSIGNED_W_B     70000u

#define SAT_VAL_H        ((int16_t)0x8000)  /* INT16_MIN for abs sat test */
#define SAT_VAL_W        ((int32_t)0x80000000)  /* INT32_MIN for abs sat test */

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

static inline int32_t my_abs(int32_t x)
{
    return x < 0 ? -x : x;
}

static inline int64_t my_abs64(int64_t x)
{
    return x < 0 ? -x : x;
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


static void test_abs_uh(void)
{
    HLX_Vector la;

    fill_h(buf_a, SIGNED_VAL_A);

    la = Q6_L_tmp_mem_A(buf_a);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_abs_Lh(la));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)my_abs((int32_t)SIGNED_VAL_A);
    }
    check_h("LX_abs_uh", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_abs_uh_sat(void)
{
    HLX_Vector la;

    fill_h(buf_a, SAT_VAL_H);

    la = Q6_L_tmp_mem_A(buf_a);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_abs_Lh_sat(la));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        int32_t v = my_abs((int32_t)SAT_VAL_H);
        expected[i] = (uint16_t)sat_h(v);
    }
    check_h("LX_abs_uh_sat", buf_out, expected, HLX_HALF_ELEMS);
}


static void test_abs_uw(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pc;

    /* Test with negative input */
    fill_w(pw_a_lo, pw_a_hi, SIGNED_W_A);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pc = Q6_LWuw_abs_LWw(pa);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)my_abs(SIGNED_W_A);
    }
    check_wp("LX_abs_uw (neg)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);

    /* Test with positive input */
    fill_w(pw_a_lo, pw_a_hi, SIGNED_W_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pc = Q6_LWuw_abs_LWw(pa);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)my_abs(SIGNED_W_B);
    }
    check_wp("LX_abs_uw (pos)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_abs_uw_sat(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pc;

    fill_w(pw_a_lo, pw_a_hi, SAT_VAL_W);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pc = Q6_LWw_abs_LWw_sat(pa);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    /*
     * abs(INT32_MIN) = 2^31, which overflows int32_t.
     * The hardware saturates this to INT32_MAX (0x7fffffff).
     */
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (uint32_t)sat_w(my_abs64((int64_t)SAT_VAL_W));
    }
    check_wp("LX_abs_uw_sat", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_abs_uhp(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pc;
    uint16_t abs_lo;
    uint16_t abs_hi;
    uint32_t exp_packed;
    uint32_t packed_a = pack_h((uint16_t)PAIRED_A_LO, (uint16_t)PAIRED_A_HI);
    fill_w(pw_a_lo, pw_a_hi, (int32_t)packed_a);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pc = Q6_LWuh_abs_LWh(pa);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    abs_lo = (uint16_t)my_abs((int32_t)PAIRED_A_LO);
    abs_hi = (uint16_t)my_abs((int32_t)PAIRED_A_HI);
    exp_packed = pack_h(abs_lo, abs_hi);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp_packed;
    }
    check_wp("LX_abs_uhp", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_abs_uhp_sat(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pc;
    uint16_t sat_lo;
    uint16_t sat_hi;
    uint32_t exp_packed;
    uint32_t packed_sat = pack_h((uint16_t)SAT_VAL_H, (uint16_t)SAT_VAL_H);
    fill_w(pw_a_lo, pw_a_hi, (int32_t)packed_sat);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pc = Q6_LWh_abs_LWh_sat(pa);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    sat_lo = (uint16_t)sat_h(my_abs((int32_t)SAT_VAL_H));
    sat_hi = sat_lo;
    exp_packed = pack_h(sat_lo, sat_hi);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp_packed;
    }
    check_wp("LX_abs_uhp_sat", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}


static void test_absdiff_h(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, SIGNED_VAL_A);
    fill_h(buf_b, SIGNED_VAL_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_absdiff_LhLh(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (uint16_t)my_abs((int32_t)SIGNED_VAL_A -
                                       (int32_t)SIGNED_VAL_B);
    }
    check_h("LX_absdiff_h", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_absdiff_uh(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, (int16_t)UNSIGNED_VAL_A);
    fill_h(buf_b, (int16_t)UNSIGNED_VAL_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Luh_absdiff_LuhLuh(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (UNSIGNED_VAL_A > UNSIGNED_VAL_B) ?
                      (UNSIGNED_VAL_A - UNSIGNED_VAL_B) :
                      (UNSIGNED_VAL_B - UNSIGNED_VAL_A);
    }
    check_h("LX_absdiff_uh", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_absdiff_w(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    /* Test with first pair of values: A - B */
    fill_w(pw_a_lo, pw_a_hi, SIGNED_W_A);
    fill_w(pw_b_lo, pw_b_hi, SIGNED_W_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuw_absdiff_LWwLWw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        int64_t diff = (int64_t)SIGNED_W_A - (int64_t)SIGNED_W_B;
        expected_w[j] = (uint32_t)my_abs64(diff);
    }
    check_wp("LX_absdiff_w (a-b)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);

    /* Test with swapped values: B - A */
    fill_w(pw_a_lo, pw_a_hi, SIGNED_W_B);
    fill_w(pw_b_lo, pw_b_hi, SIGNED_W_A);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuw_absdiff_LWwLWw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        int64_t diff = (int64_t)SIGNED_W_B - (int64_t)SIGNED_W_A;
        expected_w[j] = (uint32_t)my_abs64(diff);
    }
    check_wp("LX_absdiff_w (b-a)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_absdiff_uw(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    /* Test with first pair of values: A - B */
    fill_w(pw_a_lo, pw_a_hi, (int32_t)UNSIGNED_W_A);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)UNSIGNED_W_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuw_absdiff_LWuwLWuw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (UNSIGNED_W_A > UNSIGNED_W_B) ?
                        (UNSIGNED_W_A - UNSIGNED_W_B) :
                        (UNSIGNED_W_B - UNSIGNED_W_A);
    }
    check_wp("LX_absdiff_uw (a-b)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);

    /* Test with swapped values: B - A */
    fill_w(pw_a_lo, pw_a_hi, (int32_t)UNSIGNED_W_B);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)UNSIGNED_W_A);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWuw_absdiff_LWuwLWuw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (UNSIGNED_W_B > UNSIGNED_W_A) ?
                        (UNSIGNED_W_B - UNSIGNED_W_A) :
                        (UNSIGNED_W_A - UNSIGNED_W_B);
    }
    check_wp("LX_absdiff_uw (b-a)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}


static void test_cmp_gt_h(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, SIGNED_VAL_A);
    fill_h(buf_b, SIGNED_VAL_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_cmp_gt_LhLh(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (SIGNED_VAL_A > SIGNED_VAL_B) ? 0xFFFF : 0x0000;
    }
    check_h("LX_cmp_gt_h", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_cmp_gt_uh(void)
{
    HLX_Vector la;
    HLX_Vector lb;

    fill_h(buf_a, (int16_t)UNSIGNED_VAL_A);
    fill_h(buf_b, (int16_t)UNSIGNED_VAL_B);

    la = Q6_L_tmp_mem_A(buf_a);
    lb = Q6_L_tmp_mem_A(buf_b);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_cmp_gt_LuhLuh(la, lb));

    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = (UNSIGNED_VAL_A > UNSIGNED_VAL_B) ? 0xFFFF : 0x0000;
    }
    check_h("LX_cmp_gt_uh", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_cmp_gt_w(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    /* Test A > B */
    fill_w(pw_a_lo, pw_a_hi, SIGNED_W_A);
    fill_w(pw_b_lo, pw_b_hi, SIGNED_W_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_cmp_gt_LWwLWw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (SIGNED_W_A > SIGNED_W_B) ? 0xFFFFFFFF : 0x0;
    }
    check_wp("LX_cmp_gt_w (a>b)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);

    /* Test B > A */
    fill_w(pw_a_lo, pw_a_hi, SIGNED_W_B);
    fill_w(pw_b_lo, pw_b_hi, SIGNED_W_A);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_cmp_gt_LWwLWw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (SIGNED_W_B > SIGNED_W_A) ? 0xFFFFFFFF : 0x0;
    }
    check_wp("LX_cmp_gt_w (b>a)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_cmp_gt_uw(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;

    /* Test A > B */
    fill_w(pw_a_lo, pw_a_hi, (int32_t)UNSIGNED_W_A);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)UNSIGNED_W_B);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_cmp_gt_LWuwLWuw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (UNSIGNED_W_A > UNSIGNED_W_B) ? 0xFFFFFFFF : 0x0;
    }
    check_wp("LX_cmp_gt_uw (a>b)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);

    /* Test B > A */
    fill_w(pw_a_lo, pw_a_hi, (int32_t)UNSIGNED_W_B);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)UNSIGNED_W_A);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWw_cmp_gt_LWuwLWuw(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = (UNSIGNED_W_B > UNSIGNED_W_A) ? 0xFFFFFFFF : 0x0;
    }
    check_wp("LX_cmp_gt_uw (b>a)", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_cmp_gt_hp(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    uint16_t cmp_lo;
    uint16_t cmp_hi;
    uint32_t exp_packed;
    uint32_t packed_a = pack_h((uint16_t)PAIRED_A_LO, (uint16_t)PAIRED_A_HI);
    uint32_t packed_b = pack_h((uint16_t)PAIRED_B_LO, (uint16_t)PAIRED_B_HI);
    fill_w(pw_a_lo, pw_a_hi, (int32_t)packed_a);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)packed_b);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWh_cmp_gt_LWhLWh(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    cmp_lo = (PAIRED_A_LO > PAIRED_B_LO) ? 0xFFFF : 0x0000;
    cmp_hi = (PAIRED_A_HI > PAIRED_B_HI) ? 0xFFFF : 0x0000;
    exp_packed = pack_h(cmp_lo, cmp_hi);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp_packed;
    }
    check_wp("LX_cmp_gt_hp", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_cmp_gt_uhp(void)
{
    HLX_VectorPair pa;
    HLX_VectorPair pb;
    HLX_VectorPair pc;
    uint16_t cmp_lo;
    uint16_t cmp_hi;
    uint32_t exp_packed;
    uint32_t packed_a = pack_h((uint16_t)PAIRED_A_LO, (uint16_t)PAIRED_A_HI);
    uint32_t packed_b = pack_h((uint16_t)PAIRED_B_LO, (uint16_t)PAIRED_B_HI);
    fill_w(pw_a_lo, pw_a_hi, (int32_t)packed_a);
    fill_w(pw_b_lo, pw_b_hi, (int32_t)packed_b);

    pa = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_a_hi),
                          Q6_L_tmp_mem_A(pw_a_lo));
    pb = Q6_LW_combine_LL(Q6_L_tmp_mem_A(pw_b_hi),
                          Q6_L_tmp_mem_A(pw_b_lo));
    pc = Q6_LWh_cmp_gt_LWuhLWuh(pa, pb);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pc));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pc));

    cmp_lo = ((uint16_t)PAIRED_A_LO > (uint16_t)PAIRED_B_LO) ?
             0xFFFF : 0x0000;
    cmp_hi = ((uint16_t)PAIRED_A_HI > (uint16_t)PAIRED_B_HI) ?
             0xFFFF : 0x0000;
    exp_packed = pack_h(cmp_lo, cmp_hi);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp_packed;
    }
    check_wp("LX_cmp_gt_uhp", pw_out_lo, pw_out_hi,
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

    /* Absolute value */
    test_abs_uh();
    test_abs_uh_sat();
    test_abs_uw();
    test_abs_uw_sat();
    test_abs_uhp();
    test_abs_uhp_sat();

    /* Absolute differences */
    test_absdiff_h();
    test_absdiff_uh();
    test_absdiff_w();
    test_absdiff_uw();

    /* Compare greater-than */
    test_cmp_gt_h();
    test_cmp_gt_uh();
    test_cmp_gt_w();
    test_cmp_gt_uw();
    test_cmp_gt_hp();
    test_cmp_gt_uhp();

    puts(err ? "FAIL" : "PASS");
    return err;
}
