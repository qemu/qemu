/* SPDX-License-Identifier: GPL-2.0-or-later */
/* MIPS64R6 tests: logical */

#include "../include/tap.h"

#define main test_mips64r6_and
#include "test_mips64r6_and.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_nor
#include "test_mips64r6_nor.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_or
#include "test_mips64r6_or.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_xor
#include "test_mips64r6_xor.c"
#undef main
#undef TEST_COUNT_TOTAL

static const struct {
    const char *name;
    int32_t (*fn)(void);
} r6_tests[] = {
    { "and", test_mips64r6_and },
    { "nor", test_mips64r6_nor },
    { "or", test_mips64r6_or },
    { "xor", test_mips64r6_xor },
};

int main(void)
{
    unsigned count = sizeof(r6_tests) / sizeof(r6_tests[0]);
    unsigned i;

    TAP_START();
    for (i = 0; i < count; i++) {
        tap_report(!r6_tests[i].fn(), "MIPS64R6 logical %s",
                   r6_tests[i].name);
    }

    return tap_done();
}
