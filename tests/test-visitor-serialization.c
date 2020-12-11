/*
 * Unit-tests for visitor-based serialization
 *
 * Copyright (C) 2014-2015 Red Hat, Inc.
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Michael Roth <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <float.h>

#include "qemu-common.h"
#include "test-qapi-visit.h"
#include "qapi/error.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "qapi/string-input-visitor.h"
#include "qapi/string-output-visitor.h"
#include "qapi/dealloc-visitor.h"

enum PrimitiveTypeKind {
    PTYPE_STRING = 0,
    PTYPE_BOOLEAN,
    PTYPE_NUMBER,
    PTYPE_INTEGER,
    PTYPE_U8,
    PTYPE_U16,
    PTYPE_U32,
    PTYPE_U64,
    PTYPE_S8,
    PTYPE_S16,
    PTYPE_S32,
    PTYPE_S64,
    PTYPE_EOL,
};

typedef struct PrimitiveType {
    union {
        const char *string;
        bool boolean;
        double number;
        int64_t integer;
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;
        int8_t s8;
        int16_t s16;
        int32_t s32;
        int64_t s64;
    } value;
    enum PrimitiveTypeKind type;
    const char *description;
} PrimitiveType;

typedef struct PrimitiveList {
    union {
        strList *strings;
        boolList *booleans;
        numberList *numbers;
        intList *integers;
        int8List *s8_integers;
        int16List *s16_integers;
        int32List *s32_integers;
        int64List *s64_integers;
        uint8List *u8_integers;
        uint16List *u16_integers;
        uint32List *u32_integers;
        uint64List *u64_integers;
    } value;
    enum PrimitiveTypeKind type;
    const char *description;
} PrimitiveList;

/* test helpers */

typedef void (*VisitorFunc)(Visitor *v, void **native, Error **errp);

static void dealloc_helper(void *native_in, VisitorFunc visit, Error **errp)
{
    Visitor *v = qapi_dealloc_visitor_new();

    visit(v, &native_in, errp);

    visit_free(v);
}

static void visit_primitive_type(Visitor *v, void **native, Error **errp)
{
    PrimitiveType *pt = *native;
    switch(pt->type) {
    case PTYPE_STRING:
        visit_type_str(v, NULL, (char **)&pt->value.string, errp);
        break;
    case PTYPE_BOOLEAN:
        visit_type_bool(v, NULL, &pt->value.boolean, errp);
        break;
    case PTYPE_NUMBER:
        visit_type_number(v, NULL, &pt->value.number, errp);
        break;
    case PTYPE_INTEGER:
        visit_type_int(v, NULL, &pt->value.integer, errp);
        break;
    case PTYPE_U8:
        visit_type_uint8(v, NULL, &pt->value.u8, errp);
        break;
    case PTYPE_U16:
        visit_type_uint16(v, NULL, &pt->value.u16, errp);
        break;
    case PTYPE_U32:
        visit_type_uint32(v, NULL, &pt->value.u32, errp);
        break;
    case PTYPE_U64:
        visit_type_uint64(v, NULL, &pt->value.u64, errp);
        break;
    case PTYPE_S8:
        visit_type_int8(v, NULL, &pt->value.s8, errp);
        break;
    case PTYPE_S16:
        visit_type_int16(v, NULL, &pt->value.s16, errp);
        break;
    case PTYPE_S32:
        visit_type_int32(v, NULL, &pt->value.s32, errp);
        break;
    case PTYPE_S64:
        visit_type_int64(v, NULL, &pt->value.s64, errp);
        break;
    case PTYPE_EOL:
        g_assert_not_reached();
    }
}

