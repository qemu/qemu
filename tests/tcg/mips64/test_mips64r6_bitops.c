/* SPDX-License-Identifier: GPL-2.0-or-later */
/* MIPS64R6 tests: bit count and bit swap */

#include "../include/tap.h"

#define main test_mips64r6_bitswap
#include "test_mips64r6_bitswap.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_dbitswap
#include "test_mips64r6_dbitswap.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_clo
#include "test_mips64r6_clo.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_clz
#include "test_mips64r6_clz.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_dclo
#include "test_mips64r6_dclo.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_dclz
#include "test_mips64r6_dclz.c"
#undef main
#undef TEST_COUNT_TOTAL

static const struct {
    const char *name;
    int32_t (*fn)(void);
} r6_tests[] = {
    { "bitswap", test_mips64r6_bitswap },
    { "dbitswap", test_mips64r6_dbitswap },
    { "clo", test_mips64r6_clo },
    { "clz", test_mips64r6_clz },
    { "dclo", test_mips64r6_dclo },
    { "dclz", test_mips64r6_dclz },
};

int main(void)
{
    unsigned count = sizeof(r6_tests) / sizeof(r6_tests[0]);
    unsigned i;

    TAP_START();
    for (i = 0; i < count; i++) {
        tap_report(!r6_tests[i].fn(), "MIPS64R6 bit count and bit swap %s",
                   r6_tests[i].name);
    }

    return tap_done();
}
