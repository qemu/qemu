/*
 * Input Visitor
 *
 * Copyright (C) 2012-2017 Red Hat, Inc.
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
#include <math.h>
#include "qapi/error.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/visitor-impl.h"
#include "qemu/queue.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qnull.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qstring.h"
#include "qemu/cutils.h"
#include "qemu/option.h"

typedef struct StackObject {
    const char *name;            /* Name of @obj in its parent, if any */
    QObject *obj;                /* QDict or QList being visited */
    void *qapi; /* sanity check that caller uses same pointer */

    GHashTable *h;              /* If @obj is QDict: unvisited keys */
    const QListEntry *entry;    /* If @obj is QList: unvisited tail */
    unsigned index;             /* If @obj is QList: list index of @entry */

    QSLIST_ENTRY(StackObject) node; /* parent */
} StackObject;

struct QObjectInputVisitor {
    Visitor visitor;

    /* Root of visit at visitor creation. */
    QObject *root;
    bool keyval;                /* Assume @root made with keyval_parse() */

    /* Stack of objects being visited (all entries will be either
     * QDict or QList). */
    QSLIST_HEAD(, StackObject) stack;

    GString *errname;           /* Accumulator for full_name() */
};

static QObjectInputVisitor *to_qiv(Visitor *v)
{
    return container_of(v, QObjectInputVisitor, visitor);
}

/*
 * Find the full name of something @qiv is currently visiting.
 * @qiv is visiting something named @name in the stack of containers
 * @qiv->stack.
 * If @n is zero, return its full name.
 * If @n is positive, return the full name of the @n-th container
 * counting from the top.  The stack of containers must have at least
 * @n elements.
 * The returned string is valid until the next full_name_nth(@v) or
 * destruction of @v.
 */
static const char *full_name_nth(QObjectInputVisitor *qiv, const char *name,
                                 int n)
{
    StackObject *so;
    char buf[32];

    if (qiv->errname) {
        g_string_truncate(qiv->errname, 0);
    } else {
        qiv->errname = g_string_new("");
    }

    QSLIST_FOREACH(so , &qiv->stack, node) {
        if (n) {
            n--;
        } else if (qobject_type(so->obj) == QTYPE_QDICT) {
            g_string_prepend(qiv->errname, name ?: "<anonymous>");
            g_string_prepend_c(qiv->errname, '.');
        } else {
            snprintf(buf, sizeof(buf),
                     qiv->keyval ? ".%u" : "[%u]",
                     so->index);
            g_string_prepend(qiv->errname, buf);
        }
        name = so->name;
    }
    assert(!n);

    if (name) {
        g_string_prepend(qiv->errname, name);
    } else if (qiv->errname->str[0] == '.') {
        g_string_erase(qiv->errname, 0, 1);
    } else if (!qiv->errname->str[0]) {
        return "<anonymous>";
    }

    return qiv->errname->str;
}

static const char *full_name(QObjectInputVisitor *qiv, const char *name)
{
    return full_name_nth(qiv, name, 0);
}

static QObject *qobject_input_try_get_object(QObjectInputVisitor *qiv,
                                             const char *name,
                                             bool consume)
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
        ret = qdict_get(qobject_to(QDict, qobj), name);
        if (tos->h && consume && ret) {
            bool removed = g_hash_table_remove(tos->h, name);
            assert(removed);
        }
    } else {
        assert(qobject_type(qobj) == QTYPE_QLIST);
        assert(!name);
        if (tos->entry) {
            ret = qlist_entry_obj(tos->entry);
            if (consume) {
                tos->entry = qlist_next(tos->entry);
            }
        } else {
            ret = NULL;
        }
        if (consume) {
            tos->index++;
        }
    }

    return ret;
}

static QObject *qobject_input_get_object(QObjectInputVisitor *qiv,
                                         const char *name,
                                         bool consume, Error **errp)
{
    QObject *obj = qobject_input_try_get_object(qiv, name, consume);

    if (!obj) {
        error_setg(errp, QERR_MISSING_PARAMETER, full_name(qiv, name));
    }
    return obj;
}

