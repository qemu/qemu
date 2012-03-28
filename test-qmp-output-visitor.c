/*
 * QMP Output Visitor unit-tests.
 *
 * Copyright (C) 2011 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>

#include "qapi/qmp-output-visitor.h"
#include "test-qapi-types.h"
#include "test-qapi-visit.h"
#include "qemu-objects.h"

typedef struct TestOutputVisitorData {
    QmpOutputVisitor *qov;
    Visitor *ov;
} TestOutputVisitorData;

static void visitor_output_setup(TestOutputVisitorData *data,
                                 const void *unused)
{
    data->qov = qmp_output_visitor_new();
    g_assert(data->qov != NULL);

    data->ov = qmp_output_get_visitor(data->qov);
    g_assert(data->ov != NULL);
}

static void visitor_output_teardown(TestOutputVisitorData *data,
                                    const void *unused)
{
    qmp_output_visitor_cleanup(data->qov);
    data->qov = NULL;
    data->ov = NULL;
}

static void test_visitor_out_int(TestOutputVisitorData *data,
                                 const void *unused)
{
    int64_t value = -42;
    Error *errp = NULL;
    QObject *obj;

    visit_type_int(data->ov, &value, NULL, &errp);
    g_assert(error_is_set(&errp) == 0);

    obj = qmp_output_get_qobject(data->qov);
    g_assert(obj != NULL);
    g_assert(qobject_type(obj) == QTYPE_QINT);
    g_assert_cmpint(qint_get_int(qobject_to_qint(obj)), ==, value);

    qobject_decref(obj);
}

static void test_visitor_out_bool(TestOutputVisitorData *data,
                                  const void *unused)
{
    Error *errp = NULL;
    bool value = true;
    QObject *obj;

    visit_type_bool(data->ov, &value, NULL, &errp);
    g_assert(error_is_set(&errp) == 0);

    obj = qmp_output_get_qobject(data->qov);
    g_assert(obj != NULL);
    g_assert(qobject_type(obj) == QTYPE_QBOOL);
    g_assert(qbool_get_int(qobject_to_qbool(obj)) == value);

    qobject_decref(obj);
}

static void test_visitor_out_number(TestOutputVisitorData *data,
                                    const void *unused)
{
    double value = 3.14;
    Error *errp = NULL;
    QObject *obj;

    visit_type_number(data->ov, &value, NULL, &errp);
    g_assert(error_is_set(&errp) == 0);

    obj = qmp_output_get_qobject(data->qov);
    g_assert(obj != NULL);
    g_assert(qobject_type(obj) == QTYPE_QFLOAT);
    g_assert(qfloat_get_double(qobject_to_qfloat(obj)) == value);

    qobject_decref(obj);
}

static void test_visitor_out_string(TestOutputVisitorData *data,
                                    const void *unused)
{
    char *string = (char *) "Q E M U";
    Error *errp = NULL;
    QObject *obj;

    visit_type_str(data->ov, &string, NULL, &errp);
    g_assert(error_is_set(&errp) == 0);

    obj = qmp_output_get_qobject(data->qov);
    g_assert(obj != NULL);
    g_assert(qobject_type(obj) == QTYPE_QSTRING);
    g_assert_cmpstr(qstring_get_str(qobject_to_qstring(obj)), ==, string);

    qobject_decref(obj);
}

static void test_visitor_out_no_string(TestOutputVisitorData *data,
                                       const void *unused)
{
    char *string = NULL;
    Error *errp = NULL;
    QObject *obj;

    /* A null string should return "" */
    visit_type_str(data->ov, &string, NULL, &errp);
    g_assert(error_is_set(&errp) == 0);

    obj = qmp_output_get_qobject(data->qov);
    g_assert(obj != NULL);
    g_assert(qobject_type(obj) == QTYPE_QSTRING);
    g_assert_cmpstr(qstring_get_str(qobject_to_qstring(obj)), ==, "");

    qobject_decref(obj);
}

static void test_visitor_out_enum(TestOutputVisitorData *data,
                                  const void *unused)
{
    Error *errp = NULL;
    QObject *obj;
    EnumOne i;

    for (i = 0; i < ENUM_ONE_MAX; i++) {
        visit_type_EnumOne(data->ov, &i, "unused", &errp);
        g_assert(!error_is_set(&errp));

        obj = qmp_output_get_qobject(data->qov);
        g_assert(obj != NULL);
        g_assert(qobject_type(obj) == QTYPE_QSTRING);
        g_assert_cmpstr(qstring_get_str(qobject_to_qstring(obj)), ==,
                        EnumOne_lookup[i]);
        qobject_decref(obj);
    }
}

static void test_visitor_out_enum_errors(TestOutputVisitorData *data,
                                         const void *unused)
{
    EnumOne i, bad_values[] = { ENUM_ONE_MAX, -1 };
    Error *errp;

    for (i = 0; i < ARRAY_SIZE(bad_values) ; i++) {
        errp = NULL;
        visit_type_EnumOne(data->ov, &bad_values[i], "unused", &errp);
        g_assert(error_is_set(&errp) == true);
        error_free(errp);
    }
}

