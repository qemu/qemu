/*
 * Standard PCI Bridge Device
 *
 * Copyright (c) 2011 Red Hat Inc. Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * http://www.pcisig.com/specifications/conventional/pci_to_pci_bridge_architecture/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/msi.h"
#include "hw/pci/shpc.h"
#include "hw/pci/slotid_cap.h"
#include "hw/qdev-properties.h"
#include "system/memory.h"
#include "hw/pci/pci_bus.h"
#include "hw/hotplug.h"
#include "qom/object.h"

#define TYPE_PCI_BRIDGE_DEV      "pci-bridge"
#define TYPE_PCI_BRIDGE_SEAT_DEV "pci-bridge-seat"
OBJECT_DECLARE_SIMPLE_TYPE(PCIBridgeDev, PCI_BRIDGE_DEV)

struct PCIBridgeDev {
    /*< private >*/
    PCIBridge parent_obj;
    /*< public >*/

    MemoryRegion bar;
    uint8_t chassis_nr;
#define PCI_BRIDGE_DEV_F_SHPC_REQ 0
    uint32_t flags;

    OnOffAuto msi;

    /* additional resources to reserve */
    PCIResReserve res_reserve;
};

static void pci_bridge_dev_realize(PCIDevice *dev, Error **errp)
{
    PCIBridge *br = PCI_BRIDGE(dev);
    PCIBridgeDev *bridge_dev = PCI_BRIDGE_DEV(dev);
    int err;
    Error *local_err = NULL;

    pci_bridge_initfn(dev, TYPE_PCI_BUS);

    if (bridge_dev->flags & (1 << PCI_BRIDGE_DEV_F_SHPC_REQ)) {
        dev->config[PCI_INTERRUPT_PIN] = 0x1;
        memory_region_init(&bridge_dev->bar, OBJECT(dev), "shpc-bar",
                           shpc_bar_size(dev));
        err = shpc_init(dev, &br->sec_bus, &bridge_dev->bar, 0, errp);
        if (err) {
            goto shpc_error;
        }
    } else {
        /* MSI is not applicable without SHPC */
        bridge_dev->msi = ON_OFF_AUTO_OFF;
    }

    err = slotid_cap_init(dev, 0, bridge_dev->chassis_nr, 0, errp);
    if (err) {
        goto slotid_error;
    }

    if (bridge_dev->msi != ON_OFF_AUTO_OFF) {
        /* it means SHPC exists, because MSI is needed by SHPC */

        err = msi_init(dev, 0, 1, true, true, &local_err);
        /* Any error other than -ENOTSUP(board's MSI support is broken)
         * is a programming error */
        assert(!err || err == -ENOTSUP);
        if (err && bridge_dev->msi == ON_OFF_AUTO_ON) {
            /* Can't satisfy user's explicit msi=on request, fail */
            error_append_hint(&local_err, "You have to use msi=auto (default) "
                    "or msi=off with this machine type.\n");
            error_propagate(errp, local_err);
            goto msi_error;
        }
        assert(!local_err || bridge_dev->msi == ON_OFF_AUTO_AUTO);
        /* With msi=auto, we fall back to MSI off silently */
        error_free(local_err);
    }

    err = pci_bridge_qemu_reserve_cap_init(dev, 0,
                                         bridge_dev->res_reserve, errp);
    if (err) {
        goto cap_error;
    }

    if (shpc_present(dev)) {
        /* TODO: spec recommends using 64 bit prefetcheable BAR.
         * Check whether that works well. */
        pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_TYPE_64, &bridge_dev->bar);
    }
    return;

cap_error:
    msi_uninit(dev);
msi_error:
    slotid_cap_cleanup(dev);
slotid_error:
    if (shpc_present(dev)) {
        shpc_cleanup(dev, &bridge_dev->bar);
    }
shpc_error:
    pci_bridge_exitfn(dev);
}

static void pci_bridge_dev_exitfn(PCIDevice *dev)
{
    PCIBridgeDev *bridge_dev = PCI_BRIDGE_DEV(dev);

    pci_del_capability(dev, PCI_CAP_ID_VNDR, sizeof(PCIBridgeQemuCap));
    if (msi_present(dev)) {
        msi_uninit(dev);
    }
    slotid_cap_cleanup(dev);
    if (shpc_present(dev)) {
        shpc_cleanup(dev, &bridge_dev->bar);
    }
    pci_bridge_exitfn(dev);
}

static void pci_bridge_dev_instance_finalize(Object *obj)
{
    /* this function is idempotent and handles (PCIDevice.shpc == NULL) */
    shpc_free(PCI_DEVICE(obj));
}

static void pci_bridge_dev_write_config(PCIDevice *d,
                                        uint32_t address, uint32_t val, int len)
{
    pci_bridge_write_config(d, address, val, len);
    if (msi_present(d)) {
        msi_write_config(d, address, val, len);
    }
    if (shpc_present(d)) {
        shpc_cap_write_config(d, address, val, len);
    }
}

static void qdev_pci_bridge_dev_reset(DeviceState *qdev)
{
    PCIDevice *dev = PCI_DEVICE(qdev);

    pci_bridge_reset(qdev);
    if (shpc_present(dev)) {
        shpc_reset(dev);
    }
}

