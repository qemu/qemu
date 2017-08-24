/*
 * QAPI util functions
 *
 * Copyright Fujitsu, Inc. 2014
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QAPI_UTIL_H
#define QAPI_UTIL_H

const char *qapi_enum_lookup(const char *const lookup[], int val);
int qapi_enum_parse(const char * const lookup[], const char *buf,
                    int def, Error **errp);

int parse_qapi_name(const char *name, bool complete);

#endif
