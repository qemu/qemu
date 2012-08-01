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
#include "qapi-types.h"
#include "qerror.h"

struct Error
{
    QDict *obj;
    char *msg;
    ErrorClass err_class;
};

void error_set(Error **errp, ErrorClass err_class, const char *fmt, ...)
{
    Error *err;
    va_list ap;

    if (errp == NULL) {
        return;
    }
    assert(*errp == NULL);

    err = g_malloc0(sizeof(*err));

    va_start(ap, fmt);
    err->obj = qobject_to_qdict(qobject_from_jsonv(fmt, &ap));
    va_end(ap);
    err->msg = qerror_format(fmt, err->obj);
    err->err_class = err_class;

    *errp = err;
}

Error *error_copy(const Error *err)
{
    Error *err_new;

    err_new = g_malloc0(sizeof(*err));
    err_new->msg = g_strdup(err->msg);
    err_new->err_class = err->err_class;
    err_new->obj = err->obj;
    QINCREF(err_new->obj);

    return err_new;
}

bool error_is_set(Error **errp)
{
    return (errp && *errp);
}

ErrorClass error_get_class(const Error *err)
{
    return err->err_class;
}

const char *error_get_pretty(Error *err)
{
    return err->msg;
}

void error_free(Error *err)
{
    if (err) {
        QDECREF(err->obj);
        g_free(err->msg);
        g_free(err);
    }
}

void error_propagate(Error **dst_err, Error *local_err)
{
    if (dst_err && !*dst_err) {
        *dst_err = local_err;
    } else if (local_err) {
        error_free(local_err);
    }
}
