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
    const char *str = g_strdup_printf("%ld", LONG_MAX);
    char f = 'X';
    const char *endptr = &f;
    long res = 999;
    int err;

    err = qemu_strtol(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, LONG_MAX);
    g_assert(endptr == str + strlen(str));
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
    const char *str = g_strdup_printf("%ld", LONG_MAX);
    long res;
    int err;

    err = qemu_strtol(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, LONG_MAX);
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
    g_assert_cmpint(res, ==, 12345);
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
    g_assert_cmpint(res, ==, 123);
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
    g_assert_cmpint(res, ==, 0123);
    g_assert(endptr == str + strlen(str));

    res = 999;
    endptr = &f;
    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0123);
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
    g_assert_cmpint(res, ==, 123);
    g_assert(endptr == str + strlen(str));

    str = "123";
    res = 999;
    endptr = &f;
    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
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
    g_assert_cmpint(res, ==, 0x123);
    g_assert(endptr == str + strlen(str));

    str = "0x123";
    res = 999;
    endptr = &f;
    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0x123);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoul_max(void)
{
    const char *str = g_strdup_printf("%lu", ULONG_MAX);
    char f = 'X';
    const char *endptr = &f;
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, ULONG_MAX);
    g_assert(endptr == str + strlen(str));
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
    g_assert_cmpint(res, ==, ULONG_MAX);
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
    g_assert_cmpint(res, ==, -1ul);
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
    g_assert_cmpint(res, ==, -321ul);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoul_full_correct(void)
{
    const char *str = "123";
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
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
    g_assert_cmpint(res, ==, -321ul);
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
    const char *str = g_strdup_printf("%lu", ULONG_MAX);
    unsigned long res = 999;
    int err;

    err = qemu_strtoul(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, ULONG_MAX);
}

static void test_qemu_strtoll_correct(void)
{
    const char *str = "12345 foo";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoll(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 12345);
    g_assert(endptr == str + 5);
}