static const char *qobject_input_get_keyval(QObjectInputVisitor *qiv,
                                            const char *name,
                                            Error **errp)
{
    QObject *qobj;
    QString *qstr;

    qobj = qobject_input_get_object(qiv, name, true, errp);
    if (!qobj) {
        return NULL;
    }

    qstr = qobject_to(QString, qobj);
    if (!qstr) {
        switch (qobject_type(qobj)) {
        case QTYPE_QDICT:
        case QTYPE_QLIST:
            error_setg(errp, "Parameters '%s.*' are unexpected",
                       full_name(qiv, name));
            return NULL;
        default:
            /* Non-string scalar (should this be an assertion?) */
            error_setg(errp, "Internal error: parameter %s invalid",
                       full_name(qiv, name));
            return NULL;
        }
    }

    return qstring_get_str(qstr);
}

static const QListEntry *qobject_input_push(QObjectInputVisitor *qiv,
                                            const char *name,
                                            QObject *obj, void *qapi)
{
    GHashTable *h;
    StackObject *tos = g_new0(StackObject, 1);
    QDict *qdict = qobject_to(QDict, obj);
    QList *qlist = qobject_to(QList, obj);
    const QDictEntry *entry;

    assert(obj);
    tos->name = name;
    tos->obj = obj;
    tos->qapi = qapi;

    if (qdict) {
        h = g_hash_table_new(g_str_hash, g_str_equal);
        for (entry = qdict_first(qdict);
             entry;
             entry = qdict_next(qdict, entry)) {
            g_hash_table_insert(h, (void *)qdict_entry_key(entry), NULL);
        }
        tos->h = h;
    } else {
        assert(qlist);
        tos->entry = qlist_first(qlist);
        tos->index = -1;
    }

    QSLIST_INSERT_HEAD(&qiv->stack, tos, node);
    return tos->entry;
}


static bool qobject_input_check_struct(Visitor *v, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    StackObject *tos = QSLIST_FIRST(&qiv->stack);
    GHashTableIter iter;
    const char *key;

    assert(tos && !tos->entry);

    g_hash_table_iter_init(&iter, tos->h);
    if (g_hash_table_iter_next(&iter, (void **)&key, NULL)) {
        error_setg(errp, "Parameter '%s' is unexpected",
                   full_name(qiv, key));
        return false;
    }
    return true;
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

static bool qobject_input_start_struct(Visitor *v, const char *name, void **obj,
                                       size_t size, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);

    if (obj) {
        *obj = NULL;
    }
    if (!qobj) {
        return false;
    }
    if (qobject_type(qobj) != QTYPE_QDICT) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                   full_name(qiv, name), "object");
        return false;
    }

    qobject_input_push(qiv, name, qobj, obj);

    if (obj) {
        *obj = g_malloc0(size);
    }
    return true;
}

static void qobject_input_end_struct(Visitor *v, void **obj)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    StackObject *tos = QSLIST_FIRST(&qiv->stack);

    assert(qobject_type(tos->obj) == QTYPE_QDICT && tos->h);
    qobject_input_pop(v, obj);
}


static bool qobject_input_start_list(Visitor *v, const char *name,
                                     GenericList **list, size_t size,
                                     Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    const QListEntry *entry;

    if (list) {
        *list = NULL;
    }
    if (!qobj) {
        return false;
    }
    if (qobject_type(qobj) != QTYPE_QLIST) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                   full_name(qiv, name), "array");
        return false;
    }

    entry = qobject_input_push(qiv, name, qobj, list);
    if (entry && list) {
        *list = g_malloc0(size);
    }
    return true;
}

static GenericList *qobject_input_next_list(Visitor *v, GenericList *tail,
                                            size_t size)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    StackObject *tos = QSLIST_FIRST(&qiv->stack);

    assert(tos && qobject_to(QList, tos->obj));

    if (!tos->entry) {
        return NULL;
    }
    tail->next = g_malloc0(size);
    return tail->next;
}

static bool qobject_input_check_list(Visitor *v, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    StackObject *tos = QSLIST_FIRST(&qiv->stack);

    assert(tos && qobject_to(QList, tos->obj));

    if (tos->entry) {
        error_setg(errp, "Only %u list elements expected in %s",
                   tos->index + 1, full_name_nth(qiv, NULL, 1));
        return false;
    }
    return true;
}

static void qobject_input_end_list(Visitor *v, void **obj)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    StackObject *tos = QSLIST_FIRST(&qiv->stack);

    assert(qobject_type(tos->obj) == QTYPE_QLIST && !tos->h);
    qobject_input_pop(v, obj);
}

