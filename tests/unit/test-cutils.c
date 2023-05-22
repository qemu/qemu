/*
 * cutils.c unit-tests
 *
 * Copyright Red Hat
 *
 * Authors:
 *  Eduardo Habkost <ehabkost@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/units.h"

static void test_parse_uint_null(void)
{
    uint64_t i = 999;
    const char *endptr = "somewhere";
    int r;

    r = parse_uint(NULL, &endptr, 0, &i);

    g_assert_cmpint(r, ==, -EINVAL);
    g_assert_cmpuint(i, ==, 0);
    g_assert_null(endptr);
}

static void test_parse_uint_empty(void)
{
    uint64_t i = 999;
    const char *endptr = "somewhere";
    const char *str = "";
    int r;

    r = parse_uint(str, &endptr, 0, &i);

    g_assert_cmpint(r, ==, -EINVAL);
    g_assert_cmpuint(i, ==, 0);
    g_assert_true(endptr == str);
}

static void test_parse_uint_whitespace(void)
{
    uint64_t i = 999;
    const char *endptr = "somewhere";
    const char *str = "   \t   ";
    int r;

    r = parse_uint(str, &endptr, 0, &i);

    g_assert_cmpint(r, ==, -EINVAL);
    g_assert_cmpuint(i, ==, 0);
    g_assert_true(endptr == str);
}


static void test_parse_uint_invalid(void)
{
    uint64_t i = 999;
    const char *endptr = "somewhere";
    const char *str = " \t xxx";
    int r;

    r = parse_uint(str, &endptr, 0, &i);

    g_assert_cmpint(r, ==, -EINVAL);
    g_assert_cmpuint(i, ==, 0);
    g_assert_true(endptr == str);
}


static void test_parse_uint_trailing(void)
{
    uint64_t i = 999;
    const char *endptr = "somewhere";
    const char *str = "123xxx";
    int r;

    r = parse_uint(str, &endptr, 0, &i);

    g_assert_cmpint(r, ==, 0);
    g_assert_cmpuint(i, ==, 123);
    g_assert_true(endptr == str + 3);
}

static void test_parse_uint_correct(void)
{
    uint64_t i = 999;
    const char *endptr = "somewhere";
    const char *str = "123";
    int r;

    r = parse_uint(str, &endptr, 0, &i);

    g_assert_cmpint(r, ==, 0);
    g_assert_cmpuint(i, ==, 123);
    g_assert_true(endptr == str + strlen(str));
}

static void test_parse_uint_octal(void)
{
    uint64_t i = 999;
    const char *endptr = "somewhere";
    const char *str = "0123";
    int r;

    r = parse_uint(str, &endptr, 0, &i);

    g_assert_cmpint(r, ==, 0);
    g_assert_cmpuint(i, ==, 0123);
    g_assert_true(endptr == str + strlen(str));
}

static void test_parse_uint_decimal(void)
{
    uint64_t i = 999;
    const char *endptr = "somewhere";
    const char *str = "0123";
    int r;

    r = parse_uint(str, &endptr, 10, &i);

    g_assert_cmpint(r, ==, 0);
    g_assert_cmpuint(i, ==, 123);
    g_assert_true(endptr == str + strlen(str));
}

static void test_parse_uint_llong_max(void)
{
    uint64_t i = 999;
    const char *endptr = "somewhere";
    char *str = g_strdup_printf("%llu", (unsigned long long)LLONG_MAX + 1);
    int r;

    r = parse_uint(str, &endptr, 0, &i);

    g_assert_cmpint(r, ==, 0);
    g_assert_cmpuint(i, ==, (unsigned long long)LLONG_MAX + 1);
    g_assert_true(endptr == str + strlen(str));

    g_free(str);
}

static void test_parse_uint_max(void)
{
    uint64_t i = 999;
    const char *endptr = "somewhere";
    char *str = g_strdup_printf("%llu", ULLONG_MAX);
    int r;

    r = parse_uint(str, &endptr, 0, &i);

    g_assert_cmpint(r, ==, 0);
    g_assert_cmpuint(i, ==, ULLONG_MAX);
    g_assert_true(endptr == str + strlen(str));

    g_free(str);
}

static void test_parse_uint_overflow(void)
{
    uint64_t i;
    const char *endptr;
    const char *str;
    int r;

    i = 999;
    endptr = "somewhere";
    str = "99999999999999999999999999999999999999";
    r = parse_uint(str, &endptr, 0, &i);
    g_assert_cmpint(r, ==, -ERANGE);
    g_assert_cmpuint(i, ==, ULLONG_MAX);
    g_assert_true(endptr == str + strlen(str));

    i = 999;
    endptr = "somewhere";
    str = "0x10000000000000000"; /* 65 bits, 64-bit sign bit clear */
    r = parse_uint(str, &endptr, 0, &i);
    g_assert_cmpint(r, ==, -ERANGE);
    g_assert_cmpuint(i, ==, ULLONG_MAX);
    g_assert_true(endptr == str + strlen(str));

    i = 999;
    endptr = "somewhere";
    str = "0x18000000080000000"; /* 65 bits, 64-bit sign bit set */
    r = parse_uint(str, &endptr, 0, &i);
    g_assert_cmpint(r, ==, -ERANGE);
    g_assert_cmpuint(i, ==, ULLONG_MAX);
    g_assert_true(endptr == str + strlen(str));
}

static void test_parse_uint_negative(void)
{
    uint64_t i;
    const char *endptr;
    const char *str;
    int r;

    i = 999;
    endptr = "somewhere";
    str = " \t -321";
    r = parse_uint(str, &endptr, 0, &i);
    g_assert_cmpint(r, ==, -ERANGE);
    g_assert_cmpuint(i, ==, 0);
    g_assert_true(endptr == str + strlen(str));

    i = 999;
    endptr = "somewhere";
    str = "-0xffffffff00000001";
    r = parse_uint(str, &endptr, 0, &i);
    g_assert_cmpint(r, ==, -ERANGE);
    g_assert_cmpuint(i, ==, 0);
    g_assert_true(endptr == str + strlen(str));
}

static void test_parse_uint_negzero(void)
{
    uint64_t i = 999;
    const char *endptr = "somewhere";
    const char *str = " -0";
    int r;

    r = parse_uint(str, &endptr, 0, &i);

    g_assert_cmpint(r, ==, -ERANGE);
    g_assert_cmpuint(i, ==, 0);
    g_assert_true(endptr == str + strlen(str));
}

static void test_parse_uint_full_trailing(void)
{
    uint64_t i = 999;
    const char *str = "123xxx";
    int r;

    r = parse_uint_full(str, 0, &i);

    g_assert_cmpint(r, ==, -EINVAL);
    g_assert_cmpuint(i, ==, 0);
}

static void test_parse_uint_full_correct(void)
{
    uint64_t i = 999;
    const char *str = "123";
    int r;

    r = parse_uint_full(str, 0, &i);

    g_assert_cmpint(r, ==, 0);
    g_assert_cmpuint(i, ==, 123);
}

static void test_parse_uint_full_erange_junk(void)
{
    /* FIXME - inconsistent with qemu_strto* which favors EINVAL */
    uint64_t i = 999;
    const char *str = "-2junk";
    int r;

    r = parse_uint_full(str, 0, &i);

    g_assert_cmpint(r, ==, -ERANGE /* FIXME -EINVAL */);
    g_assert_cmpuint(i, ==, 0);
}

static void test_qemu_strtoi_correct(void)
{
    const char *str = "12345 foo";
    char f = 'X';
    const char *endptr = &f;
    int res = 999;
    int err;

    err = qemu_strtoi(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 12345);
    g_assert_true(endptr == str + 5);
}

static void test_qemu_strtoi_null(void)
{
    char f = 'X';
    const char *endptr = &f;
    int res = 999;
    int err;

    err = qemu_strtoi(NULL, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 999);
    g_assert_null(endptr);
}

