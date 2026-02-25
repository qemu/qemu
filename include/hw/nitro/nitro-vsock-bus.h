/*
 * Nitro Enclave Vsock Bus
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NITRO_VSOCK_BUS_H
#define HW_NITRO_VSOCK_BUS_H

#include "hw/core/qdev.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_NITRO_VSOCK_BUS "nitro-vsock-bus"
OBJECT_DECLARE_SIMPLE_TYPE(NitroVsockBus, NITRO_VSOCK_BUS)

#define TYPE_NITRO_VSOCK_BRIDGE "nitro-vsock-bridge"
OBJECT_DECLARE_SIMPLE_TYPE(NitroVsockBridge, NITRO_VSOCK_BRIDGE)

#define TYPE_NITRO_VSOCK_DEVICE "nitro-vsock-device"
OBJECT_DECLARE_TYPE(NitroVsockDevice, NitroVsockDeviceClass,
                    NITRO_VSOCK_DEVICE)

struct NitroVsockBus {
    BusState parent_obj;
};

struct NitroVsockBridge {
    SysBusDevice parent_obj;

    NitroVsockBus bus;
    uint32_t enclave_cid;
};

struct NitroVsockDevice {
    DeviceState parent_obj;
};

struct NitroVsockDeviceClass {
    DeviceClass parent_class;

    /*
     * Called after the enclave has been started and the CID is known.
     * Devices use this to establish vsock connections to the enclave.
     */
    void (*enclave_started)(NitroVsockDevice *dev, uint32_t enclave_cid,
                            Error **errp);
};

/*
 * Machine helper to create the Nitro vsock bridge sysbus device.
 */
NitroVsockBridge *nitro_vsock_bridge_create(void);

/*
 * Find the Nitro vsock bridge on the sysbus.
 */
static inline NitroVsockBridge *nitro_vsock_bridge_find(void)
{
    return NITRO_VSOCK_BRIDGE(
        object_resolve_path_type("", TYPE_NITRO_VSOCK_BRIDGE, NULL));
}

/*
 * Notify the bridge that the enclave has started. Dispatches
 * enclave_started() to all devices on the bus.
 */
void nitro_vsock_bridge_start_enclave(NitroVsockBridge *bridge,
                                      uint32_t enclave_cid, Error **errp);

#endif /* HW_NITRO_VSOCK_BUS_H */
