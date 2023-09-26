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
#include "exec/target_page.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-backend.h"
#include "hw/virtio/virtio-net.h"
#include "hw/virtio/vhost-shadow-virtqueue.h"
#include "hw/virtio/vhost-vdpa.h"
#include "exec/address-spaces.h"
#include "migration/blocker.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"
#include "trace.h"
#include "qapi/error.h"

/*
 * Return one past the end of the end of section. Be careful with uint64_t
 * conversions!
 */
static Int128 vhost_vdpa_section_end(const MemoryRegionSection *section,
                                     int page_mask)
{
    Int128 llend = int128_make64(section->offset_within_address_space);
    llend = int128_add(llend, section->size);
    llend = int128_and(llend, int128_exts64(page_mask));

    return llend;
}

static bool vhost_vdpa_listener_skipped_section(MemoryRegionSection *section,
                                                uint64_t iova_min,
                                                uint64_t iova_max,
                                                int page_mask)
{
    Int128 llend;

    if ((!memory_region_is_ram(section->mr) &&
         !memory_region_is_iommu(section->mr)) ||
        memory_region_is_protected(section->mr) ||
        /* vhost-vDPA doesn't allow MMIO to be mapped  */
        memory_region_is_ram_device(section->mr)) {
        return true;
    }

    if (section->offset_within_address_space < iova_min) {
        error_report("RAM section out of device range (min=0x%" PRIx64
                     ", addr=0x%" HWADDR_PRIx ")",
                     iova_min, section->offset_within_address_space);
        return true;
    }
    /*
     * While using vIOMMU, sometimes the section will be larger than iova_max,
     * but the memory that actually maps is smaller, so move the check to
     * function vhost_vdpa_iommu_map_notify(). That function will use the actual
     * size that maps to the kernel
     */

    if (!memory_region_is_iommu(section->mr)) {
        llend = vhost_vdpa_section_end(section, page_mask);
        if (int128_gt(llend, int128_make64(iova_max))) {
            error_report("RAM section out of device range (max=0x%" PRIx64
                         ", end addr=0x%" PRIx64 ")",
                         iova_max, int128_get64(llend));
            return true;
        }
    }

    return false;
}

/*
 * The caller must set asid = 0 if the device does not support asid.
 * This is not an ABI break since it is set to 0 by the initializer anyway.
 */
int vhost_vdpa_dma_map(struct vhost_vdpa *v, uint32_t asid, hwaddr iova,
                       hwaddr size, void *vaddr, bool readonly)
{
    struct vhost_msg_v2 msg = {};
    int fd = v->device_fd;
    int ret = 0;

    msg.type = v->msg_type;
    msg.asid = asid;
    msg.iotlb.iova = iova;
    msg.iotlb.size = size;
    msg.iotlb.uaddr = (uint64_t)(uintptr_t)vaddr;
    msg.iotlb.perm = readonly ? VHOST_ACCESS_RO : VHOST_ACCESS_RW;
    msg.iotlb.type = VHOST_IOTLB_UPDATE;

    trace_vhost_vdpa_dma_map(v, fd, msg.type, msg.asid, msg.iotlb.iova,
                             msg.iotlb.size, msg.iotlb.uaddr, msg.iotlb.perm,
                             msg.iotlb.type);

    if (write(fd, &msg, sizeof(msg)) != sizeof(msg)) {
        error_report("failed to write, fd=%d, errno=%d (%s)",
            fd, errno, strerror(errno));
        return -EIO ;
    }

    return ret;
}

/*
 * The caller must set asid = 0 if the device does not support asid.
 * This is not an ABI break since it is set to 0 by the initializer anyway.
 */
int vhost_vdpa_dma_unmap(struct vhost_vdpa *v, uint32_t asid, hwaddr iova,
                         hwaddr size)
{
    struct vhost_msg_v2 msg = {};
    int fd = v->device_fd;
    int ret = 0;

    msg.type = v->msg_type;
    msg.asid = asid;
    msg.iotlb.iova = iova;
    msg.iotlb.size = size;
    msg.iotlb.type = VHOST_IOTLB_INVALIDATE;

    trace_vhost_vdpa_dma_unmap(v, fd, msg.type, msg.asid, msg.iotlb.iova,
                               msg.iotlb.size, msg.iotlb.type);

    if (write(fd, &msg, sizeof(msg)) != sizeof(msg)) {
        error_report("failed to write, fd=%d, errno=%d (%s)",
            fd, errno, strerror(errno));
        return -EIO ;
    }

    return ret;
}

static void vhost_vdpa_listener_begin_batch(struct vhost_vdpa *v)
{
    int fd = v->device_fd;
    struct vhost_msg_v2 msg = {
        .type = v->msg_type,
        .iotlb.type = VHOST_IOTLB_BATCH_BEGIN,
    };

    trace_vhost_vdpa_listener_begin_batch(v, fd, msg.type, msg.iotlb.type);
    if (write(fd, &msg, sizeof(msg)) != sizeof(msg)) {
        error_report("failed to write, fd=%d, errno=%d (%s)",
                     fd, errno, strerror(errno));
    }
}

static void vhost_vdpa_iotlb_batch_begin_once(struct vhost_vdpa *v)
{
    if (v->dev->backend_cap & (0x1ULL << VHOST_BACKEND_F_IOTLB_BATCH) &&
        !v->iotlb_batch_begin_sent) {
        vhost_vdpa_listener_begin_batch(v);
    }

    v->iotlb_batch_begin_sent = true;
}

static void vhost_vdpa_listener_commit(MemoryListener *listener)
{
    struct vhost_vdpa *v = container_of(listener, struct vhost_vdpa, listener);
    struct vhost_dev *dev = v->dev;
    struct vhost_msg_v2 msg = {};
    int fd = v->device_fd;

    if (!(dev->backend_cap & (0x1ULL << VHOST_BACKEND_F_IOTLB_BATCH))) {
        return;
    }

    if (!v->iotlb_batch_begin_sent) {
        return;
    }

    msg.type = v->msg_type;
    msg.iotlb.type = VHOST_IOTLB_BATCH_END;

    trace_vhost_vdpa_listener_commit(v, fd, msg.type, msg.iotlb.type);
    if (write(fd, &msg, sizeof(msg)) != sizeof(msg)) {
        error_report("failed to write, fd=%d, errno=%d (%s)",
                     fd, errno, strerror(errno));
    }

    v->iotlb_batch_begin_sent = false;
}

