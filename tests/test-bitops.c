/*
 * Test bitops routines
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"

typedef struct {
    uint32_t value;
    int start;
    int length;
    int32_t result;
} S32Test;

typedef struct {
    uint64_t value;
    int start;
    int length;
    int64_t result;
} S64Test;

static const S32Test test_s32_data[] = {
    { 0x38463983, 4, 4, -8 },
    { 0x38463983, 12, 8, 0x63 },
    { 0x38463983, 0, 32, 0x38463983 },
};

static const S64Test test_s64_data[] = {
    { 0x8459826734967223ULL, 60, 4, -8 },
    { 0x8459826734967223ULL, 0, 64, 0x8459826734967223LL },
};

static void test_sextract32(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(test_s32_data); i++) {
        const S32Test *test = &test_s32_data[i];
        int32_t r = sextract32(test->value, test->start, test->length);

        g_assert_cmpint(r, ==, test->result);
    }
}

static void test_sextract64(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(test_s32_data); i++) {
        const S32Test *test = &test_s32_data[i];
        int64_t r = sextract64(test->value, test->start, test->length);

        g_assert_cmpint(r, ==, test->result);
    }

    for (i = 0; i < ARRAY_SIZE(test_s64_data); i++) {
        const S64Test *test = &test_s64_data[i];
        int64_t r = sextract64(test->value, test->start, test->length);

        g_assert_cmpint(r, ==, test->result);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/bitops/sextract32", test_sextract32);
    g_test_add_func("/bitops/sextract64", test_sextract64);
    return g_test_run();
}
