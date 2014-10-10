/*
 * Dealing with identifiers
 *
 * Copyright (C) 2014 Red Hat, Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1
 * or later.  See the COPYING.LIB file in the top-level directory.
 */

#include "qemu-common.h"

bool id_wellformed(const char *id)
{
    int i;

    if (!qemu_isalpha(id[0])) {
        return false;
    }
    for (i = 1; id[i]; i++) {
        if (!qemu_isalnum(id[i]) && !strchr("-._", id[i])) {
            return false;
        }
    }
    return true;
}
