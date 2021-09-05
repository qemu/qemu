/*
 * vhost-backend
 *
 * Copyright (c) 2013 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-backend.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "standard-headers/linux/vhost_types.h"

#include "hw/virtio/vhost-vdpa.h"
#ifdef CONFIG_VHOST_KERNEL
#include <linux/vhost.h>
#include <sys/ioctl.h>

static int vhost_kernel_call(struct vhost_dev *dev, unsigned long int request,
                             void *arg)
{
    int fd = (uintptr_t) dev->opaque;
    int ret;

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_KERNEL);

    ret = ioctl(fd, request, arg);
    return ret < 0 ? -errno : ret;
}

static int vhost_kernel_init(struct vhost_dev *dev, void *opaque, Error **errp)
{
    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_KERNEL);

    dev->opaque = opaque;

    return 0;
}

static int vhost_kernel_cleanup(struct vhost_dev *dev)
{
    int fd = (uintptr_t) dev->opaque;

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_KERNEL);

    return close(fd);
}

static int vhost_kernel_memslots_limit(struct vhost_dev *dev)
{
    int limit = 64;
    char *s;

    if (g_file_get_contents("/sys/module/vhost/parameters/max_mem_regions",
                            &s, NULL, NULL)) {
        uint64_t val = g_ascii_strtoull(s, NULL, 10);
        if (!((val == G_MAXUINT64 || !val) && errno)) {
            g_free(s);
            return val;
        }
        error_report("ignoring invalid max_mem_regions value in vhost module:"
                     " %s", s);
    }
    g_free(s);
    return limit;
}

static int vhost_kernel_net_set_backend(struct vhost_dev *dev,
                                        struct vhost_vring_file *file)
{
    return vhost_kernel_call(dev, VHOST_NET_SET_BACKEND, file);
}

static int vhost_kernel_scsi_set_endpoint(struct vhost_dev *dev,
                                          struct vhost_scsi_target *target)
{
    return vhost_kernel_call(dev, VHOST_SCSI_SET_ENDPOINT, target);
}

static int vhost_kernel_scsi_clear_endpoint(struct vhost_dev *dev,
                                            struct vhost_scsi_target *target)
{
    return vhost_kernel_call(dev, VHOST_SCSI_CLEAR_ENDPOINT, target);
}

static int vhost_kernel_scsi_get_abi_version(struct vhost_dev *dev, int *version)
{
    return vhost_kernel_call(dev, VHOST_SCSI_GET_ABI_VERSION, version);
}

static int vhost_kernel_set_log_base(struct vhost_dev *dev, uint64_t base,
                                     struct vhost_log *log)
{
    return vhost_kernel_call(dev, VHOST_SET_LOG_BASE, &base);
}

static int vhost_kernel_set_mem_table(struct vhost_dev *dev,
                                      struct vhost_memory *mem)
{
    return vhost_kernel_call(dev, VHOST_SET_MEM_TABLE, mem);
}

static int vhost_kernel_set_vring_addr(struct vhost_dev *dev,
                                       struct vhost_vring_addr *addr)
{
    return vhost_kernel_call(dev, VHOST_SET_VRING_ADDR, addr);
}

static int vhost_kernel_set_vring_endian(struct vhost_dev *dev,
                                         struct vhost_vring_state *ring)
{
    return vhost_kernel_call(dev, VHOST_SET_VRING_ENDIAN, ring);
}

static int vhost_kernel_set_vring_num(struct vhost_dev *dev,
                                      struct vhost_vring_state *ring)
{
    return vhost_kernel_call(dev, VHOST_SET_VRING_NUM, ring);
}

static int vhost_kernel_set_vring_base(struct vhost_dev *dev,
                                       struct vhost_vring_state *ring)
{
    return vhost_kernel_call(dev, VHOST_SET_VRING_BASE, ring);
}

static int vhost_kernel_get_vring_base(struct vhost_dev *dev,
                                       struct vhost_vring_state *ring)
{
    return vhost_kernel_call(dev, VHOST_GET_VRING_BASE, ring);
}

static int vhost_kernel_set_vring_kick(struct vhost_dev *dev,
                                       struct vhost_vring_file *file)
{
    return vhost_kernel_call(dev, VHOST_SET_VRING_KICK, file);
}

static int vhost_kernel_set_vring_call(struct vhost_dev *dev,
                                       struct vhost_vring_file *file)
{
    return vhost_kernel_call(dev, VHOST_SET_VRING_CALL, file);
}

static int vhost_kernel_set_vring_busyloop_timeout(struct vhost_dev *dev,
                                                   struct vhost_vring_state *s)
{
    return vhost_kernel_call(dev, VHOST_SET_VRING_BUSYLOOP_TIMEOUT, s);
}

static int vhost_kernel_set_features(struct vhost_dev *dev,
                                     uint64_t features)
{
    return vhost_kernel_call(dev, VHOST_SET_FEATURES, &features);
}

static int vhost_kernel_set_backend_cap(struct vhost_dev *dev)
{
    uint64_t features;
    uint64_t f = 0x1ULL << VHOST_BACKEND_F_IOTLB_MSG_V2;
    int r;

    if (vhost_kernel_call(dev, VHOST_GET_BACKEND_FEATURES, &features)) {
        return 0;
    }

    features &= f;
    r = vhost_kernel_call(dev, VHOST_SET_BACKEND_FEATURES,
                              &features);
    if (r) {
        return 0;
    }

    dev->backend_cap = features;

    return 0;
}

static int vhost_kernel_get_features(struct vhost_dev *dev,
                                     uint64_t *features)
{
    return vhost_kernel_call(dev, VHOST_GET_FEATURES, features);
}

static int vhost_kernel_set_owner(struct vhost_dev *dev)
{
    return vhost_kernel_call(dev, VHOST_SET_OWNER, NULL);
}

static int vhost_kernel_reset_device(struct vhost_dev *dev)
{
    return vhost_kernel_call(dev, VHOST_RESET_OWNER, NULL);
}

static int vhost_kernel_get_vq_index(struct vhost_dev *dev, int idx)
{
    assert(idx >= dev->vq_index && idx < dev->vq_index + dev->nvqs);

    return idx - dev->vq_index;
}

#ifdef CONFIG_VHOST_VSOCK
static int vhost_kernel_vsock_set_guest_cid(struct vhost_dev *dev,
                                            uint64_t guest_cid)
{
    return vhost_kernel_call(dev, VHOST_VSOCK_SET_GUEST_CID, &guest_cid);
}

static int vhost_kernel_vsock_set_running(struct vhost_dev *dev, int start)
{
    return vhost_kernel_call(dev, VHOST_VSOCK_SET_RUNNING, &start);
}
#endif /* CONFIG_VHOST_VSOCK */

