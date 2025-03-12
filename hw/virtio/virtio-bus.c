/*
 * VirtioBus
 *
 *  Copyright (C) 2012 : GreenSocs Ltd
 *      http://www.greensocs.com/ , email: info@greensocs.com
 *
 *  Developed by :
 *  Frederic Konrad   <fred.konrad@greensocs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio.h"
#include "system/address-spaces.h"

/* #define DEBUG_VIRTIO_BUS */

#ifdef DEBUG_VIRTIO_BUS
#define DPRINTF(fmt, ...) \
do { printf("virtio_bus: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

/* A VirtIODevice is being plugged */
void virtio_bus_device_plugged(VirtIODevice *vdev, Error **errp)
{
    DeviceState *qdev = DEVICE(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(qdev));
    VirtioBusState *bus = VIRTIO_BUS(qbus);
    VirtioBusClass *klass = VIRTIO_BUS_GET_CLASS(bus);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);
    bool has_iommu = virtio_host_has_feature(vdev, VIRTIO_F_IOMMU_PLATFORM);
    bool vdev_has_iommu;
    Error *local_err = NULL;

    DPRINTF("%s: plug device.\n", qbus->name);

    if (klass->pre_plugged != NULL) {
        klass->pre_plugged(qbus->parent, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    /* Get the features of the plugged device. */
    assert(vdc->get_features != NULL);
    vdev->host_features = vdc->get_features(vdev, vdev->host_features,
                                            &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (klass->device_plugged != NULL) {
        klass->device_plugged(qbus->parent, &local_err);
    }
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    vdev->dma_as = &address_space_memory;
    if (has_iommu) {
        vdev_has_iommu = virtio_host_has_feature(vdev, VIRTIO_F_IOMMU_PLATFORM);
        /*
         * Present IOMMU_PLATFORM to the driver iff iommu_plattform=on and
         * device operational. If the driver does not accept IOMMU_PLATFORM
         * we fail the device.
         */
        virtio_add_feature(&vdev->host_features, VIRTIO_F_IOMMU_PLATFORM);
        if (klass->get_dma_as) {
            vdev->dma_as = klass->get_dma_as(qbus->parent);
            if (!vdev_has_iommu && vdev->dma_as != &address_space_memory) {
                error_setg(errp,
                       "iommu_platform=true is not supported by the device");
                return;
            }
        }
    }
}

/* Reset the virtio_bus */
void virtio_bus_reset(VirtioBusState *bus)
{
    VirtIODevice *vdev = virtio_bus_get_device(bus);

    DPRINTF("%s: reset device.\n", BUS(bus)->name);
    virtio_bus_stop_ioeventfd(bus);
    if (vdev != NULL) {
        virtio_reset(vdev);
    }
}

/* A VirtIODevice is being unplugged */
void virtio_bus_device_unplugged(VirtIODevice *vdev)
{
    DeviceState *qdev = DEVICE(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(qdev));
    VirtioBusClass *klass = VIRTIO_BUS_GET_CLASS(qbus);

    DPRINTF("%s: remove device.\n", qbus->name);

    if (vdev != NULL) {
        if (klass->device_unplugged != NULL) {
            klass->device_unplugged(qbus->parent);
        }
    }
}

/* Get the device id of the plugged device. */
uint16_t virtio_bus_get_vdev_id(VirtioBusState *bus)
{
    VirtIODevice *vdev = virtio_bus_get_device(bus);
    assert(vdev != NULL);
    return vdev->device_id;
}

/* Get the config_len field of the plugged device. */
size_t virtio_bus_get_vdev_config_len(VirtioBusState *bus)
{
    VirtIODevice *vdev = virtio_bus_get_device(bus);
    assert(vdev != NULL);
    return vdev->config_len;
}

/* Get bad features of the plugged device. */
uint32_t virtio_bus_get_vdev_bad_features(VirtioBusState *bus)
{
    VirtIODevice *vdev = virtio_bus_get_device(bus);
    VirtioDeviceClass *k;

    assert(vdev != NULL);
    k = VIRTIO_DEVICE_GET_CLASS(vdev);
    if (k->bad_features != NULL) {
        return k->bad_features(vdev);
    } else {
        return 0;
    }
}

/* Get config of the plugged device. */
void virtio_bus_get_vdev_config(VirtioBusState *bus, uint8_t *config)
{
    VirtIODevice *vdev = virtio_bus_get_device(bus);
    VirtioDeviceClass *k;

    assert(vdev != NULL);
    k = VIRTIO_DEVICE_GET_CLASS(vdev);
    if (k->get_config != NULL) {
        k->get_config(vdev, config);
    }
}

/* Set config of the plugged device. */
void virtio_bus_set_vdev_config(VirtioBusState *bus, uint8_t *config)
{
    VirtIODevice *vdev = virtio_bus_get_device(bus);
    VirtioDeviceClass *k;

    assert(vdev != NULL);
    k = VIRTIO_DEVICE_GET_CLASS(vdev);
    if (k->set_config != NULL) {
        k->set_config(vdev, config);
    }
}

/* On success, ioeventfd ownership belongs to the caller.  */
int virtio_bus_grab_ioeventfd(VirtioBusState *bus)
{
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(bus);

    /* vhost can be used even if ioeventfd=off in the proxy device,
     * so do not check k->ioeventfd_enabled.
     */
    if (!k->ioeventfd_assign) {
        return -ENOSYS;
    }

    if (bus->ioeventfd_grabbed == 0 && bus->ioeventfd_started) {
        virtio_bus_stop_ioeventfd(bus);
        /* Remember that we need to restart ioeventfd
         * when ioeventfd_grabbed becomes zero.
         */
        bus->ioeventfd_started = true;
    }
    bus->ioeventfd_grabbed++;
    return 0;
}

void virtio_bus_release_ioeventfd(VirtioBusState *bus)
{
    assert(bus->ioeventfd_grabbed != 0);
    if (--bus->ioeventfd_grabbed == 0 && bus->ioeventfd_started) {
        /* Force virtio_bus_start_ioeventfd to act.  */
        bus->ioeventfd_started = false;
        virtio_bus_start_ioeventfd(bus);
    }
}

int virtio_bus_start_ioeventfd(VirtioBusState *bus)
{
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(bus);
    DeviceState *proxy = DEVICE(BUS(bus)->parent);
    VirtIODevice *vdev = virtio_bus_get_device(bus);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);
    int r;

    if (!k->ioeventfd_assign || !k->ioeventfd_enabled(proxy)) {
        return -ENOSYS;
    }
    if (bus->ioeventfd_started) {
        return 0;
    }

    /* Only set our notifier if we have ownership.  */
    if (!bus->ioeventfd_grabbed) {
        r = vdc->start_ioeventfd(vdev);
        if (r < 0) {
            error_report("%s: failed. Fallback to userspace (slower).", __func__);
            return r;
        }
    }
    bus->ioeventfd_started = true;
    return 0;
}

void virtio_bus_stop_ioeventfd(VirtioBusState *bus)
{
    VirtIODevice *vdev;
    VirtioDeviceClass *vdc;

    if (!bus->ioeventfd_started) {
        return;
    }

    /* Only remove our notifier if we have ownership.  */
    if (!bus->ioeventfd_grabbed) {
        vdev = virtio_bus_get_device(bus);
        vdc = VIRTIO_DEVICE_GET_CLASS(vdev);
        vdc->stop_ioeventfd(vdev);
    }
    bus->ioeventfd_started = false;
}

bool virtio_bus_ioeventfd_enabled(VirtioBusState *bus)
{
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(bus);
    DeviceState *proxy = DEVICE(BUS(bus)->parent);

    return k->ioeventfd_assign && k->ioeventfd_enabled(proxy);
}

/*
 * This function switches ioeventfd on/off in the device.
 * The caller must set or clear the handlers for the EventNotifier.
 */
int virtio_bus_set_host_notifier(VirtioBusState *bus, int n, bool assign)
{
    VirtIODevice *vdev = virtio_bus_get_device(bus);
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(bus);
    DeviceState *proxy = DEVICE(BUS(bus)->parent);
    VirtQueue *vq = virtio_get_queue(vdev, n);
    EventNotifier *notifier = virtio_queue_get_host_notifier(vq);
    int r = 0;

    if (!k->ioeventfd_assign) {
        return -ENOSYS;
    }

    if (assign) {
        r = event_notifier_init(notifier, 1);
        if (r < 0) {
            error_report("%s: unable to init event notifier: %s (%d)",
                         __func__, strerror(-r), r);
            return r;
        }
        r = k->ioeventfd_assign(proxy, notifier, n, true);
        if (r < 0) {
            error_report("%s: unable to assign ioeventfd: %d", __func__, r);
            virtio_bus_cleanup_host_notifier(bus, n);
        }
    } else {
        k->ioeventfd_assign(proxy, notifier, n, false);
    }

    if (r == 0) {
        virtio_queue_set_host_notifier_enabled(vq, assign);
    }

    return r;
}

void virtio_bus_cleanup_host_notifier(VirtioBusState *bus, int n)
{
    VirtIODevice *vdev = virtio_bus_get_device(bus);
    VirtQueue *vq = virtio_get_queue(vdev, n);
    EventNotifier *notifier = virtio_queue_get_host_notifier(vq);

    /* Test and clear notifier after disabling event,
     * in case poll callback didn't have time to run.
     */
    virtio_queue_host_notifier_read(notifier);
    event_notifier_cleanup(notifier);
}

static char *virtio_bus_get_dev_path(DeviceState *dev)
{
    BusState *bus = qdev_get_parent_bus(dev);
    DeviceState *proxy = DEVICE(bus->parent);
    return qdev_get_dev_path(proxy);
}

static char *virtio_bus_get_fw_dev_path(DeviceState *dev)
{
    return NULL;
}

bool virtio_bus_device_iommu_enabled(VirtIODevice *vdev)
{
    DeviceState *qdev = DEVICE(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(qdev));
    VirtioBusState *bus = VIRTIO_BUS(qbus);
    VirtioBusClass *klass = VIRTIO_BUS_GET_CLASS(bus);

    if (!klass->iommu_enabled) {
        return false;
    }

    return klass->iommu_enabled(qbus->parent);
}

static void virtio_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *bus_class = BUS_CLASS(klass);
    bus_class->get_dev_path = virtio_bus_get_dev_path;
    bus_class->get_fw_dev_path = virtio_bus_get_fw_dev_path;
}

static const TypeInfo virtio_bus_info = {
    .name = TYPE_VIRTIO_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(VirtioBusState),
    .abstract = true,
    .class_size = sizeof(VirtioBusClass),
    .class_init = virtio_bus_class_init
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_bus_info);
}

type_init(virtio_register_types)
