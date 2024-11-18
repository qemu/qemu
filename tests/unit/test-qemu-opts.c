/*
 * QemuOpts unit-tests.
 *
 * Copyright (C) 2014 Leandro Dorileo <l@dorileo.org>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/option.h"
#include "qemu/option_int.h"
#include "qapi/error.h"
#include "qobject/qdict.h"
#include "qobject/qstring.h"
#include "qemu/config-file.h"


static QemuOptsList opts_list_01 = {
    .name = "opts_list_01",
    .head = QTAILQ_HEAD_INITIALIZER(opts_list_01.head),
    .desc = {
        {
            .name = "str1",
            .type = QEMU_OPT_STRING,
            .help = "Help texts are preserved in qemu_opts_append",
            .def_value_str = "default",
        },{
            .name = "str2",
            .type = QEMU_OPT_STRING,
        },{
            .name = "str3",
            .type = QEMU_OPT_STRING,
        },{
            .name = "number1",
            .type = QEMU_OPT_NUMBER,
            .help = "Having help texts only for some options is okay",
        },{
            .name = "number2",
            .type = QEMU_OPT_NUMBER,
        },
        { /* end of list */ }
    },
};

static QemuOptsList opts_list_02 = {
    .name = "opts_list_02",
    .head = QTAILQ_HEAD_INITIALIZER(opts_list_02.head),
    .desc = {
        {
            .name = "str1",
            .type = QEMU_OPT_STRING,
        },{
            .name = "str2",
            .type = QEMU_OPT_STRING,
        },{
            .name = "bool1",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "bool2",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "size1",
            .type = QEMU_OPT_SIZE,
        },{
            .name = "size2",
            .type = QEMU_OPT_SIZE,
        },{
            .name = "size3",
            .type = QEMU_OPT_SIZE,
        },
        { /* end of list */ }
    },
};

static QemuOptsList opts_list_03 = {
    .name = "opts_list_03",
    .implied_opt_name = "implied",
    .head = QTAILQ_HEAD_INITIALIZER(opts_list_03.head),
    .desc = {
        /* no elements => accept any params */
        { /* end of list */ }
    },
};

static QemuOptsList opts_list_04 = {
    .name = "opts_list_04",
    .head = QTAILQ_HEAD_INITIALIZER(opts_list_04.head),
    .merge_lists = true,
    .desc = {
        {
            .name = "str3",
            .type = QEMU_OPT_STRING,
        },
        { /* end of list */ }
    },
};

static void register_opts(void)
{
    qemu_add_opts(&opts_list_01);
    qemu_add_opts(&opts_list_02);
    qemu_add_opts(&opts_list_03);
    qemu_add_opts(&opts_list_04);
}

static void test_find_unknown_opts(void)
{
    QemuOptsList *list;
    Error *err = NULL;

    /* should not return anything, we don't have an "unknown" option */
    list = qemu_find_opts_err("unknown", &err);
    g_assert(list == NULL);
    error_free_or_abort(&err);
}

static void test_qemu_find_opts(void)
{
    QemuOptsList *list;

    /* we have an "opts_list_01" option, should return it */
    list = qemu_find_opts("opts_list_01");
    g_assert(list != NULL);
    g_assert_cmpstr(list->name, ==, "opts_list_01");
}

static void test_qemu_opts_create(void)
{
    QemuOptsList *list;
    QemuOpts *opts;

    list = qemu_find_opts("opts_list_01");
    g_assert(list != NULL);
    g_assert(QTAILQ_EMPTY(&list->head));
    g_assert_cmpstr(list->name, ==, "opts_list_01");

    /* should not find anything at this point */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts == NULL);

    /* create the opts */
    opts = qemu_opts_create(list, NULL, 0, &error_abort);
    g_assert(opts != NULL);
    g_assert(!QTAILQ_EMPTY(&list->head));

    /* now we've create the opts, must find it */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts != NULL);

    qemu_opts_del(opts);

    /* should not find anything at this point */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts == NULL);
}