static void test_qemu_strtoi_empty(void)
{
    const char *str = "";
    char f = 'X';
    const char *endptr = &f;
    int res = 999;
    int err;

    err = qemu_strtoi(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtoi_whitespace(void)
{
    const char *str = "  \t  ";
    char f = 'X';
    const char *endptr = &f;
    int res = 999;
    int err;

    err = qemu_strtoi(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtoi_invalid(void)
{
    const char *str = "   xxxx  \t abc";
    char f = 'X';
    const char *endptr = &f;
    int res = 999;
    int err;

    err = qemu_strtoi(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtoi_trailing(void)
{
    const char *str = "123xxx";
    char f = 'X';
    const char *endptr = &f;
    int res = 999;
    int err;

    err = qemu_strtoi(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
    g_assert_true(endptr == str + 3);
}

static void test_qemu_strtoi_octal(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    int res = 999;
    int err;

    err = qemu_strtoi(str, &endptr, 8, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0123);
    g_assert_true(endptr == str + strlen(str));

    res = 999;
    endptr = &f;
    err = qemu_strtoi(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0123);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoi_decimal(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    int res = 999;
    int err;

    err = qemu_strtoi(str, &endptr, 10, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
    g_assert_true(endptr == str + strlen(str));

    str = "123";
    res = 999;
    endptr = &f;
    err = qemu_strtoi(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoi_hex(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    int res = 999;
    int err;

    err = qemu_strtoi(str, &endptr, 16, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0x123);
    g_assert_true(endptr == str + strlen(str));

    str = "0x123";
    res = 999;
    endptr = &f;
    err = qemu_strtoi(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0x123);
    g_assert_true(endptr == str + strlen(str));

    str = "0x";
    res = 999;
    endptr = &f;
    err = qemu_strtoi(str, &endptr, 16, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0);
    g_assert_true(endptr == str + 1);
}

static void test_qemu_strtoi_max(void)
{
    char *str = g_strdup_printf("%d", INT_MAX);
    char f = 'X';
    const char *endptr = &f;
    int res = 999;
    int err;

    err = qemu_strtoi(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, INT_MAX);
    g_assert_true(endptr == str + strlen(str));
    g_free(str);
}

static void test_qemu_strtoi_overflow(void)
{
    const char *str;
    const char *endptr;
    int res;
    int err;

    str = "2147483648"; /* INT_MAX + 1ll */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, INT_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "0x7fffffffffffffff"; /* LLONG_MAX */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, INT_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "0x8000000000000000"; /* (uint64_t)LLONG_MIN */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, INT_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "0x10000000000000000"; /* 65 bits, 32-bit sign bit clear */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, INT_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "0x18000000080000000"; /* 65 bits, 32-bit sign bit set */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, INT_MAX);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoi_min(void)
{
    char *str = g_strdup_printf("%d", INT_MIN);
    char f = 'X';
    const char *endptr = &f;
    int res = 999;
    int err;

    err = qemu_strtoi(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, INT_MIN);
    g_assert_true(endptr == str + strlen(str));
    g_free(str);
}

static void test_qemu_strtoi_underflow(void)
{
    const char *str;
    const char *endptr;
    int res;
    int err;

    str = "-2147483649"; /* INT_MIN - 1ll */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, INT_MIN);
    g_assert_true(endptr == str + strlen(str));

    str = "-0x7fffffffffffffff"; /* -LLONG_MAX */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, INT_MIN);
    g_assert_true(endptr == str + strlen(str));

    str = "-0x8000000000000000"; /* (uint64_t)LLONG_MIN */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, INT_MIN);
    g_assert_true(endptr == str + strlen(str));

    str = "-18446744073709551615"; /* -UINT64_MAX (not 1) */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, INT_MIN);
    g_assert_true(endptr == str + strlen(str));

    str = "-0x10000000000000000"; /* 65 bits, 32-bit sign bit clear */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, INT_MIN);
    g_assert_true(endptr == str + strlen(str));

    str = "-0x18000000080000000"; /* 65 bits, 32-bit sign bit set */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, INT_MIN);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoi_negative(void)
{
    const char *str;
    const char *endptr;
    int res;
    int err;

    str = "  \t -321";
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, -321);
    g_assert_true(endptr == str + strlen(str));

    str = "-2147483648"; /* INT_MIN */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, INT_MIN);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoi_negzero(void)
{
    const char *str = " -0";
    char f = 'X';
    const char *endptr = &f;
    int res = 999;
    int err;

    err = qemu_strtoi(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoi_full_correct(void)
{
    const char *str = "123";
    int res = 999;
    int err;

    err = qemu_strtoi(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
}

static void test_qemu_strtoi_full_null(void)
{
    char f = 'X';
    const char *endptr = &f;
    int res = 999;
    int err;

    err = qemu_strtoi(NULL, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 999);
    g_assert_null(endptr);
}

static void test_qemu_strtoi_full_empty(void)
{
    const char *str = "";
    int res = 999;
    int err;

    err = qemu_strtoi(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 0);
}

static void test_qemu_strtoi_full_negative(void)
{
    const char *str = " \t -321";
    int res = 999;
    int err;

    err = qemu_strtoi(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, -321);
}

static void test_qemu_strtoi_full_negzero(void)
{
    const char *str = " -0";
    int res = 999;
    int err;

    err = qemu_strtoi(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0);
}

static void test_qemu_strtoi_full_trailing(void)
{
    const char *str = "123xxx";
    int res = 999;
    int err;

    err = qemu_strtoi(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 123);
}

static void test_qemu_strtoi_full_max(void)
{
    char *str = g_strdup_printf("%d", INT_MAX);
    int res = 999;
    int err;

    err = qemu_strtoi(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, INT_MAX);
    g_free(str);
}

static void test_qemu_strtoi_full_erange_junk(void)
{
    /* EINVAL has priority over ERANGE */
    const char *str = "-9999999999junk";
    int res = 999;
    int err;

    err = qemu_strtoi(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, INT_MIN);
}

static void test_qemu_strtoui_correct(void)
{
    const char *str = "12345 foo";
    char f = 'X';
    const char *endptr = &f;
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 12345);
    g_assert_true(endptr == str + 5);
}

static void test_qemu_strtoui_null(void)
{
    char f = 'X';
    const char *endptr = &f;
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(NULL, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 999);
    g_assert_null(endptr);
}

static void test_qemu_strtoui_empty(void)
{
    const char *str = "";
    char f = 'X';
    const char *endptr = &f;
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtoui_whitespace(void)
{
    const char *str = "  \t  ";
    char f = 'X';
    const char *endptr = &f;
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtoui_invalid(void)
{
    const char *str = "   xxxx  \t abc";
    char f = 'X';
    const char *endptr = &f;
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtoui_trailing(void)
{
    const char *str = "123xxx";
    char f = 'X';
    const char *endptr = &f;
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 123);
    g_assert_true(endptr == str + 3);
}

static void test_qemu_strtoui_octal(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, &endptr, 8, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0123);
    g_assert_true(endptr == str + strlen(str));

    res = 999;
    endptr = &f;
    err = qemu_strtoui(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0123);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoui_decimal(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, &endptr, 10, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 123);
    g_assert_true(endptr == str + strlen(str));

    str = "123";
    res = 999;
    endptr = &f;
    err = qemu_strtoui(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 123);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoui_hex(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, &endptr, 16, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 0x123);
    g_assert_true(endptr == str + strlen(str));

    str = "0x123";
    res = 999;
    endptr = &f;
    err = qemu_strtoui(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 0x123);
    g_assert_true(endptr == str + strlen(str));

    str = "0x";
    res = 999;
    endptr = &f;
    err = qemu_strtoui(str, &endptr, 16, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 0);
    g_assert_true(endptr == str + 1);
}

static void test_qemu_strtoui_wrap(void)
{
    /* wraparound is consistent with 32-bit strtoul */
    const char *str = "-4294967295"; /* 1 mod 2^32 */
    char f = 'X';
    const char *endptr = &f;
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 1);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoui_max(void)
{
    char *str = g_strdup_printf("%u", UINT_MAX);
    char f = 'X';
    const char *endptr = &f;
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, UINT_MAX);
    g_assert_true(endptr == str + strlen(str));
    g_free(str);
}

static void test_qemu_strtoui_overflow(void)
{
    const char *str;
    const char *endptr;
    unsigned int res;
    int err;

    str = "4294967296"; /* UINT_MAX + 1ll */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoui(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "0x7fffffffffffffff"; /* LLONG_MAX */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoui(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "0x8000000000000000"; /* (uint64_t)LLONG_MIN */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoui(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "0xffffffff00000001"; /* ULLONG_MAX - UINT_MAX + 1 (not 1) */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoui(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "0xfffffffffffffffe"; /* ULLONG_MAX - 1 (not UINT_MAX - 1) */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoui(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "0x10000000000000000"; /* 65 bits, 32-bit sign bit clear */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoui(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "0x18000000080000000"; /* 65 bits, 32-bit sign bit set */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoui(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT_MAX);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoui_underflow(void)
{
    const char *str;
    const char *endptr;
    unsigned int res;
    int err;

    str = "-4294967296"; /* -(long long)UINT_MAX - 1ll */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoui(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "-18446744073709551615"; /* -UINT64_MAX (not -(-1)) */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoui(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "-0xffffffff00000002";
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoui(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "-0x10000000000000000"; /* 65 bits, 32-bit sign bit clear */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoui(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "-0x18000000080000000"; /* 65 bits, 32-bit sign bit set */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoui(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT_MAX);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoui_negative(void)
{
    const char *str = "  \t -321";
    char f = 'X';
    const char *endptr = &f;
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, (unsigned int)-321);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoui_negzero(void)
{
    const char *str = " -0";
    char f = 'X';
    const char *endptr = &f;
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoui_full_correct(void)
{
    const char *str = "123";
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 123);
}

static void test_qemu_strtoui_full_null(void)
{
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(NULL, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 999);
}

static void test_qemu_strtoui_full_empty(void)
{
    const char *str = "";
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 0);
}

static void test_qemu_strtoui_full_negative(void)
{
    const char *str = " \t -321";
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, NULL, 0, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, (unsigned int)-321);
}

static void test_qemu_strtoui_full_negzero(void)
{
    const char *str = " -0";
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, NULL, 0, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0);
}

static void test_qemu_strtoui_full_trailing(void)
{
    const char *str = "123xxx";
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 123);
}

static void test_qemu_strtoui_full_max(void)
{
    char *str = g_strdup_printf("%u", UINT_MAX);
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, UINT_MAX);
    g_free(str);
}

static void test_qemu_strtoui_full_erange_junk(void)
{
    /* EINVAL has priority over ERANGE */
    const char *str = "-9999999999junk";
    unsigned int res = 999;
    int err;

    err = qemu_strtoui(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, UINT_MAX);
}

static void test_qemu_strtol_correct(void)
{
    const char *str = "12345 foo";
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 12345);
    g_assert_true(endptr == str + 5);
}

static void test_qemu_strtol_null(void)
{
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err = qemu_strtol(NULL, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 999);
    g_assert_null(endptr);
}

static void test_qemu_strtol_empty(void)
{
    const char *str = "";
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtol_whitespace(void)
{
    const char *str = "  \t  ";
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtol_invalid(void)
{
    const char *str = "   xxxx  \t abc";
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtol_trailing(void)
{
    const char *str = "123xxx";
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
    g_assert_true(endptr == str + 3);
}

static void test_qemu_strtol_octal(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err = qemu_strtol(str, &endptr, 8, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0123);
    g_assert_true(endptr == str + strlen(str));

    res = 999;
    endptr = &f;
    err = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0123);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtol_decimal(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err = qemu_strtol(str, &endptr, 10, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
    g_assert_true(endptr == str + strlen(str));

    str = "123";
    res = 999;
    endptr = &f;
    err = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtol_hex(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err = qemu_strtol(str, &endptr, 16, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0x123);
    g_assert_true(endptr == str + strlen(str));

    str = "0x123";
    res = 999;
    endptr = &f;
    err = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0x123);
    g_assert_true(endptr == str + strlen(str));

    str = "0x";
    res = 999;
    endptr = &f;
    err = qemu_strtol(str, &endptr, 16, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0);
    g_assert_true(endptr == str + 1);
}

static void test_qemu_strtol_max(void)
{
    char *str = g_strdup_printf("%ld", LONG_MAX);
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, LONG_MAX);
    g_assert_true(endptr == str + strlen(str));
    g_free(str);
}

static void test_qemu_strtol_overflow(void)
{
    const char *str;
    const char *endptr;
    long res;
    int err;

    /* 1 more than LONG_MAX */
    str = LONG_MAX == INT_MAX ? "2147483648" : "9223372036854775808";
    endptr = "somewhere";
    res = 999;
    err = qemu_strtol(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, LONG_MAX);
    g_assert_true(endptr == str + strlen(str));

    if (LONG_MAX == INT_MAX) {
        str = "0xffffffff00000001"; /* ULLONG_MAX - UINT_MAX + 1 (not 1) */
        endptr = "somewhere";
        res = 999;
        err = qemu_strtol(str, &endptr, 0, &res);
        g_assert_cmpint(err, ==, -ERANGE);
        g_assert_cmpint(res, ==, LONG_MAX);
        g_assert_true(endptr == str + strlen(str));
    }

    str = "0x10000000000000000"; /* 65 bits, either sign bit position clear */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtol(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, LONG_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "0x18000000080000000"; /* 65 bits, either sign bit position set */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtol(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, LONG_MAX);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtol_min(void)
{
    char *str = g_strdup_printf("%ld", LONG_MIN);
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, LONG_MIN);
    g_assert_true(endptr == str + strlen(str));
    g_free(str);
}

static void test_qemu_strtol_underflow(void)
{
    const char *str;
    const char *endptr;
    long res;
    int err;

    /* 1 less than LONG_MIN */
    str = LONG_MIN == INT_MIN ? "-2147483649" : "-9223372036854775809";
    endptr = "somewhere";
    res = 999;
    err = qemu_strtol(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, LONG_MIN);
    g_assert_true(endptr == str + strlen(str));

    if (LONG_MAX == INT_MAX) {
        str = "-18446744073709551615"; /* -UINT64_MAX (not 1) */
        endptr = "somewhere";
        res = 999;
        err = qemu_strtol(str, &endptr, 0, &res);
        g_assert_cmpint(err, ==, -ERANGE);
        g_assert_cmpint(res, ==, LONG_MIN);
        g_assert_true(endptr == str + strlen(str));
    }

    str = "-0x10000000000000000"; /* 65 bits, either sign bit position clear */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtol(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, LONG_MIN);
    g_assert_true(endptr == str + strlen(str));

    str = "-0x18000000080000000"; /* 65 bits, either sign bit position set */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtol(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, LONG_MIN);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtol_negative(void)
{
    const char *str = "  \t -321";
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, -321);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtol_negzero(void)
{
    const char *str = " -0";
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtol_full_correct(void)
{
    const char *str = "123";
    long res = 999;
    int err;

    err = qemu_strtol(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
}

static void test_qemu_strtol_full_null(void)
{
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err = qemu_strtol(NULL, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 999);
    g_assert_null(endptr);
}

static void test_qemu_strtol_full_empty(void)
{
    const char *str = "";
    long res = 999L;
    int err;

    err = qemu_strtol(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 0);
}

static void test_qemu_strtol_full_negative(void)
{
    const char *str = " \t -321";
    long res = 999;
    int err;

    err = qemu_strtol(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, -321);
}

static void test_qemu_strtol_full_negzero(void)
{
    const char *str = " -0";
    long res = 999;
    int err;

    err = qemu_strtol(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0);
}

static void test_qemu_strtol_full_trailing(void)
{
    const char *str = "123xxx";
    long res = 999;
    int err;

    err = qemu_strtol(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 123);
}

static void test_qemu_strtol_full_max(void)
{
    char *str = g_strdup_printf("%ld", LONG_MAX);
    long res = 999;
    int err;

    err = qemu_strtol(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, LONG_MAX);
    g_free(str);
}

static void test_qemu_strtol_full_erange_junk(void)
{
    /* EINVAL has priority over ERANGE */
    const char *str = "-99999999999999999999junk";
    long res = 999;
    int err;

    err = qemu_strtol(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, LONG_MIN);
}

static void test_qemu_strtoul_correct(void)
{
    const char *str = "12345 foo";
    char f = 'X';
    const char *endptr = &f;
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 12345);
    g_assert_true(endptr == str + 5);
}

static void test_qemu_strtoul_null(void)
{
    char f = 'X';
    const char *endptr = &f;
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(NULL, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 999);
    g_assert_null(endptr);
}

static void test_qemu_strtoul_empty(void)
{
    const char *str = "";
    char f = 'X';
    const char *endptr = &f;
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtoul_whitespace(void)
{
    const char *str = "  \t  ";
    char f = 'X';
    const char *endptr = &f;
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtoul_invalid(void)
{
    const char *str = "   xxxx  \t abc";
    char f = 'X';
    const char *endptr = &f;
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtoul_trailing(void)
{
    const char *str = "123xxx";
    char f = 'X';
    const char *endptr = &f;
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 123);
    g_assert_true(endptr == str + 3);
}

static void test_qemu_strtoul_octal(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, &endptr, 8, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0123);
    g_assert_true(endptr == str + strlen(str));

    res = 999;
    endptr = &f;
    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0123);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoul_decimal(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, &endptr, 10, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 123);
    g_assert_true(endptr == str + strlen(str));

    str = "123";
    res = 999;
    endptr = &f;
    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 123);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoul_hex(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, &endptr, 16, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 0x123);
    g_assert_true(endptr == str + strlen(str));

    str = "0x123";
    res = 999;
    endptr = &f;
    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 0x123);
    g_assert_true(endptr == str + strlen(str));

    str = "0x";
    res = 999;
    endptr = &f;
    err = qemu_strtoul(str, &endptr, 16, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 0);
    g_assert_true(endptr == str + 1);
}

static void test_qemu_strtoul_wrap(void)
{
    const char *str;
    char f = 'X';
    const char *endptr = &f;
    unsigned long res = 999;
    int err;

    /* 1 mod 2^(sizeof(long)*8) */
    str = LONG_MAX == INT_MAX ? "-4294967295" : "-18446744073709551615";
    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 1);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoul_max(void)
{
    char *str = g_strdup_printf("%lu", ULONG_MAX);
    char f = 'X';
    const char *endptr = &f;
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, ULONG_MAX);
    g_assert_true(endptr == str + strlen(str));
    g_free(str);
}

static void test_qemu_strtoul_overflow(void)
{
    const char *str;
    const char *endptr;
    unsigned long res;
    int err;

    /* 1 more than ULONG_MAX */
    str = ULONG_MAX == UINT_MAX ? "4294967296" : "18446744073709551616";
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoul(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, ULONG_MAX);
    g_assert_true(endptr == str + strlen(str));

    if (LONG_MAX == INT_MAX) {
        str = "0xffffffff00000001"; /* UINT64_MAX - UINT_MAX + 1 (not 1) */
        endptr = "somewhere";
        res = 999;
        err = qemu_strtoul(str, &endptr, 0, &res);
        g_assert_cmpint(err, ==, -ERANGE);
        g_assert_cmpuint(res, ==, ULONG_MAX);
        g_assert_true(endptr == str + strlen(str));
    }

    str = "0x10000000000000000"; /* 65 bits, either sign bit position clear */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoul(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, ULONG_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "0x18000000080000000"; /* 65 bits, either sign bit position set */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoul(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, ULONG_MAX);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoul_underflow(void)
{
    const char *str;
    const char *endptr;
    unsigned long res;
    int err;

    /* 1 less than -ULONG_MAX */
    str = ULONG_MAX == UINT_MAX ? "-4294967296" : "-18446744073709551616";
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoul(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, ULONG_MAX);
    g_assert_true(endptr == str + strlen(str));

    if (LONG_MAX == INT_MAX) {
        str = "-0xffffffff00000002";
        endptr = "somewhere";
        res = 999;
        err = qemu_strtoul(str, &endptr, 0, &res);
        g_assert_cmpint(err, ==, -ERANGE);
        g_assert_cmpuint(res, ==, ULONG_MAX);
        g_assert_true(endptr == str + strlen(str));
    }

    str = "-0x10000000000000000"; /* 65 bits, either sign bit position clear */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoul(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, ULONG_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "-0x18000000080000000"; /* 65 bits, either sign bit position set */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoul(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, ULONG_MAX);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoul_negative(void)
{
    const char *str = "  \t -321";
    char f = 'X';
    const char *endptr = &f;
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, -321ul);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoul_negzero(void)
{
    const char *str = " -0";
    char f = 'X';
    const char *endptr = &f;
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoul_full_correct(void)
{
    const char *str = "123";
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 123);
}

static void test_qemu_strtoul_full_null(void)
{
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(NULL, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 999);
}

static void test_qemu_strtoul_full_empty(void)
{
    const char *str = "";
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 0);
}

static void test_qemu_strtoul_full_negative(void)
{
    const char *str = " \t -321";
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, NULL, 0, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, -321ul);
}

static void test_qemu_strtoul_full_negzero(void)
{
    const char *str = " -0";
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, NULL, 0, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0);
}

static void test_qemu_strtoul_full_trailing(void)
{
    const char *str = "123xxx";
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 123);
}

static void test_qemu_strtoul_full_max(void)
{
    char *str = g_strdup_printf("%lu", ULONG_MAX);
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, ULONG_MAX);
    g_free(str);
}

static void test_qemu_strtoul_full_erange_junk(void)
{
    /* EINVAL has priority over ERANGE */
    const char *str = "-99999999999999999999junk";
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, ULONG_MAX);
}

static void test_qemu_strtoi64_correct(void)
{
    const char *str = "12345 foo";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 12345);
    g_assert_true(endptr == str + 5);
}

static void test_qemu_strtoi64_null(void)
{
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(NULL, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 999);
    g_assert_null(endptr);
}

static void test_qemu_strtoi64_empty(void)
{
    const char *str = "";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtoi64_whitespace(void)
{
    const char *str = "  \t  ";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtoi64_invalid(void)
{
    const char *str = "   xxxx  \t abc";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtoi64_trailing(void)
{
    const char *str = "123xxx";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
    g_assert_true(endptr == str + 3);
}

static void test_qemu_strtoi64_octal(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, &endptr, 8, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0123);
    g_assert_true(endptr == str + strlen(str));

    endptr = &f;
    res = 999;
    err = qemu_strtoi64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0123);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoi64_decimal(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, &endptr, 10, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
    g_assert_true(endptr == str + strlen(str));

    str = "123";
    endptr = &f;
    res = 999;
    err = qemu_strtoi64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoi64_hex(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, &endptr, 16, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0x123);
    g_assert_true(endptr == str + strlen(str));

    str = "0x123";
    endptr = &f;
    res = 999;
    err = qemu_strtoi64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0x123);
    g_assert_true(endptr == str + strlen(str));

    str = "0x";
    endptr = &f;
    res = 999;
    err = qemu_strtoi64(str, &endptr, 16, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0);
    g_assert_true(endptr == str + 1);
}

static void test_qemu_strtoi64_max(void)
{
    char *str = g_strdup_printf("%lld", LLONG_MAX);
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, LLONG_MAX);
    g_assert_true(endptr == str + strlen(str));
    g_free(str);
}

static void test_qemu_strtoi64_overflow(void)
{
    const char *str;
    const char *endptr;
    int64_t res;
    int err;

    str = "9223372036854775808"; /* 1 more than INT64_MAX */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi64(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, INT64_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "0x10000000000000000"; /* 65 bits, 64-bit sign bit clear */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi64(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, INT64_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "0x18000000080000000"; /* 65 bits, 64-bit sign bit set */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi64(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, INT64_MAX);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoi64_min(void)
{
    char *str = g_strdup_printf("%lld", LLONG_MIN);
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, LLONG_MIN);
    g_assert_true(endptr == str + strlen(str));
    g_free(str);
}

static void test_qemu_strtoi64_underflow(void)
{
    const char *str;
    const char *endptr;
    int64_t res;
    int err;

    str = "-9223372036854775809"; /* 1 less than INT64_MIN */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi64(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, INT64_MIN);
    g_assert_true(endptr == str + strlen(str));

    str = "-0x10000000000000000"; /* 65 bits, 64-bit sign bit clear */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi64(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, INT64_MIN);
    g_assert_true(endptr == str + strlen(str));

    str = "-0x18000000080000000"; /* 65 bits, 64-bit sign bit set */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtoi64(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, INT64_MIN);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoi64_negative(void)
{
    const char *str = "  \t -321";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, -321);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoi64_negzero(void)
{
    const char *str = " -0";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtoi64_full_correct(void)
{
    const char *str = "123";
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
}

static void test_qemu_strtoi64_full_null(void)
{
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(NULL, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 999);
}

static void test_qemu_strtoi64_full_empty(void)
{
    const char *str = "";
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 0);
}

static void test_qemu_strtoi64_full_negative(void)
{
    const char *str = " \t -321";
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, -321);
}

static void test_qemu_strtoi64_full_negzero(void)
{
    const char *str = " -0";
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0);
}

static void test_qemu_strtoi64_full_trailing(void)
{
    const char *str = "123xxx";
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, 123);
}

static void test_qemu_strtoi64_full_max(void)
{

    char *str = g_strdup_printf("%lld", LLONG_MAX);
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, LLONG_MAX);
    g_free(str);
}

static void test_qemu_strtoi64_full_erange_junk(void)
{
    /* EINVAL has priority over ERANGE */
    const char *str = "-99999999999999999999junk";
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpint(res, ==, INT64_MIN);
}

static void test_qemu_strtou64_correct(void)
{
    const char *str = "12345 foo";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 12345);
    g_assert_true(endptr == str + 5);
}

static void test_qemu_strtou64_null(void)
{
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(NULL, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 999);
    g_assert_null(endptr);
}

static void test_qemu_strtou64_empty(void)
{
    const char *str = "";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtou64_whitespace(void)
{
    const char *str = "  \t  ";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtou64_invalid(void)
{
    const char *str = "   xxxx  \t abc";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 0);
    g_assert_true(endptr == str);
}

static void test_qemu_strtou64_trailing(void)
{
    const char *str = "123xxx";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 123);
    g_assert_true(endptr == str + 3);
}

static void test_qemu_strtou64_octal(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, &endptr, 8, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0123);
    g_assert_true(endptr == str + strlen(str));

    endptr = &f;
    res = 999;
    err = qemu_strtou64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0123);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtou64_decimal(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, &endptr, 10, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 123);
    g_assert_true(endptr == str + strlen(str));

    str = "123";
    endptr = &f;
    res = 999;
    err = qemu_strtou64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 123);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtou64_hex(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, &endptr, 16, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 0x123);
    g_assert_true(endptr == str + strlen(str));

    str = "0x123";
    endptr = &f;
    res = 999;
    err = qemu_strtou64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 0x123);
    g_assert_true(endptr == str + strlen(str));

    str = "0x";
    endptr = &f;
    res = 999;
    err = qemu_strtou64(str, &endptr, 16, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 0);
    g_assert_true(endptr == str + 1);
}

static void test_qemu_strtou64_wrap(void)
{
    const char *str = "-18446744073709551615"; /* 1 mod 2^64 */
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 1);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtou64_max(void)
{
    char *str = g_strdup_printf("%llu", ULLONG_MAX);
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, ULLONG_MAX);
    g_assert_true(endptr == str + strlen(str));
    g_free(str);
}

static void test_qemu_strtou64_overflow(void)
{
    const char *str;
    const char *endptr;
    uint64_t res;
    int err;

    str = "18446744073709551616"; /* 1 more than UINT64_MAX */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtou64(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT64_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "0x10000000000000000"; /* 65 bits, 64-bit sign bit clear */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtou64(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT64_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "0x18000000080000000"; /* 65 bits, 64-bit sign bit set */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtou64(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT64_MAX);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtou64_underflow(void)
{
    const char *str;
    const char *endptr;
    uint64_t res;
    int err;

    str = "-99999999999999999999999999999999999999999999";
    endptr = "somewhere";
    res = 999;
    err = qemu_strtou64(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT64_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "-0x10000000000000000"; /* 65 bits, 64-bit sign bit clear */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtou64(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT64_MAX);
    g_assert_true(endptr == str + strlen(str));

    str = "-0x18000000080000000"; /* 65 bits, 64-bit sign bit set */
    endptr = "somewhere";
    res = 999;
    err = qemu_strtou64(str, &endptr, 0, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, UINT64_MAX);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtou64_negative(void)
{
    const char *str = "  \t -321";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, -321ull);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtou64_negzero(void)
{
    const char *str = " -0";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0);
    g_assert_true(endptr == str + strlen(str));
}

static void test_qemu_strtou64_full_correct(void)
{
    const char *str = "18446744073709551614";
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 18446744073709551614ull);
}

static void test_qemu_strtou64_full_null(void)
{
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(NULL, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 999);
}

static void test_qemu_strtou64_full_empty(void)
{
    const char *str = "";
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 0);
}

static void test_qemu_strtou64_full_negative(void)
{
    const char *str = " \t -321";
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, -321ull);
}

static void test_qemu_strtou64_full_negzero(void)
{
    const char *str = " -0";
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0);
}

static void test_qemu_strtou64_full_trailing(void)
{
    const char *str = "18446744073709551614xxxxxx";
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, 18446744073709551614ULL);
}

static void test_qemu_strtou64_full_max(void)
{
    char *str = g_strdup_printf("%lld", ULLONG_MAX);
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, ULLONG_MAX);
    g_free(str);
}

static void test_qemu_strtou64_full_erange_junk(void)
{
    /* EINVAL has priority over ERANGE */
    const char *str = "-99999999999999999999junk";
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmpuint(res, ==, UINT64_MAX);
}

static void test_qemu_strtosz_simple(void)
{
    const char *str;
    const char *endptr;
    int err;
    uint64_t res;

    str = "0";
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0);
    g_assert_true(endptr == str + 1);

    /* Leading 0 gives decimal results, not octal */
    str = "08";
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 8);
    g_assert_true(endptr == str + 2);

    /* Leading space is ignored */
    str = " 12345";
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 12345);
    g_assert_true(endptr == str + 6);

    res = 0xbaadf00d;
    err = qemu_strtosz(str, NULL, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 12345);

    str = "9007199254740991"; /* 2^53-1 */
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 0x1fffffffffffffULL);
    g_assert_true(endptr == str + 16);

    str = "9007199254740992"; /* 2^53 */
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 0x20000000000000ULL);
    g_assert_true(endptr == str + 16);

    str = "9007199254740993"; /* 2^53+1 */
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 0x20000000000001ULL);
    g_assert_true(endptr == str + 16);

    str = "18446744073709549568"; /* 0xfffffffffffff800 (53 msbs set) */
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 0xfffffffffffff800ULL);
    g_assert_true(endptr == str + 20);

    str = "18446744073709550591"; /* 0xfffffffffffffbff */
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 0xfffffffffffffbffULL);
    g_assert_true(endptr == str + 20);

    str = "18446744073709551615"; /* 0xffffffffffffffff */
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 0xffffffffffffffffULL);
    g_assert_true(endptr == str + 20);
}

