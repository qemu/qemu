/*
 * Virtio MMIO bindings
 *
 * Copyright (c) 2011 Linaro Limited
 *
 * Author:
 *  Peter Maydell <peter.maydell@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "standard-headers/linux/virtio_mmio.h"
#include "hw/sysbus.h"
#include "hw/virtio/virtio.h"
#include "qemu/host-utils.h"
#include "sysemu/kvm.h"
#include "hw/virtio/virtio-bus.h"
#include "qemu/error-report.h"

/* #define DEBUG_VIRTIO_MMIO */

#ifdef DEBUG_VIRTIO_MMIO

#define DPRINTF(fmt, ...) \
do { printf("virtio_mmio: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

/* QOM macros */
/* virtio-mmio-bus */
#define TYPE_VIRTIO_MMIO_BUS "virtio-mmio-bus"
#define VIRTIO_MMIO_BUS(obj) \
        OBJECT_CHECK(VirtioBusState, (obj), TYPE_VIRTIO_MMIO_BUS)
#define VIRTIO_MMIO_BUS_GET_CLASS(obj) \
        OBJECT_GET_CLASS(VirtioBusClass, (obj), TYPE_VIRTIO_MMIO_BUS)
#define VIRTIO_MMIO_BUS_CLASS(klass) \
        OBJECT_CLASS_CHECK(VirtioBusClass, (klass), TYPE_VIRTIO_MMIO_BUS)

/* virtio-mmio */
#define TYPE_VIRTIO_MMIO "virtio-mmio"
#define VIRTIO_MMIO(obj) \
        OBJECT_CHECK(VirtIOMMIOProxy, (obj), TYPE_VIRTIO_MMIO)

#define VIRT_MAGIC 0x74726976 /* 'virt' */
#define VIRT_VERSION 1
#define VIRT_VENDOR 0x554D4551 /* 'QEMU' */

typedef struct {
    /* Generic */
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    qemu_irq irq;
    /* Guest accessible state needing migration and reset */
    uint32_t host_features_sel;
    uint32_t guest_features_sel;
    uint32_t guest_page_shift;
    /* virtio-bus */
    VirtioBusState bus;
    bool format_transport_address;
} VirtIOMMIOProxy;

static bool virtio_mmio_ioeventfd_enabled(DeviceState *d)
{
    return kvm_eventfds_enabled();
}

static int virtio_mmio_ioeventfd_assign(DeviceState *d,
                                        EventNotifier *notifier,
                                        int n, bool assign)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(d);

    if (assign) {
        memory_region_add_eventfd(&proxy->iomem, VIRTIO_MMIO_QUEUE_NOTIFY, 4,
                                  true, n, notifier);
    } else {
        memory_region_del_eventfd(&proxy->iomem, VIRTIO_MMIO_QUEUE_NOTIFY, 4,
                                  true, n, notifier);
    }
    return 0;
}

static void virtio_mmio_start_ioeventfd(VirtIOMMIOProxy *proxy)
{
    virtio_bus_start_ioeventfd(&proxy->bus);
}

static void virtio_mmio_stop_ioeventfd(VirtIOMMIOProxy *proxy)
{
    virtio_bus_stop_ioeventfd(&proxy->bus);
}

static uint64_t virtio_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    VirtIOMMIOProxy *proxy = (VirtIOMMIOProxy *)opaque;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    DPRINTF("virtio_mmio_read offset 0x%x\n", (int)offset);

    if (!vdev) {
        /* If no backend is present, we treat most registers as
         * read-as-zero, except for the magic number, version and
         * vendor ID. This is not strictly sanctioned by the virtio
         * spec, but it allows us to provide transports with no backend
         * plugged in which don't confuse Linux's virtio code: the
         * probe won't complain about the bad magic number, but the
         * device ID of zero means no backend will claim it.
         */
        switch (offset) {
        case VIRTIO_MMIO_MAGIC_VALUE:
            return VIRT_MAGIC;
        case VIRTIO_MMIO_VERSION:
            return VIRT_VERSION;
        case VIRTIO_MMIO_VENDOR_ID:
            return VIRT_VENDOR;
        default:
            return 0;
        }
    }

    if (offset >= VIRTIO_MMIO_CONFIG) {
        offset -= VIRTIO_MMIO_CONFIG;
        switch (size) {
        case 1:
            return virtio_config_readb(vdev, offset);
        case 2:
            return virtio_config_readw(vdev, offset);
        case 4:
            return virtio_config_readl(vdev, offset);
        default:
            abort();
        }
    }
    if (size != 4) {
        DPRINTF("wrong size access to register!\n");
        return 0;
    }
    switch (offset) {
    case VIRTIO_MMIO_MAGIC_VALUE:
        return VIRT_MAGIC;
    case VIRTIO_MMIO_VERSION:
        return VIRT_VERSION;
    case VIRTIO_MMIO_DEVICE_ID:
        return vdev->device_id;
    case VIRTIO_MMIO_VENDOR_ID:
        return VIRT_VENDOR;
    case VIRTIO_MMIO_DEVICE_FEATURES:
        if (proxy->host_features_sel) {
            return 0;
        }
        return vdev->host_features;
    case VIRTIO_MMIO_QUEUE_NUM_MAX:
        if (!virtio_queue_get_num(vdev, vdev->queue_sel)) {
            return 0;
        }
        return VIRTQUEUE_MAX_SIZE;
    case VIRTIO_MMIO_QUEUE_PFN:
        return virtio_queue_get_addr(vdev, vdev->queue_sel)
            >> proxy->guest_page_shift;
    case VIRTIO_MMIO_INTERRUPT_STATUS:
        return atomic_read(&vdev->isr);
    case VIRTIO_MMIO_STATUS:
        return vdev->status;
    case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
    case VIRTIO_MMIO_DRIVER_FEATURES:
    case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
    case VIRTIO_MMIO_GUEST_PAGE_SIZE:
    case VIRTIO_MMIO_QUEUE_SEL:
    case VIRTIO_MMIO_QUEUE_NUM:
    case VIRTIO_MMIO_QUEUE_ALIGN:
    case VIRTIO_MMIO_QUEUE_NOTIFY:
    case VIRTIO_MMIO_INTERRUPT_ACK:
        DPRINTF("read of write-only register\n");
        return 0;
    default:
        DPRINTF("bad register offset\n");
        return 0;
    }
    return 0;
}

