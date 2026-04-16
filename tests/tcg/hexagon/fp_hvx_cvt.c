/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <hexagon_types.h>
#include <hvx_hexagon_protos.h>

#if __HEXAGON_ARCH__ > 75
#error "After v75, compiler will replace some FP HVX instructions."
#endif

int err;
#include "hvx_misc.h"
#include "hex_test.h"

#define NAN_BF 0x7FFF

#define TEST_EXP(TO, FROM, VAL, EXP) do { \
    ((MMVector *)&buffer)->FROM[index] = VAL; \
    expect[0].TO[index] = EXP; \
    index++; \
} while (0)

#define DEF_TEST_CVT(TO, FROM, TESTS) \
    static void test_vcvt_##TO##_##FROM(void) \
    { \
        HVX_Vector *hvx_output = (HVX_Vector *)&output[0]; \
        HVX_Vector buffer; \
        int index = 0; \
        memset(&buffer, 0, sizeof(buffer)); \
        memset(expect, 0, sizeof(expect)); \
        TESTS \
        * hvx_output = Q6_V##TO##_vcvt_V##FROM(buffer); \
        check_output_##TO(__LINE__, 1); \
    }

DEF_TEST_CVT(uh, hf, { \
    TEST_EXP(uh, hf, HF_QNaN, UINT16_MAX); \
    TEST_EXP(uh, hf, HF_SNaN, UINT16_MAX); \
    TEST_EXP(uh, hf, HF_QNaN_neg, UINT16_MAX); \
    TEST_EXP(uh, hf, HF_INF, UINT16_MAX); \
    TEST_EXP(uh, hf, HF_INF_neg, 0); \
    TEST_EXP(uh, hf, HF_neg_two, 0); \
    TEST_EXP(uh, hf, HF_zero_neg, 0); \
    TEST_EXP(uh, hf, raw_hf((_Float16)2.1), 2); \
    TEST_EXP(uh, hf, HF_one_recip, 1); \
})

DEF_TEST_CVT(h, hf, { \
    TEST_EXP(h, hf, HF_QNaN, INT16_MAX); \
    TEST_EXP(h, hf, HF_SNaN, INT16_MAX); \
    TEST_EXP(h, hf, HF_QNaN_neg, INT16_MAX); \
    TEST_EXP(h, hf, HF_INF, INT16_MAX); \
    TEST_EXP(h, hf, HF_INF_neg, INT16_MIN); \
    TEST_EXP(h, hf, HF_neg_two, -2); \
    TEST_EXP(h, hf, HF_zero_neg, 0); \
    TEST_EXP(h, hf, raw_hf((_Float16)2.1), 2); \
    TEST_EXP(h, hf, HF_one_recip, 1); \
})

/*
 * Some cvt operations take two vectors as input and perform the following:
 *    VdV.TO[4*i]   = OP(VuV.FROM[2*i]);
 *    VdV.TO[4*i+1] = OP(VuV.FROM[2*i+1]);
 *    VdV.TO[4*i+2] = OP(VvV.FROM[2*i]);
 *    VdV.TO[4*i+3] = OP(VvV.FROM[2*i+1]))
 * We use bf_index and index in a way that the tests are always done either
 * using the first or third line of the above snippet.
 */
#define TEST_EXP_2(TO, FROM, VAL, EXP) do { \
    ((MMVector *)&buffers[bf_index])->FROM[2 * index] = VAL; \
    expect[0].TO[(4 * index) + (2 * bf_index)] = EXP; \
    index++; \
    bf_index = (bf_index + 1) % 2; \
} while (0)

#define DEF_TEST_CVT_2(TO, FROM, TESTS) \
    static void test_vcvt_##TO##_##FROM(void) \
    { \
        HVX_Vector *hvx_output = (HVX_Vector *)&output[0]; \
        HVX_Vector buffers[2]; \
        int index = 0, bf_index = 0; \
        memset(&buffers, 0, sizeof(buffers)); \
        memset(expect, 0, sizeof(expect)); \
        TESTS \
        * hvx_output = Q6_V##TO##_vcvt_V##FROM##V##FROM(buffers[0], buffers[1]); \
        check_output_##TO(__LINE__, 1); \
    }

DEF_TEST_CVT_2(ub, hf, { \
    TEST_EXP_2(ub, hf, HF_QNaN, UINT8_MAX); \
    TEST_EXP_2(ub, hf, HF_SNaN, UINT8_MAX); \
    TEST_EXP_2(ub, hf, HF_QNaN_neg, UINT8_MAX); \
    TEST_EXP_2(ub, hf, HF_INF, UINT8_MAX); \
    TEST_EXP_2(ub, hf, HF_INF_neg, 0); \
    TEST_EXP_2(ub, hf, HF_small_neg, 0); \
    TEST_EXP_2(ub, hf, HF_neg_two, 0); \
    TEST_EXP_2(ub, hf, HF_zero_neg, 0); \
    TEST_EXP_2(ub, hf, raw_hf((_Float16)2.1), 2); \
    TEST_EXP_2(ub, hf, HF_one_recip, 1); \
})

