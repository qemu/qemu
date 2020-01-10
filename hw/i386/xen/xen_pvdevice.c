/* Copyright (c) Citrix Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms,
 * with or without modification, are permitted provided
 * that the following conditions are met:
 *
 * *   Redistributions of source code must retain the above
 *     copyright notice, this list of conditions and the
 *     following disclaimer.
 * *   Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the
 *     following disclaimer in the documentation and/or other
 *     materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "trace.h"

#define TYPE_XEN_PV_DEVICE  "xen-pvdevice"

#define XEN_PV_DEVICE(obj) \
     OBJECT_CHECK(XenPVDevice, (obj), TYPE_XEN_PV_DEVICE)

typedef struct XenPVDevice {
    /*< private >*/
    PCIDevice       parent_obj;
    /*< public >*/
    uint16_t        vendor_id;
    uint16_t        device_id;
    uint8_t         revision;
    uint32_t        size;
    MemoryRegion    mmio;
} XenPVDevice;

static uint64_t xen_pv_mmio_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    trace_xen_pv_mmio_read(addr);

    return ~(uint64_t)0;
}

static void xen_pv_mmio_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned size)
{
    trace_xen_pv_mmio_write(addr);
}

static const MemoryRegionOps xen_pv_mmio_ops = {
    .read = &xen_pv_mmio_read,
    .write = &xen_pv_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const VMStateDescription vmstate_xen_pvdevice = {
    .name = "xen-pvdevice",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, XenPVDevice),
        VMSTATE_END_OF_LIST()
    }
};

static void xen_pv_realize(PCIDevice *pci_dev, Error **errp)
{
    XenPVDevice *d = XEN_PV_DEVICE(pci_dev);
    uint8_t *pci_conf;

    /* device-id property must always be supplied */
    if (d->device_id == 0xffff) {
        error_setg(errp, "Device ID invalid, it must always be supplied");
        return;
    }

    pci_conf = pci_dev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, d->vendor_id);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, d->vendor_id);
    pci_set_word(pci_conf + PCI_DEVICE_ID, d->device_id);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, d->device_id);
    pci_set_byte(pci_conf + PCI_REVISION_ID, d->revision);

    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_MEMORY);

    pci_config_set_prog_interface(pci_conf, 0);

    pci_conf[PCI_INTERRUPT_PIN] = 1;

    memory_region_init_io(&d->mmio, NULL, &xen_pv_mmio_ops, d,
                          "mmio", d->size);

    pci_register_bar(pci_dev, 1, PCI_BASE_ADDRESS_MEM_PREFETCH,
                     &d->mmio);
}

static Property xen_pv_props[] = {
    DEFINE_PROP_UINT16("vendor-id", XenPVDevice, vendor_id, PCI_VENDOR_ID_XEN),
    DEFINE_PROP_UINT16("device-id", XenPVDevice, device_id, 0xffff),
    DEFINE_PROP_UINT8("revision", XenPVDevice, revision, 0x01),
    DEFINE_PROP_UINT32("size", XenPVDevice, size, 0x400000),
    DEFINE_PROP_END_OF_LIST()
};

static void xen_pv_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = xen_pv_realize;
    k->class_id = PCI_CLASS_SYSTEM_OTHER;
    dc->desc = "Xen PV Device";
    device_class_set_props(dc, xen_pv_props);
    dc->vmsd = &vmstate_xen_pvdevice;
}

static const TypeInfo xen_pv_type_info = {
    .name          = TYPE_XEN_PV_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(XenPVDevice),
    .class_init    = xen_pv_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void xen_pv_register_types(void)
{
    type_register_static(&xen_pv_type_info);
}

type_init(xen_pv_register_types)
