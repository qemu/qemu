/*
 * Container for vfio-user IOMMU type: rather than communicating with the kernel
 * vfio driver, we communicate over a socket to a server using the vfio-user
 * protocol.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <linux/vfio.h>

#include "hw/vfio-user/container.h"
#include "hw/vfio-user/device.h"
#include "hw/vfio-user/trace.h"
#include "hw/vfio/vfio-device.h"
#include "hw/vfio/vfio-listener.h"
#include "qapi/error.h"

/*
 * When DMA space is the physical address space, the region add/del listeners
 * will fire during memory update transactions.  These depend on BQL being held,
 * so do any resulting map/demap ops async while keeping BQL.
 */
static void vfio_user_listener_begin(VFIOContainer *bcontainer)
{
    VFIOUserContainer *container = VFIO_IOMMU_USER(bcontainer);

    container->proxy->async_ops = true;
}

static void vfio_user_listener_commit(VFIOContainer *bcontainer)
{
    VFIOUserContainer *container = VFIO_IOMMU_USER(bcontainer);

    /* wait here for any async requests sent during the transaction */
    container->proxy->async_ops = false;
    vfio_user_wait_reqs(container->proxy);
}

static int vfio_user_dma_unmap(const VFIOContainer *bcontainer,
                               hwaddr iova, uint64_t size,
                               IOMMUTLBEntry *iotlb, bool unmap_all)
{
    VFIOUserContainer *container = VFIO_IOMMU_USER(bcontainer);

    Error *local_err = NULL;
    int ret = 0;

    VFIOUserDMAUnmap *msgp = g_malloc(sizeof(*msgp));

    vfio_user_request_msg(&msgp->hdr, VFIO_USER_DMA_UNMAP, sizeof(*msgp), 0);
    msgp->argsz = sizeof(struct vfio_iommu_type1_dma_unmap);
    msgp->flags = unmap_all ? VFIO_DMA_UNMAP_FLAG_ALL : 0;
    msgp->iova = iova;
    msgp->size = size;
    trace_vfio_user_dma_unmap(msgp->iova, msgp->size, msgp->flags,
                              container->proxy->async_ops);

    if (container->proxy->async_ops) {
        if (!vfio_user_send_nowait(container->proxy, &msgp->hdr, NULL,
                              0, &local_err)) {
            error_report_err(local_err);
            ret = -EFAULT;
        }
    } else {
        if (!vfio_user_send_wait(container->proxy, &msgp->hdr, NULL,
                                 0, &local_err)) {
                error_report_err(local_err);
                ret = -EFAULT;
        }

        if (msgp->hdr.flags & VFIO_USER_ERROR) {
            ret = -msgp->hdr.error_reply;
        }

        g_free(msgp);
    }

    return ret;
}

static int vfio_user_dma_map(const VFIOContainer *bcontainer, hwaddr iova,
                             uint64_t size, void *vaddr, bool readonly,
                             MemoryRegion *mrp)
{
    VFIOUserContainer *container = VFIO_IOMMU_USER(bcontainer);

    int fd = memory_region_get_fd(mrp);
    Error *local_err = NULL;
    int ret = 0;

    VFIOUserFDs *fds = NULL;
    VFIOUserDMAMap *msgp = g_malloc0(sizeof(*msgp));

    vfio_user_request_msg(&msgp->hdr, VFIO_USER_DMA_MAP, sizeof(*msgp), 0);
    msgp->argsz = sizeof(struct vfio_iommu_type1_dma_map);
    msgp->flags = VFIO_DMA_MAP_FLAG_READ;
    msgp->offset = 0;
    msgp->iova = iova;
    msgp->size = size;

    /*
     * vaddr enters as a QEMU process address; make it either a file offset
     * for mapped areas or leave as 0.
     */
    if (fd != -1) {
        msgp->offset = qemu_ram_block_host_offset(mrp->ram_block, vaddr);
    }

    if (!readonly) {
        msgp->flags |= VFIO_DMA_MAP_FLAG_WRITE;
    }

    trace_vfio_user_dma_map(msgp->iova, msgp->size, msgp->offset, msgp->flags,
                            container->proxy->async_ops);

    /*
     * The async_ops case sends without blocking. They're later waited for in
     * vfio_send_wait_reqs.
     */
    if (container->proxy->async_ops) {
        /* can't use auto variable since we don't block */
        if (fd != -1) {
            fds = vfio_user_getfds(1);
            fds->send_fds = 1;
            fds->fds[0] = fd;
        }

        if (!vfio_user_send_nowait(container->proxy, &msgp->hdr, fds,
                              0, &local_err)) {
            error_report_err(local_err);
            ret = -EFAULT;
        }
    } else {
        VFIOUserFDs local_fds = { 1, 0, &fd };

        fds = fd != -1 ? &local_fds : NULL;

        if (!vfio_user_send_wait(container->proxy, &msgp->hdr, fds,
                                 0, &local_err)) {
                error_report_err(local_err);
                ret = -EFAULT;
        }

        if (msgp->hdr.flags & VFIO_USER_ERROR) {
            ret = -msgp->hdr.error_reply;
        }

        g_free(msgp);
    }

    return ret;
}

static int
vfio_user_set_dirty_page_tracking(const VFIOContainer *bcontainer,
                                    bool start, Error **errp)
{
    error_setg_errno(errp, ENOTSUP, "Not supported");
    return -ENOTSUP;
}

static int vfio_user_query_dirty_bitmap(const VFIOContainer *bcontainer,
                                         VFIOBitmap *vbmap, hwaddr iova,
                                         hwaddr size, Error **errp)
{
    error_setg_errno(errp, ENOTSUP, "Not supported");
    return -ENOTSUP;
}

