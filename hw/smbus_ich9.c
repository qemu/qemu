/*
 * ACPI implementation
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */
/*
 *  Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
 *                     VA Linux Systems Japan K.K.
 *  Copyright (C) 2012 Jason Baron <jbaron@redhat.com>
 *
 *  This is based on acpi.c, but heavily rewritten.
 */
#include "hw.h"
#include "pc.h"
#include "pm_smbus.h"
#include "pci.h"
#include "sysemu.h"
#include "i2c.h"
#include "smbus.h"

#include "ich9.h"

#define TYPE_ICH9_SMB_DEVICE "ICH9 SMB"
#define ICH9_SMB_DEVICE(obj) \
     OBJECT_CHECK(ICH9SMBState, (obj), TYPE_ICH9_SMB_DEVICE)

typedef struct ICH9SMBState {
    PCIDevice dev;

    PMSMBus smb;
    MemoryRegion mem_bar;
} ICH9SMBState;

static const VMStateDescription vmstate_ich9_smbus = {
    .name = "ich9_smb",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, struct ICH9SMBState),
        VMSTATE_END_OF_LIST()
    }
};

static void ich9_smb_ioport_writeb(void *opaque, hwaddr addr,
                                   uint64_t val, unsigned size)
{
    ICH9SMBState *s = opaque;
    uint8_t hostc = s->dev.config[ICH9_SMB_HOSTC];

    if ((hostc & ICH9_SMB_HOSTC_HST_EN) && !(hostc & ICH9_SMB_HOSTC_I2C_EN)) {
        uint64_t offset = addr - s->dev.io_regions[ICH9_SMB_SMB_BASE_BAR].addr;
        smb_ioport_writeb(&s->smb, offset, val);
    }
}

static uint64_t ich9_smb_ioport_readb(void *opaque, hwaddr addr,
                                      unsigned size)
{
    ICH9SMBState *s = opaque;
    uint8_t hostc = s->dev.config[ICH9_SMB_HOSTC];

    if ((hostc & ICH9_SMB_HOSTC_HST_EN) && !(hostc & ICH9_SMB_HOSTC_I2C_EN)) {
        uint64_t offset = addr - s->dev.io_regions[ICH9_SMB_SMB_BASE_BAR].addr;
        return smb_ioport_readb(&s->smb, offset);
    }

    return 0xff;
}

static const MemoryRegionOps lpc_smb_mmio_ops = {
    .read = ich9_smb_ioport_readb,
    .write = ich9_smb_ioport_writeb,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static int ich9_smbus_initfn(PCIDevice *d)
{
    ICH9SMBState *s = ICH9_SMB_DEVICE(d);

    /* TODO? D31IP.SMIP in chipset configuration space */
    pci_config_set_interrupt_pin(d->config, 0x01); /* interrupt pin 1 */

    pci_set_byte(d->config + ICH9_SMB_HOSTC, 0);

    /*
     * update parameters based on
     * paralell_hds[0]
     * serial_hds[0]
     * serial_hds[0]
     * fdc
     *
     * Is there any OS that depends on them?
     */

    /* TODO smb_io_base */
    pci_set_byte(d->config + ICH9_SMB_HOSTC, 0);
    /* TODO bar0, bar1: 64bit BAR support*/

    memory_region_init_io(&s->mem_bar, &lpc_smb_mmio_ops, s, "ich9-smbus-bar",
                            ICH9_SMB_SMB_BASE_SIZE);
    pci_register_bar(d, ICH9_SMB_SMB_BASE_BAR, PCI_BASE_ADDRESS_SPACE_IO,
                        &s->mem_bar);
    pm_smbus_init(&d->qdev, &s->smb);
    return 0;
}

static void ich9_smb_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_ICH9_6;
    k->revision = ICH9_A2_SMB_REVISION;
    k->class_id = PCI_CLASS_SERIAL_SMBUS;
    dc->no_user = 1;
    dc->vmsd = &vmstate_ich9_smbus;
    dc->desc = "ICH9 SMBUS Bridge";
    k->init = ich9_smbus_initfn;
}

i2c_bus *ich9_smb_init(PCIBus *bus, int devfn, uint32_t smb_io_base)
{
    PCIDevice *d =
        pci_create_simple_multifunction(bus, devfn, true, TYPE_ICH9_SMB_DEVICE);
    ICH9SMBState *s = ICH9_SMB_DEVICE(d);
    return s->smb.smbus;
}

static const TypeInfo ich9_smb_info = {
    .name   = TYPE_ICH9_SMB_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(ICH9SMBState),
    .class_init = ich9_smb_class_init,
};

static void ich9_smb_register(void)
{
    type_register_static(&ich9_smb_info);
}

type_init(ich9_smb_register);
