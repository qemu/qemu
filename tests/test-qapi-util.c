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
#include "qapi/util.h"
#include "test-qapi-types.h"

static void test_qapi_enum_parse(void)
{
    Error *err = NULL;
    int ret;

    ret = qapi_enum_parse(QType_lookup, NULL, QTYPE__MAX, QTYPE_NONE,
                          &error_abort);
    g_assert_cmpint(ret, ==, QTYPE_NONE);

    ret = qapi_enum_parse(QType_lookup, "junk", QTYPE__MAX, -1,
                          NULL);
    g_assert_cmpint(ret, ==, -1);

    ret = qapi_enum_parse(QType_lookup, "junk", QTYPE__MAX, -1,
                          &err);
    error_free_or_abort(&err);

    ret = qapi_enum_parse(QType_lookup, "none", QTYPE__MAX, -1,
                          &error_abort);
    g_assert_cmpint(ret, ==, QTYPE_NONE);

    ret = qapi_enum_parse(QType_lookup, QType_lookup[QTYPE__MAX - 1],
                          QTYPE__MAX, QTYPE__MAX - 1,
                          &error_abort);
    g_assert_cmpint(ret, ==, QTYPE__MAX - 1);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/qapi/util/qapi_enum_parse", test_qapi_enum_parse);
    g_test_run();
    return 0;
}
