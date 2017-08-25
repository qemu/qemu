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

#include "qapi-types.h"
#include "qobject.h"

typedef struct LiteralQDictEntry LiteralQDictEntry;
typedef struct LiteralQObject LiteralQObject;

struct LiteralQObject {
    int type;
    union {
        int64_t qnum;
        const char *qstr;
        LiteralQDictEntry *qdict;
        LiteralQObject *qlist;
    } value;
};

struct LiteralQDictEntry {
    const char *key;
    LiteralQObject value;
};

#define QLIT_QNUM(val) \
    (LiteralQObject){.type = QTYPE_QNUM, .value.qnum = (val)}
#define QLIT_QSTR(val) \
    (LiteralQObject){.type = QTYPE_QSTRING, .value.qstr = (val)}
#define QLIT_QDICT(val) \
    (LiteralQObject){.type = QTYPE_QDICT, .value.qdict = (val)}
#define QLIT_QLIST(val) \
    (LiteralQObject){.type = QTYPE_QLIST, .value.qlist = (val)}

int compare_litqobj_to_qobj(LiteralQObject *lhs, QObject *rhs);

#endif /* QLIT_H */
