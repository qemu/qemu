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
#include "linux/vhost.h"

#include <sys/ioctl.h>

static int vhost_kernel_call(struct vhost_dev *dev, unsigned long int request,
                             void *arg)
{
    int fd = (uintptr_t) dev->opaque;

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_KERNEL);

    return ioctl(fd, request, arg);
}

static int vhost_kernel_init(struct vhost_dev *dev, void *opaque)
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
            return val;
        }
        error_report("ignoring invalid max_mem_regions value in vhost module:"
                     " %s", s);
    }
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

static int vhost_kernel_set_features(struct vhost_dev *dev,
                                     uint64_t features)
{
    return vhost_kernel_call(dev, VHOST_SET_FEATURES, &features);
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

static const VhostOps kernel_ops = {
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
        .vhost_set_features = vhost_kernel_set_features,
        .vhost_get_features = vhost_kernel_get_features,
        .vhost_set_owner = vhost_kernel_set_owner,
        .vhost_reset_device = vhost_kernel_reset_device,
        .vhost_get_vq_index = vhost_kernel_get_vq_index,
};

int vhost_set_backend_type(struct vhost_dev *dev, VhostBackendType backend_type)
{
    int r = 0;

    switch (backend_type) {
    case VHOST_BACKEND_TYPE_KERNEL:
        dev->vhost_ops = &kernel_ops;
        break;
    case VHOST_BACKEND_TYPE_USER:
        dev->vhost_ops = &user_ops;
        break;
    default:
        error_report("Unknown vhost backend type");
        r = -1;
    }

    return r;
}
