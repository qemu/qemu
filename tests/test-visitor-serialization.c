/*
 * Unit-tests for visitor-based serialization
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Michael Roth <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <stdlib.h>
#include <stdint.h>
#include <float.h>

#include "qemu-common.h"
#include "test-qapi-types.h"
#include "test-qapi-visit.h"
#include "qapi/qmp/types.h"
#include "qapi/qmp-input-visitor.h"
#include "qapi/qmp-output-visitor.h"
#include "qapi/string-input-visitor.h"
#include "qapi/string-output-visitor.h"
#include "qapi-types.h"
#include "qapi-visit.h"
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
        intmax_t max;
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
    QapiDeallocVisitor *qdv = qapi_dealloc_visitor_new();

    visit(qapi_dealloc_get_visitor(qdv), &native_in, errp);

    qapi_dealloc_visitor_cleanup(qdv);
}

static void visit_primitive_type(Visitor *v, void **native, Error **errp)
{
    PrimitiveType *pt = *native;
    switch(pt->type) {
    case PTYPE_STRING:
        visit_type_str(v, (char **)&pt->value.string, NULL, errp);
        break;
    case PTYPE_BOOLEAN:
        visit_type_bool(v, &pt->value.boolean, NULL, errp);
        break;
    case PTYPE_NUMBER:
        visit_type_number(v, &pt->value.number, NULL, errp);
        break;
    case PTYPE_INTEGER:
        visit_type_int(v, &pt->value.integer, NULL, errp);
        break;
    case PTYPE_U8:
        visit_type_uint8(v, &pt->value.u8, NULL, errp);
        break;
    case PTYPE_U16:
        visit_type_uint16(v, &pt->value.u16, NULL, errp);
        break;
    case PTYPE_U32:
        visit_type_uint32(v, &pt->value.u32, NULL, errp);
        break;
    case PTYPE_U64:
        visit_type_uint64(v, &pt->value.u64, NULL, errp);
        break;
    case PTYPE_S8:
        visit_type_int8(v, &pt->value.s8, NULL, errp);
        break;
    case PTYPE_S16:
        visit_type_int16(v, &pt->value.s16, NULL, errp);
        break;
    case PTYPE_S32:
        visit_type_int32(v, &pt->value.s32, NULL, errp);
        break;
    case PTYPE_S64:
        visit_type_int64(v, &pt->value.s64, NULL, errp);
        break;
    case PTYPE_EOL:
        g_assert(false);
    }
}

static void visit_primitive_list(Visitor *v, void **native, Error **errp)
{
    PrimitiveList *pl = *native;
    switch (pl->type) {
    case PTYPE_STRING:
        visit_type_strList(v, &pl->value.strings, NULL, errp);
        break;
    case PTYPE_BOOLEAN:
        visit_type_boolList(v, &pl->value.booleans, NULL, errp);
        break;
    case PTYPE_NUMBER:
        visit_type_numberList(v, &pl->value.numbers, NULL, errp);
        break;
    case PTYPE_INTEGER:
        visit_type_intList(v, &pl->value.integers, NULL, errp);
        break;
    case PTYPE_S8:
        visit_type_int8List(v, &pl->value.s8_integers, NULL, errp);
        break;
    case PTYPE_S16:
        visit_type_int16List(v, &pl->value.s16_integers, NULL, errp);
        break;
    case PTYPE_S32:
        visit_type_int32List(v, &pl->value.s32_integers, NULL, errp);
        break;
    case PTYPE_S64:
        visit_type_int64List(v, &pl->value.s64_integers, NULL, errp);
        break;
    case PTYPE_U8:
        visit_type_uint8List(v, &pl->value.u8_integers, NULL, errp);
        break;
    case PTYPE_U16:
        visit_type_uint16List(v, &pl->value.u16_integers, NULL, errp);
        break;
    case PTYPE_U32:
        visit_type_uint32List(v, &pl->value.u32_integers, NULL, errp);
        break;
    case PTYPE_U64:
        visit_type_uint64List(v, &pl->value.u64_integers, NULL, errp);
        break;
    default:
        g_assert(false);
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
    visit_start_struct(v, (void **)obj, NULL, name, sizeof(TestStruct), errp);

    visit_type_int(v, &(*obj)->integer, "integer", errp);
    visit_type_bool(v, &(*obj)->boolean, "boolean", errp);
    visit_type_str(v, &(*obj)->string, "string", errp);

    visit_end_struct(v, errp);
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
    visit_type_TestStruct(v, (TestStruct **)native, NULL, errp);
}

static UserDefNested *nested_struct_create(void)
{
    UserDefNested *udnp = g_malloc0(sizeof(*udnp));
    udnp->string0 = strdup("test_string0");
    udnp->dict1.string1 = strdup("test_string1");
    udnp->dict1.dict2.userdef1 = g_malloc0(sizeof(UserDefOne));
    udnp->dict1.dict2.userdef1->integer = 42;
    udnp->dict1.dict2.userdef1->string = strdup("test_string");
    udnp->dict1.dict2.string2 = strdup("test_string2");
    udnp->dict1.has_dict3 = true;
    udnp->dict1.dict3.userdef2 = g_malloc0(sizeof(UserDefOne));
    udnp->dict1.dict3.userdef2->integer = 43;
    udnp->dict1.dict3.userdef2->string = strdup("test_string");
    udnp->dict1.dict3.string3 = strdup("test_string3");
    return udnp;
}

static void nested_struct_compare(UserDefNested *udnp1, UserDefNested *udnp2)
{
    g_assert(udnp1);
    g_assert(udnp2);
    g_assert_cmpstr(udnp1->string0, ==, udnp2->string0);
    g_assert_cmpstr(udnp1->dict1.string1, ==, udnp2->dict1.string1);
    g_assert_cmpint(udnp1->dict1.dict2.userdef1->integer, ==,
                    udnp2->dict1.dict2.userdef1->integer);
    g_assert_cmpstr(udnp1->dict1.dict2.userdef1->string, ==,
                    udnp2->dict1.dict2.userdef1->string);
    g_assert_cmpstr(udnp1->dict1.dict2.string2, ==, udnp2->dict1.dict2.string2);
    g_assert(udnp1->dict1.has_dict3 == udnp2->dict1.has_dict3);
    g_assert_cmpint(udnp1->dict1.dict3.userdef2->integer, ==,
                    udnp2->dict1.dict3.userdef2->integer);
    g_assert_cmpstr(udnp1->dict1.dict3.userdef2->string, ==,
                    udnp2->dict1.dict3.userdef2->string);
    g_assert_cmpstr(udnp1->dict1.dict3.string3, ==, udnp2->dict1.dict3.string3);
}

static void nested_struct_cleanup(UserDefNested *udnp)
{
    qapi_free_UserDefNested(udnp);
}

static void visit_nested_struct(Visitor *v, void **native, Error **errp)
{
    visit_type_UserDefNested(v, (UserDefNested **)native, NULL, errp);
}

static void visit_nested_struct_list(Visitor *v, void **native, Error **errp)
{
    visit_type_UserDefNestedList(v, (UserDefNestedList **)native, NULL, errp);
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
    Error *err = NULL;
    void *serialize_data;

    pt_copy->type = pt->type;
    ops->serialize(pt, &serialize_data, visit_primitive_type, &err);
    ops->deserialize((void **)&pt_copy, serialize_data, visit_primitive_type, &err);

    g_assert(err == NULL);
    g_assert(pt_copy != NULL);
    if (pt->type == PTYPE_STRING) {
        g_assert_cmpstr(pt->value.string, ==, pt_copy->value.string);
        g_free((char *)pt_copy->value.string);
    } else if (pt->type == PTYPE_NUMBER) {
        GString *double_expected = g_string_new("");
        GString *double_actual = g_string_new("");
        /* we serialize with %f for our reference visitors, so rather than fuzzy
         * floating math to test "equality", just compare the formatted values
         */
        g_string_printf(double_expected, "%.6f", pt->value.number);
        g_string_printf(double_actual, "%.6f", pt_copy->value.number);
        g_assert_cmpstr(double_actual->str, ==, double_expected->str);

        g_string_free(double_expected, true);
        g_string_free(double_actual, true);
    } else if (pt->type == PTYPE_BOOLEAN) {
        g_assert_cmpint(!!pt->value.max, ==, !!pt->value.max);
    } else {
        g_assert_cmpint(pt->value.max, ==, pt_copy->value.max);
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
    PrimitiveList pl = { .value = { 0 } };
    PrimitiveList pl_copy = { .value = { 0 } };
    PrimitiveList *pl_copy_ptr = &pl_copy;
    Error *err = NULL;
    void *serialize_data;
    void *cur_head = NULL;
    int i;

    pl.type = pl_copy.type = pt->type;

    /* build up our list of primitive types */
    for (i = 0; i < 32; i++) {
        switch (pl.type) {
        case PTYPE_STRING: {
            strList *tmp = g_new0(strList, 1);
            tmp->value = g_strdup(pt->value.string);
            if (pl.value.strings == NULL) {
                pl.value.strings = tmp;
            } else {
                tmp->next = pl.value.strings;
                pl.value.strings = tmp;
            }
            break;
        }
        case PTYPE_INTEGER: {
            intList *tmp = g_new0(intList, 1);
            tmp->value = pt->value.integer;
            if (pl.value.integers == NULL) {
                pl.value.integers = tmp;
            } else {
                tmp->next = pl.value.integers;
                pl.value.integers = tmp;
            }
            break;
        }
        case PTYPE_S8: {
            int8List *tmp = g_new0(int8List, 1);
            tmp->value = pt->value.s8;
            if (pl.value.s8_integers == NULL) {
                pl.value.s8_integers = tmp;
            } else {
                tmp->next = pl.value.s8_integers;
                pl.value.s8_integers = tmp;
            }
            break;
        }
        case PTYPE_S16: {
            int16List *tmp = g_new0(int16List, 1);
            tmp->value = pt->value.s16;
            if (pl.value.s16_integers == NULL) {
                pl.value.s16_integers = tmp;
            } else {
                tmp->next = pl.value.s16_integers;
                pl.value.s16_integers = tmp;
            }
            break;
        }
        case PTYPE_S32: {
            int32List *tmp = g_new0(int32List, 1);
            tmp->value = pt->value.s32;
            if (pl.value.s32_integers == NULL) {
                pl.value.s32_integers = tmp;
            } else {
                tmp->next = pl.value.s32_integers;
                pl.value.s32_integers = tmp;
            }
            break;
        }
        case PTYPE_S64: {
            int64List *tmp = g_new0(int64List, 1);
            tmp->value = pt->value.s64;
            if (pl.value.s64_integers == NULL) {
                pl.value.s64_integers = tmp;
            } else {
                tmp->next = pl.value.s64_integers;
                pl.value.s64_integers = tmp;
            }
            break;
        }
        case PTYPE_U8: {
            uint8List *tmp = g_new0(uint8List, 1);
            tmp->value = pt->value.u8;
            if (pl.value.u8_integers == NULL) {
                pl.value.u8_integers = tmp;
            } else {
                tmp->next = pl.value.u8_integers;
                pl.value.u8_integers = tmp;
            }
            break;
        }
        case PTYPE_U16: {
            uint16List *tmp = g_new0(uint16List, 1);
            tmp->value = pt->value.u16;
            if (pl.value.u16_integers == NULL) {
                pl.value.u16_integers = tmp;
            } else {
                tmp->next = pl.value.u16_integers;
                pl.value.u16_integers = tmp;
            }
            break;
        }
        case PTYPE_U32: {
            uint32List *tmp = g_new0(uint32List, 1);
            tmp->value = pt->value.u32;
            if (pl.value.u32_integers == NULL) {
                pl.value.u32_integers = tmp;
            } else {
                tmp->next = pl.value.u32_integers;
                pl.value.u32_integers = tmp;
            }
            break;
        }
        case PTYPE_U64: {
            uint64List *tmp = g_new0(uint64List, 1);
            tmp->value = pt->value.u64;
            if (pl.value.u64_integers == NULL) {
                pl.value.u64_integers = tmp;
            } else {
                tmp->next = pl.value.u64_integers;
                pl.value.u64_integers = tmp;
            }
            break;
        }
        case PTYPE_NUMBER: {
            numberList *tmp = g_new0(numberList, 1);
            tmp->value = pt->value.number;
            if (pl.value.numbers == NULL) {
                pl.value.numbers = tmp;
            } else {
                tmp->next = pl.value.numbers;
                pl.value.numbers = tmp;
            }
            break;
        }
        case PTYPE_BOOLEAN: {
            boolList *tmp = g_new0(boolList, 1);
            tmp->value = pt->value.boolean;
            if (pl.value.booleans == NULL) {
                pl.value.booleans = tmp;
            } else {
                tmp->next = pl.value.booleans;
                pl.value.booleans = tmp;
            }
            break;
        }
        default:
            g_assert(0);
        }
    }

    ops->serialize((void **)&pl, &serialize_data, visit_primitive_list, &err);
    ops->deserialize((void **)&pl_copy_ptr, serialize_data, visit_primitive_list, &err);

    g_assert(err == NULL);
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
            g_assert(0);
        }
        i++;
    } while (cur_head);

    g_assert_cmpint(i, ==, 33);

    ops->cleanup(serialize_data);
    dealloc_helper(&pl, visit_primitive_list, &err);
    g_assert(!err);
    dealloc_helper(&pl_copy, visit_primitive_list, &err);
    g_assert(!err);
    g_free(args);
}

