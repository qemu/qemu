/* SPDX-License-Identifier: GPL-2.0-or-later */
/* MIPS64R6 tests: variable shift */

#include "../include/tap.h"

#define main test_mips64r6_sllv
#include "test_mips64r6_sllv.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_srav
#include "test_mips64r6_srav.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_srlv
#include "test_mips64r6_srlv.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_dsllv
#include "test_mips64r6_dsllv.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_dsrav
#include "test_mips64r6_dsrav.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_dsrlv
#include "test_mips64r6_dsrlv.c"
#undef main
#undef TEST_COUNT_TOTAL

static const struct {
    const char *name;
    int32_t (*fn)(void);
} r6_tests[] = {
    { "sllv", test_mips64r6_sllv },
    { "srav", test_mips64r6_srav },
    { "srlv", test_mips64r6_srlv },
    { "dsllv", test_mips64r6_dsllv },
    { "dsrav", test_mips64r6_dsrav },
    { "dsrlv", test_mips64r6_dsrlv },
};

int main(void)
{
    unsigned count = sizeof(r6_tests) / sizeof(r6_tests[0]);
    unsigned i;

    TAP_START();
    for (i = 0; i < count; i++) {
        tap_report(!r6_tests[i].fn(), "MIPS64R6 variable shift %s",
                   r6_tests[i].name);
    }

    return tap_done();
}
