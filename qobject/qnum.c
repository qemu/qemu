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

#include "qemu/osdep.h"
#include "qapi/qmp/qnum.h"

/**
 * qnum_from_int(): Create a new QNum from an int64_t
 *
 * Return strong reference.
 */
QNum *qnum_from_int(int64_t value)
{
    QNum *qn = g_new(QNum, 1);

    qobject_init(QOBJECT(qn), QTYPE_QNUM);
    qn->kind = QNUM_I64;
    qn->u.i64 = value;

    return qn;
}

/**
 * qnum_from_uint(): Create a new QNum from an uint64_t
 *
 * Return strong reference.
 */
QNum *qnum_from_uint(uint64_t value)
{
    QNum *qn = g_new(QNum, 1);

    qobject_init(QOBJECT(qn), QTYPE_QNUM);
    qn->kind = QNUM_U64;
    qn->u.u64 = value;

    return qn;
}

/**
 * qnum_from_double(): Create a new QNum from a double
 *
 * Return strong reference.
 */
QNum *qnum_from_double(double value)
{
    QNum *qn = g_new(QNum, 1);

    qobject_init(QOBJECT(qn), QTYPE_QNUM);
    qn->kind = QNUM_DOUBLE;
    qn->u.dbl = value;

    return qn;
}

/**
 * qnum_get_try_int(): Get an integer representation of the number
 *
 * Return true on success.
 */
bool qnum_get_try_int(const QNum *qn, int64_t *val)
{
    switch (qn->kind) {
    case QNUM_I64:
        *val = qn->u.i64;
        return true;
    case QNUM_U64:
        if (qn->u.u64 > INT64_MAX) {
            return false;
        }
        *val = qn->u.u64;
        return true;
    case QNUM_DOUBLE:
        return false;
    }

    assert(0);
    return false;
}

/**
 * qnum_get_int(): Get an integer representation of the number
 *
 * assert() on failure.
 */
int64_t qnum_get_int(const QNum *qn)
{
    int64_t val;
    bool success = qnum_get_try_int(qn, &val);
    assert(success);
    return val;
}

/**
 * qnum_get_uint(): Get an unsigned integer from the number
 *
 * Return true on success.
 */
bool qnum_get_try_uint(const QNum *qn, uint64_t *val)
{
    switch (qn->kind) {
    case QNUM_I64:
        if (qn->u.i64 < 0) {
            return false;
        }
        *val = qn->u.i64;
        return true;
    case QNUM_U64:
        *val = qn->u.u64;
        return true;
    case QNUM_DOUBLE:
        return false;
    }

    assert(0);
    return false;
}

/**
 * qnum_get_uint(): Get an unsigned integer from the number
 *
 * assert() on failure.
 */
uint64_t qnum_get_uint(const QNum *qn)
{
    uint64_t val;
    bool success = qnum_get_try_uint(qn, &val);
    assert(success);
    return val;
}

/**
 * qnum_get_double(): Get a float representation of the number
 *
 * qnum_get_double() loses precision for integers beyond 53 bits.
 */
double qnum_get_double(QNum *qn)
{
    switch (qn->kind) {
    case QNUM_I64:
        return qn->u.i64;
    case QNUM_U64:
        return qn->u.u64;
    case QNUM_DOUBLE:
        return qn->u.dbl;
    }

    assert(0);
    return 0.0;
}

char *qnum_to_string(QNum *qn)
{
    char *buffer;
    int len;

    switch (qn->kind) {
    case QNUM_I64:
        return g_strdup_printf("%" PRId64, qn->u.i64);
    case QNUM_U64:
        return g_strdup_printf("%" PRIu64, qn->u.u64);
    case QNUM_DOUBLE:
        /* FIXME: snprintf() is locale dependent; but JSON requires
         * numbers to be formatted as if in the C locale. Dependence
         * on C locale is a pervasive issue in QEMU. */
        /* FIXME: This risks printing Inf or NaN, which are not valid
         * JSON values. */
        /* FIXME: the default precision of 6 for %f often causes
         * rounding errors; we should be using DBL_DECIMAL_DIG (17),
         * and only rounding to a shorter number if the result would
         * still produce the same floating point value.  */
        buffer = g_strdup_printf("%f" , qn->u.dbl);
        len = strlen(buffer);
        while (len > 0 && buffer[len - 1] == '0') {
            len--;
        }

        if (len && buffer[len - 1] == '.') {
            buffer[len - 1] = 0;
        } else {
            buffer[len] = 0;
        }

        return buffer;
    }

    assert(0);
    return NULL;
}

/**
 * qnum_is_equal(): Test whether the two QNums are equal
 *
 * Negative integers are never considered equal to unsigned integers,
 * but positive integers in the range [0, INT64_MAX] are considered
 * equal independently of whether the QNum's kind is i64 or u64.
 *
 * Doubles are never considered equal to integers.
 */
bool qnum_is_equal(const QObject *x, const QObject *y)
{
    QNum *num_x = qobject_to(QNum, x);
    QNum *num_y = qobject_to(QNum, y);

    switch (num_x->kind) {
    case QNUM_I64:
        switch (num_y->kind) {
        case QNUM_I64:
            /* Comparison in native int64_t type */
            return num_x->u.i64 == num_y->u.i64;
        case QNUM_U64:
            /* Implicit conversion of x to uin64_t, so we have to
             * check its sign before */
            return num_x->u.i64 >= 0 && num_x->u.i64 == num_y->u.u64;
        case QNUM_DOUBLE:
            return false;
        }
        abort();
    case QNUM_U64:
        switch (num_y->kind) {
        case QNUM_I64:
            return qnum_is_equal(y, x);
        case QNUM_U64:
            /* Comparison in native uint64_t type */
            return num_x->u.u64 == num_y->u.u64;
        case QNUM_DOUBLE:
            return false;
        }
        abort();
    case QNUM_DOUBLE:
        switch (num_y->kind) {
        case QNUM_I64:
        case QNUM_U64:
            return false;
        case QNUM_DOUBLE:
            /* Comparison in native double type */
            return num_x->u.dbl == num_y->u.dbl;
        }
        abort();
    }

    abort();
}

/**
 * qnum_destroy_obj(): Free all memory allocated by a
 * QNum object
 */
void qnum_destroy_obj(QObject *obj)
{
    assert(obj != NULL);
    g_free(qobject_to(QNum, obj));
}