static void visit_primitive_list(Visitor *v, void **native, Error **errp)
{
    PrimitiveList *pl = *native;
    switch (pl->type) {
    case PTYPE_STRING:
        visit_type_strList(v, NULL, &pl->value.strings, errp);
        break;
    case PTYPE_BOOLEAN:
        visit_type_boolList(v, NULL, &pl->value.booleans, errp);
        break;
    case PTYPE_NUMBER:
        visit_type_numberList(v, NULL, &pl->value.numbers, errp);
        break;
    case PTYPE_INTEGER:
        visit_type_intList(v, NULL, &pl->value.integers, errp);
        break;
    case PTYPE_S8:
        visit_type_int8List(v, NULL, &pl->value.s8_integers, errp);
        break;
    case PTYPE_S16:
        visit_type_int16List(v, NULL, &pl->value.s16_integers, errp);
        break;
    case PTYPE_S32:
        visit_type_int32List(v, NULL, &pl->value.s32_integers, errp);
        break;
    case PTYPE_S64:
        visit_type_int64List(v, NULL, &pl->value.s64_integers, errp);
        break;
    case PTYPE_U8:
        visit_type_uint8List(v, NULL, &pl->value.u8_integers, errp);
        break;
    case PTYPE_U16:
        visit_type_uint16List(v, NULL, &pl->value.u16_integers, errp);
        break;
    case PTYPE_U32:
        visit_type_uint32List(v, NULL, &pl->value.u32_integers, errp);
        break;
    case PTYPE_U64:
        visit_type_uint64List(v, NULL, &pl->value.u64_integers, errp);
        break;
    default:
        g_assert_not_reached();
    }
}


static TestStruct *struct_create(void)
{
    TestStruct *ts = g_malloc0(sizeof(*ts));
    ts->integer = -42;
    ts->boolean = true;
    ts->string = strdup("test string");
    return ts;
}

static void struct_compare(TestStruct *ts1, TestStruct *ts2)
{
    g_assert(ts1);
    g_assert(ts2);
    g_assert_cmpint(ts1->integer, ==, ts2->integer);
    g_assert(ts1->boolean == ts2->boolean);
    g_assert_cmpstr(ts1->string, ==, ts2->string);
}

static void struct_cleanup(TestStruct *ts)
{
    g_free(ts->string);
    g_free(ts);
}

static void visit_struct(Visitor *v, void **native, Error **errp)
{
    visit_type_TestStruct(v, NULL, (TestStruct **)native, errp);
}

static UserDefTwo *nested_struct_create(void)
{
    UserDefTwo *udnp = g_malloc0(sizeof(*udnp));
    udnp->string0 = strdup("test_string0");
    udnp->dict1 = g_malloc0(sizeof(*udnp->dict1));
    udnp->dict1->string1 = strdup("test_string1");
    udnp->dict1->dict2 = g_malloc0(sizeof(*udnp->dict1->dict2));
    udnp->dict1->dict2->userdef = g_new0(UserDefOne, 1);
    udnp->dict1->dict2->userdef->integer = 42;
    udnp->dict1->dict2->userdef->string = strdup("test_string");
    udnp->dict1->dict2->string = strdup("test_string2");
    udnp->dict1->dict3 = g_malloc0(sizeof(*udnp->dict1->dict3));
    udnp->dict1->has_dict3 = true;
    udnp->dict1->dict3->userdef = g_new0(UserDefOne, 1);
    udnp->dict1->dict3->userdef->integer = 43;
    udnp->dict1->dict3->userdef->string = strdup("test_string");
    udnp->dict1->dict3->string = strdup("test_string3");
    return udnp;
}

static void nested_struct_compare(UserDefTwo *udnp1, UserDefTwo *udnp2)
{
    g_assert(udnp1);
    g_assert(udnp2);
    g_assert_cmpstr(udnp1->string0, ==, udnp2->string0);
    g_assert_cmpstr(udnp1->dict1->string1, ==, udnp2->dict1->string1);
    g_assert_cmpint(udnp1->dict1->dict2->userdef->integer, ==,
                    udnp2->dict1->dict2->userdef->integer);
    g_assert_cmpstr(udnp1->dict1->dict2->userdef->string, ==,
                    udnp2->dict1->dict2->userdef->string);
    g_assert_cmpstr(udnp1->dict1->dict2->string, ==,
                    udnp2->dict1->dict2->string);
    g_assert(udnp1->dict1->has_dict3 == udnp2->dict1->has_dict3);
    g_assert_cmpint(udnp1->dict1->dict3->userdef->integer, ==,
                    udnp2->dict1->dict3->userdef->integer);
    g_assert_cmpstr(udnp1->dict1->dict3->userdef->string, ==,
                    udnp2->dict1->dict3->userdef->string);
    g_assert_cmpstr(udnp1->dict1->dict3->string, ==,
                    udnp2->dict1->dict3->string);
}

