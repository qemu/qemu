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
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/virtio/virtio.h"
#include "migration/qemu-file-types.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "sysemu/kvm.h"
#include "sysemu/replay.h"
#include "hw/virtio/virtio-mmio.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "trace.h"

static bool virtio_mmio_ioeventfd_enabled(DeviceState *d)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(d);

    return (proxy->flags & VIRTIO_IOMMIO_FLAG_USE_IOEVENTFD) != 0;
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

static void virtio_mmio_soft_reset(VirtIOMMIOProxy *proxy)
{
    int i;

    if (proxy->legacy) {
        return;
    }

    for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
        proxy->vqs[i].enabled = 0;
    }
}

static uint64_t virtio_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
    VirtIOMMIOProxy *proxy = (VirtIOMMIOProxy *)opaque;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    trace_virtio_mmio_read(offset);

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
            if (proxy->legacy) {
                return VIRT_VERSION_LEGACY;
            } else {
                return VIRT_VERSION;
            }
        case VIRTIO_MMIO_VENDOR_ID:
            return VIRT_VENDOR;
        default:
            return 0;
        }
    }

    if (offset >= VIRTIO_MMIO_CONFIG) {
        offset -= VIRTIO_MMIO_CONFIG;
        if (proxy->legacy) {
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
        } else {
            switch (size) {
            case 1:
                return virtio_config_modern_readb(vdev, offset);
            case 2:
                return virtio_config_modern_readw(vdev, offset);
            case 4:
                return virtio_config_modern_readl(vdev, offset);
            default:
                abort();
            }
        }
    }
    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: wrong size access to register!\n",
                      __func__);
        return 0;
    }
    switch (offset) {
    case VIRTIO_MMIO_MAGIC_VALUE:
        return VIRT_MAGIC;
    case VIRTIO_MMIO_VERSION:
        if (proxy->legacy) {
            return VIRT_VERSION_LEGACY;
        } else {
            return VIRT_VERSION;
        }
    case VIRTIO_MMIO_DEVICE_ID:
        return vdev->device_id;
    case VIRTIO_MMIO_VENDOR_ID:
        return VIRT_VENDOR;
    case VIRTIO_MMIO_DEVICE_FEATURES:
        if (proxy->legacy) {
            if (proxy->host_features_sel) {
                return 0;
            } else {
                return vdev->host_features;
            }
        } else {
            VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);
            return (vdev->host_features & ~vdc->legacy_features)
                >> (32 * proxy->host_features_sel);
        }
    case VIRTIO_MMIO_QUEUE_NUM_MAX:
        if (!virtio_queue_get_num(vdev, vdev->queue_sel)) {
            return 0;
        }
        return VIRTQUEUE_MAX_SIZE;
    case VIRTIO_MMIO_QUEUE_PFN:
        if (!proxy->legacy) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: read from legacy register (0x%"
                          HWADDR_PRIx ") in non-legacy mode\n",
                          __func__, offset);
            return 0;
        }
        return virtio_queue_get_addr(vdev, vdev->queue_sel)
            >> proxy->guest_page_shift;
    case VIRTIO_MMIO_QUEUE_READY:
        if (proxy->legacy) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: read from non-legacy register (0x%"
                          HWADDR_PRIx ") in legacy mode\n",
                          __func__, offset);
            return 0;
        }
        return proxy->vqs[vdev->queue_sel].enabled;
    case VIRTIO_MMIO_INTERRUPT_STATUS:
        return qatomic_read(&vdev->isr);
    case VIRTIO_MMIO_STATUS:
        return vdev->status;
    case VIRTIO_MMIO_CONFIG_GENERATION:
        if (proxy->legacy) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: read from non-legacy register (0x%"
                          HWADDR_PRIx ") in legacy mode\n",
                          __func__, offset);
            return 0;
        }
        return vdev->generation;
   case VIRTIO_MMIO_SHM_LEN_LOW:
   case VIRTIO_MMIO_SHM_LEN_HIGH:
        /*
         * VIRTIO_MMIO_SHM_SEL is unimplemented
         * according to the linux driver, if region length is -1
         * the shared memory doesn't exist
         */
        return -1;
    case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
    case VIRTIO_MMIO_DRIVER_FEATURES:
    case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
    case VIRTIO_MMIO_GUEST_PAGE_SIZE:
    case VIRTIO_MMIO_QUEUE_SEL:
    case VIRTIO_MMIO_QUEUE_NUM:
    case VIRTIO_MMIO_QUEUE_ALIGN:
    case VIRTIO_MMIO_QUEUE_NOTIFY:
    case VIRTIO_MMIO_INTERRUPT_ACK:
    case VIRTIO_MMIO_QUEUE_DESC_LOW:
    case VIRTIO_MMIO_QUEUE_DESC_HIGH:
    case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
    case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
    case VIRTIO_MMIO_QUEUE_USED_LOW:
    case VIRTIO_MMIO_QUEUE_USED_HIGH:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read of write-only register (0x%" HWADDR_PRIx ")\n",
                      __func__, offset);
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad register offset (0x%" HWADDR_PRIx ")\n",
                      __func__, offset);
        return 0;
    }
    return 0;
}

