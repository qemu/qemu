/*
 * x86 variant of the generic event device for hw reduced acpi
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#include "qemu/osdep.h"
#include "hw/acpi/generic_event_device.h"

static const TypeInfo acpi_ged_x86_info = {
    .name          = TYPE_ACPI_GED_X86,
    .parent        = TYPE_ACPI_GED,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { TYPE_ACPI_DEVICE_IF },
        { }
    }
};

static void acpi_ged_x86_register_types(void)
{
    type_register_static(&acpi_ged_x86_info);
}

type_init(acpi_ged_x86_register_types)
