/*
 * Virtio Serial / Console Support
 *
 * Copyright IBM, Corp. 2008
 * Copyright Red Hat, Inc. 2009, 2010
 *
 * Authors:
 *  Christian Ehrhardt <ehrhardt@linux.vnet.ibm.com>
 *  Amit Shah <amit.shah@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */
#ifndef _QEMU_VIRTIO_SERIAL_H
#define _QEMU_VIRTIO_SERIAL_H

#include "standard-headers/linux/virtio_console.h"
#include "hw/qdev.h"
#include "hw/virtio/virtio.h"

struct virtio_serial_conf {
    /* Max. number of ports we can have for a virtio-serial device */
    uint32_t max_virtserial_ports;
};

#define TYPE_VIRTIO_SERIAL_PORT "virtio-serial-port"
#define VIRTIO_SERIAL_PORT(obj) \
     OBJECT_CHECK(VirtIOSerialPort, (obj), TYPE_VIRTIO_SERIAL_PORT)
#define VIRTIO_SERIAL_PORT_CLASS(klass) \
     OBJECT_CLASS_CHECK(VirtIOSerialPortClass, (klass), TYPE_VIRTIO_SERIAL_PORT)
#define VIRTIO_SERIAL_PORT_GET_CLASS(obj) \
     OBJECT_GET_CLASS(VirtIOSerialPortClass, (obj), TYPE_VIRTIO_SERIAL_PORT)

typedef struct VirtIOSerial VirtIOSerial;
typedef struct VirtIOSerialBus VirtIOSerialBus;
typedef struct VirtIOSerialPort VirtIOSerialPort;

typedef struct VirtIOSerialPortClass {
    DeviceClass parent_class;

    /* Is this a device that binds with hvc in the guest? */
    bool is_console;

    /*
     * The per-port (or per-app) realize function that's called when a
     * new device is found on the bus.
     */
    DeviceRealize realize;
    /*
     * Per-port unrealize function that's called when a port gets
     * hot-unplugged or removed.
     */
    DeviceUnrealize unrealize;

    /* Callbacks for guest events */
        /* Guest opened/closed device. */
    void (*set_guest_connected)(VirtIOSerialPort *port, int guest_connected);

        /* Guest is now ready to accept data (virtqueues set up). */
    void (*guest_ready)(VirtIOSerialPort *port);

        /*
         * Guest has enqueued a buffer for the host to write into.
         * Called each time a buffer is enqueued by the guest;
         * irrespective of whether there already were free buffers the
         * host could have consumed.
         *
         * This is dependent on both the guest and host end being
         * connected.
         */
    void (*guest_writable)(VirtIOSerialPort *port);

    /*
     * Guest wrote some data to the port. This data is handed over to
     * the app via this callback.  The app can return a size less than
     * 'len'.  In this case, throttling will be enabled for this port.
     */
    ssize_t (*have_data)(VirtIOSerialPort *port, const uint8_t *buf,
                         ssize_t len);
} VirtIOSerialPortClass;

/*
 * This is the state that's shared between all the ports.  Some of the
 * state is configurable via command-line options. Some of it can be
 * set by individual devices in their initfn routines. Some of the
 * state is set by the generic qdev device init routine.
 */
struct VirtIOSerialPort {
    DeviceState dev;

    QTAILQ_ENTRY(VirtIOSerialPort) next;

    /*
     * This field gives us the virtio device as well as the qdev bus
     * that we are associated with
     */
    VirtIOSerial *vser;

    VirtQueue *ivq, *ovq;

    /*
     * This name is sent to the guest and exported via sysfs.
     * The guest could create symlinks based on this information.
     * The name is in the reverse fqdn format, like org.qemu.console.0
     */
    char *name;

    /*
     * This id helps identify ports between the guest and the host.
     * The guest sends a "header" with this id with each data packet
     * that it sends and the host can then find out which associated
     * device to send out this data to
     */
    uint32_t id;

    /*
     * This is the elem that we pop from the virtqueue.  A slow
     * backend that consumes guest data (e.g. the file backend for
     * qemu chardevs) can cause the guest to block till all the output
     * is flushed.  This isn't desired, so we keep a note of the last
     * element popped and continue consuming it once the backend
     * becomes writable again.
     */
    VirtQueueElement *elem;

    /*
     * The index and the offset into the iov buffer that was popped in
     * elem above.
     */
    uint32_t iov_idx;
    uint64_t iov_offset;

    /*
     * When unthrottling we use a bottom-half to call flush_queued_data.
     */
    QEMUBH *bh;

    /* Is the corresponding guest device open? */
    bool guest_connected;
    /* Is this device open for IO on the host? */
    bool host_connected;
    /* Do apps not want to receive data? */
    bool throttled;
};

/* The virtio-serial bus on top of which the ports will ride as devices */
struct VirtIOSerialBus {
    BusState qbus;

    /* This is the parent device that provides the bus for ports. */
    VirtIOSerial *vser;

    /* The maximum number of ports that can ride on top of this bus */
    uint32_t max_nr_ports;
};

typedef struct VirtIOSerialPostLoad {
    QEMUTimer *timer;
    uint32_t nr_active_ports;
    struct {
        VirtIOSerialPort *port;
        uint8_t host_connected;
    } *connected;
} VirtIOSerialPostLoad;

struct VirtIOSerial {
    VirtIODevice parent_obj;

    VirtQueue *c_ivq, *c_ovq;
    /* Arrays of ivqs and ovqs: one per port */
    VirtQueue **ivqs, **ovqs;

    VirtIOSerialBus bus;

    QTAILQ_HEAD(, VirtIOSerialPort) ports;

    QLIST_ENTRY(VirtIOSerial) next;

    /* bitmap for identifying active ports */
    uint32_t *ports_map;

    struct VirtIOSerialPostLoad *post_load;

    virtio_serial_conf serial;
};

/* Interface to the virtio-serial bus */

/*
 * Open a connection to the port
 *   Returns 0 on success (always).
 */
int virtio_serial_open(VirtIOSerialPort *port);

/*
 * Close the connection to the port
 *   Returns 0 on success (always).
 */
int virtio_serial_close(VirtIOSerialPort *port);

/*
 * Send data to Guest
 */
ssize_t virtio_serial_write(VirtIOSerialPort *port, const uint8_t *buf,
                            size_t size);

/*
 * Query whether a guest is ready to receive data.
 */
size_t virtio_serial_guest_ready(VirtIOSerialPort *port);

/*
 * Flow control: Ports can signal to the virtio-serial core to stop
 * sending data or re-start sending data, depending on the 'throttle'
 * value here.
 */
void virtio_serial_throttle_port(VirtIOSerialPort *port, bool throttle);

#define TYPE_VIRTIO_SERIAL "virtio-serial-device"
#define VIRTIO_SERIAL(obj) \
        OBJECT_CHECK(VirtIOSerial, (obj), TYPE_VIRTIO_SERIAL)

#endif
