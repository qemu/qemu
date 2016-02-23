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

#ifndef QBOOL_H
#define QBOOL_H

#include "qapi/qmp/qobject.h"

typedef struct QBool {
    QObject base;
    bool value;
} QBool;

QBool *qbool_from_bool(bool value);
bool qbool_get_bool(const QBool *qb);
QBool *qobject_to_qbool(const QObject *obj);
void qbool_destroy_obj(QObject *obj);

#endif /* QBOOL_H */
