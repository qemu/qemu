/*
 *  Copyright(c) 2023-2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

int err;

#include "../hvx_misc.h"

void set_hvx_context(int n)
{
    uint32_t ssr_context_bits = n << 27;
    asm volatile(
        "r1 = ssr\n"
        "r1 = and(r1, ##0xc7ffffff)\n"
        "r1 = or(r1, %0)\n"
        "ssr = r1\r"
        "isync\n"
        :
        : "r"(ssr_context_bits)
        : "r1"
    );
}

void setv0(int n)
{
    asm volatile(
        "v0 = vsplat(%0)\n"
        : : "r"(n) : "v0"
    );
}

void store_v0(MMVector *v)
{
    asm volatile(
        "vmemu(%0) = v0\n"
        :
        : "r"(v)
        : "memory"
    );
}

uint32_t get_num_contexts(void)
{
    const int EXT_CONTEXT_OFFSET = 13;
    unsigned int cfgbase;
    asm volatile("%0 = cfgbase\n" : "=r"(cfgbase));
    uint32_t *cfgtable = (uint32_t *)(cfgbase << 16);
    return *(cfgtable + EXT_CONTEXT_OFFSET);
}

uint32_t get_rev(void)
{
    uint32_t rev;
    asm volatile("%0 = rev\n" : "=r"(rev));
    return rev;
}

/*
 * This test verifies that each new context is properly selected and is
 * independent of the thread.
 */
int main()
{
    int num_contexts = get_num_contexts();
    printf("rev=v%x, HVX-contexts=%d\n", (int)(get_rev() & 0xff), num_contexts);
    memset(&output[0], 0, 8 * sizeof(MMVector));

    /* First set v0 on all the contexts. */
    for (int i = 0; i < num_contexts; i++) {
        set_hvx_context(i);
        setv0(i + 1);
    }

    /*
     * Now each context should have its own v0 value. Save it to memory. We
     * check all possible SSR.XA values to make sure the "aliases" are
     * implemented correctly.
     */
    for (int i = 0; i < 8; i++) {
        set_hvx_context(i);
        store_v0(&output[i]);
    }


    /*
     * Set expected values:
     *
     *                            num contexts
     * SSR.XA     2              4              6              8
     * 000      HVX Context 0  HVX Context 0  HVX Context 0  HVX Context 0
     * 001      HVX Context 1  HVX Context 1  HVX Context 1  HVX Context 1
     * 010      HVX Context 0  HVX Context 2  HVX Context 2  HVX Context 2
     * 011      HVX Context 1  HVX Context 3  HVX Context 3  HVX Context 3
     * 100      HVX Context 0  HVX Context 0  HVX Context 4  HVX Context 4
     * 101      HVX Context 1  HVX Context 1  HVX Context 5  HVX Context 5
     * 110      HVX Context 0  HVX Context 2  HVX Context 2  HVX Context 6
     * 111      HVX Context 1  HVX Context 3  HVX Context 3  HVX Context 7
     */
    for (int i = 0; i < 8; i++) {
        int expected = (i % num_contexts) + 1;
        /* Exception for num_contexts=6 */
        if (num_contexts == 6 && i >= 6) {
            expected = (i - 6 + 2) + 1;
        }
        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            expect[i].w[j] = expected;
        }
    }

    check_output_w(__LINE__, 8);
    puts(err ? "FAIL" : "PASS");
    return !!err;
}
