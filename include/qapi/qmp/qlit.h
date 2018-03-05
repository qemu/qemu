/*
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
 *
 */
#ifndef QLIT_H
#define QLIT_H

#include "qobject.h"

typedef struct QLitDictEntry QLitDictEntry;
typedef struct QLitObject QLitObject;

struct QLitObject {
    QType type;
    union {
        bool qbool;
        int64_t qnum;
        const char *qstr;
        QLitDictEntry *qdict;
        QLitObject *qlist;
    } value;
};

struct QLitDictEntry {
    const char *key;
    QLitObject value;
};

#define QLIT_QNULL \
    { .type = QTYPE_QNULL }
#define QLIT_QBOOL(val) \
    { .type = QTYPE_QBOOL, .value.qbool = (val) }
#define QLIT_QNUM(val) \
    { .type = QTYPE_QNUM, .value.qnum = (val) }
#define QLIT_QSTR(val) \
    { .type = QTYPE_QSTRING, .value.qstr = (val) }
#define QLIT_QDICT(val) \
    { .type = QTYPE_QDICT, .value.qdict = (val) }
#define QLIT_QLIST(val) \
    { .type = QTYPE_QLIST, .value.qlist = (val) }

bool qlit_equal_qobject(const QLitObject *lhs, const QObject *rhs);

QObject *qobject_from_qlit(const QLitObject *qlit);

#endif /* QLIT_H */
