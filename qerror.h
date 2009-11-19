/*
 * QError header file.
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#ifndef QERROR_H
#define QERROR_H

#include "qdict.h"
#include <stdarg.h>

typedef struct QErrorStringTable {
    const char *desc;
    const char *error_fmt;
} QErrorStringTable;

typedef struct QError {
    QObject_HEAD;
    QDict *error;
    int linenr;
    const char *file;
    const char *func;
    const QErrorStringTable *entry;
} QError;

QError *qerror_new(void);
QError *qerror_from_info(const char *file, int linenr, const char *func,
                         const char *fmt, va_list *va);
void qerror_print(const QError *qerror);
QError *qobject_to_qerror(const QObject *obj);

/*
 * QError class list
 */
#define QERR_DEVICE_NOT_FOUND \
        "{ 'class': 'DeviceNotFound', 'data': { 'device': %s } }"

#endif /* QERROR_H */
