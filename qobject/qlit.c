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
#include "qapi/qmp/types.h"

typedef struct QListCompareHelper {
    int index;
    QLitObject *objs;
    bool result;
} QListCompareHelper;

static void compare_helper(QObject *obj, void *opaque)
{
    QListCompareHelper *helper = opaque;

    if (!helper->result) {
        return;
    }

    if (helper->objs[helper->index].type == QTYPE_NONE) {
        helper->result = false;
        return;
    }

    helper->result =
        qlit_equal_qobject(&helper->objs[helper->index++], obj);
}

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

bool qlit_equal_qobject(const QLitObject *lhs, const QObject *rhs)
{
    if (!rhs || lhs->type != qobject_type(rhs)) {
        return false;
    }

    switch (lhs->type) {
    case QTYPE_QBOOL:
        return lhs->value.qbool == qbool_get_bool(qobject_to_qbool(rhs));
    case QTYPE_QNUM:
        return lhs->value.qnum ==  qnum_get_int(qobject_to_qnum(rhs));
    case QTYPE_QSTRING:
        return (strcmp(lhs->value.qstr,
                       qstring_get_str(qobject_to_qstring(rhs))) == 0);
    case QTYPE_QDICT:
        return qlit_equal_qdict(lhs, qobject_to_qdict(rhs));
    case QTYPE_QLIST: {
        QListCompareHelper helper;

        helper.index = 0;
        helper.objs = lhs->value.qlist;
        helper.result = true;

        qlist_iter(qobject_to_qlist(rhs), compare_helper, &helper);

        return helper.result;
    }
    case QTYPE_QNULL:
        return true;
    default:
        break;
    }

    return false;
}
