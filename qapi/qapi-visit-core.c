/*
 * Core Definitions for QAPI Visitor Classes
 *
 * Copyright (C) 2012-2016 Red Hat, Inc.
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "qapi/qmp/qobject.h"
#include "qapi/qmp/qerror.h"
#include "qapi/visitor.h"
#include "qapi/visitor-impl.h"

void visit_start_struct(Visitor *v, const char *name, void **obj,
                        size_t size, Error **errp)
{
    Error *err = NULL;

    if (obj) {
        assert(size);
        assert(v->type != VISITOR_OUTPUT || *obj);
    }
    v->start_struct(v, name, obj, size, &err);
    if (obj && v->type == VISITOR_INPUT) {
        assert(!err != !*obj);
    }
    error_propagate(errp, err);
}

void visit_check_struct(Visitor *v, Error **errp)
{
    if (v->check_struct) {
        v->check_struct(v, errp);
    }
}

void visit_end_struct(Visitor *v)
{
    v->end_struct(v);
}

void visit_start_list(Visitor *v, const char *name, GenericList **list,
                      size_t size, Error **errp)
{
    Error *err = NULL;

    assert(!list || size >= sizeof(GenericList));
    v->start_list(v, name, list, size, &err);
    if (list && v->type == VISITOR_INPUT) {
        assert(!(err && *list));
    }
    error_propagate(errp, err);
}

GenericList *visit_next_list(Visitor *v, GenericList *tail, size_t size)
{
    assert(tail && size >= sizeof(GenericList));
    return v->next_list(v, tail, size);
}

void visit_end_list(Visitor *v)
{
    v->end_list(v);
}

void visit_start_alternate(Visitor *v, const char *name,
                           GenericAlternate **obj, size_t size,
                           bool promote_int, Error **errp)
{
    Error *err = NULL;

    assert(obj && size >= sizeof(GenericAlternate));
    assert(v->type != VISITOR_OUTPUT || *obj);
    if (v->start_alternate) {
        v->start_alternate(v, name, obj, size, promote_int, &err);
    }
    if (v->type == VISITOR_INPUT) {
        assert(v->start_alternate && !err != !*obj);
    }
    error_propagate(errp, err);
}

void visit_end_alternate(Visitor *v)
{
    if (v->end_alternate) {
        v->end_alternate(v);
    }
}

bool visit_optional(Visitor *v, const char *name, bool *present)
{
    if (v->optional) {
        v->optional(v, name, present);
    }
    return *present;
}

void visit_type_int(Visitor *v, const char *name, int64_t *obj, Error **errp)
{
    assert(obj);
    v->type_int64(v, name, obj, errp);
}

static void visit_type_uintN(Visitor *v, uint64_t *obj, const char *name,
                             uint64_t max, const char *type, Error **errp)
{
    Error *err = NULL;
    uint64_t value = *obj;

    v->type_uint64(v, name, &value, &err);
    if (err) {
        error_propagate(errp, err);
    } else if (value > max) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   name ? name : "null", type);
    } else {
        *obj = value;
    }
}

void visit_type_uint8(Visitor *v, const char *name, uint8_t *obj,
                      Error **errp)
{
    uint64_t value = *obj;
    visit_type_uintN(v, &value, name, UINT8_MAX, "uint8_t", errp);
    *obj = value;
}

void visit_type_uint16(Visitor *v, const char *name, uint16_t *obj,
                       Error **errp)
{
    uint64_t value = *obj;
    visit_type_uintN(v, &value, name, UINT16_MAX, "uint16_t", errp);
    *obj = value;
}

void visit_type_uint32(Visitor *v, const char *name, uint32_t *obj,
                       Error **errp)
{
    uint64_t value = *obj;
    visit_type_uintN(v, &value, name, UINT32_MAX, "uint32_t", errp);
    *obj = value;
}

void visit_type_uint64(Visitor *v, const char *name, uint64_t *obj,
                       Error **errp)
{
    assert(obj);
    v->type_uint64(v, name, obj, errp);
}

static void visit_type_intN(Visitor *v, int64_t *obj, const char *name,
                            int64_t min, int64_t max, const char *type,
                            Error **errp)
{
    Error *err = NULL;
    int64_t value = *obj;

    v->type_int64(v, name, &value, &err);
    if (err) {
        error_propagate(errp, err);
    } else if (value < min || value > max) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   name ? name : "null", type);
    } else {
        *obj = value;
    }
}

void visit_type_int8(Visitor *v, const char *name, int8_t *obj, Error **errp)
{
    int64_t value = *obj;
    visit_type_intN(v, &value, name, INT8_MIN, INT8_MAX, "int8_t", errp);
    *obj = value;
}

void visit_type_int16(Visitor *v, const char *name, int16_t *obj,
                      Error **errp)
{
    int64_t value = *obj;
    visit_type_intN(v, &value, name, INT16_MIN, INT16_MAX, "int16_t", errp);
    *obj = value;
}

void visit_type_int32(Visitor *v, const char *name, int32_t *obj,
                      Error **errp)
{
    int64_t value = *obj;
    visit_type_intN(v, &value, name, INT32_MIN, INT32_MAX, "int32_t", errp);
    *obj = value;
}

void visit_type_int64(Visitor *v, const char *name, int64_t *obj,
                      Error **errp)
{
    assert(obj);
    v->type_int64(v, name, obj, errp);
}

void visit_type_size(Visitor *v, const char *name, uint64_t *obj,
                     Error **errp)
{
    assert(obj);
    if (v->type_size) {
        v->type_size(v, name, obj, errp);
    } else {
        v->type_uint64(v, name, obj, errp);
    }
}

void visit_type_bool(Visitor *v, const char *name, bool *obj, Error **errp)
{
    assert(obj);
    v->type_bool(v, name, obj, errp);
}

void visit_type_str(Visitor *v, const char *name, char **obj, Error **errp)
{
    Error *err = NULL;

    assert(obj);
    /* TODO: Fix callers to not pass NULL when they mean "", so that we
     * can enable:
    assert(v->type != VISITOR_OUTPUT || *obj);
     */
    v->type_str(v, name, obj, &err);
    if (v->type == VISITOR_INPUT) {
        assert(!err != !*obj);
    }
    error_propagate(errp, err);
}

