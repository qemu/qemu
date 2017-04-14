/*
 * cutils.c unit-tests
 *
 * Copyright (C) 2013 Red Hat Inc.
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

static void test_parse_uint_null(void)
{
    unsigned long long i = 999;
    char f = 'X';
    char *endptr = &f;
    int r;

    r = parse_uint(NULL, &i, &endptr, 0);

    g_assert_cmpint(r, ==, -EINVAL);
    g_assert_cmpint(i, ==, 0);
    g_assert(endptr == NULL);
}

static void test_parse_uint_empty(void)
{
    unsigned long long i = 999;
    char f = 'X';
    char *endptr = &f;
    const char *str = "";
    int r;

    r = parse_uint(str, &i, &endptr, 0);

    g_assert_cmpint(r, ==, -EINVAL);
    g_assert_cmpint(i, ==, 0);
    g_assert(endptr == str);
}

static void test_parse_uint_whitespace(void)
{
    unsigned long long i = 999;
    char f = 'X';
    char *endptr = &f;
    const char *str = "   \t   ";
    int r;

    r = parse_uint(str, &i, &endptr, 0);

    g_assert_cmpint(r, ==, -EINVAL);
    g_assert_cmpint(i, ==, 0);
    g_assert(endptr == str);
}


static void test_parse_uint_invalid(void)
{
    unsigned long long i = 999;
    char f = 'X';
    char *endptr = &f;
    const char *str = " \t xxx";
    int r;

    r = parse_uint(str, &i, &endptr, 0);

    g_assert_cmpint(r, ==, -EINVAL);
    g_assert_cmpint(i, ==, 0);
    g_assert(endptr == str);
}


static void test_parse_uint_trailing(void)
{
    unsigned long long i = 999;
    char f = 'X';
    char *endptr = &f;
    const char *str = "123xxx";
    int r;

    r = parse_uint(str, &i, &endptr, 0);

    g_assert_cmpint(r, ==, 0);
    g_assert_cmpint(i, ==, 123);
    g_assert(endptr == str + 3);
}

static void test_parse_uint_correct(void)
{
    unsigned long long i = 999;
    char f = 'X';
    char *endptr = &f;
    const char *str = "123";
    int r;

    r = parse_uint(str, &i, &endptr, 0);

    g_assert_cmpint(r, ==, 0);
    g_assert_cmpint(i, ==, 123);
    g_assert(endptr == str + strlen(str));
}

static void test_parse_uint_octal(void)
{
    unsigned long long i = 999;
    char f = 'X';
    char *endptr = &f;
    const char *str = "0123";
    int r;

    r = parse_uint(str, &i, &endptr, 0);

    g_assert_cmpint(r, ==, 0);
    g_assert_cmpint(i, ==, 0123);
    g_assert(endptr == str + strlen(str));
}

static void test_parse_uint_decimal(void)
{
    unsigned long long i = 999;
    char f = 'X';
    char *endptr = &f;
    const char *str = "0123";
    int r;

    r = parse_uint(str, &i, &endptr, 10);

    g_assert_cmpint(r, ==, 0);
    g_assert_cmpint(i, ==, 123);
    g_assert(endptr == str + strlen(str));
}


static void test_parse_uint_llong_max(void)
{
    unsigned long long i = 999;
    char f = 'X';
    char *endptr = &f;
    char *str = g_strdup_printf("%llu", (unsigned long long)LLONG_MAX + 1);
    int r;

    r = parse_uint(str, &i, &endptr, 0);

    g_assert_cmpint(r, ==, 0);
    g_assert_cmpint(i, ==, (unsigned long long)LLONG_MAX + 1);
    g_assert(endptr == str + strlen(str));

    g_free(str);
}

static void test_parse_uint_overflow(void)
{
    unsigned long long i = 999;
    char f = 'X';
    char *endptr = &f;
    const char *str = "99999999999999999999999999999999999999";
    int r;

    r = parse_uint(str, &i, &endptr, 0);

    g_assert_cmpint(r, ==, -ERANGE);
    g_assert_cmpint(i, ==, ULLONG_MAX);
    g_assert(endptr == str + strlen(str));
}

static void test_parse_uint_negative(void)
{
    unsigned long long i = 999;
    char f = 'X';
    char *endptr = &f;
    const char *str = " \t -321";
    int r;

    r = parse_uint(str, &i, &endptr, 0);

    g_assert_cmpint(r, ==, -ERANGE);
    g_assert_cmpint(i, ==, 0);
    g_assert(endptr == str + strlen(str));
}


static void test_parse_uint_full_trailing(void)
{
    unsigned long long i = 999;
    const char *str = "123xxx";
    int r;

    r = parse_uint_full(str, &i, 0);

    g_assert_cmpint(r, ==, -EINVAL);
    g_assert_cmpint(i, ==, 0);
}

static void test_parse_uint_full_correct(void)
{
    unsigned long long i = 999;
    const char *str = "123";
    int r;

    r = parse_uint_full(str, &i, 0);

    g_assert_cmpint(r, ==, 0);
    g_assert_cmpint(i, ==, 123);
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
    g_assert(endptr == str + 5);
}

static void test_qemu_strtol_null(void)
{
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err = qemu_strtol(NULL, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert(endptr == NULL);
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
    g_assert(endptr == str);
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
    g_assert(endptr == str);
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
    g_assert(endptr == str);
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
    g_assert(endptr == str + 3);
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
    g_assert(endptr == str + strlen(str));

    res = 999;
    endptr = &f;
    err = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0123);
    g_assert(endptr == str + strlen(str));
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
    g_assert(endptr == str + strlen(str));

    str = "123";
    res = 999;
    endptr = &f;
    err = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
    g_assert(endptr == str + strlen(str));
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
    g_assert(endptr == str + strlen(str));

    str = "0x123";
    res = 999;
    endptr = &f;
    err = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0x123);
    g_assert(endptr == str + strlen(str));
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
    g_assert(endptr == str + strlen(str));
    g_free(str);
}

static void test_qemu_strtol_overflow(void)
{
    const char *str = "99999999999999999999999999999999999999999999";
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, LONG_MAX);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtol_underflow(void)
{
    const char *str = "-99999999999999999999999999999999999999999999";
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err  = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, LONG_MIN);
    g_assert(endptr == str + strlen(str));
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
    g_assert(endptr == str + strlen(str));
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
    g_assert(endptr == NULL);
}

static void test_qemu_strtol_full_empty(void)
{
    const char *str = "";
    long res = 999L;
    int err;

    err =  qemu_strtol(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
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

static void test_qemu_strtol_full_trailing(void)
{
    const char *str = "123xxx";
    long res;
    int err;

    err = qemu_strtol(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
}

static void test_qemu_strtol_full_max(void)
{
    char *str = g_strdup_printf("%ld", LONG_MAX);
    long res;
    int err;

    err = qemu_strtol(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, LONG_MAX);
    g_free(str);
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
    g_assert(endptr == str + 5);
}

static void test_qemu_strtoul_null(void)
{
    char f = 'X';
    const char *endptr = &f;
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(NULL, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert(endptr == NULL);
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
    g_assert(endptr == str);
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
    g_assert(endptr == str);
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
    g_assert(endptr == str);
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
    g_assert(endptr == str + 3);
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
    g_assert(endptr == str + strlen(str));

    res = 999;
    endptr = &f;
    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0123);
    g_assert(endptr == str + strlen(str));
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
    g_assert(endptr == str + strlen(str));

    str = "123";
    res = 999;
    endptr = &f;
    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 123);
    g_assert(endptr == str + strlen(str));
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
    g_assert(endptr == str + strlen(str));

    str = "0x123";
    res = 999;
    endptr = &f;
    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 0x123);
    g_assert(endptr == str + strlen(str));
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
    g_assert(endptr == str + strlen(str));
    g_free(str);
}

static void test_qemu_strtoul_overflow(void)
{
    const char *str = "99999999999999999999999999999999999999999999";
    char f = 'X';
    const char *endptr = &f;
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmphex(res, ==, ULONG_MAX);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoul_underflow(void)
{
    const char *str = "-99999999999999999999999999999999999999999999";
    char f = 'X';
    const char *endptr = &f;
    unsigned long res = 999;
    int err;

    err  = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpuint(res, ==, -1ul);
    g_assert(endptr == str + strlen(str));
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
    g_assert(endptr == str + strlen(str));
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
}

static void test_qemu_strtoul_full_empty(void)
{
    const char *str = "";
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
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

static void test_qemu_strtoul_full_trailing(void)
{
    const char *str = "123xxx";
    unsigned long res;
    int err;

    err = qemu_strtoul(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
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
    g_assert(endptr == str + 5);
}

static void test_qemu_strtoi64_null(void)
{
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(NULL, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert(endptr == NULL);
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
    g_assert(endptr == str);
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
    g_assert(endptr == str);
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
    g_assert(endptr == str);
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
    g_assert(endptr == str + 3);
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
    g_assert(endptr == str + strlen(str));

    endptr = &f;
    res = 999;
    err = qemu_strtoi64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0123);
    g_assert(endptr == str + strlen(str));
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
    g_assert(endptr == str + strlen(str));

    str = "123";
    endptr = &f;
    res = 999;
    err = qemu_strtoi64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
    g_assert(endptr == str + strlen(str));
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
    g_assert(endptr == str + strlen(str));

    str = "0x123";
    endptr = &f;
    res = 999;
    err = qemu_strtoi64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0x123);
    g_assert(endptr == str + strlen(str));
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
    g_assert(endptr == str + strlen(str));
    g_free(str);
}

static void test_qemu_strtoi64_overflow(void)
{
    const char *str = "99999999999999999999999999999999999999999999";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, LLONG_MAX);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoi64_underflow(void)
{
    const char *str = "-99999999999999999999999999999999999999999999";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err  = qemu_strtoi64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, LLONG_MIN);
    g_assert(endptr == str + strlen(str));
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
    g_assert(endptr == str + strlen(str));
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
}

static void test_qemu_strtoi64_full_empty(void)
{
    const char *str = "";
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
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

static void test_qemu_strtoi64_full_trailing(void)
{
    const char *str = "123xxx";
    int64_t res = 999;
    int err;

    err = qemu_strtoi64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
}

static void test_qemu_strtoi64_full_max(void)
{

    char *str = g_strdup_printf("%lld", LLONG_MAX);
    int64_t res;
    int err;

    err = qemu_strtoi64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, LLONG_MAX);
    g_free(str);
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
    g_assert(endptr == str + 5);
}

static void test_qemu_strtou64_null(void)
{
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(NULL, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert(endptr == NULL);
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
    g_assert(endptr == str);
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
    g_assert(endptr == str);
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
    g_assert(endptr == str);
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
    g_assert(endptr == str + 3);
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
    g_assert(endptr == str + strlen(str));

    endptr = &f;
    res = 999;
    err = qemu_strtou64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 0123);
    g_assert(endptr == str + strlen(str));
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
    g_assert(endptr == str + strlen(str));

    str = "123";
    endptr = &f;
    res = 999;
    err = qemu_strtou64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpuint(res, ==, 123);
    g_assert(endptr == str + strlen(str));
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
    g_assert(endptr == str + strlen(str));

    str = "0x123";
    endptr = &f;
    res = 999;
    err = qemu_strtou64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmphex(res, ==, 0x123);
    g_assert(endptr == str + strlen(str));
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
    g_assert(endptr == str + strlen(str));
    g_free(str);
}

static void test_qemu_strtou64_overflow(void)
{
    const char *str = "99999999999999999999999999999999999999999999";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmphex(res, ==, ULLONG_MAX);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtou64_underflow(void)
{
    const char *str = "-99999999999999999999999999999999999999999999";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err  = qemu_strtou64(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmphex(res, ==, -1ull);
    g_assert(endptr == str + strlen(str));
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
    g_assert(endptr == str + strlen(str));
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
}

static void test_qemu_strtou64_full_empty(void)
{
    const char *str = "";
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
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

static void test_qemu_strtou64_full_trailing(void)
{
    const char *str = "18446744073709551614xxxxxx";
    uint64_t res = 999;
    int err;

    err = qemu_strtou64(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
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

static void test_qemu_strtosz_simple(void)
{
    const char *str;
    char *endptr = NULL;
    int err;
    uint64_t res = 0xbaadf00d;

    str = "0";
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0);
    g_assert(endptr == str + 1);

    str = "12345";
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 12345);
    g_assert(endptr == str + 5);

    err = qemu_strtosz(str, NULL, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 12345);

    /* Note: precision is 53 bits since we're parsing with strtod() */

    str = "9007199254740991"; /* 2^53-1 */
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0x1fffffffffffff);
    g_assert(endptr == str + 16);

    str = "9007199254740992"; /* 2^53 */
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0x20000000000000);
    g_assert(endptr == str + 16);

    str = "9007199254740993"; /* 2^53+1 */
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0x20000000000000); /* rounded to 53 bits */
    g_assert(endptr == str + 16);

    str = "18446744073709549568"; /* 0xfffffffffffff800 (53 msbs set) */
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0xfffffffffffff800);
    g_assert(endptr == str + 20);

    str = "18446744073709550591"; /* 0xfffffffffffffbff */
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0xfffffffffffff800); /* rounded to 53 bits */
    g_assert(endptr == str + 20);

    /* 0x7ffffffffffffe00..0x7fffffffffffffff get rounded to
     * 0x8000000000000000, thus -ERANGE; see test_qemu_strtosz_erange() */
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
    char *endptr = NULL;
    uint64_t res = 0xbaadf00d;

    /* default is M */
    err = qemu_strtosz_MiB(none, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, M_BYTE);
    g_assert(endptr == none + 1);

    err = qemu_strtosz(b, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 1);
    g_assert(endptr == b + 2);

    err = qemu_strtosz(k, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, K_BYTE);
    g_assert(endptr == k + 2);

    err = qemu_strtosz(m, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, M_BYTE);
    g_assert(endptr == m + 2);

    err = qemu_strtosz(g, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, G_BYTE);
    g_assert(endptr == g + 2);

    err = qemu_strtosz(t, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, T_BYTE);
    g_assert(endptr == t + 2);

    err = qemu_strtosz(p, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, P_BYTE);
    g_assert(endptr == p + 2);

    err = qemu_strtosz(e, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, E_BYTE);
    g_assert(endptr == e + 2);
}