static void test_qemu_opt_get(void)
{
    QemuOptsList *list;
    QemuOpts *opts;
    const char *opt = NULL;

    list = qemu_find_opts("opts_list_01");
    g_assert(list != NULL);
    g_assert(QTAILQ_EMPTY(&list->head));
    g_assert_cmpstr(list->name, ==, "opts_list_01");

    /* should not find anything at this point */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts == NULL);

    /* create the opts */
    opts = qemu_opts_create(list, NULL, 0, &error_abort);
    g_assert(opts != NULL);
    g_assert(!QTAILQ_EMPTY(&list->head));

    /* haven't set anything to str2 yet */
    opt = qemu_opt_get(opts, "str2");
    g_assert(opt == NULL);

    qemu_opt_set(opts, "str2", "value", &error_abort);

    /* now we have set str2, should know about it */
    opt = qemu_opt_get(opts, "str2");
    g_assert_cmpstr(opt, ==, "value");

    qemu_opt_set(opts, "str2", "value2", &error_abort);

    /* having reset the value, the returned should be the reset one */
    opt = qemu_opt_get(opts, "str2");
    g_assert_cmpstr(opt, ==, "value2");

    qemu_opts_del(opts);

    /* should not find anything at this point */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts == NULL);
}

static void test_qemu_opt_get_bool(void)
{
    QemuOptsList *list;
    QemuOpts *opts;
    bool opt;

    list = qemu_find_opts("opts_list_02");
    g_assert(list != NULL);
    g_assert(QTAILQ_EMPTY(&list->head));
    g_assert_cmpstr(list->name, ==, "opts_list_02");

    /* should not find anything at this point */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts == NULL);

    /* create the opts */
    opts = qemu_opts_create(list, NULL, 0, &error_abort);
    g_assert(opts != NULL);
    g_assert(!QTAILQ_EMPTY(&list->head));

    /* haven't set anything to bool1 yet, so defval should be returned */
    opt = qemu_opt_get_bool(opts, "bool1", false);
    g_assert(opt == false);

    qemu_opt_set_bool(opts, "bool1", true, &error_abort);

    /* now we have set bool1, should know about it */
    opt = qemu_opt_get_bool(opts, "bool1", false);
    g_assert(opt == true);

    /* having reset the value, opt should be the reset one not defval */
    qemu_opt_set_bool(opts, "bool1", false, &error_abort);

    opt = qemu_opt_get_bool(opts, "bool1", true);
    g_assert(opt == false);

    qemu_opts_del(opts);

    /* should not find anything at this point */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts == NULL);
}

static void test_qemu_opt_get_number(void)
{
    QemuOptsList *list;
    QemuOpts *opts;
    uint64_t opt;

    list = qemu_find_opts("opts_list_01");
    g_assert(list != NULL);
    g_assert(QTAILQ_EMPTY(&list->head));
    g_assert_cmpstr(list->name, ==, "opts_list_01");

    /* should not find anything at this point */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts == NULL);

    /* create the opts */
    opts = qemu_opts_create(list, NULL, 0, &error_abort);
    g_assert(opts != NULL);
    g_assert(!QTAILQ_EMPTY(&list->head));

    /* haven't set anything to number1 yet, so defval should be returned */
    opt = qemu_opt_get_number(opts, "number1", 5);
    g_assert(opt == 5);

    qemu_opt_set_number(opts, "number1", 10, &error_abort);

    /* now we have set number1, should know about it */
    opt = qemu_opt_get_number(opts, "number1", 5);
    g_assert(opt == 10);

    /* having reset it, the returned should be the reset one not defval */
    qemu_opt_set_number(opts, "number1", 15, &error_abort);

    opt = qemu_opt_get_number(opts, "number1", 5);
    g_assert(opt == 15);

    qemu_opts_del(opts);

    /* should not find anything at this point */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts == NULL);
}

static void test_qemu_opt_get_size(void)
{
    QemuOptsList *list;
    QemuOpts *opts;
    uint64_t opt;
    QDict *dict;

    list = qemu_find_opts("opts_list_02");
    g_assert(list != NULL);
    g_assert(QTAILQ_EMPTY(&list->head));
    g_assert_cmpstr(list->name, ==, "opts_list_02");

    /* should not find anything at this point */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts == NULL);

    /* create the opts */
    opts = qemu_opts_create(list, NULL, 0, &error_abort);
    g_assert(opts != NULL);
    g_assert(!QTAILQ_EMPTY(&list->head));

    /* haven't set anything to size1 yet, so defval should be returned */
    opt = qemu_opt_get_size(opts, "size1", 5);
    g_assert(opt == 5);

    dict = qdict_new();
    g_assert(dict != NULL);

    qdict_put_str(dict, "size1", "10");

    qemu_opts_absorb_qdict(opts, dict, &error_abort);
    g_assert(error_abort == NULL);

    /* now we have set size1, should know about it */
    opt = qemu_opt_get_size(opts, "size1", 5);
    g_assert(opt == 10);

    /* reset value */
    qdict_put_str(dict, "size1", "15");

    qemu_opts_absorb_qdict(opts, dict, &error_abort);
    g_assert(error_abort == NULL);

    /* test the reset value */
    opt = qemu_opt_get_size(opts, "size1", 5);
    g_assert(opt == 15);

    qdict_del(dict, "size1");
    g_free(dict);

    qemu_opts_del(opts);

    /* should not find anything at this point */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts == NULL);
}