DEF_TEST_CVT_2(b, hf, { \
    TEST_EXP_2(b, hf, HF_QNaN, INT8_MAX); \
    TEST_EXP_2(b, hf, HF_SNaN, INT8_MAX); \
    TEST_EXP_2(b, hf, HF_QNaN_neg, INT8_MAX); \
    TEST_EXP_2(b, hf, HF_INF, INT8_MAX); \
    TEST_EXP_2(b, hf, HF_INF_neg, INT8_MIN); \
    TEST_EXP_2(b, hf, HF_small_neg, 0); \
    TEST_EXP_2(b, hf, HF_neg_two, -2); \
    TEST_EXP_2(b, hf, HF_zero_neg, 0); \
    TEST_EXP_2(b, hf, raw_hf((_Float16)2.1), 2); \
    TEST_EXP_2(b, hf, HF_one_recip, 1); \
})

#define DEF_TEST_VCONV(TO, FROM, TESTS) \
    static void test_vconv_##TO##_##FROM(void) \
    { \
        HVX_Vector *hvx_output = (HVX_Vector *)&output[0]; \
        HVX_Vector buffer; \
        int index = 0; \
        memset(&buffer, 0, sizeof(buffer)); \
        memset(expect, 0, sizeof(expect)); \
        TESTS \
        * hvx_output = Q6_V##TO##_equals_V##FROM(buffer); \
        check_output_##TO(__LINE__, 1); \
    }

DEF_TEST_VCONV(w, sf, { \
    TEST_EXP(w, sf, SF_QNaN, INT32_MAX); \
    TEST_EXP(w, sf, SF_SNaN, INT32_MAX); \
    TEST_EXP(w, sf, SF_QNaN_neg, INT32_MIN); \
    TEST_EXP(w, sf, SF_INF, INT32_MAX); \
    TEST_EXP(w, sf, SF_INF_neg, INT32_MIN); \
    TEST_EXP(w, sf, SF_small_neg, 0); \
    TEST_EXP(w, sf, SF_neg_two, -2); \
    TEST_EXP(w, sf, SF_zero_neg, 0); \
    TEST_EXP(w, sf, raw_sf(2.1f), 2); \
    TEST_EXP(w, sf, raw_sf(2.8f), 2); \
})

DEF_TEST_VCONV(h, hf, { \
    TEST_EXP(h, hf, HF_QNaN, INT16_MAX); \
    TEST_EXP(h, hf, HF_SNaN, INT16_MAX); \
    TEST_EXP(h, hf, HF_QNaN_neg, INT16_MIN); \
    TEST_EXP(h, hf, HF_INF, INT16_MAX); \
    TEST_EXP(h, hf, HF_INF_neg, INT16_MIN); \
    TEST_EXP(h, hf, HF_small_neg, 0); \
    TEST_EXP(h, hf, HF_neg_two, -2); \
    TEST_EXP(h, hf, HF_zero_neg, 0); \
    TEST_EXP(h, hf, raw_hf((_Float16)2.1), 2); \
    TEST_EXP(h, hf, raw_hf((_Float16)2.8), 2); \
})

DEF_TEST_VCONV(hf, h, { \
    TEST_EXP(hf, h, 0, HF_zero); \
    TEST_EXP(hf, h, 2, HF_two); \
    TEST_EXP(hf, h, -2, HF_neg_two); \
    TEST_EXP(hf, h, 2049, raw_hf((_Float16)2048)); /* rounds DOWN */ \
    TEST_EXP(hf, h, 2051, raw_hf((_Float16)2052)); /* rounds UP */ \
})

DEF_TEST_VCONV(sf, w, { \
    TEST_EXP(sf, w, 0, SF_zero); \
    TEST_EXP(sf, w, 2, SF_two); \
    TEST_EXP(sf, w, -2, SF_neg_two); \
    TEST_EXP(sf, w, 16777217, raw_sf((float)16777216)); /* rounds DOWN */ \
    TEST_EXP(sf, w, 16777219, raw_sf((float)16777220)); /* rounds UP */ \
})

#define TEST_EXP_BF(VAL, EXP) do { \
    ((MMVector *)&buffers[1])->sf[index] = VAL; \
    ((MMVector *)&buffers[0])->sf[index] = VAL; \
    expect[0].bf[2 * index] = EXP; \
    expect[0].bf[2 * index + 1] = EXP; \
    index++; \
} while (0)

static void test_vconv_bf_sf(void)
{
    HVX_Vector *hvx_output = (HVX_Vector *)&output[0];
    HVX_Vector buffers[2];
    int index = 0;
    memset(&buffers, 0, sizeof(buffers));
    memset(expect, 0, sizeof(expect));

    TEST_EXP_BF(SF_QNaN, NAN_BF);
    TEST_EXP_BF(SF_SNaN, NAN_BF);
    TEST_EXP_BF(SF_QNaN_neg, NAN_BF);
    TEST_EXP_BF(SF_INF, BF_INF);
    TEST_EXP_BF(SF_INF_neg, BF_INF_neg);
    TEST_EXP_BF(SF_one, BF_one);
    TEST_EXP_BF(SF_zero_neg, BF_zero_neg);

    *hvx_output = Q6_Vbf_vcvt_VsfVsf(buffers[0], buffers[1]);
    check_output_hf(__LINE__, 1);
}

int main(void)
{
    test_vcvt_uh_hf();
    test_vcvt_h_hf();
    test_vcvt_ub_hf();
    test_vcvt_b_hf();
    test_vconv_w_sf();
    test_vconv_sf_w();
    test_vconv_h_hf();
    test_vconv_hf_h();
    test_vconv_bf_sf();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
