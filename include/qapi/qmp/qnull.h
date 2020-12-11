/*
 * QNull
 *
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1
 * or later.  See the COPYING.LIB file in the top-level directory.
 */

#ifndef QNULL_H
#define QNULL_H

#include "qapi/qmp/qobject.h"

struct QNull {
    struct QObjectBase_ base;
};

extern QNull qnull_;

static inline QNull *qnull(void)
{
    return qobject_ref(&qnull_);
}

#endif /* QNULL_H */
