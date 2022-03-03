/*
 * QEMU Block backends
 *
 * Copyright (C) 2014-2016 Red Hat, Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1
 * or later.  See the COPYING.LIB file in the top-level directory.
 */

#ifndef BLOCK_BACKEND_COMMON_H
#define BLOCK_BACKEND_COMMON_H

#include "qemu/iov.h"
#include "block/throttle-groups.h"

/*
 * TODO Have to include block/block.h for a bunch of block layer
 * types.  Unfortunately, this pulls in the whole BlockDriverState
 * API, which we don't want used by many BlockBackend users.  Some of
 * the types belong here, and the rest should be split into a common
 * header and one for the BlockDriverState API.
 */
#include "block/block.h"

/* Callbacks for block device models */
typedef struct BlockDevOps {

    /*
     * Global state (GS) API. These functions run under the BQL.
     *
     * See include/block/block-global-state.h for more information about
     * the GS API.
     */

    /*
     * Runs when virtual media changed (monitor commands eject, change)
     * Argument load is true on load and false on eject.
     * Beware: doesn't run when a host device's physical media
     * changes.  Sure would be useful if it did.
     * Device models with removable media must implement this callback.
     */
    void (*change_media_cb)(void *opaque, bool load, Error **errp);
    /*
     * Runs when an eject request is issued from the monitor, the tray
     * is closed, and the medium is locked.
     * Device models that do not implement is_medium_locked will not need
     * this callback.  Device models that can lock the medium or tray might
     * want to implement the callback and unlock the tray when "force" is
     * true, even if they do not support eject requests.
     */
    void (*eject_request_cb)(void *opaque, bool force);

    /*
     * Is the virtual medium locked into the device?
     * Device models implement this only when device has such a lock.
     */
    bool (*is_medium_locked)(void *opaque);

    /*
     * I/O API functions. These functions are thread-safe.
     *
     * See include/block/block-io.h for more information about
     * the I/O API.
     */

    /*
     * Is the virtual tray open?
     * Device models implement this only when the device has a tray.
     */
    bool (*is_tray_open)(void *opaque);

    /*
     * Runs when the size changed (e.g. monitor command block_resize)
     */
    void (*resize_cb)(void *opaque);
    /*
     * Runs when the backend receives a drain request.
     */
    void (*drained_begin)(void *opaque);
    /*
     * Runs when the backend's last drain request ends.
     */
    void (*drained_end)(void *opaque);
    /*
     * Is the device still busy?
     */
    bool (*drained_poll)(void *opaque);
} BlockDevOps;

/*
 * This struct is embedded in (the private) BlockBackend struct and contains
 * fields that must be public. This is in particular for QLIST_ENTRY() and
 * friends so that BlockBackends can be kept in lists outside block-backend.c
 */
typedef struct BlockBackendPublic {
    ThrottleGroupMember throttle_group_member;
} BlockBackendPublic;

#endif /* BLOCK_BACKEND_COMMON_H */
