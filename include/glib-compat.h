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

#if !GLIB_CHECK_VERSION(2, 20, 0)
/*
 * Glib before 2.20.0 doesn't implement g_poll, so wrap it to compile properly
 * on older systems.
 */
static inline gint g_poll(GPollFD *fds, guint nfds, gint timeout)
{
    GMainContext *ctx = g_main_context_default();
    return g_main_context_get_poll_func(ctx)(fds, nfds, timeout);
}
#endif

#endif
