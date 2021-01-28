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

/*
 * QNum encapsulates how our dialect of JSON fills in the blanks left
 * by the JSON specification (RFC 8259) regarding numbers.
 *
 * Conceptually, we treat number as an abstract type with three
 * concrete subtypes: floating-point, signed integer, unsigned
 * integer.  QNum implements this as a discriminated union of double,
 * int64_t, uint64_t.
 *
 * The JSON parser picks the subtype as follows.  If the number has a
 * decimal point or an exponent, it is floating-point.  Else if it
 * fits into int64_t, it's signed integer.  Else if it fits into
 * uint64_t, it's unsigned integer.  Else it's floating-point.
 *
 * Any number can serve as double: qnum_get_double() converts under
 * the hood.
 *
 * An integer can serve as signed / unsigned integer as long as it is
 * in range: qnum_get_try_int() / qnum_get_try_uint() check range and
 * convert under the hood.
 */
struct QNum {
    struct QObjectBase_ base;
    QNumKind kind;
    union {
        int64_t i64;
        uint64_t u64;
        double dbl;
    } u;
};

QNum *qnum_from_int(int64_t value);
QNum *qnum_from_uint(uint64_t value);
QNum *qnum_from_double(double value);

bool qnum_get_try_int(const QNum *qn, int64_t *val);
int64_t qnum_get_int(const QNum *qn);

bool qnum_get_try_uint(const QNum *qn, uint64_t *val);
uint64_t qnum_get_uint(const QNum *qn);

double qnum_get_double(QNum *qn);

char *qnum_to_string(QNum *qn);

#endif /* QNUM_H */
