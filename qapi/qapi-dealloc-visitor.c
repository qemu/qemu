/*
 * Dealloc Visitor
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Michael Roth   <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qapi-dealloc-visitor.h"
#include "qemu-queue.h"
#include "qemu-common.h"
#include "qemu-objects.h"

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

static void qapi_dealloc_start_struct(Visitor *v, void **obj, const char *kind,
                                      const char *name, size_t unused,
                                      Error **errp)
{
    QapiDeallocVisitor *qov = to_qov(v);
    qapi_dealloc_push(qov, obj);
}

static void qapi_dealloc_end_struct(Visitor *v, Error **errp)
{
    QapiDeallocVisitor *qov = to_qov(v);
    void **obj = qapi_dealloc_pop(qov);
    if (obj) {
        g_free(*obj);
    }
}

static void qapi_dealloc_start_list(Visitor *v, const char *name, Error **errp)
{
}

static GenericList *qapi_dealloc_next_list(Visitor *v, GenericList **list,
                                           Error **errp)
{
    GenericList *retval = *list;
    g_free(retval->value);
    *list = retval->next;
    return retval;
}

static void qapi_dealloc_end_list(Visitor *v, Error **errp)
{
}

static void qapi_dealloc_type_str(Visitor *v, char **obj, const char *name,
                                  Error **errp)
{
    if (obj) {
        g_free(*obj);
    }
}

static void qapi_dealloc_type_int(Visitor *v, int64_t *obj, const char *name,
                                  Error **errp)
{
}

static void qapi_dealloc_type_bool(Visitor *v, bool *obj, const char *name,
                                   Error **errp)
{
}

static void qapi_dealloc_type_number(Visitor *v, double *obj, const char *name,
                                     Error **errp)
{
}

static void qapi_dealloc_type_enum(Visitor *v, int *obj, const char *strings[],
                                   const char *kind, const char *name,
                                   Error **errp)
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

    v->visitor.start_struct = qapi_dealloc_start_struct;
    v->visitor.end_struct = qapi_dealloc_end_struct;
    v->visitor.start_list = qapi_dealloc_start_list;
    v->visitor.next_list = qapi_dealloc_next_list;
    v->visitor.end_list = qapi_dealloc_end_list;
    v->visitor.type_enum = qapi_dealloc_type_enum;
    v->visitor.type_int = qapi_dealloc_type_int;
    v->visitor.type_bool = qapi_dealloc_type_bool;
    v->visitor.type_str = qapi_dealloc_type_str;
    v->visitor.type_number = qapi_dealloc_type_number;

    QTAILQ_INIT(&v->stack);

    return v;
}
