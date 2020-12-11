/*
 * QObject internals
 *
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1
 * or later.  See the COPYING.LIB file in the top-level directory.
 */

#ifndef QOBJECT_INTERNAL_H
#define QOBJECT_INTERNAL_H

#include "qapi/qmp/qobject.h"

static inline void qobject_init(QObject *obj, QType type)
{
    assert(QTYPE_NONE < type && type < QTYPE__MAX);
    obj->base.refcnt = 1;
    obj->base.type = type;
}

void qbool_destroy_obj(QObject *obj);
bool qbool_is_equal(const QObject *x, const QObject *y);

void qdict_destroy_obj(QObject *obj);
bool qdict_is_equal(const QObject *x, const QObject *y);

void qlist_destroy_obj(QObject *obj);
bool qlist_is_equal(const QObject *x, const QObject *y);

bool qnull_is_equal(const QObject *x, const QObject *y);

void qnum_destroy_obj(QObject *obj);
bool qnum_is_equal(const QObject *x, const QObject *y);

void qstring_destroy_obj(QObject *obj);
bool qstring_is_equal(const QObject *x, const QObject *y);

#endif