typedef struct TestStruct
{
    int64_t integer;
    bool boolean;
    char *string;
} TestStruct;

static void visit_type_TestStruct(Visitor *v, TestStruct **obj,
                                  const char *name, Error **errp)
{
    visit_start_struct(v, (void **)obj, "TestStruct", name, sizeof(TestStruct),
                       errp);

    visit_type_int(v, &(*obj)->integer, "integer", errp);
    visit_type_bool(v, &(*obj)->boolean, "boolean", errp);
    visit_type_str(v, &(*obj)->string, "string", errp);

    visit_end_struct(v, errp);
}

static void test_visitor_out_struct(TestOutputVisitorData *data,
                                    const void *unused)
{
    TestStruct test_struct = { .integer = 42,
                               .boolean = false,
                               .string = (char *) "foo"};
    TestStruct *p = &test_struct;
    Error *errp = NULL;
    QObject *obj;
    QDict *qdict;

    visit_type_TestStruct(data->ov, &p, NULL, &errp);
    g_assert(!error_is_set(&errp));

    obj = qmp_output_get_qobject(data->qov);
    g_assert(obj != NULL);
    g_assert(qobject_type(obj) == QTYPE_QDICT);

    qdict = qobject_to_qdict(obj);
    g_assert_cmpint(qdict_size(qdict), ==, 3);
    g_assert_cmpint(qdict_get_int(qdict, "integer"), ==, 42);
    g_assert_cmpint(qdict_get_bool(qdict, "boolean"), ==, 0);
    g_assert_cmpstr(qdict_get_str(qdict, "string"), ==, "foo");

    QDECREF(qdict);
}

static void test_visitor_out_struct_nested(TestOutputVisitorData *data,
                                           const void *unused)
{
    int64_t value = 42;
    Error *errp = NULL;
    UserDefNested *ud2;
    QObject *obj;
    QDict *qdict, *dict1, *dict2, *dict3, *userdef;
    const char *string = "user def string";
    const char *strings[] = { "forty two", "forty three", "forty four",
                              "forty five" };

    ud2 = g_malloc0(sizeof(*ud2));
    ud2->string0 = g_strdup(strings[0]);

    ud2->dict1.string1 = g_strdup(strings[1]);
    ud2->dict1.dict2.userdef1 = g_malloc0(sizeof(UserDefOne));
    ud2->dict1.dict2.userdef1->string = g_strdup(string);
    ud2->dict1.dict2.userdef1->integer = value;
    ud2->dict1.dict2.string2 = g_strdup(strings[2]);

    ud2->dict1.has_dict3 = true;
    ud2->dict1.dict3.userdef2 = g_malloc0(sizeof(UserDefOne));
    ud2->dict1.dict3.userdef2->string = g_strdup(string);
    ud2->dict1.dict3.userdef2->integer = value;
    ud2->dict1.dict3.string3 = g_strdup(strings[3]);

    visit_type_UserDefNested(data->ov, &ud2, "unused", &errp);
    g_assert(!error_is_set(&errp));

    obj = qmp_output_get_qobject(data->qov);
    g_assert(obj != NULL);
    g_assert(qobject_type(obj) == QTYPE_QDICT);

    qdict = qobject_to_qdict(obj);
    g_assert_cmpint(qdict_size(qdict), ==, 2);
    g_assert_cmpstr(qdict_get_str(qdict, "string0"), ==, strings[0]);

    dict1 = qdict_get_qdict(qdict, "dict1");
    g_assert_cmpint(qdict_size(dict1), ==, 3);
    g_assert_cmpstr(qdict_get_str(dict1, "string1"), ==, strings[1]);

    dict2 = qdict_get_qdict(dict1, "dict2");
    g_assert_cmpint(qdict_size(dict2), ==, 2);
    g_assert_cmpstr(qdict_get_str(dict2, "string2"), ==, strings[2]);
    userdef = qdict_get_qdict(dict2, "userdef1");
    g_assert_cmpint(qdict_size(userdef), ==, 2);
    g_assert_cmpint(qdict_get_int(userdef, "integer"), ==, value);
    g_assert_cmpstr(qdict_get_str(userdef, "string"), ==, string);

    dict3 = qdict_get_qdict(dict1, "dict3");
    g_assert_cmpint(qdict_size(dict3), ==, 2);
    g_assert_cmpstr(qdict_get_str(dict3, "string3"), ==, strings[3]);
    userdef = qdict_get_qdict(dict3, "userdef2");
    g_assert_cmpint(qdict_size(userdef), ==, 2);
    g_assert_cmpint(qdict_get_int(userdef, "integer"), ==, value);
    g_assert_cmpstr(qdict_get_str(userdef, "string"), ==, string);

    QDECREF(qdict);
    qapi_free_UserDefNested(ud2);
}

static void test_visitor_out_struct_errors(TestOutputVisitorData *data,
                                           const void *unused)
{
    EnumOne bad_values[] = { ENUM_ONE_MAX, -1 };
    UserDefOne u = { 0 }, *pu = &u;
    Error *errp;
    int i;

    for (i = 0; i < ARRAY_SIZE(bad_values) ; i++) {
        errp = NULL;
        u.has_enum1 = true;
        u.enum1 = bad_values[i];
        visit_type_UserDefOne(data->ov, &pu, "unused", &errp);
        g_assert(error_is_set(&errp) == true);
        error_free(errp);
    }
}

