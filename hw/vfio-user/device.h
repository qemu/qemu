#ifndef VFIO_USER_DEVICE_H
#define VFIO_USER_DEVICE_H

/*
 * vfio protocol over a UNIX socket device handling.
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "linux/vfio.h"

#include "hw/vfio-user/proxy.h"

bool vfio_user_get_device_info(VFIOUserProxy *proxy,
                               struct vfio_device_info *info, Error **errp);

void vfio_user_device_reset(VFIOUserProxy *proxy);

extern VFIODeviceIOOps vfio_user_device_io_ops_sock;

#endif /* VFIO_USER_DEVICE_H */
