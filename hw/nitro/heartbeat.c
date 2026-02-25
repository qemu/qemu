/*
 * Nitro Enclave Heartbeat device
 *
 * Copyright © 2026 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors:
 *   Alexander Graf <graf@amazon.com>
 *
 * The Nitro Enclave init process sends a heartbeat byte (0xB7) to
 * CID 3 (parent) port 9000 on boot to signal it reached initramfs.
 * The parent must accept the connection, read the byte, and echo it
 * back. If the enclave init cannot reach the listener, it exits.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "hw/nitro/heartbeat.h"
#include "trace.h"

#define HEARTBEAT_PORT      9000
#define VMADDR_CID_ANY_STR  "4294967295"

static int nitro_heartbeat_can_read(void *opaque)
{
    NitroHeartbeatState *s = opaque;

    /* One-shot protocol: stop reading after the first heartbeat */
    return s->done ? 0 : 1;
}

static void nitro_heartbeat_read(void *opaque, const uint8_t *buf, int size)
{
    NitroHeartbeatState *s = opaque;

    if (s->done || size < 1) {
        return;
    }

    /* Echo the heartbeat byte back and disconnect */
    qemu_chr_fe_write_all(&s->vsock, buf, 1);
    s->done = true;
    qemu_chr_fe_deinit(&s->vsock, true);

    trace_nitro_heartbeat_done();
}

static void nitro_heartbeat_event(void *opaque, QEMUChrEvent event)
{
    trace_nitro_heartbeat_event(event);
}

static void nitro_heartbeat_realize(DeviceState *dev, Error **errp)
{
    NitroHeartbeatState *s = NITRO_HEARTBEAT(dev);
    g_autofree char *chardev_id = NULL;
    Chardev *chr;
    ChardevBackend *backend;
    ChardevSocket *sock;

    chardev_id = g_strdup_printf("nitro-heartbeat");

    backend = g_new0(ChardevBackend, 1);
    backend->type = CHARDEV_BACKEND_KIND_SOCKET;
    sock = backend->u.socket.data = g_new0(ChardevSocket, 1);
    sock->addr = g_new0(SocketAddressLegacy, 1);
    sock->addr->type = SOCKET_ADDRESS_TYPE_VSOCK;
    sock->addr->u.vsock.data = g_new0(VsockSocketAddress, 1);
    sock->addr->u.vsock.data->cid = g_strdup(VMADDR_CID_ANY_STR);
    sock->addr->u.vsock.data->port = g_strdup_printf("%u", HEARTBEAT_PORT);
    sock->server = true;
    sock->has_server = true;
    sock->wait = false;
    sock->has_wait = true;

    chr = qemu_chardev_new(chardev_id, TYPE_CHARDEV_SOCKET,
                           backend, NULL, errp);
    if (!chr) {
        return;
    }

    if (!qemu_chr_fe_init(&s->vsock, chr, errp)) {
        return;
    }

    qemu_chr_fe_set_handlers(&s->vsock,
                             nitro_heartbeat_can_read,
                             nitro_heartbeat_read,
                             nitro_heartbeat_event,
                             NULL, s, NULL, true);
}

static void nitro_heartbeat_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = nitro_heartbeat_realize;
}

static const TypeInfo nitro_heartbeat_info = {
    .name = TYPE_NITRO_HEARTBEAT,
    .parent = TYPE_NITRO_VSOCK_DEVICE,
    .instance_size = sizeof(NitroHeartbeatState),
    .class_init = nitro_heartbeat_class_init,
};

static void nitro_heartbeat_register(void)
{
    type_register_static(&nitro_heartbeat_info);
}

type_init(nitro_heartbeat_register);