static void nested_struct_cleanup(UserDefTwo *udnp)
{
    qapi_free_UserDefTwo(udnp);
}

static void visit_nested_struct(Visitor *v, void **native, Error **errp)
{
    visit_type_UserDefTwo(v, NULL, (UserDefTwo **)native, errp);
}

static void visit_nested_struct_list(Visitor *v, void **native, Error **errp)
{
    visit_type_UserDefTwoList(v, NULL, (UserDefTwoList **)native, errp);
}

/* test cases */

typedef enum VisitorCapabilities {
    VCAP_PRIMITIVES = 1,
    VCAP_STRUCTURES = 2,
    VCAP_LISTS = 4,
    VCAP_PRIMITIVE_LISTS = 8,
} VisitorCapabilities;

typedef struct SerializeOps {
    void (*serialize)(void *native_in, void **datap,
                      VisitorFunc visit, Error **errp);
    void (*deserialize)(void **native_out, void *datap,
                            VisitorFunc visit, Error **errp);
    void (*cleanup)(void *datap);
    const char *type;
    VisitorCapabilities caps;
} SerializeOps;

typedef struct TestArgs {
    const SerializeOps *ops;
    void *test_data;
} TestArgs;

static void test_primitives(gconstpointer opaque)
{
    TestArgs *args = (TestArgs *) opaque;
    const SerializeOps *ops = args->ops;
    PrimitiveType *pt = args->test_data;
    PrimitiveType *pt_copy = g_malloc0(sizeof(*pt_copy));
    void *serialize_data;

    pt_copy->type = pt->type;
    ops->serialize(pt, &serialize_data, visit_primitive_type, &error_abort);
    ops->deserialize((void **)&pt_copy, serialize_data, visit_primitive_type,
                     &error_abort);

    g_assert(pt_copy != NULL);
    switch (pt->type) {
    case PTYPE_STRING:
        g_assert_cmpstr(pt->value.string, ==, pt_copy->value.string);
        g_free((char *)pt_copy->value.string);
        break;
    case PTYPE_BOOLEAN:
        g_assert_cmpint(pt->value.boolean, ==, pt->value.boolean);
        break;
    case PTYPE_NUMBER:
        g_assert_cmpfloat(pt->value.number, ==, pt_copy->value.number);
        break;
    case PTYPE_INTEGER:
        g_assert_cmpint(pt->value.integer, ==, pt_copy->value.integer);
        break;
    case PTYPE_U8:
        g_assert_cmpuint(pt->value.u8, ==, pt_copy->value.u8);
        break;
    case PTYPE_U16:
        g_assert_cmpuint(pt->value.u16, ==, pt_copy->value.u16);
        break;
    case PTYPE_U32:
        g_assert_cmpuint(pt->value.u32, ==, pt_copy->value.u32);
        break;
    case PTYPE_U64:
        g_assert_cmpuint(pt->value.u64, ==, pt_copy->value.u64);
        break;
    case PTYPE_S8:
        g_assert_cmpint(pt->value.s8, ==, pt_copy->value.s8);
        break;
    case PTYPE_S16:
        g_assert_cmpint(pt->value.s16, ==, pt_copy->value.s16);
        break;
    case PTYPE_S32:
        g_assert_cmpint(pt->value.s32, ==, pt_copy->value.s32);
        break;
    case PTYPE_S64:
        g_assert_cmpint(pt->value.s64, ==, pt_copy->value.s64);
        break;
    case PTYPE_EOL:
        g_assert_not_reached();
    }

    ops->cleanup(serialize_data);
    g_free(args);
    g_free(pt_copy);
}

