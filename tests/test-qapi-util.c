/*
 * Unit tests for QAPI utility functions
 *
 * Copyright (C) 2017 Red Hat Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"

static void test_qapi_enum_parse(void)
{
    Error *err = NULL;
    int ret;

    ret = qapi_enum_parse(&QType_lookup, NULL, QTYPE_NONE, &error_abort);
    g_assert_cmpint(ret, ==, QTYPE_NONE);

    ret = qapi_enum_parse(&QType_lookup, "junk", -1, NULL);
    g_assert_cmpint(ret, ==, -1);

    ret = qapi_enum_parse(&QType_lookup, "junk", -1, &err);
    error_free_or_abort(&err);

    ret = qapi_enum_parse(&QType_lookup, "none", -1, &error_abort);
    g_assert_cmpint(ret, ==, QTYPE_NONE);

    ret = qapi_enum_parse(&QType_lookup, QType_str(QTYPE__MAX - 1),
                          QTYPE__MAX - 1, &error_abort);
    g_assert_cmpint(ret, ==, QTYPE__MAX - 1);
}

static void test_parse_qapi_name(void)
{
    int ret;

    /* Must start with a letter */
    ret = parse_qapi_name("a", true);
    g_assert(ret == 1);
    ret = parse_qapi_name("a$", false);
    g_assert(ret == 1);
    ret = parse_qapi_name("", false);
    g_assert(ret == -1);
    ret = parse_qapi_name("1", false);
    g_assert(ret == -1);

    /* Only letters, digits, hyphen, underscore */
    ret = parse_qapi_name("A-Za-z0-9_", true);
    g_assert(ret == 10);
    ret = parse_qapi_name("A-Za-z0-9_$", false);
    g_assert(ret == 10);
    ret = parse_qapi_name("A-Za-z0-9_$", true);
    g_assert(ret == -1);

    /* __RFQDN_ */
    ret = parse_qapi_name("__com.redhat_supports", true);
    g_assert(ret == 21);
    ret = parse_qapi_name("_com.example_", false);
    g_assert(ret == -1);
    ret = parse_qapi_name("__com.example", false);
    g_assert(ret == -1);
    ret = parse_qapi_name("__com.example_", false);
    g_assert(ret == -1);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/qapi/util/qapi_enum_parse", test_qapi_enum_parse);
    g_test_add_func("/qapi/util/parse_qapi_name", test_parse_qapi_name);
    g_test_run();
    return 0;
}
