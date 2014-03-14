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
#include "hw/hw.h"
#include "hw/acpi/cpu_hotplug.h"

static uint64_t cpu_status_read(void *opaque, hwaddr addr, unsigned int size)
{
    AcpiCpuHotplug *cpus = opaque;
    uint64_t val = cpus->sts[addr];

    return val;
}

static void cpu_status_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned int size)
{
    /* TODO: implement VCPU removal on guest signal that CPU can be removed */
}

static const MemoryRegionOps AcpiCpuHotplug_ops = {
    .read = cpu_status_read,
    .write = cpu_status_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

void AcpiCpuHotplug_add(ACPIGPE *gpe, AcpiCpuHotplug *g, CPUState *cpu)
{
    CPUClass *k = CPU_GET_CLASS(cpu);
    int64_t cpu_id;

    *gpe->sts = *gpe->sts | ACPI_CPU_HOTPLUG_STATUS;
    cpu_id = k->get_arch_id(CPU(cpu));
    g_assert((cpu_id / 8) < ACPI_GPE_PROC_LEN);
    g->sts[cpu_id / 8] |= (1 << (cpu_id % 8));
}

void AcpiCpuHotplug_init(MemoryRegion *parent, Object *owner,
                         AcpiCpuHotplug *gpe_cpu, uint16_t base)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        CPUClass *cc = CPU_GET_CLASS(cpu);
        int64_t id = cc->get_arch_id(cpu);

        g_assert((id / 8) < ACPI_GPE_PROC_LEN);
        gpe_cpu->sts[id / 8] |= (1 << (id % 8));
    }
    memory_region_init_io(&gpe_cpu->io, owner, &AcpiCpuHotplug_ops,
                          gpe_cpu, "acpi-cpu-hotplug", ACPI_GPE_PROC_LEN);
    memory_region_add_subregion(parent, base, &gpe_cpu->io);
}