static void test_primitive_lists(gconstpointer opaque)
{
    TestArgs *args = (TestArgs *) opaque;
    const SerializeOps *ops = args->ops;
    PrimitiveType *pt = args->test_data;
    PrimitiveList pl = { .value = { NULL } };
    PrimitiveList pl_copy = { .value = { NULL } };
    PrimitiveList *pl_copy_ptr = &pl_copy;
    void *serialize_data;
    void *cur_head = NULL;
    int i;

    pl.type = pl_copy.type = pt->type;

    /* build up our list of primitive types */
    for (i = 0; i < 32; i++) {
        switch (pl.type) {
        case PTYPE_STRING: {
            QAPI_LIST_PREPEND(pl.value.strings, g_strdup(pt->value.string));
            break;
        }
        case PTYPE_INTEGER: {
            QAPI_LIST_PREPEND(pl.value.integers, pt->value.integer);
            break;
        }
        case PTYPE_S8: {
            QAPI_LIST_PREPEND(pl.value.s8_integers, pt->value.s8);
            break;
        }
        case PTYPE_S16: {
            QAPI_LIST_PREPEND(pl.value.s16_integers, pt->value.s16);
            break;
        }
        case PTYPE_S32: {
            QAPI_LIST_PREPEND(pl.value.s32_integers, pt->value.s32);
            break;
        }
        case PTYPE_S64: {
            QAPI_LIST_PREPEND(pl.value.s64_integers, pt->value.s64);
            break;
        }
        case PTYPE_U8: {
            QAPI_LIST_PREPEND(pl.value.u8_integers, pt->value.u8);
            break;
        }
        case PTYPE_U16: {
            QAPI_LIST_PREPEND(pl.value.u16_integers, pt->value.u16);
            break;
        }
        case PTYPE_U32: {
            QAPI_LIST_PREPEND(pl.value.u32_integers, pt->value.u32);
            break;
        }
        case PTYPE_U64: {
            QAPI_LIST_PREPEND(pl.value.u64_integers, pt->value.u64);
            break;
        }
        case PTYPE_NUMBER: {
            QAPI_LIST_PREPEND(pl.value.numbers, pt->value.number);
            break;
        }
        case PTYPE_BOOLEAN: {
            QAPI_LIST_PREPEND(pl.value.booleans, pt->value.boolean);
            break;
        }
        default:
            g_assert_not_reached();
        }
    }

    ops->serialize((void **)&pl, &serialize_data, visit_primitive_list,
                   &error_abort);
    ops->deserialize((void **)&pl_copy_ptr, serialize_data,
                     visit_primitive_list, &error_abort);

    i = 0;

    /* compare our deserialized list of primitives to the original */
    do {
        switch (pl_copy.type) {
        case PTYPE_STRING: {
            strList *ptr;
            if (cur_head) {
                ptr = cur_head;
                cur_head = ptr->next;
            } else {
                cur_head = ptr = pl_copy.value.strings;
            }
            g_assert_cmpstr(pt->value.string, ==, ptr->value);
            break;
        }
        case PTYPE_INTEGER: {
            intList *ptr;
            if (cur_head) {
                ptr = cur_head;
                cur_head = ptr->next;
            } else {
                cur_head = ptr = pl_copy.value.integers;
            }
            g_assert_cmpint(pt->value.integer, ==, ptr->value);
            break;
        }
        case PTYPE_S8: {
            int8List *ptr;
            if (cur_head) {
                ptr = cur_head;
                cur_head = ptr->next;
            } else {
                cur_head = ptr = pl_copy.value.s8_integers;
            }
            g_assert_cmpint(pt->value.s8, ==, ptr->value);
            break;
        }
        case PTYPE_S16: {
            int16List *ptr;
            if (cur_head) {
                ptr = cur_head;
                cur_head = ptr->next;
            } else {
                cur_head = ptr = pl_copy.value.s16_integers;
            }
            g_assert_cmpint(pt->value.s16, ==, ptr->value);
            break;
        }
        case PTYPE_S32: {
            int32List *ptr;
            if (cur_head) {
                ptr = cur_head;
                cur_head = ptr->next;
            } else {
                cur_head = ptr = pl_copy.value.s32_integers;
            }
            g_assert_cmpint(pt->value.s32, ==, ptr->value);
            break;
        }
        case PTYPE_S64: {
            int64List *ptr;
            if (cur_head) {
                ptr = cur_head;
                cur_head = ptr->next;
            } else {
                cur_head = ptr = pl_copy.value.s64_integers;
            }
            g_assert_cmpint(pt->value.s64, ==, ptr->value);
            break;
        }
        case PTYPE_U8: {
            uint8List *ptr;
            if (cur_head) {
                ptr = cur_head;
                cur_head = ptr->next;
            } else {
                cur_head = ptr = pl_copy.value.u8_integers;
            }
            g_assert_cmpint(pt->value.u8, ==, ptr->value);
            break;
        }
        case PTYPE_U16: {
            uint16List *ptr;
            if (cur_head) {
                ptr = cur_head;
                cur_head = ptr->next;
            } else {
                cur_head = ptr = pl_copy.value.u16_integers;
            }
            g_assert_cmpint(pt->value.u16, ==, ptr->value);
            break;
        }
        case PTYPE_U32: {
            uint32List *ptr;
            if (cur_head) {
                ptr = cur_head;
                cur_head = ptr->next;
            } else {
                cur_head = ptr = pl_copy.value.u32_integers;
            }
            g_assert_cmpint(pt->value.u32, ==, ptr->value);
            break;
        }
        case PTYPE_U64: {
            uint64List *ptr;
            if (cur_head) {
                ptr = cur_head;
                cur_head = ptr->next;
            } else {
                cur_head = ptr = pl_copy.value.u64_integers;
            }
            g_assert_cmpint(pt->value.u64, ==, ptr->value);
            break;
        }
        case PTYPE_NUMBER: {
            numberList *ptr;
            GString *double_expected = g_string_new("");
            GString *double_actual = g_string_new("");
            if (cur_head) {
                ptr = cur_head;
                cur_head = ptr->next;
            } else {
                cur_head = ptr = pl_copy.value.numbers;
            }
            /* we serialize with %f for our reference visitors, so rather than
             * fuzzy floating math to test "equality", just compare the
             * formatted values
             */
            g_string_printf(double_expected, "%.6f", pt->value.number);
            g_string_printf(double_actual, "%.6f", ptr->value);
            g_assert_cmpstr(double_actual->str, ==, double_expected->str);
            g_string_free(double_expected, true);
            g_string_free(double_actual, true);
            break;
        }
        case PTYPE_BOOLEAN: {
            boolList *ptr;
            if (cur_head) {
                ptr = cur_head;
                cur_head = ptr->next;
            } else {
                cur_head = ptr = pl_copy.value.booleans;
            }
            g_assert_cmpint(!!pt->value.boolean, ==, !!ptr->value);
            break;
        }
        default:
            g_assert_not_reached();
        }
        i++;
    } while (cur_head);

    g_assert_cmpint(i, ==, 33);

    ops->cleanup(serialize_data);
    dealloc_helper(&pl, visit_primitive_list, &error_abort);
    dealloc_helper(&pl_copy, visit_primitive_list, &error_abort);
    g_free(args);
}

