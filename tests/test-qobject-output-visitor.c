/*
 * QObject Output Visitor unit-tests.
 *
 * Copyright (C) 2011-2016 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/qobject-output-visitor.h"
#include "test-qapi-visit.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qnull.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"

typedef struct TestOutputVisitorData {
    Visitor *ov;
    QObject *obj;
} TestOutputVisitorData;

static void visitor_output_setup(TestOutputVisitorData *data,
                                 const void *unused)
{
    data->ov = qobject_output_visitor_new(&data->obj);
    g_assert(data->ov);
}

static void visitor_output_teardown(TestOutputVisitorData *data,
                                    const void *unused)
{
    visit_free(data->ov);
    data->ov = NULL;
    qobject_unref(data->obj);
    data->obj = NULL;
}

static QObject *visitor_get(TestOutputVisitorData *data)
{
    visit_complete(data->ov, &data->obj);
    g_assert(data->obj);
    return data->obj;
}

static void visitor_reset(TestOutputVisitorData *data)
{
    visitor_output_teardown(data, NULL);
    visitor_output_setup(data, NULL);
}

static void test_visitor_out_int(TestOutputVisitorData *data,
                                 const void *unused)
{
    int64_t value = -42;
    int64_t val;
    QNum *qnum;

    visit_type_int(data->ov, NULL, &value, &error_abort);

    qnum = qobject_to(QNum, visitor_get(data));
    g_assert(qnum);
    g_assert(qnum_get_try_int(qnum, &val));
    g_assert_cmpint(val, ==, value);
}

static void test_visitor_out_bool(TestOutputVisitorData *data,
                                  const void *unused)
{
    bool value = true;
    QBool *qbool;

    visit_type_bool(data->ov, NULL, &value, &error_abort);

    qbool = qobject_to(QBool, visitor_get(data));
    g_assert(qbool);
    g_assert(qbool_get_bool(qbool) == value);
}

static void test_visitor_out_number(TestOutputVisitorData *data,
                                    const void *unused)
{
    double value = 3.14;
    QNum *qnum;

    visit_type_number(data->ov, NULL, &value, &error_abort);

    qnum = qobject_to(QNum, visitor_get(data));
    g_assert(qnum);
    g_assert(qnum_get_double(qnum) == value);
}

static void test_visitor_out_string(TestOutputVisitorData *data,
                                    const void *unused)
{
    char *string = (char *) "Q E M U";
    QString *qstr;

    visit_type_str(data->ov, NULL, &string, &error_abort);

    qstr = qobject_to(QString, visitor_get(data));
    g_assert(qstr);
    g_assert_cmpstr(qstring_get_str(qstr), ==, string);
}

static void test_visitor_out_no_string(TestOutputVisitorData *data,
                                       const void *unused)
{
    char *string = NULL;
    QString *qstr;

    /* A null string should return "" */
    visit_type_str(data->ov, NULL, &string, &error_abort);

    qstr = qobject_to(QString, visitor_get(data));
    g_assert(qstr);
    g_assert_cmpstr(qstring_get_str(qstr), ==, "");
}

static void test_visitor_out_enum(TestOutputVisitorData *data,
                                  const void *unused)
{
    EnumOne i;
    QString *qstr;

    for (i = 0; i < ENUM_ONE__MAX; i++) {
        visit_type_EnumOne(data->ov, "unused", &i, &error_abort);

        qstr = qobject_to(QString, visitor_get(data));
        g_assert(qstr);
        g_assert_cmpstr(qstring_get_str(qstr), ==, EnumOne_str(i));
        visitor_reset(data);
    }
}

static void test_visitor_out_enum_errors(TestOutputVisitorData *data,
                                         const void *unused)
{
    EnumOne i, bad_values[] = { ENUM_ONE__MAX, -1 };

    for (i = 0; i < ARRAY_SIZE(bad_values) ; i++) {
        Error *err = NULL;

        visit_type_EnumOne(data->ov, "unused", &bad_values[i], &err);
        error_free_or_abort(&err);
        visitor_reset(data);
    }
}


