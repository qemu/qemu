/*
 * Test that icinva ends the current translation block.
 *
 * icinva only invalidates the emulator's cached translation for a
 * code range; it doesn't retroactively fix up code that has already
 * been decoded as part of the still-executing translation block. If
 * icinva doesn't force a new TB to start right after it, a packet
 * patched via a store immediately before icinva (with no
 * change-of-flow in between) still runs the stale decode baked into
 * the current TB instead of the freshly-patched instruction.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>

int err;

#include "hex_test.h"

/* Encoding of "r0 = #99" */
#define ICINVA_NEW_INSN 0x7800cc60

static uint32_t __attribute__((noinline)) test_icinva_smc(void)
{
    uint32_t result;

    /*
     * r1 = address of the "patch_slot" packet below (1:)
     * Overwrite it with the "r0 = #99" encoding, invalidate the
     * icache for that address, then fall straight through into it
     * with no intervening jump/call.
     */
    asm volatile(
        "r1 = ##1f\n"
        "r2 = ##%[newinsn]\n"
        "memw(r1) = r2\n"
        "icinva(r1)\n"
        "1:\n"
        "   r0 = #11\n"
        "%[out] = r0\n"
        : [out] "=r"(result)
        : [newinsn] "i"(ICINVA_NEW_INSN)
        : "r0", "r1", "r2", "memory"
    );

    return result;
}

int main(void)
{
    int pagesize = 4096;
    uintptr_t page = (uintptr_t)test_icinva_smc & ~(pagesize - 1);
    uint32_t result;

    if (mprotect((void *)page, 2 * pagesize,
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        perror("mprotect");
        return 1;
    }

    result = test_icinva_smc();
    check32(result, 99);

    puts(err ? "FAIL" : "PASS");
    return err;
}