static void vhost_vdpa_iommu_map_notify(IOMMUNotifier *n, IOMMUTLBEntry *iotlb)
{
    struct vdpa_iommu *iommu = container_of(n, struct vdpa_iommu, n);

    hwaddr iova = iotlb->iova + iommu->iommu_offset;
    struct vhost_vdpa *v = iommu->dev;
    void *vaddr;
    int ret;
    Int128 llend;

    if (iotlb->target_as != &address_space_memory) {
        error_report("Wrong target AS \"%s\", only system memory is allowed",
                     iotlb->target_as->name ? iotlb->target_as->name : "none");
        return;
    }
    RCU_READ_LOCK_GUARD();
    /* check if RAM section out of device range */
    llend = int128_add(int128_makes64(iotlb->addr_mask), int128_makes64(iova));
    if (int128_gt(llend, int128_make64(v->iova_range.last))) {
        error_report("RAM section out of device range (max=0x%" PRIx64
                     ", end addr=0x%" PRIx64 ")",
                     v->iova_range.last, int128_get64(llend));
        return;
    }

    if ((iotlb->perm & IOMMU_RW) != IOMMU_NONE) {
        bool read_only;

        if (!memory_get_xlat_addr(iotlb, &vaddr, NULL, &read_only, NULL)) {
            return;
        }
        ret = vhost_vdpa_dma_map(v, VHOST_VDPA_GUEST_PA_ASID, iova,
                                 iotlb->addr_mask + 1, vaddr, read_only);
        if (ret) {
            error_report("vhost_vdpa_dma_map(%p, 0x%" HWADDR_PRIx ", "
                         "0x%" HWADDR_PRIx ", %p) = %d (%m)",
                         v, iova, iotlb->addr_mask + 1, vaddr, ret);
        }
    } else {
        ret = vhost_vdpa_dma_unmap(v, VHOST_VDPA_GUEST_PA_ASID, iova,
                                   iotlb->addr_mask + 1);
        if (ret) {
            error_report("vhost_vdpa_dma_unmap(%p, 0x%" HWADDR_PRIx ", "
                         "0x%" HWADDR_PRIx ") = %d (%m)",
                         v, iova, iotlb->addr_mask + 1, ret);
        }
    }
}

static void vhost_vdpa_iommu_region_add(MemoryListener *listener,
                                        MemoryRegionSection *section)
{
    struct vhost_vdpa *v = container_of(listener, struct vhost_vdpa, listener);

    struct vdpa_iommu *iommu;
    Int128 end;
    int iommu_idx;
    IOMMUMemoryRegion *iommu_mr;
    int ret;

    iommu_mr = IOMMU_MEMORY_REGION(section->mr);

    iommu = g_malloc0(sizeof(*iommu));
    end = int128_add(int128_make64(section->offset_within_region),
                     section->size);
    end = int128_sub(end, int128_one());
    iommu_idx = memory_region_iommu_attrs_to_index(iommu_mr,
                                                   MEMTXATTRS_UNSPECIFIED);
    iommu->iommu_mr = iommu_mr;
    iommu_notifier_init(&iommu->n, vhost_vdpa_iommu_map_notify,
                        IOMMU_NOTIFIER_IOTLB_EVENTS,
                        section->offset_within_region,
                        int128_get64(end),
                        iommu_idx);
    iommu->iommu_offset = section->offset_within_address_space -
                          section->offset_within_region;
    iommu->dev = v;

    ret = memory_region_register_iommu_notifier(section->mr, &iommu->n, NULL);
    if (ret) {
        g_free(iommu);
        return;
    }

    QLIST_INSERT_HEAD(&v->iommu_list, iommu, iommu_next);
    memory_region_iommu_replay(iommu->iommu_mr, &iommu->n);

    return;
}

static void vhost_vdpa_iommu_region_del(MemoryListener *listener,
                                        MemoryRegionSection *section)
{
    struct vhost_vdpa *v = container_of(listener, struct vhost_vdpa, listener);

    struct vdpa_iommu *iommu;

    QLIST_FOREACH(iommu, &v->iommu_list, iommu_next)
    {
        if (MEMORY_REGION(iommu->iommu_mr) == section->mr &&
            iommu->n.start == section->offset_within_region) {
            memory_region_unregister_iommu_notifier(section->mr, &iommu->n);
            QLIST_REMOVE(iommu, iommu_next);
            g_free(iommu);
            break;
        }
    }
}

