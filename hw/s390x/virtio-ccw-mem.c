/*
 * virtio-mem CCW implementation
 *
 * Copyright (C) 2024 Red Hat, Inc.
 *
 * Authors:
 *  David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "virtio-ccw-mem.h"
#include "hw/mem/memory-device.h"
#include "qapi/qapi-events-machine.h"
#include "qapi/qapi-events-misc.h"

static void virtio_ccw_mem_realize(VirtioCcwDevice *ccw_dev, Error **errp)
{
    VirtIOMEMCcw *dev = VIRTIO_MEM_CCW(ccw_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    qdev_realize(vdev, BUS(&ccw_dev->bus), errp);
}

static void virtio_ccw_mem_set_addr(MemoryDeviceState *md, uint64_t addr,
                                    Error **errp)
{
    object_property_set_uint(OBJECT(md), VIRTIO_MEM_ADDR_PROP, addr, errp);
}

static uint64_t virtio_ccw_mem_get_addr(const MemoryDeviceState *md)
{
    return object_property_get_uint(OBJECT(md), VIRTIO_MEM_ADDR_PROP,
                                    &error_abort);
}

static MemoryRegion *virtio_ccw_mem_get_memory_region(MemoryDeviceState *md,
                                                      Error **errp)
{
    VirtIOMEMCcw *dev = VIRTIO_MEM_CCW(md);
    VirtIOMEM *vmem = &dev->vdev;
    VirtIOMEMClass *vmc = VIRTIO_MEM_GET_CLASS(vmem);

    return vmc->get_memory_region(vmem, errp);
}

static void virtio_ccw_mem_decide_memslots(MemoryDeviceState *md,
                                           unsigned int limit)
{
    VirtIOMEMCcw *dev = VIRTIO_MEM_CCW(md);
    VirtIOMEM *vmem = VIRTIO_MEM(&dev->vdev);
    VirtIOMEMClass *vmc = VIRTIO_MEM_GET_CLASS(vmem);

    vmc->decide_memslots(vmem, limit);
}

static unsigned int virtio_ccw_mem_get_memslots(MemoryDeviceState *md)
{
    VirtIOMEMCcw *dev = VIRTIO_MEM_CCW(md);
    VirtIOMEM *vmem = VIRTIO_MEM(&dev->vdev);
    VirtIOMEMClass *vmc = VIRTIO_MEM_GET_CLASS(vmem);

    return vmc->get_memslots(vmem);
}

static uint64_t virtio_ccw_mem_get_plugged_size(const MemoryDeviceState *md,
                                                Error **errp)
{
    return object_property_get_uint(OBJECT(md), VIRTIO_MEM_SIZE_PROP,
                                    errp);
}

static void virtio_ccw_mem_fill_device_info(const MemoryDeviceState *md,
                                            MemoryDeviceInfo *info)
{
    VirtioMEMDeviceInfo *vi = g_new0(VirtioMEMDeviceInfo, 1);
    VirtIOMEMCcw *dev = VIRTIO_MEM_CCW(md);
    VirtIOMEM *vmem = &dev->vdev;
    VirtIOMEMClass *vpc = VIRTIO_MEM_GET_CLASS(vmem);
    DeviceState *vdev = DEVICE(md);

    if (vdev->id) {
        vi->id = g_strdup(vdev->id);
    }

    /* let the real device handle everything else */
    vpc->fill_device_info(vmem, vi);

    info->u.virtio_mem.data = vi;
    info->type = MEMORY_DEVICE_INFO_KIND_VIRTIO_MEM;
}

static uint64_t virtio_ccw_mem_get_min_alignment(const MemoryDeviceState *md)
{
    return object_property_get_uint(OBJECT(md), VIRTIO_MEM_BLOCK_SIZE_PROP,
                                    &error_abort);
}

static void virtio_ccw_mem_size_change_notify(Notifier *notifier, void *data)
{
    VirtIOMEMCcw *dev = container_of(notifier, VirtIOMEMCcw,
                                         size_change_notifier);
    DeviceState *vdev = DEVICE(dev);
    char *qom_path = object_get_canonical_path(OBJECT(dev));
    const uint64_t * const size_p = data;

    qapi_event_send_memory_device_size_change(vdev->id, *size_p, qom_path);
    g_free(qom_path);
}

