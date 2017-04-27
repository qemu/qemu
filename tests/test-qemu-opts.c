/*
 * QemuOpts unit-tests.
 *
 * Copyright (C) 2014 Leandro Dorileo <l@dorileo.org>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qapi/qmp/qstring.h"
#include "qemu/config-file.h"


static QemuOptsList opts_list_01 = {
    .name = "opts_list_01",
    .head = QTAILQ_HEAD_INITIALIZER(opts_list_01.head),
    .desc = {
        {
            .name = "str1",
            .type = QEMU_OPT_STRING,
        },{
            .name = "str2",
            .type = QEMU_OPT_STRING,
        },{
            .name = "str3",
            .type = QEMU_OPT_STRING,
        },{
            .name = "number1",
            .type = QEMU_OPT_NUMBER,
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

static void register_opts(void)
{
    qemu_add_opts(&opts_list_01);
    qemu_add_opts(&opts_list_02);
    qemu_add_opts(&opts_list_03);
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
    Error *err = NULL;
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

    qemu_opt_set_bool(opts, "bool1", true, &err);
    g_assert(!err);

    /* now we have set bool1, should know about it */
    opt = qemu_opt_get_bool(opts, "bool1", false);
    g_assert(opt == true);

    /* having reset the value, opt should be the reset one not defval */
    qemu_opt_set_bool(opts, "bool1", false, &err);
    g_assert(!err);

    opt = qemu_opt_get_bool(opts, "bool1", true);
    g_assert(opt == false);

    qemu_opts_del(opts);

    /* should not find anything at this point */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts == NULL);
}

static void test_qemu_opt_get_number(void)
{
    Error *err = NULL;
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

    qemu_opt_set_number(opts, "number1", 10, &err);
    g_assert(!err);

    /* now we have set number1, should know about it */
    opt = qemu_opt_get_number(opts, "number1", 5);
    g_assert(opt == 10);

    /* having reset it, the returned should be the reset one not defval */
    qemu_opt_set_number(opts, "number1", 15, &err);
    g_assert(!err);

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
    Error *err = NULL;
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

    qemu_opt_set_number(opts, "number1", 10, &err);
    g_assert(!err);

    /* now we have set number1, should know about it */
    opt = qemu_opt_get_number(opts, "number1", 5);
    g_assert(opt == 10);

    qemu_opts_reset(list);

    /* should not find anything at this point */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts == NULL);
}