static void virtio_mmio_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    VirtIOMMIOProxy *proxy = (VirtIOMMIOProxy *)opaque;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    DPRINTF("virtio_mmio_write offset 0x%x value 0x%" PRIx64 "\n",
            (int)offset, value);

    if (!vdev) {
        /* If no backend is present, we just make all registers
         * write-ignored. This allows us to provide transports with
         * no backend plugged in.
         */
        return;
    }

    if (offset >= VIRTIO_MMIO_CONFIG) {
        offset -= VIRTIO_MMIO_CONFIG;
        switch (size) {
        case 1:
            virtio_config_writeb(vdev, offset, value);
            break;
        case 2:
            virtio_config_writew(vdev, offset, value);
            break;
        case 4:
            virtio_config_writel(vdev, offset, value);
            break;
        default:
            abort();
        }
        return;
    }
    if (size != 4) {
        DPRINTF("wrong size access to register!\n");
        return;
    }
    switch (offset) {
    case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
        proxy->host_features_sel = value;
        break;
    case VIRTIO_MMIO_DRIVER_FEATURES:
        if (!proxy->guest_features_sel) {
            virtio_set_features(vdev, value);
        }
        break;
    case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
        proxy->guest_features_sel = value;
        break;
    case VIRTIO_MMIO_GUEST_PAGE_SIZE:
        proxy->guest_page_shift = ctz32(value);
        if (proxy->guest_page_shift > 31) {
            proxy->guest_page_shift = 0;
        }
        DPRINTF("guest page size %" PRIx64 " shift %d\n", value,
                proxy->guest_page_shift);
        break;
    case VIRTIO_MMIO_QUEUE_SEL:
        if (value < VIRTIO_QUEUE_MAX) {
            vdev->queue_sel = value;
        }
        break;
    case VIRTIO_MMIO_QUEUE_NUM:
        DPRINTF("mmio_queue write %d max %d\n", (int)value, VIRTQUEUE_MAX_SIZE);
        virtio_queue_set_num(vdev, vdev->queue_sel, value);
        /* Note: only call this function for legacy devices */
        virtio_queue_update_rings(vdev, vdev->queue_sel);
        break;
    case VIRTIO_MMIO_QUEUE_ALIGN:
        /* Note: this is only valid for legacy devices */
        virtio_queue_set_align(vdev, vdev->queue_sel, value);
        break;
    case VIRTIO_MMIO_QUEUE_PFN:
        if (value == 0) {
            virtio_reset(vdev);
        } else {
            virtio_queue_set_addr(vdev, vdev->queue_sel,
                                  value << proxy->guest_page_shift);
        }
        break;
    case VIRTIO_MMIO_QUEUE_NOTIFY:
        if (value < VIRTIO_QUEUE_MAX) {
            virtio_queue_notify(vdev, value);
        }
        break;
    case VIRTIO_MMIO_INTERRUPT_ACK:
        atomic_and(&vdev->isr, ~value);
        virtio_update_irq(vdev);
        break;
    case VIRTIO_MMIO_STATUS:
        if (!(value & VIRTIO_CONFIG_S_DRIVER_OK)) {
            virtio_mmio_stop_ioeventfd(proxy);
        }

        virtio_set_status(vdev, value & 0xff);

        if (value & VIRTIO_CONFIG_S_DRIVER_OK) {
            virtio_mmio_start_ioeventfd(proxy);
        }

        if (vdev->status == 0) {
            virtio_reset(vdev);
        }
        break;
    case VIRTIO_MMIO_MAGIC_VALUE:
    case VIRTIO_MMIO_VERSION:
    case VIRTIO_MMIO_DEVICE_ID:
    case VIRTIO_MMIO_VENDOR_ID:
    case VIRTIO_MMIO_DEVICE_FEATURES:
    case VIRTIO_MMIO_QUEUE_NUM_MAX:
    case VIRTIO_MMIO_INTERRUPT_STATUS:
        DPRINTF("write to readonly register\n");
        break;

    default:
        DPRINTF("bad register offset\n");
    }
}

