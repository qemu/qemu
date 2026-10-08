/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include <hexagon_types.h>
#include <hvx_hexagon_protos.h>

static int err;

#include "hvx_misc.h"

static void expect_h(int output_index, uint16_t value)
{
    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 2; i++) {
        expect[output_index].uh[i] = value;
    }
}

static void expect_w(int output_index, uint32_t value)
{
    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        expect[output_index].uw[i] = value;
    }
}

static void test_qf16(void)
{
    asm volatile(
        "r0 = #0x3c003c00\n"
        "v1 = vsplat(r0)\n"
        "r0 = #0x40004000\n"
        "v2 = vsplat(r0)\n"
        "v0.qf16 = vadd(v1.hf, v2.hf)\n"
        "v3.hf = v0.qf16\n"
        "r0 = #0x10001000\n"
        "v2 = vsplat(r0)\n"
        "v0.qf16 = vadd(v1.hf, v2.hf)\n"
        "v5.hf = v0.qf16\n"
        "r0 = #0xc000c000\n"
        "v1 = vsplat(r0)\n"
        "v0.qf16 = vabs(v1.hf)\n"
        "v6.hf = v0.qf16\n"
        "r0 = #0x40004000\n"
        "v1 = vsplat(r0)\n"
        "v0.qf16 = vneg(v1.hf)\n"
        "v7.hf = v0.qf16\n"
        "vmemu(%[out0]) = v3\n"
        "vmemu(%[out1]) = v5\n"
        "vmemu(%[out2]) = v6\n"
        "vmemu(%[out3]) = v7\n"
        :
        : [out0] "r"(&output[0]), [out1] "r"(&output[1]),
          [out2] "r"(&output[2]), [out3] "r"(&output[3])
        : "r0", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
          "memory");

    expect_h(0, 0x4200);
    expect_h(1, 0x3c00);
    expect_h(2, 0x4000);
    expect_h(3, 0xc002);
    check_output_h(__LINE__, 4);
}

static void test_qf32(void)
{
    asm volatile(
        "r0 = #0x3f800000\n"
        "v1 = vsplat(r0)\n"
        "r0 = #0x40000000\n"
        "v2 = vsplat(r0)\n"
        "v0.qf32 = vadd(v1.sf, v2.sf)\n"
        "v3.sf = v0.qf32\n"
        "v4.w = vilog2(v0.qf32)\n"
        "r0 = #0x7f800000\n"
        "v1 = vsplat(r0)\n"
        "r0 = #0xff800000\n"
        "v2 = vsplat(r0)\n"
        "v0.qf32 = vadd(v1.sf, v2.sf)\n"
        "v5.sf = v0.qf32\n"
        "vmemu(%[out0]) = v3\n"
        "vmemu(%[out1]) = v4\n"
        "vmemu(%[out2]) = v5\n"
        :
        : [out0] "r"(&output[0]), [out1] "r"(&output[1]),
          [out2] "r"(&output[2])
        : "r0", "v0", "v1", "v2", "v3", "v4", "v5", "memory");

    expect_w(0, 0x40400000);
    expect_w(1, 1);
    expect_w(2, 0x7fffffff);
    check_output_w(__LINE__, 3);
}

static void test_qf32_bf_conversion(void)
{
    HVX_Vector one = Q6_V_vsplat_R(0x3c003c00);
    HVX_VectorPair qf = Q6_Wqf32_vmpy_VhfVhf(one, one);
    HVX_Vector result = Q6_Vbf_equals_Wqf32(qf);

    memcpy(output, &result, sizeof(result));
    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 2; i++) {
        expect[0].uh[i] = 0x3f80;
    }
    check_output_h(__LINE__, 1);
}

int main()
{
    test_qf16();
    test_qf32();
    test_qf32_bf_conversion();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
