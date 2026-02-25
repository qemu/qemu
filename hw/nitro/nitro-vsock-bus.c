/*
 * Nitro Enclave Vsock Bus
 *
 * Copyright © 2026 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors:
 *   Alexander Graf <graf@amazon.com>
 *
 * A bus for Nitro Enclave vsock devices. In Nitro Enclaves, communication
 * between parent and enclave/hypervisor happens almost exclusively through
 * vsock. The nitro-vsock-bus models this dependency in QEMU, which allows
 * devices in this bus to implement individual services on top of vsock.
 *
 * The nitro accel advertises the Enclave's CID to the bus by calling
 * nitro_vsock_bridge_start_enclave() on the bridge device as soon as it
 * knows the CID.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "monitor/qdev.h"
#include "hw/core/sysbus.h"
#include "hw/nitro/nitro-vsock-bus.h"

void nitro_vsock_bridge_start_enclave(NitroVsockBridge *bridge,
                                      uint32_t enclave_cid, Error **errp)
{
    ERRP_GUARD();
    BusState *qbus = BUS(&bridge->bus);
    BusChild *kid;

    bridge->enclave_cid = enclave_cid;

    QTAILQ_FOREACH(kid, &qbus->children, sibling) {
        NitroVsockDevice *ndev = NITRO_VSOCK_DEVICE(kid->child);
        NitroVsockDeviceClass *ndc = NITRO_VSOCK_DEVICE_GET_CLASS(ndev);

        if (ndc->enclave_started) {
            ndc->enclave_started(ndev, enclave_cid, errp);
            if (*errp) {
                return;
            }
        }
    }
}

NitroVsockBridge *nitro_vsock_bridge_create(void)
{
    DeviceState *dev = qdev_new(TYPE_NITRO_VSOCK_BRIDGE);

    qdev_set_id(dev, g_strdup("nitro-vsock"), &error_fatal);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    return NITRO_VSOCK_BRIDGE(dev);
}

static void nitro_vsock_bridge_init(Object *obj)
{
    NitroVsockBridge *s = NITRO_VSOCK_BRIDGE(obj);

    qbus_init(&s->bus, sizeof(s->bus), TYPE_NITRO_VSOCK_BUS,
              DEVICE(s), "nitro-vsock");
    object_property_add_uint32_ptr(obj, "enclave-cid",
                                   &s->enclave_cid, OBJ_PROP_FLAG_READ);
}

static void nitro_vsock_device_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->bus_type = TYPE_NITRO_VSOCK_BUS;
}

static const TypeInfo nitro_vsock_bus_types[] = {
    {
        .name = TYPE_NITRO_VSOCK_BUS,
        .parent = TYPE_BUS,
        .instance_size = sizeof(NitroVsockBus),
    },
    {
        .name = TYPE_NITRO_VSOCK_BRIDGE,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(NitroVsockBridge),
        .instance_init = nitro_vsock_bridge_init,
    },
    {
        .name = TYPE_NITRO_VSOCK_DEVICE,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(NitroVsockDevice),
        .class_size = sizeof(NitroVsockDeviceClass),
        .class_init = nitro_vsock_device_class_init,
        .abstract = true,
    },
};

DEFINE_TYPES(nitro_vsock_bus_types);
