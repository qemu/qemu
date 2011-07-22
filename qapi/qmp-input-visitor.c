/*
 * Input Visitor
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

#include "qmp-input-visitor.h"
#include "qemu-queue.h"
#include "qemu-common.h"
#include "qemu-objects.h"
#include "qerror.h"

#define QIV_STACK_SIZE 1024

typedef struct StackObject
{
    const QObject *obj;
    const  QListEntry *entry;
} StackObject;

struct QmpInputVisitor
{
    Visitor visitor;
    QObject *obj;
    StackObject stack[QIV_STACK_SIZE];
    int nb_stack;
};

static QmpInputVisitor *to_qiv(Visitor *v)
{
    return container_of(v, QmpInputVisitor, visitor);
}

static const QObject *qmp_input_get_object(QmpInputVisitor *qiv,
                                           const char *name)
{
    const QObject *qobj;

    if (qiv->nb_stack == 0) {
        qobj = qiv->obj;
    } else {
        qobj = qiv->stack[qiv->nb_stack - 1].obj;
    }

    if (name && qobject_type(qobj) == QTYPE_QDICT) {
        return qdict_get(qobject_to_qdict(qobj), name);
    } else if (qiv->nb_stack > 0 && qobject_type(qobj) == QTYPE_QLIST) {
        return qlist_entry_obj(qiv->stack[qiv->nb_stack - 1].entry);
    }

    return qobj;
}

static void qmp_input_push(QmpInputVisitor *qiv, const QObject *obj, Error **errp)
{
    qiv->stack[qiv->nb_stack].obj = obj;
    if (qobject_type(obj) == QTYPE_QLIST) {
        qiv->stack[qiv->nb_stack].entry = qlist_first(qobject_to_qlist(obj));
    }
    qiv->nb_stack++;

    if (qiv->nb_stack >= QIV_STACK_SIZE) {
        error_set(errp, QERR_BUFFER_OVERRUN);
        return;
    }
}

static void qmp_input_pop(QmpInputVisitor *qiv, Error **errp)
{
    qiv->nb_stack--;
    if (qiv->nb_stack < 0) {
        error_set(errp, QERR_BUFFER_OVERRUN);
        return;
    }
}

static void qmp_input_start_struct(Visitor *v, void **obj, const char *kind,
                                   const char *name, size_t size, Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);
    const QObject *qobj = qmp_input_get_object(qiv, name);

    if (!qobj || qobject_type(qobj) != QTYPE_QDICT) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                  "QDict");
        return;
    }

    qmp_input_push(qiv, qobj, errp);
    if (error_is_set(errp)) {
        return;
    }

    if (obj) {
        *obj = qemu_mallocz(size);
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
    const QObject *qobj = qmp_input_get_object(qiv, name);

    if (!qobj || qobject_type(qobj) != QTYPE_QLIST) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                  "list");
        return;
    }

    qmp_input_push(qiv, qobj, errp);
}

static GenericList *qmp_input_next_list(Visitor *v, GenericList **list,
                                        Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);
    GenericList *entry;
    StackObject *so = &qiv->stack[qiv->nb_stack - 1];

    if (so->entry == NULL) {
        return NULL;
    }

    entry = qemu_mallocz(sizeof(*entry));
    if (*list) {
        so->entry = qlist_next(so->entry);
        if (so->entry == NULL) {
            qemu_free(entry);
            return NULL;
        }
        (*list)->next = entry;
    }
    *list = entry;


    return entry;
}

static void qmp_input_end_list(Visitor *v, Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);

    qmp_input_pop(qiv, errp);
}

static void qmp_input_type_int(Visitor *v, int64_t *obj, const char *name,
                               Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);
    const QObject *qobj = qmp_input_get_object(qiv, name);

    if (!qobj || qobject_type(qobj) != QTYPE_QINT) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                  "integer");
        return;
    }

    *obj = qint_get_int(qobject_to_qint(qobj));
}

static void qmp_input_type_bool(Visitor *v, bool *obj, const char *name,
                                Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);
    const QObject *qobj = qmp_input_get_object(qiv, name);

    if (!qobj || qobject_type(qobj) != QTYPE_QBOOL) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                  "boolean");
        return;
    }

    *obj = qbool_get_int(qobject_to_qbool(qobj));
}

static void qmp_input_type_str(Visitor *v, char **obj, const char *name,
                               Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);
    const QObject *qobj = qmp_input_get_object(qiv, name);

    if (!qobj || qobject_type(qobj) != QTYPE_QSTRING) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                  "string");
        return;
    }

    *obj = qemu_strdup(qstring_get_str(qobject_to_qstring(qobj)));
}

static void qmp_input_type_number(Visitor *v, double *obj, const char *name,
                                  Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);
    const QObject *qobj = qmp_input_get_object(qiv, name);

    if (!qobj || qobject_type(qobj) != QTYPE_QFLOAT) {
        error_set(errp, QERR_INVALID_PARAMETER_TYPE, name ? name : "null",
                  "double");
        return;
    }

    *obj = qfloat_get_double(qobject_to_qfloat(qobj));
}

static void qmp_input_type_enum(Visitor *v, int *obj, const char *strings[],
                                const char *kind, const char *name,
                                Error **errp)
{
    int64_t value = 0;
    char *enum_str;

    assert(strings);

    qmp_input_type_str(v, &enum_str, name, errp);
    if (error_is_set(errp)) {
        return;
    }

    while (strings[value] != NULL) {
        if (strcmp(strings[value], enum_str) == 0) {
            break;
        }
        value++;
    }

    if (strings[value] == NULL) {
        error_set(errp, QERR_INVALID_PARAMETER, name ? name : "null");
        return;
    }

    *obj = value;
}

static void qmp_input_start_optional(Visitor *v, bool *present,
                                     const char *name, Error **errp)
{
    QmpInputVisitor *qiv = to_qiv(v);
    const QObject *qobj = qmp_input_get_object(qiv, name);

    if (!qobj) {
        *present = false;
        return;
    }

    *present = true;
}

static void qmp_input_end_optional(Visitor *v, Error **errp)
{
}

Visitor *qmp_input_get_visitor(QmpInputVisitor *v)
{
    return &v->visitor;
}

void qmp_input_visitor_cleanup(QmpInputVisitor *v)
{
    qobject_decref(v->obj);
    qemu_free(v);
}

QmpInputVisitor *qmp_input_visitor_new(QObject *obj)
{
    QmpInputVisitor *v;

    v = qemu_mallocz(sizeof(*v));

    v->visitor.start_struct = qmp_input_start_struct;
    v->visitor.end_struct = qmp_input_end_struct;
    v->visitor.start_list = qmp_input_start_list;
    v->visitor.next_list = qmp_input_next_list;
    v->visitor.end_list = qmp_input_end_list;
    v->visitor.type_enum = qmp_input_type_enum;
    v->visitor.type_int = qmp_input_type_int;
    v->visitor.type_bool = qmp_input_type_bool;
    v->visitor.type_str = qmp_input_type_str;
    v->visitor.type_number = qmp_input_type_number;
    v->visitor.start_optional = qmp_input_start_optional;
    v->visitor.end_optional = qmp_input_end_optional;

    v->obj = obj;
    qobject_incref(v->obj);

    return v;
}
