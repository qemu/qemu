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

#include "qapi/qapi-types-acpi.h"
#include "hw/qdev-core.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/aml-build.h"
#include "hw/boards.h"
#include "hw/hotplug.h"

#define ACPI_CPU_HOTPLUG_REG_LEN 12

typedef struct AcpiCpuStatus {
    CPUState *cpu;
    uint64_t arch_id;
    bool is_inserting;
    bool is_removing;
    bool fw_remove;
    uint32_t ost_event;
    uint32_t ost_status;
} AcpiCpuStatus;

typedef struct CPUHotplugState {
    MemoryRegion ctrl_reg;
    uint32_t selector;
    uint8_t command;
    uint32_t dev_count;
    AcpiCpuStatus *devs;
} CPUHotplugState;

void acpi_cpu_plug_cb(HotplugHandler *hotplug_dev,
                      CPUHotplugState *cpu_st, DeviceState *dev, Error **errp);

void acpi_cpu_unplug_request_cb(HotplugHandler *hotplug_dev,
                                CPUHotplugState *cpu_st,
                                DeviceState *dev, Error **errp);

void acpi_cpu_unplug_cb(CPUHotplugState *cpu_st,
                        DeviceState *dev, Error **errp);

void cpu_hotplug_hw_init(MemoryRegion *as, Object *owner,
                         CPUHotplugState *state, hwaddr base_addr);

typedef struct CPUHotplugFeatures {
    bool acpi_1_compatible;
    bool has_legacy_cphp;
    bool fw_unplugs_cpu;
    const char *smi_path;
} CPUHotplugFeatures;

typedef void (*build_madt_cpu_fn)(int uid, const CPUArchIdList *apic_ids,
                                  GArray *entry, bool force_enabled);

void build_cpus_aml(Aml *table, MachineState *machine, CPUHotplugFeatures opts,
                    build_madt_cpu_fn build_madt_cpu, hwaddr base_addr,
                    const char *res_root,
                    const char *event_handler_method,
                    AmlRegionSpace rs);

void acpi_cpu_ospm_status(CPUHotplugState *cpu_st, ACPIOSTInfoList ***list);

extern const VMStateDescription vmstate_cpu_hotplug;
#define VMSTATE_CPU_HOTPLUG(cpuhp, state) \
    VMSTATE_STRUCT(cpuhp, state, 1, \
                   vmstate_cpu_hotplug, CPUHotplugState)

#endif
