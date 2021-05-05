/*
 * Copyright (c) 2018  Citrix Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/xen/xen.h"
#include "hw/xen/xen-bus.h"
#include "hw/xen/xen-bus-helper.h"
#include "qapi/error.h"

#include <glib/gprintf.h>

struct xs_state {
    enum xenbus_state statenum;
    const char *statestr;
};
#define XS_STATE(state) { state, #state }

static struct xs_state xs_state[] = {
    XS_STATE(XenbusStateUnknown),
    XS_STATE(XenbusStateInitialising),
    XS_STATE(XenbusStateInitWait),
    XS_STATE(XenbusStateInitialised),
    XS_STATE(XenbusStateConnected),
    XS_STATE(XenbusStateClosing),
    XS_STATE(XenbusStateClosed),
    XS_STATE(XenbusStateReconfiguring),
    XS_STATE(XenbusStateReconfigured),
};

#undef XS_STATE

const char *xs_strstate(enum xenbus_state state)
{
    unsigned int i;

   for (i = 0; i < ARRAY_SIZE(xs_state); i++) {
        if (xs_state[i].statenum == state) {
            return xs_state[i].statestr;
        }
    }

    return "INVALID";
}

void xs_node_create(struct xs_handle *xsh, xs_transaction_t tid,
                    const char *node, struct xs_permissions perms[],
                    unsigned int nr_perms, Error **errp)
{
    trace_xs_node_create(node);

    if (!xs_write(xsh, tid, node, "", 0)) {
        error_setg_errno(errp, errno, "failed to create node '%s'", node);
        return;
    }

    if (!xs_set_permissions(xsh, tid, node, perms, nr_perms)) {
        error_setg_errno(errp, errno, "failed to set node '%s' permissions",
                         node);
    }
}

void xs_node_destroy(struct xs_handle *xsh, xs_transaction_t tid,
                     const char *node, Error **errp)
{
    trace_xs_node_destroy(node);

    if (!xs_rm(xsh, tid, node)) {
        error_setg_errno(errp, errno, "failed to destroy node '%s'", node);
    }
}

void xs_node_vprintf(struct xs_handle *xsh, xs_transaction_t tid,
                     const char *node, const char *key, Error **errp,
                     const char *fmt, va_list ap)
{
    char *path, *value;
    int len;

    path = (strlen(node) != 0) ? g_strdup_printf("%s/%s", node, key) :
        g_strdup(key);
    len = g_vasprintf(&value, fmt, ap);

    trace_xs_node_vprintf(path, value);

    if (!xs_write(xsh, tid, path, value, len)) {
        error_setg_errno(errp, errno, "failed to write '%s' to '%s'",
                         value, path);
    }

    g_free(value);
    g_free(path);
}

void xs_node_printf(struct xs_handle *xsh,  xs_transaction_t tid,
                    const char *node, const char *key, Error **errp,
                    const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    xs_node_vprintf(xsh, tid, node, key, errp, fmt, ap);
    va_end(ap);
}

int xs_node_vscanf(struct xs_handle *xsh,  xs_transaction_t tid,
                   const char *node, const char *key, Error **errp,
                   const char *fmt, va_list ap)
{
    char *path, *value;
    int rc;

    path = (strlen(node) != 0) ? g_strdup_printf("%s/%s", node, key) :
        g_strdup(key);
    value = xs_read(xsh, tid, path, NULL);

    trace_xs_node_vscanf(path, value);

    if (value) {
        rc = vsscanf(value, fmt, ap);
    } else {
        error_setg_errno(errp, errno, "failed to read from '%s'",
                         path);
        rc = EOF;
    }

    free(value);
    g_free(path);

    return rc;
}

int xs_node_scanf(struct xs_handle *xsh,  xs_transaction_t tid,
                  const char *node, const char *key, Error **errp,
                  const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start(ap, fmt);
    rc = xs_node_vscanf(xsh, tid, node, key, errp, fmt, ap);
    va_end(ap);

    return rc;
}

void xs_node_watch(struct xs_handle *xsh, const char *node, const char *key,
                   char *token, Error **errp)
{
    char *path;

    path = (strlen(node) != 0) ? g_strdup_printf("%s/%s", node, key) :
        g_strdup(key);

    trace_xs_node_watch(path);

    if (!xs_watch(xsh, path, token)) {
        error_setg_errno(errp, errno, "failed to watch node '%s'", path);
    }

    g_free(path);
}

void xs_node_unwatch(struct xs_handle *xsh, const char *node,
                     const char *key, const char *token, Error **errp)
{
    char *path;

    path = (strlen(node) != 0) ? g_strdup_printf("%s/%s", node, key) :
        g_strdup(key);

    trace_xs_node_unwatch(path);

    if (!xs_unwatch(xsh, path, token)) {
        error_setg_errno(errp, errno, "failed to unwatch node '%s'", path);
    }

    g_free(path);
}
