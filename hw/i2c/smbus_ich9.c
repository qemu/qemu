/*
 * ACPI implementation
 *
 * Copyright (c) 2006 Fabrice Bellard
 * Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
 *               VA Linux Systems Japan K.K.
 * Copyright (C) 2012 Jason Baron <jbaron@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "qemu/range.h"
#include "hw/i2c/pm_smbus.h"
#include "hw/pci/pci.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

#include "hw/i386/ich9.h"
#include "qom/object.h"
#include "hw/acpi/acpi_aml_interface.h"

OBJECT_DECLARE_SIMPLE_TYPE(ICH9SMBState, ICH9_SMB_DEVICE)

struct ICH9SMBState {
    PCIDevice dev;

    bool irq_enabled;

    PMSMBus smb;
};

static bool ich9_vmstate_need_smbus(void *opaque, int version_id)
{
    return pm_smbus_vmstate_needed();
}

static const VMStateDescription vmstate_ich9_smbus = {
    .name = "ich9_smb",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, ICH9SMBState),
        VMSTATE_BOOL_TEST(irq_enabled, ICH9SMBState, ich9_vmstate_need_smbus),
        VMSTATE_STRUCT_TEST(smb, ICH9SMBState, ich9_vmstate_need_smbus, 1,
                            pmsmb_vmstate, PMSMBus),
        VMSTATE_END_OF_LIST()
    }
};

static void ich9_smbus_write_config(PCIDevice *d, uint32_t address,
                                    uint32_t val, int len)
{
    ICH9SMBState *s = ICH9_SMB_DEVICE(d);

    pci_default_write_config(d, address, val, len);
    if (range_covers_byte(address, len, ICH9_SMB_HOSTC)) {
        uint8_t hostc = s->dev.config[ICH9_SMB_HOSTC];
        if (hostc & ICH9_SMB_HOSTC_HST_EN) {
            memory_region_set_enabled(&s->smb.io, true);
        } else {
            memory_region_set_enabled(&s->smb.io, false);
        }
        s->smb.i2c_enable = (hostc & ICH9_SMB_HOSTC_I2C_EN) != 0;
        if (hostc & ICH9_SMB_HOSTC_SSRESET) {
            s->smb.reset(&s->smb);
            s->dev.config[ICH9_SMB_HOSTC] &= ~ICH9_SMB_HOSTC_SSRESET;
        }
    }
}

static void ich9_smbus_realize(PCIDevice *d, Error **errp)
{
    ICH9SMBState *s = ICH9_SMB_DEVICE(d);

    /* TODO? D31IP.SMIP in chipset configuration space */
    pci_config_set_interrupt_pin(d->config, 0x01); /* interrupt pin 1 */

    pci_set_byte(d->config + ICH9_SMB_HOSTC, 0);
    /* TODO bar0, bar1: 64bit BAR support*/

    pm_smbus_init(&d->qdev, &s->smb, false);
    pci_register_bar(d, ICH9_SMB_SMB_BASE_BAR, PCI_BASE_ADDRESS_SPACE_IO,
                     &s->smb.io);
}

static void build_ich9_smb_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    ICH9SMBState *s = ICH9_SMB_DEVICE(adev);
    BusState *bus = BUS(s->smb.smbus);

    qbus_build_aml(bus, scope);
}

static void ich9_smb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    AcpiDevAmlIfClass *adevc = ACPI_DEV_AML_IF_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_ICH9_6;
    k->revision = ICH9_A2_SMB_REVISION;
    k->class_id = PCI_CLASS_SERIAL_SMBUS;
    dc->vmsd = &vmstate_ich9_smbus;
    dc->desc = "ICH9 SMBUS Bridge";
    k->realize = ich9_smbus_realize;
    k->config_write = ich9_smbus_write_config;
    /*
     * Reason: part of ICH9 southbridge, needs to be wired up by
     * pc_q35_init()
     */
    dc->user_creatable = false;
    adevc->build_dev_aml = build_ich9_smb_aml;
}

static void ich9_smb_set_irq(PMSMBus *pmsmb, bool enabled)
{
    ICH9SMBState *s = pmsmb->opaque;

    if (enabled == s->irq_enabled) {
        return;
    }

    s->irq_enabled = enabled;
    pci_set_irq(&s->dev, enabled);
}

I2CBus *ich9_smb_init(PCIBus *bus, int devfn, uint32_t smb_io_base)
{
    PCIDevice *d =
        pci_create_simple_multifunction(bus, devfn, true, TYPE_ICH9_SMB_DEVICE);
    ICH9SMBState *s = ICH9_SMB_DEVICE(d);
    s->smb.set_irq = ich9_smb_set_irq;
    s->smb.opaque = s;
    return s->smb.smbus;
}

static const TypeInfo ich9_smb_info = {
    .name   = TYPE_ICH9_SMB_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(ICH9SMBState),
    .class_init = ich9_smb_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { TYPE_ACPI_DEV_AML_IF },
        { },
    },
};

static void ich9_smb_register(void)
{
    type_register_static(&ich9_smb_info);
}

type_init(ich9_smb_register);
