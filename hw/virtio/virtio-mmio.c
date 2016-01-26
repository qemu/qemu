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

/* Memory mapped register offsets */
#define VIRTIO_MMIO_MAGIC 0x0
#define VIRTIO_MMIO_VERSION 0x4
#define VIRTIO_MMIO_DEVICEID 0x8
#define VIRTIO_MMIO_VENDORID 0xc
#define VIRTIO_MMIO_HOSTFEATURES 0x10
#define VIRTIO_MMIO_HOSTFEATURESSEL 0x14
#define VIRTIO_MMIO_GUESTFEATURES 0x20
#define VIRTIO_MMIO_GUESTFEATURESSEL 0x24
#define VIRTIO_MMIO_GUESTPAGESIZE 0x28
#define VIRTIO_MMIO_QUEUESEL 0x30
#define VIRTIO_MMIO_QUEUENUMMAX 0x34
#define VIRTIO_MMIO_QUEUENUM 0x38
#define VIRTIO_MMIO_QUEUEALIGN 0x3c
#define VIRTIO_MMIO_QUEUEPFN 0x40
#define VIRTIO_MMIO_QUEUENOTIFY 0x50
#define VIRTIO_MMIO_INTERRUPTSTATUS 0x60
#define VIRTIO_MMIO_INTERRUPTACK 0x64
#define VIRTIO_MMIO_STATUS 0x70
/* Device specific config space starts here */
#define VIRTIO_MMIO_CONFIG 0x100

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
    bool ioeventfd_disabled;
    bool ioeventfd_started;
} VirtIOMMIOProxy;

static int virtio_mmio_set_host_notifier_internal(VirtIOMMIOProxy *proxy,
                                                  int n, bool assign,
                                                  bool set_handler)
{
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    VirtQueue *vq = virtio_get_queue(vdev, n);
    EventNotifier *notifier = virtio_queue_get_host_notifier(vq);
    int r = 0;

    if (assign) {
        r = event_notifier_init(notifier, 1);
        if (r < 0) {
            error_report("%s: unable to init event notifier: %d",
                         __func__, r);
            return r;
        }
        virtio_queue_set_host_notifier_fd_handler(vq, true, set_handler);
        memory_region_add_eventfd(&proxy->iomem, VIRTIO_MMIO_QUEUENOTIFY, 4,
                                  true, n, notifier);
    } else {
        memory_region_del_eventfd(&proxy->iomem, VIRTIO_MMIO_QUEUENOTIFY, 4,
                                  true, n, notifier);
        virtio_queue_set_host_notifier_fd_handler(vq, false, false);
        event_notifier_cleanup(notifier);
    }
    return r;
}

static void virtio_mmio_start_ioeventfd(VirtIOMMIOProxy *proxy)
{
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    int n, r;

    if (!kvm_eventfds_enabled() ||
        proxy->ioeventfd_disabled ||
        proxy->ioeventfd_started) {
        return;
    }

    for (n = 0; n < VIRTIO_QUEUE_MAX; n++) {
        if (!virtio_queue_get_num(vdev, n)) {
            continue;
        }

        r = virtio_mmio_set_host_notifier_internal(proxy, n, true, true);
        if (r < 0) {
            goto assign_error;
        }
    }
    proxy->ioeventfd_started = true;
    return;

assign_error:
    while (--n >= 0) {
        if (!virtio_queue_get_num(vdev, n)) {
            continue;
        }

        r = virtio_mmio_set_host_notifier_internal(proxy, n, false, false);
        assert(r >= 0);
    }
    proxy->ioeventfd_started = false;
    error_report("%s: failed. Fallback to a userspace (slower).", __func__);
}

