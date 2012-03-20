/*
 * Core Definitions for QAPI/QMP Command Registry
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

#include "qmp-output-visitor.h"
#include "qapi/qapi-visit-impl.h"
#include "qemu-queue.h"
#include "qemu-common.h"
#include "qemu-objects.h"
#include "qerror.h"

typedef struct QStackEntry
{
    QObject *value;
    bool is_list_head;
    QTAILQ_ENTRY(QStackEntry) node;
} QStackEntry;

typedef QTAILQ_HEAD(QStack, QStackEntry) QStack;

struct QmpOutputVisitor
{
    Visitor visitor;
    QStack stack;
};

#define qmp_output_add(qov, name, value) \
    qmp_output_add_obj(qov, name, QOBJECT(value))
#define qmp_output_push(qov, value) qmp_output_push_obj(qov, QOBJECT(value))

static QmpOutputVisitor *to_qov(Visitor *v)
{
    return container_of(v, QmpOutputVisitor, visitor);
}

static void qmp_output_push_obj(QmpOutputVisitor *qov, QObject *value)
{
    QStackEntry *e = g_malloc0(sizeof(*e));

    e->value = value;
    if (qobject_type(e->value) == QTYPE_QLIST) {
        e->is_list_head = true;
    }
    QTAILQ_INSERT_HEAD(&qov->stack, e, node);
}

static QObject *qmp_output_pop(QmpOutputVisitor *qov)
{
    QStackEntry *e = QTAILQ_FIRST(&qov->stack);
    QObject *value;
    QTAILQ_REMOVE(&qov->stack, e, node);
    value = e->value;
    g_free(e);
    return value;
}

static QObject *qmp_output_first(QmpOutputVisitor *qov)
{
    QStackEntry *e = QTAILQ_LAST(&qov->stack, QStack);
    return e->value;
}

static QObject *qmp_output_last(QmpOutputVisitor *qov)
{
    QStackEntry *e = QTAILQ_FIRST(&qov->stack);
    return e->value;
}

static void qmp_output_add_obj(QmpOutputVisitor *qov, const char *name,
                               QObject *value)
{
    QObject *cur;

    if (QTAILQ_EMPTY(&qov->stack)) {
        qmp_output_push_obj(qov, value);
        return;
    }

    cur = qmp_output_last(qov);

    switch (qobject_type(cur)) {
    case QTYPE_QDICT:
        qdict_put_obj(qobject_to_qdict(cur), name, value);
        break;
    case QTYPE_QLIST:
        qlist_append_obj(qobject_to_qlist(cur), value);
        break;
    default:
        qobject_decref(qmp_output_pop(qov));
        qmp_output_push_obj(qov, value);
        break;
    }
}

static void qmp_output_start_struct(Visitor *v, void **obj, const char *kind,
                                    const char *name, size_t unused,
                                    Error **errp)
{
    QmpOutputVisitor *qov = to_qov(v);
    QDict *dict = qdict_new();

    qmp_output_add(qov, name, dict);
    qmp_output_push(qov, dict);
}

static void qmp_output_end_struct(Visitor *v, Error **errp)
{
    QmpOutputVisitor *qov = to_qov(v);
    qmp_output_pop(qov);
}

static void qmp_output_start_list(Visitor *v, const char *name, Error **errp)
{
    QmpOutputVisitor *qov = to_qov(v);
    QList *list = qlist_new();

    qmp_output_add(qov, name, list);
    qmp_output_push(qov, list);
}

static GenericList *qmp_output_next_list(Visitor *v, GenericList **listp,
                                         Error **errp)
{
    GenericList *list = *listp;
    QmpOutputVisitor *qov = to_qov(v);
    QStackEntry *e = QTAILQ_FIRST(&qov->stack);

    assert(e);
    if (e->is_list_head) {
        e->is_list_head = false;
        return list;
    }

    return list ? list->next : NULL;
}

static void qmp_output_end_list(Visitor *v, Error **errp)
{
    QmpOutputVisitor *qov = to_qov(v);
    qmp_output_pop(qov);
}

static void qmp_output_type_int(Visitor *v, int64_t *obj, const char *name,
                                Error **errp)
{
    QmpOutputVisitor *qov = to_qov(v);
    qmp_output_add(qov, name, qint_from_int(*obj));
}

static void qmp_output_type_bool(Visitor *v, bool *obj, const char *name,
                                 Error **errp)
{
    QmpOutputVisitor *qov = to_qov(v);
    qmp_output_add(qov, name, qbool_from_int(*obj));
}

static void qmp_output_type_str(Visitor *v, char **obj, const char *name,
                                Error **errp)
{
    QmpOutputVisitor *qov = to_qov(v);
    if (*obj) {
        qmp_output_add(qov, name, qstring_from_str(*obj));
    } else {
        qmp_output_add(qov, name, qstring_from_str(""));
    }
}

static void qmp_output_type_number(Visitor *v, double *obj, const char *name,
                                   Error **errp)
{
    QmpOutputVisitor *qov = to_qov(v);
    qmp_output_add(qov, name, qfloat_from_double(*obj));
}

QObject *qmp_output_get_qobject(QmpOutputVisitor *qov)
{
    QObject *obj = qmp_output_first(qov);
    if (obj) {
        qobject_incref(obj);
    }
    return obj;
}

Visitor *qmp_output_get_visitor(QmpOutputVisitor *v)
{
    return &v->visitor;
}

void qmp_output_visitor_cleanup(QmpOutputVisitor *v)
{
    QStackEntry *e, *tmp;

    /* The bottom QStackEntry, if any, owns the root QObject. See the
     * qmp_output_push_obj() invocations in qmp_output_add_obj(). */
    QObject *root = QTAILQ_EMPTY(&v->stack) ? NULL : qmp_output_first(v);

    QTAILQ_FOREACH_SAFE(e, &v->stack, node, tmp) {
        QTAILQ_REMOVE(&v->stack, e, node);
        g_free(e);
    }

    qobject_decref(root);
    g_free(v);
}

QmpOutputVisitor *qmp_output_visitor_new(void)
{
    QmpOutputVisitor *v;

    v = g_malloc0(sizeof(*v));

    v->visitor.start_struct = qmp_output_start_struct;
    v->visitor.end_struct = qmp_output_end_struct;
    v->visitor.start_list = qmp_output_start_list;
    v->visitor.next_list = qmp_output_next_list;
    v->visitor.end_list = qmp_output_end_list;
    v->visitor.type_enum = output_type_enum;
    v->visitor.type_int = qmp_output_type_int;
    v->visitor.type_bool = qmp_output_type_bool;
    v->visitor.type_str = qmp_output_type_str;
    v->visitor.type_number = qmp_output_type_number;

    QTAILQ_INIT(&v->stack);

    return v;
}