static const MemoryRegionOps virtio_mem_ops = {
    .read = virtio_mmio_read,
    .write = virtio_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void virtio_mmio_update_irq(DeviceState *opaque, uint16_t vector)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(opaque);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    int level;

    if (!vdev) {
        return;
    }
    level = (atomic_read(&vdev->isr) != 0);
    DPRINTF("virtio_mmio setting IRQ %d\n", level);
    qemu_set_irq(proxy->irq, level);
}

static int virtio_mmio_load_config(DeviceState *opaque, QEMUFile *f)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(opaque);

    proxy->host_features_sel = qemu_get_be32(f);
    proxy->guest_features_sel = qemu_get_be32(f);
    proxy->guest_page_shift = qemu_get_be32(f);
    return 0;
}

static void virtio_mmio_save_config(DeviceState *opaque, QEMUFile *f)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(opaque);

    qemu_put_be32(f, proxy->host_features_sel);
    qemu_put_be32(f, proxy->guest_features_sel);
    qemu_put_be32(f, proxy->guest_page_shift);
}

static void virtio_mmio_reset(DeviceState *d)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(d);

    virtio_mmio_stop_ioeventfd(proxy);
    virtio_bus_reset(&proxy->bus);
    proxy->host_features_sel = 0;
    proxy->guest_features_sel = 0;
    proxy->guest_page_shift = 0;
}

static int virtio_mmio_set_guest_notifier(DeviceState *d, int n, bool assign,
                                          bool with_irqfd)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(d);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);
    VirtQueue *vq = virtio_get_queue(vdev, n);
    EventNotifier *notifier = virtio_queue_get_guest_notifier(vq);

    if (assign) {
        int r = event_notifier_init(notifier, 0);
        if (r < 0) {
            return r;
        }
        virtio_queue_set_guest_notifier_fd_handler(vq, true, with_irqfd);
    } else {
        virtio_queue_set_guest_notifier_fd_handler(vq, false, with_irqfd);
        event_notifier_cleanup(notifier);
    }

    if (vdc->guest_notifier_mask && vdev->use_guest_notifier_mask) {
        vdc->guest_notifier_mask(vdev, n, !assign);
    }

    return 0;
}

static int virtio_mmio_set_guest_notifiers(DeviceState *d, int nvqs,
                                           bool assign)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(d);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    /* TODO: need to check if kvm-arm supports irqfd */
    bool with_irqfd = false;
    int r, n;

    nvqs = MIN(nvqs, VIRTIO_QUEUE_MAX);

    for (n = 0; n < nvqs; n++) {
        if (!virtio_queue_get_num(vdev, n)) {
            break;
        }

        r = virtio_mmio_set_guest_notifier(d, n, assign, with_irqfd);
        if (r < 0) {
            goto assign_error;
        }
    }

    return 0;