static void test_struct(gconstpointer opaque)
{
    TestArgs *args = (TestArgs *) opaque;
    const SerializeOps *ops = args->ops;
    TestStruct *ts = struct_create();
    TestStruct *ts_copy = NULL;
    void *serialize_data;

    ops->serialize(ts, &serialize_data, visit_struct, &error_abort);
    ops->deserialize((void **)&ts_copy, serialize_data, visit_struct,
                     &error_abort);

    struct_compare(ts, ts_copy);

    struct_cleanup(ts);
    struct_cleanup(ts_copy);

    ops->cleanup(serialize_data);
    g_free(args);
}

static void test_nested_struct(gconstpointer opaque)
{
    TestArgs *args = (TestArgs *) opaque;
    const SerializeOps *ops = args->ops;
    UserDefTwo *udnp = nested_struct_create();
    UserDefTwo *udnp_copy = NULL;
    void *serialize_data;

    ops->serialize(udnp, &serialize_data, visit_nested_struct, &error_abort);
    ops->deserialize((void **)&udnp_copy, serialize_data, visit_nested_struct,
                     &error_abort);

    nested_struct_compare(udnp, udnp_copy);

    nested_struct_cleanup(udnp);
    nested_struct_cleanup(udnp_copy);

    ops->cleanup(serialize_data);
    g_free(args);
}