static void test_qemu_strtosz_hex(void)
{
    const char *str;
    const char *endptr;
    int err;
    uint64_t res;

    str = "0x0";
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0);
    g_assert_true(endptr == str + 3);

    str = "0xab";
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 171);
    g_assert_true(endptr == str + 4);

    str = "0xae";
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 174);
    g_assert_true(endptr == str + 4);
}

static void test_qemu_strtosz_units(void)
{
    const char *none = "1";
    const char *b = "1B";
    const char *k = "1K";
    const char *m = "1M";
    const char *g = "1G";
    const char *t = "1T";
    const char *p = "1P";
    const char *e = "1E";
    int err;
    const char *endptr;
    uint64_t res;

    /* default is M */
    endptr = NULL;
    res = 0xbaadf00d;
    err = qemu_strtosz_MiB(none, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, MiB);
    g_assert_true(endptr == none + 1);

    endptr = NULL;
    res = 0xbaadf00d;
    err = qemu_strtosz(b, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 1);
    g_assert_true(endptr == b + 2);

    endptr = NULL;
    res = 0xbaadf00d;
    err = qemu_strtosz(k, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, KiB);
    g_assert_true(endptr == k + 2);

    endptr = NULL;
    res = 0xbaadf00d;
    err = qemu_strtosz(m, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, MiB);
    g_assert_true(endptr == m + 2);

    endptr = NULL;
    res = 0xbaadf00d;
    err = qemu_strtosz(g, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, GiB);
    g_assert_true(endptr == g + 2);

    endptr = NULL;
    res = 0xbaadf00d;
    err = qemu_strtosz(t, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, TiB);
    g_assert_true(endptr == t + 2);

    endptr = NULL;
    res = 0xbaadf00d;
    err = qemu_strtosz(p, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, PiB);
    g_assert_true(endptr == p + 2);

    endptr = NULL;
    res = 0xbaadf00d;
    err = qemu_strtosz(e, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, EiB);
    g_assert_true(endptr == e + 2);
}

