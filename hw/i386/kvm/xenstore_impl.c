/*
 * QEMU Xen emulation: The actual implementation of XenStore
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>, Paul Durrant <paul@xen.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "xen_xenstore.h"
#include "xenstore_impl.h"

struct XenstoreImplState {
};

int xs_impl_read(XenstoreImplState *s, unsigned int dom_id,
                 xs_transaction_t tx_id, const char *path, GByteArray *data)
{
    /*
     * The data GByteArray shall exist, and will be freed by caller.
     * Just g_byte_array_append() to it.
     */
    return ENOENT;
}

int xs_impl_write(XenstoreImplState *s, unsigned int dom_id,
                  xs_transaction_t tx_id, const char *path, GByteArray *data)
{
    /*
     * The data GByteArray shall exist, will be freed by caller. You are
     * free to use g_byte_array_steal() and keep the data.
     */
    return ENOSYS;
}

int xs_impl_directory(XenstoreImplState *s, unsigned int dom_id,
                      xs_transaction_t tx_id, const char *path,
                      uint64_t *gencnt, GList **items)
{
    /*
     * The items are (char *) to be freed by caller. Although it's consumed
     * immediately so if you want to change it to (const char *) and keep
     * them, go ahead and change the caller.
     */
    return ENOENT;
}

int xs_impl_transaction_start(XenstoreImplState *s, unsigned int dom_id,
                              xs_transaction_t *tx_id)
{
    return ENOSYS;
}

int xs_impl_transaction_end(XenstoreImplState *s, unsigned int dom_id,
                            xs_transaction_t tx_id, bool commit)
{
    return ENOSYS;
}

int xs_impl_rm(XenstoreImplState *s, unsigned int dom_id,
               xs_transaction_t tx_id, const char *path)
{
    return ENOSYS;
}

int xs_impl_get_perms(XenstoreImplState *s, unsigned int dom_id,
                      xs_transaction_t tx_id, const char *path, GList **perms)
{
    /*
     * The perms are (char *) in the <perm-as-string> wire format to be
     * freed by the caller.
     */
    return ENOSYS;
}

int xs_impl_set_perms(XenstoreImplState *s, unsigned int dom_id,
                      xs_transaction_t tx_id, const char *path, GList *perms)
{
    /*
     * The perms are (const char *) in the <perm-as-string> wire format.
     */
    return ENOSYS;
}

int xs_impl_watch(XenstoreImplState *s, unsigned int dom_id, const char *path,
                  const char *token, xs_impl_watch_fn fn, void *opaque)
{
    /*
     * When calling the callback @fn, note that the path should
     * precisely match the relative path that the guest provided, even
     * if it was a relative path which needed to be prefixed with
     * /local/domain/${domid}/
     */
    return ENOSYS;
}

int xs_impl_unwatch(XenstoreImplState *s, unsigned int dom_id,
                    const char *path, const char *token,
                    xs_impl_watch_fn fn, void *opaque)
{
    /* Remove the watch that matches all four criteria */
    return ENOSYS;
}

int xs_impl_reset_watches(XenstoreImplState *s, unsigned int dom_id)
{
    return ENOSYS;
}

XenstoreImplState *xs_impl_create(void)
{
    return g_new0(XenstoreImplState, 1);
}
