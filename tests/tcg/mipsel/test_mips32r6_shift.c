/*
 * MIPS32R6 tests: shift instructions
 *
 * Author:
 *   Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>

#include "../include/tap.h"
#include "../mips/include/wrappers_mips32r6.h"
#include "../mips/include/test_inputs_32.h"

typedef void (*binary_op_32)(const uint32_t *, const uint32_t *, uint32_t *);
typedef uint32_t (*ref_op_32)(uint32_t, uint32_t);

/* Only the low 5 bits of the shift amount (rs) are used. */
static uint32_t ref_sllv(uint32_t a, uint32_t b) { return a << (b & 31); }
static uint32_t ref_srlv(uint32_t a, uint32_t b) { return a >> (b & 31); }
static uint32_t ref_srav(uint32_t a, uint32_t b)
{
    return (uint32_t)((int32_t)a >> (b & 31));
}

/* LSA: shift rs left by 1..4, then add rt. */
static uint32_t ref_lsa1(uint32_t a, uint32_t b) { return (a << 1) + b; }
static uint32_t ref_lsa2(uint32_t a, uint32_t b) { return (a << 2) + b; }
static uint32_t ref_lsa3(uint32_t a, uint32_t b) { return (a << 3) + b; }
static uint32_t ref_lsa4(uint32_t a, uint32_t b) { return (a << 4) + b; }

static int check_one(const char *name, binary_op_32 op, ref_op_32 ref,
                     uint32_t a, uint32_t b)
{
    uint32_t res = 0;

    op(&a, &b, &res);
    if (res != ref(a, b)) {
        TAP_INFO("%s: %08x, %08x: got %08x expected %08x",
                 name, a, b, res, ref(a, b));
        return 1;
    }
    return 0;
}

static int run(const char *name, binary_op_32 op, ref_op_32 ref)
{
    int ret = 0;

    /* Value/amount pairs from the input tables. */
    for (size_t i = 0; i < PATTERN_INPUTS_32_SHORT_COUNT; i++) {
        for (size_t j = 0; j < PATTERN_INPUTS_32_SHORT_COUNT; j++) {
            ret |= check_one(name, op, ref, b32_pattern[i], b32_pattern[j]);
        }
    }
    for (size_t i = 0; i < RANDOM_INPUTS_32_SHORT_COUNT; i++) {
        for (size_t j = 0; j < RANDOM_INPUTS_32_SHORT_COUNT; j++) {
            ret |= check_one(name, op, ref, b32_random[i], b32_random[j]);
        }
    }
    /* Every shift amount (including ones with ignored high bits). */
    for (size_t i = 0; i < PATTERN_INPUTS_32_SHORT_COUNT; i++) {
        for (uint32_t sa = 0; sa < 64; sa++) {
            ret |= check_one(name, op, ref, b32_pattern[i], sa);
        }
    }
    tap_report(!ret, "MIPS32R6 shift %s", name);
    return ret;
}

int main(void)
{
    TAP_START();
    run("SLLV", do_mips32r6_SLLV, ref_sllv);
    run("SRLV", do_mips32r6_SRLV, ref_srlv);
    run("SRAV", do_mips32r6_SRAV, ref_srav);
    run("LSA1", do_mips32r6_LSA1, ref_lsa1);
    run("LSA2", do_mips32r6_LSA2, ref_lsa2);
    run("LSA3", do_mips32r6_LSA3, ref_lsa3);
    run("LSA4", do_mips32r6_LSA4, ref_lsa4);

    return tap_done();
}
