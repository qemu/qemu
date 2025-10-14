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
#include "qemu/lockable.h"
#include "qemu/thread.h"

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

void vfio_user_device_reset(VFIOUserProxy *proxy)
{
    Error *local_err = NULL;
    VFIOUserHdr hdr;

    vfio_user_request_msg(&hdr, VFIO_USER_DEVICE_RESET, sizeof(hdr), 0);

    if (!vfio_user_send_wait(proxy, &hdr, NULL, 0, &local_err)) {
        error_prepend(&local_err, "%s: ", __func__);
        error_report_err(local_err);
        return;
    }

    if (hdr.flags & VFIO_USER_ERROR) {
        error_printf("reset reply error %d\n", hdr.error_reply);
    }
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

    /*
     * If at least one region is directly mapped into the VM, then we can no
     * longer rely on the sequential nature of vfio-user request handling to
     * ensure that posted writes are completed before a subsequent read. In this
     * case, disable posted write support. This is a per-device property, not
     * per-region.
     */
    if (info->flags & VFIO_REGION_INFO_FLAG_MMAP) {
        vfio_user_disable_posted_writes(proxy);
    }

    return 0;
}

static int vfio_user_device_io_get_region_info(VFIODevice *vbasedev,
                                               struct vfio_region_info *info,
                                               int *fd)
{
    VFIOUserFDs fds = { 0, 1, fd};
    int ret;

    if (info->index > vbasedev->num_initial_regions) {
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

static int vfio_user_device_io_get_irq_info(VFIODevice *vbasedev,
                                            struct vfio_irq_info *info)
{
    VFIOUserProxy *proxy = vbasedev->proxy;
    Error *local_err = NULL;
    VFIOUserIRQInfo msg;

    memset(&msg, 0, sizeof(msg));
    vfio_user_request_msg(&msg.hdr, VFIO_USER_DEVICE_GET_IRQ_INFO,
                          sizeof(msg), 0);
    msg.argsz = info->argsz;
    msg.index = info->index;

    if (!vfio_user_send_wait(proxy, &msg.hdr, NULL, 0, &local_err)) {
        error_prepend(&local_err, "%s: ", __func__);
        error_report_err(local_err);
        return -EFAULT;
    }

    if (msg.hdr.flags & VFIO_USER_ERROR) {
        return -msg.hdr.error_reply;
    }
    trace_vfio_user_get_irq_info(msg.index, msg.flags, msg.count);

    memcpy(info, &msg.argsz, sizeof(*info));
    return 0;
}

static int irq_howmany(int *fdp, uint32_t cur, uint32_t max)
{
    int n = 0;

    if (fdp[cur] != -1) {
        do {
            n++;
        } while (n < max && fdp[cur + n] != -1);
    } else {
        do {
            n++;
        } while (n < max && fdp[cur + n] == -1);
    }

    return n;
}

static int vfio_user_device_io_set_irqs(VFIODevice *vbasedev,
                                        struct vfio_irq_set *irq)
{
    VFIOUserProxy *proxy = vbasedev->proxy;
    g_autofree VFIOUserIRQSet *msgp = NULL;
    uint32_t size, nfds, send_fds, sent_fds, max;
    Error *local_err = NULL;

    if (irq->argsz < sizeof(*irq)) {
        error_printf("vfio_user_set_irqs argsz too small\n");
        return -EINVAL;
    }

    /*
     * Handle simple case
     */
    if ((irq->flags & VFIO_IRQ_SET_DATA_EVENTFD) == 0) {
        size = sizeof(VFIOUserHdr) + irq->argsz;
        msgp = g_malloc0(size);

        vfio_user_request_msg(&msgp->hdr, VFIO_USER_DEVICE_SET_IRQS, size, 0);
        msgp->argsz = irq->argsz;
        msgp->flags = irq->flags;
        msgp->index = irq->index;
        msgp->start = irq->start;
        msgp->count = irq->count;
        trace_vfio_user_set_irqs(msgp->index, msgp->start, msgp->count,
                                 msgp->flags);

        if (!vfio_user_send_wait(proxy, &msgp->hdr, NULL, 0, &local_err)) {
            error_prepend(&local_err, "%s: ", __func__);
            error_report_err(local_err);
            return -EFAULT;
        }

        if (msgp->hdr.flags & VFIO_USER_ERROR) {
            return -msgp->hdr.error_reply;
        }

        return 0;
    }

    /*
     * Calculate the number of FDs to send
     * and adjust argsz
     */
    nfds = (irq->argsz - sizeof(*irq)) / sizeof(int);
    irq->argsz = sizeof(*irq);
    msgp = g_malloc0(sizeof(*msgp));
    /*
     * Send in chunks if over max_send_fds
     */
    for (sent_fds = 0; nfds > sent_fds; sent_fds += send_fds) {
        VFIOUserFDs *arg_fds, loop_fds;

        /* must send all valid FDs or all invalid FDs in single msg */
        max = nfds - sent_fds;
        if (max > proxy->max_send_fds) {
            max = proxy->max_send_fds;
        }
        send_fds = irq_howmany((int *)irq->data, sent_fds, max);

        vfio_user_request_msg(&msgp->hdr, VFIO_USER_DEVICE_SET_IRQS,
                              sizeof(*msgp), 0);
        msgp->argsz = irq->argsz;
        msgp->flags = irq->flags;
        msgp->index = irq->index;
        msgp->start = irq->start + sent_fds;
        msgp->count = send_fds;
        trace_vfio_user_set_irqs(msgp->index, msgp->start, msgp->count,
                                 msgp->flags);

        loop_fds.send_fds = send_fds;
        loop_fds.recv_fds = 0;
        loop_fds.fds = (int *)irq->data + sent_fds;
        arg_fds = loop_fds.fds[0] != -1 ? &loop_fds : NULL;

        if (!vfio_user_send_wait(proxy, &msgp->hdr, arg_fds, 0, &local_err)) {
            error_prepend(&local_err, "%s: ", __func__);
            error_report_err(local_err);
            return -EFAULT;
        }

        if (msgp->hdr.flags & VFIO_USER_ERROR) {
            return -msgp->hdr.error_reply;
        }
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

/*
 * If this is a posted write, and VFIO_PROXY_NO_POST is not set, then we are OK
 * to send the write to the socket without waiting for the server's reply:
 * a subsequent read (of any region) will not pass the posted write, as all
 * messages are handled sequentially.
 */
static int vfio_user_device_io_region_write(VFIODevice *vbasedev, uint8_t index,
                                            off_t off, unsigned count,
                                            void *data, bool post)
{
    VFIOUserRegionRW *msgp = NULL;
    VFIOUserProxy *proxy = vbasedev->proxy;
    int size = sizeof(*msgp) + count;
    Error *local_err = NULL;
    bool can_multi;
    int flags = 0;
    int ret;

    if (count > proxy->max_xfer_size) {
        return -EINVAL;
    }

    if (proxy->flags & VFIO_PROXY_NO_POST) {
        post = false;
    }

    if (post) {
        flags |= VFIO_USER_NO_REPLY;
    }

    /* write eligible to be in a WRITE_MULTI msg ? */
    can_multi = (proxy->flags & VFIO_PROXY_USE_MULTI) && post &&
        count <= VFIO_USER_MULTI_DATA;

    /*
     * This should be a rare case, so first check without the lock,
     * if we're wrong, vfio_send_queued() will flush any posted writes
     * we missed here
     */
    if (proxy->wr_multi != NULL ||
        (proxy->num_outgoing > VFIO_USER_OUT_HIGH && can_multi)) {

        /*
         * re-check with lock
         *
         * if already building a WRITE_MULTI msg,
         *  add this one if possible else flush pending before
         *  sending the current one
         *
         * else if outgoing queue is over the highwater,
         *  start a new WRITE_MULTI message
         */
        WITH_QEMU_LOCK_GUARD(&proxy->lock) {
            if (proxy->wr_multi != NULL) {
                if (can_multi) {
                    vfio_user_add_multi(proxy, index, off, count, data);
                    return count;
                }
                vfio_user_flush_multi(proxy);
            } else if (proxy->num_outgoing > VFIO_USER_OUT_HIGH && can_multi) {
                vfio_user_create_multi(proxy);
                vfio_user_add_multi(proxy, index, off, count, data);
                return count;
            }
        }
    }

    msgp = g_malloc0(size);
    vfio_user_request_msg(&msgp->hdr, VFIO_USER_REGION_WRITE, size, flags);
    msgp->offset = off;
    msgp->region = index;
    msgp->count = count;
    memcpy(&msgp->data, data, count);
    trace_vfio_user_region_rw(msgp->region, msgp->offset, msgp->count);

    /* async send will free msg after it's sent */
    if (post) {
        if (!vfio_user_send_async(proxy, &msgp->hdr, NULL, &local_err)) {
            error_prepend(&local_err, "%s: ", __func__);
            error_report_err(local_err);
            return -EFAULT;
        }

        return count;
    }

    if (!vfio_user_send_wait(proxy, &msgp->hdr, NULL, 0, &local_err)) {
        error_prepend(&local_err, "%s: ", __func__);
        error_report_err(local_err);
        g_free(msgp);
        return -EFAULT;
    }

    if (msgp->hdr.flags & VFIO_USER_ERROR) {
        ret = -msgp->hdr.error_reply;
    } else {
        ret = count;
    }

    g_free(msgp);
    return ret;
}

/*
 * Socket-based io_ops
 */
VFIODeviceIOOps vfio_user_device_io_ops_sock = {
    .get_region_info = vfio_user_device_io_get_region_info,
    .get_irq_info = vfio_user_device_io_get_irq_info,
    .set_irqs = vfio_user_device_io_set_irqs,
    .region_read = vfio_user_device_io_region_read,
    .region_write = vfio_user_device_io_region_write,

};
