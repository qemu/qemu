/*
 * QObject
 *
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1
 * or later.  See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qfloat.h"
#include "qapi/qmp/qint.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qstring.h"

static void (*qdestroy[QTYPE__MAX])(QObject *) = {
    [QTYPE_NONE] = NULL,               /* No such object exists */
    [QTYPE_QNULL] = NULL,              /* qnull_ is indestructible */
    [QTYPE_QINT] = qint_destroy_obj,
    [QTYPE_QSTRING] = qstring_destroy_obj,
    [QTYPE_QDICT] = qdict_destroy_obj,
    [QTYPE_QLIST] = qlist_destroy_obj,
    [QTYPE_QFLOAT] = qfloat_destroy_obj,
    [QTYPE_QBOOL] = qbool_destroy_obj,
};

void qobject_destroy(QObject *obj)
{
    assert(!obj->refcnt);
    assert(QTYPE_QNULL < obj->type && obj->type < QTYPE__MAX);
    qdestroy[obj->type](obj);
}
