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

#include "qapi/qmp/qbool.h"
#include "qapi/qmp/qobject.h"
#include "qemu-common.h"

static void qbool_destroy_obj(QObject *obj);

static const QType qbool_type = {
    .code = QTYPE_QBOOL,
    .destroy = qbool_destroy_obj,
};

/**
 * qbool_from_int(): Create a new QBool from an int
 *
 * Return strong reference.
 */
QBool *qbool_from_int(int value)
{
    QBool *qb;

    qb = g_malloc(sizeof(*qb));
    qb->value = value;
    QOBJECT_INIT(qb, &qbool_type);

    return qb;
}

/**
 * qbool_get_int(): Get the stored int
 */
int qbool_get_int(const QBool *qb)
{
    return qb->value;
}

/**
 * qobject_to_qbool(): Convert a QObject into a QBool
 */
QBool *qobject_to_qbool(const QObject *obj)
{
    if (qobject_type(obj) != QTYPE_QBOOL)
        return NULL;

    return container_of(obj, QBool, base);
}

/**
 * qbool_destroy_obj(): Free all memory allocated by a
 * QBool object
 */
static void qbool_destroy_obj(QObject *obj)
{
    assert(obj != NULL);
    g_free(qobject_to_qbool(obj));
}
