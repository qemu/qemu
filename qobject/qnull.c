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

#include "qemu-common.h"
#include "qapi/qmp/qobject.h"

static void qnull_destroy_obj(QObject *obj)
{
    assert(0);
}

static const QType qnull_type = {
    .code = QTYPE_QNULL,
    .destroy = qnull_destroy_obj,
};

QObject qnull_ = {
    .type = &qnull_type,
    .refcnt = 1,
};