static void vhost_vdpa_listener_region_add(MemoryListener *listener,
                                           MemoryRegionSection *section)
{
    DMAMap mem_region = {};
    struct vhost_vdpa *v = container_of(listener, struct vhost_vdpa, listener);
    hwaddr iova;
    Int128 llend, llsize;
    void *vaddr;
    int ret;
    int page_size = qemu_target_page_size();
    int page_mask = -page_size;

    if (vhost_vdpa_listener_skipped_section(section, v->iova_range.first,
                                            v->iova_range.last, page_mask)) {
        return;
    }
    if (memory_region_is_iommu(section->mr)) {
        vhost_vdpa_iommu_region_add(listener, section);
        return;
    }

    if (unlikely((section->offset_within_address_space & ~page_mask) !=
                 (section->offset_within_region & ~page_mask))) {
        trace_vhost_vdpa_listener_region_add_unaligned(v, section->mr->name,
                       section->offset_within_address_space & ~page_mask,
                       section->offset_within_region & ~page_mask);
        return;
    }

    iova = ROUND_UP(section->offset_within_address_space, page_size);
    llend = vhost_vdpa_section_end(section, page_mask);
    if (int128_ge(int128_make64(iova), llend)) {
        return;
    }

    memory_region_ref(section->mr);

    /* Here we assume that memory_region_is_ram(section->mr)==true */

    vaddr = memory_region_get_ram_ptr(section->mr) +
            section->offset_within_region +
            (iova - section->offset_within_address_space);

    trace_vhost_vdpa_listener_region_add(v, iova, int128_get64(llend),
                                         vaddr, section->readonly);

    llsize = int128_sub(llend, int128_make64(iova));
    if (v->shadow_data) {
        int r;

        mem_region.translated_addr = (hwaddr)(uintptr_t)vaddr,
        mem_region.size = int128_get64(llsize) - 1,
        mem_region.perm = IOMMU_ACCESS_FLAG(true, section->readonly),

        r = vhost_iova_tree_map_alloc(v->iova_tree, &mem_region);
        if (unlikely(r != IOVA_OK)) {
            error_report("Can't allocate a mapping (%d)", r);
            goto fail;
        }

        iova = mem_region.iova;
    }

    vhost_vdpa_iotlb_batch_begin_once(v);
    ret = vhost_vdpa_dma_map(v, VHOST_VDPA_GUEST_PA_ASID, iova,
                             int128_get64(llsize), vaddr, section->readonly);
    if (ret) {
        error_report("vhost vdpa map fail!");
        goto fail_map;
    }

    return;

fail_map:
    if (v->shadow_data) {
        vhost_iova_tree_remove(v->iova_tree, mem_region);
    }

fail:
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
    int page_size = qemu_target_page_size();
    int page_mask = -page_size;

    if (vhost_vdpa_listener_skipped_section(section, v->iova_range.first,
                                            v->iova_range.last, page_mask)) {
        return;
    }
    if (memory_region_is_iommu(section->mr)) {
        vhost_vdpa_iommu_region_del(listener, section);
    }

    if (unlikely((section->offset_within_address_space & ~page_mask) !=
                 (section->offset_within_region & ~page_mask))) {
        trace_vhost_vdpa_listener_region_del_unaligned(v, section->mr->name,
                       section->offset_within_address_space & ~page_mask,
                       section->offset_within_region & ~page_mask);
        return;
    }

    iova = ROUND_UP(section->offset_within_address_space, page_size);
    llend = vhost_vdpa_section_end(section, page_mask);

    trace_vhost_vdpa_listener_region_del(v, iova,
        int128_get64(int128_sub(llend, int128_one())));

    if (int128_ge(int128_make64(iova), llend)) {
        return;
    }

    llsize = int128_sub(llend, int128_make64(iova));

    if (v->shadow_data) {
        const DMAMap *result;
        const void *vaddr = memory_region_get_ram_ptr(section->mr) +
            section->offset_within_region +
            (iova - section->offset_within_address_space);
        DMAMap mem_region = {
            .translated_addr = (hwaddr)(uintptr_t)vaddr,
            .size = int128_get64(llsize) - 1,
        };

        result = vhost_iova_tree_find_iova(v->iova_tree, &mem_region);
        if (!result) {
            /* The memory listener map wasn't mapped */
            return;
        }
        iova = result->iova;
        vhost_iova_tree_remove(v->iova_tree, *result);
    }
    vhost_vdpa_iotlb_batch_begin_once(v);
    /*
     * The unmap ioctl doesn't accept a full 64-bit. need to check it
     */
    if (int128_eq(llsize, int128_2_64())) {
        llsize = int128_rshift(llsize, 1);
        ret = vhost_vdpa_dma_unmap(v, VHOST_VDPA_GUEST_PA_ASID, iova,
                                   int128_get64(llsize));

        if (ret) {
            error_report("vhost_vdpa_dma_unmap(%p, 0x%" HWADDR_PRIx ", "
                         "0x%" HWADDR_PRIx ") = %d (%m)",
                         v, iova, int128_get64(llsize), ret);
        }
        iova += int128_get64(llsize);
    }
    ret = vhost_vdpa_dma_unmap(v, VHOST_VDPA_GUEST_PA_ASID, iova,
                               int128_get64(llsize));

    if (ret) {
        error_report("vhost_vdpa_dma_unmap(%p, 0x%" HWADDR_PRIx ", "
                     "0x%" HWADDR_PRIx ") = %d (%m)",
                     v, iova, int128_get64(llsize), ret);
    }

    memory_region_unref(section->mr);
}
/*
 * IOTLB API is used by vhost-vdpa which requires incremental updating
 * of the mapping. So we can not use generic vhost memory listener which
 * depends on the addnop().
 */
static const MemoryListener vhost_vdpa_memory_listener = {
    .name = "vhost-vdpa",
    .commit = vhost_vdpa_listener_commit,
    .region_add = vhost_vdpa_listener_region_add,
    .region_del = vhost_vdpa_listener_region_del,
};

static int vhost_vdpa_call(struct vhost_dev *dev, unsigned long int request,
                             void *arg)
{
    struct vhost_vdpa *v = dev->opaque;
    int fd = v->device_fd;
    int ret;

    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_VDPA);

    ret = ioctl(fd, request, arg);
    return ret < 0 ? -errno : ret;
}

static int vhost_vdpa_add_status(struct vhost_dev *dev, uint8_t status)
{
    uint8_t s;
    int ret;

    trace_vhost_vdpa_add_status(dev, status);
    ret = vhost_vdpa_call(dev, VHOST_VDPA_GET_STATUS, &s);
    if (ret < 0) {
        return ret;
    }

    s |= status;

    ret = vhost_vdpa_call(dev, VHOST_VDPA_SET_STATUS, &s);
    if (ret < 0) {
        return ret;
    }

    ret = vhost_vdpa_call(dev, VHOST_VDPA_GET_STATUS, &s);
    if (ret < 0) {
        return ret;
    }

    if (!(s & status)) {
        return -EIO;
    }

    return 0;
}

int vhost_vdpa_get_iova_range(int fd, struct vhost_vdpa_iova_range *iova_range)
{
    int ret = ioctl(fd, VHOST_VDPA_GET_IOVA_RANGE, iova_range);

    return ret < 0 ? -errno : 0;
}

/*
 * The use of this function is for requests that only need to be
 * applied once. Typically such request occurs at the beginning
 * of operation, and before setting up queues. It should not be
 * used for request that performs operation until all queues are
 * set, which would need to check dev->vq_index_end instead.
 */
static bool vhost_vdpa_first_dev(struct vhost_dev *dev)
{
    struct vhost_vdpa *v = dev->opaque;

    return v->index == 0;
}

static int vhost_vdpa_get_dev_features(struct vhost_dev *dev,
                                       uint64_t *features)
{
    int ret;

    ret = vhost_vdpa_call(dev, VHOST_GET_FEATURES, features);
    trace_vhost_vdpa_get_features(dev, *features);
    return ret;
}

