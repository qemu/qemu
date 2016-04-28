/*
 * Core Definitions for QAPI Visitor implementations
 *
 * Copyright (C) 2012-2016 Red Hat, Inc.
 *
 * Author: Paolo Bonizni <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */
#ifndef QAPI_VISITOR_IMPL_H
#define QAPI_VISITOR_IMPL_H

#include "qapi/visitor.h"

/*
 * There are three classes of visitors; setting the class determines
 * how QAPI enums are visited, as well as what additional restrictions
 * can be asserted.
 */
typedef enum VisitorType {
    VISITOR_INPUT,
    VISITOR_OUTPUT,
    VISITOR_DEALLOC,
} VisitorType;

struct Visitor
{
    /* Must be set */
    void (*start_struct)(Visitor *v, const char *name, void **obj,
                         size_t size, Error **errp);
    void (*end_struct)(Visitor *v, Error **errp);

    void (*start_list)(Visitor *v, const char *name, Error **errp);
    /* Must be set */
    GenericList *(*next_list)(Visitor *v, GenericList **list, size_t size);
    /* Must be set */
    void (*end_list)(Visitor *v);

    /* Optional, needed for input and dealloc visitors.  */
    void (*start_alternate)(Visitor *v, const char *name,
                            GenericAlternate **obj, size_t size,
                            bool promote_int, Error **errp);

    /* Optional, needed for dealloc visitor.  */
    void (*end_alternate)(Visitor *v);

    /* Must be set. */
    void (*type_int64)(Visitor *v, const char *name, int64_t *obj,
                       Error **errp);
    /* Must be set. */
    void (*type_uint64)(Visitor *v, const char *name, uint64_t *obj,
                        Error **errp);
    /* Optional; fallback is type_uint64().  */
    void (*type_size)(Visitor *v, const char *name, uint64_t *obj,
                      Error **errp);
    /* Must be set. */
    void (*type_bool)(Visitor *v, const char *name, bool *obj, Error **errp);
    void (*type_str)(Visitor *v, const char *name, char **obj, Error **errp);
    void (*type_number)(Visitor *v, const char *name, double *obj,
                        Error **errp);
    void (*type_any)(Visitor *v, const char *name, QObject **obj,
                     Error **errp);

    /* May be NULL; most useful for input visitors. */
    void (*optional)(Visitor *v, const char *name, bool *present);

    /* Must be set */
    VisitorType type;
};

#endif
