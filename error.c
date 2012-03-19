/*
 * QEMU Error Objects
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.  See
 * the COPYING.LIB file in the top-level directory.
 */

#include "qemu-common.h"
#include "error.h"
#include "qjson.h"
#include "qdict.h"
#include "error_int.h"
#include "qerror.h"

struct Error
{
    QDict *obj;
    const char *fmt;
    char *msg;
};

void error_set(Error **errp, const char *fmt, ...)
{
    Error *err;
    va_list ap;

    if (errp == NULL) {
        return;
    }

    err = g_malloc0(sizeof(*err));

    va_start(ap, fmt);
    err->obj = qobject_to_qdict(qobject_from_jsonv(fmt, &ap));
    va_end(ap);
    err->fmt = fmt;

    *errp = err;
}

Error *error_copy(const Error *err)
{
    Error *err_new;

    err_new = g_malloc0(sizeof(*err));
    err_new->msg = g_strdup(err->msg);
    err_new->fmt = err->fmt;
    err_new->obj = err->obj;
    QINCREF(err_new->obj);

    return err_new;
}

bool error_is_set(Error **errp)
{
    return (errp && *errp);
}

const char *error_get_pretty(Error *err)
{
    if (err->msg == NULL) {
        QString *str;
        str = qerror_format(err->fmt, err->obj);
        err->msg = g_strdup(qstring_get_str(str));
        QDECREF(str);
    }

    return err->msg;
}

const char *error_get_field(Error *err, const char *field)
{
    if (strcmp(field, "class") == 0) {
        return qdict_get_str(err->obj, field);
    } else {
        QDict *dict = qdict_get_qdict(err->obj, "data");
        return qdict_get_str(dict, field);
    }
}

QDict *error_get_data(Error *err)
{
    QDict *data = qdict_get_qdict(err->obj, "data");
    QINCREF(data);
    return data;
}

void error_set_field(Error *err, const char *field, const char *value)
{
    QDict *dict = qdict_get_qdict(err->obj, "data");
    return qdict_put(dict, field, qstring_from_str(value));
}

void error_free(Error *err)
{
    if (err) {
        QDECREF(err->obj);
        g_free(err->msg);
        g_free(err);
    }
}

bool error_is_type(Error *err, const char *fmt)
{
    const char *error_class;
    char *ptr;
    char *end;

    if (!err) {
        return false;
    }

    ptr = strstr(fmt, "'class': '");
    assert(ptr != NULL);
    ptr += strlen("'class': '");

    end = strchr(ptr, '\'');
    assert(end != NULL);

    error_class = error_get_field(err, "class");
    if (strlen(error_class) != end - ptr) {
        return false;
    }

    return strncmp(ptr, error_class, end - ptr) == 0;
}

void error_propagate(Error **dst_err, Error *local_err)
{
    if (dst_err) {
        *dst_err = local_err;
    } else if (local_err) {
        error_free(local_err);
    }
}

QObject *error_get_qobject(Error *err)
{
    QINCREF(err->obj);
    return QOBJECT(err->obj);
}

void error_set_qobject(Error **errp, QObject *obj)
{
    Error *err;
    if (errp == NULL) {
        return;
    }
    err = g_malloc0(sizeof(*err));
    err->obj = qobject_to_qdict(obj);
    qobject_incref(obj);

    *errp = err;
}