static void virtio_mmio_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    VirtIOMMIOProxy *proxy = (VirtIOMMIOProxy *)opaque;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    trace_virtio_mmio_write_offset(offset, value);

    if (!vdev) {
        /* If no backend is present, we just make all registers
         * write-ignored. This allows us to provide transports with
         * no backend plugged in.
         */
        return;
    }

    if (offset >= VIRTIO_MMIO_CONFIG) {
        offset -= VIRTIO_MMIO_CONFIG;
        if (proxy->legacy) {
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
        } else {
            switch (size) {
            case 1:
                virtio_config_modern_writeb(vdev, offset, value);
                break;
            case 2:
                virtio_config_modern_writew(vdev, offset, value);
                break;
            case 4:
                virtio_config_modern_writel(vdev, offset, value);
                break;
            default:
                abort();
            }
            return;
        }
    }
    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: wrong size access to register!\n",
                      __func__);
        return;
    }
    switch (offset) {
    case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
        if (value) {
            proxy->host_features_sel = 1;
        } else {
            proxy->host_features_sel = 0;
        }
        break;
    case VIRTIO_MMIO_DRIVER_FEATURES:
        if (proxy->legacy) {
            if (proxy->guest_features_sel) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: attempt to write guest features with "
                              "guest_features_sel > 0 in legacy mode\n",
                              __func__);
            } else {
                virtio_set_features(vdev, value);
            }
        } else {
            proxy->guest_features[proxy->guest_features_sel] = value;
        }
        break;
    case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
        if (value) {
            proxy->guest_features_sel = 1;
        } else {
            proxy->guest_features_sel = 0;
        }
        break;
    case VIRTIO_MMIO_GUEST_PAGE_SIZE:
        if (!proxy->legacy) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to legacy register (0x%"
                          HWADDR_PRIx ") in non-legacy mode\n",
                          __func__, offset);
            return;
        }
        proxy->guest_page_shift = ctz32(value);
        if (proxy->guest_page_shift > 31) {
            proxy->guest_page_shift = 0;
        }
        trace_virtio_mmio_guest_page(value, proxy->guest_page_shift);
        break;
    case VIRTIO_MMIO_QUEUE_SEL:
        if (value < VIRTIO_QUEUE_MAX) {
            vdev->queue_sel = value;
        }
        break;
    case VIRTIO_MMIO_QUEUE_NUM:
        trace_virtio_mmio_queue_write(value, VIRTQUEUE_MAX_SIZE);
        virtio_queue_set_num(vdev, vdev->queue_sel, value);

        if (proxy->legacy) {
            virtio_queue_update_rings(vdev, vdev->queue_sel);
        } else {
            proxy->vqs[vdev->queue_sel].num = value;
        }
        break;
    case VIRTIO_MMIO_QUEUE_ALIGN:
        if (!proxy->legacy) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to legacy register (0x%"
                          HWADDR_PRIx ") in non-legacy mode\n",
                          __func__, offset);
            return;
        }
        virtio_queue_set_align(vdev, vdev->queue_sel, value);
        break;
    case VIRTIO_MMIO_QUEUE_PFN:
        if (!proxy->legacy) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to legacy register (0x%"
                          HWADDR_PRIx ") in non-legacy mode\n",
                          __func__, offset);
            return;
        }
        if (value == 0) {
            virtio_reset(vdev);
        } else {
            virtio_queue_set_addr(vdev, vdev->queue_sel,
                                  value << proxy->guest_page_shift);
        }
        break;
    case VIRTIO_MMIO_QUEUE_READY:
        if (proxy->legacy) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to non-legacy register (0x%"
                          HWADDR_PRIx ") in legacy mode\n",
                          __func__, offset);
            return;
        }
        if (value) {
            virtio_queue_set_num(vdev, vdev->queue_sel,
                                 proxy->vqs[vdev->queue_sel].num);
            virtio_queue_set_rings(vdev, vdev->queue_sel,
                ((uint64_t)proxy->vqs[vdev->queue_sel].desc[1]) << 32 |
                proxy->vqs[vdev->queue_sel].desc[0],
                ((uint64_t)proxy->vqs[vdev->queue_sel].avail[1]) << 32 |
                proxy->vqs[vdev->queue_sel].avail[0],
                ((uint64_t)proxy->vqs[vdev->queue_sel].used[1]) << 32 |
                proxy->vqs[vdev->queue_sel].used[0]);
            proxy->vqs[vdev->queue_sel].enabled = 1;
        } else {
            proxy->vqs[vdev->queue_sel].enabled = 0;
        }
        break;
    case VIRTIO_MMIO_QUEUE_NOTIFY:
        if (value < VIRTIO_QUEUE_MAX) {
            virtio_queue_notify(vdev, value);
        }
        break;
    case VIRTIO_MMIO_INTERRUPT_ACK:
        qatomic_and(&vdev->isr, ~value);
        virtio_update_irq(vdev);
        break;
    case VIRTIO_MMIO_STATUS:
        if (!(value & VIRTIO_CONFIG_S_DRIVER_OK)) {
            virtio_mmio_stop_ioeventfd(proxy);
        }

        if (!proxy->legacy && (value & VIRTIO_CONFIG_S_FEATURES_OK)) {
            virtio_set_features(vdev,
                                ((uint64_t)proxy->guest_features[1]) << 32 |
                                proxy->guest_features[0]);
        }

        virtio_set_status(vdev, value & 0xff);

        if (value & VIRTIO_CONFIG_S_DRIVER_OK) {
            virtio_mmio_start_ioeventfd(proxy);
        }

        if (vdev->status == 0) {
            virtio_reset(vdev);
            virtio_mmio_soft_reset(proxy);
        }
        break;
    case VIRTIO_MMIO_QUEUE_DESC_LOW:
        if (proxy->legacy) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to non-legacy register (0x%"
                          HWADDR_PRIx ") in legacy mode\n",
                          __func__, offset);
            return;
        }
        proxy->vqs[vdev->queue_sel].desc[0] = value;
        break;
    case VIRTIO_MMIO_QUEUE_DESC_HIGH:
        if (proxy->legacy) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to non-legacy register (0x%"
                          HWADDR_PRIx ") in legacy mode\n",
                          __func__, offset);
            return;
        }
        proxy->vqs[vdev->queue_sel].desc[1] = value;
        break;
    case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
        if (proxy->legacy) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to non-legacy register (0x%"
                          HWADDR_PRIx ") in legacy mode\n",
                          __func__, offset);
            return;
        }
        proxy->vqs[vdev->queue_sel].avail[0] = value;
        break;
    case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
        if (proxy->legacy) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to non-legacy register (0x%"
                          HWADDR_PRIx ") in legacy mode\n",
                          __func__, offset);
            return;
        }
        proxy->vqs[vdev->queue_sel].avail[1] = value;
        break;
    case VIRTIO_MMIO_QUEUE_USED_LOW:
        if (proxy->legacy) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to non-legacy register (0x%"
                          HWADDR_PRIx ") in legacy mode\n",
                          __func__, offset);
            return;
        }
        proxy->vqs[vdev->queue_sel].used[0] = value;
        break;
    case VIRTIO_MMIO_QUEUE_USED_HIGH:
        if (proxy->legacy) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to non-legacy register (0x%"
                          HWADDR_PRIx ") in legacy mode\n",
                          __func__, offset);
            return;
        }
        proxy->vqs[vdev->queue_sel].used[1] = value;
        break;
    case VIRTIO_MMIO_MAGIC_VALUE:
    case VIRTIO_MMIO_VERSION:
    case VIRTIO_MMIO_DEVICE_ID:
    case VIRTIO_MMIO_VENDOR_ID:
    case VIRTIO_MMIO_DEVICE_FEATURES:
    case VIRTIO_MMIO_QUEUE_NUM_MAX:
    case VIRTIO_MMIO_INTERRUPT_STATUS:
    case VIRTIO_MMIO_CONFIG_GENERATION:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only register (0x%" HWADDR_PRIx ")\n",
                      __func__, offset);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad register offset (0x%" HWADDR_PRIx ")\n",
                      __func__, offset);
    }
}