static void test_visitor_out_struct(TestOutputVisitorData *data,
                                    const void *unused)
{
    TestStruct test_struct = { .integer = 42,
                               .boolean = false,
                               .string = (char *) "foo"};
    TestStruct *p = &test_struct;
    QDict *qdict;

    visit_type_TestStruct(data->ov, NULL, &p, &error_abort);

    qdict = qobject_to(QDict, visitor_get(data));
    g_assert(qdict);
    g_assert_cmpint(qdict_size(qdict), ==, 3);
    g_assert_cmpint(qdict_get_int(qdict, "integer"), ==, 42);
    g_assert_cmpint(qdict_get_bool(qdict, "boolean"), ==, false);
    g_assert_cmpstr(qdict_get_str(qdict, "string"), ==, "foo");
}

static void test_visitor_out_struct_nested(TestOutputVisitorData *data,
                                           const void *unused)
{
    int64_t value = 42;
    UserDefTwo *ud2;
    QDict *qdict, *dict1, *dict2, *dict3, *userdef;
    const char *string = "user def string";
    const char *strings[] = { "forty two", "forty three", "forty four",
                              "forty five" };

    ud2 = g_malloc0(sizeof(*ud2));
    ud2->string0 = g_strdup(strings[0]);

    ud2->dict1 = g_malloc0(sizeof(*ud2->dict1));
    ud2->dict1->string1 = g_strdup(strings[1]);

    ud2->dict1->dict2 = g_malloc0(sizeof(*ud2->dict1->dict2));
    ud2->dict1->dict2->userdef = g_new0(UserDefOne, 1);
    ud2->dict1->dict2->userdef->string = g_strdup(string);
    ud2->dict1->dict2->userdef->integer = value;
    ud2->dict1->dict2->string = g_strdup(strings[2]);

    ud2->dict1->dict3 = g_malloc0(sizeof(*ud2->dict1->dict3));
    ud2->dict1->has_dict3 = true;
    ud2->dict1->dict3->userdef = g_new0(UserDefOne, 1);
    ud2->dict1->dict3->userdef->string = g_strdup(string);
    ud2->dict1->dict3->userdef->integer = value;
    ud2->dict1->dict3->string = g_strdup(strings[3]);

    visit_type_UserDefTwo(data->ov, "unused", &ud2, &error_abort);

    qdict = qobject_to(QDict, visitor_get(data));
    g_assert(qdict);
    g_assert_cmpint(qdict_size(qdict), ==, 2);
    g_assert_cmpstr(qdict_get_str(qdict, "string0"), ==, strings[0]);

    dict1 = qdict_get_qdict(qdict, "dict1");
    g_assert_cmpint(qdict_size(dict1), ==, 3);
    g_assert_cmpstr(qdict_get_str(dict1, "string1"), ==, strings[1]);

    dict2 = qdict_get_qdict(dict1, "dict2");
    g_assert_cmpint(qdict_size(dict2), ==, 2);
    g_assert_cmpstr(qdict_get_str(dict2, "string"), ==, strings[2]);
    userdef = qdict_get_qdict(dict2, "userdef");
    g_assert_cmpint(qdict_size(userdef), ==, 2);
    g_assert_cmpint(qdict_get_int(userdef, "integer"), ==, value);
    g_assert_cmpstr(qdict_get_str(userdef, "string"), ==, string);

    dict3 = qdict_get_qdict(dict1, "dict3");
    g_assert_cmpint(qdict_size(dict3), ==, 2);
    g_assert_cmpstr(qdict_get_str(dict3, "string"), ==, strings[3]);
    userdef = qdict_get_qdict(dict3, "userdef");
    g_assert_cmpint(qdict_size(userdef), ==, 2);
    g_assert_cmpint(qdict_get_int(userdef, "integer"), ==, value);
    g_assert_cmpstr(qdict_get_str(userdef, "string"), ==, string);

    qapi_free_UserDefTwo(ud2);
}