static void test_qemu_opt_unset(void)
{
    QemuOpts *opts;
    const char *value;
    int ret;

    /* dynamically initialized (parsed) opts */
    opts = qemu_opts_parse(&opts_list_03, "key=value", false, NULL);
    g_assert(opts != NULL);

    /* check default/parsed value */
    value = qemu_opt_get(opts, "key");
    g_assert_cmpstr(value, ==, "value");

    /* reset it to value2 */
    qemu_opt_set(opts, "key", "value2", &error_abort);

    value = qemu_opt_get(opts, "key");
    g_assert_cmpstr(value, ==, "value2");

    /* unset, valid only for "accept any" */
    ret = qemu_opt_unset(opts, "key");
    g_assert(ret == 0);

    /* after reset the value should be the parsed/default one */
    value = qemu_opt_get(opts, "key");
    g_assert_cmpstr(value, ==, "value");

    qemu_opts_del(opts);
}

static void test_qemu_opts_reset(void)
{
    QemuOptsList *list;
    QemuOpts *opts;
    uint64_t opt;

    list = qemu_find_opts("opts_list_01");
    g_assert(list != NULL);
    g_assert(QTAILQ_EMPTY(&list->head));
    g_assert_cmpstr(list->name, ==, "opts_list_01");

    /* should not find anything at this point */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts == NULL);

    /* create the opts */
    opts = qemu_opts_create(list, NULL, 0, &error_abort);
    g_assert(opts != NULL);
    g_assert(!QTAILQ_EMPTY(&list->head));

    /* haven't set anything to number1 yet, so defval should be returned */
    opt = qemu_opt_get_number(opts, "number1", 5);
    g_assert(opt == 5);

    qemu_opt_set_number(opts, "number1", 10, &error_abort);

    /* now we have set number1, should know about it */
    opt = qemu_opt_get_number(opts, "number1", 5);
    g_assert(opt == 10);

    qemu_opts_reset(list);

    /* should not find anything at this point */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts == NULL);
}

static int opts_count_iter(void *opaque, const char *name, const char *value,
                           Error **errp)
{
    (*(size_t *)opaque)++;
    return 0;
}

static size_t opts_count(QemuOpts *opts)
{
    size_t n = 0;

    qemu_opt_foreach(opts, opts_count_iter, &n, NULL);
    return n;
}

