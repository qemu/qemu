/*
 * vhost-user-stub.c
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "clients.h"
#include "net/vhost_net.h"
#include "net/vhost-user.h"
#include "qemu/error-report.h"
#include "qapi/error.h"

int net_init_vhost_user(const Netdev *netdev, const char *name,
                        NetClientState *peer, Error **errp)
{
    error_setg(errp, "vhost-user requires frontend driver virtio-net-*");
    return -1;
}