static void test_qemu_strtoll_null(void)
{
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoll(NULL, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert(endptr == NULL);
}

static void test_qemu_strtoll_empty(void)
{
    const char *str = "";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoll(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
}

static void test_qemu_strtoll_whitespace(void)
{
    const char *str = "  \t  ";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoll(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
}

static void test_qemu_strtoll_invalid(void)
{
    const char *str = "   xxxx  \t abc";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoll(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
}

static void test_qemu_strtoll_trailing(void)
{
    const char *str = "123xxx";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoll(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
    g_assert(endptr == str + 3);
}

static void test_qemu_strtoll_octal(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoll(str, &endptr, 8, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0123);
    g_assert(endptr == str + strlen(str));

    endptr = &f;
    res = 999;
    err = qemu_strtoll(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0123);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoll_decimal(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoll(str, &endptr, 10, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
    g_assert(endptr == str + strlen(str));

    str = "123";
    endptr = &f;
    res = 999;
    err = qemu_strtoll(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoll_hex(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoll(str, &endptr, 16, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0x123);
    g_assert(endptr == str + strlen(str));

    str = "0x123";
    endptr = &f;
    res = 999;
    err = qemu_strtoll(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0x123);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoll_max(void)
{
    const char *str = g_strdup_printf("%lld", LLONG_MAX);
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoll(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, LLONG_MAX);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoll_overflow(void)
{
    const char *str = "99999999999999999999999999999999999999999999";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoll(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, LLONG_MAX);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoll_underflow(void)
{
    const char *str = "-99999999999999999999999999999999999999999999";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err  = qemu_strtoll(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, LLONG_MIN);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoll_negative(void)
{
    const char *str = "  \t -321";
    char f = 'X';
    const char *endptr = &f;
    int64_t res = 999;
    int err;

    err = qemu_strtoll(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, -321);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoll_full_correct(void)
{
    const char *str = "123";
    int64_t res = 999;
    int err;

    err = qemu_strtoll(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
}

static void test_qemu_strtoll_full_null(void)
{
    int64_t res = 999;
    int err;

    err = qemu_strtoll(NULL, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
}

static void test_qemu_strtoll_full_empty(void)
{
    const char *str = "";
    int64_t res = 999;
    int err;

    err = qemu_strtoll(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
}

static void test_qemu_strtoll_full_negative(void)
{
    const char *str = " \t -321";
    int64_t res = 999;
    int err;

    err = qemu_strtoll(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, -321);
}

static void test_qemu_strtoll_full_trailing(void)
{
    const char *str = "123xxx";
    int64_t res = 999;
    int err;

    err = qemu_strtoll(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
}

static void test_qemu_strtoll_full_max(void)
{

    const char *str = g_strdup_printf("%lld", LLONG_MAX);
    int64_t res;
    int err;

    err = qemu_strtoll(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, LLONG_MAX);
}

static void test_qemu_strtoull_correct(void)
{
    const char *str = "12345 foo";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 12345);
    g_assert(endptr == str + 5);
}

static void test_qemu_strtoull_null(void)
{
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(NULL, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
    g_assert(endptr == NULL);
}

static void test_qemu_strtoull_empty(void)
{
    const char *str = "";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
}

static void test_qemu_strtoull_whitespace(void)
{
    const char *str = "  \t  ";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
}

static void test_qemu_strtoull_invalid(void)
{
    const char *str = "   xxxx  \t abc";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
}

static void test_qemu_strtoull_trailing(void)
{
    const char *str = "123xxx";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
    g_assert(endptr == str + 3);
}

static void test_qemu_strtoull_octal(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(str, &endptr, 8, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0123);
    g_assert(endptr == str + strlen(str));

    endptr = &f;
    res = 999;
    err = qemu_strtoull(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0123);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoull_decimal(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(str, &endptr, 10, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
    g_assert(endptr == str + strlen(str));

    str = "123";
    endptr = &f;
    res = 999;
    err = qemu_strtoull(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 123);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoull_hex(void)
{
    const char *str = "0123";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(str, &endptr, 16, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0x123);
    g_assert(endptr == str + strlen(str));

    str = "0x123";
    endptr = &f;
    res = 999;
    err = qemu_strtoull(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0x123);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoull_max(void)
{
    const char *str = g_strdup_printf("%llu", ULLONG_MAX);
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, ULLONG_MAX);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoull_overflow(void)
{
    const char *str = "99999999999999999999999999999999999999999999";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, ULLONG_MAX);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoull_underflow(void)
{
    const char *str = "-99999999999999999999999999999999999999999999";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err  = qemu_strtoull(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, -ERANGE);
    g_assert_cmpint(res, ==, -1);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoull_negative(void)
{
    const char *str = "  \t -321";
    char f = 'X';
    const char *endptr = &f;
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(str, &endptr, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, -321);
    g_assert(endptr == str + strlen(str));
}

static void test_qemu_strtoull_full_correct(void)
{
    const char *str = "18446744073709551614";
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 18446744073709551614LLU);
}

static void test_qemu_strtoull_full_null(void)
{
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(NULL, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
}

static void test_qemu_strtoull_full_empty(void)
{
    const char *str = "";
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
}

static void test_qemu_strtoull_full_negative(void)
{
    const char *str = " \t -321";
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 18446744073709551295LLU);
}

static void test_qemu_strtoull_full_trailing(void)
{
    const char *str = "18446744073709551614xxxxxx";
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, -EINVAL);
}

static void test_qemu_strtoull_full_max(void)
{
    const char *str = g_strdup_printf("%lld", ULLONG_MAX);
    uint64_t res = 999;
    int err;

    err = qemu_strtoull(str, NULL, 0, &res);

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, ULLONG_MAX);
}

static void test_qemu_strtosz_simple(void)
{
    const char *str = "12345M";
    char *endptr = NULL;
    int64_t res;

    res = qemu_strtosz(str, &endptr);
    g_assert_cmpint(res, ==, 12345 * M_BYTE);
    g_assert(endptr == str + 6);

    res = qemu_strtosz(str, NULL);
    g_assert_cmpint(res, ==, 12345 * M_BYTE);
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
    int64_t res;

    /* default is M */
    res = qemu_strtosz(none, NULL);
    g_assert_cmpint(res, ==, M_BYTE);

    res = qemu_strtosz(b, NULL);
    g_assert_cmpint(res, ==, 1);

    res = qemu_strtosz(k, NULL);
    g_assert_cmpint(res, ==, K_BYTE);

    res = qemu_strtosz(m, NULL);
    g_assert_cmpint(res, ==, M_BYTE);

    res = qemu_strtosz(g, NULL);
    g_assert_cmpint(res, ==, G_BYTE);

    res = qemu_strtosz(t, NULL);
    g_assert_cmpint(res, ==, T_BYTE);

    res = qemu_strtosz(p, NULL);
    g_assert_cmpint(res, ==, P_BYTE);

    res = qemu_strtosz(e, NULL);
    g_assert_cmpint(res, ==, E_BYTE);
}

static void test_qemu_strtosz_float(void)
{
    const char *str = "12.345M";
    int64_t res;

    res = qemu_strtosz(str, NULL);
    g_assert_cmpint(res, ==, 12.345 * M_BYTE);
}

static void test_qemu_strtosz_erange(void)
{
    const char *str = "10E";
    int64_t res;

    res = qemu_strtosz(str, NULL);
    g_assert_cmpint(res, ==, -ERANGE);
}

static void test_qemu_strtosz_suffix_unit(void)
{
    const char *str = "12345";
    int64_t res;

    res = qemu_strtosz_suffix_unit(str, NULL,
                                   QEMU_STRTOSZ_DEFSUFFIX_KB, 1000);
    g_assert_cmpint(res, ==, 12345000);
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
    g_test_add_func("/cutils/qemu_strtol/correct", test_qemu_strtol_correct);
    g_test_add_func("/cutils/qemu_strtol/null", test_qemu_strtol_null);
    g_test_add_func("/cutils/qemu_strtol/empty", test_qemu_strtol_empty);
    g_test_add_func("/cutils/qemu_strtol/whitespace",
                    test_qemu_strtol_whitespace);
    g_test_add_func("/cutils/qemu_strtol/invalid", test_qemu_strtol_invalid);
    g_test_add_func("/cutils/qemu_strtol/trailing", test_qemu_strtol_trailing);
    g_test_add_func("/cutils/qemu_strtol/octal", test_qemu_strtol_octal);
    g_test_add_func("/cutils/qemu_strtol/decimal", test_qemu_strtol_decimal);
    g_test_add_func("/cutils/qemu_strtol/hex", test_qemu_strtol_hex);
    g_test_add_func("/cutils/qemu_strtol/max", test_qemu_strtol_max);
    g_test_add_func("/cutils/qemu_strtol/overflow", test_qemu_strtol_overflow);
    g_test_add_func("/cutils/qemu_strtol/underflow",
                    test_qemu_strtol_underflow);
    g_test_add_func("/cutils/qemu_strtol/negative", test_qemu_strtol_negative);
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
    g_test_add_func("/cutils/qemu_strtoul/correct", test_qemu_strtoul_correct);
    g_test_add_func("/cutils/qemu_strtoul/null", test_qemu_strtoul_null);
    g_test_add_func("/cutils/qemu_strtoul/empty", test_qemu_strtoul_empty);
    g_test_add_func("/cutils/qemu_strtoul/whitespace",
                    test_qemu_strtoul_whitespace);
    g_test_add_func("/cutils/qemu_strtoul/invalid", test_qemu_strtoul_invalid);
    g_test_add_func("/cutils/qemu_strtoul/trailing",
                    test_qemu_strtoul_trailing);
    g_test_add_func("/cutils/qemu_strtoul/octal", test_qemu_strtoul_octal);
    g_test_add_func("/cutils/qemu_strtoul/decimal", test_qemu_strtoul_decimal);
    g_test_add_func("/cutils/qemu_strtoul/hex", test_qemu_strtoul_hex);
    g_test_add_func("/cutils/qemu_strtoul/max", test_qemu_strtoul_max);
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

    /* qemu_strtoll() tests */
    g_test_add_func("/cutils/qemu_strtoll/correct", test_qemu_strtoll_correct);
    g_test_add_func("/cutils/qemu_strtoll/null", test_qemu_strtoll_null);
    g_test_add_func("/cutils/qemu_strtoll/empty", test_qemu_strtoll_empty);
    g_test_add_func("/cutils/qemu_strtoll/whitespace",
                    test_qemu_strtoll_whitespace);
    g_test_add_func("/cutils/qemu_strtoll/invalid", test_qemu_strtoll_invalid);
    g_test_add_func("/cutils/qemu_strtoll/trailing",
                    test_qemu_strtoll_trailing);
    g_test_add_func("/cutils/qemu_strtoll/octal", test_qemu_strtoll_octal);
    g_test_add_func("/cutils/qemu_strtoll/decimal", test_qemu_strtoll_decimal);
    g_test_add_func("/cutils/qemu_strtoll/hex", test_qemu_strtoll_hex);
    g_test_add_func("/cutils/qemu_strtoll/max", test_qemu_strtoll_max);
    g_test_add_func("/cutils/qemu_strtoll/overflow",
                    test_qemu_strtoll_overflow);
    g_test_add_func("/cutils/qemu_strtoll/underflow",
                    test_qemu_strtoll_underflow);
    g_test_add_func("/cutils/qemu_strtoll/negative",
                    test_qemu_strtoll_negative);
    g_test_add_func("/cutils/qemu_strtoll_full/correct",
                    test_qemu_strtoll_full_correct);
    g_test_add_func("/cutils/qemu_strtoll_full/null",
                    test_qemu_strtoll_full_null);
    g_test_add_func("/cutils/qemu_strtoll_full/empty",
                    test_qemu_strtoll_full_empty);
    g_test_add_func("/cutils/qemu_strtoll_full/negative",
                    test_qemu_strtoll_full_negative);
    g_test_add_func("/cutils/qemu_strtoll_full/trailing",
                    test_qemu_strtoll_full_trailing);
    g_test_add_func("/cutils/qemu_strtoll_full/max",
                    test_qemu_strtoll_full_max);

    /* qemu_strtoull() tests */
    g_test_add_func("/cutils/qemu_strtoull/correct",
                    test_qemu_strtoull_correct);
    g_test_add_func("/cutils/qemu_strtoull/null",
                    test_qemu_strtoull_null);
    g_test_add_func("/cutils/qemu_strtoull/empty", test_qemu_strtoull_empty);
    g_test_add_func("/cutils/qemu_strtoull/whitespace",
                    test_qemu_strtoull_whitespace);
    g_test_add_func("/cutils/qemu_strtoull/invalid",
                    test_qemu_strtoull_invalid);
    g_test_add_func("/cutils/qemu_strtoull/trailing",
                    test_qemu_strtoull_trailing);
    g_test_add_func("/cutils/qemu_strtoull/octal", test_qemu_strtoull_octal);
    g_test_add_func("/cutils/qemu_strtoull/decimal",
                    test_qemu_strtoull_decimal);
    g_test_add_func("/cutils/qemu_strtoull/hex", test_qemu_strtoull_hex);
    g_test_add_func("/cutils/qemu_strtoull/max", test_qemu_strtoull_max);
    g_test_add_func("/cutils/qemu_strtoull/overflow",
                    test_qemu_strtoull_overflow);
    g_test_add_func("/cutils/qemu_strtoull/underflow",
                    test_qemu_strtoull_underflow);
    g_test_add_func("/cutils/qemu_strtoull/negative",
                    test_qemu_strtoull_negative);
    g_test_add_func("/cutils/qemu_strtoull_full/correct",
                    test_qemu_strtoull_full_correct);
    g_test_add_func("/cutils/qemu_strtoull_full/null",
                    test_qemu_strtoull_full_null);
    g_test_add_func("/cutils/qemu_strtoull_full/empty",
                    test_qemu_strtoull_full_empty);
    g_test_add_func("/cutils/qemu_strtoull_full/negative",
                    test_qemu_strtoull_full_negative);
    g_test_add_func("/cutils/qemu_strtoull_full/trailing",
                    test_qemu_strtoull_full_trailing);
    g_test_add_func("/cutils/qemu_strtoull_full/max",
                    test_qemu_strtoull_full_max);

    g_test_add_func("/cutils/strtosz/simple",
                    test_qemu_strtosz_simple);
    g_test_add_func("/cutils/strtosz/units",
                    test_qemu_strtosz_units);
    g_test_add_func("/cutils/strtosz/float",
                    test_qemu_strtosz_float);
    g_test_add_func("/cutils/strtosz/erange",
                    test_qemu_strtosz_erange);
    g_test_add_func("/cutils/strtosz/suffix-unit",
                    test_qemu_strtosz_suffix_unit);

    return g_test_run();
}