static void test_nested_struct_list(gconstpointer opaque)
{
    TestArgs *args = (TestArgs *) opaque;
    const SerializeOps *ops = args->ops;
    UserDefTwoList *listp = NULL, *tmp, *tmp_copy, *listp_copy = NULL;
    void *serialize_data;
    int i = 0;

    for (i = 0; i < 8; i++) {
        QAPI_LIST_PREPEND(listp, nested_struct_create());
    }

    ops->serialize(listp, &serialize_data, visit_nested_struct_list,
                   &error_abort);
    ops->deserialize((void **)&listp_copy, serialize_data,
                     visit_nested_struct_list, &error_abort);

    tmp = listp;
    tmp_copy = listp_copy;
    while (listp_copy) {
        g_assert(listp);
        nested_struct_compare(listp->value, listp_copy->value);
        listp = listp->next;
        listp_copy = listp_copy->next;
    }

    qapi_free_UserDefTwoList(tmp);
    qapi_free_UserDefTwoList(tmp_copy);

    ops->cleanup(serialize_data);
    g_free(args);
}

static PrimitiveType pt_values[] = {
    /* string tests */
    {
        .description = "string_empty",
        .type = PTYPE_STRING,
        .value.string = "",
    },
    {
        .description = "string_whitespace",
        .type = PTYPE_STRING,
        .value.string = "a b  c\td",
    },
    {
        .description = "string_newlines",
        .type = PTYPE_STRING,
        .value.string = "a\nb\n",
    },
    {
        .description = "string_commas",
        .type = PTYPE_STRING,
        .value.string = "a,b, c,d",
    },
    {
        .description = "string_single_quoted",
        .type = PTYPE_STRING,
        .value.string = "'a b',cd",
    },
    {
        .description = "string_double_quoted",
        .type = PTYPE_STRING,
        .value.string = "\"a b\",cd",
    },
    /* boolean tests */
    {
        .description = "boolean_true1",
        .type = PTYPE_BOOLEAN,
        .value.boolean = true,
    },
    {
        .description = "boolean_true2",
        .type = PTYPE_BOOLEAN,
        .value.boolean = 8,
    },
    {
        .description = "boolean_true3",
        .type = PTYPE_BOOLEAN,
        .value.boolean = -1,
    },
    {
        .description = "boolean_false1",
        .type = PTYPE_BOOLEAN,
        .value.boolean = false,
    },
    {
        .description = "boolean_false2",
        .type = PTYPE_BOOLEAN,
        .value.boolean = 0,
    },
    /* number tests (double) */
    {
        .description = "number_sanity1",
        .type = PTYPE_NUMBER,
        .value.number = -1,
    },
    {
        .description = "number_sanity2",
        .type = PTYPE_NUMBER,
        .value.number = 3.141593,
    },
    {
        .description = "number_min",
        .type = PTYPE_NUMBER,
        .value.number = DBL_MIN,
    },
    {
        .description = "number_max",
        .type = PTYPE_NUMBER,
        .value.number = DBL_MAX,
    },
    /* integer tests (int64) */
    {
        .description = "integer_sanity1",
        .type = PTYPE_INTEGER,
        .value.integer = -1,
    },
    {
        .description = "integer_sanity2",
        .type = PTYPE_INTEGER,
        .value.integer = INT64_MAX / 2 + 1,
    },
    {
        .description = "integer_min",
        .type = PTYPE_INTEGER,
        .value.integer = INT64_MIN,
    },
    {
        .description = "integer_max",
        .type = PTYPE_INTEGER,
        .value.integer = INT64_MAX,
    },
    /* uint8 tests */
    {
        .description = "uint8_sanity1",
        .type = PTYPE_U8,
        .value.u8 = 1,
    },
    {
        .description = "uint8_sanity2",
        .type = PTYPE_U8,
        .value.u8 = UINT8_MAX / 2 + 1,
    },
    {
        .description = "uint8_min",
        .type = PTYPE_U8,
        .value.u8 = 0,
    },
    {
        .description = "uint8_max",
        .type = PTYPE_U8,
        .value.u8 = UINT8_MAX,
    },
    /* uint16 tests */
    {
        .description = "uint16_sanity1",
        .type = PTYPE_U16,
        .value.u16 = 1,
    },
    {
        .description = "uint16_sanity2",
        .type = PTYPE_U16,
        .value.u16 = UINT16_MAX / 2 + 1,
    },
    {
        .description = "uint16_min",
        .type = PTYPE_U16,
        .value.u16 = 0,
    },
    {
        .description = "uint16_max",
        .type = PTYPE_U16,
        .value.u16 = UINT16_MAX,
    },
    /* uint32 tests */
    {
        .description = "uint32_sanity1",
        .type = PTYPE_U32,
        .value.u32 = 1,
    },
    {
        .description = "uint32_sanity2",
        .type = PTYPE_U32,
        .value.u32 = UINT32_MAX / 2 + 1,
    },
    {
        .description = "uint32_min",
        .type = PTYPE_U32,
        .value.u32 = 0,
    },
    {
        .description = "uint32_max",
        .type = PTYPE_U32,
        .value.u32 = UINT32_MAX,
    },
    /* uint64 tests */
    {
        .description = "uint64_sanity1",
        .type = PTYPE_U64,
        .value.u64 = 1,
    },
    {
        .description = "uint64_sanity2",
        .type = PTYPE_U64,
        .value.u64 = UINT64_MAX / 2 + 1,
    },
    {
        .description = "uint64_min",
        .type = PTYPE_U64,
        .value.u64 = 0,
    },
    {
        .description = "uint64_max",
        .type = PTYPE_U64,
        .value.u64 = UINT64_MAX,
    },
    /* int8 tests */
    {
        .description = "int8_sanity1",
        .type = PTYPE_S8,
        .value.s8 = -1,
    },
    {
        .description = "int8_sanity2",
        .type = PTYPE_S8,
        .value.s8 = INT8_MAX / 2 + 1,
    },
    {
        .description = "int8_min",
        .type = PTYPE_S8,
        .value.s8 = INT8_MIN,
    },
    {
        .description = "int8_max",
        .type = PTYPE_S8,
        .value.s8 = INT8_MAX,
    },
    /* int16 tests */
    {
        .description = "int16_sanity1",
        .type = PTYPE_S16,
        .value.s16 = -1,
    },
    {
        .description = "int16_sanity2",
        .type = PTYPE_S16,
        .value.s16 = INT16_MAX / 2 + 1,
    },
    {
        .description = "int16_min",
        .type = PTYPE_S16,
        .value.s16 = INT16_MIN,
    },
    {
        .description = "int16_max",
        .type = PTYPE_S16,
        .value.s16 = INT16_MAX,
    },
    /* int32 tests */
    {
        .description = "int32_sanity1",
        .type = PTYPE_S32,
        .value.s32 = -1,
    },
    {
        .description = "int32_sanity2",
        .type = PTYPE_S32,
        .value.s32 = INT32_MAX / 2 + 1,
    },
    {
        .description = "int32_min",
        .type = PTYPE_S32,
        .value.s32 = INT32_MIN,
    },
    {
        .description = "int32_max",
        .type = PTYPE_S32,
        .value.s32 = INT32_MAX,
    },
    /* int64 tests */
    {
        .description = "int64_sanity1",
        .type = PTYPE_S64,
        .value.s64 = -1,
    },
    {
        .description = "int64_sanity2",
        .type = PTYPE_S64,
        .value.s64 = INT64_MAX / 2 + 1,
    },
    {
        .description = "int64_min",
        .type = PTYPE_S64,
        .value.s64 = INT64_MIN,
    },
    {
        .description = "int64_max",
        .type = PTYPE_S64,
        .value.s64 = INT64_MAX,
    },
    { .type = PTYPE_EOL }
};

