/*
 * GLIB Compatibility Functions
 *
 * Copyright IBM, Corp. 2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_GLIB_COMPAT_H
#define QEMU_GLIB_COMPAT_H

#include <glib.h>

#if !GLIB_CHECK_VERSION(2, 14, 0)
static inline guint g_timeout_add_seconds(guint interval, GSourceFunc function,
                                          gpointer data)
{
    return g_timeout_add(interval * 1000, function, data);
}
#endif

#endif
