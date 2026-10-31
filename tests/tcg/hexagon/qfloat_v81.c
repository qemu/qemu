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

/* v81 FP8/qf16 conversions use exponent bias 7, unlike v79 vcvt. */
#define F8_NAN 0x80
#define QF16_NAN 0x801f

static void f8_to_qf16_parts(uint8_t f8, int *sig11, int *exp)
{
    int e = (f8 >> 3) & 0xf;
    int sig = e ? 8 | (f8 & 7) : f8 & 7;
    int p = 31 - __builtin_clz(sig);

    *exp = p + (e ? e : 1) - 10;
    *sig11 = sig << (9 - p);
}

static uint16_t ref_f8_to_qf16(uint8_t f8)
{
    int sig11, exp;

    if (f8 == 0) {
        return 0;
    }
    if (f8 == F8_NAN) {
        return QF16_NAN;
    }
    f8_to_qf16_parts(f8, &sig11, &exp);
    /* qf16 negative significands are complemented. */
    if (f8 & 0x80) {
        sig11 = ~sig11 & 0x7ff;
    }
    return sig11 << 5 | (exp + 15);
}

#define WIDEN_VEC(I) ((I) % 2)
#define WIDEN_IDX(I) (2 * ((I) / 4) + (((I) % 4) / 2))

static void test_vconv_qf16_f8(void)
{
    for (int blk = 0; blk < 2; blk++) {
        MMVector input;

        for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) {
            input.ub[i] = blk * MAX_VEC_SIZE_BYTES + i;
            expect[WIDEN_VEC(i)].uh[WIDEN_IDX(i)] = ref_f8_to_qf16(input.ub[i]);
        }
        asm volatile(
            "v2 = vmemu(%[in])\n"
            "v1:0.qf16 = v2.f8\n"
            "vmemu(%[out0]) = v0\n"
            "vmemu(%[out1]) = v1\n"
            :
            : [in] "r"(&input), [out0] "r"(&output[0]),
              [out1] "r"(&output[1])
            : "v0", "v1", "v2", "memory");
        check_output_uh(__LINE__, 2);
    }
}

/* Round half precision to FP8, nearest-even without saturation. */
static uint8_t ref_hf_to_f8(uint16_t hf)
{
    _Float16 h;
    double x, ax;
    int sign = hf >> 15;
    int code;

    memcpy(&h, &hf, sizeof(h));
    x = h;
    if (x != x || __builtin_isinf(x)) {
        return F8_NAN;
    }
    ax = __builtin_fabs(x);
    if (ax < 1.0 / 64) {
        /* Subnormal units are 2^-9. */
        code = __builtin_rint(ax * 512);
    } else {
        int e = __builtin_ilogb(ax);
        int m = __builtin_rint(__builtin_scalbn(ax, 3 - e));

        code = (e + 7) * 8 + m - 8;
    }
    if (code > 0x7f) {
        return F8_NAN;
    }
    return code ? sign << 7 | code : 0;
}

/* Form qf16 values from each half-precision input. */
static void test_vconv_f8_qf16(void)
{
    for (int base = 0; base < 0x10000; base += MAX_VEC_SIZE_BYTES / 2) {
        MMVector input;

        for (int i = 0; i < MAX_VEC_SIZE_BYTES / 2; i++) {
            input.uh[i] = base + i;
            expect[0].uh[i] = ref_hf_to_f8(base + i);
        }
        asm volatile(
            "v2 = vmemu(%[in])\n"
            "v3 = vxor(v3, v3)\n"
            "v0.qf16 = vadd(v2.hf, v3.hf)\n"
            "v1.f8 = v0.qf16\n"
            "vmemu(%[out]) = v1\n"
            :
            : [in] "r"(&input), [out] "r"(&output[0])
            : "v0", "v1", "v2", "v3", "memory");
        check_output_uh(__LINE__, 1);
    }
}

int main()
{
    test_qf16();
    test_qf32();
    test_qf32_bf_conversion();
    test_vconv_qf16_f8();
    test_vconv_f8_qf16();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