static bool vfio_user_setup(VFIOContainer *bcontainer, Error **errp)
{
    VFIOUserContainer *container = VFIO_IOMMU_USER(bcontainer);

    assert(container->proxy->dma_pgsizes != 0);
    bcontainer->pgsizes = container->proxy->dma_pgsizes;
    bcontainer->dma_max_mappings = container->proxy->max_dma;

    /* No live migration support yet. */
    bcontainer->dirty_pages_supported = false;
    bcontainer->max_dirty_bitmap_size = container->proxy->max_bitmap;
    bcontainer->dirty_pgsizes = container->proxy->migr_pgsize;

    return true;
}

static VFIOUserContainer *vfio_user_create_container(VFIODevice *vbasedev,
                                                     Error **errp)
{
    VFIOUserContainer *container;

    container = VFIO_IOMMU_USER(object_new(TYPE_VFIO_IOMMU_USER));
    container->proxy = vbasedev->proxy;
    return container;
}

/*
 * Try to mirror vfio_container_connect() as much as possible.
 */
static VFIOUserContainer *
vfio_user_container_connect(AddressSpace *as, VFIODevice *vbasedev,
                            Error **errp)
{
    VFIOContainer *bcontainer;
    VFIOUserContainer *container;
    VFIOAddressSpace *space;
    VFIOIOMMUClass *vioc;
    int ret;

    space = vfio_address_space_get(as);

    container = vfio_user_create_container(vbasedev, errp);
    if (!container) {
        goto put_space_exit;
    }

    bcontainer = VFIO_IOMMU(container);

    ret = ram_block_uncoordinated_discard_disable(true);
    if (ret) {
        error_setg_errno(errp, -ret, "Cannot set discarding of RAM broken");
        goto free_container_exit;
    }

    vioc = VFIO_IOMMU_GET_CLASS(bcontainer);
    assert(vioc->setup);

    if (!vioc->setup(bcontainer, errp)) {
        goto enable_discards_exit;
    }

    vfio_address_space_insert(space, bcontainer);

    if (!vfio_listener_register(bcontainer, errp)) {
        goto listener_release_exit;
    }

    bcontainer->initialized = true;

    return container;

listener_release_exit:
    vfio_listener_unregister(bcontainer);
    if (vioc->release) {
        vioc->release(bcontainer);
    }

enable_discards_exit:
    ram_block_uncoordinated_discard_disable(false);

free_container_exit:
    object_unref(container);

put_space_exit:
    vfio_address_space_put(space);

    return NULL;
}

static void vfio_user_container_disconnect(VFIOUserContainer *container)
{
    VFIOContainer *bcontainer = VFIO_IOMMU(container);
    VFIOIOMMUClass *vioc = VFIO_IOMMU_GET_CLASS(bcontainer);
    VFIOAddressSpace *space = bcontainer->space;

    ram_block_uncoordinated_discard_disable(false);

    vfio_listener_unregister(bcontainer);
    if (vioc->release) {
        vioc->release(bcontainer);
    }

    object_unref(container);

    vfio_address_space_put(space);
}

static bool vfio_user_device_get(VFIOUserContainer *container,
                                 VFIODevice *vbasedev, Error **errp)
{
    struct vfio_device_info info = { .argsz = sizeof(info) };


    if (!vfio_user_get_device_info(vbasedev->proxy, &info, errp)) {
        return false;
    }

    vbasedev->fd = -1;

    vfio_device_prepare(vbasedev, VFIO_IOMMU(container), &info);

    return true;
}

/*
 * vfio_user_device_attach: attach a device to a new container.
 */
static bool vfio_user_device_attach(const char *name, VFIODevice *vbasedev,
                                    AddressSpace *as, Error **errp)
{
    VFIOUserContainer *container;

    container = vfio_user_container_connect(as, vbasedev, errp);
    if (container == NULL) {
        error_prepend(errp, "failed to connect proxy");
        return false;
    }

    return vfio_user_device_get(container, vbasedev, errp);
}

static void vfio_user_device_detach(VFIODevice *vbasedev)
{
    VFIOUserContainer *container = VFIO_IOMMU_USER(vbasedev->bcontainer);

    vfio_device_unprepare(vbasedev);

    vfio_user_container_disconnect(container);
}

static int vfio_user_pci_hot_reset(VFIODevice *vbasedev, bool single)
{
    /* ->needs_reset is always false for vfio-user. */
    return 0;
}

static void vfio_iommu_user_class_init(ObjectClass *klass, const void *data)
{
    VFIOIOMMUClass *vioc = VFIO_IOMMU_CLASS(klass);

    vioc->setup = vfio_user_setup;
    vioc->listener_begin = vfio_user_listener_begin,
    vioc->listener_commit = vfio_user_listener_commit,
    vioc->dma_map = vfio_user_dma_map;
    vioc->dma_unmap = vfio_user_dma_unmap;
    vioc->attach_device = vfio_user_device_attach;
    vioc->detach_device = vfio_user_device_detach;
    vioc->set_dirty_page_tracking = vfio_user_set_dirty_page_tracking;
    vioc->query_dirty_bitmap = vfio_user_query_dirty_bitmap;
    vioc->pci_hot_reset = vfio_user_pci_hot_reset;
};

static const TypeInfo types[] = {
    {
        .name = TYPE_VFIO_IOMMU_USER,
        .parent = TYPE_VFIO_IOMMU,
        .instance_size = sizeof(VFIOUserContainer),
        .class_init = vfio_iommu_user_class_init,
    },
};

DEFINE_TYPES(types)