static void vhost_kernel_iotlb_read(void *opaque)
{
    struct vhost_dev *dev = opaque;
    ssize_t len;

    if (dev->backend_cap &
        (0x1ULL << VHOST_BACKEND_F_IOTLB_MSG_V2)) {
        struct vhost_msg_v2 msg;

        while ((len = read((uintptr_t)dev->opaque, &msg, sizeof msg)) > 0) {
            if (len < sizeof msg) {
                error_report("Wrong vhost message len: %d", (int)len);
                break;
            }
            if (msg.type != VHOST_IOTLB_MSG_V2) {
                error_report("Unknown vhost iotlb message type");
                break;
            }

            vhost_backend_handle_iotlb_msg(dev, &msg.iotlb);
        }
    } else {
        struct vhost_msg msg;

        while ((len = read((uintptr_t)dev->opaque, &msg, sizeof msg)) > 0) {
            if (len < sizeof msg) {
                error_report("Wrong vhost message len: %d", (int)len);
                break;
            }
            if (msg.type != VHOST_IOTLB_MSG) {
                error_report("Unknown vhost iotlb message type");
                break;
            }

            vhost_backend_handle_iotlb_msg(dev, &msg.iotlb);
        }
    }
}

static int vhost_kernel_send_device_iotlb_msg(struct vhost_dev *dev,
                                              struct vhost_iotlb_msg *imsg)
{
    if (dev->backend_cap & (1ULL << VHOST_BACKEND_F_IOTLB_MSG_V2)) {
        struct vhost_msg_v2 msg = {};

        msg.type = VHOST_IOTLB_MSG_V2;
        msg.iotlb = *imsg;

        if (write((uintptr_t)dev->opaque, &msg, sizeof msg) != sizeof msg) {
            error_report("Fail to update device iotlb");
            return -EFAULT;
        }
    } else {
        struct vhost_msg msg = {};

        msg.type = VHOST_IOTLB_MSG;
        msg.iotlb = *imsg;

        if (write((uintptr_t)dev->opaque, &msg, sizeof msg) != sizeof msg) {
            error_report("Fail to update device iotlb");
            return -EFAULT;
        }
    }

    return 0;
}

static void vhost_kernel_set_iotlb_callback(struct vhost_dev *dev,
                                           int enabled)
{
    if (enabled)
        qemu_set_fd_handler((uintptr_t)dev->opaque,
                            vhost_kernel_iotlb_read, NULL, dev);
    else
        qemu_set_fd_handler((uintptr_t)dev->opaque, NULL, NULL, NULL);
}

