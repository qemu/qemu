/*
 * Memory tagging, unaligned access crossing pages.
 * https://gitlab.com/qemu-project/qemu/-/issues/403
 *
 * Copyright (c) 2021 Linaro Ltd
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "mte.h"

int main(int ac, char **av)
{
    void *p;

    enable_mte(PR_MTE_TCF_SYNC);
    p = alloc_mte_mem(2 * 0x1000);

    /* Tag the pointer. */
    p = (void *)((unsigned long)p | (1ul << 56));

    /* Store tag in sequential granules. */
    asm("stg %0, [%0]" : : "r"(p + 0x0ff0));
    asm("stg %0, [%0]" : : "r"(p + 0x1000));

    /*
     * Perform an unaligned store with tag 1 crossing the pages.
     * Failure dies with SIGSEGV.
     */
    asm("str %0, [%0]" : : "r"(p + 0x0ffc));
    return 0;
}
