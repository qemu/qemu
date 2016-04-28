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
#include "qapi/qmp-input-visitor.h"
#include "qapi/visitor-impl.h"
#include "qemu/queue.h"
#include "qemu-common.h"
#include "qapi/qmp/types.h"
#include "qapi/qmp/qerror.h"

#define QIV_STACK_SIZE 1024

typedef struct StackObject
{
    QObject *obj; /* Object being visited */

    GHashTable *h;           /* If obj is dict: unvisited keys */
    const QListEntry *entry; /* If obj is list: unvisited tail */
    bool first;              /* If obj is list: next_list() not yet called? */
} StackObject;

struct QmpInputVisitor
{
    Visitor visitor;

    /* Root of visit at visitor creation. */
    QObject *root;

    /* Stack of objects being visited (all entries will be either
     * QDict or QList). */
    StackObject stack[QIV_STACK_SIZE];
    int nb_stack;

    /* True to reject parse in visit_end_struct() if unvisited keys remain. */
    bool strict;
};

static QmpInputVisitor *to_qiv(Visitor *v)
{
    return container_of(v, QmpInputVisitor, visitor);
}

static QObject *qmp_input_get_object(QmpInputVisitor *qiv,
                                     const char *name,
                                     bool consume)
{
    StackObject *tos;
    QObject *qobj;
    QObject *ret;

    if (!qiv->nb_stack) {
        /* Starting at root, name is ignored. */
        return qiv->root;
    }

    /* We are in a container; find the next element. */
    tos = &qiv->stack[qiv->nb_stack - 1];
    qobj = tos->obj;
    assert(qobj);

    if (qobject_type(qobj) == QTYPE_QDICT) {
        assert(name);
        ret = qdict_get(qobject_to_qdict(qobj), name);
        if (tos->h && consume && ret) {
            bool removed = g_hash_table_remove(tos->h, name);
            assert(removed);
        }
    } else {
        assert(qobject_type(qobj) == QTYPE_QLIST);
        assert(!name);
        assert(!tos->first);
        ret = qlist_entry_obj(tos->entry);
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

static void qmp_input_push(QmpInputVisitor *qiv, QObject *obj, Error **errp)
{
    GHashTable *h;
    StackObject *tos = &qiv->stack[qiv->nb_stack];

    assert(obj);
    if (qiv->nb_stack >= QIV_STACK_SIZE) {
        error_setg(errp, "An internal buffer overran");
        return;
    }

    tos->obj = obj;
    assert(!tos->h);
    assert(!tos->entry);

    if (qiv->strict && qobject_type(obj) == QTYPE_QDICT) {
        h = g_hash_table_new(g_str_hash, g_str_equal);
        qdict_iter(qobject_to_qdict(obj), qdict_add_key, h);
        tos->h = h;
    } else if (qobject_type(obj) == QTYPE_QLIST) {
        tos->entry = qlist_first(qobject_to_qlist(obj));
        tos->first = true;
    }

    qiv->nb_stack++;
}


static void qmp_input_pop(QmpInputVisitor *qiv, Error **errp)
{
    StackObject *tos = &qiv->stack[qiv->nb_stack - 1];
    assert(qiv->nb_stack > 0);

    if (qiv->strict) {
        GHashTable *const top_ht = tos->h;
        if (top_ht) {
            GHashTableIter iter;
            const char *key;

            g_hash_table_iter_init(&iter, top_ht);
            if (g_hash_table_iter_next(&iter, (void **)&key, NULL)) {
                error_setg(errp, QERR_QMP_EXTRA_MEMBER, key);
            }
            g_hash_table_unref(top_ht);
        }
        tos->h = NULL;
    }

    qiv->nb_stack--;
}

static void qmp_input_start_struct(Visitor *v, const char *name, void **obj,
                                   size_t size, Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qmp_input_get_object(qiv, name, true);
    Error *err = NULL;

    if (obj) {
        *obj = NULL;
    }
    if (!qobj || qobject_type(qobj) != QTYPE_QDICT) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "QDict");
        return;
    }

    qmp_input_push(qiv, qobj, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    if (obj) {
        *obj = g_malloc0(size);
    }
}

static void qmp_input_end_struct(Visitor *v, Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);

    qmp_input_pop(qiv, errp);
}

static void qmp_input_start_list(Visitor *v, const char *name, Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qmp_input_get_object(qiv, name, true);

    if (!qobj || qobject_type(qobj) != QTYPE_QLIST) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "list");
        return;
    }

    qmp_input_push(qiv, qobj, errp);
}