/* visitor-specific op implementations */

typedef struct QmpSerializeData {
    Visitor *qov;
    QObject *obj;
    Visitor *qiv;
} QmpSerializeData;

static void qmp_serialize(void *native_in, void **datap,
                          VisitorFunc visit, Error **errp)
{
    QmpSerializeData *d = g_malloc0(sizeof(*d));

    d->qov = qobject_output_visitor_new(&d->obj);
    visit(d->qov, &native_in, errp);
    *datap = d;
}

static void qmp_deserialize(void **native_out, void *datap,
                            VisitorFunc visit, Error **errp)
{
    QmpSerializeData *d = datap;
    GString *output_json;
    QObject *obj_orig, *obj;

    visit_complete(d->qov, &d->obj);
    obj_orig = d->obj;
    output_json = qobject_to_json(obj_orig);
    obj = qobject_from_json(output_json->str, &error_abort);

    g_string_free(output_json, true);
    d->qiv = qobject_input_visitor_new(obj);
    qobject_unref(obj_orig);
    qobject_unref(obj);
    visit(d->qiv, native_out, errp);
}

static void qmp_cleanup(void *datap)
{
    QmpSerializeData *d = datap;
    visit_free(d->qov);
    visit_free(d->qiv);

    g_free(d);
}

typedef struct StringSerializeData {
    char *string;
    Visitor *sov;
    Visitor *siv;
} StringSerializeData;

