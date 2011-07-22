#include <glib.h>
#include "qapi/qmp-output-visitor.h"
#include "qapi/qmp-input-visitor.h"
#include "test-qapi-types.h"
#include "test-qapi-visit.h"
#include "qemu-objects.h"

typedef struct TestStruct
{
    int64_t x;
    int64_t y;
} TestStruct;

typedef struct TestStructList
{
    TestStruct *value;
    struct TestStructList *next;
} TestStructList;

static void visit_type_TestStruct(Visitor *v, TestStruct **obj, const char *name, Error **errp)
{
    visit_start_struct(v, (void **)obj, "TestStruct", name, sizeof(TestStruct), errp);
    visit_type_int(v, &(*obj)->x, "x", errp);
    visit_type_int(v, &(*obj)->y, "y", errp);
    visit_end_struct(v, errp);
}

static void visit_type_TestStructList(Visitor *m, TestStructList ** obj, const char *name, Error **errp)
{
    GenericList *i;

    visit_start_list(m, name, errp);

    for (i = visit_next_list(m, (GenericList **)obj, errp); i; i = visit_next_list(m, &i, errp)) {
        TestStructList *native_i = (TestStructList *)i;
        visit_type_TestStruct(m, &native_i->value, NULL, errp);
    }

    visit_end_list(m, errp);
}

/* test core visitor methods */
static void test_visitor_core(void)
{
    QmpOutputVisitor *mo;
    QmpInputVisitor *mi;
    Visitor *v;
    TestStruct ts = { 42, 82 };
    TestStruct *pts = &ts;
    TestStructList *lts = NULL;
    Error *err = NULL;
    QObject *obj;
    QString *str;
    int64_t value = 0;

    mo = qmp_output_visitor_new();
    v = qmp_output_get_visitor(mo);

    visit_type_TestStruct(v, &pts, NULL, &err);

    obj = qmp_output_get_qobject(mo);

    str = qobject_to_json(obj);

    printf("%s\n", qstring_get_str(str));

    QDECREF(str);

    obj = QOBJECT(qint_from_int(0x42));

    mi = qmp_input_visitor_new(obj);
    v = qmp_input_get_visitor(mi);

    visit_type_int(v, &value, NULL, &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }

    g_assert(value == 0x42);

    qobject_decref(obj);

    obj = qobject_from_json("{'x': 42, 'y': 84}");
    mi = qmp_input_visitor_new(obj);
    v = qmp_input_get_visitor(mi);

    pts = NULL;

    visit_type_TestStruct(v, &pts, NULL, &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }

    g_assert(pts != NULL);
    g_assert(pts->x == 42);
    g_assert(pts->y == 84);

    qobject_decref(obj);

    obj = qobject_from_json("[{'x': 42, 'y': 84}, {'x': 12, 'y': 24}]");
    mi = qmp_input_visitor_new(obj);
    v = qmp_input_get_visitor(mi);

    visit_type_TestStructList(v, &lts, NULL, &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }

    g_assert(lts != NULL);
    g_assert(lts->value->x == 42);
    g_assert(lts->value->y == 84);

    lts = lts->next;
    g_assert(lts != NULL);
    g_assert(lts->value->x == 12);
    g_assert(lts->value->y == 24);

    g_assert(lts->next == NULL);

    qobject_decref(obj);
}

/* test deep nesting with refs to other user-defined types */
static void test_nested_structs(void)
{
    QmpOutputVisitor *mo;
    QmpInputVisitor *mi;
    Visitor *v;
    UserDefOne ud1;
    UserDefOne *ud1_p = &ud1, *ud1c_p = NULL;
    UserDefTwo ud2;
    UserDefTwo *ud2_p = &ud2, *ud2c_p = NULL;
    Error *err = NULL;
    QObject *obj;
    QString *str;

    ud1.integer = 42;
    ud1.string = strdup("fourty two");

    /* sanity check */
    mo = qmp_output_visitor_new();
    v = qmp_output_get_visitor(mo);
    visit_type_UserDefOne(v, &ud1_p, "o_O", &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }
    obj = qmp_output_get_qobject(mo);
    g_assert(obj);
    qobject_decref(obj);

    ud2.string = strdup("fourty three");
    ud2.dict.string = strdup("fourty four");
    ud2.dict.dict.userdef = ud1_p;
    ud2.dict.dict.string = strdup("fourty five");
    ud2.dict.has_dict2 = true;
    ud2.dict.dict2.userdef = ud1_p;
    ud2.dict.dict2.string = strdup("fourty six");

    /* c type -> qobject */
    mo = qmp_output_visitor_new();
    v = qmp_output_get_visitor(mo);
    visit_type_UserDefTwo(v, &ud2_p, "unused", &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }
    obj = qmp_output_get_qobject(mo);
    g_assert(obj);
    str = qobject_to_json_pretty(obj);
    g_print("%s\n", qstring_get_str(str));
    QDECREF(str);

    /* qobject -> c type, should match original struct */
    mi = qmp_input_visitor_new(obj);
    v = qmp_input_get_visitor(mi);
    visit_type_UserDefTwo(v, &ud2c_p, NULL, &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }

    g_assert(!g_strcmp0(ud2c_p->string, ud2.string));
    g_assert(!g_strcmp0(ud2c_p->dict.string, ud2.dict.string));

    ud1c_p = ud2c_p->dict.dict.userdef;
    g_assert(ud1c_p->integer == ud1_p->integer);
    g_assert(!g_strcmp0(ud1c_p->string, ud1_p->string));

    g_assert(!g_strcmp0(ud2c_p->dict.dict.string, ud2.dict.dict.string));

    ud1c_p = ud2c_p->dict.dict2.userdef;
    g_assert(ud1c_p->integer == ud1_p->integer);
    g_assert(!g_strcmp0(ud1c_p->string, ud1_p->string));

    g_assert(!g_strcmp0(ud2c_p->dict.dict2.string, ud2.dict.dict2.string));
    qemu_free(ud1.string);
    qemu_free(ud2.string);
    qemu_free(ud2.dict.string);
    qemu_free(ud2.dict.dict.string);
    qemu_free(ud2.dict.dict2.string);

    qapi_free_UserDefTwo(ud2c_p);

    qobject_decref(obj);
}