static void test_qemu_strtosz_float(void)
{
    const char *str = "12.345M";
    int err;
    char *endptr = NULL;
    uint64_t res = 0xbaadf00d;

    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 12.345 * M_BYTE);
    g_assert(endptr == str + 7);
}

static void test_qemu_strtosz_invalid(void)
{
    const char *str;
    char *endptr = NULL;
    int err;
    uint64_t res = 0xbaadf00d;

    str = "";
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert(endptr == str);

    str = " \t ";
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert(endptr == str);

    str = "crap";
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -EINVAL);
    g_assert(endptr == str);
}

static void test_qemu_strtosz_trailing(void)
{
    const char *str;
    char *endptr = NULL;
    int err;
    uint64_t res = 0xbaadf00d;

    str = "123xxx";
    err = qemu_strtosz_MiB(str, &endptr, &res);
    g_assert_cmpint(res, ==, 123 * M_BYTE);
    g_assert(endptr == str + 3);

    err = qemu_strtosz(str, NULL, &res);
    g_assert_cmpint(err, ==, -EINVAL);

    str = "1kiB";
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 1024);
    g_assert(endptr == str + 2);

    err = qemu_strtosz(str, NULL, &res);
    g_assert_cmpint(err, ==, -EINVAL);
}

static void test_qemu_strtosz_erange(void)
{
    const char *str;
    char *endptr = NULL;
    int err;
    uint64_t res = 0xbaadf00d;

    str = "-1";
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert(endptr == str + 2);

    str = "18446744073709550592"; /* 0xfffffffffffffc00 */
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert(endptr == str + 20);

    str = "18446744073709551615"; /* 2^64-1 */
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert(endptr == str + 20);

    str = "18446744073709551616"; /* 2^64 */
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert(endptr == str + 20);

    str = "20E";
    err = qemu_strtosz(str, &endptr, &res);
    g_assert_cmpint(err, ==, -ERANGE);
    g_assert(endptr == str + 3);
}

