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
#ifndef QAPI_VISITOR_CORE_H
#define QAPI_VISITOR_CORE_H

#include "qemu/typedefs.h"
#include "qapi/qmp/qobject.h"

/* This struct is layout-compatible with all other *List structs
 * created by the qapi generator.  It is used as a typical
 * singly-linked list. */
typedef struct GenericList {
    struct GenericList *next;
    char padding[];
} GenericList;

/* This struct is layout-compatible with all Alternate types
 * created by the qapi generator. */
typedef struct GenericAlternate {
    QType type;
    char padding[];
} GenericAlternate;

void visit_start_struct(Visitor *v, const char *name, void **obj,
                        size_t size, Error **errp);
void visit_end_struct(Visitor *v, Error **errp);

void visit_start_list(Visitor *v, const char *name, Error **errp);
GenericList *visit_next_list(Visitor *v, GenericList **list, size_t size);
void visit_end_list(Visitor *v);

/*
 * Start the visit of an alternate @obj with the given @size.
 *
 * @name specifies the relationship to the containing struct (ignored
 * for a top level visit, the name of the key if this alternate is
 * part of an object, or NULL if this alternate is part of a list).
 *
 * @obj must not be NULL. Input visitors will allocate @obj and
 * determine the qtype of the next thing to be visited, stored in
 * (*@obj)->type.  Other visitors will leave @obj unchanged.
 *
 * If @promote_int, treat integers as QTYPE_FLOAT.
 *
 * If successful, this must be paired with visit_end_alternate(), even
 * if visiting the contents of the alternate fails.
 */
void visit_start_alternate(Visitor *v, const char *name,
                           GenericAlternate **obj, size_t size,
                           bool promote_int, Error **errp);

/*
 * Finish visiting an alternate type.
 *
 * Must be called after a successful visit_start_alternate(), even if
 * an error occurred in the meantime.
 *
 * TODO: Should all the visit_end_* interfaces take obj parameter, so
 * that dealloc visitor need not track what was passed in visit_start?
 */
void visit_end_alternate(Visitor *v);

/**
 * Check if an optional member @name of an object needs visiting.
 * For input visitors, set *@present according to whether the
 * corresponding visit_type_*() needs calling; for other visitors,
 * leave *@present unchanged.  Return *@present for convenience.
 */
bool visit_optional(Visitor *v, const char *name, bool *present);

void visit_type_enum(Visitor *v, const char *name, int *obj,
                     const char *const strings[], Error **errp);
void visit_type_int(Visitor *v, const char *name, int64_t *obj, Error **errp);
void visit_type_uint8(Visitor *v, const char *name, uint8_t *obj,
                      Error **errp);
void visit_type_uint16(Visitor *v, const char *name, uint16_t *obj,
                       Error **errp);
void visit_type_uint32(Visitor *v, const char *name, uint32_t *obj,
                       Error **errp);
void visit_type_uint64(Visitor *v, const char *name, uint64_t *obj,
                       Error **errp);
void visit_type_int8(Visitor *v, const char *name, int8_t *obj, Error **errp);
void visit_type_int16(Visitor *v, const char *name, int16_t *obj,
                      Error **errp);
void visit_type_int32(Visitor *v, const char *name, int32_t *obj,
                      Error **errp);
void visit_type_int64(Visitor *v, const char *name, int64_t *obj,
                      Error **errp);
void visit_type_size(Visitor *v, const char *name, uint64_t *obj,
                     Error **errp);
void visit_type_bool(Visitor *v, const char *name, bool *obj, Error **errp);
void visit_type_str(Visitor *v, const char *name, char **obj, Error **errp);
void visit_type_number(Visitor *v, const char *name, double *obj,
                       Error **errp);
void visit_type_any(Visitor *v, const char *name, QObject **obj, Error **errp);

#endif