static void test_qemu_strtosz_float(void)
{
    const char *str;
    int err;
    const char *endptr;
    uint64_t res;

    str = "0.5E";
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, EiB / 2);
    g_assert_true(endptr == str + 4);

    /* For convenience, a fraction of 0 is tolerated even on bytes */
    str = "1.0B";
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 1);
    g_assert_true(endptr == str + 4);

    /* An empty fraction is tolerated */
    str = "1.k";
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 1024);
    g_assert_true(endptr == str + 3);

    /* For convenience, we permit values that are not byte-exact */
    str = "12.345M";
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, (uint64_t) (12.345 * MiB + 0.5));
    g_assert_true(endptr == str + 7);
}

static void test_qemu_strtosz_invalid(void)
{
    const char *str;
    const char *endptr;
    int err;
    uint64_t res = 0xbaadf00d;

    str = "";
    endptr = NULL;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);
    g_assert_true(endptr == str);

    str = " \t ";
    endptr = NULL;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);
    g_assert_true(endptr == str);

    str = "crap";
    endptr = NULL;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);
    g_assert_true(endptr == str);

    str = "inf";
    endptr = NULL;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);
    g_assert_true(endptr == str);

    str = "NaN";
    endptr = NULL;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);
    g_assert_true(endptr == str);

    /* Fractional values require scale larger than bytes */
    str = "1.1B";
    endptr = NULL;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);
    g_assert_true(endptr == str);

    str = "1.1";
    endptr = NULL;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);
    g_assert_true(endptr == str);

    /* No floating point exponents */
    str = "1.5e1k";
    endptr = NULL;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);
    g_assert_true(endptr == str);

    str = "1.5E+0k";
    endptr = NULL;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);
    g_assert_true(endptr == str);

    /* No hex fractions */
    str = "0x1.8k";
    endptr = NULL;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);
    g_assert_true(endptr == str);

    /* No suffixes */
    str = "0x18M";
    endptr = NULL;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);
    g_assert_true(endptr == str);

    /* No negative values */
    str = "-0";
    endptr = NULL;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);
    g_assert_true(endptr == str);

    str = "-1";
    endptr = NULL;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);
    g_assert_true(endptr == str);
}