void visit_type_number(Visitor *v, const char *name, double *obj,
                       Error **errp)
{
    assert(obj);
    v->type_number(v, name, obj, errp);
}

void visit_type_any(Visitor *v, const char *name, QObject **obj, Error **errp)
{
    Error *err = NULL;

    assert(obj);
    assert(v->type != VISITOR_OUTPUT || *obj);
    v->type_any(v, name, obj, &err);
    if (v->type == VISITOR_INPUT) {
        assert(!err != !*obj);
    }
    error_propagate(errp, err);
}

void visit_type_null(Visitor *v, const char *name, Error **errp)
{
    v->type_null(v, name, errp);
}

static void output_type_enum(Visitor *v, const char *name, int *obj,
                             const char *const strings[], Error **errp)
{
    int i = 0;
    int value = *obj;
    char *enum_str;

    while (strings[i++] != NULL);
    if (value < 0 || value >= i - 1) {
        error_setg(errp, QERR_INVALID_PARAMETER, name ? name : "null");
        return;
    }

    enum_str = (char *)strings[value];
    visit_type_str(v, name, &enum_str, errp);
}

static void input_type_enum(Visitor *v, const char *name, int *obj,
                            const char *const strings[], Error **errp)
{
    Error *local_err = NULL;
    int64_t value = 0;
    char *enum_str;

    visit_type_str(v, name, &enum_str, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    while (strings[value] != NULL) {
        if (strcmp(strings[value], enum_str) == 0) {
            break;
        }
        value++;
    }

    if (strings[value] == NULL) {
        error_setg(errp, QERR_INVALID_PARAMETER, enum_str);
        g_free(enum_str);
        return;
    }

    g_free(enum_str);
    *obj = value;
}

void visit_type_enum(Visitor *v, const char *name, int *obj,
                     const char *const strings[], Error **errp)
{
    assert(obj && strings);
    if (v->type == VISITOR_INPUT) {
        input_type_enum(v, name, obj, strings, errp);
    } else if (v->type == VISITOR_OUTPUT) {
        output_type_enum(v, name, obj, strings, errp);
    }
}
