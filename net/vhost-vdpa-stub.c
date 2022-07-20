/*
 * vhost-vdpa-stub.c
 *
 * Copyright (c) 2022 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "clients.h"
#include "net/vhost-vdpa.h"
#include "qapi/error.h"

int net_init_vhost_vdpa(const Netdev *netdev, const char *name,
                        NetClientState *peer, Error **errp)
{
    error_setg(errp, "vhost-vdpa requires frontend driver virtio-net-*");
    return -1;
}
