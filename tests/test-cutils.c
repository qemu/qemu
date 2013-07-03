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

    return g_test_run();
}
