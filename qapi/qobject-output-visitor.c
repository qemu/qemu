/*
 * Core Definitions for QAPI/QMP Command Registry
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
#include "qapi/qobject-output-visitor.h"
#include "qapi/visitor-impl.h"
#include "qemu/queue.h"
#include "qemu-common.h"
#include "qapi/qmp/types.h"

typedef struct QStackEntry {
    QObject *value;
    void *qapi; /* sanity check that caller uses same pointer */
    QSLIST_ENTRY(QStackEntry) node;
} QStackEntry;

struct QObjectOutputVisitor {
    Visitor visitor;
    QSLIST_HEAD(, QStackEntry) stack; /* Stack of unfinished containers */
    QObject *root; /* Root of the output visit */
    QObject **result; /* User's storage location for result */
};

#define qobject_output_add(qov, name, value) \
    qobject_output_add_obj(qov, name, QOBJECT(value))
#define qobject_output_push(qov, value, qapi) \
    qobject_output_push_obj(qov, QOBJECT(value), qapi)

static QObjectOutputVisitor *to_qov(Visitor *v)
{
    return container_of(v, QObjectOutputVisitor, visitor);
}

/* Push @value onto the stack of current QObjects being built */
static void qobject_output_push_obj(QObjectOutputVisitor *qov, QObject *value,
                                    void *qapi)
{
    QStackEntry *e = g_malloc0(sizeof(*e));

    assert(qov->root);
    assert(value);
    e->value = value;
    e->qapi = qapi;
    QSLIST_INSERT_HEAD(&qov->stack, e, node);
}

/* Pop a value off the stack of QObjects being built, and return it. */
static QObject *qobject_output_pop(QObjectOutputVisitor *qov, void *qapi)
{
    QStackEntry *e = QSLIST_FIRST(&qov->stack);
    QObject *value;

    assert(e);
    assert(e->qapi == qapi);
    QSLIST_REMOVE_HEAD(&qov->stack, node);
    value = e->value;
    assert(value);
    g_free(e);
    return value;
}

/* Add @value to the current QObject being built.
 * If the stack is visiting a dictionary or list, @value is now owned
 * by that container. Otherwise, @value is now the root.  */
static void qobject_output_add_obj(QObjectOutputVisitor *qov, const char *name,
                                   QObject *value)
{
    QStackEntry *e = QSLIST_FIRST(&qov->stack);
    QObject *cur = e ? e->value : NULL;

    if (!cur) {
        /* Don't allow reuse of visitor on more than one root */
        assert(!qov->root);
        qov->root = value;
    } else {
        switch (qobject_type(cur)) {
        case QTYPE_QDICT:
            assert(name);
            qdict_put_obj(qobject_to_qdict(cur), name, value);
            break;
        case QTYPE_QLIST:
            assert(!name);
            qlist_append_obj(qobject_to_qlist(cur), value);
            break;
        default:
            g_assert_not_reached();
        }
    }
}

static void qobject_output_start_struct(Visitor *v, const char *name,
                                        void **obj, size_t unused, Error **errp)
{
    QObjectOutputVisitor *qov = to_qov(v);
    QDict *dict = qdict_new();

    qobject_output_add(qov, name, dict);
    qobject_output_push(qov, dict, obj);
}

static void qobject_output_end_struct(Visitor *v, void **obj)
{
    QObjectOutputVisitor *qov = to_qov(v);
    QObject *value = qobject_output_pop(qov, obj);
    assert(qobject_type(value) == QTYPE_QDICT);
}

static void qobject_output_start_list(Visitor *v, const char *name,
                                      GenericList **listp, size_t size,
                                      Error **errp)
{
    QObjectOutputVisitor *qov = to_qov(v);
    QList *list = qlist_new();

    qobject_output_add(qov, name, list);
    qobject_output_push(qov, list, listp);
}

static GenericList *qobject_output_next_list(Visitor *v, GenericList *tail,
                                             size_t size)
{
    return tail->next;
}

