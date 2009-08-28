/*
 * QString data type.
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */
#include "qobject.h"
#include "qstring.h"
#include "qemu-common.h"

static const QType qstring_type;

/**
 * qstring_from_str(): Create a new QString from a regular C string
 *
 * Return strong reference.
 */
QString *qstring_from_str(const char *str)
{
    QString *qstring;

    qstring = qemu_malloc(sizeof(*qstring));
    qstring->string = qemu_strdup(str);
    QOBJECT_INIT(qstring, &qstring_type);

    return qstring;
}

/**
 * qobject_to_qstring(): Convert a QObject to a QString
 */
QString *qobject_to_qstring(const QObject *obj)
{
    if (qobject_type(obj) != QTYPE_QSTRING)
        return NULL;

    return container_of(obj, QString, base);
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
 * qstring_destroy_obj(): Free all memory allocated by a QString
 * object
 */
static void qstring_destroy_obj(QObject *obj)
{
    QString *qs;

    assert(obj != NULL);
    qs = qobject_to_qstring(obj);
    qemu_free(qs->string);
    qemu_free(qs);
}

static const QType qstring_type = {
    .code = QTYPE_QSTRING,
    .destroy = qstring_destroy_obj,
};