static void test_visitor_out_struct_errors(TestOutputVisitorData *data,
                                           const void *unused)
{
    EnumOne bad_values[] = { ENUM_ONE__MAX, -1 };
    UserDefOne u = {0};
    UserDefOne *pu = &u;
    int i;

    for (i = 0; i < ARRAY_SIZE(bad_values) ; i++) {
        Error *err = NULL;

        u.has_enum1 = true;
        u.enum1 = bad_values[i];
        visit_type_UserDefOne(data->ov, "unused", &pu, &err);
        error_free_or_abort(&err);
        visitor_reset(data);
    }
}


static void test_visitor_out_list(TestOutputVisitorData *data,
                                  const void *unused)
{
    const char *value_str = "list value";
    TestStructList *p, *head = NULL;
    const int max_items = 10;
    bool value_bool = true;
    int value_int = 10;
    QListEntry *entry;
    QList *qlist;
    int i;

    /* Build the list in reverse order... */
    for (i = 0; i < max_items; i++) {
        p = g_malloc0(sizeof(*p));
        p->value = g_malloc0(sizeof(*p->value));
        p->value->integer = value_int + (max_items - i - 1);
        p->value->boolean = value_bool;
        p->value->string = g_strdup(value_str);

        p->next = head;
        head = p;
    }

    visit_type_TestStructList(data->ov, NULL, &head, &error_abort);

    qlist = qobject_to(QList, visitor_get(data));
    g_assert(qlist);
    g_assert(!qlist_empty(qlist));

    /* ...and ensure that the visitor sees it in order */
    i = 0;
    QLIST_FOREACH_ENTRY(qlist, entry) {
        QDict *qdict;

        qdict = qobject_to(QDict, entry->value);
        g_assert(qdict);
        g_assert_cmpint(qdict_size(qdict), ==, 3);
        g_assert_cmpint(qdict_get_int(qdict, "integer"), ==, value_int + i);
        g_assert_cmpint(qdict_get_bool(qdict, "boolean"), ==, value_bool);
        g_assert_cmpstr(qdict_get_str(qdict, "string"), ==, value_str);
        i++;
    }
    g_assert_cmpint(i, ==, max_items);

    qapi_free_TestStructList(head);
}

static void test_visitor_out_list_qapi_free(TestOutputVisitorData *data,
                                            const void *unused)
{
    UserDefTwoList *p, *head = NULL;
    const char string[] = "foo bar";
    int i, max_count = 1024;

    for (i = 0; i < max_count; i++) {
        p = g_malloc0(sizeof(*p));
        p->value = g_malloc0(sizeof(*p->value));

        p->value->string0 = g_strdup(string);
        p->value->dict1 = g_new0(UserDefTwoDict, 1);
        p->value->dict1->string1 = g_strdup(string);
        p->value->dict1->dict2 = g_new0(UserDefTwoDictDict, 1);
        p->value->dict1->dict2->userdef = g_new0(UserDefOne, 1);
        p->value->dict1->dict2->userdef->string = g_strdup(string);
        p->value->dict1->dict2->userdef->integer = 42;
        p->value->dict1->dict2->string = g_strdup(string);
        p->value->dict1->has_dict3 = false;

        p->next = head;
        head = p;
    }

    qapi_free_UserDefTwoList(head);
}

static void test_visitor_out_any(TestOutputVisitorData *data,
                                 const void *unused)
{
    QObject *qobj;
    QNum *qnum;
    QBool *qbool;
    QString *qstring;
    QDict *qdict;
    int64_t val;

    qobj = QOBJECT(qnum_from_int(-42));
    visit_type_any(data->ov, NULL, &qobj, &error_abort);
    qnum = qobject_to(QNum, visitor_get(data));
    g_assert(qnum);
    g_assert(qnum_get_try_int(qnum, &val));
    g_assert_cmpint(val, ==, -42);
    qobject_unref(qobj);

    visitor_reset(data);
    qdict = qdict_new();
    qdict_put_int(qdict, "integer", -42);
    qdict_put_bool(qdict, "boolean", true);
    qdict_put_str(qdict, "string", "foo");
    qobj = QOBJECT(qdict);
    visit_type_any(data->ov, NULL, &qobj, &error_abort);
    qobject_unref(qobj);
    qdict = qobject_to(QDict, visitor_get(data));
    g_assert(qdict);
    qnum = qobject_to(QNum, qdict_get(qdict, "integer"));
    g_assert(qnum);
    g_assert(qnum_get_try_int(qnum, &val));
    g_assert_cmpint(val, ==, -42);
    qbool = qobject_to(QBool, qdict_get(qdict, "boolean"));
    g_assert(qbool);
    g_assert(qbool_get_bool(qbool) == true);
    qstring = qobject_to(QString, qdict_get(qdict, "string"));
    g_assert(qstring);
    g_assert_cmpstr(qstring_get_str(qstring), ==, "foo");
}

