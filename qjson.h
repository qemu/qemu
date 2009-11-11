/*
 * QObject JSON integration
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

#ifndef QJSON_H
#define QJSON_H

#include "qobject.h"
#include "qstring.h"

QObject *qobject_from_json(const char *string);
QObject *qobject_from_jsonf(const char *string, ...)
    __attribute__((__format__ (__printf__, 1, 2)));

QString *qobject_to_json(const QObject *obj);

#endif /* QJSON_H */