static void vhost_vdpa_init_svq(struct vhost_dev *hdev, struct vhost_vdpa *v)
{
    g_autoptr(GPtrArray) shadow_vqs = NULL;

    shadow_vqs = g_ptr_array_new_full(hdev->nvqs, vhost_svq_free);
    for (unsigned n = 0; n < hdev->nvqs; ++n) {
        VhostShadowVirtqueue *svq;

        svq = vhost_svq_new(v->shadow_vq_ops, v->shadow_vq_ops_opaque);
        g_ptr_array_add(shadow_vqs, svq);
    }

    v->shadow_vqs = g_steal_pointer(&shadow_vqs);
}

static int vhost_vdpa_init(struct vhost_dev *dev, void *opaque, Error **errp)
{
    struct vhost_vdpa *v;
    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_VDPA);
    trace_vhost_vdpa_init(dev, opaque);
    int ret;

    v = opaque;
    v->dev = dev;
    dev->opaque =  opaque ;
    v->listener = vhost_vdpa_memory_listener;
    v->msg_type = VHOST_IOTLB_MSG_V2;
    vhost_vdpa_init_svq(dev, v);

    error_propagate(&dev->migration_blocker, v->migration_blocker);
    if (!vhost_vdpa_first_dev(dev)) {
        return 0;
    }

    /*
     * If dev->shadow_vqs_enabled at initialization that means the device has
     * been started with x-svq=on, so don't block migration
     */
    if (dev->migration_blocker == NULL && !v->shadow_vqs_enabled) {
        /* We don't have dev->features yet */
        uint64_t features;
        ret = vhost_vdpa_get_dev_features(dev, &features);
        if (unlikely(ret)) {
            error_setg_errno(errp, -ret, "Could not get device features");
            return ret;
        }
        vhost_svq_valid_features(features, &dev->migration_blocker);
    }

    /*
     * Similar to VFIO, we end up pinning all guest memory and have to
     * disable discarding of RAM.
     */
    ret = ram_block_discard_disable(true);
    if (ret) {
        error_report("Cannot set discarding of RAM broken");
        return ret;
    }

    vhost_vdpa_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE |
                               VIRTIO_CONFIG_S_DRIVER);

    return 0;
}

static void vhost_vdpa_host_notifier_uninit(struct vhost_dev *dev,
                                            int queue_index)
{
    size_t page_size = qemu_real_host_page_size();
    struct vhost_vdpa *v = dev->opaque;
    VirtIODevice *vdev = dev->vdev;
    VhostVDPAHostNotifier *n;

    n = &v->notifier[queue_index];

    if (n->addr) {
        virtio_queue_set_host_notifier_mr(vdev, queue_index, &n->mr, false);
        object_unparent(OBJECT(&n->mr));
        munmap(n->addr, page_size);
        n->addr = NULL;
    }
}

static int vhost_vdpa_host_notifier_init(struct vhost_dev *dev, int queue_index)
{
    size_t page_size = qemu_real_host_page_size();
    struct vhost_vdpa *v = dev->opaque;
    VirtIODevice *vdev = dev->vdev;
    VhostVDPAHostNotifier *n;
    int fd = v->device_fd;
    void *addr;
    char *name;

    vhost_vdpa_host_notifier_uninit(dev, queue_index);

    n = &v->notifier[queue_index];

    addr = mmap(NULL, page_size, PROT_WRITE, MAP_SHARED, fd,
                queue_index * page_size);
    if (addr == MAP_FAILED) {
        goto err;
    }

    name = g_strdup_printf("vhost-vdpa/host-notifier@%p mmaps[%d]",
                           v, queue_index);
    memory_region_init_ram_device_ptr(&n->mr, OBJECT(vdev), name,
                                      page_size, addr);
    g_free(name);

    if (virtio_queue_set_host_notifier_mr(vdev, queue_index, &n->mr, true)) {
        object_unparent(OBJECT(&n->mr));
        munmap(addr, page_size);
        goto err;
    }
    n->addr = addr;

    return 0;

err:
    return -1;
}

static void vhost_vdpa_host_notifiers_uninit(struct vhost_dev *dev, int n)
{
    int i;

    /*
     * Pack all the changes to the memory regions in a single
     * transaction to avoid a few updating of the address space
     * topology.
     */
    memory_region_transaction_begin();

    for (i = dev->vq_index; i < dev->vq_index + n; i++) {
        vhost_vdpa_host_notifier_uninit(dev, i);
    }

    memory_region_transaction_commit();
}

static void vhost_vdpa_host_notifiers_init(struct vhost_dev *dev)
{
    struct vhost_vdpa *v = dev->opaque;
    int i;

    if (v->shadow_vqs_enabled) {
        /* FIXME SVQ is not compatible with host notifiers mr */
        return;
    }

    /*
     * Pack all the changes to the memory regions in a single
     * transaction to avoid a few updating of the address space
     * topology.
     */
    memory_region_transaction_begin();

    for (i = dev->vq_index; i < dev->vq_index + dev->nvqs; i++) {
        if (vhost_vdpa_host_notifier_init(dev, i)) {
            vhost_vdpa_host_notifiers_uninit(dev, i - dev->vq_index);
            break;
        }
    }

    memory_region_transaction_commit();
}

static void vhost_vdpa_svq_cleanup(struct vhost_dev *dev)
{
    struct vhost_vdpa *v = dev->opaque;
    size_t idx;

    for (idx = 0; idx < v->shadow_vqs->len; ++idx) {
        vhost_svq_stop(g_ptr_array_index(v->shadow_vqs, idx));
    }
    g_ptr_array_free(v->shadow_vqs, true);
}

static int vhost_vdpa_cleanup(struct vhost_dev *dev)
{
    struct vhost_vdpa *v;
    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_VDPA);
    v = dev->opaque;
    trace_vhost_vdpa_cleanup(dev, v);
    if (vhost_vdpa_first_dev(dev)) {
        ram_block_discard_disable(false);
    }

    vhost_vdpa_host_notifiers_uninit(dev, dev->nvqs);
    memory_listener_unregister(&v->listener);
    vhost_vdpa_svq_cleanup(dev);

    dev->opaque = NULL;

    return 0;
}

static int vhost_vdpa_memslots_limit(struct vhost_dev *dev)
{
    trace_vhost_vdpa_memslots_limit(dev, INT_MAX);
    return INT_MAX;
}

