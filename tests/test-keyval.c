/*
 * Unit tests for parsing of KEY=VALUE,... strings
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
#include "qemu/option.h"

static void test_keyval_parse(void)
{
    Error *err = NULL;
    QDict *qdict, *sub_qdict;
    char long_key[129];
    char *params;

    /* Nothing */
    qdict = keyval_parse("", NULL, &error_abort);
    g_assert_cmpuint(qdict_size(qdict), ==, 0);
    QDECREF(qdict);

    /* Empty key (qemu_opts_parse() accepts this) */
    qdict = keyval_parse("=val", NULL, &err);
    error_free_or_abort(&err);
    g_assert(!qdict);

    /* Empty key fragment */
    qdict = keyval_parse(".", NULL, &err);
    error_free_or_abort(&err);
    g_assert(!qdict);
    qdict = keyval_parse("key.", NULL, &err);
    error_free_or_abort(&err);
    g_assert(!qdict);

    /* Overlong key */
    memset(long_key, 'a', 127);
    long_key[127] = 'z';
    long_key[128] = 0;
    params = g_strdup_printf("k.%s=v", long_key);
    qdict = keyval_parse(params + 2, NULL, &err);
    error_free_or_abort(&err);
    g_assert(!qdict);

    /* Overlong key fragment */
    qdict = keyval_parse(params, NULL, &err);
    error_free_or_abort(&err);
    g_assert(!qdict);
    g_free(params);

    /* Long key (qemu_opts_parse() accepts and truncates silently) */
    params = g_strdup_printf("k.%s=v", long_key + 1);
    qdict = keyval_parse(params + 2, NULL, &error_abort);
    g_assert_cmpuint(qdict_size(qdict), ==, 1);
    g_assert_cmpstr(qdict_get_try_str(qdict, long_key + 1), ==, "v");
    QDECREF(qdict);

    /* Long key fragment */
    qdict = keyval_parse(params, NULL, &error_abort);
    g_assert_cmpuint(qdict_size(qdict), ==, 1);
    sub_qdict = qdict_get_qdict(qdict, "k");
    g_assert(sub_qdict);
    g_assert_cmpuint(qdict_size(sub_qdict), ==, 1);
    g_assert_cmpstr(qdict_get_try_str(sub_qdict, long_key + 1), ==, "v");
    QDECREF(qdict);
    g_free(params);

    /* Multiple keys, last one wins */
    qdict = keyval_parse("a=1,b=2,,x,a=3", NULL, &error_abort);
    g_assert_cmpuint(qdict_size(qdict), ==, 2);
    g_assert_cmpstr(qdict_get_try_str(qdict, "a"), ==, "3");
    g_assert_cmpstr(qdict_get_try_str(qdict, "b"), ==, "2,x");
    QDECREF(qdict);

    /* Even when it doesn't in qemu_opts_parse() */
    qdict = keyval_parse("id=foo,id=bar", NULL, &error_abort);
    g_assert_cmpuint(qdict_size(qdict), ==, 1);
    g_assert_cmpstr(qdict_get_try_str(qdict, "id"), ==, "bar");
    QDECREF(qdict);

    /* Dotted keys */
    qdict = keyval_parse("a.b.c=1,a.b.c=2,d=3", NULL, &error_abort);
    g_assert_cmpuint(qdict_size(qdict), ==, 2);
    sub_qdict = qdict_get_qdict(qdict, "a");
    g_assert(sub_qdict);
    g_assert_cmpuint(qdict_size(sub_qdict), ==, 1);
    sub_qdict = qdict_get_qdict(sub_qdict, "b");
    g_assert(sub_qdict);
    g_assert_cmpuint(qdict_size(sub_qdict), ==, 1);
    g_assert_cmpstr(qdict_get_try_str(sub_qdict, "c"), ==, "2");
    g_assert_cmpstr(qdict_get_try_str(qdict, "d"), ==, "3");
    QDECREF(qdict);

    /* Inconsistent dotted keys */
    qdict = keyval_parse("a.b=1,a=2", NULL, &err);
    error_free_or_abort(&err);
    g_assert(!qdict);
    qdict = keyval_parse("a.b=1,a.b.c=2", NULL, &err);
    error_free_or_abort(&err);
    g_assert(!qdict);

    /* Trailing comma is ignored */
    qdict = keyval_parse("x=y,", NULL, &error_abort);
    g_assert_cmpuint(qdict_size(qdict), ==, 1);
    g_assert_cmpstr(qdict_get_try_str(qdict, "x"), ==, "y");
    QDECREF(qdict);

    /* Except when it isn't */
    qdict = keyval_parse(",", NULL, &err);
    error_free_or_abort(&err);
    g_assert(!qdict);

    /* Value containing ,id= not misinterpreted as qemu_opts_parse() does */
    qdict = keyval_parse("x=,,id=bar", NULL, &error_abort);
    g_assert_cmpuint(qdict_size(qdict), ==, 1);
    g_assert_cmpstr(qdict_get_try_str(qdict, "x"), ==, ",id=bar");
    QDECREF(qdict);

    /* Anti-social ID is left to caller (qemu_opts_parse() rejects it) */
    qdict = keyval_parse("id=666", NULL, &error_abort);
    g_assert_cmpuint(qdict_size(qdict), ==, 1);
    g_assert_cmpstr(qdict_get_try_str(qdict, "id"), ==, "666");
    QDECREF(qdict);

    /* Implied value not supported (unlike qemu_opts_parse()) */
    qdict = keyval_parse("an,noaus,noaus=", NULL, &err);
    error_free_or_abort(&err);
    g_assert(!qdict);

    /* Implied value, key "no" (qemu_opts_parse(): negated empty key) */
    qdict = keyval_parse("no", NULL, &err);
    error_free_or_abort(&err);
    g_assert(!qdict);

    /* Implied key */
    qdict = keyval_parse("an,aus=off,noaus=", "implied", &error_abort);
    g_assert_cmpuint(qdict_size(qdict), ==, 3);
    g_assert_cmpstr(qdict_get_try_str(qdict, "implied"), ==, "an");
    g_assert_cmpstr(qdict_get_try_str(qdict, "aus"), ==, "off");
    g_assert_cmpstr(qdict_get_try_str(qdict, "noaus"), ==, "");
    QDECREF(qdict);

    /* Implied dotted key */
    qdict = keyval_parse("val", "eins.zwei", &error_abort);
    g_assert_cmpuint(qdict_size(qdict), ==, 1);
    sub_qdict = qdict_get_qdict(qdict, "eins");
    g_assert(sub_qdict);
    g_assert_cmpuint(qdict_size(sub_qdict), ==, 1);
    g_assert_cmpstr(qdict_get_try_str(sub_qdict, "zwei"), ==, "val");
    QDECREF(qdict);

    /* Implied key with empty value (qemu_opts_parse() accepts this) */
    qdict = keyval_parse(",", "implied", &err);
    error_free_or_abort(&err);
    g_assert(!qdict);

    /* Likewise (qemu_opts_parse(): implied key with comma value) */
    qdict = keyval_parse(",,,a=1", "implied", &err);
    error_free_or_abort(&err);
    g_assert(!qdict);

    /* Empty key is not an implied key */
    qdict = keyval_parse("=val", "implied", &err);
    error_free_or_abort(&err);
    g_assert(!qdict);
}

int main(int argc, char *argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/keyval/keyval_parse", test_keyval_parse);
    g_test_run();
    return 0;
}
