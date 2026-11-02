/* SPDX-License-Identifier: GPL-2.0-or-later */
/* MIPS64R6 tests: CRC32 */

#include "../include/tap.h"

#define main test_mips64r6_crc32b
#include "test_mips64r6_crc32b.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_crc32h
#include "test_mips64r6_crc32h.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_crc32w
#include "test_mips64r6_crc32w.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_crc32d
#include "test_mips64r6_crc32d.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_crc32cb
#include "test_mips64r6_crc32cb.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_crc32ch
#include "test_mips64r6_crc32ch.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_crc32cw
#include "test_mips64r6_crc32cw.c"
#undef main
#undef TEST_COUNT_TOTAL

#define main test_mips64r6_crc32cd
#include "test_mips64r6_crc32cd.c"
#undef main
#undef TEST_COUNT_TOTAL

static const struct {
    const char *name;
    int32_t (*fn)(void);
} r6_tests[] = {
    { "crc32b", test_mips64r6_crc32b },
    { "crc32h", test_mips64r6_crc32h },
    { "crc32w", test_mips64r6_crc32w },
    { "crc32d", test_mips64r6_crc32d },
    { "crc32cb", test_mips64r6_crc32cb },
    { "crc32ch", test_mips64r6_crc32ch },
    { "crc32cw", test_mips64r6_crc32cw },
    { "crc32cd", test_mips64r6_crc32cd },
};

int main(void)
{
    unsigned count = sizeof(r6_tests) / sizeof(r6_tests[0]);
    unsigned i;

    TAP_START();
    for (i = 0; i < count; i++) {
        tap_report(!r6_tests[i].fn(), "MIPS64R6 CRC32 %s",
                   r6_tests[i].name);
    }

    return tap_done();
}