assign_error:
    /* We get here on assignment failure. Recover by undoing for VQs 0 .. n. */
    assert(assign);
    while (--n >= 0) {
        virtio_mmio_set_guest_notifier(d, n, !assign, false);
    }
    return r;
}

/* virtio-mmio device */

static Property virtio_mmio_properties[] = {
    DEFINE_PROP_BOOL("format_transport_address", VirtIOMMIOProxy,
                     format_transport_address, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_mmio_realizefn(DeviceState *d, Error **errp)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(d);
    SysBusDevice *sbd = SYS_BUS_DEVICE(d);

    qbus_create_inplace(&proxy->bus, sizeof(proxy->bus), TYPE_VIRTIO_MMIO_BUS,
                        d, NULL);
    sysbus_init_irq(sbd, &proxy->irq);
    memory_region_init_io(&proxy->iomem, OBJECT(d), &virtio_mem_ops, proxy,
                          TYPE_VIRTIO_MMIO, 0x200);
    sysbus_init_mmio(sbd, &proxy->iomem);
}

static void virtio_mmio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = virtio_mmio_realizefn;
    dc->reset = virtio_mmio_reset;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->props = virtio_mmio_properties;
}

static const TypeInfo virtio_mmio_info = {
    .name          = TYPE_VIRTIO_MMIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VirtIOMMIOProxy),
    .class_init    = virtio_mmio_class_init,
};

/* virtio-mmio-bus. */

static char *virtio_mmio_bus_get_dev_path(DeviceState *dev)
{
    BusState *virtio_mmio_bus;
    VirtIOMMIOProxy *virtio_mmio_proxy;
    char *proxy_path;
    SysBusDevice *proxy_sbd;
    char *path;

    virtio_mmio_bus = qdev_get_parent_bus(dev);
    virtio_mmio_proxy = VIRTIO_MMIO(virtio_mmio_bus->parent);
    proxy_path = qdev_get_dev_path(DEVICE(virtio_mmio_proxy));

    /*
     * If @format_transport_address is false, then we just perform the same as
     * virtio_bus_get_dev_path(): we delegate the address formatting for the
     * device on the virtio-mmio bus to the bus that the virtio-mmio proxy
     * (i.e., the device that implements the virtio-mmio bus) resides on. In
     * this case the base address of the virtio-mmio transport will be
     * invisible.
     */
    if (!virtio_mmio_proxy->format_transport_address) {
        return proxy_path;
    }

    /* Otherwise, we append the base address of the transport. */
    proxy_sbd = SYS_BUS_DEVICE(virtio_mmio_proxy);
    assert(proxy_sbd->num_mmio == 1);
    assert(proxy_sbd->mmio[0].memory == &virtio_mmio_proxy->iomem);

    if (proxy_path) {
        path = g_strdup_printf("%s/virtio-mmio@" TARGET_FMT_plx, proxy_path,
                               proxy_sbd->mmio[0].addr);
    } else {
        path = g_strdup_printf("virtio-mmio@" TARGET_FMT_plx,
                               proxy_sbd->mmio[0].addr);
    }
    g_free(proxy_path);
    return path;
}

static void virtio_mmio_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *bus_class = BUS_CLASS(klass);
    VirtioBusClass *k = VIRTIO_BUS_CLASS(klass);

    k->notify = virtio_mmio_update_irq;
    k->save_config = virtio_mmio_save_config;
    k->load_config = virtio_mmio_load_config;
    k->set_guest_notifiers = virtio_mmio_set_guest_notifiers;
    k->ioeventfd_enabled = virtio_mmio_ioeventfd_enabled;
    k->ioeventfd_assign = virtio_mmio_ioeventfd_assign;
    k->has_variable_vring_alignment = true;
    bus_class->max_dev = 1;
    bus_class->get_dev_path = virtio_mmio_bus_get_dev_path;
}

static const TypeInfo virtio_mmio_bus_info = {
    .name          = TYPE_VIRTIO_MMIO_BUS,
    .parent        = TYPE_VIRTIO_BUS,
    .instance_size = sizeof(VirtioBusState),
    .class_init    = virtio_mmio_bus_class_init,
};

static void virtio_mmio_register_types(void)
{
    type_register_static(&virtio_mmio_bus_info);
    type_register_static(&virtio_mmio_info);
}

type_init(virtio_mmio_register_types)