static void test_visitor_out_union_flat(TestOutputVisitorData *data,
                                        const void *unused)
{
    QDict *qdict;

    UserDefFlatUnion *tmp = g_malloc0(sizeof(UserDefFlatUnion));
    tmp->enum1 = ENUM_ONE_VALUE1;
    tmp->string = g_strdup("str");
    tmp->integer = 41;
    tmp->u.value1.boolean = true;

    visit_type_UserDefFlatUnion(data->ov, NULL, &tmp, &error_abort);
    qdict = qobject_to(QDict, visitor_get(data));
    g_assert(qdict);
    g_assert_cmpstr(qdict_get_str(qdict, "enum1"), ==, "value1");
    g_assert_cmpstr(qdict_get_str(qdict, "string"), ==, "str");
    g_assert_cmpint(qdict_get_int(qdict, "integer"), ==, 41);
    g_assert_cmpint(qdict_get_bool(qdict, "boolean"), ==, true);

    qapi_free_UserDefFlatUnion(tmp);
}

static void test_visitor_out_alternate(TestOutputVisitorData *data,
                                       const void *unused)
{
    UserDefAlternate *tmp;
    QNum *qnum;
    QString *qstr;
    QDict *qdict;
    int64_t val;

    tmp = g_new0(UserDefAlternate, 1);
    tmp->type = QTYPE_QNUM;
    tmp->u.i = 42;

    visit_type_UserDefAlternate(data->ov, NULL, &tmp, &error_abort);
    qnum = qobject_to(QNum, visitor_get(data));
    g_assert(qnum);
    g_assert(qnum_get_try_int(qnum, &val));
    g_assert_cmpint(val, ==, 42);

    qapi_free_UserDefAlternate(tmp);

    visitor_reset(data);
    tmp = g_new0(UserDefAlternate, 1);
    tmp->type = QTYPE_QSTRING;
    tmp->u.e = ENUM_ONE_VALUE1;

    visit_type_UserDefAlternate(data->ov, NULL, &tmp, &error_abort);
    qstr = qobject_to(QString, visitor_get(data));
    g_assert(qstr);
    g_assert_cmpstr(qstring_get_str(qstr), ==, "value1");

    qapi_free_UserDefAlternate(tmp);

    visitor_reset(data);
    tmp = g_new0(UserDefAlternate, 1);
    tmp->type = QTYPE_QNULL;
    tmp->u.n = qnull();

    visit_type_UserDefAlternate(data->ov, NULL, &tmp, &error_abort);
    g_assert_cmpint(qobject_type(visitor_get(data)), ==, QTYPE_QNULL);

    qapi_free_UserDefAlternate(tmp);

    visitor_reset(data);
    tmp = g_new0(UserDefAlternate, 1);
    tmp->type = QTYPE_QDICT;
    tmp->u.udfu.integer = 1;
    tmp->u.udfu.string = g_strdup("str");
    tmp->u.udfu.enum1 = ENUM_ONE_VALUE1;
    tmp->u.udfu.u.value1.boolean = true;

    visit_type_UserDefAlternate(data->ov, NULL, &tmp, &error_abort);
    qdict = qobject_to(QDict, visitor_get(data));
    g_assert(qdict);
    g_assert_cmpint(qdict_size(qdict), ==, 4);
    g_assert_cmpint(qdict_get_int(qdict, "integer"), ==, 1);
    g_assert_cmpstr(qdict_get_str(qdict, "string"), ==, "str");
    g_assert_cmpstr(qdict_get_str(qdict, "enum1"), ==, "value1");
    g_assert_cmpint(qdict_get_bool(qdict, "boolean"), ==, true);

    qapi_free_UserDefAlternate(tmp);
}