static void test_qemu_opts_set(void)
{
    Error *err = NULL;
    QemuOptsList *list;
    QemuOpts *opts;
    const char *opt;

    list = qemu_find_opts("opts_list_01");
    g_assert(list != NULL);
    g_assert(QTAILQ_EMPTY(&list->head));
    g_assert_cmpstr(list->name, ==, "opts_list_01");

    /* should not find anything at this point */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts == NULL);

    /* implicitly create opts and set str3 value */
    qemu_opts_set(list, NULL, "str3", "value", &err);
    g_assert(!err);
    g_assert(!QTAILQ_EMPTY(&list->head));

    /* get the just created opts */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts != NULL);

    /* check the str3 value */
    opt = qemu_opt_get(opts, "str3");
    g_assert_cmpstr(opt, ==, "value");

    qemu_opts_del(opts);

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
    char long_key[129];
    char *params;

    /* Nothing */
    opts = qemu_opts_parse(&opts_list_03, "", false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 0);

    /* Empty key */
    opts = qemu_opts_parse(&opts_list_03, "=val", false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 1);
    g_assert_cmpstr(qemu_opt_get(opts, ""), ==, "val");

    /* Long key */
    memset(long_key, 'a', 127);
    long_key[127] = 'z';
    long_key[128] = 0;
    params = g_strdup_printf("%s=v", long_key);
    opts = qemu_opts_parse(&opts_list_03, params + 1, NULL, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 1);
    g_assert_cmpstr(qemu_opt_get(opts, long_key + 1), ==, "v");

    /* Overlong key gets truncated */
    opts = qemu_opts_parse(&opts_list_03, params, NULL, &error_abort);
    g_assert(opts_count(opts) == 1);
    long_key[127] = 0;
    g_assert_cmpstr(qemu_opt_get(opts, long_key), ==, "v");
    g_free(params);

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

    /* Buggy ID recognition */
    opts = qemu_opts_parse(&opts_list_03, "x=,,id=bar", false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 1);
    g_assert_cmpstr(qemu_opts_id(opts), ==, "bar"); /* BUG */
    g_assert_cmpstr(qemu_opt_get(opts, "x"), ==, ",id=bar");

    /* Anti-social ID */
    opts = qemu_opts_parse(&opts_list_01, "id=666", false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);

    /* Implied value */
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

    /* Note: precision is 53 bits since we're parsing with strtod() */

    /* Around limit of precision: 2^53-1, 2^53, 2^54 */
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
                     ==, 0x20000000000000);

    /* Close to signed upper limit 0x7ffffffffffffc00 (53 msbs set) */
    opts = qemu_opts_parse(&opts_list_02,
                           "size1=9223372036854774784," /* 7ffffffffffffc00 */
                           "size2=9223372036854775295", /* 7ffffffffffffdff */
                           false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 2);
    g_assert_cmphex(qemu_opt_get_size(opts, "size1", 1),
                     ==, 0x7ffffffffffffc00);
    g_assert_cmphex(qemu_opt_get_size(opts, "size2", 1),
                     ==, 0x7ffffffffffffc00);

    /* Close to actual upper limit 0xfffffffffffff800 (53 msbs set) */
    opts = qemu_opts_parse(&opts_list_02,
                           "size1=18446744073709549568," /* fffffffffffff800 */
                           "size2=18446744073709550591", /* fffffffffffffbff */
                           false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 2);
    g_assert_cmphex(qemu_opt_get_size(opts, "size1", 1),
                     ==, 0xfffffffffffff800);
    g_assert_cmphex(qemu_opt_get_size(opts, "size2", 1),
                     ==, 0xfffffffffffff800);

    /* Beyond limits */
    opts = qemu_opts_parse(&opts_list_02, "size1=-1", false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);
    opts = qemu_opts_parse(&opts_list_02,
                           "size1=18446744073709550592", /* fffffffffffffc00 */
                           false, &err);
    error_free_or_abort(&err);
    g_assert(!opts);

    /* Suffixes */
    opts = qemu_opts_parse(&opts_list_02, "size1=8b,size2=1.5k,size3=2M",
                           false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 3);
    g_assert_cmphex(qemu_opt_get_size(opts, "size1", 0), ==, 8);
    g_assert_cmphex(qemu_opt_get_size(opts, "size2", 0), ==, 1536);
    g_assert_cmphex(qemu_opt_get_size(opts, "size3", 0), ==, 2 * M_BYTE);
    opts = qemu_opts_parse(&opts_list_02, "size1=0.1G,size2=16777215T",
                           false, &error_abort);
    g_assert_cmpuint(opts_count(opts), ==, 2);
    g_assert_cmphex(qemu_opt_get_size(opts, "size1", 0), ==, G_BYTE / 10);
    g_assert_cmphex(qemu_opt_get_size(opts, "size2", 0),
                     ==, 16777215 * T_BYTE);

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
    g_test_add_func("/qemu-opts/opts_set", test_qemu_opts_set);
    g_test_add_func("/qemu-opts/opts_parse/general", test_opts_parse);
    g_test_add_func("/qemu-opts/opts_parse/bool", test_opts_parse_bool);
    g_test_add_func("/qemu-opts/opts_parse/number", test_opts_parse_number);
    g_test_add_func("/qemu-opts/opts_parse/size", test_opts_parse_size);
    g_test_run();
    return 0;
}
