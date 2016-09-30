/*
 * Input Visitor
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
#include "qapi/error.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/visitor-impl.h"
#include "qemu/queue.h"
#include "qemu-common.h"
#include "qapi/qmp/types.h"
#include "qapi/qmp/qerror.h"

#define QIV_STACK_SIZE 1024

typedef struct StackObject
{
    QObject *obj; /* Object being visited */
    void *qapi; /* sanity check that caller uses same pointer */

    GHashTable *h;           /* If obj is dict: unvisited keys */
    const QListEntry *entry; /* If obj is list: unvisited tail */

    QSLIST_ENTRY(StackObject) node;
} StackObject;

struct QObjectInputVisitor
{
    Visitor visitor;

    /* Root of visit at visitor creation. */
    QObject *root;

    /* Stack of objects being visited (all entries will be either
     * QDict or QList). */
    QSLIST_HEAD(, StackObject) stack;

    /* True to reject parse in visit_end_struct() if unvisited keys remain. */
    bool strict;
};

static QObjectInputVisitor *to_qiv(Visitor *v)
{
    return container_of(v, QObjectInputVisitor, visitor);
}

static QObject *qobject_input_get_object(QObjectInputVisitor *qiv,
                                         const char *name,
                                         bool consume, Error **errp)
{
    StackObject *tos;
    QObject *qobj;
    QObject *ret;

    if (QSLIST_EMPTY(&qiv->stack)) {
        /* Starting at root, name is ignored. */
        assert(qiv->root);
        return qiv->root;
    }

    /* We are in a container; find the next element. */
    tos = QSLIST_FIRST(&qiv->stack);
    qobj = tos->obj;
    assert(qobj);

    if (qobject_type(qobj) == QTYPE_QDICT) {
        assert(name);
        ret = qdict_get(qobject_to_qdict(qobj), name);
        if (tos->h && consume && ret) {
            bool removed = g_hash_table_remove(tos->h, name);
            assert(removed);
        }
        if (!ret) {
            error_setg(errp, QERR_MISSING_PARAMETER, name);
        }
    } else {
        assert(qobject_type(qobj) == QTYPE_QLIST);
        assert(!name);
        ret = qlist_entry_obj(tos->entry);
        assert(ret);
        if (consume) {
            tos->entry = qlist_next(tos->entry);
        }
    }

    return ret;
}

static void qdict_add_key(const char *key, QObject *obj, void *opaque)
{
    GHashTable *h = opaque;
    g_hash_table_insert(h, (gpointer) key, NULL);
}

static const QListEntry *qobject_input_push(QObjectInputVisitor *qiv,
                                            QObject *obj, void *qapi,
                                            Error **errp)
{
    GHashTable *h;
    StackObject *tos = g_new0(StackObject, 1);

    assert(obj);
    tos->obj = obj;
    tos->qapi = qapi;

    if (qiv->strict && qobject_type(obj) == QTYPE_QDICT) {
        h = g_hash_table_new(g_str_hash, g_str_equal);
        qdict_iter(qobject_to_qdict(obj), qdict_add_key, h);
        tos->h = h;
    } else if (qobject_type(obj) == QTYPE_QLIST) {
        tos->entry = qlist_first(qobject_to_qlist(obj));
    }

    QSLIST_INSERT_HEAD(&qiv->stack, tos, node);
    return tos->entry;
}


static void qobject_input_check_struct(Visitor *v, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    StackObject *tos = QSLIST_FIRST(&qiv->stack);

    assert(tos && !tos->entry);
    if (qiv->strict) {
        GHashTable *const top_ht = tos->h;
        if (top_ht) {
            GHashTableIter iter;
            const char *key;

            g_hash_table_iter_init(&iter, top_ht);
            if (g_hash_table_iter_next(&iter, (void **)&key, NULL)) {
                error_setg(errp, QERR_QMP_EXTRA_MEMBER, key);
            }
        }
    }
}

static void qobject_input_stack_object_free(StackObject *tos)
{
    if (tos->h) {
        g_hash_table_unref(tos->h);
    }

    g_free(tos);
}

static void qobject_input_pop(Visitor *v, void **obj)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    StackObject *tos = QSLIST_FIRST(&qiv->stack);

    assert(tos && tos->qapi == obj);
    QSLIST_REMOVE_HEAD(&qiv->stack, node);
    qobject_input_stack_object_free(tos);
}

static void qobject_input_start_struct(Visitor *v, const char *name, void **obj,
                                       size_t size, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    Error *err = NULL;

    if (obj) {
        *obj = NULL;
    }
    if (!qobj) {
        return;
    }
    if (qobject_type(qobj) != QTYPE_QDICT) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "QDict");
        return;
    }

    qobject_input_push(qiv, qobj, obj, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (obj) {
        *obj = g_malloc0(size);
    }
}


static void qobject_input_start_list(Visitor *v, const char *name,
                                     GenericList **list, size_t size,
                                     Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    const QListEntry *entry;

    if (!qobj) {
        return;
    }
    if (qobject_type(qobj) != QTYPE_QLIST) {
        if (list) {
            *list = NULL;
        }
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "list");
        return;
    }

    entry = qobject_input_push(qiv, qobj, list, errp);
    if (list) {
        if (entry) {
            *list = g_malloc0(size);
        } else {
            *list = NULL;
        }
    }
}