static int vhost_vdpa_set_mem_table(struct vhost_dev *dev,
                                    struct vhost_memory *mem)
{
    if (!vhost_vdpa_first_dev(dev)) {
        return 0;
    }

    trace_vhost_vdpa_set_mem_table(dev, mem->nregions, mem->padding);
    if (trace_event_get_state_backends(TRACE_VHOST_VDPA_SET_MEM_TABLE) &&
        trace_event_get_state_backends(TRACE_VHOST_VDPA_DUMP_REGIONS)) {
        int i;
        for (i = 0; i < mem->nregions; i++) {
            trace_vhost_vdpa_dump_regions(dev, i,
                                          mem->regions[i].guest_phys_addr,
                                          mem->regions[i].memory_size,
                                          mem->regions[i].userspace_addr,
                                          mem->regions[i].flags_padding);
        }
    }
    if (mem->padding) {
        return -EINVAL;
    }

    return 0;
}

static int vhost_vdpa_set_features(struct vhost_dev *dev,
                                   uint64_t features)
{
    struct vhost_vdpa *v = dev->opaque;
    int ret;

    if (!vhost_vdpa_first_dev(dev)) {
        return 0;
    }

    if (v->shadow_vqs_enabled) {
        if ((v->acked_features ^ features) == BIT_ULL(VHOST_F_LOG_ALL)) {
            /*
             * QEMU is just trying to enable or disable logging. SVQ handles
             * this sepparately, so no need to forward this.
             */
            v->acked_features = features;
            return 0;
        }

        v->acked_features = features;

        /* We must not ack _F_LOG if SVQ is enabled */
        features &= ~BIT_ULL(VHOST_F_LOG_ALL);
    }

    trace_vhost_vdpa_set_features(dev, features);
    ret = vhost_vdpa_call(dev, VHOST_SET_FEATURES, &features);
    if (ret) {
        return ret;
    }

    return vhost_vdpa_add_status(dev, VIRTIO_CONFIG_S_FEATURES_OK);
}

static int vhost_vdpa_set_backend_cap(struct vhost_dev *dev)
{
    uint64_t features;
    uint64_t f = 0x1ULL << VHOST_BACKEND_F_IOTLB_MSG_V2 |
        0x1ULL << VHOST_BACKEND_F_IOTLB_BATCH |
        0x1ULL << VHOST_BACKEND_F_IOTLB_ASID |
        0x1ULL << VHOST_BACKEND_F_SUSPEND;
    int r;

    if (vhost_vdpa_call(dev, VHOST_GET_BACKEND_FEATURES, &features)) {
        return -EFAULT;
    }

    features &= f;

    if (vhost_vdpa_first_dev(dev)) {
        r = vhost_vdpa_call(dev, VHOST_SET_BACKEND_FEATURES, &features);
        if (r) {
            return -EFAULT;
        }
    }

    dev->backend_cap = features;

    return 0;
}

static int vhost_vdpa_get_device_id(struct vhost_dev *dev,
                                    uint32_t *device_id)
{
    int ret;
    ret = vhost_vdpa_call(dev, VHOST_VDPA_GET_DEVICE_ID, device_id);
    trace_vhost_vdpa_get_device_id(dev, *device_id);
    return ret;
}

static int vhost_vdpa_reset_device(struct vhost_dev *dev)
{
    struct vhost_vdpa *v = dev->opaque;
    int ret;
    uint8_t status = 0;

    ret = vhost_vdpa_call(dev, VHOST_VDPA_SET_STATUS, &status);
    trace_vhost_vdpa_reset_device(dev);
    v->suspended = false;
    return ret;
}

static int vhost_vdpa_get_vq_index(struct vhost_dev *dev, int idx)
{
    assert(idx >= dev->vq_index && idx < dev->vq_index + dev->nvqs);

    trace_vhost_vdpa_get_vq_index(dev, idx, idx);
    return idx;
}

int vhost_vdpa_set_vring_ready(struct vhost_vdpa *v, unsigned idx)
{
    struct vhost_dev *dev = v->dev;
    struct vhost_vring_state state = {
        .index = idx,
        .num = 1,
    };
    int r = vhost_vdpa_call(dev, VHOST_VDPA_SET_VRING_ENABLE, &state);

    trace_vhost_vdpa_set_vring_ready(dev, idx, r);
    return r;
}

static int vhost_vdpa_set_config_call(struct vhost_dev *dev,
                                       int fd)
{
    trace_vhost_vdpa_set_config_call(dev, fd);
    return vhost_vdpa_call(dev, VHOST_VDPA_SET_CONFIG_CALL, &fd);
}

static void vhost_vdpa_dump_config(struct vhost_dev *dev, const uint8_t *config,
                                   uint32_t config_len)
{
    int b, len;
    char line[QEMU_HEXDUMP_LINE_LEN];

    for (b = 0; b < config_len; b += 16) {
        len = config_len - b;
        qemu_hexdump_line(line, b, config, len, false);
        trace_vhost_vdpa_dump_config(dev, line);
    }
}

static int vhost_vdpa_set_config(struct vhost_dev *dev, const uint8_t *data,
                                   uint32_t offset, uint32_t size,
                                   uint32_t flags)
{
    struct vhost_vdpa_config *config;
    int ret;
    unsigned long config_size = offsetof(struct vhost_vdpa_config, buf);

    trace_vhost_vdpa_set_config(dev, offset, size, flags);
    config = g_malloc(size + config_size);
    config->off = offset;
    config->len = size;
    memcpy(config->buf, data, size);
    if (trace_event_get_state_backends(TRACE_VHOST_VDPA_SET_CONFIG) &&
        trace_event_get_state_backends(TRACE_VHOST_VDPA_DUMP_CONFIG)) {
        vhost_vdpa_dump_config(dev, data, size);
    }
    ret = vhost_vdpa_call(dev, VHOST_VDPA_SET_CONFIG, config);
    g_free(config);
    return ret;
}

static int vhost_vdpa_get_config(struct vhost_dev *dev, uint8_t *config,
                                   uint32_t config_len, Error **errp)
{
    struct vhost_vdpa_config *v_config;
    unsigned long config_size = offsetof(struct vhost_vdpa_config, buf);
    int ret;

    trace_vhost_vdpa_get_config(dev, config, config_len);
    v_config = g_malloc(config_len + config_size);
    v_config->len = config_len;
    v_config->off = 0;
    ret = vhost_vdpa_call(dev, VHOST_VDPA_GET_CONFIG, v_config);
    memcpy(config, v_config->buf, config_len);
    g_free(v_config);
    if (trace_event_get_state_backends(TRACE_VHOST_VDPA_GET_CONFIG) &&
        trace_event_get_state_backends(TRACE_VHOST_VDPA_DUMP_CONFIG)) {
        vhost_vdpa_dump_config(dev, config, config_len);
    }
    return ret;
 }

