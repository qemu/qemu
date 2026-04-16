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

int err;
#include "hvx_misc.h"

#if __HEXAGON_ARCH__ > 75
#error "After v75, compiler will replace some FP HVX instructions."
#endif

/******************************************************************************
 * NAN handling
 *****************************************************************************/

#define isnan(X) \
     (sizeof(X) == bytes_hf ? ((raw_hf(X) & ~0x8000) > 0x7c00) : \
                              ((raw_sf(X) & ~(1 << 31)) > 0x7f800000UL))

#define CHECK_NAN(A, DEF_NAN) (isnan(A) ? DEF_NAN : (A))
#define NAN_SF float_sf(0x7FFFFFFF)
#define NAN_HF float_hf(0x7FFF)

/******************************************************************************
 * Binary operations
 *****************************************************************************/

#define DEF_TEST_OP_2(vop, op, type_res, type_arg) \
    static void test_##vop##_##type_res##_##type_arg(void) \
    { \
        memset(expect, 0xff, sizeof(expect)); \
        memset(output, 0xff, sizeof(output)); \
        for (int i = 0; i < BUFSIZE; i++) { \
            HVX_Vector *hvx_output = (HVX_Vector *)&output[i]; \
            HVX_Vector hvx_buffer0 = *(HVX_Vector *)&buffer0[i]; \
            HVX_Vector hvx_buffer1 = *(HVX_Vector *)&buffer1[i]; \
            *hvx_output = \
                Q6_V##type_res##_##vop##_V##type_arg##V##type_arg(hvx_buffer0, \
                                                                  hvx_buffer1); \
            for (int j = 0; j < MAX_VEC_SIZE_BYTES / bytes_##type_res; j++) { \
                expect[i].type_res[j] = \
                    raw_##type_res(op(float_##type_arg(buffer0[i].type_arg[j]), \
                                      float_##type_arg(buffer1[i].type_arg[j]))); \
            } \
        } \
        check_output_##type_res(__LINE__, BUFSIZE); \
    }

#define SUM(X, Y, DEF_NAN) CHECK_NAN((X) + (Y), DEF_NAN)
#define SUB(X, Y, DEF_NAN) CHECK_NAN((X) - (Y), DEF_NAN)
#define MULT(X, Y, DEF_NAN) CHECK_NAN((X) * (Y), DEF_NAN)

#define SUM_SF(X, Y) SUM(X, Y, NAN_SF)
#define SUM_HF(X, Y) SUM(X, Y, NAN_HF)
#define SUB_SF(X, Y) SUB(X, Y, NAN_SF)
#define SUB_HF(X, Y) SUB(X, Y, NAN_HF)
#define MULT_SF(X, Y) MULT(X, Y, NAN_SF)
#define MULT_HF(X, Y) MULT(X, Y, NAN_HF)

DEF_TEST_OP_2(vadd, SUM_SF, sf, sf);
DEF_TEST_OP_2(vadd, SUM_HF, hf, hf);
DEF_TEST_OP_2(vsub, SUB_SF, sf, sf);
DEF_TEST_OP_2(vsub, SUB_HF, hf, hf);
DEF_TEST_OP_2(vmpy, MULT_SF, sf, sf);
DEF_TEST_OP_2(vmpy, MULT_HF, hf, hf);

/******************************************************************************
 * Other tests
 *****************************************************************************/

static void test_vdmpy_sf_hf(bool acc)
{
    memset(expect, 0xff, sizeof(expect));

    for (int i = 0; i < BUFSIZE; i++) {
        HVX_Vector hvx_buffer0 = *(HVX_Vector *)&buffer0[i];
        HVX_Vector hvx_buffer1 = *(HVX_Vector *)&buffer1[i];
        HVX_Vector *hvx_output = (HVX_Vector *)&output[i];

        uint32_t PREFIL_VAL = 0x111222;
        *hvx_output = Q6_V_vsplat_R(PREFIL_VAL);

        if (!acc) {
            *hvx_output = Q6_Vsf_vdmpy_VhfVhf(hvx_buffer0, hvx_buffer1);
        } else {
            *hvx_output = Q6_Vsf_vdmpyacc_VsfVhfVhf(*hvx_output, hvx_buffer0,
                                                    hvx_buffer1);
        }

        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            float a1 = float_hf_to_sf(float_hf(buffer0[i].hf[2 * j + 1]));
            float a2 = float_hf_to_sf(float_hf(buffer0[i].hf[2 * j]));
            float a3 = float_hf_to_sf(float_hf(buffer1[i].hf[2 * j + 1]));
            float a4 = float_hf_to_sf(float_hf(buffer1[i].hf[2 * j]));
            /*
             * Note, IEEE FP specifies +0.0 + -0.0 == +0.0. So we use -0.0 in
             * the default case to preserve the zero sign.
             */
            float prev = acc ? float_sf(PREFIL_VAL) : -0.0;
            expect[i].sf[j] = raw_sf(CHECK_NAN((a1 * a3) + (a2 * a4) + prev, NAN_SF));
        }
    }
    check_output_sf(__LINE__, BUFSIZE);
}

static void test_new(void)
{
    asm volatile("r0 = #%2\n"
                 "v0 = vsplat(r0)\n"
                 "vmem(%1 + #0) = v0\n"
                 "r1 = #%3\n"
                 "v1 = vsplat(r1)\n"
                 "v2 = vsplat(r1)\n"
                 "{\n"
                 "  v0.sf = vadd(v1.sf, v2.sf)\n"
                 "  vmem(%0 + #0) = v0.new\n"
                 "}\n"
                 :
                 : "r"(output), "r"(expect), "i"(SF_two), "i"(SF_one)
                 : "r0", "r1", "v0", "v1", "v2", "memory");
    check_output_w(__LINE__, 1);
}

int main(void)
{
    init_buffers_fp();

    /* add/sub */
    test_vadd_sf_sf();
    test_vadd_hf_hf();
    test_vsub_sf_sf();
    test_vsub_hf_hf();

    /* multiply */
    test_vmpy_sf_sf();
    test_vmpy_hf_hf();

    /* dot product */
    test_vdmpy_sf_hf(false);
    test_vdmpy_sf_hf(true);

    test_new();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
