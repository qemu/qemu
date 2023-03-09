/*
 * QEMU Xen emulation: The actual implementation of XenStore
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_XENSTORE_IMPL_H
#define QEMU_XENSTORE_IMPL_H

#include "hw/xen/xen_backend_ops.h"

typedef struct XenstoreImplState XenstoreImplState;

XenstoreImplState *xs_impl_create(unsigned int dom_id);

char *xs_perm_as_string(unsigned int perm, unsigned int domid);

/*
 * These functions return *positive* error numbers. This is a little
 * unconventional but it helps to keep us honest because there is
 * also a very limited set of error numbers that they are permitted
 * to return (those in xsd_errors).
 */

int xs_impl_read(XenstoreImplState *s, unsigned int dom_id,
                 xs_transaction_t tx_id, const char *path, GByteArray *data);
int xs_impl_write(XenstoreImplState *s, unsigned int dom_id,
                  xs_transaction_t tx_id, const char *path, GByteArray *data);
int xs_impl_directory(XenstoreImplState *s, unsigned int dom_id,
                      xs_transaction_t tx_id, const char *path,
                      uint64_t *gencnt, GList **items);
int xs_impl_transaction_start(XenstoreImplState *s, unsigned int dom_id,
                              xs_transaction_t *tx_id);
int xs_impl_transaction_end(XenstoreImplState *s, unsigned int dom_id,
                            xs_transaction_t tx_id, bool commit);
int xs_impl_rm(XenstoreImplState *s, unsigned int dom_id,
               xs_transaction_t tx_id, const char *path);
int xs_impl_get_perms(XenstoreImplState *s, unsigned int dom_id,
                      xs_transaction_t tx_id, const char *path, GList **perms);
int xs_impl_set_perms(XenstoreImplState *s, unsigned int dom_id,
                      xs_transaction_t tx_id, const char *path, GList *perms);

/* This differs from xs_watch_fn because it has the token */
typedef void(xs_impl_watch_fn)(void *opaque, const char *path,
                               const char *token);
int xs_impl_watch(XenstoreImplState *s, unsigned int dom_id, const char *path,
                  const char *token, xs_impl_watch_fn fn, void *opaque);
int xs_impl_unwatch(XenstoreImplState *s, unsigned int dom_id,
                    const char *path, const char *token, xs_impl_watch_fn fn,
                    void *opaque);
int xs_impl_reset_watches(XenstoreImplState *s, unsigned int dom_id);

GByteArray *xs_impl_serialize(XenstoreImplState *s);
int xs_impl_deserialize(XenstoreImplState *s, GByteArray *bytes,
                        unsigned int dom_id, xs_impl_watch_fn watch_fn,
                        void *watch_opaque);

#endif /* QEMU_XENSTORE_IMPL_H */
