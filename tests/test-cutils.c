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

#include <glib.h>
#include <errno.h>
#include <string.h>

#include "qemu-common.h"


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

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0);
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

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0);
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

    g_assert_cmpint(err, ==, 0);
    g_assert_cmpint(res, ==, 0);
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

    g_assert_cmpint(err, ==, 0);
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

    return g_test_run();
}
