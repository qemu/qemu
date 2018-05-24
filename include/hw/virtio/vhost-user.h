/*
 * Copyright (c) 2017-2018 Intel Corporation
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_VIRTIO_VHOST_USER_H
#define HW_VIRTIO_VHOST_USER_H

#include "chardev/char-fe.h"

typedef struct VhostUserState {
    CharBackend *chr;
} VhostUserState;

VhostUserState *vhost_user_init(void);
void vhost_user_cleanup(VhostUserState *user);

#endif
