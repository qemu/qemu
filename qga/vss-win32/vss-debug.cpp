/*
 * QEMU Guest Agent VSS debug declarations
 *
 * Copyright (C) 2023 Red Hat Inc
 *
 * Authors:
 *  Konstantin Kostiuk <kkostiuk@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "vss-debug.h"
#include "vss-common.h"

void qga_debug_internal(const char *funcname, const char *fmt, ...)
{
    char user_string[512] = {0};
    char full_string[640] = {0};

    va_list args;
    va_start(args, fmt);
    if (vsnprintf(user_string, _countof(user_string), fmt, args) <= 0) {
        va_end(args);
        return;
    }

    va_end(args);

    if (snprintf(full_string, _countof(full_string),
                 QGA_PROVIDER_NAME "[%lu]: %s %s\n",
                 GetCurrentThreadId(), funcname, user_string) <= 0) {
        return;
    }

    OutputDebugString(full_string);
    fputs(full_string, stderr);
}