static const MemoryRegionOps virtio_legacy_mem_ops = {
    .read = virtio_mmio_read,
    .write = virtio_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps virtio_mem_ops = {
    .read = virtio_mmio_read,
    .write = virtio_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void virtio_mmio_update_irq(DeviceState *opaque, uint16_t vector)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(opaque);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    int level;

    if (!vdev) {
        return;
    }
    level = (qatomic_read(&vdev->isr) != 0);
    trace_virtio_mmio_setting_irq(level);
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

static const VMStateDescription vmstate_virtio_mmio_queue_state = {
    .name = "virtio_mmio/queue_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(num, VirtIOMMIOQueue),
        VMSTATE_BOOL(enabled, VirtIOMMIOQueue),
        VMSTATE_UINT32_ARRAY(desc, VirtIOMMIOQueue, 2),
        VMSTATE_UINT32_ARRAY(avail, VirtIOMMIOQueue, 2),
        VMSTATE_UINT32_ARRAY(used, VirtIOMMIOQueue, 2),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_mmio_state_sub = {
    .name = "virtio_mmio/state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(guest_features, VirtIOMMIOProxy, 2),
        VMSTATE_STRUCT_ARRAY(vqs, VirtIOMMIOProxy, VIRTIO_QUEUE_MAX, 0,
                             vmstate_virtio_mmio_queue_state,
                             VirtIOMMIOQueue),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_virtio_mmio = {
    .name = "virtio_mmio",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * []) {
        &vmstate_virtio_mmio_state_sub,
        NULL
    }
};

static void virtio_mmio_save_extra_state(DeviceState *opaque, QEMUFile *f)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(opaque);

    vmstate_save_state(f, &vmstate_virtio_mmio, proxy, NULL);
}

static int virtio_mmio_load_extra_state(DeviceState *opaque, QEMUFile *f)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(opaque);

    return vmstate_load_state(f, &vmstate_virtio_mmio, proxy, 1);
}

static bool virtio_mmio_has_extra_state(DeviceState *opaque)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(opaque);

    return !proxy->legacy;
}

