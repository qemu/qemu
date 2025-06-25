/*
 * vfio protocol over a UNIX socket device handling.
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

#include "hw/vfio-user/device.h"
#include "hw/vfio-user/trace.h"

/*
 * These are to defend against a malign server trying
 * to force us to run out of memory.
 */
#define VFIO_USER_MAX_REGIONS   100
#define VFIO_USER_MAX_IRQS      50

bool vfio_user_get_device_info(VFIOUserProxy *proxy,
                               struct vfio_device_info *info, Error **errp)
{
    VFIOUserDeviceInfo msg;
    uint32_t argsz = sizeof(msg) - sizeof(msg.hdr);

    memset(&msg, 0, sizeof(msg));
    vfio_user_request_msg(&msg.hdr, VFIO_USER_DEVICE_GET_INFO, sizeof(msg), 0);
    msg.argsz = argsz;

    if (!vfio_user_send_wait(proxy, &msg.hdr, NULL, 0, errp)) {
        return false;
    }

    if (msg.hdr.flags & VFIO_USER_ERROR) {
        error_setg_errno(errp, -msg.hdr.error_reply,
                         "VFIO_USER_DEVICE_GET_INFO failed");
        return false;
    }

    trace_vfio_user_get_info(msg.num_regions, msg.num_irqs);

    memcpy(info, &msg.argsz, argsz);

    /* defend against a malicious server */
    if (info->num_regions > VFIO_USER_MAX_REGIONS ||
        info->num_irqs > VFIO_USER_MAX_IRQS) {
        error_setg_errno(errp, EINVAL, "invalid reply");
        return false;
    }

    return true;
}
