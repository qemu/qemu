/*
 * QEMU GMCH/ICH9 LPC PM Emulation
 *
 *  Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
 *                     VA Linux Systems Japan K.K.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef HW_ACPI_ICH9_H
#define HW_ACPI_ICH9_H

#include "hw/acpi/acpi.h"
#include "hw/acpi/cpu_hotplug.h"
#include "hw/acpi/cpu.h"
#include "hw/acpi/pcihp.h"
#include "hw/acpi/memory_hotplug.h"
#include "hw/acpi/acpi_dev_interface.h"
#include "hw/acpi/tco.h"

#define ACPI_PCIHP_ADDR_ICH9 0x0cc0

typedef struct ICH9LPCPMRegs {
    /*
     * In ich9 spec says that pm1_cnt register is 32bit width and
     * that the upper 16bits are reserved and unused.
     * PM1a_CNT_BLK = 2 in FADT so it is defined as uint16_t.
     */
    ACPIREGS acpi_regs;

    MemoryRegion io;
    MemoryRegion io_gpe;
    MemoryRegion io_smi;

    uint32_t smi_en;
    uint32_t smi_en_wmask;
    uint32_t smi_sts;

    qemu_irq irq;      /* SCI */

    uint32_t pm_io_base;
    Notifier powerdown_notifier;

    bool cpu_hotplug_legacy;
    AcpiCpuHotplug gpe_cpu;
    CPUHotplugState cpuhp_state;

    bool keep_pci_slot_hpc;
    bool use_acpi_hotplug_bridge;
    AcpiPciHpState acpi_pci_hotplug;
    MemHotplugState acpi_memory_hotplug;

    uint8_t disable_s3;
    uint8_t disable_s4;
    uint8_t s4_val;
    uint8_t smm_enabled;
    bool smm_compat;
    bool enable_tco;
    TCOIORegs tco_regs;
} ICH9LPCPMRegs;

#define ACPI_PM_PROP_TCO_ENABLED "enable_tco"

void ich9_pm_init(PCIDevice *lpc_pci, ICH9LPCPMRegs *pm,
                  bool smm_enabled,
                  qemu_irq sci_irq);

void ich9_pm_iospace_update(ICH9LPCPMRegs *pm, uint32_t pm_io_base);
extern const VMStateDescription vmstate_ich9_pm;

void ich9_pm_add_properties(Object *obj, ICH9LPCPMRegs *pm);

void ich9_pm_device_pre_plug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                                Error **errp);
void ich9_pm_device_plug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                            Error **errp);
void ich9_pm_device_unplug_request_cb(HotplugHandler *hotplug_dev,
                                      DeviceState *dev, Error **errp);
void ich9_pm_device_unplug_cb(HotplugHandler *hotplug_dev, DeviceState *dev,
                              Error **errp);

void ich9_pm_ospm_status(AcpiDeviceIf *adev, ACPIOSTInfoList ***list);
#endif /* HW_ACPI_ICH9_H */
