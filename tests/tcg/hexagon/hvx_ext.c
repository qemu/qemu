/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Tests for HVX qfloat extended-precision bits: vsetqfext/
 * vgetqfext, vmerge's ext-bit passthrough, and the "any non-qfloat write
 * resets a vector's ext bits to the V_EXTENDED_BYTEVAL sentinel" rule --
 * including through .cur/.tmp loads and predicated/packet-internal
 * forwarding, where it's easy to get the reset timing wrong.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/auxv.h>
#include <elf.h>
#include <hexagon_types.h>
#include <hvx_hexagon_protos.h>

static int err;
static bool extended_qfloat;

#include "hvx_misc.h"

static unsigned long hwcap(void)
{
    return getauxval(AT_HWCAP);
}

static void test_qf32_mpy(void)
{
    HVX_Vector input = Q6_V_vsplat_R(-1);
    HVX_Vector result = Q6_Vqf32_vmpy_Vqf32Vqf32(input, input);

    memcpy(output, &result, sizeof(result));

    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        expect[0].uw[i] = extended_qfloat ? 0x0000007f : 0x7fffffff;
    }

    check_output_w(__LINE__, 1);
}

static void test_ext_bits_reset_on_copy(void)
{
    /* Fixed register identities are part of what this test exercises. */
    asm volatile(
        "r0 = #0x11111111\n"
        "v10 = vsplat(r0)\n"
        "v0.x = vsetqfext(v10, r0)\n"
        "v1.x = vsetqfext(v10, r0)\n"
        "v2 = vgetqfext(v0.x, r0)\n"
        "v3 = vgetqfext(v1.x, r0)\n"
        "v1 = v0\n"
        "v4 = vgetqfext(v0.x, r0)\n"
        "v5 = vgetqfext(v1.x, r0)\n"
        "vmemu(%[out0]) = v2\n"
        "vmemu(%[out1]) = v3\n"
        "vmemu(%[out2]) = v4\n"
        "vmemu(%[out3]) = v5\n"
        :
        : [out0] "r"(&output[0]), [out1] "r"(&output[1]),
          [out2] "r"(&output[2]), [out3] "r"(&output[3])
        : "r0", "v0", "v1", "v2", "v3", "v4", "v5", "v10", "memory");

    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        expect[0].uw[i] = expect[1].uw[i] = expect[2].uw[i] = 0x11111111;
        expect[3].uw[i] = 0x11001100;
    }

    check_output_w(__LINE__, 4);
}

static void test_ext_bits_reset_multiple_insns(void)
{
    asm volatile(
        "r1 = #0xe60e31d4\n"
        "r2 = #0xbaf1fa15\n"
        "r3 = #0x4a777c7b\n"
        "v1 = vsplat(r1)\n"
        "v2 = vsplat(r2)\n"
        "v3 = vsplat(r3)\n"

        "{\n"
        "    v2 = v1\n"
        "    v4.sf = vmax(v3.sf,v2.sf)\n"
        "}\n"
        "v5.hf = v4.qf16\n"
        "vmemu(%[out]) = v5\n"
        :
        : [out] "r"(output)
        : "r1", "r2", "r3", "v1", "v2", "v3", "v4", "v5", "memory");

    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        expect[0].uw[i] = 0x5ca76fc7; /* reference from the sim */
    }

    check_output_w(__LINE__, 1);
}

static void test_ext_bits_reset_interleaved_qf(void)
{
    HVX_Vector v1 = Q6_V_vsplat_R(0x00000057);
    HVX_Vector v2 = v1;
    HVX_Vector v3 = Q6_V_vsplat_R(0x2a8b9bf4);
    HVX_Vector v4 = v3;
    HVX_Vector v5 = Q6_Vqf32_vadd_VsfVsf(v2, v4);
    HVX_Vector v6;

    v5 = Q6_Vhf_vmin_VhfVhf(v1, v3);
    v6 = Q6_Vqf16_vmpy_Vqf16Vhf(v5, v1);
    memcpy(output, &v6, sizeof(v6));

    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        expect[0].uw[i] = 0x0000f766; /* reference from the sim */
    }

    check_output_w(__LINE__, 1);
}