static void test_visitor_out_null(TestOutputVisitorData *data,
                                  const void *unused)
{
    QNull *null = NULL;
    QDict *qdict;
    QObject *nil;

    visit_start_struct(data->ov, NULL, NULL, 0, &error_abort);
    visit_type_null(data->ov, "a", &null, &error_abort);
    visit_check_struct(data->ov, &error_abort);
    visit_end_struct(data->ov, NULL);
    qdict = qobject_to(QDict, visitor_get(data));
    g_assert(qdict);
    g_assert_cmpint(qdict_size(qdict), ==, 1);
    nil = qdict_get(qdict, "a");
    g_assert(nil);
    g_assert(qobject_type(nil) == QTYPE_QNULL);
}

static void init_list_union(UserDefListUnion *cvalue)
{
    int i;
    switch (cvalue->type) {
    case USER_DEF_LIST_UNION_KIND_INTEGER: {
        intList **list = &cvalue->u.integer.data;
        for (i = 0; i < 32; i++) {
            *list = g_new0(intList, 1);
            (*list)->value = i;
            (*list)->next = NULL;
            list = &(*list)->next;
        }
        break;
    }
    case USER_DEF_LIST_UNION_KIND_S8: {
        int8List **list = &cvalue->u.s8.data;
        for (i = 0; i < 32; i++) {
            *list = g_new0(int8List, 1);
            (*list)->value = i;
            (*list)->next = NULL;
            list = &(*list)->next;
        }
        break;
    }
    case USER_DEF_LIST_UNION_KIND_S16: {
        int16List **list = &cvalue->u.s16.data;
        for (i = 0; i < 32; i++) {
            *list = g_new0(int16List, 1);
            (*list)->value = i;
            (*list)->next = NULL;
            list = &(*list)->next;
        }
        break;
    }
    case USER_DEF_LIST_UNION_KIND_S32: {
        int32List **list = &cvalue->u.s32.data;
        for (i = 0; i < 32; i++) {
            *list = g_new0(int32List, 1);
            (*list)->value = i;
            (*list)->next = NULL;
            list = &(*list)->next;
        }
        break;
    }
    case USER_DEF_LIST_UNION_KIND_S64: {
        int64List **list = &cvalue->u.s64.data;
        for (i = 0; i < 32; i++) {
            *list = g_new0(int64List, 1);
            (*list)->value = i;
            (*list)->next = NULL;
            list = &(*list)->next;
        }
        break;
    }
    case USER_DEF_LIST_UNION_KIND_U8: {
        uint8List **list = &cvalue->u.u8.data;
        for (i = 0; i < 32; i++) {
            *list = g_new0(uint8List, 1);
            (*list)->value = i;
            (*list)->next = NULL;
            list = &(*list)->next;
        }
        break;
    }
    case USER_DEF_LIST_UNION_KIND_U16: {
        uint16List **list = &cvalue->u.u16.data;
        for (i = 0; i < 32; i++) {
            *list = g_new0(uint16List, 1);
            (*list)->value = i;
            (*list)->next = NULL;
            list = &(*list)->next;
        }
        break;
    }
    case USER_DEF_LIST_UNION_KIND_U32: {
        uint32List **list = &cvalue->u.u32.data;
        for (i = 0; i < 32; i++) {
            *list = g_new0(uint32List, 1);
            (*list)->value = i;
            (*list)->next = NULL;
            list = &(*list)->next;
        }
        break;
    }
    case USER_DEF_LIST_UNION_KIND_U64: {
        uint64List **list = &cvalue->u.u64.data;
        for (i = 0; i < 32; i++) {
            *list = g_new0(uint64List, 1);
            (*list)->value = i;
            (*list)->next = NULL;
            list = &(*list)->next;
        }
        break;
    }
    case USER_DEF_LIST_UNION_KIND_BOOLEAN: {
        boolList **list = &cvalue->u.boolean.data;
        for (i = 0; i < 32; i++) {
            *list = g_new0(boolList, 1);
            (*list)->value = QEMU_IS_ALIGNED(i, 3);
            (*list)->next = NULL;
            list = &(*list)->next;
        }
        break;
    }
    case USER_DEF_LIST_UNION_KIND_STRING: {
        strList **list = &cvalue->u.string.data;
        for (i = 0; i < 32; i++) {
            *list = g_new0(strList, 1);
            (*list)->value = g_strdup_printf("%d", i);
            (*list)->next = NULL;
            list = &(*list)->next;
        }
        break;
    }
    case USER_DEF_LIST_UNION_KIND_NUMBER: {
        numberList **list = &cvalue->u.number.data;
        for (i = 0; i < 32; i++) {
            *list = g_new0(numberList, 1);
            (*list)->value = (double)i / 3;
            (*list)->next = NULL;
            list = &(*list)->next;
        }
        break;
    }
    default:
        g_assert_not_reached();
    }
}