typedef struct TestStructList
{
    TestStruct *value;
    struct TestStructList *next;
} TestStructList;

static void visit_type_TestStructList(Visitor *v, TestStructList **obj,
                                      const char *name, Error **errp)
{
    GenericList *i, **head = (GenericList **)obj;

    visit_start_list(v, name, errp);

    for (*head = i = visit_next_list(v, head, errp); i; i = visit_next_list(v, &i, errp)) {
        TestStructList *native_i = (TestStructList *)i;
        visit_type_TestStruct(v, &native_i->value, NULL, errp);
    }

    visit_end_list(v, errp);
}

static void test_visitor_out_list(TestOutputVisitorData *data,
                                  const void *unused)
{
    char *value_str = (char *) "list value";
    TestStructList *p, *head = NULL;
    const int max_items = 10;
    bool value_bool = true;
    int value_int = 10;
    Error *errp = NULL;
    QListEntry *entry;
    QObject *obj;
    QList *qlist;
    int i;

    for (i = 0; i < max_items; i++) {
        p = g_malloc0(sizeof(*p));
        p->value = g_malloc0(sizeof(*p->value));
        p->value->integer = value_int;
        p->value->boolean = value_bool;
        p->value->string = value_str;

        p->next = head;
        head = p;
    }

    visit_type_TestStructList(data->ov, &head, NULL, &errp);
    g_assert(!error_is_set(&errp));

    obj = qmp_output_get_qobject(data->qov);
    g_assert(obj != NULL);
    g_assert(qobject_type(obj) == QTYPE_QLIST);

    qlist = qobject_to_qlist(obj);
    g_assert(!qlist_empty(qlist));

    i = 0;
    QLIST_FOREACH_ENTRY(qlist, entry) {
        QDict *qdict;

        g_assert(qobject_type(entry->value) == QTYPE_QDICT);
        qdict = qobject_to_qdict(entry->value);
        g_assert_cmpint(qdict_size(qdict), ==, 3);
        g_assert_cmpint(qdict_get_int(qdict, "integer"), ==, value_int);
        g_assert_cmpint(qdict_get_bool(qdict, "boolean"), ==, value_bool);
        g_assert_cmpstr(qdict_get_str(qdict, "string"), ==, value_str);
        i++;
    }
    g_assert_cmpint(i, ==, max_items);

    QDECREF(qlist);

    for (p = head; p;) {
        TestStructList *tmp = p->next;
        g_free(p->value);
        g_free(p);
        p = tmp;
    }
}

static void test_visitor_out_list_qapi_free(TestOutputVisitorData *data,
                                            const void *unused)
{
    UserDefNestedList *p, *head = NULL;
    const char string[] = "foo bar";
    int i, max_count = 1024;

    for (i = 0; i < max_count; i++) {
        p = g_malloc0(sizeof(*p));
        p->value = g_malloc0(sizeof(*p->value));

        p->value->string0 = g_strdup(string);
        p->value->dict1.string1 = g_strdup(string);
        p->value->dict1.dict2.userdef1 = g_malloc0(sizeof(UserDefOne));
        p->value->dict1.dict2.userdef1->string = g_strdup(string);
        p->value->dict1.dict2.userdef1->integer = 42;
        p->value->dict1.dict2.string2 = g_strdup(string);
        p->value->dict1.has_dict3 = false;

        p->next = head;
        head = p;
    }

    qapi_free_UserDefNestedList(head);
}

static void test_visitor_out_union(TestOutputVisitorData *data,
                                   const void *unused)
{
    QObject *arg, *qvalue;
    QDict *qdict, *value;

    Error *err = NULL;

    UserDefUnion *tmp = g_malloc0(sizeof(UserDefUnion));
    tmp->kind = USER_DEF_UNION_KIND_A;
    tmp->a = g_malloc0(sizeof(UserDefA));
    tmp->a->boolean = true;

    visit_type_UserDefUnion(data->ov, &tmp, NULL, &err);
    g_assert(err == NULL);
    arg = qmp_output_get_qobject(data->qov);

    g_assert(qobject_type(arg) == QTYPE_QDICT);
    qdict = qobject_to_qdict(arg);

    g_assert_cmpstr(qdict_get_str(qdict, "type"), ==, "a");

    qvalue = qdict_get(qdict, "data");
    g_assert(data != NULL);
    g_assert(qobject_type(qvalue) == QTYPE_QDICT);
    value = qobject_to_qdict(qvalue);
    g_assert_cmpint(qdict_get_bool(value, "boolean"), ==, true);

    qapi_free_UserDefUnion(tmp);
    QDECREF(qdict);
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
    output_visitor_test_add("/visitor/output/list-qapi-free",
                            &out_visitor_data, test_visitor_out_list_qapi_free);
    output_visitor_test_add("/visitor/output/union",
                            &out_visitor_data, test_visitor_out_union);

    g_test_run();

    return 0;
}