static void test_ext_bits_preserved_on_false_predicate(void)
{
    asm volatile(
        "r0 = #0x11111111\n"
        "v3 = vsplat(r0)\n"
        "v0.x = vsetqfext(v3, r0)\n"
        "r2 = #1\n"
        "v1 = vsplat(r2)\n"
        "r1 = #0\n"
        "p0 = cmp.eq(r1, r2)\n"
        "{\n"
        "    if (p0) v0 = v1\n"
        "}\n"
        "v2 = vgetqfext(v0.x, r0)\n"
        "vmemu(%[out]) = v2\n"
        :
        : [out] "r"(output)
        : "r0", "r1", "r2", "v0", "v1", "v2", "v3", "p0", "memory");

    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        expect[0].uw[i] = 0x11111111;
    }
    check_output_w(__LINE__, 1);
}

static void test_qfloat_semantics(void)
{
    HVX_Vector a = Q6_V_vsplat_R(0xfde1d91d);
    HVX_Vector b = Q6_V_vsplat_R(0xffffff80);
    HVX_Vector result = Q6_Vqf32_vsub_Vqf32Vqf32(a, b);

    memcpy(output, &result, sizeof(result));

    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        expect[0].uw[i] = extended_qfloat ? 0x7fffff68 : 0x3fffff69;
    }

    check_output_w(__LINE__, 1);
}

static void test_qfloat_with_cur(void)
{
    memset(buffer0, 0xff, sizeof(buffer0));
    asm volatile(
        "r0 = #0x11111111\n"
        "v10 = vsplat(r0)\n"

        "r1 = #0xffffff80\n"
        "v1 = vsplat(r1)\n"

        /* tweak ext bits */
        "v0.x = vsetqfext(v10, r0)\n"
        "v2 = vmerge(v0.x, v1.w)\n"

        "{\n"
        "    v2.cur = vmem(%[buf]++#0)\n"
        "    v3.qf32=vsub(v2.qf32, v1.qf32)\n"
        "}\n"
        "vmemu(%[out]) = v3\n"
        :
        : [buf] "r"(buffer0), [out] "r"(output)
        : "r0", "r1", "v0", "v1", "v2", "v3", "v10", "memory");

    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        expect[0].uw[i] = 0x800000e7; /* reference from the sim */
    }

    check_output_w(__LINE__, 1);
}

static void test_qfloat_with_tmp(void)
{
    memset(buffer0, 0xff, sizeof(buffer0));
    asm volatile(
        "r0 = #0x11111111\n"
        "v10 = vsplat(r0)\n"

        "r1 = #0xffffff80\n"
        "v1 = vsplat(r1)\n"

        /* tweak ext bits */
        "v0.x = vsetqfext(v10, r0)\n"
        "v2 = vmerge(v0.x, v1.w)\n"

        "{\n"
        "    v3.tmp = vmem(%[buf]++#0)\n"
        "    v3.qf32=vsub(v2.qf32, v1.qf32)\n"
        "    v4 = v3\n"
        "}\n"
        "vmemu(%[out0]) = v3\n"

        "v10 = vgetqfext(v3.x, r0)\n"
        "vmemu(%[out1]) = v10\n"
        :
        : [buf] "r"(buffer0), [out0] "r"(&output[0]), [out1] "r"(&output[1])
        : "r0", "r1", "v0", "v1", "v2", "v3", "v4", "v10", "memory");

    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
        expect[0].uw[i] = 0x00000080; /* reference from the sim */
        expect[1].uw[i] = 0x00111111; /* tmp should not reset v3's ext bits */
    }

    check_output_w(__LINE__, 2);
}

int main()
{
    extended_qfloat = HWCAP_HEXAGON_HAS_ISA(hwcap(),
                                            HWCAP_HEXAGON_ISA_V79);

    test_qf32_mpy();
    if (extended_qfloat) {
        test_ext_bits_reset_on_copy();
        test_ext_bits_preserved_on_false_predicate();
        test_ext_bits_reset_interleaved_qf();
        test_qfloat_with_cur();
        test_qfloat_with_tmp();
    }
    test_ext_bits_reset_multiple_insns();
    test_qfloat_semantics();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
