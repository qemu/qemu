/*
 * QInt Module
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/qmp/qint.h"
#include "qapi/qmp/qobject.h"
#include "qemu-common.h"

/**
 * qint_from_int(): Create a new QInt from an int64_t
 *
 * Return strong reference.
 */
QInt *qint_from_int(int64_t value)
{
    QInt *qi;

    qi = g_malloc(sizeof(*qi));
    qobject_init(QOBJECT(qi), QTYPE_QINT);
    qi->value = value;

    return qi;
}

/**
 * qint_get_int(): Get the stored integer
 */
int64_t qint_get_int(const QInt *qi)
{
    return qi->value;
}

/**
 * qobject_to_qint(): Convert a QObject into a QInt
 */
QInt *qobject_to_qint(const QObject *obj)
{
    if (!obj || qobject_type(obj) != QTYPE_QINT) {
        return NULL;
    }
    return container_of(obj, QInt, base);
}

/**
 * qint_destroy_obj(): Free all memory allocated by a
 * QInt object
 */
void qint_destroy_obj(QObject *obj)
{
    assert(obj != NULL);
    g_free(qobject_to_qint(obj));
}
