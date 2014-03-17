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
#ifndef ERROR_H
#define ERROR_H

#include "qemu/compiler.h"
#include "qapi-types.h"
#include <stdbool.h>

/**
 * A class representing internal errors within QEMU.  An error has a ErrorClass
 * code and a human message.
 */
typedef struct Error Error;

/**
 * Set an indirect pointer to an error given a ErrorClass value and a
 * printf-style human message.  This function is not meant to be used outside
 * of QEMU.
 */
void error_set(Error **err, ErrorClass err_class, const char *fmt, ...) GCC_FMT_ATTR(3, 4);

/**
 * Set an indirect pointer to an error given a ErrorClass value and a
 * printf-style human message, followed by a strerror() string if
 * @os_error is not zero.
 */
void error_set_errno(Error **err, int os_error, ErrorClass err_class, const char *fmt, ...) GCC_FMT_ATTR(4, 5);

#ifdef _WIN32
/**
 * Set an indirect pointer to an error given a ErrorClass value and a
 * printf-style human message, followed by a g_win32_error_message() string if
 * @win32_err is not zero.
 */
void error_set_win32(Error **err, int win32_err, ErrorClass err_class, const char *fmt, ...) GCC_FMT_ATTR(4, 5);
#endif

/**
 * Same as error_set(), but sets a generic error
 */
#define error_setg(err, fmt, ...) \
    error_set(err, ERROR_CLASS_GENERIC_ERROR, fmt, ## __VA_ARGS__)
#define error_setg_errno(err, os_error, fmt, ...) \
    error_set_errno(err, os_error, ERROR_CLASS_GENERIC_ERROR, fmt, ## __VA_ARGS__)
#ifdef _WIN32
#define error_setg_win32(err, win32_err, fmt, ...) \
    error_set_win32(err, win32_err, ERROR_CLASS_GENERIC_ERROR, fmt, ## __VA_ARGS__)
#endif

/**
 * Helper for open() errors
 */
void error_setg_file_open(Error **errp, int os_errno, const char *filename);

/**
 * Returns true if an indirect pointer to an error is pointing to a valid
 * error object.
 */
bool error_is_set(Error **err);

/*
 * Get the error class of an error object.
 */
ErrorClass error_get_class(const Error *err);

/**
 * Returns an exact copy of the error passed as an argument.
 */
Error *error_copy(const Error *err);

/**
 * Get a human readable representation of an error object.
 */
const char *error_get_pretty(Error *err);

/**
 * Propagate an error to an indirect pointer to an error.  This function will
 * always transfer ownership of the error reference and handles the case where
 * dst_err is NULL correctly.  Errors after the first are discarded.
 */
void error_propagate(Error **dst_err, Error *local_err);

/**
 * Free an error object.
 */
void error_free(Error *err);

/**
 * If passed to error_set and friends, abort().
 */

extern Error *error_abort;

#endif