static void test_opts_parse(void)
{
    Error *err = NULL;
    QemuOpts *opts;

    /* Nothing */
    opts = qemu_opts_parse(&opts_list_03, "", false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 0);

    /* Empty key */
    opts = qemu_opts_parse(&opts_list_03, "=val", false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 1);
    g_assert_cmpstr(qemu_opt_get(opts, ""), ==, "val");

    /* Multiple keys, last one wins */
    opts = qemu_opts_parse(&opts_list_03, "a=1,b=2,,x,a=3",
                           false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 3);
    g_assert_cmpstr(qemu_opt_get(opts, "a"), ==, "3");
    g_assert_cmpstr(qemu_opt_get(opts, "b"), ==, "2,x");

    /* Except when it doesn't */
    opts = qemu_opts_parse(&opts_list_03, "id=foo,id=bar",
                           false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 0);
    g_assert_cmpstr(qemu_opts_id(opts), ==, "foo");

    /* TODO Cover low-level access to repeated keys */

    /* Trailing comma is ignored */
    opts = qemu_opts_parse(&opts_list_03, "x=y,", false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 1);
    g_assert_cmpstr(qemu_opt_get(opts, "x"), ==, "y");

    /* Except when it isn't */
    opts = qemu_opts_parse(&opts_list_03, ",", false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 1);
    g_assert_cmpstr(qemu_opt_get(opts, ""), ==, "on");

    /* Duplicate ID */
    opts = qemu_opts_parse(&opts_list_03, "x=y,id=foo", false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);
    /* TODO Cover .merge_lists = true */

    /* Buggy ID recognition (fixed) */
    opts = qemu_opts_parse(&opts_list_03, "x=,,id=bar", false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 1);
    g_assert(!qemu_opts_id(opts));
    g_assert_cmpstr(qemu_opt_get(opts, "x"), ==, ",id=bar");

    /* Anti-social ID */
    opts = qemu_opts_parse(&opts_list_01, "id=666", false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);

    /* Implied value (qemu_opts_parse warns but accepts it) */
    opts = qemu_opts_parse(&opts_list_03, "an,noaus,noaus=",
                           false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 3);
    g_assert_cmpstr(qemu_opt_get(opts, "an"), ==, "on");
    g_assert_cmpstr(qemu_opt_get(opts, "aus"), ==, "off");
    g_assert_cmpstr(qemu_opt_get(opts, "noaus"), ==, "");

    /* Implied value, negated empty key */
    opts = qemu_opts_parse(&opts_list_03, "no", false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 1);
    g_assert_cmpstr(qemu_opt_get(opts, ""), ==, "off");

    /* Implied key */
    opts = qemu_opts_parse(&opts_list_03, "an,noaus,noaus=", true,
                           &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 3);
    g_assert_cmpstr(qemu_opt_get(opts, "implied"), ==, "an");
    g_assert_cmpstr(qemu_opt_get(opts, "aus"), ==, "off");
    g_assert_cmpstr(qemu_opt_get(opts, "noaus"), ==, "");

    /* Implied key with empty value */
    opts = qemu_opts_parse(&opts_list_03, ",", true, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 1);
    g_assert_cmpstr(qemu_opt_get(opts, "implied"), ==, "");

    /* Implied key with comma value */
    opts = qemu_opts_parse(&opts_list_03, ",,,a=1", true, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 2);
    g_assert_cmpstr(qemu_opt_get(opts, "implied"), ==, ",");
    g_assert_cmpstr(qemu_opt_get(opts, "a"), ==, "1");

    /* Empty key is not an implied key */
    opts = qemu_opts_parse(&opts_list_03, "=val", true, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 1);
    g_assert_cmpstr(qemu_opt_get(opts, ""), ==, "val");

    /* Unknown key */
    opts = qemu_opts_parse(&opts_list_01, "nonexistent=", false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);

    qemu_opts_reset(&opts_list_01);
    qemu_opts_reset(&opts_list_03);
}

static void test_opts_parse_bool(void)
{
    Error *err = NULL;
    QemuOpts *opts;

    opts = qemu_opts_parse(&opts_list_02, "bool1=on,bool2=off",
                           false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 2);
    g_assert(qemu_opt_get_bool(opts, "bool1", false));
    g_assert(!qemu_opt_get_bool(opts, "bool2", true));

    opts = qemu_opts_parse(&opts_list_02, "bool1=offer", false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);

    qemu_opts_reset(&opts_list_02);
}

static void test_opts_parse_number(void)
{
    Error *err = NULL;
    QemuOpts *opts;

    /* Lower limit zero */
    opts = qemu_opts_parse(&opts_list_01, "number1=0", false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 1);
    g_assert_cmpuint(qemu_opt_get_number(opts, "number1", 1), ==, 0);

    /* Upper limit 2^64-1 */
    opts = qemu_opts_parse(&opts_list_01,
                           "number1=18446744073709551615,number2=-1",
                           false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 2);
    g_assert_cmphex(qemu_opt_get_number(opts, "number1", 1), ==, UINT64_MAX);
    g_assert_cmphex(qemu_opt_get_number(opts, "number2", 0), ==, UINT64_MAX);

    /* Above upper limit */
    opts = qemu_opts_parse(&opts_list_01, "number1=18446744073709551616",
                           false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);

    /* Below lower limit */
    opts = qemu_opts_parse(&opts_list_01, "number1=-18446744073709551616",
                           false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);

    /* Hex and octal */
    opts = qemu_opts_parse(&opts_list_01, "number1=0x2a,number2=052",
                           false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 2);
    g_assert_cmpuint(qemu_opt_get_number(opts, "number1", 1), ==, 42);
    g_assert_cmpuint(qemu_opt_get_number(opts, "number2", 0), ==, 42);

    /* Invalid */
    opts = qemu_opts_parse(&opts_list_01, "number1=", false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);
    opts = qemu_opts_parse(&opts_list_01, "number1=eins", false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);

    /* Leading whitespace */
    opts = qemu_opts_parse(&opts_list_01, "number1= \t42",
                           false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 1);
    g_assert_cmpuint(qemu_opt_get_number(opts, "number1", 1), ==, 42);

    /* Trailing crap */
    opts = qemu_opts_parse(&opts_list_01, "number1=3.14", false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);
    opts = qemu_opts_parse(&opts_list_01, "number1=08", false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);
    opts = qemu_opts_parse(&opts_list_01, "number1=0 ", false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);

    qemu_opts_reset(&opts_list_01);
}

