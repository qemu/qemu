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

#include <stdbool.h>

/**
 * A class representing internal errors within QEMU.  An error has a string
 * typename and optionally a set of named string parameters.
 */
typedef struct Error Error;

/**
 * Set an indirect pointer to an error given a printf-style format parameter.
 * Currently, qerror.h defines these error formats.  This function is not
 * meant to be used outside of QEMU.
 */
void error_set(Error **err, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/**
 * Returns true if an indirect pointer to an error is pointing to a valid
 * error object.
 */
bool error_is_set(Error **err);

/**
 * Get a human readable representation of an error object.
 */
const char *error_get_pretty(Error *err);

/**
 * Get an individual named error field.
 */
const char *error_get_field(Error *err, const char *field);

/**
 * Get an individual named error field.
 */
void error_set_field(Error *err, const char *field, const char *value);

/**
 * Propagate an error to an indirect pointer to an error.  This function will
 * always transfer ownership of the error reference and handles the case where
 * dst_err is NULL correctly.
 */
void error_propagate(Error **dst_err, Error *local_err);

/**
 * Free an error object.
 */
void error_free(Error *err);

/**
 * Determine if an error is of a speific type (based on the qerror format).
 * Non-QEMU users should get the `class' field to identify the error type.
 */
bool error_is_type(Error *err, const char *fmt);

#endif
