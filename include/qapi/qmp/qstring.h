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
    const char *string;
};

QString *qstring_new(void);
QString *qstring_from_str(const char *str);
QString *qstring_from_substr(const char *str, size_t start, size_t end);
QString *qstring_from_gstring(GString *gstr);
const char *qstring_get_str(const QString *qstring);

#endif /* QSTRING_H */