static void virtio_ccw_mem_unplug_request_check(VirtIOMDCcw *vmd, Error **errp)
{
    VirtIOMEMCcw *dev = VIRTIO_MEM_CCW(vmd);
    VirtIOMEM *vmem = &dev->vdev;
    VirtIOMEMClass *vpc = VIRTIO_MEM_GET_CLASS(vmem);

    vpc->unplug_request_check(vmem, errp);
}

static void virtio_ccw_mem_get_requested_size(Object *obj, Visitor *v,
                                              const char *name, void *opaque,
                                              Error **errp)
{
    VirtIOMEMCcw *dev = VIRTIO_MEM_CCW(obj);

    object_property_get(OBJECT(&dev->vdev), name, v, errp);
}

static void virtio_ccw_mem_set_requested_size(Object *obj, Visitor *v,
                                              const char *name, void *opaque,
                                              Error **errp)
{
    VirtIOMEMCcw *dev = VIRTIO_MEM_CCW(obj);
    DeviceState *vdev = DEVICE(obj);

    /*
     * If we passed virtio_ccw_mem_unplug_request_check(), making sure that
     * the requested size is 0, don't allow modifying the requested size
     * anymore, otherwise the VM might end up hotplugging memory before
     * handling the unplug request.
     */
    if (vdev->pending_deleted_event) {
        error_setg(errp, "'%s' cannot be changed if the device is in the"
                   " process of unplug", name);
        return;
    }

    object_property_set(OBJECT(&dev->vdev), name, v, errp);
}

static const Property virtio_ccw_mem_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtioCcwDevice, flags,
                    VIRTIO_CCW_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("max_revision", VirtioCcwDevice, max_rev,
                       VIRTIO_CCW_MAX_REV),
};

static void virtio_ccw_mem_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOCCWDeviceClass *k = VIRTIO_CCW_DEVICE_CLASS(klass);
    MemoryDeviceClass *mdc = MEMORY_DEVICE_CLASS(klass);
    VirtIOMDCcwClass *vmdc = VIRTIO_MD_CCW_CLASS(klass);

    k->realize = virtio_ccw_mem_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, virtio_ccw_mem_properties);

    mdc->get_addr = virtio_ccw_mem_get_addr;
    mdc->set_addr = virtio_ccw_mem_set_addr;
    mdc->get_plugged_size = virtio_ccw_mem_get_plugged_size;
    mdc->get_memory_region = virtio_ccw_mem_get_memory_region;
    mdc->decide_memslots = virtio_ccw_mem_decide_memslots;
    mdc->get_memslots = virtio_ccw_mem_get_memslots;
    mdc->fill_device_info = virtio_ccw_mem_fill_device_info;
    mdc->get_min_alignment = virtio_ccw_mem_get_min_alignment;

    vmdc->unplug_request_check = virtio_ccw_mem_unplug_request_check;
}

static void virtio_ccw_mem_instance_init(Object *obj)
{
    VirtIOMEMCcw *dev = VIRTIO_MEM_CCW(obj);
    VirtIOMEMClass *vmc;
    VirtIOMEM *vmem;

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_MEM);

    dev->size_change_notifier.notify = virtio_ccw_mem_size_change_notify;
    vmem = &dev->vdev;
    vmc = VIRTIO_MEM_GET_CLASS(vmem);
    /*
     * We never remove the notifier again, as we expect both devices to
     * disappear at the same time.
     */
    vmc->add_size_change_notifier(vmem, &dev->size_change_notifier);

    object_property_add_alias(obj, VIRTIO_MEM_BLOCK_SIZE_PROP,
                              OBJECT(&dev->vdev), VIRTIO_MEM_BLOCK_SIZE_PROP);
    object_property_add_alias(obj, VIRTIO_MEM_SIZE_PROP, OBJECT(&dev->vdev),
                              VIRTIO_MEM_SIZE_PROP);
    object_property_add(obj, VIRTIO_MEM_REQUESTED_SIZE_PROP, "size",
                        virtio_ccw_mem_get_requested_size,
                        virtio_ccw_mem_set_requested_size, NULL, NULL);
}

static const TypeInfo virtio_ccw_mem = {
    .name = TYPE_VIRTIO_MEM_CCW,
    .parent = TYPE_VIRTIO_MD_CCW,
    .instance_size = sizeof(VirtIOMEMCcw),
    .instance_init = virtio_ccw_mem_instance_init,
    .class_init = virtio_ccw_mem_class_init,
};

static void virtio_ccw_mem_register_types(void)
{
    type_register_static(&virtio_ccw_mem);
}
type_init(virtio_ccw_mem_register_types)
