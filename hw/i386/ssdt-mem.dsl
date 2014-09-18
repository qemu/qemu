/*
 * Memory hotplug ACPI DSDT static objects definitions
 *
 * Copyright ProfitBricks GmbH 2012
 * Copyright (C) 2013-2014 Red Hat Inc
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

/* This file is the basis for the ssdt_mem[] variable in src/acpi.c.
 * It defines the contents of the memory device object.  At
 * runtime, a dynamically generated SSDT will contain one copy of this
 * AML snippet for every possible memory device in the system.  The
 * objects will be placed in the \_SB_ namespace.
 *
 * In addition to the aml code generated from this file, the
 * src/acpi.c file creates a MTFY method with an entry for each memdevice:
 *     Method(MTFY, 2) {
 *         If (LEqual(Arg0, 0x00)) { Notify(MP00, Arg1) }
 *         If (LEqual(Arg0, 0x01)) { Notify(MP01, Arg1) }
 *         ...
 *     }
 */
#include "hw/acpi/pc-hotplug.h"

ACPI_EXTRACT_ALL_CODE ssdm_mem_aml

DefinitionBlock ("ssdt-mem.aml", "SSDT", 0x02, "BXPC", "CSSDT", 0x1)
{

    External(\_SB.PCI0.MEMORY_HOTPLUG_DEVICE.MEMORY_SLOT_CRS_METHOD, MethodObj)
    External(\_SB.PCI0.MEMORY_HOTPLUG_DEVICE.MEMORY_SLOT_STATUS_METHOD, MethodObj)
    External(\_SB.PCI0.MEMORY_HOTPLUG_DEVICE.MEMORY_SLOT_OST_METHOD, MethodObj)
    External(\_SB.PCI0.MEMORY_HOTPLUG_DEVICE.MEMORY_SLOT_PROXIMITY_METHOD, MethodObj)

    Scope(\_SB) {
/*  v------------------ DO NOT EDIT ------------------v */
        ACPI_EXTRACT_DEVICE_START ssdt_mem_start
        ACPI_EXTRACT_DEVICE_END ssdt_mem_end
        ACPI_EXTRACT_DEVICE_STRING ssdt_mem_name
        Device(MPAA) {
            ACPI_EXTRACT_NAME_STRING ssdt_mem_id
            Name(_UID, "0xAA")
/*  ^------------------ DO NOT EDIT ------------------^
 * Don't change the above without also updating the C code.
 */
            Name(_HID, EISAID("PNP0C80"))

            Method(_CRS, 0) {
                Return(\_SB.PCI0.MEMORY_HOTPLUG_DEVICE.MEMORY_SLOT_CRS_METHOD(_UID))
            }

            Method(_STA, 0) {
                Return(\_SB.PCI0.MEMORY_HOTPLUG_DEVICE.MEMORY_SLOT_STATUS_METHOD(_UID))
            }

            Method(_PXM, 0) {
                Return(\_SB.PCI0.MEMORY_HOTPLUG_DEVICE.MEMORY_SLOT_PROXIMITY_METHOD(_UID))
            }

            Method(_OST, 3) {
                \_SB.PCI0.MEMORY_HOTPLUG_DEVICE.MEMORY_SLOT_OST_METHOD(_UID, Arg0, Arg1, Arg2)
            }
        }
    }
}
