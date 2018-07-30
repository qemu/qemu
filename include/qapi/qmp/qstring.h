/*
 * QString Module
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef QSTRING_H
#define QSTRING_H

#include "qapi/qmp/qobject.h"

struct QString {
    struct QObjectBase_ base;
    char *string;
    size_t length;
    size_t capacity;
};

QString *qstring_new(void);
QString *qstring_from_str(const char *str);
QString *qstring_from_substr(const char *str, size_t start, size_t end);
size_t qstring_get_length(const QString *qstring);
const char *qstring_get_str(const QString *qstring);
const char *qstring_get_try_str(const QString *qstring);
const char *qobject_get_try_str(const QObject *qstring);
void qstring_append_int(QString *qstring, int64_t value);
void qstring_append(QString *qstring, const char *str);
void qstring_append_chr(QString *qstring, int c);
bool qstring_is_equal(const QObject *x, const QObject *y);
void qstring_destroy_obj(QObject *obj);

#endif /* QSTRING_H */