static void test_opts_parse_size(void)
{
    Error *err = NULL;
    QemuOpts *opts;

    /* Lower limit zero */
    opts = qemu_opts_parse(&opts_list_02, "size1=0", false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 1);
    g_assert_cmpuint(qemu_opt_get_size(opts, "size1", 1), ==, 0);

    /* Note: full 64 bits of precision */

    /* Around double limit of precision: 2^53-1, 2^53, 2^53+1 */
    opts = qemu_opts_parse(&opts_list_02,
                           "size1=9007199254740991,"
                           "size2=9007199254740992,"
                           "size3=9007199254740993",
                           false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 3);
    g_assert_cmphex(qemu_opt_get_size(opts, "size1", 1),
                     ==, 0x1fffffffffffff);
    g_assert_cmphex(qemu_opt_get_size(opts, "size2", 1),
                     ==, 0x20000000000000);
    g_assert_cmphex(qemu_opt_get_size(opts, "size3", 1),
                     ==, 0x20000000000001);

    /* Close to signed int limit: 2^63-1, 2^63, 2^63+1 */
    opts = qemu_opts_parse(&opts_list_02,
                           "size1=9223372036854775807," /* 7fffffffffffffff */
                           "size2=9223372036854775808," /* 8000000000000000 */
                           "size3=9223372036854775809", /* 8000000000000001 */
                           false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 3);
    g_assert_cmphex(qemu_opt_get_size(opts, "size1", 1),
                     ==, 0x7fffffffffffffff);
    g_assert_cmphex(qemu_opt_get_size(opts, "size2", 1),
                     ==, 0x8000000000000000);
    g_assert_cmphex(qemu_opt_get_size(opts, "size3", 1),
                     ==, 0x8000000000000001);

    /* Close to actual upper limit 0xfffffffffffff800 (53 msbs set) */
    opts = qemu_opts_parse(&opts_list_02,
                           "size1=18446744073709549568," /* fffffffffffff800 */
                           "size2=18446744073709550591", /* fffffffffffffbff */
                           false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 2);
    g_assert_cmphex(qemu_opt_get_size(opts, "size1", 1),
                     ==, 0xfffffffffffff800);
    g_assert_cmphex(qemu_opt_get_size(opts, "size2", 1),
                     ==, 0xfffffffffffffbff);

    /* Actual limit, 2^64-1 */
    opts = qemu_opts_parse(&opts_list_02,
                           "size1=18446744073709551615", /* ffffffffffffffff */
                           false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 1);
    g_assert_cmphex(qemu_opt_get_size(opts, "size1", 1),
                     ==, 0xffffffffffffffff);

    /* Beyond limits */
    opts = qemu_opts_parse(&opts_list_02, "size1=-1", false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);
    opts = qemu_opts_parse(&opts_list_02,
                           "size1=18446744073709551616", /* 2^64 */
                           false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);

    /* Suffixes */
    opts = qemu_opts_parse(&opts_list_02, "size1=8b,size2=1.5k,size3=2M",
                           false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 3);
    g_assert_cmphex(qemu_opt_get_size(opts, "size1", 0), ==, 8);
    g_assert_cmphex(qemu_opt_get_size(opts, "size2", 0), ==, 1536);
    g_assert_cmphex(qemu_opt_get_size(opts, "size3", 0), ==, 2 * MiB);
    opts = qemu_opts_parse(&opts_list_02, "size1=0.1G,size2=16777215T",
                           false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 2);
    g_assert_cmphex(qemu_opt_get_size(opts, "size1", 0), ==, GiB / 10);
    g_assert_cmphex(qemu_opt_get_size(opts, "size2", 0), ==, 16777215ULL * TiB);

    /* Beyond limit with suffix */
    opts = qemu_opts_parse(&opts_list_02, "size1=16777216T",
                           false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);

    /* Trailing crap */
    opts = qemu_opts_parse(&opts_list_02, "size1=16E", false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);
    opts = qemu_opts_parse(&opts_list_02, "size1=16Gi", false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);

    qemu_opts_reset(&opts_list_02);
}

