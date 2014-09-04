/*
 * QemuOpts unit-tests.
 *
 * Copyright (C) 2014 Leandro Dorileo <l@dorileo.org>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qapi/error.h"
#include "qapi/qmp/qstring.h"
#include "qemu/config-file.h"

#include <glib.h>
#include <string.h>

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
            .name = "bool1",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "str2",
            .type = QEMU_OPT_STRING,
        },{
            .name = "size1",
            .type = QEMU_OPT_SIZE,
        },
        { /* end of list */ }
    },
};

static QemuOptsList opts_list_03 = {
    .name = "opts_list_03",
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
    g_assert(err);
    error_free(err);
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

    qemu_opt_set(opts, "str2", "value");

    /* now we have set str2, should know about it */
    opt = qemu_opt_get(opts, "str2");
    g_assert_cmpstr(opt, ==, "value");

    qemu_opt_set(opts, "str2", "value2");

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
    int ret;

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

    ret = qemu_opt_set_bool(opts, "bool1", true);
    g_assert(ret == 0);

    /* now we have set bool1, should know about it */
    opt = qemu_opt_get_bool(opts, "bool1", false);
    g_assert(opt == true);

    /* having reset the value, opt should be the reset one not defval */
    ret = qemu_opt_set_bool(opts, "bool1", false);
    g_assert(ret == 0);

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
    int ret;

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

    ret = qemu_opt_set_number(opts, "number1", 10);
    g_assert(ret == 0);

    /* now we have set number1, should know about it */
    opt = qemu_opt_get_number(opts, "number1", 5);
    g_assert(opt == 10);

    /* having reset it, the returned should be the reset one not defval */
    ret = qemu_opt_set_number(opts, "number1", 15);
    g_assert(ret == 0);

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

    qdict_put(dict, "size1", qstring_from_str("10"));

    qemu_opts_absorb_qdict(opts, dict, &error_abort);
    g_assert(error_abort == NULL);

    /* now we have set size1, should know about it */
    opt = qemu_opt_get_size(opts, "size1", 5);
    g_assert(opt == 10);

    /* reset value */
    qdict_put(dict, "size1", qstring_from_str("15"));

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
    opts = qemu_opts_parse(&opts_list_03, "key=value", 0);
    g_assert(opts != NULL);

    /* check default/parsed value */
    value = qemu_opt_get(opts, "key");
    g_assert_cmpstr(value, ==, "value");

    /* reset it to value2 */
    qemu_opt_set(opts, "key", "value2");

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
    int ret;

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

    ret = qemu_opt_set_number(opts, "number1", 10);
    g_assert(ret == 0);

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
    QemuOptsList *list;
    QemuOpts *opts;
    int ret;
    const char *opt;

    list = qemu_find_opts("opts_list_01");
    g_assert(list != NULL);
    g_assert(QTAILQ_EMPTY(&list->head));
    g_assert_cmpstr(list->name, ==, "opts_list_01");

    /* should not find anything at this point */
    opts = qemu_opts_find(list, NULL);
    g_assert(opts == NULL);

    /* implicitly create opts and set str3 value */
    ret = qemu_opts_set(list, NULL, "str3", "value");
    g_assert(ret == 0);
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
    g_test_run();
    return 0;
}
