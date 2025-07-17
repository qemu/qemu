/*
 * 9p backend
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <glib/gprintf.h>
#include <dirent.h>
#include <utime.h>

#include "9p-marshal.h"

P9ARRAY_DEFINE_TYPE(V9fsString, v9fs_string_free);

void v9fs_string_free(V9fsString *str)
{
    g_free(str->data);
    str->data = NULL;
    str->size = 0;
}

void v9fs_string_sprintf(V9fsString *str, const char *fmt, ...)
{
    va_list ap;

    v9fs_string_free(str);

    va_start(ap, fmt);
    str->size = g_vasprintf(&str->data, fmt, ap);
    va_end(ap);
}

void v9fs_string_copy(V9fsString *lhs, V9fsString *rhs)
{
    v9fs_string_free(lhs);
    v9fs_string_sprintf(lhs, "%s", rhs->data);
}
