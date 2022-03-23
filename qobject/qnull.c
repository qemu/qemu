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

#include "qemu/osdep.h"
#include "qapi/qmp/qnull.h"
#include "qobject-internal.h"

QNull qnull_ = {
    .base = {
        .type = QTYPE_QNULL,
        .refcnt = 1,
    },
};

/**
 * qnull_is_equal(): Always return true because any two QNull objects
 * are equal.
 */
bool qnull_is_equal(const QObject *x, const QObject *y)
{
    return true;
}

void qnull_unref(QNull *q)
{
    qobject_unref(q);
}
