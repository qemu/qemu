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

#include "qapi/qapi-visit-core.h"

void visit_start_handle(Visitor *v, void **obj, const char *kind,
                        const char *name, Error **errp)
{
    if (!error_is_set(errp) && v->start_handle) {
        v->start_handle(v, obj, kind, name, errp);
    }
}

void visit_end_handle(Visitor *v, Error **errp)
{
    if (!error_is_set(errp) && v->end_handle) {
        v->end_handle(v, errp);
    }
}

void visit_start_struct(Visitor *v, void **obj, const char *kind,
                        const char *name, size_t size, Error **errp)
{
    if (!error_is_set(errp)) {
        v->start_struct(v, obj, kind, name, size, errp);
    }
}

void visit_end_struct(Visitor *v, Error **errp)
{
    if (!error_is_set(errp)) {
        v->end_struct(v, errp);
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
    if (!error_is_set(errp)) {
        v->end_list(v, errp);
    }
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
