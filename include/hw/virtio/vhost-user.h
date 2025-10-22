/*
 * Copyright (c) 2017-2018 Intel Corporation
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_VIRTIO_VHOST_USER_H
#define HW_VIRTIO_VHOST_USER_H

#include "chardev/char-fe.h"
#include "hw/virtio/virtio.h"

enum VhostUserProtocolFeature {
    VHOST_USER_PROTOCOL_F_MQ = 0,
    VHOST_USER_PROTOCOL_F_LOG_SHMFD = 1,
    VHOST_USER_PROTOCOL_F_RARP = 2,
    VHOST_USER_PROTOCOL_F_REPLY_ACK = 3,
    VHOST_USER_PROTOCOL_F_NET_MTU = 4,
    VHOST_USER_PROTOCOL_F_BACKEND_REQ = 5,
    VHOST_USER_PROTOCOL_F_CROSS_ENDIAN = 6,
    VHOST_USER_PROTOCOL_F_CRYPTO_SESSION = 7,
    VHOST_USER_PROTOCOL_F_PAGEFAULT = 8,
    VHOST_USER_PROTOCOL_F_CONFIG = 9,
    VHOST_USER_PROTOCOL_F_BACKEND_SEND_FD = 10,
    VHOST_USER_PROTOCOL_F_HOST_NOTIFIER = 11,
    VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD = 12,
    VHOST_USER_PROTOCOL_F_RESET_DEVICE = 13,
    VHOST_USER_PROTOCOL_F_INBAND_NOTIFICATIONS = 14,
    VHOST_USER_PROTOCOL_F_CONFIGURE_MEM_SLOTS = 15,
    VHOST_USER_PROTOCOL_F_STATUS = 16,
    /* Feature 17 reserved for VHOST_USER_PROTOCOL_F_XEN_MMAP. */
    VHOST_USER_PROTOCOL_F_SHARED_OBJECT = 18,
    VHOST_USER_PROTOCOL_F_DEVICE_STATE = 19,
    VHOST_USER_PROTOCOL_F_MAX
};

/**
 * VhostUserHostNotifier - notifier information for one queue
 * @rcu: rcu_head for cleanup
 * @mr: memory region of notifier
 * @addr: current mapped address
 * @unmap_addr: address to be un-mapped
 * @idx: virtioqueue index
 *
 * The VhostUserHostNotifier entries are re-used. When an old mapping
 * is to be released it is moved to @unmap_addr and @addr is replaced.
 * Once the RCU process has completed the unmap @unmap_addr is
 * cleared.
 */
typedef struct VhostUserHostNotifier {
    struct rcu_head rcu;
    MemoryRegion mr;
    void *addr;
    void *unmap_addr;
    int idx;
    bool destroy;
} VhostUserHostNotifier;

/**
 * VhostUserState - shared state for all vhost-user devices
 * @chr: the character backend for the socket
 * @notifiers: GPtrArray of @VhostUserHostnotifier
 * @memory_slots:
 */
typedef struct VhostUserState {
    CharFrontend *chr;
    GPtrArray *notifiers;
    int memory_slots;
    bool supports_config;
} VhostUserState;

/**
 * vhost_user_init() - initialise shared vhost_user state
 * @user: allocated area for storing shared state
 * @chr: the chardev for the vhost socket
 * @errp: error handle
 *
 * User can either directly g_new() space for the state or embed
 * VhostUserState in their larger device structure and just point to
 * it.
 *
 * Return: true on success, false on error while setting errp.
 */
bool vhost_user_init(VhostUserState *user, CharFrontend *chr, Error **errp);

/**
 * vhost_user_cleanup() - cleanup state
 * @user: ptr to use state
 *
 * Cleans up shared state and notifiers, callee is responsible for
 * freeing the @VhostUserState memory itself.
 */
void vhost_user_cleanup(VhostUserState *user);

/**
 * vhost_user_async_close() - cleanup vhost-user post connection drop
 * @d: DeviceState for the associated device (passed to callback)
 * @chardev: the CharFrontend associated with the connection
 * @vhost: the common vhost device
 * @cb: the user callback function to complete the clean-up
 *
 * This function is used to handle the shutdown of a vhost-user
 * connection to a backend. We handle this centrally to make sure we
 * do all the steps and handle potential races due to VM shutdowns.
 * Once the connection is disabled we call a backhalf to ensure
 */
typedef void (*vu_async_close_fn)(DeviceState *cb);

void vhost_user_async_close(DeviceState *d,
                            CharFrontend *chardev, struct vhost_dev *vhost,
                            vu_async_close_fn cb);

#endif