static GenericList *qobject_input_next_list(Visitor *v, GenericList *tail,
                                            size_t size)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    StackObject *so = QSLIST_FIRST(&qiv->stack);

    if (!so->entry) {
        return NULL;
    }
    tail->next = g_malloc0(size);
    return tail->next;
}


static void qobject_input_start_alternate(Visitor *v, const char *name,
                                          GenericAlternate **obj, size_t size,
                                          bool promote_int, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, false, errp);

    if (!qobj) {
        *obj = NULL;
        return;
    }
    *obj = g_malloc0(size);
    (*obj)->type = qobject_type(qobj);
    if (promote_int && (*obj)->type == QTYPE_QINT) {
        (*obj)->type = QTYPE_QFLOAT;
    }
}

static void qobject_input_type_int64(Visitor *v, const char *name, int64_t *obj,
                                     Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    QInt *qint;

    if (!qobj) {
        return;
    }
    qint = qobject_to_qint(qobj);
    if (!qint) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "integer");
        return;
    }

    *obj = qint_get_int(qint);
}

static void qobject_input_type_uint64(Visitor *v, const char *name,
                                      uint64_t *obj, Error **errp)
{
    /* FIXME: qobject_to_qint mishandles values over INT64_MAX */
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    QInt *qint;

    if (!qobj) {
        return;
    }
    qint = qobject_to_qint(qobj);
    if (!qint) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "integer");
        return;
    }

    *obj = qint_get_int(qint);
}

static void qobject_input_type_bool(Visitor *v, const char *name, bool *obj,
                                    Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    QBool *qbool;

    if (!qobj) {
        return;
    }
    qbool = qobject_to_qbool(qobj);
    if (!qbool) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "boolean");
        return;
    }

    *obj = qbool_get_bool(qbool);
}

static void qobject_input_type_str(Visitor *v, const char *name, char **obj,
                                   Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    QString *qstr;

    *obj = NULL;
    if (!qobj) {
        return;
    }
    qstr = qobject_to_qstring(qobj);
    if (!qstr) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "string");
        return;
    }

    *obj = g_strdup(qstring_get_str(qstr));
}

static void qobject_input_type_number(Visitor *v, const char *name, double *obj,
                                      Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    QInt *qint;
    QFloat *qfloat;

    if (!qobj) {
        return;
    }
    qint = qobject_to_qint(qobj);
    if (qint) {
        *obj = qint_get_int(qobject_to_qint(qobj));
        return;
    }

    qfloat = qobject_to_qfloat(qobj);
    if (qfloat) {
        *obj = qfloat_get_double(qobject_to_qfloat(qobj));
        return;
    }

    error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
               "number");
}

static void qobject_input_type_any(Visitor *v, const char *name, QObject **obj,
                                   Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);

    *obj = NULL;
    if (!qobj) {
        return;
    }

    qobject_incref(qobj);
    *obj = qobj;
}

static void qobject_input_type_null(Visitor *v, const char *name, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);

    if (!qobj) {
        return;
    }

    if (qobject_type(qobj) != QTYPE_QNULL) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "null");
    }
}

static void qobject_input_optional(Visitor *v, const char *name, bool *present)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, false, NULL);

    if (!qobj) {
        *present = false;
        return;
    }

    *present = true;
}

static void qobject_input_free(Visitor *v)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    while (!QSLIST_EMPTY(&qiv->stack)) {
        StackObject *tos = QSLIST_FIRST(&qiv->stack);

        QSLIST_REMOVE_HEAD(&qiv->stack, node);
        qobject_input_stack_object_free(tos);
    }

    qobject_decref(qiv->root);
    g_free(qiv);
}

Visitor *qobject_input_visitor_new(QObject *obj, bool strict)
{
    QObjectInputVisitor *v;

    assert(obj);
    v = g_malloc0(sizeof(*v));

    v->visitor.type = VISITOR_INPUT;
    v->visitor.start_struct = qobject_input_start_struct;
    v->visitor.check_struct = qobject_input_check_struct;
    v->visitor.end_struct = qobject_input_pop;
    v->visitor.start_list = qobject_input_start_list;
    v->visitor.next_list = qobject_input_next_list;
    v->visitor.end_list = qobject_input_pop;
    v->visitor.start_alternate = qobject_input_start_alternate;
    v->visitor.type_int64 = qobject_input_type_int64;
    v->visitor.type_uint64 = qobject_input_type_uint64;
    v->visitor.type_bool = qobject_input_type_bool;
    v->visitor.type_str = qobject_input_type_str;
    v->visitor.type_number = qobject_input_type_number;
    v->visitor.type_any = qobject_input_type_any;
    v->visitor.type_null = qobject_input_type_null;
    v->visitor.optional = qobject_input_optional;
    v->visitor.free = qobject_input_free;
    v->strict = strict;

    v->root = obj;
    qobject_incref(obj);

    return &v->visitor;
}
