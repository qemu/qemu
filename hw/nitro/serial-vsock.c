/*
 * Nitro Enclave Vsock Serial
 *
 * Copyright © 2026 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors:
 *   Alexander Graf <graf@amazon.com>
 *
 * With Nitro Enclaves in debug mode, the Nitro Hypervisor provides a vsock
 * port that the parent can connect to to receive serial console output of
 * the Enclave. This driver implements short-circuit logic to establish the
 * vsock connection to that port and feed its data into a chardev, so that
 * a machine model can use it as serial device.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/nitro/serial-vsock.h"
#include "trace.h"

#define CONSOLE_PORT_START 10000
#define VMADDR_CID_HYPERVISOR_STR "0"

static int nitro_serial_vsock_can_read(void *opaque)
{
    NitroSerialVsockState *s = opaque;

    /* Refuse vsock input until the output backend is ready */
    return qemu_chr_fe_backend_open(&s->output) ? 4096 : 0;
}

static void nitro_serial_vsock_read(void *opaque, const uint8_t *buf, int size)
{
    NitroSerialVsockState *s = opaque;

    /* Forward all vsock data to the output chardev */
    qemu_chr_fe_write_all(&s->output, buf, size);
}

static void nitro_serial_vsock_event(void *opaque, QEMUChrEvent event)
{
    /* No need to action on connect/disconnect events, but trace for debug */
    trace_nitro_serial_vsock_event(event);
}

static void nitro_serial_vsock_enclave_started(NitroVsockDevice *dev,
                                               uint32_t enclave_cid,
                                               Error **errp)
{
    NitroSerialVsockState *s = NITRO_SERIAL_VSOCK(dev);
    uint32_t port = enclave_cid + CONSOLE_PORT_START;
    g_autofree char *chardev_id = NULL;
    Chardev *chr;
    ChardevBackend *backend;
    ChardevSocket *sock;

    /*
     * We know the Enclave CID to connect to now. Create a vsock
     * client chardev that connects to the Enclave's console.
     */
    chardev_id = g_strdup_printf("nitro-console-%u", enclave_cid);

    backend = g_new0(ChardevBackend, 1);
    backend->type = CHARDEV_BACKEND_KIND_SOCKET;
    sock = backend->u.socket.data = g_new0(ChardevSocket, 1);
    sock->addr = g_new0(SocketAddressLegacy, 1);
    sock->addr->type = SOCKET_ADDRESS_TYPE_VSOCK;
    sock->addr->u.vsock.data = g_new0(VsockSocketAddress, 1);
    sock->addr->u.vsock.data->cid = g_strdup(VMADDR_CID_HYPERVISOR_STR);
    sock->addr->u.vsock.data->port = g_strdup_printf("%u", port);
    sock->server = false;
    sock->has_server = true;

    chr = qemu_chardev_new(chardev_id, TYPE_CHARDEV_SOCKET,
                           backend, NULL, errp);
    if (!chr) {
        return;
    }

    if (!qemu_chr_fe_init(&s->vsock, chr, errp)) {
        return;
    }

    qemu_chr_fe_set_handlers(&s->vsock,
                             nitro_serial_vsock_can_read,
                             nitro_serial_vsock_read,
                             nitro_serial_vsock_event,
                             NULL, s, NULL, true);
}

static const Property nitro_serial_vsock_props[] = {
    DEFINE_PROP_CHR("chardev", NitroSerialVsockState, output),
};

static void nitro_serial_vsock_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    NitroVsockDeviceClass *ndc = NITRO_VSOCK_DEVICE_CLASS(oc);

    device_class_set_props(dc, nitro_serial_vsock_props);
    ndc->enclave_started = nitro_serial_vsock_enclave_started;
}

static const TypeInfo nitro_serial_vsock_info = {
    .name = TYPE_NITRO_SERIAL_VSOCK,
    .parent = TYPE_NITRO_VSOCK_DEVICE,
    .instance_size = sizeof(NitroSerialVsockState),
    .class_init = nitro_serial_vsock_class_init,
};

static void nitro_serial_vsock_register(void)
{
    type_register_static(&nitro_serial_vsock_info);
}

type_init(nitro_serial_vsock_register);
