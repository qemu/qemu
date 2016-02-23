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

#ifndef QINT_H
#define QINT_H

#include "qapi/qmp/qobject.h"

typedef struct QInt {
    QObject base;
    int64_t value;
} QInt;

QInt *qint_from_int(int64_t value);
int64_t qint_get_int(const QInt *qi);
QInt *qobject_to_qint(const QObject *obj);
void qint_destroy_obj(QObject *obj);

#endif /* QINT_H */