static void virtio_mmio_stop_ioeventfd(VirtIOMMIOProxy *proxy)
{
    int r;
    int n;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    if (!proxy->ioeventfd_started) {
        return;
    }

    for (n = 0; n < VIRTIO_QUEUE_MAX; n++) {
        if (!virtio_queue_get_num(vdev, n)) {
            continue;
        }

        r = virtio_mmio_set_host_notifier_internal(proxy, n, false, false);
        assert(r >= 0);
    }
    proxy->ioeventfd_started = false;
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
        case VIRTIO_MMIO_MAGIC:
            return VIRT_MAGIC;
        case VIRTIO_MMIO_VERSION:
            return VIRT_VERSION;
        case VIRTIO_MMIO_VENDORID:
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
    case VIRTIO_MMIO_MAGIC:
        return VIRT_MAGIC;
    case VIRTIO_MMIO_VERSION:
        return VIRT_VERSION;
    case VIRTIO_MMIO_DEVICEID:
        return vdev->device_id;
    case VIRTIO_MMIO_VENDORID:
        return VIRT_VENDOR;
    case VIRTIO_MMIO_HOSTFEATURES:
        if (proxy->host_features_sel) {
            return 0;
        }
        return vdev->host_features;
    case VIRTIO_MMIO_QUEUENUMMAX:
        if (!virtio_queue_get_num(vdev, vdev->queue_sel)) {
            return 0;
        }
        return VIRTQUEUE_MAX_SIZE;
    case VIRTIO_MMIO_QUEUEPFN:
        return virtio_queue_get_addr(vdev, vdev->queue_sel)
            >> proxy->guest_page_shift;
    case VIRTIO_MMIO_INTERRUPTSTATUS:
        return vdev->isr;
    case VIRTIO_MMIO_STATUS:
        return vdev->status;
    case VIRTIO_MMIO_HOSTFEATURESSEL:
    case VIRTIO_MMIO_GUESTFEATURES:
    case VIRTIO_MMIO_GUESTFEATURESSEL:
    case VIRTIO_MMIO_GUESTPAGESIZE:
    case VIRTIO_MMIO_QUEUESEL:
    case VIRTIO_MMIO_QUEUENUM:
    case VIRTIO_MMIO_QUEUEALIGN:
    case VIRTIO_MMIO_QUEUENOTIFY:
    case VIRTIO_MMIO_INTERRUPTACK:
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
    case VIRTIO_MMIO_HOSTFEATURESSEL:
        proxy->host_features_sel = value;
        break;
    case VIRTIO_MMIO_GUESTFEATURES:
        if (!proxy->guest_features_sel) {
            virtio_set_features(vdev, value);
        }
        break;
    case VIRTIO_MMIO_GUESTFEATURESSEL:
        proxy->guest_features_sel = value;
        break;
    case VIRTIO_MMIO_GUESTPAGESIZE:
        proxy->guest_page_shift = ctz32(value);
        if (proxy->guest_page_shift > 31) {
            proxy->guest_page_shift = 0;
        }
        DPRINTF("guest page size %" PRIx64 " shift %d\n", value,
                proxy->guest_page_shift);
        break;
    case VIRTIO_MMIO_QUEUESEL:
        if (value < VIRTIO_QUEUE_MAX) {
            vdev->queue_sel = value;
        }
        break;
    case VIRTIO_MMIO_QUEUENUM:
        DPRINTF("mmio_queue write %d max %d\n", (int)value, VIRTQUEUE_MAX_SIZE);
        virtio_queue_set_num(vdev, vdev->queue_sel, value);
        /* Note: only call this function for legacy devices */
        virtio_queue_update_rings(vdev, vdev->queue_sel);
        break;
    case VIRTIO_MMIO_QUEUEALIGN:
        /* Note: this is only valid for legacy devices */
        virtio_queue_set_align(vdev, vdev->queue_sel, value);
        break;
    case VIRTIO_MMIO_QUEUEPFN:
        if (value == 0) {
            virtio_reset(vdev);
        } else {
            virtio_queue_set_addr(vdev, vdev->queue_sel,
                                  value << proxy->guest_page_shift);
        }
        break;
    case VIRTIO_MMIO_QUEUENOTIFY:
        if (value < VIRTIO_QUEUE_MAX) {
            virtio_queue_notify(vdev, value);
        }
        break;
    case VIRTIO_MMIO_INTERRUPTACK:
        vdev->isr &= ~value;
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
    case VIRTIO_MMIO_MAGIC:
    case VIRTIO_MMIO_VERSION:
    case VIRTIO_MMIO_DEVICEID:
    case VIRTIO_MMIO_VENDORID:
    case VIRTIO_MMIO_HOSTFEATURES:
    case VIRTIO_MMIO_QUEUENUMMAX:
    case VIRTIO_MMIO_INTERRUPTSTATUS:
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
    level = (vdev->isr != 0);
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

    if (vdc->guest_notifier_mask) {
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

static int virtio_mmio_set_host_notifier(DeviceState *opaque, int n,
                                         bool assign)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(opaque);

    /* Stop using ioeventfd for virtqueue kick if the device starts using host
     * notifiers.  This makes it easy to avoid stepping on each others' toes.
     */
    proxy->ioeventfd_disabled = assign;
    if (assign) {
        virtio_mmio_stop_ioeventfd(proxy);
    }
    /* We don't need to start here: it's not needed because backend
     * currently only stops on status change away from ok,
     * reset, vmstop and such. If we do add code to start here,
     * need to check vmstate, device state etc. */
    return virtio_mmio_set_host_notifier_internal(proxy, n, assign, false);
}

/* virtio-mmio device */

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
}

static const TypeInfo virtio_mmio_info = {
    .name          = TYPE_VIRTIO_MMIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VirtIOMMIOProxy),
    .class_init    = virtio_mmio_class_init,
};

/* virtio-mmio-bus. */

static void virtio_mmio_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *bus_class = BUS_CLASS(klass);
    VirtioBusClass *k = VIRTIO_BUS_CLASS(klass);

    k->notify = virtio_mmio_update_irq;
    k->save_config = virtio_mmio_save_config;
    k->load_config = virtio_mmio_load_config;
    k->set_host_notifier = virtio_mmio_set_host_notifier;
    k->set_guest_notifiers = virtio_mmio_set_guest_notifiers;
    k->has_variable_vring_alignment = true;
    bus_class->max_dev = 1;
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