static void test_has_help_option(void)
{
    static const struct {
        const char *params;
        /* expected value of qemu_opt_has_help_opt() with implied=false */
        bool expect;
        /* expected value of qemu_opt_has_help_opt() with implied=true */
        bool expect_implied;
    } test[] = {
        { "help", true, false },
        { "?", true, false },
        { "helpme", false, false },
        { "?me", false, false },
        { "a,help", true, true },
        { "a,?", true, true },
        { "a=0,help,b", true, true },
        { "a=0,?,b", true, true },
        { "help,b=1", true, false },
        { "?,b=1", true, false },
        { "a,b,,help", true, true },
        { "a,b,,?", true, true },
    };
    int i;
    QemuOpts *opts;

    for (i = 0; i < ARRAY_SIZE(test); i++) {
        g_assert_cmpint(has_help_option(test[i].params),
                        ==, test[i].expect);
        opts = qemu_opts_parse(&opts_list_03, test[i].params, false,
                               &error_abort);
        g_assert_cmpint(qemu_opt_has_help_opt(opts),
                        ==, test[i].expect);
        qemu_opts_del(opts);
        opts = qemu_opts_parse(&opts_list_03, test[i].params, true,
                               &error_abort);
        g_assert_cmpint(qemu_opt_has_help_opt(opts),
                        ==, test[i].expect_implied);
        qemu_opts_del(opts);
    }
}

static void append_verify_list_01(QemuOptDesc *desc, bool with_overlapping)
{
    int i = 0;

    if (with_overlapping) {
        g_assert_cmpstr(desc[i].name, ==, "str1");
        g_assert_cmpint(desc[i].type, ==, QEMU_OPT_STRING);
        g_assert_cmpstr(desc[i].help, ==,
                        "Help texts are preserved in qemu_opts_append");
        g_assert_cmpstr(desc[i].def_value_str, ==, "default");
        i++;

        g_assert_cmpstr(desc[i].name, ==, "str2");
        g_assert_cmpint(desc[i].type, ==, QEMU_OPT_STRING);
        g_assert_cmpstr(desc[i].help, ==, NULL);
        g_assert_cmpstr(desc[i].def_value_str, ==, NULL);
        i++;
    }

    g_assert_cmpstr(desc[i].name, ==, "str3");
    g_assert_cmpint(desc[i].type, ==, QEMU_OPT_STRING);
    g_assert_cmpstr(desc[i].help, ==, NULL);
    g_assert_cmpstr(desc[i].def_value_str, ==, NULL);
    i++;

    g_assert_cmpstr(desc[i].name, ==, "number1");
    g_assert_cmpint(desc[i].type, ==, QEMU_OPT_NUMBER);
    g_assert_cmpstr(desc[i].help, ==,
                    "Having help texts only for some options is okay");
    g_assert_cmpstr(desc[i].def_value_str, ==, NULL);
    i++;

    g_assert_cmpstr(desc[i].name, ==, "number2");
    g_assert_cmpint(desc[i].type, ==, QEMU_OPT_NUMBER);
    g_assert_cmpstr(desc[i].help, ==, NULL);
    g_assert_cmpstr(desc[i].def_value_str, ==, NULL);
    i++;

    g_assert_cmpstr(desc[i].name, ==, NULL);
}

static void append_verify_list_02(QemuOptDesc *desc)
{
    int i = 0;

    g_assert_cmpstr(desc[i].name, ==, "str1");
    g_assert_cmpint(desc[i].type, ==, QEMU_OPT_STRING);
    g_assert_cmpstr(desc[i].help, ==, NULL);
    g_assert_cmpstr(desc[i].def_value_str, ==, NULL);
    i++;

    g_assert_cmpstr(desc[i].name, ==, "str2");
    g_assert_cmpint(desc[i].type, ==, QEMU_OPT_STRING);
    g_assert_cmpstr(desc[i].help, ==, NULL);
    g_assert_cmpstr(desc[i].def_value_str, ==, NULL);
    i++;

    g_assert_cmpstr(desc[i].name, ==, "bool1");
    g_assert_cmpint(desc[i].type, ==, QEMU_OPT_BOOL);
    g_assert_cmpstr(desc[i].help, ==, NULL);
    g_assert_cmpstr(desc[i].def_value_str, ==, NULL);
    i++;

    g_assert_cmpstr(desc[i].name, ==, "bool2");
    g_assert_cmpint(desc[i].type, ==, QEMU_OPT_BOOL);
    g_assert_cmpstr(desc[i].help, ==, NULL);
    g_assert_cmpstr(desc[i].def_value_str, ==, NULL);
    i++;

    g_assert_cmpstr(desc[i].name, ==, "size1");
    g_assert_cmpint(desc[i].type, ==, QEMU_OPT_SIZE);
    g_assert_cmpstr(desc[i].help, ==, NULL);
    g_assert_cmpstr(desc[i].def_value_str, ==, NULL);
    i++;

    g_assert_cmpstr(desc[i].name, ==, "size2");
    g_assert_cmpint(desc[i].type, ==, QEMU_OPT_SIZE);
    g_assert_cmpstr(desc[i].help, ==, NULL);
    g_assert_cmpstr(desc[i].def_value_str, ==, NULL);
    i++;

    g_assert_cmpstr(desc[i].name, ==, "size3");
    g_assert_cmpint(desc[i].type, ==, QEMU_OPT_SIZE);
    g_assert_cmpstr(desc[i].help, ==, NULL);
    g_assert_cmpstr(desc[i].def_value_str, ==, NULL);
}

