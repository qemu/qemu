/*
 * Dealloc Visitor
 *
 * Copyright (C) 2012-2016 Red Hat, Inc.
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Michael Roth   <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/dealloc-visitor.h"
#include "qemu/queue.h"
#include "qemu-common.h"
#include "qapi/qmp/types.h"
#include "qapi/visitor-impl.h"

typedef struct StackEntry
{
    void *value;
    QTAILQ_ENTRY(StackEntry) node;
} StackEntry;

struct QapiDeallocVisitor
{
    Visitor visitor;
    QTAILQ_HEAD(, StackEntry) stack;
};

static QapiDeallocVisitor *to_qov(Visitor *v)
{
    return container_of(v, QapiDeallocVisitor, visitor);
}

static void qapi_dealloc_push(QapiDeallocVisitor *qov, void *value)
{
    StackEntry *e = g_malloc0(sizeof(*e));

    e->value = value;

    QTAILQ_INSERT_HEAD(&qov->stack, e, node);
}

static void *qapi_dealloc_pop(QapiDeallocVisitor *qov)
{
    StackEntry *e = QTAILQ_FIRST(&qov->stack);
    QObject *value;
    QTAILQ_REMOVE(&qov->stack, e, node);
    value = e->value;
    g_free(e);
    return value;
}

static void qapi_dealloc_start_struct(Visitor *v, const char *name, void **obj,
                                      size_t unused, Error **errp)
{
    QapiDeallocVisitor *qov = to_qov(v);
    qapi_dealloc_push(qov, obj);
}

static void qapi_dealloc_end_struct(Visitor *v)
{
    QapiDeallocVisitor *qov = to_qov(v);
    void **obj = qapi_dealloc_pop(qov);
    if (obj) {
        g_free(*obj);
    }
}

static void qapi_dealloc_start_alternate(Visitor *v, const char *name,
                                         GenericAlternate **obj, size_t size,
                                         bool promote_int, Error **errp)
{
    QapiDeallocVisitor *qov = to_qov(v);
    qapi_dealloc_push(qov, obj);
}

static void qapi_dealloc_end_alternate(Visitor *v)
{
    QapiDeallocVisitor *qov = to_qov(v);
    void **obj = qapi_dealloc_pop(qov);
    if (obj) {
        g_free(*obj);
    }
}

static void qapi_dealloc_start_list(Visitor *v, const char *name,
                                    GenericList **list, size_t size,
                                    Error **errp)
{
}

static GenericList *qapi_dealloc_next_list(Visitor *v, GenericList *tail,
                                           size_t size)
{
    GenericList *next = tail->next;
    g_free(tail);
    return next;
}

static void qapi_dealloc_end_list(Visitor *v)
{
}

static void qapi_dealloc_type_str(Visitor *v, const char *name, char **obj,
                                  Error **errp)
{
    if (obj) {
        g_free(*obj);
    }
}

static void qapi_dealloc_type_int64(Visitor *v, const char *name, int64_t *obj,
                                    Error **errp)
{
}

static void qapi_dealloc_type_uint64(Visitor *v, const char *name,
                                     uint64_t *obj, Error **errp)
{
}

static void qapi_dealloc_type_bool(Visitor *v, const char *name, bool *obj,
                                   Error **errp)
{
}

static void qapi_dealloc_type_number(Visitor *v, const char *name, double *obj,
                                     Error **errp)
{
}

static void qapi_dealloc_type_anything(Visitor *v, const char *name,
                                       QObject **obj, Error **errp)
{
    if (obj) {
        qobject_decref(*obj);
    }
}

static void qapi_dealloc_type_null(Visitor *v, const char *name, Error **errp)
{
}

Visitor *qapi_dealloc_get_visitor(QapiDeallocVisitor *v)
{
    return &v->visitor;
}

void qapi_dealloc_visitor_cleanup(QapiDeallocVisitor *v)
{
    g_free(v);
}

QapiDeallocVisitor *qapi_dealloc_visitor_new(void)
{
    QapiDeallocVisitor *v;

    v = g_malloc0(sizeof(*v));

    v->visitor.type = VISITOR_DEALLOC;
    v->visitor.start_struct = qapi_dealloc_start_struct;
    v->visitor.end_struct = qapi_dealloc_end_struct;
    v->visitor.start_alternate = qapi_dealloc_start_alternate;
    v->visitor.end_alternate = qapi_dealloc_end_alternate;
    v->visitor.start_list = qapi_dealloc_start_list;
    v->visitor.next_list = qapi_dealloc_next_list;
    v->visitor.end_list = qapi_dealloc_end_list;
    v->visitor.type_int64 = qapi_dealloc_type_int64;
    v->visitor.type_uint64 = qapi_dealloc_type_uint64;
    v->visitor.type_bool = qapi_dealloc_type_bool;
    v->visitor.type_str = qapi_dealloc_type_str;
    v->visitor.type_number = qapi_dealloc_type_number;
    v->visitor.type_any = qapi_dealloc_type_anything;
    v->visitor.type_null = qapi_dealloc_type_null;

    QTAILQ_INIT(&v->stack);

    return v;
}
