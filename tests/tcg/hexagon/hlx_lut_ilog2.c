/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "hlx_test.h"

/*
 * Broadcast constants for uniform buffer init.
 *
 * LUT_SRC: top 2 bits = 3 (selects table entry[3]), lower 14 bits nonzero
 * (so lut4:mask produces 0).
 */
#define LUT_SRC         ((int16_t)0xC123)

/* A second LUT source whose lower 14 bits are zero (so lut4:mask passes) */
#define LUT_SRC_MASK    ((int16_t)0xC000)   /* top 2 bits = 3, lower = 0 */

/* ilog2 sources */
#define ILOG_SRC_UH     ((int16_t)500)      /* unsigned halfword */
#define ILOG_SRC_SH     ((int16_t)-500)     /* signed halfword */
#define ILOG_SRC_UW     ((int32_t)100000)   /* unsigned word */
#define ILOG_SRC_SW     ((int32_t)-100000)  /* signed word */

static uint16_t *buf_src;
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


static void test_lut4(void)
{
    int16_t entries[4] = { 100, -200, 300, -400 };
    uint64_t table;
    HLX_Vector la;
    int idx;
    uint16_t exp_val;

    fill_h(buf_src, LUT_SRC);

    /*
     * Ld.h = lut4(Lu.uh, Rtt.h): look up by top 2 bits of each element
     * Table entries in Rtt: h[0], h[1], h[2], h[3]
     */
    memcpy(&table, entries, 8);

    la = Q6_L_tmp_mem_A(buf_src);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_lut4_LuhPh(la, table));

    idx = ((uint16_t)LUT_SRC >> 14) & 0x3;
    exp_val = (uint16_t)entries[idx];
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp_val;
    }
    check_h("LX_lut4", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_lut4_sh(void)
{
    uint16_t entries[4] = { 10, 200, 3000, 40000 };
    uint64_t table;
    HLX_Vector la;
    HLX_VectorPair pp;
    int idx;
    uint32_t exp_val;

    fill_h(buf_src, LUT_SRC);

    /*
     * Ldd.w = lut4(Lu.uh, Rtt.uh):>>1
     * Semantics: LddV.l[i].w = (int32_t)(uint16_t entry) << 15
     */
    memcpy(&table, entries, 8);

    la = Q6_L_tmp_mem_A(buf_src);
    pp = Q6_LWw_lut4_LuhPuh_rs1(la, table);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pp));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pp));

    idx = ((uint16_t)LUT_SRC >> 14) & 0x3;
    exp_val = (uint32_t)((int32_t)(uint32_t)entries[idx] << 15);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp_val;
    }
    check_wp("LX_lut4_sh", pw_out_lo, pw_out_hi, expected_w, HLX_HALF_ELEMS);
}

static void test_lut4_sh_neg(void)
{
    uint16_t entries[4] = { 10, 200, 3000, 40000 };
    uint64_t table;
    HLX_Vector la;
    HLX_VectorPair pp;
    int idx;
    uint32_t e;
    uint32_t exp_val;

    fill_h(buf_src, LUT_SRC);

    /*
     * Ldd.w = lut4(Lu.uh, Rtt.uh):>>1:neg
     * Semantics: LddV.l[i].w = ((uint32_t)entry - (1<<16)) << 15
     */
    memcpy(&table, entries, 8);

    la = Q6_L_tmp_mem_A(buf_src);
    pp = Q6_LWw_lut4_LuhPuh_rs1_neg(la, table);
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_lo, Q6_L_lo_LW(pp));
    Q6_mem_Ku16_new_AL((uint16_t *)pw_out_hi, Q6_L_hi_LW(pp));

    idx = ((uint16_t)LUT_SRC >> 14) & 0x3;
    e = (uint32_t)entries[idx];
    exp_val = (uint32_t)((uint32_t)((int32_t)(e - (1u << 16))) << 15);
    for (int j = 0; j < HLX_HALF_ELEMS; j++) {
        expected_w[j] = exp_val;
    }
    check_wp("LX_lut4_sh_neg", pw_out_lo, pw_out_hi,
             expected_w, HLX_HALF_ELEMS);
}