static void test_qemu_strtosz_metric(void)
{
    const char *str = "12345k";
    int err;
    char *endptr = NULL;
    uint64_t res = 0xbaadf00d;

    err = qemu_strtosz_metric(str, &endptr, &res);
    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 12345000);
    g_assert(endptr == str + 6);
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
    g_test_add_func("/cutils/parse_uint/overflow", test_parse_uint_overflow);
    g_test_add_func("/cutils/parse_uint/negative", test_parse_uint_negative);
    g_test_add_func("/cutils/parse_uint_full/trailing",
                    test_parse_uint_full_trailing);
    g_test_add_func("/cutils/parse_uint_full/correct",
                    test_parse_uint_full_correct);

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
    g_test_add_func("/cutils/qemu_strtol/underflow",
                    test_qemu_strtol_underflow);
    g_test_add_func("/cutils/qemu_strtol/negative",
                    test_qemu_strtol_negative);
    g_test_add_func("/cutils/qemu_strtol_full/correct",
                    test_qemu_strtol_full_correct);
    g_test_add_func("/cutils/qemu_strtol_full/null",
                    test_qemu_strtol_full_null);
    g_test_add_func("/cutils/qemu_strtol_full/empty",
                    test_qemu_strtol_full_empty);
    g_test_add_func("/cutils/qemu_strtol_full/negative",
                    test_qemu_strtol_full_negative);
    g_test_add_func("/cutils/qemu_strtol_full/trailing",
                    test_qemu_strtol_full_trailing);
    g_test_add_func("/cutils/qemu_strtol_full/max",
                    test_qemu_strtol_full_max);

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
    g_test_add_func("/cutils/qemu_strtoul/max",
                    test_qemu_strtoul_max);
    g_test_add_func("/cutils/qemu_strtoul/overflow",
                    test_qemu_strtoul_overflow);
    g_test_add_func("/cutils/qemu_strtoul/underflow",
                    test_qemu_strtoul_underflow);
    g_test_add_func("/cutils/qemu_strtoul/negative",
                    test_qemu_strtoul_negative);
    g_test_add_func("/cutils/qemu_strtoul_full/correct",
                    test_qemu_strtoul_full_correct);
    g_test_add_func("/cutils/qemu_strtoul_full/null",
                    test_qemu_strtoul_full_null);
    g_test_add_func("/cutils/qemu_strtoul_full/empty",
                    test_qemu_strtoul_full_empty);
    g_test_add_func("/cutils/qemu_strtoul_full/negative",
                    test_qemu_strtoul_full_negative);
    g_test_add_func("/cutils/qemu_strtoul_full/trailing",
                    test_qemu_strtoul_full_trailing);
    g_test_add_func("/cutils/qemu_strtoul_full/max",
                    test_qemu_strtoul_full_max);

    /* qemu_strtoi64() tests */
    g_test_add_func("/cutils/qemu_strtoi64/correct",
                    test_qemu_strtoi64_correct);
    g_test_add_func("/cutils/qemu_strtoi64/null",
                    test_qemu_strtoi64_null);
    g_test_add_func("/cutils/qemu_strtoi64/empty",
                    test_qemu_strtoi64_empty);
    g_test_add_func("/cutils/qemu_strtoi64/whitespace",
                    test_qemu_strtoi64_whitespace);
    g_test_add_func("/cutils/qemu_strtoi64/invalid"
                    ,
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
    g_test_add_func("/cutils/qemu_strtoi64/underflow",
                    test_qemu_strtoi64_underflow);
    g_test_add_func("/cutils/qemu_strtoi64/negative",
                    test_qemu_strtoi64_negative);
    g_test_add_func("/cutils/qemu_strtoi64_full/correct",
                    test_qemu_strtoi64_full_correct);
    g_test_add_func("/cutils/qemu_strtoi64_full/null",
                    test_qemu_strtoi64_full_null);
    g_test_add_func("/cutils/qemu_strtoi64_full/empty",
                    test_qemu_strtoi64_full_empty);
    g_test_add_func("/cutils/qemu_strtoi64_full/negative",
                    test_qemu_strtoi64_full_negative);
    g_test_add_func("/cutils/qemu_strtoi64_full/trailing",
                    test_qemu_strtoi64_full_trailing);
    g_test_add_func("/cutils/qemu_strtoi64_full/max",
                    test_qemu_strtoi64_full_max);

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
    g_test_add_func("/cutils/qemu_strtou64/max",
                    test_qemu_strtou64_max);
    g_test_add_func("/cutils/qemu_strtou64/overflow",
                    test_qemu_strtou64_overflow);
    g_test_add_func("/cutils/qemu_strtou64/underflow",
                    test_qemu_strtou64_underflow);
    g_test_add_func("/cutils/qemu_strtou64/negative",
                    test_qemu_strtou64_negative);
    g_test_add_func("/cutils/qemu_strtou64_full/correct",
                    test_qemu_strtou64_full_correct);
    g_test_add_func("/cutils/qemu_strtou64_full/null",
                    test_qemu_strtou64_full_null);
    g_test_add_func("/cutils/qemu_strtou64_full/empty",
                    test_qemu_strtou64_full_empty);
    g_test_add_func("/cutils/qemu_strtou64_full/negative",
                    test_qemu_strtou64_full_negative);
    g_test_add_func("/cutils/qemu_strtou64_full/trailing",
                    test_qemu_strtou64_full_trailing);
    g_test_add_func("/cutils/qemu_strtou64_full/max",
                    test_qemu_strtou64_full_max);

    g_test_add_func("/cutils/strtosz/simple",
                    test_qemu_strtosz_simple);
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

    return g_test_run();
}
