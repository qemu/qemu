/*
 * ACPI implementation
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef HW_ACPI_PIIX4_H
#define HW_ACPI_PIIX4_H

#include "hw/pci/pci.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/cpu_hotplug.h"
#include "hw/acpi/memory_hotplug.h"
#include "hw/acpi/pcihp.h"
#include "hw/i2c/pm_smbus.h"
#include "hw/isa/apm.h"

#define TYPE_PIIX4_PM "PIIX4_PM"
OBJECT_DECLARE_SIMPLE_TYPE(PIIX4PMState, PIIX4_PM)

struct PIIX4PMState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    MemoryRegion io;
    uint32_t io_base;

    MemoryRegion io_gpe;
    ACPIREGS ar;

    APMState apm;

    PMSMBus smb;
    uint32_t smb_io_base;

    qemu_irq irq;
    qemu_irq smi_irq;
    bool smm_enabled;
    bool smm_compat;
    Notifier machine_ready;
    Notifier powerdown_notifier;

    AcpiPciHpState acpi_pci_hotplug;
    bool use_acpi_hotplug_bridge;
    bool use_acpi_root_pci_hotplug;
    bool not_migrate_acpi_index;

    uint8_t disable_s3;
    uint8_t disable_s4;
    uint8_t s4_val;

    bool cpu_hotplug_legacy;
    AcpiCpuHotplug gpe_cpu;
    CPUHotplugState cpuhp_state;

    MemHotplugState acpi_memory_hotplug;
};

#endif
