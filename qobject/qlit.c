/*
 * QLit literal qobject
 *
 * Copyright IBM, Corp. 2009
 * Copyright (c) 2013, 2015, 2017 Red Hat Inc.
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Markus Armbruster <armbru@redhat.com>
 *  Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qapi/qmp/qlit.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qnum.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qnull.h"

static bool qlit_equal_qdict(const QLitObject *lhs, const QDict *qdict)
{
    int i;

    for (i = 0; lhs->value.qdict[i].key; i++) {
        QObject *obj = qdict_get(qdict, lhs->value.qdict[i].key);

        if (!qlit_equal_qobject(&lhs->value.qdict[i].value, obj)) {
            return false;
        }
    }

    /* Note: the literal qdict must not contain duplicates, this is
     * considered a programming error and it isn't checked here. */
    if (qdict_size(qdict) != i) {
        return false;
    }

    return true;
}

static bool qlit_equal_qlist(const QLitObject *lhs, const QList *qlist)
{
    QListEntry *e;
    int i = 0;

    QLIST_FOREACH_ENTRY(qlist, e) {
        QObject *obj = qlist_entry_obj(e);

        if (!qlit_equal_qobject(&lhs->value.qlist[i], obj)) {
            return false;
        }
        i++;
    }

    return !e && lhs->value.qlist[i].type == QTYPE_NONE;
}

bool qlit_equal_qobject(const QLitObject *lhs, const QObject *rhs)
{
    if (!rhs || lhs->type != qobject_type(rhs)) {
        return false;
    }

    switch (lhs->type) {
    case QTYPE_QBOOL:
        return lhs->value.qbool == qbool_get_bool(qobject_to(QBool, rhs));
    case QTYPE_QNUM:
        return lhs->value.qnum ==  qnum_get_int(qobject_to(QNum, rhs));
    case QTYPE_QSTRING:
        return (strcmp(lhs->value.qstr,
                       qstring_get_str(qobject_to(QString, rhs))) == 0);
    case QTYPE_QDICT:
        return qlit_equal_qdict(lhs, qobject_to(QDict, rhs));
    case QTYPE_QLIST:
        return qlit_equal_qlist(lhs, qobject_to(QList, rhs));
    case QTYPE_QNULL:
        return true;
    default:
        break;
    }

    return false;
}

QObject *qobject_from_qlit(const QLitObject *qlit)
{
    switch (qlit->type) {
    case QTYPE_QNULL:
        return QOBJECT(qnull());
    case QTYPE_QNUM:
        return QOBJECT(qnum_from_int(qlit->value.qnum));
    case QTYPE_QSTRING:
        return QOBJECT(qstring_from_str(qlit->value.qstr));
    case QTYPE_QDICT: {
        QDict *qdict = qdict_new();
        QLitDictEntry *e;

        for (e = qlit->value.qdict; e->key; e++) {
            qdict_put_obj(qdict, e->key, qobject_from_qlit(&e->value));
        }
        return QOBJECT(qdict);
    }
    case QTYPE_QLIST: {
        QList *qlist = qlist_new();
        QLitObject *e;

        for (e = qlit->value.qlist; e->type != QTYPE_NONE; e++) {
            qlist_append_obj(qlist, qobject_from_qlit(e));
        }
        return QOBJECT(qlist);
    }
    case QTYPE_QBOOL:
        return QOBJECT(qbool_from_bool(qlit->value.qbool));
    default:
        assert(0);
    }

    return NULL;
}