static void check_list_union(QObject *qobj,
                             UserDefListUnionKind kind)
{
    QDict *qdict;
    QList *qlist;
    int i;

    qdict = qobject_to(QDict, qobj);
    g_assert(qdict);
    g_assert(qdict_haskey(qdict, "data"));
    qlist = qlist_copy(qobject_to(QList, qdict_get(qdict, "data")));

    switch (kind) {
    case USER_DEF_LIST_UNION_KIND_U8:
    case USER_DEF_LIST_UNION_KIND_U16:
    case USER_DEF_LIST_UNION_KIND_U32:
    case USER_DEF_LIST_UNION_KIND_U64:
        for (i = 0; i < 32; i++) {
            QObject *tmp;
            QNum *qvalue;
            uint64_t val;

            tmp = qlist_peek(qlist);
            g_assert(tmp);
            qvalue = qobject_to(QNum, tmp);
            g_assert(qnum_get_try_uint(qvalue, &val));
            g_assert_cmpint(val, ==, i);
            qobject_unref(qlist_pop(qlist));
        }
        break;

    case USER_DEF_LIST_UNION_KIND_S8:
    case USER_DEF_LIST_UNION_KIND_S16:
    case USER_DEF_LIST_UNION_KIND_S32:
    case USER_DEF_LIST_UNION_KIND_S64:
        /*
         * All integer elements in JSON arrays get stored into QNums
         * when we convert to QObjects, so we can check them all in
         * the same fashion, so simply fall through here.
         */
    case USER_DEF_LIST_UNION_KIND_INTEGER:
        for (i = 0; i < 32; i++) {
            QObject *tmp;
            QNum *qvalue;
            int64_t val;

            tmp = qlist_peek(qlist);
            g_assert(tmp);
            qvalue = qobject_to(QNum, tmp);
            g_assert(qnum_get_try_int(qvalue, &val));
            g_assert_cmpint(val, ==, i);
            qobject_unref(qlist_pop(qlist));
        }
        break;
    case USER_DEF_LIST_UNION_KIND_BOOLEAN:
        for (i = 0; i < 32; i++) {
            QObject *tmp;
            QBool *qvalue;
            tmp = qlist_peek(qlist);
            g_assert(tmp);
            qvalue = qobject_to(QBool, tmp);
            g_assert_cmpint(qbool_get_bool(qvalue), ==, i % 3 == 0);
            qobject_unref(qlist_pop(qlist));
        }
        break;
    case USER_DEF_LIST_UNION_KIND_STRING:
        for (i = 0; i < 32; i++) {
            QObject *tmp;
            QString *qvalue;
            gchar str[8];
            tmp = qlist_peek(qlist);
            g_assert(tmp);
            qvalue = qobject_to(QString, tmp);
            sprintf(str, "%d", i);
            g_assert_cmpstr(qstring_get_str(qvalue), ==, str);
            qobject_unref(qlist_pop(qlist));
        }
        break;
    case USER_DEF_LIST_UNION_KIND_NUMBER:
        for (i = 0; i < 32; i++) {
            QObject *tmp;
            QNum *qvalue;
            GString *double_expected = g_string_new("");
            GString *double_actual = g_string_new("");

            tmp = qlist_peek(qlist);
            g_assert(tmp);
            qvalue = qobject_to(QNum, tmp);
            g_string_printf(double_expected, "%.6f", (double)i / 3);
            g_string_printf(double_actual, "%.6f", qnum_get_double(qvalue));
            g_assert_cmpstr(double_actual->str, ==, double_expected->str);

            qobject_unref(qlist_pop(qlist));
            g_string_free(double_expected, true);
            g_string_free(double_actual, true);
        }
        break;
    default:
        g_assert_not_reached();
    }
    qobject_unref(qlist);
}

