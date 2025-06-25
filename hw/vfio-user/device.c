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

static int vfio_user_get_region_info(VFIOUserProxy *proxy,
                                     struct vfio_region_info *info,
                                     VFIOUserFDs *fds)
{
    g_autofree VFIOUserRegionInfo *msgp = NULL;
    Error *local_err = NULL;
    uint32_t size;

    /* data returned can be larger than vfio_region_info */
    if (info->argsz < sizeof(*info)) {
        error_printf("vfio_user_get_region_info argsz too small\n");
        return -E2BIG;
    }
    if (fds != NULL && fds->send_fds != 0) {
        error_printf("vfio_user_get_region_info can't send FDs\n");
        return -EINVAL;
    }

    size = info->argsz + sizeof(VFIOUserHdr);
    msgp = g_malloc0(size);

    vfio_user_request_msg(&msgp->hdr, VFIO_USER_DEVICE_GET_REGION_INFO,
                          sizeof(*msgp), 0);
    msgp->argsz = info->argsz;
    msgp->index = info->index;

    if (!vfio_user_send_wait(proxy, &msgp->hdr, fds, size, &local_err)) {
        error_prepend(&local_err, "%s: ", __func__);
        error_report_err(local_err);
        return -EFAULT;
    }

    if (msgp->hdr.flags & VFIO_USER_ERROR) {
        return -msgp->hdr.error_reply;
    }
    trace_vfio_user_get_region_info(msgp->index, msgp->flags, msgp->size);

    memcpy(info, &msgp->argsz, info->argsz);
    return 0;
}


static int vfio_user_device_io_get_region_info(VFIODevice *vbasedev,
                                               struct vfio_region_info *info,
                                               int *fd)
{
    VFIOUserFDs fds = { 0, 1, fd};
    int ret;

    if (info->index > vbasedev->num_regions) {
        return -EINVAL;
    }

    ret = vfio_user_get_region_info(vbasedev->proxy, info, &fds);
    if (ret) {
        return ret;
    }

    /* cap_offset in valid area */
    if ((info->flags & VFIO_REGION_INFO_FLAG_CAPS) &&
        (info->cap_offset < sizeof(*info) || info->cap_offset > info->argsz)) {
        return -EINVAL;
    }

    return 0;
}

static int vfio_user_device_io_region_read(VFIODevice *vbasedev, uint8_t index,
                                           off_t off, uint32_t count,
                                           void *data)
{
    g_autofree VFIOUserRegionRW *msgp = NULL;
    VFIOUserProxy *proxy = vbasedev->proxy;
    int size = sizeof(*msgp) + count;
    Error *local_err = NULL;

    if (count > proxy->max_xfer_size) {
        return -EINVAL;
    }

    msgp = g_malloc0(size);
    vfio_user_request_msg(&msgp->hdr, VFIO_USER_REGION_READ, sizeof(*msgp), 0);
    msgp->offset = off;
    msgp->region = index;
    msgp->count = count;
    trace_vfio_user_region_rw(msgp->region, msgp->offset, msgp->count);

    if (!vfio_user_send_wait(proxy, &msgp->hdr, NULL, size, &local_err)) {
        error_prepend(&local_err, "%s: ", __func__);
        error_report_err(local_err);
        return -EFAULT;
    }

    if (msgp->hdr.flags & VFIO_USER_ERROR) {
        return -msgp->hdr.error_reply;
    } else if (msgp->count > count) {
        return -E2BIG;
    } else {
        memcpy(data, &msgp->data, msgp->count);
    }

    return msgp->count;
}

static int vfio_user_device_io_region_write(VFIODevice *vbasedev, uint8_t index,
                                            off_t off, unsigned count,
                                            void *data, bool post)
{
    g_autofree VFIOUserRegionRW *msgp = NULL;
    VFIOUserProxy *proxy = vbasedev->proxy;
    int size = sizeof(*msgp) + count;
    Error *local_err = NULL;
    int ret;

    if (count > proxy->max_xfer_size) {
        return -EINVAL;
    }

    msgp = g_malloc0(size);
    vfio_user_request_msg(&msgp->hdr, VFIO_USER_REGION_WRITE, size, 0);
    msgp->offset = off;
    msgp->region = index;
    msgp->count = count;
    memcpy(&msgp->data, data, count);
    trace_vfio_user_region_rw(msgp->region, msgp->offset, msgp->count);

    /* Ignore post: all writes are synchronous/non-posted. */

    if (!vfio_user_send_wait(proxy, &msgp->hdr, NULL, 0, &local_err)) {
        error_prepend(&local_err, "%s: ", __func__);
        error_report_err(local_err);
        return -EFAULT;
    }

    if (msgp->hdr.flags & VFIO_USER_ERROR) {
        ret = -msgp->hdr.error_reply;
    } else {
        ret = count;
    }

    return ret;
}

/*
 * Socket-based io_ops
 */
VFIODeviceIOOps vfio_user_device_io_ops_sock = {
    .get_region_info = vfio_user_device_io_get_region_info,
    .region_read = vfio_user_device_io_region_read,
    .region_write = vfio_user_device_io_region_write,

};
