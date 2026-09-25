/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Test conditional calls, compound testbit0-and-jump instructions,
 * and endloop01 (dual hardware loop end).
 */

#include <stdio.h>
#include <stdint.h>

int err;

#include "hex_test.h"

/* Target function for conditional calls */
static int call_target_val;

__attribute__((noinline))
static void call_target(void)
{
    call_target_val = 42;
}

/*
 * Test conditional call: if (Pu) callr Rs / if (!Pu) callr Rs
 */
static void check_cond_call(void)
{
    void (*fn)(void) = call_target;

    /*
     * callr clobbers all caller-saved registers.
     * Hexagon caller-saved: r0-r5, r14-r15, r28, p0-p3.
     */

    /* if (p0) callr Rs -- predicate true */
    call_target_val = 0;
    asm volatile("p0 = cmp.eq(%[one], %[one])\n\t"  /* p0 = true */
                 "if (p0) callr %[fn]\n\t"
                 :
                 : [one] "r"(1), [fn] "r"(fn)
                 : "r0", "r1", "r2", "r3", "r4", "r5",
                   "r14", "r15", "r28", "r31",
                   "p0", "p1", "p2", "p3", "memory");
    check32(call_target_val, 42);

    /* if (!p0) callr Rs -- predicate true, so !p0 is false */
    call_target_val = 0;
    asm volatile("p0 = cmp.eq(%[one], %[one])\n\t"  /* p0 = true */
                 "if (!p0) callr %[fn]\n\t"
                 :
                 : [one] "r"(1), [fn] "r"(fn)
                 : "r0", "r1", "r2", "r3", "r4", "r5",
                   "r14", "r15", "r28", "r31",
                   "p0", "p1", "p2", "p3", "memory");
    check32(call_target_val, 0);

    /* if (!p0) callr Rs -- predicate false, so !p0 is true */
    call_target_val = 0;
    asm volatile("p0 = cmp.eq(%[a], %[b])\n\t"  /* p0 = false (1 != 2) */
                 "if (!p0) callr %[fn]\n\t"
                 :
                 : [a] "r"(1), [b] "r"(2), [fn] "r"(fn)
                 : "r0", "r1", "r2", "r3", "r4", "r5",
                   "r14", "r15", "r28", "r31",
                   "p0", "p1", "p2", "p3", "memory");
    check32(call_target_val, 42);
}

/*
 * Test compound testbit0-and-jump.
 * p0 = tstbit(Rs, #0); if ([!]p0.new) jump:t/nt target
 */
static void check_tstbit0_jump(void)
{
    uint32_t result;

    /* Rs has bit0 = 1, test "if (p0.new) jump" -> should jump */
    result = 0;
    asm volatile(
        "{\n\t"
        "    p0 = tstbit(%[val], #0)\n\t"
        "    if (p0.new) jump:t 1f\n\t"
        "}\n\t"
        "%[res] = #0\n\t"
        "jump 2f\n\t"
        "1:\n\t"
        "%[res] = #1\n\t"
        "2:\n\t"
        : [res] "=r"(result)
        : [val] "r"(0x5)  /* bit 0 is set */
        : "p0");
    check32(result, 1);

    /* Rs has bit0 = 0, test "if (p0.new) jump" -> should NOT jump */
    result = 0;
    asm volatile(
        "{\n\t"
        "    p0 = tstbit(%[val], #0)\n\t"
        "    if (p0.new) jump:t 1f\n\t"
        "}\n\t"
        "%[res] = #0\n\t"
        "jump 2f\n\t"
        "1:\n\t"
        "%[res] = #1\n\t"
        "2:\n\t"
        : [res] "=r"(result)
        : [val] "r"(0x4)  /* bit 0 is clear */
        : "p0");
    check32(result, 0);

    /* Rs has bit0 = 0, test "if (!p0.new) jump" -> should jump */
    result = 0;
    asm volatile(
        "{\n\t"
        "    p0 = tstbit(%[val], #0)\n\t"
        "    if (!p0.new) jump:t 1f\n\t"
        "}\n\t"
        "%[res] = #0\n\t"
        "jump 2f\n\t"
        "1:\n\t"
        "%[res] = #1\n\t"
        "2:\n\t"
        : [res] "=r"(result)
        : [val] "r"(0x4)  /* bit 0 is clear */
        : "p0");
    check32(result, 1);
}

/*
 * Test endloop01: a packet that terminates both loop0 and loop1.
 * Outer loop runs 3 iterations, inner loop runs 4.
 */
static void check_endloop01(void)
{
    uint32_t count;

    asm volatile(
        "%[cnt] = #0\n\t"
        "loop1(1f, #3)\n\t"    /* outer loop: 3 iterations */
        "1:\n\t"
        "loop0(2f, #4)\n\t"    /* inner loop: 4 iterations */
        "2:\n\t"
        "{ %[cnt] = add(%[cnt], #1) }:endloop01\n\t"
        : [cnt] "=r"(count));

    /* 3 outer * 4 inner = 12 total iterations */
    check32(count, 12);
}

int main(void)
{
    check_cond_call();
    check_tstbit0_jump();
    check_endloop01();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