static bool qobject_input_start_alternate(Visitor *v, const char *name,
                                          GenericAlternate **obj, size_t size,
                                          Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, false, errp);

    if (!qobj) {
        *obj = NULL;
        return false;
    }
    *obj = g_malloc0(size);
    (*obj)->type = qobject_type(qobj);
    return true;
}

static bool qobject_input_type_int64(Visitor *v, const char *name, int64_t *obj,
                                     Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    QNum *qnum;

    if (!qobj) {
        return false;
    }
    qnum = qobject_to(QNum, qobj);
    if (!qnum || !qnum_get_try_int(qnum, obj)) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                   full_name(qiv, name), "integer");
        return false;
    }
    return true;
}

static bool qobject_input_type_int64_keyval(Visitor *v, const char *name,
                                            int64_t *obj, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    const char *str = qobject_input_get_keyval(qiv, name, errp);

    if (!str) {
        return false;
    }

    if (qemu_strtoi64(str, NULL, 0, obj) < 0) {
        /* TODO report -ERANGE more nicely */
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   full_name(qiv, name), "integer");
        return false;
    }
    return true;
}

static bool qobject_input_type_uint64(Visitor *v, const char *name,
                                      uint64_t *obj, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    QNum *qnum;
    int64_t val;

    if (!qobj) {
        return false;
    }
    qnum = qobject_to(QNum, qobj);
    if (!qnum) {
        goto err;
    }

    if (qnum_get_try_uint(qnum, obj)) {
        return true;
    }

    /* Need to accept negative values for backward compatibility */
    if (qnum_get_try_int(qnum, &val)) {
        *obj = val;
        return true;
    }

err:
    error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
               full_name(qiv, name), "uint64");
    return false;
}

static bool qobject_input_type_uint64_keyval(Visitor *v, const char *name,
                                             uint64_t *obj, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    const char *str = qobject_input_get_keyval(qiv, name, errp);

    if (!str) {
        return false;
    }

    if (qemu_strtou64(str, NULL, 0, obj) < 0) {
        /* TODO report -ERANGE more nicely */
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   full_name(qiv, name), "integer");
        return false;
    }
    return true;
}

static bool qobject_input_type_bool(Visitor *v, const char *name, bool *obj,
                                    Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    QBool *qbool;

    if (!qobj) {
        return false;
    }
    qbool = qobject_to(QBool, qobj);
    if (!qbool) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                   full_name(qiv, name), "boolean");
        return false;
    }

    *obj = qbool_get_bool(qbool);
    return true;
}

static bool qobject_input_type_bool_keyval(Visitor *v, const char *name,
                                           bool *obj, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    const char *str = qobject_input_get_keyval(qiv, name, errp);

    if (!str) {
        return false;
    }

    if (!strcmp(str, "on")) {
        *obj = true;
    } else if (!strcmp(str, "off")) {
        *obj = false;
    } else {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   full_name(qiv, name), "'on' or 'off'");
        return false;
    }
    return true;
}

static bool qobject_input_type_str(Visitor *v, const char *name, char **obj,
                                   Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    QString *qstr;

    *obj = NULL;
    if (!qobj) {
        return false;
    }
    qstr = qobject_to(QString, qobj);
    if (!qstr) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                   full_name(qiv, name), "string");
        return false;
    }

    *obj = g_strdup(qstring_get_str(qstr));
    return true;
}

static bool qobject_input_type_str_keyval(Visitor *v, const char *name,
                                          char **obj, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    const char *str = qobject_input_get_keyval(qiv, name, errp);

    *obj = g_strdup(str);
    return !!str;
}

static bool qobject_input_type_number(Visitor *v, const char *name, double *obj,
                                      Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);
    QNum *qnum;

    if (!qobj) {
        return false;
    }
    qnum = qobject_to(QNum, qobj);
    if (!qnum) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                   full_name(qiv, name), "number");
        return false;
    }

    *obj = qnum_get_double(qnum);
    return true;
}

static bool qobject_input_type_number_keyval(Visitor *v, const char *name,
                                             double *obj, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    const char *str = qobject_input_get_keyval(qiv, name, errp);
    double val;

    if (!str) {
        return false;
    }

    if (qemu_strtod_finite(str, NULL, &val)) {
        /* TODO report -ERANGE more nicely */
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                   full_name(qiv, name), "number");
        return false;
    }

    *obj = val;
    return true;
}

