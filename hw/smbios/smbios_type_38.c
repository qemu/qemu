/*
 * IPMI SMBIOS firmware handling
 *
 * Copyright (c) 2015,2016 Corey Minyard, MontaVista Software, LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/ipmi/ipmi.h"
#include "hw/firmware/smbios.h"
#include "qemu/error-report.h"
#include "smbios_build.h"

/* SMBIOS type 38 - IPMI */
struct smbios_type_38 {
    struct smbios_structure_header header;
    uint8_t interface_type;
    uint8_t ipmi_spec_revision;
    uint8_t i2c_slave_address;
    uint8_t nv_storage_device_address;
    uint64_t base_address;
    uint8_t base_address_modifier;
    uint8_t interrupt_number;
} QEMU_PACKED;

static void smbios_build_one_type_38(IPMIFwInfo *info)
{
    uint64_t baseaddr = info->base_address;
    SMBIOS_BUILD_TABLE_PRE(38, 0x3000, true);

    t->interface_type = info->interface_type;
    t->ipmi_spec_revision = ((info->ipmi_spec_major_revision << 4)
                             | info->ipmi_spec_minor_revision);
    t->i2c_slave_address = info->i2c_slave_address;
    t->nv_storage_device_address = 0;

    assert(info->ipmi_spec_minor_revision <= 15);
    assert(info->ipmi_spec_major_revision <= 15);

    /* or 1 to set it to I/O space */
    switch (info->memspace) {
    case IPMI_MEMSPACE_IO:
        baseaddr |= 1;
        break;
    case IPMI_MEMSPACE_MEM32:
    case IPMI_MEMSPACE_MEM64:
        break;
    case IPMI_MEMSPACE_SMBUS:
        baseaddr <<= 1;
        break;
    }

    t->base_address = cpu_to_le64(baseaddr);

    t->base_address_modifier = 0;
    if (info->irq_type == IPMI_LEVEL_IRQ) {
        t->base_address_modifier |= 1;
    }
    switch (info->register_spacing) {
    case 1:
        break;
    case 4:
        t->base_address_modifier |= 1 << 6;
        break;
    case 16:
        t->base_address_modifier |= 2 << 6;
        break;
    default:
        error_report("IPMI register spacing %d is not compatible with"
                     " SMBIOS, ignoring this entry.", info->register_spacing);
        return;
    }
    if (info->irq_source == IPMI_ISA_IRQ) {
        t->interrupt_number = info->interrupt_number;
    } else {
        /* TODO: How to handle PCI? */
        t->interrupt_number = 0;
    }

    SMBIOS_BUILD_TABLE_POST;
}

static void smbios_add_ipmi_devices(BusState *bus)
{
    BusChild *kid;

    QTAILQ_FOREACH(kid, &bus->children,  sibling) {
        DeviceState *dev = kid->child;
        Object *obj = object_dynamic_cast(OBJECT(dev), TYPE_IPMI_INTERFACE);
        BusState *childbus;

        if (obj) {
            IPMIInterface *ii;
            IPMIInterfaceClass *iic;
            IPMIFwInfo info;

            ii = IPMI_INTERFACE(obj);
            iic = IPMI_INTERFACE_GET_CLASS(obj);
            memset(&info, 0, sizeof(info));
            if (!iic->get_fwinfo) {
                continue;
            }
            iic->get_fwinfo(ii, &info);
            smbios_build_one_type_38(&info);
            continue;
        }

        QLIST_FOREACH(childbus, &dev->child_bus, sibling) {
            smbios_add_ipmi_devices(childbus);
        }
    }
}

void smbios_build_type_38_table(void)
{
    BusState *bus;

    bus = sysbus_get_default();
    if (bus) {
        smbios_add_ipmi_devices(bus);
    }
}
