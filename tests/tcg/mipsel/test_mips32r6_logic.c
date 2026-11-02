/*
 * MIPS32R6 tests: logical instructions
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

static uint32_t ref_and(uint32_t a, uint32_t b) { return a & b; }
static uint32_t ref_nor(uint32_t a, uint32_t b) { return ~(a | b); }
static uint32_t ref_or(uint32_t a, uint32_t b) { return a | b; }
static uint32_t ref_xor(uint32_t a, uint32_t b) { return a ^ b; }
/* SELEQZ/SELNEZ select rs, or zero, depending on rt. */
static uint32_t ref_seleqz(uint32_t a, uint32_t b) { return b == 0 ? a : 0; }
static uint32_t ref_selnez(uint32_t a, uint32_t b) { return b != 0 ? a : 0; }

static int check_op(const char *name, binary_op_32 op, ref_op_32 ref,
                    const uint32_t *input, size_t count)
{
    int fail = 0;

    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < count; j++) {
            uint32_t a = input[i], b = input[j], res = 0;

            op(&a, &b, &res);
            if (res != ref(a, b)) {
                TAP_INFO("%s: %08x, %08x: got %08x expected %08x",
                         name, a, b, res, ref(a, b));
                fail = 1;
            }
        }
    }
    return fail;
}

static int run(const char *name, binary_op_32 op, ref_op_32 ref)
{
    int ret = 0;

    ret |= check_op(name, op, ref, b32_pattern, PATTERN_INPUTS_32_SHORT_COUNT);
    ret |= check_op(name, op, ref, b32_random, RANDOM_INPUTS_32_SHORT_COUNT);
    tap_report(!ret, "MIPS32R6 logic %s", name);
    return ret;
}

int main(void)
{
    TAP_START();
    run("AND", do_mips32r6_AND, ref_and);
    run("NOR", do_mips32r6_NOR, ref_nor);
    run("OR", do_mips32r6_OR, ref_or);
    run("XOR", do_mips32r6_XOR, ref_xor);
    run("SELEQZ", do_mips32r6_SELEQZ, ref_seleqz);
    run("SELNEZ", do_mips32r6_SELNEZ, ref_selnez);

    return tap_done();
}