static int vhost_vdpa_set_dev_vring_base(struct vhost_dev *dev,
                                         struct vhost_vring_state *ring)
{
    trace_vhost_vdpa_set_vring_base(dev, ring->index, ring->num);
    return vhost_vdpa_call(dev, VHOST_SET_VRING_BASE, ring);
}

static int vhost_vdpa_set_vring_dev_kick(struct vhost_dev *dev,
                                         struct vhost_vring_file *file)
{
    trace_vhost_vdpa_set_vring_kick(dev, file->index, file->fd);
    return vhost_vdpa_call(dev, VHOST_SET_VRING_KICK, file);
}

static int vhost_vdpa_set_vring_dev_call(struct vhost_dev *dev,
                                         struct vhost_vring_file *file)
{
    trace_vhost_vdpa_set_vring_call(dev, file->index, file->fd);
    return vhost_vdpa_call(dev, VHOST_SET_VRING_CALL, file);
}

static int vhost_vdpa_set_vring_dev_addr(struct vhost_dev *dev,
                                         struct vhost_vring_addr *addr)
{
    trace_vhost_vdpa_set_vring_addr(dev, addr->index, addr->flags,
                                addr->desc_user_addr, addr->used_user_addr,
                                addr->avail_user_addr,
                                addr->log_guest_addr);

    return vhost_vdpa_call(dev, VHOST_SET_VRING_ADDR, addr);

}

/**
 * Set the shadow virtqueue descriptors to the device
 *
 * @dev: The vhost device model
 * @svq: The shadow virtqueue
 * @idx: The index of the virtqueue in the vhost device
 * @errp: Error
 *
 * Note that this function does not rewind kick file descriptor if cannot set
 * call one.
 */
static int vhost_vdpa_svq_set_fds(struct vhost_dev *dev,
                                  VhostShadowVirtqueue *svq, unsigned idx,
                                  Error **errp)
{
    struct vhost_vring_file file = {
        .index = dev->vq_index + idx,
    };
    const EventNotifier *event_notifier = &svq->hdev_kick;
    int r;

    r = event_notifier_init(&svq->hdev_kick, 0);
    if (r != 0) {
        error_setg_errno(errp, -r, "Couldn't create kick event notifier");
        goto err_init_hdev_kick;
    }

    r = event_notifier_init(&svq->hdev_call, 0);
    if (r != 0) {
        error_setg_errno(errp, -r, "Couldn't create call event notifier");
        goto err_init_hdev_call;
    }

    file.fd = event_notifier_get_fd(event_notifier);
    r = vhost_vdpa_set_vring_dev_kick(dev, &file);
    if (unlikely(r != 0)) {
        error_setg_errno(errp, -r, "Can't set device kick fd");
        goto err_init_set_dev_fd;
    }

    event_notifier = &svq->hdev_call;
    file.fd = event_notifier_get_fd(event_notifier);
    r = vhost_vdpa_set_vring_dev_call(dev, &file);
    if (unlikely(r != 0)) {
        error_setg_errno(errp, -r, "Can't set device call fd");
        goto err_init_set_dev_fd;
    }

    return 0;

err_init_set_dev_fd:
    event_notifier_set_handler(&svq->hdev_call, NULL);

err_init_hdev_call:
    event_notifier_cleanup(&svq->hdev_kick);

err_init_hdev_kick:
    return r;
}

/**
 * Unmap a SVQ area in the device
 */
static void vhost_vdpa_svq_unmap_ring(struct vhost_vdpa *v, hwaddr addr)
{
    const DMAMap needle = {
        .translated_addr = addr,
    };
    const DMAMap *result = vhost_iova_tree_find_iova(v->iova_tree, &needle);
    hwaddr size;
    int r;

    if (unlikely(!result)) {
        error_report("Unable to find SVQ address to unmap");
        return;
    }

    size = ROUND_UP(result->size, qemu_real_host_page_size());
    r = vhost_vdpa_dma_unmap(v, v->address_space_id, result->iova, size);
    if (unlikely(r < 0)) {
        error_report("Unable to unmap SVQ vring: %s (%d)", g_strerror(-r), -r);
        return;
    }

    vhost_iova_tree_remove(v->iova_tree, *result);
}

static void vhost_vdpa_svq_unmap_rings(struct vhost_dev *dev,
                                       const VhostShadowVirtqueue *svq)
{
    struct vhost_vdpa *v = dev->opaque;
    struct vhost_vring_addr svq_addr;

    vhost_svq_get_vring_addr(svq, &svq_addr);

    vhost_vdpa_svq_unmap_ring(v, svq_addr.desc_user_addr);

    vhost_vdpa_svq_unmap_ring(v, svq_addr.used_user_addr);
}

/**
 * Map the SVQ area in the device
 *
 * @v: Vhost-vdpa device
 * @needle: The area to search iova
 * @errorp: Error pointer
 */
static bool vhost_vdpa_svq_map_ring(struct vhost_vdpa *v, DMAMap *needle,
                                    Error **errp)
{
    int r;

    r = vhost_iova_tree_map_alloc(v->iova_tree, needle);
    if (unlikely(r != IOVA_OK)) {
        error_setg(errp, "Cannot allocate iova (%d)", r);
        return false;
    }

    r = vhost_vdpa_dma_map(v, v->address_space_id, needle->iova,
                           needle->size + 1,
                           (void *)(uintptr_t)needle->translated_addr,
                           needle->perm == IOMMU_RO);
    if (unlikely(r != 0)) {
        error_setg_errno(errp, -r, "Cannot map region to device");
        vhost_iova_tree_remove(v->iova_tree, *needle);
    }

    return r == 0;
}

/**
 * Map the shadow virtqueue rings in the device
 *
 * @dev: The vhost device
 * @svq: The shadow virtqueue
 * @addr: Assigned IOVA addresses
 * @errp: Error pointer
 */