static void test_struct(gconstpointer opaque)
{
    TestArgs *args = (TestArgs *) opaque;
    const SerializeOps *ops = args->ops;
    TestStruct *ts = struct_create();
    TestStruct *ts_copy = NULL;
    Error *err = NULL;
    void *serialize_data;

    ops->serialize(ts, &serialize_data, visit_struct, &err);
    ops->deserialize((void **)&ts_copy, serialize_data, visit_struct, &err); 

    g_assert(err == NULL);
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
    UserDefNested *udnp = nested_struct_create();
    UserDefNested *udnp_copy = NULL;
    Error *err = NULL;
    void *serialize_data;
    
    ops->serialize(udnp, &serialize_data, visit_nested_struct, &err);
    ops->deserialize((void **)&udnp_copy, serialize_data, visit_nested_struct, &err); 

    g_assert(err == NULL);
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
    UserDefNestedList *listp = NULL, *tmp, *tmp_copy, *listp_copy = NULL;
    Error *err = NULL;
    void *serialize_data;
    int i = 0;

    for (i = 0; i < 8; i++) {
        tmp = g_malloc0(sizeof(UserDefNestedList));
        tmp->value = nested_struct_create();
        tmp->next = listp;
        listp = tmp;
    }
    
    ops->serialize(listp, &serialize_data, visit_nested_struct_list, &err);
    ops->deserialize((void **)&listp_copy, serialize_data,
                     visit_nested_struct_list, &err); 

    g_assert(err == NULL);

    tmp = listp;
    tmp_copy = listp_copy;
    while (listp_copy) {
        g_assert(listp);
        nested_struct_compare(listp->value, listp_copy->value);
        listp = listp->next;
        listp_copy = listp_copy->next;
    }

    qapi_free_UserDefNestedList(tmp);
    qapi_free_UserDefNestedList(tmp_copy);

    ops->cleanup(serialize_data);
    g_free(args);
}