static void test_opts_append_to_null(void)
{
    QemuOptsList *merged;

    merged = qemu_opts_append(NULL, &opts_list_01);
    g_assert(merged != &opts_list_01);

    g_assert_cmpstr(merged->name, ==, NULL);
    g_assert_cmpstr(merged->implied_opt_name, ==, NULL);
    g_assert_false(merged->merge_lists);

    append_verify_list_01(merged->desc, true);

    qemu_opts_free(merged);
}

static void test_opts_append(void)
{
    QemuOptsList *first, *merged;

    first = qemu_opts_append(NULL, &opts_list_02);
    merged = qemu_opts_append(first, &opts_list_01);
    g_assert(first != &opts_list_02);
    g_assert(merged != &opts_list_01);

    g_assert_cmpstr(merged->name, ==, NULL);
    g_assert_cmpstr(merged->implied_opt_name, ==, NULL);
    g_assert_false(merged->merge_lists);

    append_verify_list_02(&merged->desc[0]);
    append_verify_list_01(&merged->desc[7], false);

    qemu_opts_free(merged);
}

static void test_opts_to_qdict_basic(void)
{
    QemuOpts *opts;
    QDict *dict;

    opts = qemu_opts_parse(&opts_list_01, "str1=foo,str2=,str3=bar,number1=42",
                           false, &error_abort);
    g_assert(opts != NULL);

    dict = qemu_opts_to_qdict(opts, NULL);
    g_assert(dict != NULL);

    g_assert_cmpstr(qdict_get_str(dict, "str1"), ==, "foo");
    g_assert_cmpstr(qdict_get_str(dict, "str2"), ==, "");
    g_assert_cmpstr(qdict_get_str(dict, "str3"), ==, "bar");
    g_assert_cmpstr(qdict_get_str(dict, "number1"), ==, "42");
    g_assert_false(qdict_haskey(dict, "number2"));

    qobject_unref(dict);
    qemu_opts_del(opts);
}

static void test_opts_to_qdict_filtered(void)
{
    QemuOptsList *first, *merged;
    QemuOpts *opts;
    QDict *dict;

    first = qemu_opts_append(NULL, &opts_list_02);
    merged = qemu_opts_append(first, &opts_list_01);

    opts = qemu_opts_parse(merged,
                           "str1=foo,str2=,str3=bar,bool1=off,number1=42",
                           false, &error_abort);
    g_assert(opts != NULL);

    /* Convert to QDict without deleting from opts */
    dict = qemu_opts_to_qdict_filtered(opts, NULL, &opts_list_01, false);
    g_assert(dict != NULL);
    g_assert_cmpstr(qdict_get_str(dict, "str1"), ==, "foo");
    g_assert_cmpstr(qdict_get_str(dict, "str2"), ==, "");
    g_assert_cmpstr(qdict_get_str(dict, "str3"), ==, "bar");
    g_assert_cmpstr(qdict_get_str(dict, "number1"), ==, "42");
    g_assert_false(qdict_haskey(dict, "number2"));
    g_assert_false(qdict_haskey(dict, "bool1"));
    qobject_unref(dict);

    dict = qemu_opts_to_qdict_filtered(opts, NULL, &opts_list_02, false);
    g_assert(dict != NULL);
    g_assert_cmpstr(qdict_get_str(dict, "str1"), ==, "foo");
    g_assert_cmpstr(qdict_get_str(dict, "str2"), ==, "");
    g_assert_cmpstr(qdict_get_str(dict, "bool1"), ==, "off");
    g_assert_false(qdict_haskey(dict, "str3"));
    g_assert_false(qdict_haskey(dict, "number1"));
    g_assert_false(qdict_haskey(dict, "number2"));
    qobject_unref(dict);

    /* Now delete converted options from opts */
    dict = qemu_opts_to_qdict_filtered(opts, NULL, &opts_list_01, true);
    g_assert(dict != NULL);
    g_assert_cmpstr(qdict_get_str(dict, "str1"), ==, "foo");
    g_assert_cmpstr(qdict_get_str(dict, "str2"), ==, "");
    g_assert_cmpstr(qdict_get_str(dict, "str3"), ==, "bar");
    g_assert_cmpstr(qdict_get_str(dict, "number1"), ==, "42");
    g_assert_false(qdict_haskey(dict, "number2"));
    g_assert_false(qdict_haskey(dict, "bool1"));
    qobject_unref(dict);

    dict = qemu_opts_to_qdict_filtered(opts, NULL, &opts_list_02, true);
    g_assert(dict != NULL);
    g_assert_cmpstr(qdict_get_str(dict, "bool1"), ==, "off");
    g_assert_false(qdict_haskey(dict, "str1"));
    g_assert_false(qdict_haskey(dict, "str2"));
    g_assert_false(qdict_haskey(dict, "str3"));
    g_assert_false(qdict_haskey(dict, "number1"));
    g_assert_false(qdict_haskey(dict, "number2"));
    qobject_unref(dict);

    g_assert_true(QTAILQ_EMPTY(&opts->head));

    qemu_opts_del(opts);
    qemu_opts_free(merged);
}

