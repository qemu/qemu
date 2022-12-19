/*
 * Copyright (c) 2018  Citrix Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_XEN_BUS_HELPER_H
#define HW_XEN_BUS_HELPER_H

#include "hw/xen/xen_common.h"

const char *xs_strstate(enum xenbus_state state);

void xs_node_create(struct xs_handle *xsh,  xs_transaction_t tid,
                    const char *node, struct xs_permissions perms[],
                    unsigned int nr_perms, Error **errp);
void xs_node_destroy(struct xs_handle *xsh,  xs_transaction_t tid,
                     const char *node, Error **errp);

/* Write to node/key unless node is empty, in which case write to key */
void xs_node_vprintf(struct xs_handle *xsh,  xs_transaction_t tid,
                     const char *node, const char *key, Error **errp,
                     const char *fmt, va_list ap)
    G_GNUC_PRINTF(6, 0);
void xs_node_printf(struct xs_handle *xsh,  xs_transaction_t tid,
                    const char *node, const char *key, Error **errp,
                    const char *fmt, ...)
    G_GNUC_PRINTF(6, 7);

/* Read from node/key unless node is empty, in which case read from key */
int xs_node_vscanf(struct xs_handle *xsh,  xs_transaction_t tid,
                   const char *node, const char *key, Error **errp,
                   const char *fmt, va_list ap)
    G_GNUC_SCANF(6, 0);
int xs_node_scanf(struct xs_handle *xsh,  xs_transaction_t tid,
                  const char *node, const char *key, Error **errp,
                  const char *fmt, ...)
    G_GNUC_SCANF(6, 7);

/* Watch node/key unless node is empty, in which case watch key */
void xs_node_watch(struct xs_handle *xsh, const char *node, const char *key,
                   char *token, Error **errp);
void xs_node_unwatch(struct xs_handle *xsh, const char *node, const char *key,
                     const char *token, Error **errp);

#endif /* HW_XEN_BUS_HELPER_H */