static void test_qemu_strtosz_trailing(void)
{
    const char *str;
    const char *endptr;
    int err;
    uint64_t res;

    str = "123xxx";
    endptr = NULL;
    res = 0xbaadf00d;
    err = qemu_strtosz_MiB(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 123 * MiB);
    g_assert_true(endptr == str + 3);

    res = 0xbaadf00d;
    err = qemu_strtosz(str, NULL, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);

    str = "1kiB";
    endptr = NULL;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 1024);
    g_assert_true(endptr == str + 2);

    res = 0xbaadf00d;
    err = qemu_strtosz(str, NULL, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);

    str = "0x";
    endptr = NULL;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0);
    g_assert_true(endptr == str + 1);

    res = 0xbaadf00d;
    err = qemu_strtosz(str, NULL, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);

    str = "0.NaN";
    endptr = NULL;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0);
    g_assert_true(endptr == str + 2);

    res = 0xbaadf00d;
    err = qemu_strtosz(str, NULL, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);

    str = "123-45";
    endptr = NULL;
    res = 0xbaadf00d;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 123);
    g_assert_true(endptr == str + 3);

    res = 0xbaadf00d;
    err = qemu_strtosz(str, NULL, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert_cmphex(res, ==, 0xbaadf00d);
}

static void test_qemu_strtosz_erange(void)
{
    const char *str;
    const char *endptr;
    int err;
    uint64_t res = 0xbaadf00d;

    str = "18446744073709551616"; /* 2^64; see strtosz_simple for 2^64-1 */
    endptr = NULL;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmphex(res, ==, 0xbaadf00d);
    g_assert_true(endptr == str + 20);

    str = "20E";
    endptr = NULL;
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmphex(res, ==, 0xbaadf00d);
    g_assert_true(endptr == str + 3);
}

