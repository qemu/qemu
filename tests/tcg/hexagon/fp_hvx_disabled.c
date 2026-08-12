/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include <hexagon_types.h>
#include <hvx_hexagon_protos.h>

int err;
#include "hvx_misc.h"

static void test_disabled(void)
{
    memset(output, 0xAA, sizeof(output));
    memset(expect, 0, sizeof(expect));
    asm volatile("r0 = #0xff\n"
                 "v0 = vsplat(r0)\n"
                 "r1 = #0x1\n"
                 "v1 = vsplat(r1)\n"
                 "v2 = vsplat(r1)\n"
                 "v0.sf = vadd(v1.sf, v2.sf)\n"
                 "vmem(%0 + #0) = v0\n"
                 :
                 : "r"(output)
                 : "r0", "r1", "v0", "v1", "v2", "memory");
    check_output_w(__LINE__, 1);
}

static void test_disabled_with_new(void)
{
    memset(output, 0xAA, sizeof(output));
    memset(expect, 0, sizeof(expect));
    asm volatile("r0 = #0xff\n"
                 "v0 = vsplat(r0)\n"
                 "r1 = #0x1\n"
                 "v1 = vsplat(r1)\n"
                 "v2 = vsplat(r1)\n"
                 "{\n"
                 "    v0.sf = vadd(v1.sf, v2.sf)\n"
                 "    vmem(%0 + #0) = v0.new\n"
                 "}\n"
                 :
                 : "r"(output)
                 : "r0", "r1", "v0", "v1", "v2", "memory");
    check_output_w(__LINE__, 1);
}

int main(void)
{
    test_disabled();
    test_disabled_with_new();
    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
