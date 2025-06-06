/*
 * QEMU Error Objects - struct definition
 *
 * Copyright IBM, Corp. 2011
 * Copyright (C) 2011-2015 Red Hat, Inc.
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.  See
 * the COPYING.LIB file in the top-level directory.
 */

#ifndef QAPI_ERROR_INTERNAL_H

struct Error
{
    char *msg;
    ErrorClass err_class;

    /* Used for error_abort only, may be NULL. */
    const char *func;

    /*
     * src might be NUL-terminated or not.  If it is, src_len is negative.
     * If it is not, src_len is the length.
     */
    const char *src;
    int src_len;
    int line;
    GString *hint;
};

#endif
