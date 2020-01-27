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

#include "qemu/osdep.h"
#include "qapi/qmp/qstring.h"

/**
 * qstring_new(): Create a new empty QString
 *
 * Return strong reference.
 */
QString *qstring_new(void)
{
    return qstring_from_str("");
}

/**
 * qstring_get_length(): Get the length of a QString
 */
size_t qstring_get_length(const QString *qstring)
{
    return qstring->length;
}

/**
 * qstring_from_substr(): Create a new QString from a C string substring
 *
 * Return string reference
 */
QString *qstring_from_substr(const char *str, size_t start, size_t end)
{
    QString *qstring;

    assert(start <= end);

    qstring = g_malloc(sizeof(*qstring));
    qobject_init(QOBJECT(qstring), QTYPE_QSTRING);

    qstring->length = end - start;
    qstring->capacity = qstring->length;

    assert(qstring->capacity < SIZE_MAX);
    qstring->string = g_malloc(qstring->capacity + 1);
    memcpy(qstring->string, str + start, qstring->length);
    qstring->string[qstring->length] = 0;

    return qstring;
}

/**
 * qstring_from_str(): Create a new QString from a regular C string
 *
 * Return strong reference.
 */
QString *qstring_from_str(const char *str)
{
    return qstring_from_substr(str, 0, strlen(str));
}

static void capacity_increase(QString *qstring, size_t len)
{
    if (qstring->capacity < (qstring->length + len)) {
        assert(len <= SIZE_MAX - qstring->capacity);
        qstring->capacity += len;
        assert(qstring->capacity <= SIZE_MAX / 2);
        qstring->capacity *= 2; /* use exponential growth */

        qstring->string = g_realloc(qstring->string, qstring->capacity + 1);
    }
}

/* qstring_append(): Append a C string to a QString
 */
void qstring_append(QString *qstring, const char *str)
{
    size_t len = strlen(str);

    capacity_increase(qstring, len);
    memcpy(qstring->string + qstring->length, str, len);
    qstring->length += len;
    qstring->string[qstring->length] = 0;
}

void qstring_append_int(QString *qstring, int64_t value)
{
    char num[32];

    snprintf(num, sizeof(num), "%" PRId64, value);
    qstring_append(qstring, num);
}

/**
 * qstring_append_chr(): Append a C char to a QString
 */
void qstring_append_chr(QString *qstring, int c)
{
    capacity_increase(qstring, 1);
    qstring->string[qstring->length++] = c;
    qstring->string[qstring->length] = 0;
}

/**
 * qstring_get_str(): Return a pointer to the stored string
 *
 * NOTE: Should be used with caution, if the object is deallocated
 * this pointer becomes invalid.
 */
const char *qstring_get_str(const QString *qstring)
{
    return qstring->string;
}

/**
 * qstring_get_try_str(): Return a pointer to the stored string
 *
 * NOTE: will return NULL if qstring is not provided.
 */
const char *qstring_get_try_str(const QString *qstring)
{
    return qstring ? qstring_get_str(qstring) : NULL;
}

/**
 * qobject_get_try_str(): Return a pointer to the corresponding string
 *
 * NOTE: the string will only be returned if the object is valid, and
 * its type is QString, otherwise NULL is returned.
 */
const char *qobject_get_try_str(const QObject *qstring)
{
    return qstring_get_try_str(qobject_to(QString, qstring));
}

/**
 * qstring_is_equal(): Test whether the two QStrings are equal
 */
bool qstring_is_equal(const QObject *x, const QObject *y)
{
    return !strcmp(qobject_to(QString, x)->string,
                   qobject_to(QString, y)->string);
}

/**
 * qstring_free(): Free the memory allocated by a QString object
 *
 * Return: if @return_str, return the underlying string, to be
 * g_free(), otherwise NULL is returned.
 */
char *qstring_free(QString *qstring, bool return_str)
{
    char *rv = NULL;

    if (return_str) {
        rv = qstring->string;
    } else {
        g_free(qstring->string);
    }

    g_free(qstring);

    return rv;
}

/**
 * qstring_destroy_obj(): Free all memory allocated by a QString
 * object
 */
void qstring_destroy_obj(QObject *obj)
{
    assert(obj != NULL);
    qstring_free(qobject_to(QString, obj), FALSE);
}