static void test_list_union(TestOutputVisitorData *data,
                            const void *unused,
                            UserDefListUnionKind kind)
{
    UserDefListUnion *cvalue = g_new0(UserDefListUnion, 1);
    QObject *obj;

    cvalue->type = kind;
    init_list_union(cvalue);

    visit_type_UserDefListUnion(data->ov, NULL, &cvalue, &error_abort);

    obj = visitor_get(data);
    check_list_union(obj, cvalue->type);
    qapi_free_UserDefListUnion(cvalue);
}

static void test_visitor_out_list_union_int(TestOutputVisitorData *data,
                                            const void *unused)
{
    test_list_union(data, unused, USER_DEF_LIST_UNION_KIND_INTEGER);
}

static void test_visitor_out_list_union_int8(TestOutputVisitorData *data,
                                             const void *unused)
{
    test_list_union(data, unused, USER_DEF_LIST_UNION_KIND_S8);
}

static void test_visitor_out_list_union_int16(TestOutputVisitorData *data,
                                              const void *unused)
{
    test_list_union(data, unused, USER_DEF_LIST_UNION_KIND_S16);
}

static void test_visitor_out_list_union_int32(TestOutputVisitorData *data,
                                              const void *unused)
{
    test_list_union(data, unused, USER_DEF_LIST_UNION_KIND_S32);
}

static void test_visitor_out_list_union_int64(TestOutputVisitorData *data,
                                              const void *unused)
{
    test_list_union(data, unused, USER_DEF_LIST_UNION_KIND_S64);
}

static void test_visitor_out_list_union_uint8(TestOutputVisitorData *data,
                                              const void *unused)
{
    test_list_union(data, unused, USER_DEF_LIST_UNION_KIND_U8);
}

static void test_visitor_out_list_union_uint16(TestOutputVisitorData *data,
                                               const void *unused)
{
    test_list_union(data, unused, USER_DEF_LIST_UNION_KIND_U16);
}

static void test_visitor_out_list_union_uint32(TestOutputVisitorData *data,
                                               const void *unused)
{
    test_list_union(data, unused, USER_DEF_LIST_UNION_KIND_U32);
}

static void test_visitor_out_list_union_uint64(TestOutputVisitorData *data,
                                               const void *unused)
{
    test_list_union(data, unused, USER_DEF_LIST_UNION_KIND_U64);
}

static void test_visitor_out_list_union_bool(TestOutputVisitorData *data,
                                             const void *unused)
{
    test_list_union(data, unused, USER_DEF_LIST_UNION_KIND_BOOLEAN);
}

static void test_visitor_out_list_union_str(TestOutputVisitorData *data,
                                            const void *unused)
{
    test_list_union(data, unused, USER_DEF_LIST_UNION_KIND_STRING);
}

static void test_visitor_out_list_union_number(TestOutputVisitorData *data,
                                               const void *unused)
{
    test_list_union(data, unused, USER_DEF_LIST_UNION_KIND_NUMBER);
}