PrimitiveType pt_values[] = {
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
    /* note: we format these to %.6f before comparing, since that's how
     * we serialize them and it doesn't make sense to check precision
     * beyond that.
     */
    {
        .description = "number_sanity1",
        .type = PTYPE_NUMBER,
        .value.number = -1,
    },
    {
        .description = "number_sanity2",
        .type = PTYPE_NUMBER,
        .value.number = 3.14159265,
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
    QmpOutputVisitor *qov;
    QmpInputVisitor *qiv;
} QmpSerializeData;

static void qmp_serialize(void *native_in, void **datap,
                          VisitorFunc visit, Error **errp)
{
    QmpSerializeData *d = g_malloc0(sizeof(*d));

    d->qov = qmp_output_visitor_new();
    visit(qmp_output_get_visitor(d->qov), &native_in, errp);
    *datap = d;
}

static void qmp_deserialize(void **native_out, void *datap,
                            VisitorFunc visit, Error **errp)
{
    QmpSerializeData *d = datap;
    QString *output_json;
    QObject *obj_orig, *obj;

    obj_orig = qmp_output_get_qobject(d->qov);
    output_json = qobject_to_json(obj_orig);
    obj = qobject_from_json(qstring_get_str(output_json));

    QDECREF(output_json);
    d->qiv = qmp_input_visitor_new(obj);
    qobject_decref(obj_orig);
    qobject_decref(obj);
    visit(qmp_input_get_visitor(d->qiv), native_out, errp);
}

static void qmp_cleanup(void *datap)
{
    QmpSerializeData *d = datap;
    qmp_output_visitor_cleanup(d->qov);
    qmp_input_visitor_cleanup(d->qiv);

    g_free(d);
}

typedef struct StringSerializeData {
    char *string;
    StringOutputVisitor *sov;
    StringInputVisitor *siv;
} StringSerializeData;

static void string_serialize(void *native_in, void **datap,
                             VisitorFunc visit, Error **errp)
{
    StringSerializeData *d = g_malloc0(sizeof(*d));

    d->sov = string_output_visitor_new();
    visit(string_output_get_visitor(d->sov), &native_in, errp);
    *datap = d;
}

static void string_deserialize(void **native_out, void *datap,
                               VisitorFunc visit, Error **errp)
{
    StringSerializeData *d = datap;

    d->string = string_output_get_string(d->sov);
    d->siv = string_input_visitor_new(d->string);
    visit(string_input_get_visitor(d->siv), native_out, errp);
}

static void string_cleanup(void *datap)
{
    StringSerializeData *d = datap;

    string_output_visitor_cleanup(d->sov);
    string_input_visitor_cleanup(d->siv);
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
    char testname_prefix[128];
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
