/*
 *  Copyright(c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdint.h>

int err;

#include "hex_test.h"

/*
 * allocframe pushes {LR, FP}, scrambling LR with FRAMEKEY.  Return through
 * dealloc_return and report the FP that it restores.
 */
static inline uint32_t frame_roundtrip(uint32_t key, uint32_t fp)
{
    uint32_t fp_back, saved_fp, saved_lr;

    asm volatile("%[saved_fp] = r30\n\t"
                 "%[saved_lr] = r31\n\t"
                 "framekey = %[key]\n\t"
                 "r30 = %[fp]\n\t"
                 "r31 = ##1f\n\t"
                 "allocframe(#0)\n\t"
                 "r31 = #0\n\t"
                 "dealloc_return\n\t"
                 "1:\n\t"
                 "%[fp_back] = r30\n\t"
                 "framekey = %[zero]\n\t"
                 "r30 = %[saved_fp]\n\t"
                 "r31 = %[saved_lr]\n"
                 : [fp_back] "=&r"(fp_back),
                   [saved_fp] "=&r"(saved_fp), [saved_lr] "=&r"(saved_lr)
                 : [key] "r"(key), [fp] "r"(fp), [zero] "r"(0)
                 : "memory", "framekey");
    return fp_back;
}

static void check_frame(uint32_t key, uint32_t fp)
{
    check32(frame_roundtrip(key, fp), fp);
}

int main()
{
    check_frame(0, 0x1234fff0);
    check_frame(0xf21f20a0, 0x1234fff0);

    puts(err ? "FAIL" : "PASS");
    return err;
}
