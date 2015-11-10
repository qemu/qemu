/*
 * QEMU Error Objects
 *
 * Copyright IBM, Corp. 2011
 * Copyright (C) 2011-2015 Red Hat, Inc.
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Markus Armbruster <armbru@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.  See
 * the COPYING.LIB file in the top-level directory.
 */

/*
 * Error reporting system loosely patterned after Glib's GError.
 *
 * Create an error:
 *     error_setg(&err, "situation normal, all fouled up");
 *
 * Report an error to stderr:
 *     error_report_err(err);
 * This frees the error object.
 *
 * Report an error somewhere else:
 *     const char *msg = error_get_pretty(err);
 *     do with msg what needs to be done...
 *     error_free(err);
 *
 * Handle an error without reporting it (just for completeness):
 *     error_free(err);
 *
 * Assert that an expected error occurred, but clean it up without
 * reporting it (primarily useful in testsuites):
 *     error_free_or_abort(&err);
 *
 * Pass an existing error to the caller:
 *     error_propagate(errp, err);
 * where Error **errp is a parameter, by convention the last one.
 *
 * Create a new error and pass it to the caller:
 *     error_setg(errp, "situation normal, all fouled up");
 *
 * Call a function and receive an error from it:
 *     Error *err = NULL;
 *     foo(arg, &err);
 *     if (err) {
 *         handle the error...
 *     }
 *
 * Call a function ignoring errors:
 *     foo(arg, NULL);
 *
 * Call a function aborting on errors:
 *     foo(arg, &error_abort);
 *
 * Call a function treating errors as fatal:
 *     foo(arg, &error_fatal);
 *
 * Receive an error and pass it on to the caller:
 *     Error *err = NULL;
 *     foo(arg, &err);
 *     if (err) {
 *         handle the error...
 *         error_propagate(errp, err);
 *     }
 * where Error **errp is a parameter, by convention the last one.
 *
 * Do *not* "optimize" this to
 *     foo(arg, errp);
 *     if (*errp) { // WRONG!
 *         handle the error...
 *     }
 * because errp may be NULL!
 *
 * But when all you do with the error is pass it on, please use
 *     foo(arg, errp);
 * for readability.
 */

#ifndef ERROR_H
#define ERROR_H

#include "qemu/compiler.h"
#include "qapi-types.h"
#include <stdbool.h>

/*
 * Opaque error object.
 */
typedef struct Error Error;

/*
 * Get @err's human-readable error message.
 */
const char *error_get_pretty(Error *err);

/*
 * Get @err's error class.
 * Note: use of error classes other than ERROR_CLASS_GENERIC_ERROR is
 * strongly discouraged.
 */
ErrorClass error_get_class(const Error *err);

/*
 * Create a new error object and assign it to *@errp.
 * If @errp is NULL, the error is ignored.  Don't bother creating one
 * then.
 * If @errp is &error_abort, print a suitable message and abort().
 * If @errp is &error_fatal, print a suitable message and exit(1).
 * If @errp is anything else, *@errp must be NULL.
 * The new error's class is ERROR_CLASS_GENERIC_ERROR, and its
 * human-readable error message is made from printf-style @fmt, ...
 */
#define error_setg(errp, fmt, ...)                              \
    error_setg_internal((errp), __FILE__, __LINE__, __func__,   \
                        (fmt), ## __VA_ARGS__)
void error_setg_internal(Error **errp,
                         const char *src, int line, const char *func,
                         const char *fmt, ...)
    GCC_FMT_ATTR(5, 6);

/*
 * Just like error_setg(), with @os_error info added to the message.
 * If @os_error is non-zero, ": " + strerror(os_error) is appended to
 * the human-readable error message.
 */
#define error_setg_errno(errp, os_error, fmt, ...)                      \
    error_setg_errno_internal((errp), __FILE__, __LINE__, __func__,     \
                              (os_error), (fmt), ## __VA_ARGS__)
void error_setg_errno_internal(Error **errp,
                               const char *fname, int line, const char *func,
                               int os_error, const char *fmt, ...)
    GCC_FMT_ATTR(6, 7);

#ifdef _WIN32
/*
 * Just like error_setg(), with @win32_error info added to the message.
 * If @win32_error is non-zero, ": " + g_win32_error_message(win32_err)
 * is appended to the human-readable error message.
 */
#define error_setg_win32(errp, win32_err, fmt, ...)                     \
    error_setg_win32_internal((errp), __FILE__, __LINE__, __func__,     \
                              (win32_err), (fmt), ## __VA_ARGS__)
void error_setg_win32_internal(Error **errp,
                               const char *src, int line, const char *func,
                               int win32_err, const char *fmt, ...)
    GCC_FMT_ATTR(6, 7);
#endif

/*
 * Propagate error object (if any) from @local_err to @dst_errp.
 * If @local_err is NULL, do nothing (because there's nothing to
 * propagate).
 * Else, if @dst_errp is NULL, errors are being ignored.  Free the
 * error object.
 * Else, if @dst_errp is &error_abort, print a suitable message and
 * abort().
 * Else, if @dst_errp is &error_fatal, print a suitable message and
 * exit(1).
 * Else, if @dst_errp already contains an error, ignore this one: free
 * the error object.
 * Else, move the error object from @local_err to *@dst_errp.
 * On return, @local_err is invalid.
 */
void error_propagate(Error **dst_errp, Error *local_err);

/**
 * Append a printf-style human-readable explanation to an existing error.
 * May be called multiple times, and safe if @errp is NULL.
 */
void error_append_hint(Error **errp, const char *fmt, ...)
    GCC_FMT_ATTR(2, 3);

/*
 * Convenience function to report open() failure.
 */
#define error_setg_file_open(errp, os_errno, filename)                  \
    error_setg_file_open_internal((errp), __FILE__, __LINE__, __func__, \
                                  (os_errno), (filename))
void error_setg_file_open_internal(Error **errp,
                                   const char *src, int line, const char *func,
                                   int os_errno, const char *filename);

/*
 * Return an exact copy of @err.
 */
Error *error_copy(const Error *err);

/*
 * Free @err.
 * @err may be NULL.
 */
void error_free(Error *err);

/*
 * Convenience function to assert that *@errp is set, then silently free it.
 */
void error_free_or_abort(Error **errp);

/*
 * Convenience function to error_report() and free @err.
 */
void error_report_err(Error *);

/*
 * Just like error_setg(), except you get to specify the error class.
 * Note: use of error classes other than ERROR_CLASS_GENERIC_ERROR is
 * strongly discouraged.
 */
#define error_set(errp, err_class, fmt, ...)                    \
    error_set_internal((errp), __FILE__, __LINE__, __func__,    \
                       (err_class), (fmt), ## __VA_ARGS__)
void error_set_internal(Error **errp,
                        const char *src, int line, const char *func,
                        ErrorClass err_class, const char *fmt, ...)
    GCC_FMT_ATTR(6, 7);

/*
 * Pass to error_setg() & friends to abort() on error.
 */
extern Error *error_abort;

/*
 * Pass to error_setg() & friends to exit(1) on error.
 */
extern Error *error_fatal;

#endif
