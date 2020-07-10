/*
 * vhost-vdpa
 *
 *  Copyright(c) 2017-2018 Intel Corporation.
 *  Copyright(c) 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include <linux/vhost.h>
#include <linux/vfio.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-backend.h"
#include "hw/virtio/virtio-net.h"
#include "hw/virtio/vhost-vdpa.h"
#include "qemu/main-loop.h"
#include "cpu.h"

static bool vhost_vdpa_listener_skipped_section(MemoryRegionSection *section)
{
    return (!memory_region_is_ram(section->mr) &&
            !memory_region_is_iommu(section->mr)) ||
           /*
            * Sizing an enabled 64-bit BAR can cause spurious mappings to
            * addresses in the upper part of the 64-bit address space.  These
            * are never accessed by the CPU and beyond the address width of
            * some IOMMU hardware.  TODO: VDPA should tell us the IOMMU width.
            */
           section->offset_within_address_space & (1ULL << 63);
}

static int vhost_vdpa_dma_map(struct vhost_vdpa *v, hwaddr iova, hwaddr size,
                              void *vaddr, bool readonly)
{
    struct vhost_msg_v2 msg = {};
    int fd = v->device_fd;
    int ret = 0;

    msg.type = v->msg_type;
    msg.iotlb.iova = iova;
    msg.iotlb.size = size;
    msg.iotlb.uaddr = (uint64_t)(uintptr_t)vaddr;
    msg.iotlb.perm = readonly ? VHOST_ACCESS_RO : VHOST_ACCESS_RW;
    msg.iotlb.type = VHOST_IOTLB_UPDATE;

    if (write(fd, &msg, sizeof(msg)) != sizeof(msg)) {
        error_report("failed to write, fd=%d, errno=%d (%s)",
            fd, errno, strerror(errno));
        return -EIO ;
    }

    return ret;
}

static int vhost_vdpa_dma_unmap(struct vhost_vdpa *v, hwaddr iova,
                                hwaddr size)
{
    struct vhost_msg_v2 msg = {};
    int fd = v->device_fd;
    int ret = 0;

    msg.type = v->msg_type;
    msg.iotlb.iova = iova;
    msg.iotlb.size = size;
    msg.iotlb.type = VHOST_IOTLB_INVALIDATE;

    if (write(fd, &msg, sizeof(msg)) != sizeof(msg)) {
        error_report("failed to write, fd=%d, errno=%d (%s)",
            fd, errno, strerror(errno));
        return -EIO ;
    }

    return ret;
}

static void vhost_vdpa_listener_region_add(MemoryListener *listener,
                                           MemoryRegionSection *section)
{
    struct vhost_vdpa *v = container_of(listener, struct vhost_vdpa, listener);
    hwaddr iova;
    Int128 llend, llsize;
    void *vaddr;
    int ret;

    if (vhost_vdpa_listener_skipped_section(section)) {
        return;
    }

    if (unlikely((section->offset_within_address_space & ~TARGET_PAGE_MASK) !=
                 (section->offset_within_region & ~TARGET_PAGE_MASK))) {
        error_report("%s received unaligned region", __func__);
        return;
    }

    iova = TARGET_PAGE_ALIGN(section->offset_within_address_space);
    llend = int128_make64(section->offset_within_address_space);
    llend = int128_add(llend, section->size);
    llend = int128_and(llend, int128_exts64(TARGET_PAGE_MASK));

    if (int128_ge(int128_make64(iova), llend)) {
        return;
    }

    memory_region_ref(section->mr);

    /* Here we assume that memory_region_is_ram(section->mr)==true */

    vaddr = memory_region_get_ram_ptr(section->mr) +
            section->offset_within_region +
            (iova - section->offset_within_address_space);

    llsize = int128_sub(llend, int128_make64(iova));

    ret = vhost_vdpa_dma_map(v, iova, int128_get64(llsize),
                             vaddr, section->readonly);
    if (ret) {
        error_report("vhost vdpa map fail!");
        if (memory_region_is_ram_device(section->mr)) {
            /* Allow unexpected mappings not to be fatal for RAM devices */
            error_report("map ram fail!");
          return ;
        }
        goto fail;
    }

    return;

fail:
    if (memory_region_is_ram_device(section->mr)) {
        error_report("failed to vdpa_dma_map. pci p2p may not work");
        return;

    }
    /*
     * On the initfn path, store the first error in the container so we
     * can gracefully fail.  Runtime, there's not much we can do other
     * than throw a hardware error.
     */
    error_report("vhost-vdpa: DMA mapping failed, unable to continue");
    return;

}

