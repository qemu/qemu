/*
 * QError Module
 *
 * Copyright (C) 2009 Red Hat Inc.
 *
 * Authors:
 *  Luiz Capitulino <lcapitulino@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#include "monitor/monitor.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qerror.h"
#include "qemu-common.h"

static void qerror_destroy_obj(QObject *obj);

static const QType qerror_type = {
    .code = QTYPE_QERROR,
    .destroy = qerror_destroy_obj,
};

/**
 * qerror_new(): Create a new QError
 *
 * Return strong reference.
 */
static QError *qerror_new(void)
{
    QError *qerr;

    qerr = g_malloc0(sizeof(*qerr));
    QOBJECT_INIT(qerr, &qerror_type);

    return qerr;
}

/**
 * qerror_from_info(): Create a new QError from error information
 *
 * Return strong reference.
 */
static QError * GCC_FMT_ATTR(2, 0)
qerror_from_info(ErrorClass err_class, const char *fmt, va_list *va)
{
    QError *qerr;

    qerr = qerror_new();
    loc_save(&qerr->loc);

    qerr->err_msg = g_strdup_vprintf(fmt, *va);
    qerr->err_class = err_class;

    return qerr;
}

/**
 * qerror_human(): Format QError data into human-readable string.
 */
QString *qerror_human(const QError *qerror)
{
    return qstring_from_str(qerror->err_msg);
}

/**
 * qerror_print(): Print QError data
 *
 * This function will print the member 'desc' of the specified QError object,
 * it uses error_report() for this, so that the output is routed to the right
 * place (ie. stderr or Monitor's device).
 */
static void qerror_print(QError *qerror)
{
    QString *qstring = qerror_human(qerror);
    loc_push_restore(&qerror->loc);
    error_report("%s", qstring_get_str(qstring));
    loc_pop(&qerror->loc);
    QDECREF(qstring);
}

void qerror_report(ErrorClass eclass, const char *fmt, ...)
{
    va_list va;
    QError *qerror;

    va_start(va, fmt);
    qerror = qerror_from_info(eclass, fmt, &va);
    va_end(va);

    if (monitor_cur_is_qmp()) {
        monitor_set_error(cur_mon, qerror);
    } else {
        qerror_print(qerror);
        QDECREF(qerror);
    }
}

/* Evil... */
struct Error
{
    char *msg;
    ErrorClass err_class;
};

void qerror_report_err(Error *err)
{
    QError *qerr;

    qerr = qerror_new();
    loc_save(&qerr->loc);
    qerr->err_msg = g_strdup(err->msg);
    qerr->err_class = err->err_class;

    if (monitor_cur_is_qmp()) {
        monitor_set_error(cur_mon, qerr);
    } else {
        qerror_print(qerr);
        QDECREF(qerr);
    }
}

/**
 * qobject_to_qerror(): Convert a QObject into a QError
 */
static QError *qobject_to_qerror(const QObject *obj)
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

    g_free(qerr->err_msg);
    g_free(qerr);
}
