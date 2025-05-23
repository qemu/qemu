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
    const char *src, *func;
    int line;
    GString *hint;
};

#endif