static void vhost_vdpa_listener_region_del(MemoryListener *listener,
                                           MemoryRegionSection *section)
{
    struct vhost_vdpa *v = container_of(listener, struct vhost_vdpa, listener);
    hwaddr iova;
    Int128 llend, llsize;
    int ret;
    bool try_unmap = true;

    if (vhost_vdpa_listener_skipped_section(section)) {
        return;
    }

    if (unlikely((section->offset_within_address_space & ~TARGET_PAGE_MASK) !=
                 (section->offset_within_region & ~TARGET_PAGE_MASK))) {
        error_report("%s received unaligned region", __func__);
        return;
    }

    iova = TARGET_PAGE_ALIGN(section->offset_within_address_space);
    llend = int128_make64(section->offset_within_address_space);
    llend = int128_add(llend, section->size);
    llend = int128_and(llend, int128_exts64(TARGET_PAGE_MASK));

    if (int128_ge(int128_make64(iova), llend)) {
        return;
    }

    llsize = int128_sub(llend, int128_make64(iova));

    if (try_unmap) {
        ret = vhost_vdpa_dma_unmap(v, iova, int128_get64(llsize));
        if (ret) {
            error_report("vhost_vdpa dma unmap error!");
        }
    }

    memory_region_unref(section->mr);
}
/*
 * IOTLB API is used by vhost-vpda which requires incremental updating
 * of the mapping. So we can not use generic vhost memory listener which
 * depends on the addnop().
 */
static const MemoryListener vhost_vdpa_memory_listener = {
    .region_add = vhost_vdpa_listener_region_add,
    .region_del = vhost_vdpa_listener_region_del,
};

static int vhost_vdpa_call(struct vhost_dev *dev, unsigned long int request,
                             void *arg)
{
    struct vhost_vdpa *v = dev->opaque;
    int fd = v->device_fd;

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_VDPA);

    return ioctl(fd, request, arg);
}

static void vhost_vdpa_add_status(struct vhost_dev *dev, uint8_t status)
{
    uint8_t s;

    if (vhost_vdpa_call(dev, VHOST_VDPA_GET_STATUS, &s)) {
        return;
    }

    s |= status;

    vhost_vdpa_call(dev, VHOST_VDPA_SET_STATUS, &s);
}

static int vhost_vdpa_init(struct vhost_dev *dev, void *opaque)
{
    struct vhost_vdpa *v;
    uint64_t features;
    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_VDPA);

    v = opaque;
    dev->opaque =  opaque ;
    vhost_vdpa_call(dev, VHOST_GET_FEATURES, &features);
    dev->backend_features = features;
    v->listener = vhost_vdpa_memory_listener;
    v->msg_type = VHOST_IOTLB_MSG_V2;

    vhost_vdpa_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE |
                               VIRTIO_CONFIG_S_DRIVER);

    return 0;
}

static int vhost_vdpa_cleanup(struct vhost_dev *dev)
{
    struct vhost_vdpa *v;
    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_VDPA);
    v = dev->opaque;
    memory_listener_unregister(&v->listener);

    dev->opaque = NULL;
    return 0;
}

static int vhost_vdpa_memslots_limit(struct vhost_dev *dev)
{
    return INT_MAX;
}

static int vhost_vdpa_set_mem_table(struct vhost_dev *dev,
                                    struct vhost_memory *mem)
{

    if (mem->padding) {
        return -1;
    }

    return 0;
}