static const Property pci_bridge_dev_properties[] = {
                    /* Note: 0 is not a legal chassis number. */
    DEFINE_PROP_UINT8(PCI_BRIDGE_DEV_PROP_CHASSIS_NR, PCIBridgeDev, chassis_nr,
                      0),
    DEFINE_PROP_ON_OFF_AUTO(PCI_BRIDGE_DEV_PROP_MSI, PCIBridgeDev, msi,
                            ON_OFF_AUTO_AUTO),
    DEFINE_PROP_BIT(PCI_BRIDGE_DEV_PROP_SHPC, PCIBridgeDev, flags,
                    PCI_BRIDGE_DEV_F_SHPC_REQ, true),
    DEFINE_PROP_UINT32("bus-reserve", PCIBridgeDev,
                       res_reserve.bus, -1),
    DEFINE_PROP_SIZE("io-reserve", PCIBridgeDev,
                     res_reserve.io, -1),
    DEFINE_PROP_SIZE("mem-reserve", PCIBridgeDev,
                     res_reserve.mem_non_pref, -1),
    DEFINE_PROP_SIZE("pref32-reserve", PCIBridgeDev,
                     res_reserve.mem_pref_32, -1),
    DEFINE_PROP_SIZE("pref64-reserve", PCIBridgeDev,
                     res_reserve.mem_pref_64, -1),
};

static bool pci_device_shpc_present(void *opaque, int version_id)
{
    PCIDevice *dev = opaque;

    return shpc_present(dev);
}

static const VMStateDescription pci_bridge_dev_vmstate = {
    .name = "pci_bridge",
    .priority = MIG_PRI_PCI_BUS,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBridge),
        SHPC_VMSTATE(shpc, PCIDevice, pci_device_shpc_present),
        VMSTATE_END_OF_LIST()
    }
};

void pci_bridge_dev_plug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                            Error **errp)
{
    PCIDevice *pci_hotplug_dev = PCI_DEVICE(hotplug_dev);

    if (!shpc_present(pci_hotplug_dev)) {
        error_setg(errp, "standard hotplug controller has been disabled for "
                   "this %s", object_get_typename(OBJECT(hotplug_dev)));
        return;
    }
    shpc_device_plug_cb(hotplug_dev, dev, errp);
}

void pci_bridge_dev_unplug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                              Error **errp)
{
    PCIDevice *pci_hotplug_dev = PCI_DEVICE(hotplug_dev);

    g_assert(shpc_present(pci_hotplug_dev));
    shpc_device_unplug_cb(hotplug_dev, dev, errp);
}

void pci_bridge_dev_unplug_request_cb(HotplugHandler *hotplug_dev,
                                      DeviceState *dev, Error **errp)
{
    PCIDevice *pci_hotplug_dev = PCI_DEVICE(hotplug_dev);

    if (!shpc_present(pci_hotplug_dev)) {
        error_setg(errp, "standard hotplug controller has been disabled for "
                   "this %s", object_get_typename(OBJECT(hotplug_dev)));
        return;
    }
    shpc_device_unplug_request_cb(hotplug_dev, dev, errp);
}

static void pci_bridge_dev_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);

    k->realize = pci_bridge_dev_realize;
    k->exit = pci_bridge_dev_exitfn;
    k->config_write = pci_bridge_dev_write_config;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_BRIDGE;
    k->class_id = PCI_CLASS_BRIDGE_PCI;
    dc->desc = "Standard PCI Bridge";
    device_class_set_legacy_reset(dc, qdev_pci_bridge_dev_reset);
    device_class_set_props(dc, pci_bridge_dev_properties);
    dc->vmsd = &pci_bridge_dev_vmstate;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    hc->plug = pci_bridge_dev_plug_cb;
    hc->unplug = pci_bridge_dev_unplug_cb;
    hc->unplug_request = pci_bridge_dev_unplug_request_cb;
}

static const TypeInfo pci_bridge_dev_info = {
    .name              = TYPE_PCI_BRIDGE_DEV,
    .parent            = TYPE_PCI_BRIDGE,
    .instance_size     = sizeof(PCIBridgeDev),
    .class_init        = pci_bridge_dev_class_init,
    .instance_finalize = pci_bridge_dev_instance_finalize,
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    }
};

/*
 * Multiseat bridge.  Same as the standard pci bridge, only with a
 * different pci id, so we can match it easily in the guest for
 * automagic multiseat configuration.  See docs/multiseat.txt for more.
 */
static void pci_bridge_dev_seat_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->device_id = PCI_DEVICE_ID_REDHAT_BRIDGE_SEAT;
    dc->desc = "Standard PCI Bridge (multiseat)";
}

static const TypeInfo pci_bridge_dev_seat_info = {
    .name              = TYPE_PCI_BRIDGE_SEAT_DEV,
    .parent            = TYPE_PCI_BRIDGE_DEV,
    .instance_size     = sizeof(PCIBridgeDev),
    .class_init        = pci_bridge_dev_seat_class_init,
};

static void pci_bridge_dev_register(void)
{
    type_register_static(&pci_bridge_dev_info);
    type_register_static(&pci_bridge_dev_seat_info);
}

type_init(pci_bridge_dev_register);