static bool qobject_input_type_any(Visitor *v, const char *name, QObject **obj,
                                   Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);

    *obj = NULL;
    if (!qobj) {
        return false;
    }

    *obj = qobject_ref(qobj);
    return true;
}

static bool qobject_input_type_null(Visitor *v, const char *name,
                                    QNull **obj, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_get_object(qiv, name, true, errp);

    *obj = NULL;
    if (!qobj) {
        return false;
    }

    if (qobject_type(qobj) != QTYPE_QNULL) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE,
                   full_name(qiv, name), "null");
        return false;
    }
    *obj = qnull();
    return true;
}

static bool qobject_input_type_size_keyval(Visitor *v, const char *name,
                                           uint64_t *obj, Error **errp)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    const char *str = qobject_input_get_keyval(qiv, name, errp);

    if (!str) {
        return false;
    }

    if (qemu_strtosz(str, NULL, obj) < 0) {
        /* TODO report -ERANGE more nicely */
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   full_name(qiv, name), "size");
        return false;
    }
    return true;
}

static void qobject_input_optional(Visitor *v, const char *name, bool *present)
{
    QObjectInputVisitor *qiv = to_qiv(v);
    QObject *qobj = qobject_input_try_get_object(qiv, name, false);

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

    qobject_unref(qiv->root);
    if (qiv->errname) {
        g_string_free(qiv->errname, TRUE);
    }
    g_free(qiv);
}

static QObjectInputVisitor *qobject_input_visitor_base_new(QObject *obj)
{
    QObjectInputVisitor *v = g_malloc0(sizeof(*v));

    assert(obj);

    v->visitor.type = VISITOR_INPUT;
    v->visitor.start_struct = qobject_input_start_struct;
    v->visitor.check_struct = qobject_input_check_struct;
    v->visitor.end_struct = qobject_input_end_struct;
    v->visitor.start_list = qobject_input_start_list;
    v->visitor.next_list = qobject_input_next_list;
    v->visitor.check_list = qobject_input_check_list;
    v->visitor.end_list = qobject_input_end_list;
    v->visitor.start_alternate = qobject_input_start_alternate;
    v->visitor.optional = qobject_input_optional;
    v->visitor.free = qobject_input_free;

    v->root = qobject_ref(obj);

    return v;
}

Visitor *qobject_input_visitor_new(QObject *obj)
{
    QObjectInputVisitor *v = qobject_input_visitor_base_new(obj);

    v->visitor.type_int64 = qobject_input_type_int64;
    v->visitor.type_uint64 = qobject_input_type_uint64;
    v->visitor.type_bool = qobject_input_type_bool;
    v->visitor.type_str = qobject_input_type_str;
    v->visitor.type_number = qobject_input_type_number;
    v->visitor.type_any = qobject_input_type_any;
    v->visitor.type_null = qobject_input_type_null;

    return &v->visitor;
}

Visitor *qobject_input_visitor_new_keyval(QObject *obj)
{
    QObjectInputVisitor *v = qobject_input_visitor_base_new(obj);

    v->visitor.type_int64 = qobject_input_type_int64_keyval;
    v->visitor.type_uint64 = qobject_input_type_uint64_keyval;
    v->visitor.type_bool = qobject_input_type_bool_keyval;
    v->visitor.type_str = qobject_input_type_str_keyval;
    v->visitor.type_number = qobject_input_type_number_keyval;
    v->visitor.type_any = qobject_input_type_any;
    v->visitor.type_null = qobject_input_type_null;
    v->visitor.type_size = qobject_input_type_size_keyval;
    v->keyval = true;

    return &v->visitor;
}

Visitor *qobject_input_visitor_new_str(const char *str,
                                       const char *implied_key,
                                       Error **errp)
{
    bool is_json = str[0] == '{';
    QObject *obj;
    QDict *args;
    Visitor *v;

    if (is_json) {
        obj = qobject_from_json(str, errp);
        if (!obj) {
            return NULL;
        }
        args = qobject_to(QDict, obj);
        assert(args);
        v = qobject_input_visitor_new(QOBJECT(args));
    } else {
        args = keyval_parse(str, implied_key, errp);
        if (!args) {
            return NULL;
        }
        v = qobject_input_visitor_new_keyval(QOBJECT(args));
    }
    qobject_unref(args);

    return v;
}