static bool vhost_vdpa_svq_map_rings(struct vhost_dev *dev,
                                     const VhostShadowVirtqueue *svq,
                                     struct vhost_vring_addr *addr,
                                     Error **errp)
{
    ERRP_GUARD();
    DMAMap device_region, driver_region;
    struct vhost_vring_addr svq_addr;
    struct vhost_vdpa *v = dev->opaque;
    size_t device_size = vhost_svq_device_area_size(svq);
    size_t driver_size = vhost_svq_driver_area_size(svq);
    size_t avail_offset;
    bool ok;

    vhost_svq_get_vring_addr(svq, &svq_addr);

    driver_region = (DMAMap) {
        .translated_addr = svq_addr.desc_user_addr,
        .size = driver_size - 1,
        .perm = IOMMU_RO,
    };
    ok = vhost_vdpa_svq_map_ring(v, &driver_region, errp);
    if (unlikely(!ok)) {
        error_prepend(errp, "Cannot create vq driver region: ");
        return false;
    }
    addr->desc_user_addr = driver_region.iova;
    avail_offset = svq_addr.avail_user_addr - svq_addr.desc_user_addr;
    addr->avail_user_addr = driver_region.iova + avail_offset;

    device_region = (DMAMap) {
        .translated_addr = svq_addr.used_user_addr,
        .size = device_size - 1,
        .perm = IOMMU_RW,
    };
    ok = vhost_vdpa_svq_map_ring(v, &device_region, errp);
    if (unlikely(!ok)) {
        error_prepend(errp, "Cannot create vq device region: ");
        vhost_vdpa_svq_unmap_ring(v, driver_region.translated_addr);
    }
    addr->used_user_addr = device_region.iova;

    return ok;
}

static bool vhost_vdpa_svq_setup(struct vhost_dev *dev,
                                 VhostShadowVirtqueue *svq, unsigned idx,
                                 Error **errp)
{
    uint16_t vq_index = dev->vq_index + idx;
    struct vhost_vring_state s = {
        .index = vq_index,
    };
    int r;

    r = vhost_vdpa_set_dev_vring_base(dev, &s);
    if (unlikely(r)) {
        error_setg_errno(errp, -r, "Cannot set vring base");
        return false;
    }

    r = vhost_vdpa_svq_set_fds(dev, svq, idx, errp);
    return r == 0;
}

static bool vhost_vdpa_svqs_start(struct vhost_dev *dev)
{
    struct vhost_vdpa *v = dev->opaque;
    Error *err = NULL;
    unsigned i;

    if (!v->shadow_vqs_enabled) {
        return true;
    }

    for (i = 0; i < v->shadow_vqs->len; ++i) {
        VirtQueue *vq = virtio_get_queue(dev->vdev, dev->vq_index + i);
        VhostShadowVirtqueue *svq = g_ptr_array_index(v->shadow_vqs, i);
        struct vhost_vring_addr addr = {
            .index = dev->vq_index + i,
        };
        int r;
        bool ok = vhost_vdpa_svq_setup(dev, svq, i, &err);
        if (unlikely(!ok)) {
            goto err;
        }

        vhost_svq_start(svq, dev->vdev, vq, v->iova_tree);
        ok = vhost_vdpa_svq_map_rings(dev, svq, &addr, &err);
        if (unlikely(!ok)) {
            goto err_map;
        }

        /* Override vring GPA set by vhost subsystem */
        r = vhost_vdpa_set_vring_dev_addr(dev, &addr);
        if (unlikely(r != 0)) {
            error_setg_errno(&err, -r, "Cannot set device address");
            goto err_set_addr;
        }
    }

    return true;

err_set_addr:
    vhost_vdpa_svq_unmap_rings(dev, g_ptr_array_index(v->shadow_vqs, i));

err_map:
    vhost_svq_stop(g_ptr_array_index(v->shadow_vqs, i));

err:
    error_reportf_err(err, "Cannot setup SVQ %u: ", i);
    for (unsigned j = 0; j < i; ++j) {
        VhostShadowVirtqueue *svq = g_ptr_array_index(v->shadow_vqs, j);
        vhost_vdpa_svq_unmap_rings(dev, svq);
        vhost_svq_stop(svq);
    }

    return false;
}

static void vhost_vdpa_svqs_stop(struct vhost_dev *dev)
{
    struct vhost_vdpa *v = dev->opaque;

    if (!v->shadow_vqs_enabled) {
        return;
    }

    for (unsigned i = 0; i < v->shadow_vqs->len; ++i) {
        VhostShadowVirtqueue *svq = g_ptr_array_index(v->shadow_vqs, i);

        vhost_svq_stop(svq);
        vhost_vdpa_svq_unmap_rings(dev, svq);

        event_notifier_cleanup(&svq->hdev_kick);
        event_notifier_cleanup(&svq->hdev_call);
    }
}

static void vhost_vdpa_suspend(struct vhost_dev *dev)
{
    struct vhost_vdpa *v = dev->opaque;
    int r;

    if (!vhost_vdpa_first_dev(dev)) {
        return;
    }

    if (dev->backend_cap & BIT_ULL(VHOST_BACKEND_F_SUSPEND)) {
        trace_vhost_vdpa_suspend(dev);
        r = ioctl(v->device_fd, VHOST_VDPA_SUSPEND);
        if (unlikely(r)) {
            error_report("Cannot suspend: %s(%d)", g_strerror(errno), errno);
        } else {
            v->suspended = true;
            return;
        }
    }

    vhost_vdpa_reset_device(dev);
}

static int vhost_vdpa_dev_start(struct vhost_dev *dev, bool started)
{
    struct vhost_vdpa *v = dev->opaque;
    bool ok;
    trace_vhost_vdpa_dev_start(dev, started);

    if (started) {
        vhost_vdpa_host_notifiers_init(dev);
        ok = vhost_vdpa_svqs_start(dev);
        if (unlikely(!ok)) {
            return -1;
        }
    } else {
        vhost_vdpa_suspend(dev);
        vhost_vdpa_svqs_stop(dev);
        vhost_vdpa_host_notifiers_uninit(dev, dev->nvqs);
    }

    if (dev->vq_index + dev->nvqs != dev->vq_index_end) {
        return 0;
    }

    if (started) {
        if (vhost_dev_has_iommu(dev) && (v->shadow_vqs_enabled)) {
            error_report("SVQ can not work while IOMMU enable, please disable"
                         "IOMMU and try again");
            return -1;
        }
        memory_listener_register(&v->listener, dev->vdev->dma_as);

        return vhost_vdpa_add_status(dev, VIRTIO_CONFIG_S_DRIVER_OK);
    }

    return 0;
}