static void output_visitor_test_add(const char *testpath,
                                    TestOutputVisitorData *data,
                                    void (*test_func)(TestOutputVisitorData *data, const void *user_data))
{
    g_test_add(testpath, TestOutputVisitorData, data, visitor_output_setup,
               test_func, visitor_output_teardown);
}

int main(int argc, char **argv)
{
    TestOutputVisitorData out_visitor_data;

    g_test_init(&argc, &argv, NULL);

    output_visitor_test_add("/visitor/output/int",
                            &out_visitor_data, test_visitor_out_int);
    output_visitor_test_add("/visitor/output/bool",
                            &out_visitor_data, test_visitor_out_bool);
    output_visitor_test_add("/visitor/output/number",
                            &out_visitor_data, test_visitor_out_number);
    output_visitor_test_add("/visitor/output/string",
                            &out_visitor_data, test_visitor_out_string);
    output_visitor_test_add("/visitor/output/no-string",
                            &out_visitor_data, test_visitor_out_no_string);
    output_visitor_test_add("/visitor/output/enum",
                            &out_visitor_data, test_visitor_out_enum);
    output_visitor_test_add("/visitor/output/enum-errors",
                            &out_visitor_data, test_visitor_out_enum_errors);
    output_visitor_test_add("/visitor/output/struct",
                            &out_visitor_data, test_visitor_out_struct);
    output_visitor_test_add("/visitor/output/struct-nested",
                            &out_visitor_data, test_visitor_out_struct_nested);
    output_visitor_test_add("/visitor/output/struct-errors",
                            &out_visitor_data, test_visitor_out_struct_errors);
    output_visitor_test_add("/visitor/output/list",
                            &out_visitor_data, test_visitor_out_list);
    output_visitor_test_add("/visitor/output/any",
                            &out_visitor_data, test_visitor_out_any);
    output_visitor_test_add("/visitor/output/list-qapi-free",
                            &out_visitor_data, test_visitor_out_list_qapi_free);
    output_visitor_test_add("/visitor/output/union-flat",
                            &out_visitor_data, test_visitor_out_union_flat);
    output_visitor_test_add("/visitor/output/alternate",
                            &out_visitor_data, test_visitor_out_alternate);
    output_visitor_test_add("/visitor/output/null",
                            &out_visitor_data, test_visitor_out_null);
    output_visitor_test_add("/visitor/output/list_union/int",
                            &out_visitor_data,
                            test_visitor_out_list_union_int);
    output_visitor_test_add("/visitor/output/list_union/int8",
                            &out_visitor_data,
                            test_visitor_out_list_union_int8);
    output_visitor_test_add("/visitor/output/list_union/int16",
                            &out_visitor_data,
                            test_visitor_out_list_union_int16);
    output_visitor_test_add("/visitor/output/list_union/int32",
                            &out_visitor_data,
                            test_visitor_out_list_union_int32);
    output_visitor_test_add("/visitor/output/list_union/int64",
                            &out_visitor_data,
                            test_visitor_out_list_union_int64);
    output_visitor_test_add("/visitor/output/list_union/uint8",
                            &out_visitor_data,
                            test_visitor_out_list_union_uint8);
    output_visitor_test_add("/visitor/output/list_union/uint16",
                            &out_visitor_data,
                            test_visitor_out_list_union_uint16);
    output_visitor_test_add("/visitor/output/list_union/uint32",
                            &out_visitor_data,
                            test_visitor_out_list_union_uint32);
    output_visitor_test_add("/visitor/output/list_union/uint64",
                            &out_visitor_data,
                            test_visitor_out_list_union_uint64);
    output_visitor_test_add("/visitor/output/list_union/bool",
                            &out_visitor_data,
                            test_visitor_out_list_union_bool);
    output_visitor_test_add("/visitor/output/list_union/string",
                            &out_visitor_data,
                            test_visitor_out_list_union_str);
    output_visitor_test_add("/visitor/output/list_union/number",
                            &out_visitor_data,
                            test_visitor_out_list_union_number);

    g_test_run();

    return 0;
}
