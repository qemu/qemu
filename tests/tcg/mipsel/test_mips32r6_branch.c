/*
 * MIPS32R6 tests: compact branches and jumps instructions
 *
 * Author:
 *   Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>

#include "../include/tap.h"

/*
 * BALC: branch and link, no delay slot. The callee records $ra and returns;
 * $ra must be the address of the instruction following BALC (label 2).
 */
static int test_balc(void)
{
    uint32_t seen, expected;

    __asm__ volatile (
        ".set push\n\t"
        ".set noreorder\n\t"
        "move %0, $zero\n\t"
        "balc 1f\n\t"
        "2: bc 3f\n\t"
        "1: move %0, $ra\n\t"
        "jrc $ra\n\t"
        "3: lapc %1, 2b\n\t"
        ".set pop\n\t"
        : "=&r" (seen), "=&r" (expected)
        :
        : "ra");

    if (seen != expected) {
        TAP_INFO("BALC: ra 0x%08x expected 0x%08x", seen, expected);
    }
    return seen != expected;
}

static int run(const char *name, int (*test)(void))
{
    int ret = test();

    tap_report(!ret, "MIPS32R6 branch %s", name);
    return ret;
}

int main(void)
{
    TAP_START();
    run("BALC", test_balc);

    return tap_done();
}