static GenericList *qmp_input_next_list(Visitor *v, GenericList **list,
                                        size_t size)
{
    QmpInputVisitor *qiv = to_qiv(v);
    GenericList *entry;
    StackObject *so = &qiv->stack[qiv->nb_stack - 1];

    if (!so->entry) {
        return NULL;
    }

    entry = g_malloc0(size);
    if (so->first) {
        *list = entry;
        so->first = false;
    } else {
        (*list)->next = entry;
    }

    return entry;
}

static void qmp_input_end_list(Visitor *v)
{
    QmpInputVisitor *qiv = to_qiv(v);

    qmp_input_pop(qiv, &error_abort);
}

static void qmp_input_start_alternate(Visitor *v, const char *name,
                                      GenericAlternate **obj, size_t size,
                                      bool promote_int, Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qmp_input_get_object(qiv, name, false);

    if (!qobj) {
        *obj = NULL;
        error_setg(errp, QERR_MISSING_PARAMETER, name ? name : "null");
        return;
    }
    *obj = g_malloc0(size);
    (*obj)->type = qobject_type(qobj);
    if (promote_int && (*obj)->type == QTYPE_QINT) {
        (*obj)->type = QTYPE_QFLOAT;
    }
}

static void qmp_input_type_int64(Visitor *v, const char *name, int64_t *obj,
                                 Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);
    QInt *qint = qobject_to_qint(qmp_input_get_object(qiv, name, true));

    if (!qint) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "integer");
        return;
    }

    *obj = qint_get_int(qint);
}

static void qmp_input_type_uint64(Visitor *v, const char *name, uint64_t *obj,
                                  Error **errp)
{
    /* FIXME: qobject_to_qint mishandles values over INT64_MAX */
    QmpInputVisitor *qiv = to_qiv(v);
    QInt *qint = qobject_to_qint(qmp_input_get_object(qiv, name, true));

    if (!qint) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "integer");
        return;
    }

    *obj = qint_get_int(qint);
}

static void qmp_input_type_bool(Visitor *v, const char *name, bool *obj,
                                Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);
    QBool *qbool = qobject_to_qbool(qmp_input_get_object(qiv, name, true));

    if (!qbool) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "boolean");
        return;
    }

    *obj = qbool_get_bool(qbool);
}

static void qmp_input_type_str(Visitor *v, const char *name, char **obj,
                               Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);
    QString *qstr = qobject_to_qstring(qmp_input_get_object(qiv, name, true));

    if (!qstr) {
        *obj = NULL;
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "string");
        return;
    }

    *obj = g_strdup(qstring_get_str(qstr));
}

static void qmp_input_type_number(Visitor *v, const char *name, double *obj,
                                  Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qmp_input_get_object(qiv, name, true);
    QInt *qint;
    QFloat *qfloat;

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

static void qmp_input_type_any(Visitor *v, const char *name, QObject **obj,
                               Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qmp_input_get_object(qiv, name, true);

    qobject_incref(qobj);
    *obj = qobj;
}

static void qmp_input_type_null(Visitor *v, const char *name, Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qmp_input_get_object(qiv, name, true);

    if (qobject_type(qobj) != QTYPE_QNULL) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                   "null");
    }
}

static void qmp_input_optional(Visitor *v, const char *name, bool *present)
{
    QmpInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qmp_input_get_object(qiv, name, false);

    if (!qobj) {
        *present = false;
        return;
    }

    *present = true;
}

Visitor *qmp_input_get_visitor(QmpInputVisitor *v)
{
    return &v->visitor;
}

void qmp_input_visitor_cleanup(QmpInputVisitor *v)
{
    qobject_decref(v->root);
    g_free(v);
}

QmpInputVisitor *qmp_input_visitor_new(QObject *obj, bool strict)
{
    QmpInputVisitor *v;

    v = g_malloc0(sizeof(*v));

    v->visitor.type = VISITOR_INPUT;
    v->visitor.start_struct = qmp_input_start_struct;
    v->visitor.end_struct = qmp_input_end_struct;
    v->visitor.start_list = qmp_input_start_list;
    v->visitor.next_list = qmp_input_next_list;
    v->visitor.end_list = qmp_input_end_list;
    v->visitor.start_alternate = qmp_input_start_alternate;
    v->visitor.type_int64 = qmp_input_type_int64;
    v->visitor.type_uint64 = qmp_input_type_uint64;
    v->visitor.type_bool = qmp_input_type_bool;
    v->visitor.type_str = qmp_input_type_str;
    v->visitor.type_number = qmp_input_type_number;
    v->visitor.type_any = qmp_input_type_any;
    v->visitor.type_null = qmp_input_type_null;
    v->visitor.optional = qmp_input_optional;
    v->strict = strict;

    v->root = obj;
    qobject_incref(obj);

    return v;
}
