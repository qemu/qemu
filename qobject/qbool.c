/*
 * QBool Module
 *
 * Copyright IBM, Corp. 2009
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qobject.h"
#include "qemu-common.h"

/**
 * qbool_from_bool(): Create a new QBool from a bool
 *
 * Return strong reference.
 */
QBool *qbool_from_bool(bool value)
{
    QBool *qb;

    qb = g_malloc(sizeof(*qb));
    qobject_init(QOBJECT(qb), QTYPE_QBOOL);
    qb->value = value;

    return qb;
}

/**
 * qbool_get_bool(): Get the stored bool
 */
bool qbool_get_bool(const QBool *qb)
{
    return qb->value;
}

/**
 * qobject_to_qbool(): Convert a QObject into a QBool
 */
QBool *qobject_to_qbool(const QObject *obj)
{
    if (!obj || qobject_type(obj) != QTYPE_QBOOL) {
        return NULL;
    }
    return container_of(obj, QBool, base);
}

/**
 * qbool_destroy_obj(): Free all memory allocated by a
 * QBool object
 */
void qbool_destroy_obj(QObject *obj)
{
    assert(obj != NULL);
    g_free(qobject_to_qbool(obj));
}