static void string_serialize(void *native_in, void **datap,
                             VisitorFunc visit, Error **errp)
{
    StringSerializeData *d = g_malloc0(sizeof(*d));

    d->sov = string_output_visitor_new(false, &d->string);
    visit(d->sov, &native_in, errp);
    *datap = d;
}

static void string_deserialize(void **native_out, void *datap,
                               VisitorFunc visit, Error **errp)
{
    StringSerializeData *d = datap;

    visit_complete(d->sov, &d->string);
    d->siv = string_input_visitor_new(d->string);
    visit(d->siv, native_out, errp);
}

static void string_cleanup(void *datap)
{
    StringSerializeData *d = datap;

    visit_free(d->sov);
    visit_free(d->siv);
    g_free(d->string);
    g_free(d);
}

/* visitor registration, test harness */

/* note: to function interchangeably as a serialization mechanism your
 * visitor test implementation should pass the test cases for all visitor
 * capabilities: primitives, structures, and lists
 */
static const SerializeOps visitors[] = {
    {
        .type = "QMP",
        .serialize = qmp_serialize,
        .deserialize = qmp_deserialize,
        .cleanup = qmp_cleanup,
        .caps = VCAP_PRIMITIVES | VCAP_STRUCTURES | VCAP_LISTS |
                VCAP_PRIMITIVE_LISTS
    },
    {
        .type = "String",
        .serialize = string_serialize,
        .deserialize = string_deserialize,
        .cleanup = string_cleanup,
        .caps = VCAP_PRIMITIVES
    },
    { NULL }
};

static void add_visitor_type(const SerializeOps *ops)
{
    char testname_prefix[32];
    char testname[128];
    TestArgs *args;
    int i = 0;

    sprintf(testname_prefix, "/visitor/serialization/%s", ops->type);

    if (ops->caps & VCAP_PRIMITIVES) {
        while (pt_values[i].type != PTYPE_EOL) {
            sprintf(testname, "%s/primitives/%s", testname_prefix,
                    pt_values[i].description);
            args = g_malloc0(sizeof(*args));
            args->ops = ops;
            args->test_data = &pt_values[i];
            g_test_add_data_func(testname, args, test_primitives);
            i++;
        }
    }

    if (ops->caps & VCAP_STRUCTURES) {
        sprintf(testname, "%s/struct", testname_prefix);
        args = g_malloc0(sizeof(*args));
        args->ops = ops;
        args->test_data = NULL;
        g_test_add_data_func(testname, args, test_struct);

        sprintf(testname, "%s/nested_struct", testname_prefix);
        args = g_malloc0(sizeof(*args));
        args->ops = ops;
        args->test_data = NULL;
        g_test_add_data_func(testname, args, test_nested_struct);
    }

    if (ops->caps & VCAP_LISTS) {
        sprintf(testname, "%s/nested_struct_list", testname_prefix);
        args = g_malloc0(sizeof(*args));
        args->ops = ops;
        args->test_data = NULL;
        g_test_add_data_func(testname, args, test_nested_struct_list);
    }

    if (ops->caps & VCAP_PRIMITIVE_LISTS) {
        i = 0;
        while (pt_values[i].type != PTYPE_EOL) {
            sprintf(testname, "%s/primitive_list/%s", testname_prefix,
                    pt_values[i].description);
            args = g_malloc0(sizeof(*args));
            args->ops = ops;
            args->test_data = &pt_values[i];
            g_test_add_data_func(testname, args, test_primitive_lists);
            i++;
        }
    }
}

int main(int argc, char **argv)
{
    int i = 0;

    g_test_init(&argc, &argv, NULL);

    while (visitors[i].type != NULL) {
        add_visitor_type(&visitors[i]);
        i++;
    }

    g_test_run();

    return 0;
}
