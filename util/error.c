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
#include "qapi/error.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qdict.h"
#include "qapi-types.h"
#include "qapi/qmp/qerror.h"

struct Error
{
    char *msg;
    ErrorClass err_class;
};

Error *error_abort;

void error_set(Error **errp, ErrorClass err_class, const char *fmt, ...)
{
    Error *err;
    va_list ap;
    int saved_errno = errno;

    if (errp == NULL) {
        return;
    }
    assert(*errp == NULL);

    err = g_malloc0(sizeof(*err));

    va_start(ap, fmt);
    err->msg = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    err->err_class = err_class;

    if (errp == &error_abort) {
        error_report("%s", error_get_pretty(err));
        abort();
    }

    *errp = err;

    errno = saved_errno;
}

void error_set_errno(Error **errp, int os_errno, ErrorClass err_class,
                     const char *fmt, ...)
{
    Error *err;
    char *msg1;
    va_list ap;
    int saved_errno = errno;

    if (errp == NULL) {
        return;
    }
    assert(*errp == NULL);

    err = g_malloc0(sizeof(*err));

    va_start(ap, fmt);
    msg1 = g_strdup_vprintf(fmt, ap);
    if (os_errno != 0) {
        err->msg = g_strdup_printf("%s: %s", msg1, strerror(os_errno));
        g_free(msg1);
    } else {
        err->msg = msg1;
    }
    va_end(ap);
    err->err_class = err_class;

    if (errp == &error_abort) {
        error_report("%s", error_get_pretty(err));
        abort();
    }

    *errp = err;

    errno = saved_errno;
}

void error_setg_file_open(Error **errp, int os_errno, const char *filename)
{
    error_setg_errno(errp, os_errno, "Could not open '%s'", filename);
}

#ifdef _WIN32

void error_set_win32(Error **errp, int win32_err, ErrorClass err_class,
                     const char *fmt, ...)
{
    Error *err;
    char *msg1;
    va_list ap;

    if (errp == NULL) {
        return;
    }
    assert(*errp == NULL);

    err = g_malloc0(sizeof(*err));

    va_start(ap, fmt);
    msg1 = g_strdup_vprintf(fmt, ap);
    if (win32_err != 0) {
        char *msg2 = g_win32_error_message(win32_err);
        err->msg = g_strdup_printf("%s: %s (error: %x)", msg1, msg2,
                                   (unsigned)win32_err);
        g_free(msg2);
        g_free(msg1);
    } else {
        err->msg = msg1;
    }
    va_end(ap);
    err->err_class = err_class;

    if (errp == &error_abort) {
        error_report("%s", error_get_pretty(err));
        abort();
    }

    *errp = err;
}

#endif

Error *error_copy(const Error *err)
{
    Error *err_new;

    err_new = g_malloc0(sizeof(*err));
    err_new->msg = g_strdup(err->msg);
    err_new->err_class = err->err_class;

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
        g_free(err->msg);
        g_free(err);
    }
}

void error_propagate(Error **dst_err, Error *local_err)
{
    if (local_err && dst_err == &error_abort) {
        error_report("%s", error_get_pretty(local_err));
        abort();
    } else if (dst_err && !*dst_err) {
        *dst_err = local_err;
    } else if (local_err) {
        error_free(local_err);
    }
}
