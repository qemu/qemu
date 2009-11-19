/*
 * QError: QEMU Error data-type.
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */
#include "qjson.h"
#include "qerror.h"
#include "qstring.h"
#include "sysemu.h"
#include "qemu-common.h"

static void qerror_destroy_obj(QObject *obj);

static const QType qerror_type = {
    .code = QTYPE_QERROR,
    .destroy = qerror_destroy_obj,
};

/**
 * The 'desc' parameter is a printf-like string, the format of the format
 * string is:
 *
 * %(KEY)
 *
 * Where KEY is a QDict key, which has to be passed to qerror_from_info().
 *
 * Example:
 *
 * "foo error on device: %(device) slot: %(slot_nr)"
 *
 * A single percent sign can be printed if followed by a second one,
 * for example:
 *
 * "running out of foo: %(foo)%%"
 */
const QErrorStringTable qerror_table[] = {
    {
        .error_fmt = QERR_DEVICE_NOT_FOUND,
        .desc      = "The %(device) device has not been found",
    },
    {}
};

/**
 * qerror_new(): Create a new QError
 *
 * Return strong reference.
 */
QError *qerror_new(void)
{
    QError *qerr;

    qerr = qemu_mallocz(sizeof(*qerr));
    QOBJECT_INIT(qerr, &qerror_type);

    return qerr;
}

static void qerror_abort(const QError *qerr, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "qerror: bad call in function '%s':\n", qerr->func);
    fprintf(stderr, "qerror: -> ");

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\nqerror: call at %s:%d\n", qerr->file, qerr->linenr);
    abort();
}

static void qerror_set_data(QError *qerr, const char *fmt, va_list *va)
{
    QObject *obj;

    obj = qobject_from_jsonv(fmt, va);
    if (!obj) {
        qerror_abort(qerr, "invalid format '%s'", fmt);
    }
    if (qobject_type(obj) != QTYPE_QDICT) {
        qerror_abort(qerr, "error format is not a QDict '%s'", fmt);
    }

    qerr->error = qobject_to_qdict(obj);

    obj = qdict_get(qerr->error, "class");
    if (!obj) {
        qerror_abort(qerr, "missing 'class' key in '%s'", fmt);
    }
    if (qobject_type(obj) != QTYPE_QSTRING) {
        qerror_abort(qerr, "'class' key value should be a QString");
    }
    
    obj = qdict_get(qerr->error, "data");
    if (!obj) {
        qerror_abort(qerr, "missing 'data' key in '%s'", fmt);
    }
    if (qobject_type(obj) != QTYPE_QDICT) {
        qerror_abort(qerr, "'data' key value should be a QDICT");
    }
}

static void qerror_set_desc(QError *qerr, const char *fmt)
{
    int i;

    // FIXME: inefficient loop

    for (i = 0; qerror_table[i].error_fmt; i++) {
        if (strcmp(qerror_table[i].error_fmt, fmt) == 0) {
            qerr->entry = &qerror_table[i];
            return;
        }
    }

    qerror_abort(qerr, "error format '%s' not found", fmt);
}

/**
 * qerror_from_info(): Create a new QError from error information
 *
 * The information consists of:
 *
 * - file   the file name of where the error occurred
 * - linenr the line number of where the error occurred
 * - func   the function name of where the error occurred
 * - fmt    JSON printf-like dictionary, there must exist keys 'class' and
 *          'data'
 * - va     va_list of all arguments specified by fmt
 *
 * Return strong reference.
 */
QError *qerror_from_info(const char *file, int linenr, const char *func,
                         const char *fmt, va_list *va)
{
    QError *qerr;

    qerr = qerror_new();
    qerr->linenr = linenr;
    qerr->file = file;
    qerr->func = func;

    if (!fmt) {
        qerror_abort(qerr, "QDict not specified");
    }

    qerror_set_data(qerr, fmt, va);
    qerror_set_desc(qerr, fmt);

    return qerr;
}

static void parse_error(const QError *qerror, int c)
{
    qerror_abort(qerror, "expected '%c' in '%s'", c, qerror->entry->desc);
}

static const char *append_field(QString *outstr, const QError *qerror,
                                const char *start)
{
    QObject *obj;
    QDict *qdict;
    QString *key_qs;
    const char *end, *key;

    if (*start != '%')
        parse_error(qerror, '%');
    start++;
    if (*start != '(')
        parse_error(qerror, '(');
    start++;

    end = strchr(start, ')');
    if (!end)
        parse_error(qerror, ')');

    key_qs = qstring_from_substr(start, 0, end - start - 1);
    key = qstring_get_str(key_qs);

    qdict = qobject_to_qdict(qdict_get(qerror->error, "data"));
    obj = qdict_get(qdict, key);
    if (!obj) {
        qerror_abort(qerror, "key '%s' not found in QDict", key);
    }

    switch (qobject_type(obj)) {
        case QTYPE_QSTRING:
            qstring_append(outstr, qdict_get_str(qdict, key));
            break;
        case QTYPE_QINT:
            qstring_append_int(outstr, qdict_get_int(qdict, key));
            break;
        default:
            qerror_abort(qerror, "invalid type '%c'", qobject_type(obj));
    }

    QDECREF(key_qs);
    return ++end;
}

/**
 * qerror_print(): Print QError data
 *
 * This function will print the member 'desc' of the specified QError object,
 * it uses qemu_error() for this, so that the output is routed to the right
 * place (ie. stderr or Monitor's device).
 */
void qerror_print(const QError *qerror)
{
    const char *p;
    QString *qstring;

    assert(qerror->entry != NULL);

    qstring = qstring_new();

    for (p = qerror->entry->desc; *p != '\0';) {
        if (*p != '%') {
            qstring_append_chr(qstring, *p++);
        } else if (*(p + 1) == '%') {
            qstring_append_chr(qstring, '%');
            p += 2;
        } else {
            p = append_field(qstring, qerror, p);
        }
    }

    qemu_error("%s\n", qstring_get_str(qstring));
    QDECREF(qstring);
}

/**
 * qobject_to_qerror(): Convert a QObject into a QError
 */
QError *qobject_to_qerror(const QObject *obj)
{
    if (qobject_type(obj) != QTYPE_QERROR) {
        return NULL;
    }

    return container_of(obj, QError, base);
}

/**
 * qerror_destroy_obj(): Free all memory allocated by a QError
 */
static void qerror_destroy_obj(QObject *obj)
{
    QError *qerr;

    assert(obj != NULL);
    qerr = qobject_to_qerror(obj);

    QDECREF(qerr->error);
    qemu_free(qerr);
}
