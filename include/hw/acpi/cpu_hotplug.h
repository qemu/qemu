/*
 * QEMU ACPI hotplug utilities
 *
 * Copyright (C) 2013 Red Hat Inc
 *
 * Authors:
 *   Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_ACPI_CPU_HOTPLUG_H
#define HW_ACPI_CPU_HOTPLUG_H

#include "hw/acpi/acpi.h"
#include "hw/acpi/pc-hotplug.h"
#include "hw/acpi/aml-build.h"
#include "hw/hotplug.h"
#include "hw/acpi/cpu.h"

typedef struct AcpiCpuHotplug {
    Object *device;
    MemoryRegion io;
    uint8_t sts[ACPI_GPE_PROC_LEN];
} AcpiCpuHotplug;

void legacy_acpi_cpu_plug_cb(HotplugHandler *hotplug_dev,
                             AcpiCpuHotplug *g, DeviceState *dev, Error **errp);

void legacy_acpi_cpu_hotplug_init(MemoryRegion *parent, Object *owner,
                                  AcpiCpuHotplug *gpe_cpu, uint16_t base);

void acpi_switch_to_modern_cphp(AcpiCpuHotplug *gpe_cpu,
                                CPUHotplugState *cpuhp_state,
                                uint16_t io_port);

void build_legacy_cpu_hotplug_aml(Aml *ctx, MachineState *machine,
                                  uint16_t io_base);
#endif
