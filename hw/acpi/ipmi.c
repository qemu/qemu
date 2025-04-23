/*
 * IPMI ACPI firmware handling
 *
 * Copyright (c) 2015,2016 Corey Minyard, MontaVista Software, LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/ipmi/ipmi.h"
#include "hw/acpi/aml-build.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/ipmi.h"

static Aml *aml_ipmi_crs(IPMIFwInfo *info)
{
    Aml *crs = aml_resource_template();

    /*
     * The base address is fixed and cannot change.  That may be different
     * if someone does PCI, but we aren't there yet.
     */
    switch (info->memspace) {
    case IPMI_MEMSPACE_IO:
        aml_append(crs, aml_io(AML_DECODE16, info->base_address,
                               info->base_address + info->register_length - 1,
                               info->register_spacing, info->register_length));
        break;
    case IPMI_MEMSPACE_MEM32:
        aml_append(crs,
                   aml_dword_memory(AML_POS_DECODE,
                            AML_MIN_FIXED, AML_MAX_FIXED,
                            AML_NON_CACHEABLE, AML_READ_WRITE,
                            0xffffffff,
                            info->base_address,
                            info->base_address + info->register_length - 1,
                            info->register_spacing, info->register_length));
        break;
    case IPMI_MEMSPACE_MEM64:
        aml_append(crs,
                   aml_qword_memory(AML_POS_DECODE,
                            AML_MIN_FIXED, AML_MAX_FIXED,
                            AML_NON_CACHEABLE, AML_READ_WRITE,
                            0xffffffffffffffffULL,
                            info->base_address,
                            info->base_address + info->register_length - 1,
                            info->register_spacing, info->register_length));
        break;
    case IPMI_MEMSPACE_SMBUS:
        aml_append(crs, aml_i2c_serial_bus_device(info->base_address,
                                                  "^"));
        break;
    default:
        abort();
    }

    /* Should PCI interrupts also be appended? */
    if (info->irq_source == IPMI_ISA_IRQ && info->interrupt_number) {
        aml_append(crs, aml_irq_no_flags(info->interrupt_number));
    }

    return crs;
}

void build_ipmi_dev_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    Aml *dev;
    IPMIFwInfo info = {};
    IPMIInterface *ii = IPMI_INTERFACE(adev);
    IPMIInterfaceClass *iic = IPMI_INTERFACE_GET_CLASS(ii);
    uint16_t version;

    iic->get_fwinfo(ii, &info);
    assert(info.ipmi_spec_minor_revision <= 15);
    version = ((info.ipmi_spec_major_revision << 8)
              | (info.ipmi_spec_minor_revision << 4));

    dev = aml_device("MI%d", info.uuid);
    aml_append(dev, aml_name_decl("_HID", aml_eisaid("IPI0001")));
    aml_append(dev, aml_name_decl("_STR", aml_string("ipmi_%s",
                                                     info.interface_name)));
    aml_append(dev, aml_name_decl("_UID", aml_int(info.uuid)));
    aml_append(dev, aml_name_decl("_CRS", aml_ipmi_crs(&info)));
    aml_append(dev, aml_name_decl("_IFT", aml_int(info.interface_type)));
    aml_append(dev, aml_name_decl("_SRV", aml_int(version)));

    aml_append(scope, dev);
}