static void qobject_output_end_list(Visitor *v, void **obj)
{
    QObjectOutputVisitor *qov = to_qov(v);
    QObject *value = qobject_output_pop(qov, obj);
    assert(qobject_type(value) == QTYPE_QLIST);
}

static void qobject_output_type_int64(Visitor *v, const char *name,
                                      int64_t *obj, Error **errp)
{
    QObjectOutputVisitor *qov = to_qov(v);
    qobject_output_add(qov, name, qnum_from_int(*obj));
}

static void qobject_output_type_uint64(Visitor *v, const char *name,
                                       uint64_t *obj, Error **errp)
{
    QObjectOutputVisitor *qov = to_qov(v);
    qobject_output_add(qov, name, qnum_from_uint(*obj));
}

static void qobject_output_type_bool(Visitor *v, const char *name, bool *obj,
                                     Error **errp)
{
    QObjectOutputVisitor *qov = to_qov(v);
    qobject_output_add(qov, name, qbool_from_bool(*obj));
}

static void qobject_output_type_str(Visitor *v, const char *name, char **obj,
                                    Error **errp)
{
    QObjectOutputVisitor *qov = to_qov(v);
    if (*obj) {
        qobject_output_add(qov, name, qstring_from_str(*obj));
    } else {
        qobject_output_add(qov, name, qstring_from_str(""));
    }
}

static void qobject_output_type_number(Visitor *v, const char *name,
                                       double *obj, Error **errp)
{
    QObjectOutputVisitor *qov = to_qov(v);
    qobject_output_add(qov, name, qnum_from_double(*obj));
}

static void qobject_output_type_any(Visitor *v, const char *name,
                                    QObject **obj, Error **errp)
{
    QObjectOutputVisitor *qov = to_qov(v);
    qobject_incref(*obj);
    qobject_output_add_obj(qov, name, *obj);
}

static void qobject_output_type_null(Visitor *v, const char *name,
                                     QNull **obj, Error **errp)
{
    QObjectOutputVisitor *qov = to_qov(v);
    qobject_output_add(qov, name, qnull());
}

/* Finish building, and return the root object.
 * The root object is never null. The caller becomes the object's
 * owner, and should use qobject_decref() when done with it.  */
static void qobject_output_complete(Visitor *v, void *opaque)
{
    QObjectOutputVisitor *qov = to_qov(v);

    /* A visit must have occurred, with each start paired with end.  */
    assert(qov->root && QSLIST_EMPTY(&qov->stack));
    assert(opaque == qov->result);

    qobject_incref(qov->root);
    *qov->result = qov->root;
    qov->result = NULL;
}

static void qobject_output_free(Visitor *v)
{
    QObjectOutputVisitor *qov = to_qov(v);
    QStackEntry *e;

    while (!QSLIST_EMPTY(&qov->stack)) {
        e = QSLIST_FIRST(&qov->stack);
        QSLIST_REMOVE_HEAD(&qov->stack, node);
        g_free(e);
    }

    qobject_decref(qov->root);
    g_free(qov);
}

Visitor *qobject_output_visitor_new(QObject **result)
{
    QObjectOutputVisitor *v;

    v = g_malloc0(sizeof(*v));

    v->visitor.type = VISITOR_OUTPUT;
    v->visitor.start_struct = qobject_output_start_struct;
    v->visitor.end_struct = qobject_output_end_struct;
    v->visitor.start_list = qobject_output_start_list;
    v->visitor.next_list = qobject_output_next_list;
    v->visitor.end_list = qobject_output_end_list;
    v->visitor.type_int64 = qobject_output_type_int64;
    v->visitor.type_uint64 = qobject_output_type_uint64;
    v->visitor.type_bool = qobject_output_type_bool;
    v->visitor.type_str = qobject_output_type_str;
    v->visitor.type_number = qobject_output_type_number;
    v->visitor.type_any = qobject_output_type_any;
    v->visitor.type_null = qobject_output_type_null;
    v->visitor.complete = qobject_output_complete;
    v->visitor.free = qobject_output_free;

    *result = NULL;
    v->result = result;

    return &v->visitor;
}