/* test enum values */
static void test_enums(void)
{
    QmpOutputVisitor *mo;
    QmpInputVisitor *mi;
    Visitor *v;
    EnumOne enum1 = ENUM_ONE_VALUE2, enum1_cpy = ENUM_ONE_VALUE1;
    Error *err = NULL;
    QObject *obj;
    QString *str;

    /* C type -> QObject */
    mo = qmp_output_visitor_new();
    v = qmp_output_get_visitor(mo);
    visit_type_EnumOne(v, &enum1, "unused", &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }
    obj = qmp_output_get_qobject(mo);
    g_assert(obj);
    str = qobject_to_json_pretty(obj);
    g_print("%s\n", qstring_get_str(str));
    QDECREF(str);
    g_assert(g_strcmp0(qstring_get_str(qobject_to_qstring(obj)), "value2") == 0);

    /* QObject -> C type */
    mi = qmp_input_visitor_new(obj);
    v = qmp_input_get_visitor(mi);
    visit_type_EnumOne(v, &enum1_cpy, "unused", &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }
    g_debug("enum1_cpy, enum1: %d, %d", enum1_cpy, enum1);
    g_assert(enum1_cpy == enum1);

    qobject_decref(obj);
}

/* test enum values nested in schema-defined structs */
static void test_nested_enums(void)
{
    QmpOutputVisitor *mo;
    QmpInputVisitor *mi;
    Visitor *v;
    NestedEnumsOne *nested_enums, *nested_enums_cpy = NULL;
    Error *err = NULL;
    QObject *obj;
    QString *str;

    nested_enums = qemu_mallocz(sizeof(NestedEnumsOne));
    nested_enums->enum1 = ENUM_ONE_VALUE1;
    nested_enums->enum2 = ENUM_ONE_VALUE2;
    nested_enums->enum3 = ENUM_ONE_VALUE3;
    nested_enums->enum4 = ENUM_ONE_VALUE3;
    nested_enums->has_enum2 = false;
    nested_enums->has_enum4 = true;

    /* C type -> QObject */
    mo = qmp_output_visitor_new();
    v = qmp_output_get_visitor(mo);
    visit_type_NestedEnumsOne(v, &nested_enums, NULL, &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }
    obj = qmp_output_get_qobject(mo);
    g_assert(obj);
    str = qobject_to_json_pretty(obj);
    g_print("%s\n", qstring_get_str(str));
    QDECREF(str);

    /* QObject -> C type */
    mi = qmp_input_visitor_new(obj);
    v = qmp_input_get_visitor(mi);
    visit_type_NestedEnumsOne(v, &nested_enums_cpy, NULL, &err);
    if (err) {
        g_error("%s", error_get_pretty(err));
    }
    g_assert(nested_enums_cpy);
    g_assert(nested_enums_cpy->enum1 == nested_enums->enum1);
    g_assert(nested_enums_cpy->enum3 == nested_enums->enum3);
    g_assert(nested_enums_cpy->enum4 == nested_enums->enum4);
    g_assert(nested_enums_cpy->has_enum2 == false);
    g_assert(nested_enums_cpy->has_enum4 == true);

    qobject_decref(obj);
    qapi_free_NestedEnumsOne(nested_enums);
    qapi_free_NestedEnumsOne(nested_enums_cpy);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/0.15/visitor_core", test_visitor_core);
    g_test_add_func("/0.15/nested_structs", test_nested_structs);
    g_test_add_func("/0.15/enums", test_enums);
    g_test_add_func("/0.15/nested_enums", test_nested_enums);

    g_test_run();

    return 0;
}
