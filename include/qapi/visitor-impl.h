/*
 * Core Definitions for QAPI Visitor implementations
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * Author: Paolo Bonizni <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */
#ifndef QAPI_VISITOR_IMPL_H
#define QAPI_VISITOR_IMPL_H

#include "qapi/error.h"
#include "qapi/visitor.h"

struct Visitor
{
    /* Must be set */
    void (*start_struct)(Visitor *v, void **obj, const char *kind,
                         const char *name, size_t size, Error **errp);
    void (*end_struct)(Visitor *v, Error **errp);

    void (*start_implicit_struct)(Visitor *v, void **obj, size_t size,
                                  Error **errp);
    void (*end_implicit_struct)(Visitor *v, Error **errp);

    void (*start_list)(Visitor *v, const char *name, Error **errp);
    GenericList *(*next_list)(Visitor *v, GenericList **list, Error **errp);
    void (*end_list)(Visitor *v, Error **errp);

    void (*type_enum)(Visitor *v, int *obj, const char *strings[],
                      const char *kind, const char *name, Error **errp);
    void (*get_next_type)(Visitor *v, int *kind, const int *qobjects,
                          const char *name, Error **errp);

    void (*type_int)(Visitor *v, int64_t *obj, const char *name, Error **errp);
    void (*type_bool)(Visitor *v, bool *obj, const char *name, Error **errp);
    void (*type_str)(Visitor *v, char **obj, const char *name, Error **errp);
    void (*type_number)(Visitor *v, double *obj, const char *name,
                        Error **errp);

    /* May be NULL */
    void (*start_optional)(Visitor *v, bool *present, const char *name,
                           Error **errp);
    void (*end_optional)(Visitor *v, Error **errp);

    void (*start_handle)(Visitor *v, void **obj, const char *kind,
                         const char *name, Error **errp);
    void (*end_handle)(Visitor *v, Error **errp);
    void (*type_uint8)(Visitor *v, uint8_t *obj, const char *name, Error **errp);
    void (*type_uint16)(Visitor *v, uint16_t *obj, const char *name, Error **errp);
    void (*type_uint32)(Visitor *v, uint32_t *obj, const char *name, Error **errp);
    void (*type_uint64)(Visitor *v, uint64_t *obj, const char *name, Error **errp);
    void (*type_int8)(Visitor *v, int8_t *obj, const char *name, Error **errp);
    void (*type_int16)(Visitor *v, int16_t *obj, const char *name, Error **errp);
    void (*type_int32)(Visitor *v, int32_t *obj, const char *name, Error **errp);
    void (*type_int64)(Visitor *v, int64_t *obj, const char *name, Error **errp);
    /* visit_type_size() falls back to (*type_uint64)() if type_size is unset */
    void (*type_size)(Visitor *v, uint64_t *obj, const char *name, Error **errp);
};

void input_type_enum(Visitor *v, int *obj, const char *strings[],
                     const char *kind, const char *name, Error **errp);
void output_type_enum(Visitor *v, int *obj, const char *strings[],
                      const char *kind, const char *name, Error **errp);

#endif
