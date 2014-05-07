/*
 * Core Definitions for QAPI Visitor Classes
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "qapi/qmp/qobject.h"
#include "qapi/qmp/qerror.h"
#include "qapi/visitor.h"
#include "qapi/visitor-impl.h"

void visit_start_struct(Visitor *v, void **obj, const char *kind,
                        const char *name, size_t size, Error **errp)
{
    if (!error_is_set(errp)) {
        v->start_struct(v, obj, kind, name, size, errp);
    }
}

void visit_end_struct(Visitor *v, Error **errp)
{
    assert(!error_is_set(errp));
    v->end_struct(v, errp);
}

void visit_start_implicit_struct(Visitor *v, void **obj, size_t size,
                                 Error **errp)
{
    if (!error_is_set(errp) && v->start_implicit_struct) {
        v->start_implicit_struct(v, obj, size, errp);
    }
}

void visit_end_implicit_struct(Visitor *v, Error **errp)
{
    assert(!error_is_set(errp));
    if (v->end_implicit_struct) {
        v->end_implicit_struct(v, errp);
    }
}

void visit_start_list(Visitor *v, const char *name, Error **errp)
{
    if (!error_is_set(errp)) {
        v->start_list(v, name, errp);
    }
}

GenericList *visit_next_list(Visitor *v, GenericList **list, Error **errp)
{
    if (!error_is_set(errp)) {
        return v->next_list(v, list, errp);
    }

    return 0;
}

void visit_end_list(Visitor *v, Error **errp)
{
    assert(!error_is_set(errp));
    v->end_list(v, errp);
}

void visit_start_optional(Visitor *v, bool *present, const char *name,
                          Error **errp)
{
    if (!error_is_set(errp) && v->start_optional) {
        v->start_optional(v, present, name, errp);
    }
}

void visit_end_optional(Visitor *v, Error **errp)
{
    if (!error_is_set(errp) && v->end_optional) {
        v->end_optional(v, errp);
    }
}

void visit_get_next_type(Visitor *v, int *obj, const int *qtypes,
                         const char *name, Error **errp)
{
    if (!error_is_set(errp) && v->get_next_type) {
        v->get_next_type(v, obj, qtypes, name, errp);
    }
}

void visit_type_enum(Visitor *v, int *obj, const char *strings[],
                     const char *kind, const char *name, Error **errp)
{
    if (!error_is_set(errp)) {
        v->type_enum(v, obj, strings, kind, name, errp);
    }
}

void visit_type_int(Visitor *v, int64_t *obj, const char *name, Error **errp)
{
    if (!error_is_set(errp)) {
        v->type_int(v, obj, name, errp);
    }
}

void visit_type_uint8(Visitor *v, uint8_t *obj, const char *name, Error **errp)
{
    int64_t value;
    if (!error_is_set(errp)) {
        if (v->type_uint8) {
            v->type_uint8(v, obj, name, errp);
        } else {
            value = *obj;
            v->type_int(v, &value, name, errp);
            if (value < 0 || value > UINT8_MAX) {
                error_set(errp, QERR_INVALID_PARAMETER_VALUE, name ? name : "null",
                          "uint8_t");
                return;
            }
            *obj = value;
        }
    }
}

void visit_type_uint16(Visitor *v, uint16_t *obj, const char *name, Error **errp)
{
    int64_t value;
    if (!error_is_set(errp)) {
        if (v->type_uint16) {
            v->type_uint16(v, obj, name, errp);
        } else {
            value = *obj;
            v->type_int(v, &value, name, errp);
            if (value < 0 || value > UINT16_MAX) {
                error_set(errp, QERR_INVALID_PARAMETER_VALUE, name ? name : "null",
                          "uint16_t");
                return;
            }
            *obj = value;
        }
    }
}

void visit_type_uint32(Visitor *v, uint32_t *obj, const char *name, Error **errp)
{
    int64_t value;
    if (!error_is_set(errp)) {
        if (v->type_uint32) {
            v->type_uint32(v, obj, name, errp);
        } else {
            value = *obj;
            v->type_int(v, &value, name, errp);
            if (value < 0 || value > UINT32_MAX) {
                error_set(errp, QERR_INVALID_PARAMETER_VALUE, name ? name : "null",
                          "uint32_t");
                return;
            }
            *obj = value;
        }
    }
}

void visit_type_uint64(Visitor *v, uint64_t *obj, const char *name, Error **errp)
{
    int64_t value;
    if (!error_is_set(errp)) {
        if (v->type_uint64) {
            v->type_uint64(v, obj, name, errp);
        } else {
            value = *obj;
            v->type_int(v, &value, name, errp);
            *obj = value;
        }
    }
}

void visit_type_int8(Visitor *v, int8_t *obj, const char *name, Error **errp)
{
    int64_t value;
    if (!error_is_set(errp)) {
        if (v->type_int8) {
            v->type_int8(v, obj, name, errp);
        } else {
            value = *obj;
            v->type_int(v, &value, name, errp);
            if (value < INT8_MIN || value > INT8_MAX) {
                error_set(errp, QERR_INVALID_PARAMETER_VALUE, name ? name : "null",
                          "int8_t");
                return;
            }
            *obj = value;
        }
    }
}

void visit_type_int16(Visitor *v, int16_t *obj, const char *name, Error **errp)
{
    int64_t value;
    if (!error_is_set(errp)) {
        if (v->type_int16) {
            v->type_int16(v, obj, name, errp);
        } else {
            value = *obj;
            v->type_int(v, &value, name, errp);
            if (value < INT16_MIN || value > INT16_MAX) {
                error_set(errp, QERR_INVALID_PARAMETER_VALUE, name ? name : "null",
                          "int16_t");
                return;
            }
            *obj = value;
        }
    }
}

void visit_type_int32(Visitor *v, int32_t *obj, const char *name, Error **errp)
{
    int64_t value;
    if (!error_is_set(errp)) {
        if (v->type_int32) {
            v->type_int32(v, obj, name, errp);
        } else {
            value = *obj;
            v->type_int(v, &value, name, errp);
            if (value < INT32_MIN || value > INT32_MAX) {
                error_set(errp, QERR_INVALID_PARAMETER_VALUE, name ? name : "null",
                          "int32_t");
                return;
            }
            *obj = value;
        }
    }
}

void visit_type_int64(Visitor *v, int64_t *obj, const char *name, Error **errp)
{
    if (!error_is_set(errp)) {
        if (v->type_int64) {
            v->type_int64(v, obj, name, errp);
        } else {
            v->type_int(v, obj, name, errp);
        }
    }
}

void visit_type_size(Visitor *v, uint64_t *obj, const char *name, Error **errp)
{
    int64_t value;
    if (!error_is_set(errp)) {
        if (v->type_size) {
            v->type_size(v, obj, name, errp);
        } else if (v->type_uint64) {
            v->type_uint64(v, obj, name, errp);
        } else {
            value = *obj;
            v->type_int(v, &value, name, errp);
            *obj = value;
        }
    }
}

void visit_type_bool(Visitor *v, bool *obj, const char *name, Error **errp)
{
    if (!error_is_set(errp)) {
        v->type_bool(v, obj, name, errp);
    }
}

void visit_type_str(Visitor *v, char **obj, const char *name, Error **errp)
{
    if (!error_is_set(errp)) {
        v->type_str(v, obj, name, errp);
    }
}

void visit_type_number(Visitor *v, double *obj, const char *name, Error **errp)
{
    if (!error_is_set(errp)) {
        v->type_number(v, obj, name, errp);
    }
}

void output_type_enum(Visitor *v, int *obj, const char *strings[],
                      const char *kind, const char *name,
                      Error **errp)
{
    int i = 0;
    int value = *obj;
    char *enum_str;

    assert(strings);
    while (strings[i++] != NULL);
    if (value < 0 || value >= i - 1) {
        error_set(errp, QERR_INVALID_PARAMETER, name ? name : "null");
        return;
    }

    enum_str = (char *)strings[value];
    visit_type_str(v, &enum_str, name, errp);
}

void input_type_enum(Visitor *v, int *obj, const char *strings[],
                     const char *kind, const char *name,
                     Error **errp)
{
    int64_t value = 0;
    char *enum_str;

    assert(strings);

    visit_type_str(v, &enum_str, name, errp);
    if (error_is_set(errp)) {
        return;
    }

    while (strings[value] != NULL) {
        if (strcmp(strings[value], enum_str) == 0) {
            break;
        }
        value++;
    }

    if (strings[value] == NULL) {
        error_set(errp, QERR_INVALID_PARAMETER, enum_str);
        g_free(enum_str);
        return;
    }

    g_free(enum_str);
    *obj = value;
}