static void test_lut4_mask(void)
{
    int16_t entries[4] = { 100, -200, 300, -400 };
    uint64_t table;
    HLX_Vector la;
    int idx;
    int16_t val;
    int mask;
    uint16_t exp_val;

    /*
     * Ld.h = lut4(Lu.uh, Rtt.h):mask
     * If lower 14 bits are 0, use table value; otherwise 0.
     *
     * Use LUT_SRC_MASK (lower 14 bits == 0) so the mask passes.
     */
    fill_h(buf_src, LUT_SRC_MASK);

    memcpy(&table, entries, 8);

    la = Q6_L_tmp_mem_A(buf_src);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_lut4_LuhPh_mask(la, table));

    idx = ((uint16_t)LUT_SRC_MASK >> 14) & 0x3;
    val = entries[idx];
    mask = ((uint16_t)LUT_SRC_MASK & 0x3FFF) == 0 ? 0xFFFF : 0;
    exp_val = (uint16_t)(val & mask);
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp_val;
    }
    check_h("LX_lut4_mask", buf_out, expected, HLX_HALF_ELEMS);
}


static void test_sub_ilog2_uh(void)
{
    HLX_Vector la;
    uint16_t exp_val;

    fill_h(buf_src, ILOG_SRC_UH);

    /*
     * Ld.h = sub(ilog2(Lu.uh), #u4)
     * Semantics: fBIT_WIDTH(16, Lu.uh[i]) - 1 - u4
     */
    la = Q6_L_tmp_mem_A(buf_src);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_sub_ilog2_LuhI(la, 2));

    /* bit_width(500) = 9 */
    exp_val = 9 - 1 - 2;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp_val;
    }
    check_h("LX_sub_ilog2_uh", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_sub_ilog2_h(void)
{
    HLX_Vector la;
    uint16_t exp_val;

    fill_h(buf_src, ILOG_SRC_SH);

    /*
     * Ld.h = sub(ilog2(Lu.h), #u4)
     * Semantics: fBIT_WIDTH(16, Lu.h[i] ^ -(Lu.h[i] < 0)) - 1 - u4
     */
    la = Q6_L_tmp_mem_A(buf_src);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_sub_ilog2_LhI(la, 3));

    /* bit_width(-500 ^ -1) = bit_width(499) = 9 */
    exp_val = 9 - 1 - 3;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp_val;
    }
    check_h("LX_sub_ilog2_h", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_sub_ilog2_uw(void)
{
    HLX_Vector l_lo;
    HLX_Vector l_hi;
    HLX_VectorPair pa;
    uint16_t exp_val;

    fill_w(pw_src_lo, pw_src_hi, ILOG_SRC_UW);

    /* Ld.h = sub(ilog2(Luu.uw), #u5) */
    l_lo = Q6_L_tmp_mem_A(pw_src_lo);
    l_hi = Q6_L_tmp_mem_A(pw_src_hi);
    pa = Q6_LW_combine_LL(l_hi, l_lo);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_sub_ilog2_LWuwI(pa, 4));

    /* bit_width(100000) = 17 */
    exp_val = 17 - 1 - 4;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp_val;
    }
    check_h("LX_sub_ilog2_uw", buf_out, expected, HLX_HALF_ELEMS);
}

static void test_sub_ilog2_w(void)
{
    HLX_Vector l_lo;
    HLX_Vector l_hi;
    HLX_VectorPair pa;
    uint16_t exp_val;

    fill_w(pw_src_lo, pw_src_hi, ILOG_SRC_SW);

    /* Ld.h = sub(ilog2(Luu.w), #u5) */
    l_lo = Q6_L_tmp_mem_A(pw_src_lo);
    l_hi = Q6_L_tmp_mem_A(pw_src_hi);
    pa = Q6_LW_combine_LL(l_hi, l_lo);
    Q6_mem_Ku16_new_AL(buf_out, Q6_Lh_sub_ilog2_LWwI(pa, 5));

    /* bit_width(-100000 ^ -1) = bit_width(99999) = 17 */
    exp_val = 17 - 1 - 5;
    for (int i = 0; i < HLX_HALF_ELEMS; i++) {
        expected[i] = exp_val;
    }
    check_h("LX_sub_ilog2_w", buf_out, expected, HLX_HALF_ELEMS);
}

int main(void)
{
    init_vtcm();
    enable_hlx();

    buf_src = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    buf_out = vtcm_alloc(HLX_HALF_ELEMS * sizeof(uint16_t));
    pw_src_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_src_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_out_lo = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    pw_out_hi = vtcm_alloc(HLX_WORD_ELEMS * sizeof(uint32_t));
    init_hlx_vsp();

    /* Lookup table */
    test_lut4();
    test_lut4_sh();
    test_lut4_sh_neg();
    test_lut4_mask();

    /* ilog2 */
    test_sub_ilog2_uh();
    test_sub_ilog2_h();
    test_sub_ilog2_uw();
    test_sub_ilog2_w();

    puts(err ? "FAIL" : "PASS");
    return err;
}
