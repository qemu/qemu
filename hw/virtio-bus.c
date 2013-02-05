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

/* Plug the VirtIODevice */
int virtio_bus_plug_device(VirtIODevice *vdev)
{
    DeviceState *qdev = DEVICE(vdev);
    BusState *qbus = BUS(qdev_get_parent_bus(qdev));
    VirtioBusState *bus = VIRTIO_BUS(qbus);
    VirtioBusClass *klass = VIRTIO_BUS_GET_CLASS(bus);
    DPRINTF("%s: plug device.\n", qbus->name);

    bus->vdev = vdev;

    /*
     * The lines below will disappear when we drop VirtIOBindings, at the end
     * of the series.
     */
    bus->bindings.notify = klass->notify;
    bus->bindings.save_config = klass->save_config;
    bus->bindings.save_queue = klass->save_queue;
    bus->bindings.load_config = klass->load_config;
    bus->bindings.load_queue = klass->load_queue;
    bus->bindings.load_done = klass->load_done;
    bus->bindings.get_features = klass->get_features;
    bus->bindings.query_guest_notifiers = klass->query_guest_notifiers;
    bus->bindings.set_guest_notifiers = klass->set_guest_notifiers;
    bus->bindings.set_host_notifier = klass->set_host_notifier;
    bus->bindings.vmstate_change = klass->vmstate_change;
    virtio_bind_device(bus->vdev, &bus->bindings, qbus->parent);

    if (klass->device_plugged != NULL) {
        klass->device_plugged(qbus->parent);
    }

    return 0;
}

/* Reset the virtio_bus */
void virtio_bus_reset(VirtioBusState *bus)
{
    DPRINTF("%s: reset device.\n", qbus->name);
    if (bus->vdev != NULL) {
        virtio_reset(bus->vdev);
    }
}

/* Destroy the VirtIODevice */
void virtio_bus_destroy_device(VirtioBusState *bus)
{
    DeviceState *qdev;
    BusState *qbus = BUS(bus);
    VirtioBusClass *klass = VIRTIO_BUS_GET_CLASS(bus);
    DPRINTF("%s: remove device.\n", qbus->name);

    if (bus->vdev != NULL) {
        if (klass->device_unplug != NULL) {
            klass->device_unplug(qbus->parent);
        }
        qdev = DEVICE(bus->vdev);
        qdev_free(qdev);
        bus->vdev = NULL;
    }
}

/* Get the device id of the plugged device. */
uint16_t virtio_bus_get_vdev_id(VirtioBusState *bus)
{
    assert(bus->vdev != NULL);
    return bus->vdev->device_id;
}

/* Get the config_len field of the plugged device. */
size_t virtio_bus_get_vdev_config_len(VirtioBusState *bus)
{
    assert(bus->vdev != NULL);
    return bus->vdev->config_len;
}

/* Get the features of the plugged device. */
uint32_t virtio_bus_get_vdev_features(VirtioBusState *bus,
                                    uint32_t requested_features)
{
    VirtioDeviceClass *k;
    assert(bus->vdev != NULL);
    k = VIRTIO_DEVICE_GET_CLASS(bus->vdev);
    assert(k->get_features != NULL);
    return k->get_features(bus->vdev, requested_features);
}

/* Get bad features of the plugged device. */
uint32_t virtio_bus_get_vdev_bad_features(VirtioBusState *bus)
{
    VirtioDeviceClass *k;
    assert(bus->vdev != NULL);
    k = VIRTIO_DEVICE_GET_CLASS(bus->vdev);
    if (k->bad_features != NULL) {
        return k->bad_features(bus->vdev);
    } else {
        return 0;
    }
}

/* Get config of the plugged device. */
void virtio_bus_get_vdev_config(VirtioBusState *bus, uint8_t *config)
{
    VirtioDeviceClass *k;
    assert(bus->vdev != NULL);
    k = VIRTIO_DEVICE_GET_CLASS(bus->vdev);
    if (k->get_config != NULL) {
        k->get_config(bus->vdev, config);
    }
}

static const TypeInfo virtio_bus_info = {
    .name = TYPE_VIRTIO_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(VirtioBusState),
    .abstract = true,
    .class_size = sizeof(VirtioBusClass),
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_bus_info);
}

type_init(virtio_register_types)
