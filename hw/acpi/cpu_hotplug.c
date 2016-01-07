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
#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/acpi/cpu_hotplug.h"
#include "qom/cpu.h"

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

static void acpi_set_cpu_present_bit(AcpiCpuHotplug *g, CPUState *cpu,
                                     Error **errp)
{
    CPUClass *k = CPU_GET_CLASS(cpu);
    int64_t cpu_id;

    cpu_id = k->get_arch_id(cpu);
    if ((cpu_id / 8) >= ACPI_GPE_PROC_LEN) {
        error_setg(errp, "acpi: invalid cpu id: %" PRIi64, cpu_id);
        return;
    }

    g->sts[cpu_id / 8] |= (1 << (cpu_id % 8));
}

void acpi_cpu_plug_cb(ACPIREGS *ar, qemu_irq irq,
                      AcpiCpuHotplug *g, DeviceState *dev, Error **errp)
{
    acpi_set_cpu_present_bit(g, CPU(dev), errp);
    if (*errp != NULL) {
        return;
    }

    acpi_send_gpe_event(ar, irq, ACPI_CPU_HOTPLUG_STATUS);
}

void acpi_cpu_hotplug_init(MemoryRegion *parent, Object *owner,
                           AcpiCpuHotplug *gpe_cpu, uint16_t base)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        acpi_set_cpu_present_bit(gpe_cpu, cpu, &error_abort);
    }
    memory_region_init_io(&gpe_cpu->io, owner, &AcpiCpuHotplug_ops,
                          gpe_cpu, "acpi-cpu-hotplug", ACPI_GPE_PROC_LEN);
    memory_region_add_subregion(parent, base, &gpe_cpu->io);
}
