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
#include "qobject-internal.h"

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
    qstring->string = g_strndup(str + start, end - start);
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

/**
 * qstring_from_gstring(): Convert a GString to a QString
 *
 * Return strong reference.
 */

QString *qstring_from_gstring(GString *gstr)
{
    QString *qstring;

    qstring = g_malloc(sizeof(*qstring));
    qobject_init(QOBJECT(qstring), QTYPE_QSTRING);
    qstring->string = g_string_free(gstr, false);
    return qstring;
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
 * qstring_is_equal(): Test whether the two QStrings are equal
 */
bool qstring_is_equal(const QObject *x, const QObject *y)
{
    return !strcmp(qobject_to(QString, x)->string,
                   qobject_to(QString, y)->string);
}

/**
 * qstring_destroy_obj(): Free all memory allocated by a QString
 * object
 */
void qstring_destroy_obj(QObject *obj)
{
    QString *qs;

    assert(obj != NULL);
    qs = qobject_to(QString, obj);
    g_free((char *)qs->string);
    g_free(qs);
}

void qstring_unref(QString *q)
{
    qobject_unref(q);
}
