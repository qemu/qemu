/*
 * MIPS32R6 tests: bit operations instructions
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

typedef void (*unary_op_32)(const uint32_t *, uint32_t *);
typedef uint32_t (*ref_op_32)(uint32_t);

static uint32_t ref_clz(uint32_t a) { return a ? __builtin_clz(a) : 32; }
static uint32_t ref_clo(uint32_t a) { return ref_clz(~a); }

/* BITSWAP reverses the bit order inside each byte. */
static uint32_t ref_bitswap(uint32_t a)
{
    uint32_t res = 0;

    for (int byte = 0; byte < 4; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            if (a & (1u << (byte * 8 + bit))) {
                res |= 1u << (byte * 8 + 7 - bit);
            }
        }
    }
    return res;
}

static int check_all(const char *name, unary_op_32 op, ref_op_32 ref,
                     const uint32_t *input, size_t count)
{
    int fail = 0;

    for (size_t i = 0; i < count; i++) {
        uint32_t a = input[i], res = 0;

        op(&a, &res);
        if (res != ref(a)) {
            TAP_INFO("%s: %08x: got %08x expected %08x",
                     name, a, res, ref(a));
            fail = 1;
        }
    }
    return fail;
}

static int run(const char *name, unary_op_32 op, ref_op_32 ref)
{
    int ret = 0;

    ret |= check_all(name, op, ref, b32_pattern, PATTERN_INPUTS_32_COUNT);
    ret |= check_all(name, op, ref, b32_random, RANDOM_INPUTS_32_COUNT);
    /* Every leading-run length, for both polarities. */
    for (int n = 0; n < 32; n++) {
        uint32_t v[2] = { 0xffffffffu >> n, ~(0xffffffffu >> n) };

        ret |= check_all(name, op, ref, v, 2);
        v[0] = 1u << n;
        v[1] = ~(1u << n);
        ret |= check_all(name, op, ref, v, 2);
    }
    tap_report(!ret, "MIPS32R6 bitops %s", name);
    return ret;
}

int main(void)
{
    TAP_START();
    run("CLO", do_mips32r6_CLO, ref_clo);
    run("CLZ", do_mips32r6_CLZ, ref_clz);
    run("BITSWAP", do_mips32r6_BITSWAP, ref_bitswap);

    return tap_done();
}