static void test_qemu_strtosz_metric(void)
{
    const char *str;
    int err;
    const char *endptr;
    uint64_t res;

    str = "12345k";
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz_metric(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 12345000);
    g_assert_true(endptr == str + 6);

    str = "12.345M";
    endptr = str;
    res = 0xbaadf00d;
    err = qemu_strtosz_metric(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 12345000);
    g_assert_true(endptr == str + 7);
}

static void test_freq_to_str(void)
{
    char *str;

    str = freq_to_str(999);
    g_assert_cmpstr(str, ==, "999 Hz");
    g_free(str);

    str = freq_to_str(1000);
    g_assert_cmpstr(str, ==, "1 KHz");
    g_free(str);

    str = freq_to_str(1010);
    g_assert_cmpstr(str, ==, "1.01 KHz");
    g_free(str);
}

static void test_size_to_str(void)
{
    char *str;

    str = size_to_str(0);
    g_assert_cmpstr(str, ==, "0 B");
    g_free(str);

    str = size_to_str(1);
    g_assert_cmpstr(str, ==, "1 B");
    g_free(str);

    str = size_to_str(1016);
    g_assert_cmpstr(str, ==, "0.992 KiB");
    g_free(str);

    str = size_to_str(1024);
    g_assert_cmpstr(str, ==, "1 KiB");
    g_free(str);

    str = size_to_str(512ull << 20);
    g_assert_cmpstr(str, ==, "512 MiB");
    g_free(str);
}

static void test_iec_binary_prefix(void)
{
    g_assert_cmpstr(iec_binary_prefix(0), ==, "");
    g_assert_cmpstr(iec_binary_prefix(10), ==, "Ki");
    g_assert_cmpstr(iec_binary_prefix(20), ==, "Mi");
    g_assert_cmpstr(iec_binary_prefix(30), ==, "Gi");
    g_assert_cmpstr(iec_binary_prefix(40), ==, "Ti");
    g_assert_cmpstr(iec_binary_prefix(50), ==, "Pi");
    g_assert_cmpstr(iec_binary_prefix(60), ==, "Ei");
}

