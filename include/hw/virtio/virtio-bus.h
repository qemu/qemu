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

#ifndef VIRTIO_BUS_H
#define VIRTIO_BUS_H

#include "hw/qdev-core.h"
#include "hw/virtio/virtio.h"

#define TYPE_VIRTIO_BUS "virtio-bus"
#define VIRTIO_BUS_GET_CLASS(obj) \
        OBJECT_GET_CLASS(VirtioBusClass, obj, TYPE_VIRTIO_BUS)
#define VIRTIO_BUS_CLASS(klass) \
        OBJECT_CLASS_CHECK(VirtioBusClass, klass, TYPE_VIRTIO_BUS)
#define VIRTIO_BUS(obj) OBJECT_CHECK(VirtioBusState, (obj), TYPE_VIRTIO_BUS)

typedef struct VirtioBusState VirtioBusState;

typedef struct VirtioBusClass {
    /* This is what a VirtioBus must implement */
    BusClass parent;
    void (*notify)(DeviceState *d, uint16_t vector);
    void (*save_config)(DeviceState *d, QEMUFile *f);
    void (*save_queue)(DeviceState *d, int n, QEMUFile *f);
    void (*save_extra_state)(DeviceState *d, QEMUFile *f);
    int (*load_config)(DeviceState *d, QEMUFile *f);
    int (*load_queue)(DeviceState *d, int n, QEMUFile *f);
    int (*load_done)(DeviceState *d, QEMUFile *f);
    int (*load_extra_state)(DeviceState *d, QEMUFile *f);
    bool (*has_extra_state)(DeviceState *d);
    bool (*query_guest_notifiers)(DeviceState *d);
    int (*set_guest_notifiers)(DeviceState *d, int nvqs, bool assign);
    int (*set_host_notifier_mr)(DeviceState *d, int n,
                                MemoryRegion *mr, bool assign);
    void (*vmstate_change)(DeviceState *d, bool running);
    /*
     * Expose the features the transport layer supports before
     * the negotiation takes place.
     */
    void (*pre_plugged)(DeviceState *d, Error **errp);
    /*
     * transport independent init function.
     * This is called by virtio-bus just after the device is plugged.
     */
    void (*device_plugged)(DeviceState *d, Error **errp);
    /*
     * transport independent exit function.
     * This is called by virtio-bus just before the device is unplugged.
     */
    void (*device_unplugged)(DeviceState *d);
    int (*query_nvectors)(DeviceState *d);
    /*
     * ioeventfd handling: if the transport implements ioeventfd_assign,
     * it must implement ioeventfd_enabled as well.
     */
    /* Returns true if the ioeventfd is enabled for the device. */
    bool (*ioeventfd_enabled)(DeviceState *d);
    /*
     * Assigns/deassigns the ioeventfd backing for the transport on
     * the device for queue number n. Returns an error value on
     * failure.
     */
    int (*ioeventfd_assign)(DeviceState *d, EventNotifier *notifier,
                            int n, bool assign);
    /*
     * Whether queue number n is enabled.
     */
    bool (*queue_enabled)(DeviceState *d, int n);
    /*
     * Does the transport have variable vring alignment?
     * (ie can it ever call virtio_queue_set_align()?)
     * Note that changing this will break migration for this transport.
     */
    bool has_variable_vring_alignment;
    AddressSpace *(*get_dma_as)(DeviceState *d);
} VirtioBusClass;

struct VirtioBusState {
    BusState parent_obj;

    /*
     * Set if ioeventfd has been started.
     */
    bool ioeventfd_started;

    /*
     * Set if ioeventfd has been grabbed by vhost.  When ioeventfd
     * is grabbed by vhost, we track its started/stopped state (which
     * depends in turn on the virtio status register), but do not
     * register a handler for the ioeventfd.  When ioeventfd is
     * released, if ioeventfd_started is true we finally register
     * the handler so that QEMU's device model can use ioeventfd.
     */
    int ioeventfd_grabbed;
};

void virtio_bus_device_plugged(VirtIODevice *vdev, Error **errp);
void virtio_bus_reset(VirtioBusState *bus);
void virtio_bus_device_unplugged(VirtIODevice *bus);
/* Get the device id of the plugged device. */
uint16_t virtio_bus_get_vdev_id(VirtioBusState *bus);
/* Get the config_len field of the plugged device. */
size_t virtio_bus_get_vdev_config_len(VirtioBusState *bus);
/* Get bad features of the plugged device. */
uint32_t virtio_bus_get_vdev_bad_features(VirtioBusState *bus);
/* Get config of the plugged device. */
void virtio_bus_get_vdev_config(VirtioBusState *bus, uint8_t *config);
/* Set config of the plugged device. */
void virtio_bus_set_vdev_config(VirtioBusState *bus, uint8_t *config);

static inline VirtIODevice *virtio_bus_get_device(VirtioBusState *bus)
{
    BusState *qbus = &bus->parent_obj;
    BusChild *kid = QTAILQ_FIRST(&qbus->children);
    DeviceState *qdev = kid ? kid->child : NULL;

    /* This is used on the data path, the cast is guaranteed
     * to succeed by the qdev machinery.
     */
    return (VirtIODevice *)qdev;
}

/* Return whether the proxy allows ioeventfd.  */
bool virtio_bus_ioeventfd_enabled(VirtioBusState *bus);
/* Start the ioeventfd. */
int virtio_bus_start_ioeventfd(VirtioBusState *bus);
/* Stop the ioeventfd. */
void virtio_bus_stop_ioeventfd(VirtioBusState *bus);
/* Tell the bus that vhost is grabbing the ioeventfd. */
int virtio_bus_grab_ioeventfd(VirtioBusState *bus);
/* bus that vhost is not using the ioeventfd anymore. */
void virtio_bus_release_ioeventfd(VirtioBusState *bus);
/* Switch from/to the generic ioeventfd handler */
int virtio_bus_set_host_notifier(VirtioBusState *bus, int n, bool assign);
/* Tell the bus that the ioeventfd handler is no longer required. */
void virtio_bus_cleanup_host_notifier(VirtioBusState *bus, int n);

#endif /* VIRTIO_BUS_H */