static int vhost_vdpa_set_features(struct vhost_dev *dev,
                                   uint64_t features)
{
    int ret;
    ret = vhost_vdpa_call(dev, VHOST_SET_FEATURES, &features);
    uint8_t status = 0;
    if (ret) {
        return ret;
    }
    vhost_vdpa_add_status(dev, VIRTIO_CONFIG_S_FEATURES_OK);
    vhost_vdpa_call(dev, VHOST_VDPA_GET_STATUS, &status);

    return !(status & VIRTIO_CONFIG_S_FEATURES_OK);
}

int vhost_vdpa_get_device_id(struct vhost_dev *dev,
                                   uint32_t *device_id)
{
    return vhost_vdpa_call(dev, VHOST_VDPA_GET_DEVICE_ID, device_id);
}

static int vhost_vdpa_reset_device(struct vhost_dev *dev)
{
    uint8_t status = 0;

    return vhost_vdpa_call(dev, VHOST_VDPA_SET_STATUS, &status);
}

static int vhost_vdpa_get_vq_index(struct vhost_dev *dev, int idx)
{
    assert(idx >= dev->vq_index && idx < dev->vq_index + dev->nvqs);

    return idx - dev->vq_index;
}

static int vhost_vdpa_set_vring_ready(struct vhost_dev *dev)
{
    int i;
    for (i = 0; i < dev->nvqs; ++i) {
        struct vhost_vring_state state = {
            .index = dev->vq_index + i,
            .num = 1,
        };
        vhost_vdpa_call(dev, VHOST_VDPA_SET_VRING_ENABLE, &state);
    }
    return 0;
}

static int vhost_vdpa_set_config(struct vhost_dev *dev, const uint8_t *data,
                                   uint32_t offset, uint32_t size,
                                   uint32_t flags)
{
    struct vhost_vdpa_config *config;
    int ret;
    unsigned long config_size = offsetof(struct vhost_vdpa_config, buf);
    config = g_malloc(size + config_size);
    if (config == NULL) {
        return -1;
    }
    config->off = offset;
    config->len = size;
    memcpy(config->buf, data, size);
    ret = vhost_vdpa_call(dev, VHOST_VDPA_SET_CONFIG, config);
    g_free(config);
    return ret;
}

static int vhost_vdpa_get_config(struct vhost_dev *dev, uint8_t *config,
                                   uint32_t config_len)
{
    struct vhost_vdpa_config *v_config;
    unsigned long config_size = offsetof(struct vhost_vdpa_config, buf);
    int ret;

    v_config = g_malloc(config_len + config_size);
    if (v_config == NULL) {
        return -1;
    }
    v_config->len = config_len;
    v_config->off = 0;
    ret = vhost_vdpa_call(dev, VHOST_VDPA_GET_CONFIG, v_config);
    memcpy(config, v_config->buf, config_len);
    g_free(v_config);
    return ret;
 }

static int vhost_vdpa_dev_start(struct vhost_dev *dev, bool started)
{
    struct vhost_vdpa *v = dev->opaque;
    if (started) {
        uint8_t status = 0;
        memory_listener_register(&v->listener, &address_space_memory);
        vhost_vdpa_set_vring_ready(dev);
        vhost_vdpa_add_status(dev, VIRTIO_CONFIG_S_DRIVER_OK);
        vhost_vdpa_call(dev, VHOST_VDPA_GET_STATUS, &status);

        return !(status & VIRTIO_CONFIG_S_DRIVER_OK);
    } else {
        vhost_vdpa_reset_device(dev);
        vhost_vdpa_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE |
                                   VIRTIO_CONFIG_S_DRIVER);
        memory_listener_unregister(&v->listener);

        return 0;
    }
}

static int vhost_vdpa_set_log_base(struct vhost_dev *dev, uint64_t base,
                                     struct vhost_log *log)
{
    return vhost_vdpa_call(dev, VHOST_SET_LOG_BASE, &base);
}

static int vhost_vdpa_set_vring_addr(struct vhost_dev *dev,
                                       struct vhost_vring_addr *addr)
{
    return vhost_vdpa_call(dev, VHOST_SET_VRING_ADDR, addr);
}