static void virtio_mmio_reset(DeviceState *d)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(d);
    int i;

    virtio_mmio_stop_ioeventfd(proxy);
    virtio_bus_reset(&proxy->bus);
    proxy->host_features_sel = 0;
    proxy->guest_features_sel = 0;
    proxy->guest_page_shift = 0;

    if (!proxy->legacy) {
        proxy->guest_features[0] = proxy->guest_features[1] = 0;

        for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
            proxy->vqs[i].enabled = 0;
            proxy->vqs[i].num = 0;
            proxy->vqs[i].desc[0] = proxy->vqs[i].desc[1] = 0;
            proxy->vqs[i].avail[0] = proxy->vqs[i].avail[1] = 0;
            proxy->vqs[i].used[0] = proxy->vqs[i].used[1] = 0;
        }
    }
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

static void virtio_mmio_pre_plugged(DeviceState *d, Error **errp)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(d);
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);

    if (!proxy->legacy) {
        virtio_add_feature(&vdev->host_features, VIRTIO_F_VERSION_1);
    }
}

/* virtio-mmio device */

static Property virtio_mmio_properties[] = {
    DEFINE_PROP_BOOL("format_transport_address", VirtIOMMIOProxy,
                     format_transport_address, true),
    DEFINE_PROP_BOOL("force-legacy", VirtIOMMIOProxy, legacy, true),
    DEFINE_PROP_BIT("ioeventfd", VirtIOMMIOProxy, flags,
                    VIRTIO_IOMMIO_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_mmio_realizefn(DeviceState *d, Error **errp)
{
    VirtIOMMIOProxy *proxy = VIRTIO_MMIO(d);
    SysBusDevice *sbd = SYS_BUS_DEVICE(d);

    qbus_init(&proxy->bus, sizeof(proxy->bus), TYPE_VIRTIO_MMIO_BUS, d, NULL);
    sysbus_init_irq(sbd, &proxy->irq);

    if (!kvm_eventfds_enabled()) {
        proxy->flags &= ~VIRTIO_IOMMIO_FLAG_USE_IOEVENTFD;
    }

    /* fd-based ioevents can't be synchronized in record/replay */
    if (replay_mode != REPLAY_MODE_NONE) {
        proxy->flags &= ~VIRTIO_IOMMIO_FLAG_USE_IOEVENTFD;
    }

    if (proxy->legacy) {
        memory_region_init_io(&proxy->iomem, OBJECT(d),
                              &virtio_legacy_mem_ops, proxy,
                              TYPE_VIRTIO_MMIO, 0x200);
    } else {
        memory_region_init_io(&proxy->iomem, OBJECT(d),
                              &virtio_mem_ops, proxy,
                              TYPE_VIRTIO_MMIO, 0x200);
    }
    sysbus_init_mmio(sbd, &proxy->iomem);
}

static void virtio_mmio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = virtio_mmio_realizefn;
    dc->reset = virtio_mmio_reset;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, virtio_mmio_properties);
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
    char *path;
    MemoryRegionSection section;

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
    section = memory_region_find(&virtio_mmio_proxy->iomem, 0, 0x200);
    assert(section.mr);

    if (proxy_path) {
        path = g_strdup_printf("%s/virtio-mmio@" TARGET_FMT_plx, proxy_path,
                               section.offset_within_address_space);
    } else {
        path = g_strdup_printf("virtio-mmio@" TARGET_FMT_plx,
                               section.offset_within_address_space);
    }
    memory_region_unref(section.mr);

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
    k->save_extra_state = virtio_mmio_save_extra_state;
    k->load_extra_state = virtio_mmio_load_extra_state;
    k->has_extra_state = virtio_mmio_has_extra_state;
    k->set_guest_notifiers = virtio_mmio_set_guest_notifiers;
    k->ioeventfd_enabled = virtio_mmio_ioeventfd_enabled;
    k->ioeventfd_assign = virtio_mmio_ioeventfd_assign;
    k->pre_plugged = virtio_mmio_pre_plugged;
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
