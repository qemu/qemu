/*
 * QEMU ACPI hotplug utilities
 *
 * Copyright (C) 2016 Red Hat Inc
 *
 * Authors:
 *   Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef ACPI_CPU_H
#define ACPI_CPU_H

#include "hw/qdev-core.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/hotplug.h"

typedef struct AcpiCpuStatus {
    struct CPUState *cpu;
    uint64_t arch_id;
} AcpiCpuStatus;

typedef struct CPUHotplugState {
    MemoryRegion ctrl_reg;
    uint32_t selector;
    uint32_t dev_count;
    AcpiCpuStatus *devs;
} CPUHotplugState;

void acpi_cpu_plug_cb(HotplugHandler *hotplug_dev,
                      CPUHotplugState *cpu_st, DeviceState *dev, Error **errp);

void cpu_hotplug_hw_init(MemoryRegion *as, Object *owner,
                         CPUHotplugState *state, hwaddr base_addr);

typedef struct CPUHotplugFeatures {
    bool apci_1_compatible;
} CPUHotplugFeatures;

void build_cpus_aml(Aml *table, MachineState *machine, CPUHotplugFeatures opts,
                    hwaddr io_base,
                    const char *res_root);

extern const VMStateDescription vmstate_cpu_hotplug;
#define VMSTATE_CPU_HOTPLUG(cpuhp, state) \
    VMSTATE_STRUCT(cpuhp, state, 1, \
                   vmstate_cpu_hotplug, CPUHotplugState)

#endif