static int vhost_vdpa_set_vring_num(struct vhost_dev *dev,
                                      struct vhost_vring_state *ring)
{
    return vhost_vdpa_call(dev, VHOST_SET_VRING_NUM, ring);
}

static int vhost_vdpa_set_vring_base(struct vhost_dev *dev,
                                       struct vhost_vring_state *ring)
{
    return vhost_vdpa_call(dev, VHOST_SET_VRING_BASE, ring);
}

static int vhost_vdpa_get_vring_base(struct vhost_dev *dev,
                                       struct vhost_vring_state *ring)
{
    return vhost_vdpa_call(dev, VHOST_GET_VRING_BASE, ring);
}

static int vhost_vdpa_set_vring_kick(struct vhost_dev *dev,
                                       struct vhost_vring_file *file)
{
    return vhost_vdpa_call(dev, VHOST_SET_VRING_KICK, file);
}

static int vhost_vdpa_set_vring_call(struct vhost_dev *dev,
                                       struct vhost_vring_file *file)
{
    return vhost_vdpa_call(dev, VHOST_SET_VRING_CALL, file);
}

static int vhost_vdpa_get_features(struct vhost_dev *dev,
                                     uint64_t *features)
{
    return vhost_vdpa_call(dev, VHOST_GET_FEATURES, features);
}

static int vhost_vdpa_set_owner(struct vhost_dev *dev)
{
    return vhost_vdpa_call(dev, VHOST_SET_OWNER, NULL);
}

static int vhost_vdpa_vq_get_addr(struct vhost_dev *dev,
                    struct vhost_vring_addr *addr, struct vhost_virtqueue *vq)
{
    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_VDPA);
    addr->desc_user_addr = (uint64_t)(unsigned long)vq->desc_phys;
    addr->avail_user_addr = (uint64_t)(unsigned long)vq->avail_phys;
    addr->used_user_addr = (uint64_t)(unsigned long)vq->used_phys;
    return 0;
}

static bool  vhost_vdpa_force_iommu(struct vhost_dev *dev)
{
    return true;
}

const VhostOps vdpa_ops = {
        .backend_type = VHOST_BACKEND_TYPE_VDPA,
        .vhost_backend_init = vhost_vdpa_init,
        .vhost_backend_cleanup = vhost_vdpa_cleanup,
        .vhost_set_log_base = vhost_vdpa_set_log_base,
        .vhost_set_vring_addr = vhost_vdpa_set_vring_addr,
        .vhost_set_vring_num = vhost_vdpa_set_vring_num,
        .vhost_set_vring_base = vhost_vdpa_set_vring_base,
        .vhost_get_vring_base = vhost_vdpa_get_vring_base,
        .vhost_set_vring_kick = vhost_vdpa_set_vring_kick,
        .vhost_set_vring_call = vhost_vdpa_set_vring_call,
        .vhost_get_features = vhost_vdpa_get_features,
        .vhost_set_owner = vhost_vdpa_set_owner,
        .vhost_set_vring_endian = NULL,
        .vhost_backend_memslots_limit = vhost_vdpa_memslots_limit,
        .vhost_set_mem_table = vhost_vdpa_set_mem_table,
        .vhost_set_features = vhost_vdpa_set_features,
        .vhost_reset_device = vhost_vdpa_reset_device,
        .vhost_get_vq_index = vhost_vdpa_get_vq_index,
        .vhost_get_config  = vhost_vdpa_get_config,
        .vhost_set_config = vhost_vdpa_set_config,
        .vhost_requires_shm_log = NULL,
        .vhost_migration_done = NULL,
        .vhost_backend_can_merge = NULL,
        .vhost_net_set_mtu = NULL,
        .vhost_set_iotlb_callback = NULL,
        .vhost_send_device_iotlb_msg = NULL,
        .vhost_dev_start = vhost_vdpa_dev_start,
        .vhost_get_device_id = vhost_vdpa_get_device_id,
         .vhost_vq_get_addr = vhost_vdpa_vq_get_addr,
         .vhost_force_iommu = vhost_vdpa_force_iommu,
};
