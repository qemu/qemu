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
#include "hw/hw.h"
#include "qemu/error-report.h"
#include "hw/qdev.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio.h"

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

    DPRINTF("%s: plug device.\n", qbus->name);

    if (klass->device_plugged != NULL) {
        klass->device_plugged(qbus->parent, errp);
    }

    /* Get the features of the plugged device. */
    assert(vdc->get_features != NULL);
    vdev->host_features = vdc->get_features(vdev, vdev->host_features,
                                            errp);
    if (klass->post_plugged != NULL) {
        klass->post_plugged(qbus->parent, errp);
    }
}

/* Reset the virtio_bus */
void virtio_bus_reset(VirtioBusState *bus)
{
    VirtIODevice *vdev = virtio_bus_get_device(bus);

    DPRINTF("%s: reset device.\n", BUS(bus)->name);
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

/*
 * This function handles both assigning the ioeventfd handler and
 * registering it with the kernel.
 * assign: register/deregister ioeventfd with the kernel
 * set_handler: use the generic ioeventfd handler
 */
static int set_host_notifier_internal(DeviceState *proxy, VirtioBusState *bus,
                                      int n, bool assign, bool set_handler)
{
    VirtIODevice *vdev = virtio_bus_get_device(bus);
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(bus);
    VirtQueue *vq = virtio_get_queue(vdev, n);
    EventNotifier *notifier = virtio_queue_get_host_notifier(vq);
    int r = 0;

    if (assign) {
        r = event_notifier_init(notifier, 1);
        if (r < 0) {
            error_report("%s: unable to init event notifier: %d", __func__, r);
            return r;
        }
        virtio_queue_set_host_notifier_fd_handler(vq, true, set_handler);
        r = k->ioeventfd_assign(proxy, notifier, n, assign);
        if (r < 0) {
            error_report("%s: unable to assign ioeventfd: %d", __func__, r);
            virtio_queue_set_host_notifier_fd_handler(vq, false, false);
            event_notifier_cleanup(notifier);
            return r;
        }
    } else {
        k->ioeventfd_assign(proxy, notifier, n, assign);
        virtio_queue_set_host_notifier_fd_handler(vq, false, false);
        event_notifier_cleanup(notifier);
    }
    return r;
}

void virtio_bus_start_ioeventfd(VirtioBusState *bus)
{
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(bus);
    DeviceState *proxy = DEVICE(BUS(bus)->parent);
    VirtIODevice *vdev;
    int n, r;

    if (!k->ioeventfd_started || k->ioeventfd_started(proxy)) {
        return;
    }
    if (k->ioeventfd_disabled(proxy)) {
        return;
    }
    vdev = virtio_bus_get_device(bus);
    for (n = 0; n < VIRTIO_QUEUE_MAX; n++) {
        if (!virtio_queue_get_num(vdev, n)) {
            continue;
        }
        r = set_host_notifier_internal(proxy, bus, n, true, true);
        if (r < 0) {
            goto assign_error;
        }
    }
    k->ioeventfd_set_started(proxy, true, false);
    return;

assign_error:
    while (--n >= 0) {
        if (!virtio_queue_get_num(vdev, n)) {
            continue;
        }

        r = set_host_notifier_internal(proxy, bus, n, false, false);
        assert(r >= 0);
    }
    k->ioeventfd_set_started(proxy, false, true);
    error_report("%s: failed. Fallback to userspace (slower).", __func__);
}

void virtio_bus_stop_ioeventfd(VirtioBusState *bus)
{
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(bus);
    DeviceState *proxy = DEVICE(BUS(bus)->parent);
    VirtIODevice *vdev;
    int n, r;

    if (!k->ioeventfd_started || !k->ioeventfd_started(proxy)) {
        return;
    }
    vdev = virtio_bus_get_device(bus);
    for (n = 0; n < VIRTIO_QUEUE_MAX; n++) {
        if (!virtio_queue_get_num(vdev, n)) {
            continue;
        }
        r = set_host_notifier_internal(proxy, bus, n, false, false);
        assert(r >= 0);
    }
    k->ioeventfd_set_started(proxy, false, false);
}

/*
 * This function switches from/to the generic ioeventfd handler.
 * assign==false means 'use generic ioeventfd handler'.
 */
int virtio_bus_set_host_notifier(VirtioBusState *bus, int n, bool assign)
{
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(bus);
    DeviceState *proxy = DEVICE(BUS(bus)->parent);

    if (!k->ioeventfd_started) {
        return -ENOSYS;
    }
    k->ioeventfd_set_disabled(proxy, assign);
    if (assign) {
        /*
         * Stop using the generic ioeventfd, we are doing eventfd handling
         * ourselves below
         *
         * FIXME: We should just switch the handler and not deassign the
         * ioeventfd.
         * Otherwise, there's a window where we don't have an
         * ioeventfd and we may end up with a notification where
         * we don't expect one.
         */
        virtio_bus_stop_ioeventfd(bus);
    }
    return set_host_notifier_internal(proxy, bus, n, assign, false);
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
