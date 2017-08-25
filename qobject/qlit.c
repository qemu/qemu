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
    LiteralQObject *objs;
    int result;
} QListCompareHelper;

static void compare_helper(QObject *obj, void *opaque)
{
    QListCompareHelper *helper = opaque;

    if (helper->result == 0) {
        return;
    }

    if (helper->objs[helper->index].type == QTYPE_NONE) {
        helper->result = 0;
        return;
    }

    helper->result =
        compare_litqobj_to_qobj(&helper->objs[helper->index++], obj);
}

int compare_litqobj_to_qobj(LiteralQObject *lhs, QObject *rhs)
{
    int64_t val;

    if (!rhs || lhs->type != qobject_type(rhs)) {
        return 0;
    }

    switch (lhs->type) {
    case QTYPE_QNUM:
        g_assert(qnum_get_try_int(qobject_to_qnum(rhs), &val));
        return lhs->value.qnum == val;
    case QTYPE_QSTRING:
        return (strcmp(lhs->value.qstr,
                       qstring_get_str(qobject_to_qstring(rhs))) == 0);
    case QTYPE_QDICT: {
        int i;

        for (i = 0; lhs->value.qdict[i].key; i++) {
            QObject *obj = qdict_get(qobject_to_qdict(rhs),
                                     lhs->value.qdict[i].key);

            if (!compare_litqobj_to_qobj(&lhs->value.qdict[i].value, obj)) {
                return 0;
            }
        }

        return 1;
    }
    case QTYPE_QLIST: {
        QListCompareHelper helper;

        helper.index = 0;
        helper.objs = lhs->value.qlist;
        helper.result = 1;

        qlist_iter(qobject_to_qlist(rhs), compare_helper, &helper);

        return helper.result;
    }
    default:
        break;
    }

    return 0;
}