const VhostOps kernel_ops = {
        .backend_type = VHOST_BACKEND_TYPE_KERNEL,
        .vhost_backend_init = vhost_kernel_init,
        .vhost_backend_cleanup = vhost_kernel_cleanup,
        .vhost_backend_memslots_limit = vhost_kernel_memslots_limit,
        .vhost_net_set_backend = vhost_kernel_net_set_backend,
        .vhost_scsi_set_endpoint = vhost_kernel_scsi_set_endpoint,
        .vhost_scsi_clear_endpoint = vhost_kernel_scsi_clear_endpoint,
        .vhost_scsi_get_abi_version = vhost_kernel_scsi_get_abi_version,
        .vhost_set_log_base = vhost_kernel_set_log_base,
        .vhost_set_mem_table = vhost_kernel_set_mem_table,
        .vhost_set_vring_addr = vhost_kernel_set_vring_addr,
        .vhost_set_vring_endian = vhost_kernel_set_vring_endian,
        .vhost_set_vring_num = vhost_kernel_set_vring_num,
        .vhost_set_vring_base = vhost_kernel_set_vring_base,
        .vhost_get_vring_base = vhost_kernel_get_vring_base,
        .vhost_set_vring_kick = vhost_kernel_set_vring_kick,
        .vhost_set_vring_call = vhost_kernel_set_vring_call,
        .vhost_set_vring_busyloop_timeout =
                                vhost_kernel_set_vring_busyloop_timeout,
        .vhost_set_features = vhost_kernel_set_features,
        .vhost_get_features = vhost_kernel_get_features,
        .vhost_set_backend_cap = vhost_kernel_set_backend_cap,
        .vhost_set_owner = vhost_kernel_set_owner,
        .vhost_reset_device = vhost_kernel_reset_device,
        .vhost_get_vq_index = vhost_kernel_get_vq_index,
#ifdef CONFIG_VHOST_VSOCK
        .vhost_vsock_set_guest_cid = vhost_kernel_vsock_set_guest_cid,
        .vhost_vsock_set_running = vhost_kernel_vsock_set_running,
#endif /* CONFIG_VHOST_VSOCK */
        .vhost_set_iotlb_callback = vhost_kernel_set_iotlb_callback,
        .vhost_send_device_iotlb_msg = vhost_kernel_send_device_iotlb_msg,
};
#endif

int vhost_backend_update_device_iotlb(struct vhost_dev *dev,
                                             uint64_t iova, uint64_t uaddr,
                                             uint64_t len,
                                             IOMMUAccessFlags perm)
{
    struct vhost_iotlb_msg imsg;

    imsg.iova =  iova;
    imsg.uaddr = uaddr;
    imsg.size = len;
    imsg.type = VHOST_IOTLB_UPDATE;

    switch (perm) {
    case IOMMU_RO:
        imsg.perm = VHOST_ACCESS_RO;
        break;
    case IOMMU_WO:
        imsg.perm = VHOST_ACCESS_WO;
        break;
    case IOMMU_RW:
        imsg.perm = VHOST_ACCESS_RW;
        break;
    default:
        return -EINVAL;
    }

    if (dev->vhost_ops && dev->vhost_ops->vhost_send_device_iotlb_msg)
        return dev->vhost_ops->vhost_send_device_iotlb_msg(dev, &imsg);

    return -ENODEV;
}

int vhost_backend_invalidate_device_iotlb(struct vhost_dev *dev,
                                                 uint64_t iova, uint64_t len)
{
    struct vhost_iotlb_msg imsg;

    imsg.iova = iova;
    imsg.size = len;
    imsg.type = VHOST_IOTLB_INVALIDATE;

    if (dev->vhost_ops && dev->vhost_ops->vhost_send_device_iotlb_msg)
        return dev->vhost_ops->vhost_send_device_iotlb_msg(dev, &imsg);

    return -ENODEV;
}

int vhost_backend_handle_iotlb_msg(struct vhost_dev *dev,
                                          struct vhost_iotlb_msg *imsg)
{
    int ret = 0;

    if (unlikely(!dev->vdev)) {
        error_report("Unexpected IOTLB message when virtio device is stopped");
        return -EINVAL;
    }

    switch (imsg->type) {
    case VHOST_IOTLB_MISS:
        ret = vhost_device_iotlb_miss(dev, imsg->iova,
                                      imsg->perm != VHOST_ACCESS_RO);
        break;
    case VHOST_IOTLB_ACCESS_FAIL:
        /* FIXME: report device iotlb error */
        error_report("Access failure IOTLB message type not supported");
        ret = -ENOTSUP;
        break;
    case VHOST_IOTLB_UPDATE:
    case VHOST_IOTLB_INVALIDATE:
    default:
        error_report("Unexpected IOTLB message type");
        ret = -EINVAL;
        break;
    }

    return ret;
}