static void test_opts_to_qdict_duplicates(void)
{
    QemuOpts *opts;
    QemuOpt *opt;
    QDict *dict;

    opts = qemu_opts_parse(&opts_list_03, "foo=a,foo=b", false, &error_abort);
    g_assert(opts != NULL);

    /* Verify that opts has two options with the same name */
    opt = QTAILQ_FIRST(&opts->head);
    g_assert_cmpstr(opt->name, ==, "foo");
    g_assert_cmpstr(opt->str , ==, "a");

    opt = QTAILQ_NEXT(opt, next);
    g_assert_cmpstr(opt->name, ==, "foo");
    g_assert_cmpstr(opt->str , ==, "b");

    opt = QTAILQ_NEXT(opt, next);
    g_assert(opt == NULL);

    /* In the conversion to QDict, the last one wins */
    dict = qemu_opts_to_qdict(opts, NULL);
    g_assert(dict != NULL);
    g_assert_cmpstr(qdict_get_str(dict, "foo"), ==, "b");
    qobject_unref(dict);

    /* The last one still wins if entries are deleted, and both are deleted */
    dict = qemu_opts_to_qdict_filtered(opts, NULL, NULL, true);
    g_assert(dict != NULL);
    g_assert_cmpstr(qdict_get_str(dict, "foo"), ==, "b");
    qobject_unref(dict);

    g_assert_true(QTAILQ_EMPTY(&opts->head));

    qemu_opts_del(opts);
}

int main(int argc, char *argv[])
{
    register_opts();
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/qemu-opts/find_unknown_opts", test_find_unknown_opts);
    g_test_add_func("/qemu-opts/find_opts", test_qemu_find_opts);
    g_test_add_func("/qemu-opts/opts_create", test_qemu_opts_create);
    g_test_add_func("/qemu-opts/opt_get", test_qemu_opt_get);
    g_test_add_func("/qemu-opts/opt_get_bool", test_qemu_opt_get_bool);
    g_test_add_func("/qemu-opts/opt_get_number", test_qemu_opt_get_number);
    g_test_add_func("/qemu-opts/opt_get_size", test_qemu_opt_get_size);
    g_test_add_func("/qemu-opts/opt_unset", test_qemu_opt_unset);
    g_test_add_func("/qemu-opts/opts_reset", test_qemu_opts_reset);
    g_test_add_func("/qemu-opts/opts_parse/general", test_opts_parse);
    g_test_add_func("/qemu-opts/opts_parse/bool", test_opts_parse_bool);
    g_test_add_func("/qemu-opts/opts_parse/number", test_opts_parse_number);
    g_test_add_func("/qemu-opts/opts_parse/size", test_opts_parse_size);
    g_test_add_func("/qemu-opts/has_help_option", test_has_help_option);
    g_test_add_func("/qemu-opts/append_to_null", test_opts_append_to_null);
    g_test_add_func("/qemu-opts/append", test_opts_append);
    g_test_add_func("/qemu-opts/to_qdict/basic", test_opts_to_qdict_basic);
    g_test_add_func("/qemu-opts/to_qdict/filtered", test_opts_to_qdict_filtered);
    g_test_add_func("/qemu-opts/to_qdict/duplicates", test_opts_to_qdict_duplicates);
    g_test_run();
    return 0;
}