static void vhost_vdpa_reset_status(struct vhost_dev *dev)
{
    struct vhost_vdpa *v = dev->opaque;

    if (dev->vq_index + dev->nvqs != dev->vq_index_end) {
        return;
    }

    vhost_vdpa_reset_device(dev);
    vhost_vdpa_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE |
                               VIRTIO_CONFIG_S_DRIVER);
    memory_listener_unregister(&v->listener);
}

static int vhost_vdpa_set_log_base(struct vhost_dev *dev, uint64_t base,
                                     struct vhost_log *log)
{
    struct vhost_vdpa *v = dev->opaque;
    if (v->shadow_vqs_enabled || !vhost_vdpa_first_dev(dev)) {
        return 0;
    }

    trace_vhost_vdpa_set_log_base(dev, base, log->size, log->refcnt, log->fd,
                                  log->log);
    return vhost_vdpa_call(dev, VHOST_SET_LOG_BASE, &base);
}

static int vhost_vdpa_set_vring_addr(struct vhost_dev *dev,
                                       struct vhost_vring_addr *addr)
{
    struct vhost_vdpa *v = dev->opaque;

    if (v->shadow_vqs_enabled) {
        /*
         * Device vring addr was set at device start. SVQ base is handled by
         * VirtQueue code.
         */
        return 0;
    }

    return vhost_vdpa_set_vring_dev_addr(dev, addr);
}

static int vhost_vdpa_set_vring_num(struct vhost_dev *dev,
                                      struct vhost_vring_state *ring)
{
    trace_vhost_vdpa_set_vring_num(dev, ring->index, ring->num);
    return vhost_vdpa_call(dev, VHOST_SET_VRING_NUM, ring);
}

static int vhost_vdpa_set_vring_base(struct vhost_dev *dev,
                                       struct vhost_vring_state *ring)
{
    struct vhost_vdpa *v = dev->opaque;

    if (v->shadow_vqs_enabled) {
        /*
         * Device vring base was set at device start. SVQ base is handled by
         * VirtQueue code.
         */
        return 0;
    }

    return vhost_vdpa_set_dev_vring_base(dev, ring);
}

static int vhost_vdpa_get_vring_base(struct vhost_dev *dev,
                                       struct vhost_vring_state *ring)
{
    struct vhost_vdpa *v = dev->opaque;
    int ret;

    if (v->shadow_vqs_enabled) {
        ring->num = virtio_queue_get_last_avail_idx(dev->vdev, ring->index);
        return 0;
    }

    if (!v->suspended) {
        /*
         * Cannot trust in value returned by device, let vhost recover used
         * idx from guest.
         */
        return -1;
    }

    ret = vhost_vdpa_call(dev, VHOST_GET_VRING_BASE, ring);
    trace_vhost_vdpa_get_vring_base(dev, ring->index, ring->num);
    return ret;
}

static int vhost_vdpa_set_vring_kick(struct vhost_dev *dev,
                                       struct vhost_vring_file *file)
{
    struct vhost_vdpa *v = dev->opaque;
    int vdpa_idx = file->index - dev->vq_index;

    if (v->shadow_vqs_enabled) {
        VhostShadowVirtqueue *svq = g_ptr_array_index(v->shadow_vqs, vdpa_idx);
        vhost_svq_set_svq_kick_fd(svq, file->fd);
        return 0;
    } else {
        return vhost_vdpa_set_vring_dev_kick(dev, file);
    }
}

static int vhost_vdpa_set_vring_call(struct vhost_dev *dev,
                                       struct vhost_vring_file *file)
{
    struct vhost_vdpa *v = dev->opaque;
    int vdpa_idx = file->index - dev->vq_index;
    VhostShadowVirtqueue *svq = g_ptr_array_index(v->shadow_vqs, vdpa_idx);

    /* Remember last call fd because we can switch to SVQ anytime. */
    vhost_svq_set_svq_call_fd(svq, file->fd);
    if (v->shadow_vqs_enabled) {
        return 0;
    }

    return vhost_vdpa_set_vring_dev_call(dev, file);
}

static int vhost_vdpa_get_features(struct vhost_dev *dev,
                                     uint64_t *features)
{
    int ret = vhost_vdpa_get_dev_features(dev, features);

    if (ret == 0) {
        /* Add SVQ logging capabilities */
        *features |= BIT_ULL(VHOST_F_LOG_ALL);
    }

    return ret;
}

static int vhost_vdpa_set_owner(struct vhost_dev *dev)
{
    if (!vhost_vdpa_first_dev(dev)) {
        return 0;
    }

    trace_vhost_vdpa_set_owner(dev);
    return vhost_vdpa_call(dev, VHOST_SET_OWNER, NULL);
}

static int vhost_vdpa_vq_get_addr(struct vhost_dev *dev,
                    struct vhost_vring_addr *addr, struct vhost_virtqueue *vq)
{
    assert(dev->vhost_ops->backend_type == VHOST_BACKEND_TYPE_VDPA);
    addr->desc_user_addr = (uint64_t)(unsigned long)vq->desc_phys;
    addr->avail_user_addr = (uint64_t)(unsigned long)vq->avail_phys;
    addr->used_user_addr = (uint64_t)(unsigned long)vq->used_phys;
    trace_vhost_vdpa_vq_get_addr(dev, vq, addr->desc_user_addr,
                                 addr->avail_user_addr, addr->used_user_addr);
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
        .vhost_set_backend_cap = vhost_vdpa_set_backend_cap,
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
        .vhost_net_set_mtu = NULL,
        .vhost_set_iotlb_callback = NULL,
        .vhost_send_device_iotlb_msg = NULL,
        .vhost_dev_start = vhost_vdpa_dev_start,
        .vhost_get_device_id = vhost_vdpa_get_device_id,
        .vhost_vq_get_addr = vhost_vdpa_vq_get_addr,
        .vhost_force_iommu = vhost_vdpa_force_iommu,
        .vhost_set_config_call = vhost_vdpa_set_config_call,
        .vhost_reset_status = vhost_vdpa_reset_status,
};
