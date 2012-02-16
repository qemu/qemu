/*
 * Virtio Console and Generic Serial Port Devices
 *
 * Copyright Red Hat, Inc. 2009, 2010
 *
 * Authors:
 *  Amit Shah <amit.shah@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu-char.h"
#include "qemu-error.h"
#include "trace.h"
#include "virtio-serial.h"

typedef struct VirtConsole {
    VirtIOSerialPort port;
    CharDriverState *chr;
} VirtConsole;


/* Callback function that's called when the guest sends us data */
static ssize_t flush_buf(VirtIOSerialPort *port, const uint8_t *buf, size_t len)
{
    VirtConsole *vcon = DO_UPCAST(VirtConsole, port, port);
    ssize_t ret;

    if (!vcon->chr) {
        /* If there's no backend, we can just say we consumed all data. */
        return len;
    }

    ret = qemu_chr_fe_write(vcon->chr, buf, len);
    trace_virtio_console_flush_buf(port->id, len, ret);

    if (ret < 0) {
        /*
         * Ideally we'd get a better error code than just -1, but
         * that's what the chardev interface gives us right now.  If
         * we had a finer-grained message, like -EPIPE, we could close
         * this connection.  Absent such error messages, the most we
         * can do is to return 0 here.
         *
         * This will prevent stray -1 values to go to
         * virtio-serial-bus.c and cause abort()s in
         * do_flush_queued_data().
         */
        ret = 0;
    }
    return ret;
}

/* Callback function that's called when the guest opens the port */
static void guest_open(VirtIOSerialPort *port)
{
    VirtConsole *vcon = DO_UPCAST(VirtConsole, port, port);

    if (!vcon->chr) {
        return;
    }
    qemu_chr_fe_open(vcon->chr);
}

/* Callback function that's called when the guest closes the port */
static void guest_close(VirtIOSerialPort *port)
{
    VirtConsole *vcon = DO_UPCAST(VirtConsole, port, port);

    if (!vcon->chr) {
        return;
    }
    qemu_chr_fe_close(vcon->chr);
}

/* Readiness of the guest to accept data on a port */
static int chr_can_read(void *opaque)
{
    VirtConsole *vcon = opaque;

    return virtio_serial_guest_ready(&vcon->port);
}

/* Send data from a char device over to the guest */
static void chr_read(void *opaque, const uint8_t *buf, int size)
{
    VirtConsole *vcon = opaque;

    trace_virtio_console_chr_read(vcon->port.id, size);
    virtio_serial_write(&vcon->port, buf, size);
}

static void chr_event(void *opaque, int event)
{
    VirtConsole *vcon = opaque;

    trace_virtio_console_chr_event(vcon->port.id, event);
    switch (event) {
    case CHR_EVENT_OPENED:
        virtio_serial_open(&vcon->port);
        break;
    case CHR_EVENT_CLOSED:
        virtio_serial_close(&vcon->port);
        break;
    }
}

static int virtconsole_initfn(VirtIOSerialPort *port)
{
    VirtConsole *vcon = DO_UPCAST(VirtConsole, port, port);
    VirtIOSerialPortClass *k = VIRTIO_SERIAL_PORT_GET_CLASS(port);

    if (port->id == 0 && !k->is_console) {
        error_report("Port number 0 on virtio-serial devices reserved for virtconsole devices for backward compatibility.");
        return -1;
    }

    if (vcon->chr) {
        qemu_chr_add_handlers(vcon->chr, chr_can_read, chr_read, chr_event,
                              vcon);
    }

    return 0;
}

static Property virtconsole_properties[] = {
    DEFINE_PROP_CHR("chardev", VirtConsole, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtconsole_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOSerialPortClass *k = VIRTIO_SERIAL_PORT_CLASS(klass);

    k->is_console = true;
    k->init = virtconsole_initfn;
    k->have_data = flush_buf;
    k->guest_open = guest_open;
    k->guest_close = guest_close;
    dc->props = virtconsole_properties;
}

static TypeInfo virtconsole_info = {
    .name          = "virtconsole",
    .parent        = TYPE_VIRTIO_SERIAL_PORT,
    .instance_size = sizeof(VirtConsole),
    .class_init    = virtconsole_class_init,
};

static Property virtserialport_properties[] = {
    DEFINE_PROP_CHR("chardev", VirtConsole, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtserialport_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtIOSerialPortClass *k = VIRTIO_SERIAL_PORT_CLASS(klass);

    k->init = virtconsole_initfn;
    k->have_data = flush_buf;
    k->guest_open = guest_open;
    k->guest_close = guest_close;
    dc->props = virtserialport_properties;
}

static TypeInfo virtserialport_info = {
    .name          = "virtserialport",
    .parent        = TYPE_VIRTIO_SERIAL_PORT,
    .instance_size = sizeof(VirtConsole),
    .class_init    = virtserialport_class_init,
};

static void virtconsole_register_types(void)
{
    type_register_static(&virtconsole_info);
    type_register_static(&virtserialport_info);
}

type_init(virtconsole_register_types)
