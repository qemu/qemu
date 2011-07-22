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
#ifndef QAPI_VISITOR_CORE_H
#define QAPI_VISITOR_CORE_H

#include "qapi/qapi-types-core.h"
#include <stdlib.h>

typedef struct GenericList
{
    void *value;
    struct GenericList *next;
} GenericList;

typedef struct Visitor Visitor;

struct Visitor
{
    /* Must be set */
    void (*start_struct)(Visitor *v, void **obj, const char *kind,
                         const char *name, size_t size, Error **errp);
    void (*end_struct)(Visitor *v, Error **errp);

    void (*start_list)(Visitor *v, const char *name, Error **errp);
    GenericList *(*next_list)(Visitor *v, GenericList **list, Error **errp);
    void (*end_list)(Visitor *v, Error **errp);

    void (*type_enum)(Visitor *v, int *obj, const char *strings[],
                      const char *kind, const char *name, Error **errp);

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
};

void visit_start_handle(Visitor *v, void **obj, const char *kind,
                        const char *name, Error **errp);
void visit_end_handle(Visitor *v, Error **errp);
void visit_start_struct(Visitor *v, void **obj, const char *kind,
                        const char *name, size_t size, Error **errp);
void visit_end_struct(Visitor *v, Error **errp);
void visit_start_list(Visitor *v, const char *name, Error **errp);
GenericList *visit_next_list(Visitor *v, GenericList **list, Error **errp);
void visit_end_list(Visitor *v, Error **errp);
void visit_start_optional(Visitor *v, bool *present, const char *name,
                          Error **errp);
void visit_end_optional(Visitor *v, Error **errp);
void visit_type_enum(Visitor *v, int *obj, const char *strings[],
                     const char *kind, const char *name, Error **errp);
void visit_type_int(Visitor *v, int64_t *obj, const char *name, Error **errp);
void visit_type_bool(Visitor *v, bool *obj, const char *name, Error **errp);
void visit_type_str(Visitor *v, char **obj, const char *name, Error **errp);
void visit_type_number(Visitor *v, double *obj, const char *name, Error **errp);

#endif
