/*
 * QEMU vhost-user backend
 *
 * Copyright (C) 2018 Red Hat Inc
 *
 * Authors:
 *  Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_VHOST_USER_BACKEND_H
#define QEMU_VHOST_USER_BACKEND_H

#include "qom/object.h"
#include "system/memory.h"
#include "qemu/option.h"
#include "qemu/bitmap.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"
#include "chardev/char-fe.h"
#include "io/channel.h"

#define TYPE_VHOST_USER_BACKEND "vhost-user-backend"
OBJECT_DECLARE_SIMPLE_TYPE(VhostUserBackend,
                           VHOST_USER_BACKEND)



struct VhostUserBackend {
    /* private */
    Object parent;

    char *chr_name;
    CharFrontend chr;
    VhostUserState vhost_user;
    struct vhost_dev dev;
    VirtIODevice *vdev;
    bool started;
    bool completed;
};

int vhost_user_backend_dev_init(VhostUserBackend *b, VirtIODevice *vdev,
                                unsigned nvqs, Error **errp);
void vhost_user_backend_start(VhostUserBackend *b);
int vhost_user_backend_stop(VhostUserBackend *b);

#endif
