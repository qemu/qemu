/*
 * QNum Module
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *  Anthony Liguori <aliguori@us.ibm.com>
 *  Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef QNUM_H
#define QNUM_H

#include "qapi/qmp/qobject.h"

typedef enum {
    QNUM_I64,
    QNUM_U64,
    QNUM_DOUBLE
} QNumKind;

typedef struct QNum {
    QObject base;
    QNumKind kind;
    union {
        int64_t i64;
        uint64_t u64;
        double dbl;
    } u;
} QNum;

QNum *qnum_from_int(int64_t value);
QNum *qnum_from_uint(uint64_t value);
QNum *qnum_from_double(double value);

bool qnum_get_try_int(const QNum *qn, int64_t *val);
int64_t qnum_get_int(const QNum *qn);

bool qnum_get_try_uint(const QNum *qn, uint64_t *val);
uint64_t qnum_get_uint(const QNum *qn);

double qnum_get_double(QNum *qn);

char *qnum_to_string(QNum *qn);

QNum *qobject_to_qnum(const QObject *obj);
void qnum_destroy_obj(QObject *obj);

#endif /* QNUM_H */
