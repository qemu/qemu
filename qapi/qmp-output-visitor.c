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
#include "qapi/qmp-output-visitor.h"
#include "qapi/visitor-impl.h"
#include "qemu/queue.h"
#include "qemu-common.h"
#include "qapi/qmp/types.h"

typedef struct QStackEntry
{
    QObject *value;
    QTAILQ_ENTRY(QStackEntry) node;
} QStackEntry;

typedef QTAILQ_HEAD(QStack, QStackEntry) QStack;

struct QmpOutputVisitor
{
    Visitor visitor;
    QStack stack; /* Stack of containers that haven't yet been finished */
    QObject *root; /* Root of the output visit */
};

#define qmp_output_add(qov, name, value) \
    qmp_output_add_obj(qov, name, QOBJECT(value))
#define qmp_output_push(qov, value) qmp_output_push_obj(qov, QOBJECT(value))

static QmpOutputVisitor *to_qov(Visitor *v)
{
    return container_of(v, QmpOutputVisitor, visitor);
}

/* Push @value onto the stack of current QObjects being built */
static void qmp_output_push_obj(QmpOutputVisitor *qov, QObject *value)
{
    QStackEntry *e = g_malloc0(sizeof(*e));

    assert(qov->root);
    assert(value);
    e->value = value;
    QTAILQ_INSERT_HEAD(&qov->stack, e, node);
}

/* Pop a value off the stack of QObjects being built, and return it. */
static QObject *qmp_output_pop(QmpOutputVisitor *qov)
{
    QStackEntry *e = QTAILQ_FIRST(&qov->stack);
    QObject *value;

    assert(e);
    QTAILQ_REMOVE(&qov->stack, e, node);
    value = e->value;
    assert(value);
    g_free(e);
    return value;
}

/* Add @value to the current QObject being built.
 * If the stack is visiting a dictionary or list, @value is now owned
 * by that container. Otherwise, @value is now the root.  */
static void qmp_output_add_obj(QmpOutputVisitor *qov, const char *name,
                               QObject *value)
{
    QStackEntry *e = QTAILQ_FIRST(&qov->stack);
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

static void qmp_output_start_struct(Visitor *v, const char *name, void **obj,
                                    size_t unused, Error **errp)
{
    QmpOutputVisitor *qov = to_qov(v);
    QDict *dict = qdict_new();

    qmp_output_add(qov, name, dict);
    qmp_output_push(qov, dict);
}

static void qmp_output_end_struct(Visitor *v)
{
    QmpOutputVisitor *qov = to_qov(v);
    QObject *value = qmp_output_pop(qov);
    assert(qobject_type(value) == QTYPE_QDICT);
}

static void qmp_output_start_list(Visitor *v, const char *name,
                                  GenericList **listp, size_t size,
                                  Error **errp)
{
    QmpOutputVisitor *qov = to_qov(v);
    QList *list = qlist_new();

    qmp_output_add(qov, name, list);
    qmp_output_push(qov, list);
}

static GenericList *qmp_output_next_list(Visitor *v, GenericList *tail,
                                         size_t size)
{
    return tail->next;
}

static void qmp_output_end_list(Visitor *v)
{
    QmpOutputVisitor *qov = to_qov(v);
    QObject *value = qmp_output_pop(qov);
    assert(qobject_type(value) == QTYPE_QLIST);
}

static void qmp_output_type_int64(Visitor *v, const char *name, int64_t *obj,
                                  Error **errp)
{
    QmpOutputVisitor *qov = to_qov(v);
    qmp_output_add(qov, name, qint_from_int(*obj));
}

static void qmp_output_type_uint64(Visitor *v, const char *name, uint64_t *obj,
                                   Error **errp)
{
    /* FIXME: QMP outputs values larger than INT64_MAX as negative */
    QmpOutputVisitor *qov = to_qov(v);
    qmp_output_add(qov, name, qint_from_int(*obj));
}

static void qmp_output_type_bool(Visitor *v, const char *name, bool *obj,
                                 Error **errp)
{
    QmpOutputVisitor *qov = to_qov(v);
    qmp_output_add(qov, name, qbool_from_bool(*obj));
}

static void qmp_output_type_str(Visitor *v, const char *name, char **obj,
                                Error **errp)
{
    QmpOutputVisitor *qov = to_qov(v);
    if (*obj) {
        qmp_output_add(qov, name, qstring_from_str(*obj));
    } else {
        qmp_output_add(qov, name, qstring_from_str(""));
    }
}

static void qmp_output_type_number(Visitor *v, const char *name, double *obj,
                                   Error **errp)
{
    QmpOutputVisitor *qov = to_qov(v);
    qmp_output_add(qov, name, qfloat_from_double(*obj));
}

static void qmp_output_type_any(Visitor *v, const char *name, QObject **obj,
                                Error **errp)
{
    QmpOutputVisitor *qov = to_qov(v);
    qobject_incref(*obj);
    qmp_output_add_obj(qov, name, *obj);
}

static void qmp_output_type_null(Visitor *v, const char *name, Error **errp)
{
    QmpOutputVisitor *qov = to_qov(v);
    qmp_output_add_obj(qov, name, qnull());
}

/* Finish building, and return the root object.
 * The root object is never null. The caller becomes the object's
 * owner, and should use qobject_decref() when done with it.  */
QObject *qmp_output_get_qobject(QmpOutputVisitor *qov)
{
    /* A visit must have occurred, with each start paired with end.  */
    assert(qov->root && QTAILQ_EMPTY(&qov->stack));

    qobject_incref(qov->root);
    return qov->root;
}

Visitor *qmp_output_get_visitor(QmpOutputVisitor *v)
{
    return &v->visitor;
}

void qmp_output_visitor_cleanup(QmpOutputVisitor *v)
{
    QStackEntry *e, *tmp;

    QTAILQ_FOREACH_SAFE(e, &v->stack, node, tmp) {
        QTAILQ_REMOVE(&v->stack, e, node);
        g_free(e);
    }

    qobject_decref(v->root);
    g_free(v);
}

QmpOutputVisitor *qmp_output_visitor_new(void)
{
    QmpOutputVisitor *v;

    v = g_malloc0(sizeof(*v));

    v->visitor.type = VISITOR_OUTPUT;
    v->visitor.start_struct = qmp_output_start_struct;
    v->visitor.end_struct = qmp_output_end_struct;
    v->visitor.start_list = qmp_output_start_list;
    v->visitor.next_list = qmp_output_next_list;
    v->visitor.end_list = qmp_output_end_list;
    v->visitor.type_int64 = qmp_output_type_int64;
    v->visitor.type_uint64 = qmp_output_type_uint64;
    v->visitor.type_bool = qmp_output_type_bool;
    v->visitor.type_str = qmp_output_type_str;
    v->visitor.type_number = qmp_output_type_number;
    v->visitor.type_any = qmp_output_type_any;
    v->visitor.type_null = qmp_output_type_null;

    QTAILQ_INIT(&v->stack);

    return v;
}