static void test_si_prefix(void)
{
    g_assert_cmpstr(si_prefix(-18), ==, "a");
    g_assert_cmpstr(si_prefix(-15), ==, "f");
    g_assert_cmpstr(si_prefix(-12), ==, "p");
    g_assert_cmpstr(si_prefix(-9), ==, "n");
    g_assert_cmpstr(si_prefix(-6), ==, "u");
    g_assert_cmpstr(si_prefix(-3), ==, "m");
    g_assert_cmpstr(si_prefix(0), ==, "");
    g_assert_cmpstr(si_prefix(3), ==, "K");
    g_assert_cmpstr(si_prefix(6), ==, "M");
    g_assert_cmpstr(si_prefix(9), ==, "G");
    g_assert_cmpstr(si_prefix(12), ==, "T");
    g_assert_cmpstr(si_prefix(15), ==, "P");
    g_assert_cmpstr(si_prefix(18), ==, "E");
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/cutils/parse_uint/null", test_parse_uint_null);
    g_test_add_func("/cutils/parse_uint/empty", test_parse_uint_empty);
    g_test_add_func("/cutils/parse_uint/whitespace",
                    test_parse_uint_whitespace);
    g_test_add_func("/cutils/parse_uint/invalid", test_parse_uint_invalid);
    g_test_add_func("/cutils/parse_uint/trailing", test_parse_uint_trailing);
    g_test_add_func("/cutils/parse_uint/correct", test_parse_uint_correct);
    g_test_add_func("/cutils/parse_uint/octal", test_parse_uint_octal);
    g_test_add_func("/cutils/parse_uint/decimal", test_parse_uint_decimal);
    g_test_add_func("/cutils/parse_uint/llong_max", test_parse_uint_llong_max);
    g_test_add_func("/cutils/parse_uint/max", test_parse_uint_max);
    g_test_add_func("/cutils/parse_uint/overflow", test_parse_uint_overflow);
    g_test_add_func("/cutils/parse_uint/negative", test_parse_uint_negative);
    g_test_add_func("/cutils/parse_uint/negzero", test_parse_uint_negzero);
    g_test_add_func("/cutils/parse_uint_full/trailing",
                    test_parse_uint_full_trailing);
    g_test_add_func("/cutils/parse_uint_full/correct",
                    test_parse_uint_full_correct);
    g_test_add_func("/cutils/parse_uint_full/erange_junk",
                    test_parse_uint_full_erange_junk);

    /* qemu_strtoi() tests */
    g_test_add_func("/cutils/qemu_strtoi/correct",
                    test_qemu_strtoi_correct);
    g_test_add_func("/cutils/qemu_strtoi/null",
                    test_qemu_strtoi_null);
    g_test_add_func("/cutils/qemu_strtoi/empty",
                    test_qemu_strtoi_empty);
    g_test_add_func("/cutils/qemu_strtoi/whitespace",
                    test_qemu_strtoi_whitespace);
    g_test_add_func("/cutils/qemu_strtoi/invalid",
                    test_qemu_strtoi_invalid);
    g_test_add_func("/cutils/qemu_strtoi/trailing",
                    test_qemu_strtoi_trailing);
    g_test_add_func("/cutils/qemu_strtoi/octal",
                    test_qemu_strtoi_octal);
    g_test_add_func("/cutils/qemu_strtoi/decimal",
                    test_qemu_strtoi_decimal);
    g_test_add_func("/cutils/qemu_strtoi/hex",
                    test_qemu_strtoi_hex);
    g_test_add_func("/cutils/qemu_strtoi/max",
                    test_qemu_strtoi_max);
    g_test_add_func("/cutils/qemu_strtoi/overflow",
                    test_qemu_strtoi_overflow);
    g_test_add_func("/cutils/qemu_strtoi/min",
                    test_qemu_strtoi_min);
    g_test_add_func("/cutils/qemu_strtoi/underflow",
                    test_qemu_strtoi_underflow);
    g_test_add_func("/cutils/qemu_strtoi/negative",
                    test_qemu_strtoi_negative);
    g_test_add_func("/cutils/qemu_strtoi/negzero",
                    test_qemu_strtoi_negzero);
    g_test_add_func("/cutils/qemu_strtoi_full/correct",
                    test_qemu_strtoi_full_correct);
    g_test_add_func("/cutils/qemu_strtoi_full/null",
                    test_qemu_strtoi_full_null);
    g_test_add_func("/cutils/qemu_strtoi_full/empty",
                    test_qemu_strtoi_full_empty);
    g_test_add_func("/cutils/qemu_strtoi_full/negative",
                    test_qemu_strtoi_full_negative);
    g_test_add_func("/cutils/qemu_strtoi_full/negzero",
                    test_qemu_strtoi_full_negzero);
    g_test_add_func("/cutils/qemu_strtoi_full/trailing",
                    test_qemu_strtoi_full_trailing);
    g_test_add_func("/cutils/qemu_strtoi_full/max",
                    test_qemu_strtoi_full_max);
    g_test_add_func("/cutils/qemu_strtoi_full/erange_junk",
                    test_qemu_strtoi_full_erange_junk);

    /* qemu_strtoui() tests */
    g_test_add_func("/cutils/qemu_strtoui/correct",
                    test_qemu_strtoui_correct);
    g_test_add_func("/cutils/qemu_strtoui/null",
                    test_qemu_strtoui_null);
    g_test_add_func("/cutils/qemu_strtoui/empty",
                    test_qemu_strtoui_empty);
    g_test_add_func("/cutils/qemu_strtoui/whitespace",
                    test_qemu_strtoui_whitespace);
    g_test_add_func("/cutils/qemu_strtoui/invalid",
                    test_qemu_strtoui_invalid);
    g_test_add_func("/cutils/qemu_strtoui/trailing",
                    test_qemu_strtoui_trailing);
    g_test_add_func("/cutils/qemu_strtoui/octal",
                    test_qemu_strtoui_octal);
    g_test_add_func("/cutils/qemu_strtoui/decimal",
                    test_qemu_strtoui_decimal);
    g_test_add_func("/cutils/qemu_strtoui/hex",
                    test_qemu_strtoui_hex);
    g_test_add_func("/cutils/qemu_strtoui/wrap",
                    test_qemu_strtoui_wrap);
    g_test_add_func("/cutils/qemu_strtoui/max",
                    test_qemu_strtoui_max);
    g_test_add_func("/cutils/qemu_strtoui/overflow",
                    test_qemu_strtoui_overflow);
    g_test_add_func("/cutils/qemu_strtoui/underflow",
                    test_qemu_strtoui_underflow);
    g_test_add_func("/cutils/qemu_strtoui/negative",
                    test_qemu_strtoui_negative);
    g_test_add_func("/cutils/qemu_strtoui/negzero",
                    test_qemu_strtoui_negzero);
    g_test_add_func("/cutils/qemu_strtoui_full/correct",
                    test_qemu_strtoui_full_correct);
    g_test_add_func("/cutils/qemu_strtoui_full/null",
                    test_qemu_strtoui_full_null);
    g_test_add_func("/cutils/qemu_strtoui_full/empty",
                    test_qemu_strtoui_full_empty);
    g_test_add_func("/cutils/qemu_strtoui_full/negative",
                    test_qemu_strtoui_full_negative);
    g_test_add_func("/cutils/qemu_strtoui_full/negzero",
                    test_qemu_strtoui_full_negzero);
    g_test_add_func("/cutils/qemu_strtoui_full/trailing",
                    test_qemu_strtoui_full_trailing);
    g_test_add_func("/cutils/qemu_strtoui_full/max",
                    test_qemu_strtoui_full_max);
    g_test_add_func("/cutils/qemu_strtoui_full/erange_junk",
                    test_qemu_strtoui_full_erange_junk);

    /* qemu_strtol() tests */
    g_test_add_func("/cutils/qemu_strtol/correct",
                    test_qemu_strtol_correct);
    g_test_add_func("/cutils/qemu_strtol/null",
                    test_qemu_strtol_null);
    g_test_add_func("/cutils/qemu_strtol/empty",
                    test_qemu_strtol_empty);
    g_test_add_func("/cutils/qemu_strtol/whitespace",
                    test_qemu_strtol_whitespace);
    g_test_add_func("/cutils/qemu_strtol/invalid",
                    test_qemu_strtol_invalid);
    g_test_add_func("/cutils/qemu_strtol/trailing",
                    test_qemu_strtol_trailing);
    g_test_add_func("/cutils/qemu_strtol/octal",
                    test_qemu_strtol_octal);
    g_test_add_func("/cutils/qemu_strtol/decimal",
                    test_qemu_strtol_decimal);
    g_test_add_func("/cutils/qemu_strtol/hex",
                    test_qemu_strtol_hex);
    g_test_add_func("/cutils/qemu_strtol/max",
                    test_qemu_strtol_max);
    g_test_add_func("/cutils/qemu_strtol/overflow",
                    test_qemu_strtol_overflow);
    g_test_add_func("/cutils/qemu_strtol/min",
                    test_qemu_strtol_min);
    g_test_add_func("/cutils/qemu_strtol/underflow",
                    test_qemu_strtol_underflow);
    g_test_add_func("/cutils/qemu_strtol/negative",
                    test_qemu_strtol_negative);
    g_test_add_func("/cutils/qemu_strtol/negzero",
                    test_qemu_strtol_negzero);
    g_test_add_func("/cutils/qemu_strtol_full/correct",
                    test_qemu_strtol_full_correct);
    g_test_add_func("/cutils/qemu_strtol_full/null",
                    test_qemu_strtol_full_null);
    g_test_add_func("/cutils/qemu_strtol_full/empty",
                    test_qemu_strtol_full_empty);
    g_test_add_func("/cutils/qemu_strtol_full/negative",
                    test_qemu_strtol_full_negative);
    g_test_add_func("/cutils/qemu_strtol_full/negzero",
                    test_qemu_strtol_full_negzero);
    g_test_add_func("/cutils/qemu_strtol_full/trailing",
                    test_qemu_strtol_full_trailing);
    g_test_add_func("/cutils/qemu_strtol_full/max",
                    test_qemu_strtol_full_max);
    g_test_add_func("/cutils/qemu_strtol_full/erange_junk",
                    test_qemu_strtol_full_erange_junk);

    /* qemu_strtoul() tests */
    g_test_add_func("/cutils/qemu_strtoul/correct",
                    test_qemu_strtoul_correct);
    g_test_add_func("/cutils/qemu_strtoul/null",
                    test_qemu_strtoul_null);
    g_test_add_func("/cutils/qemu_strtoul/empty",
                    test_qemu_strtoul_empty);
    g_test_add_func("/cutils/qemu_strtoul/whitespace",
                    test_qemu_strtoul_whitespace);
    g_test_add_func("/cutils/qemu_strtoul/invalid",
                    test_qemu_strtoul_invalid);
    g_test_add_func("/cutils/qemu_strtoul/trailing",
                    test_qemu_strtoul_trailing);
    g_test_add_func("/cutils/qemu_strtoul/octal",
                    test_qemu_strtoul_octal);
    g_test_add_func("/cutils/qemu_strtoul/decimal",
                    test_qemu_strtoul_decimal);
    g_test_add_func("/cutils/qemu_strtoul/hex",
                    test_qemu_strtoul_hex);
    g_test_add_func("/cutils/qemu_strtoul/wrap",
                    test_qemu_strtoul_wrap);
    g_test_add_func("/cutils/qemu_strtoul/max",
                    test_qemu_strtoul_max);
    g_test_add_func("/cutils/qemu_strtoul/overflow",
                    test_qemu_strtoul_overflow);
    g_test_add_func("/cutils/qemu_strtoul/underflow",
                    test_qemu_strtoul_underflow);
    g_test_add_func("/cutils/qemu_strtoul/negative",
                    test_qemu_strtoul_negative);
    g_test_add_func("/cutils/qemu_strtoul/negzero",
                    test_qemu_strtoul_negzero);
    g_test_add_func("/cutils/qemu_strtoul_full/correct",
                    test_qemu_strtoul_full_correct);
    g_test_add_func("/cutils/qemu_strtoul_full/null",
                    test_qemu_strtoul_full_null);
    g_test_add_func("/cutils/qemu_strtoul_full/empty",
                    test_qemu_strtoul_full_empty);
    g_test_add_func("/cutils/qemu_strtoul_full/negative",
                    test_qemu_strtoul_full_negative);
    g_test_add_func("/cutils/qemu_strtoul_full/negzero",
                    test_qemu_strtoul_full_negzero);
    g_test_add_func("/cutils/qemu_strtoul_full/trailing",
                    test_qemu_strtoul_full_trailing);
    g_test_add_func("/cutils/qemu_strtoul_full/max",
                    test_qemu_strtoul_full_max);
    g_test_add_func("/cutils/qemu_strtoul_full/erange_junk",
                    test_qemu_strtoul_full_erange_junk);

    /* qemu_strtoi64() tests */
    g_test_add_func("/cutils/qemu_strtoi64/correct",
                    test_qemu_strtoi64_correct);
    g_test_add_func("/cutils/qemu_strtoi64/null",
                    test_qemu_strtoi64_null);
    g_test_add_func("/cutils/qemu_strtoi64/empty",
                    test_qemu_strtoi64_empty);
    g_test_add_func("/cutils/qemu_strtoi64/whitespace",
                    test_qemu_strtoi64_whitespace);
    g_test_add_func("/cutils/qemu_strtoi64/invalid",
                    test_qemu_strtoi64_invalid);
    g_test_add_func("/cutils/qemu_strtoi64/trailing",
                    test_qemu_strtoi64_trailing);
    g_test_add_func("/cutils/qemu_strtoi64/octal",
                    test_qemu_strtoi64_octal);
    g_test_add_func("/cutils/qemu_strtoi64/decimal",
                    test_qemu_strtoi64_decimal);
    g_test_add_func("/cutils/qemu_strtoi64/hex",
                    test_qemu_strtoi64_hex);
    g_test_add_func("/cutils/qemu_strtoi64/max",
                    test_qemu_strtoi64_max);
    g_test_add_func("/cutils/qemu_strtoi64/overflow",
                    test_qemu_strtoi64_overflow);
    g_test_add_func("/cutils/qemu_strtoi64/min",
                    test_qemu_strtoi64_min);
    g_test_add_func("/cutils/qemu_strtoi64/underflow",
                    test_qemu_strtoi64_underflow);
    g_test_add_func("/cutils/qemu_strtoi64/negative",
                    test_qemu_strtoi64_negative);
    g_test_add_func("/cutils/qemu_strtoi64/negzero",
                    test_qemu_strtoi64_negzero);
    g_test_add_func("/cutils/qemu_strtoi64_full/correct",
                    test_qemu_strtoi64_full_correct);
    g_test_add_func("/cutils/qemu_strtoi64_full/null",
                    test_qemu_strtoi64_full_null);
    g_test_add_func("/cutils/qemu_strtoi64_full/empty",
                    test_qemu_strtoi64_full_empty);
    g_test_add_func("/cutils/qemu_strtoi64_full/negative",
                    test_qemu_strtoi64_full_negative);
    g_test_add_func("/cutils/qemu_strtoi64_full/negzero",
                    test_qemu_strtoi64_full_negzero);
    g_test_add_func("/cutils/qemu_strtoi64_full/trailing",
                    test_qemu_strtoi64_full_trailing);
    g_test_add_func("/cutils/qemu_strtoi64_full/max",
                    test_qemu_strtoi64_full_max);
    g_test_add_func("/cutils/qemu_strtoi64_full/erange_junk",
                    test_qemu_strtoi64_full_erange_junk);

    /* qemu_strtou64() tests */
    g_test_add_func("/cutils/qemu_strtou64/correct",
                    test_qemu_strtou64_correct);
    g_test_add_func("/cutils/qemu_strtou64/null",
                    test_qemu_strtou64_null);
    g_test_add_func("/cutils/qemu_strtou64/empty",
                    test_qemu_strtou64_empty);
    g_test_add_func("/cutils/qemu_strtou64/whitespace",
                    test_qemu_strtou64_whitespace);
    g_test_add_func("/cutils/qemu_strtou64/invalid",
                    test_qemu_strtou64_invalid);
    g_test_add_func("/cutils/qemu_strtou64/trailing",
                    test_qemu_strtou64_trailing);
    g_test_add_func("/cutils/qemu_strtou64/octal",
                    test_qemu_strtou64_octal);
    g_test_add_func("/cutils/qemu_strtou64/decimal",
                    test_qemu_strtou64_decimal);
    g_test_add_func("/cutils/qemu_strtou64/hex",
                    test_qemu_strtou64_hex);
    g_test_add_func("/cutils/qemu_strtou64/wrap",
                    test_qemu_strtou64_wrap);
    g_test_add_func("/cutils/qemu_strtou64/max",
                    test_qemu_strtou64_max);
    g_test_add_func("/cutils/qemu_strtou64/overflow",
                    test_qemu_strtou64_overflow);
    g_test_add_func("/cutils/qemu_strtou64/underflow",
                    test_qemu_strtou64_underflow);
    g_test_add_func("/cutils/qemu_strtou64/negative",
                    test_qemu_strtou64_negative);
    g_test_add_func("/cutils/qemu_strtou64/negzero",
                    test_qemu_strtou64_negzero);
    g_test_add_func("/cutils/qemu_strtou64_full/correct",
                    test_qemu_strtou64_full_correct);
    g_test_add_func("/cutils/qemu_strtou64_full/null",
                    test_qemu_strtou64_full_null);
    g_test_add_func("/cutils/qemu_strtou64_full/empty",
                    test_qemu_strtou64_full_empty);
    g_test_add_func("/cutils/qemu_strtou64_full/negative",
                    test_qemu_strtou64_full_negative);
    g_test_add_func("/cutils/qemu_strtou64_full/negzero",
                    test_qemu_strtou64_full_negzero);
    g_test_add_func("/cutils/qemu_strtou64_full/trailing",
                    test_qemu_strtou64_full_trailing);
    g_test_add_func("/cutils/qemu_strtou64_full/max",
                    test_qemu_strtou64_full_max);
    g_test_add_func("/cutils/qemu_strtou64_full/erange_junk",
                    test_qemu_strtou64_full_erange_junk);

    g_test_add_func("/cutils/strtosz/simple",
                    test_qemu_strtosz_simple);
    g_test_add_func("/cutils/strtosz/hex",
                    test_qemu_strtosz_hex);
    g_test_add_func("/cutils/strtosz/units",
                    test_qemu_strtosz_units);
    g_test_add_func("/cutils/strtosz/float",
                    test_qemu_strtosz_float);
    g_test_add_func("/cutils/strtosz/invalid",
                    test_qemu_strtosz_invalid);
    g_test_add_func("/cutils/strtosz/trailing",
                    test_qemu_strtosz_trailing);
    g_test_add_func("/cutils/strtosz/erange",
                    test_qemu_strtosz_erange);
    g_test_add_func("/cutils/strtosz/metric",
                    test_qemu_strtosz_metric);

    g_test_add_func("/cutils/size_to_str",
                    test_size_to_str);
    g_test_add_func("/cutils/freq_to_str",
                    test_freq_to_str);
    g_test_add_func("/cutils/iec_binary_prefix",
                    test_iec_binary_prefix);
    g_test_add_func("/cutils/si_prefix",
                    test_si_prefix);
    return g_test_run();
}
