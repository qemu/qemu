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
#include "hw/acpi/cpu_hotplug.h"
#include "qapi/error.h"
#include "hw/core/cpu.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "qemu/error-report.h"

#define CPU_EJECT_METHOD "CPEJ"
#define CPU_MAT_METHOD "CPMA"
#define CPU_ON_BITMAP "CPON"
#define CPU_STATUS_METHOD "CPST"
#define CPU_STATUS_MAP "PRS"
#define CPU_SCAN_METHOD "PRSC"

static uint64_t cpu_status_read(void *opaque, hwaddr addr, unsigned int size)
{
    AcpiCpuHotplug *cpus = opaque;
    uint64_t val = cpus->sts[addr];

    return val;
}

static void cpu_status_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned int size)
{
    /* firmware never used to write in CPU present bitmap so use
       this fact as means to switch QEMU into modern CPU hotplug
       mode by writing 0 at the beginning of legacy CPU bitmap
     */
    if (addr == 0 && data == 0) {
        AcpiCpuHotplug *cpus = opaque;
        object_property_set_bool(cpus->device, "cpu-hotplug-legacy", false,
                                 &error_abort);
    }
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

static void acpi_set_cpu_present_bit(AcpiCpuHotplug *g, CPUState *cpu)
{
    CPUClass *k = CPU_GET_CLASS(cpu);
    int64_t cpu_id;

    cpu_id = k->get_arch_id(cpu);
    if ((cpu_id / 8) >= ACPI_GPE_PROC_LEN) {
        object_property_set_bool(g->device, "cpu-hotplug-legacy", false,
                                 &error_abort);
        return;
    }

    g->sts[cpu_id / 8] |= (1 << (cpu_id % 8));
}

void legacy_acpi_cpu_plug_cb(HotplugHandler *hotplug_dev,
                             AcpiCpuHotplug *g, DeviceState *dev, Error **errp)
{
    acpi_set_cpu_present_bit(g, CPU(dev));
    acpi_send_event(DEVICE(hotplug_dev), ACPI_CPU_HOTPLUG_STATUS);
}

void legacy_acpi_cpu_hotplug_init(MemoryRegion *parent, Object *owner,
                                  AcpiCpuHotplug *gpe_cpu, uint16_t base)
{
    CPUState *cpu;

    memory_region_init_io(&gpe_cpu->io, owner, &AcpiCpuHotplug_ops,
                          gpe_cpu, "acpi-cpu-hotplug", ACPI_GPE_PROC_LEN);
    memory_region_add_subregion(parent, base, &gpe_cpu->io);
    gpe_cpu->device = owner;

    CPU_FOREACH(cpu) {
        acpi_set_cpu_present_bit(gpe_cpu, cpu);
    }
}

void acpi_switch_to_modern_cphp(AcpiCpuHotplug *gpe_cpu,
                                CPUHotplugState *cpuhp_state,
                                uint16_t io_port)
{
    MemoryRegion *parent = pci_address_space_io(PCI_DEVICE(gpe_cpu->device));

    memory_region_del_subregion(parent, &gpe_cpu->io);
    cpu_hotplug_hw_init(parent, gpe_cpu->device, cpuhp_state, io_port);
}

void build_legacy_cpu_hotplug_aml(Aml *ctx, MachineState *machine,
                                  uint16_t io_base)
{
    Aml *dev;
    Aml *crs;
    Aml *pkg;
    Aml *field;
    Aml *method;
    Aml *if_ctx;
    Aml *else_ctx;
    int i, apic_idx;
    Aml *sb_scope = aml_scope("_SB");
    uint8_t madt_tmpl[8] = {0x00, 0x08, 0x00, 0x00, 0x00, 0, 0, 0};
    Aml *cpu_id = aml_arg(1);
    Aml *apic_id = aml_arg(0);
    Aml *cpu_on = aml_local(0);
    Aml *madt = aml_local(1);
    Aml *cpus_map = aml_name(CPU_ON_BITMAP);
    Aml *zero = aml_int(0);
    Aml *one = aml_int(1);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const CPUArchIdList *apic_ids = mc->possible_cpu_arch_ids(machine);
    X86MachineState *x86ms = X86_MACHINE(machine);

    /*
     * _MAT method - creates an madt apic buffer
     * apic_id = Arg0 = Local APIC ID
     * cpu_id  = Arg1 = Processor ID
     * cpu_on = Local0 = CPON flag for this cpu
     * madt = Local1 = Buffer (in madt apic form) to return
     */
    method = aml_method(CPU_MAT_METHOD, 2, AML_NOTSERIALIZED);
    aml_append(method,
        aml_store(aml_derefof(aml_index(cpus_map, apic_id)), cpu_on));
    aml_append(method,
        aml_store(aml_buffer(sizeof(madt_tmpl), madt_tmpl), madt));
    /* Update the processor id, lapic id, and enable/disable status */
    aml_append(method, aml_store(cpu_id, aml_index(madt, aml_int(2))));
    aml_append(method, aml_store(apic_id, aml_index(madt, aml_int(3))));
    aml_append(method, aml_store(cpu_on, aml_index(madt, aml_int(4))));
    aml_append(method, aml_return(madt));
    aml_append(sb_scope, method);

    /*
     * _STA method - return ON status of cpu
     * apic_id = Arg0 = Local APIC ID
     * cpu_on = Local0 = CPON flag for this cpu
     */
    method = aml_method(CPU_STATUS_METHOD, 1, AML_NOTSERIALIZED);
    aml_append(method,
        aml_store(aml_derefof(aml_index(cpus_map, apic_id)), cpu_on));
    if_ctx = aml_if(cpu_on);
    {
        aml_append(if_ctx, aml_return(aml_int(0xF)));
    }
    aml_append(method, if_ctx);
    else_ctx = aml_else();
    {
        aml_append(else_ctx, aml_return(zero));
    }
    aml_append(method, else_ctx);
    aml_append(sb_scope, method);

    method = aml_method(CPU_EJECT_METHOD, 2, AML_NOTSERIALIZED);
    aml_append(method, aml_sleep(200));
    aml_append(sb_scope, method);

    method = aml_method(CPU_SCAN_METHOD, 0, AML_NOTSERIALIZED);
    {
        Aml *while_ctx, *if_ctx2, *else_ctx2;
        Aml *bus_check_evt = aml_int(1);
        Aml *remove_evt = aml_int(3);
        Aml *status_map = aml_local(5); /* Local5 = active cpu bitmap */
        Aml *byte = aml_local(2); /* Local2 = last read byte from bitmap */
        Aml *idx = aml_local(0); /* Processor ID / APIC ID iterator */
        Aml *is_cpu_on = aml_local(1); /* Local1 = CPON flag for cpu */
        Aml *status = aml_local(3); /* Local3 = active state for cpu */

        aml_append(method, aml_store(aml_name(CPU_STATUS_MAP), status_map));
        aml_append(method, aml_store(zero, byte));
        aml_append(method, aml_store(zero, idx));

        /* While (idx < SizeOf(CPON)) */
        while_ctx = aml_while(aml_lless(idx, aml_sizeof(cpus_map)));
        aml_append(while_ctx,
            aml_store(aml_derefof(aml_index(cpus_map, idx)), is_cpu_on));

        if_ctx = aml_if(aml_and(idx, aml_int(0x07), NULL));
        {
            /* Shift down previously read bitmap byte */
            aml_append(if_ctx, aml_shiftright(byte, one, byte));
        }
        aml_append(while_ctx, if_ctx);

        else_ctx = aml_else();
        {
            /* Read next byte from cpu bitmap */
            aml_append(else_ctx, aml_store(aml_derefof(aml_index(status_map,
                       aml_shiftright(idx, aml_int(3), NULL))), byte));
        }
        aml_append(while_ctx, else_ctx);

        aml_append(while_ctx, aml_store(aml_and(byte, one, NULL), status));
        if_ctx = aml_if(aml_lnot(aml_equal(is_cpu_on, status)));
        {
            /* State change - update CPON with new state */
            aml_append(if_ctx, aml_store(status, aml_index(cpus_map, idx)));
            if_ctx2 = aml_if(aml_equal(status, one));
            {
                aml_append(if_ctx2,
                    aml_call2(AML_NOTIFY_METHOD, idx, bus_check_evt));
            }
            aml_append(if_ctx, if_ctx2);
            else_ctx2 = aml_else();
            {
                aml_append(else_ctx2,
                    aml_call2(AML_NOTIFY_METHOD, idx, remove_evt));
            }
        }
        aml_append(if_ctx, else_ctx2);
        aml_append(while_ctx, if_ctx);

        aml_append(while_ctx, aml_increment(idx)); /* go to next cpu */
        aml_append(method, while_ctx);
    }
    aml_append(sb_scope, method);

    /* The current AML generator can cover the APIC ID range [0..255],
     * inclusive, for VCPU hotplug. */
    QEMU_BUILD_BUG_ON(ACPI_CPU_HOTPLUG_ID_LIMIT > 256);
    if (x86ms->apic_id_limit > ACPI_CPU_HOTPLUG_ID_LIMIT) {
        error_report("max_cpus is too large. APIC ID of last CPU is %u",
                     x86ms->apic_id_limit - 1);
        exit(1);
    }

    /* create PCI0.PRES device and its _CRS to reserve CPU hotplug MMIO */
    dev = aml_device("PCI0." stringify(CPU_HOTPLUG_RESOURCE_DEVICE));
    aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0A06")));
    aml_append(dev,
        aml_name_decl("_UID", aml_string("CPU Hotplug resources"))
    );
    /* device present, functioning, decoding, not shown in UI */
    aml_append(dev, aml_name_decl("_STA", aml_int(0xB)));
    crs = aml_resource_template();
    aml_append(crs,
        aml_io(AML_DECODE16, io_base, io_base, 1, ACPI_GPE_PROC_LEN)
    );
    aml_append(dev, aml_name_decl("_CRS", crs));
    aml_append(sb_scope, dev);
    /* declare CPU hotplug MMIO region and PRS field to access it */
    aml_append(sb_scope, aml_operation_region(
        "PRST", AML_SYSTEM_IO, aml_int(io_base), ACPI_GPE_PROC_LEN));
    field = aml_field("PRST", AML_BYTE_ACC, AML_NOLOCK, AML_PRESERVE);
    aml_append(field, aml_named_field("PRS", 256));
    aml_append(sb_scope, field);

    /* build Processor object for each processor */
    for (i = 0; i < apic_ids->len; i++) {
        int apic_id = apic_ids->cpus[i].arch_id;

        assert(apic_id < ACPI_CPU_HOTPLUG_ID_LIMIT);

        dev = aml_processor(i, 0, 0, "CP%.02X", apic_id);

        method = aml_method("_MAT", 0, AML_NOTSERIALIZED);
        aml_append(method,
            aml_return(aml_call2(CPU_MAT_METHOD, aml_int(apic_id), aml_int(i))
        ));
        aml_append(dev, method);

        method = aml_method("_STA", 0, AML_NOTSERIALIZED);
        aml_append(method,
            aml_return(aml_call1(CPU_STATUS_METHOD, aml_int(apic_id))));
        aml_append(dev, method);

        method = aml_method("_EJ0", 1, AML_NOTSERIALIZED);
        aml_append(method,
            aml_return(aml_call2(CPU_EJECT_METHOD, aml_int(apic_id),
                aml_arg(0)))
        );
        aml_append(dev, method);

        aml_append(sb_scope, dev);
    }

    /* build this code:
     *   Method(NTFY, 2) {If (LEqual(Arg0, 0x00)) {Notify(CP00, Arg1)} ...}
     */
    /* Arg0 = APIC ID */
    method = aml_method(AML_NOTIFY_METHOD, 2, AML_NOTSERIALIZED);
    for (i = 0; i < apic_ids->len; i++) {
        int apic_id = apic_ids->cpus[i].arch_id;

        if_ctx = aml_if(aml_equal(aml_arg(0), aml_int(apic_id)));
        aml_append(if_ctx,
            aml_notify(aml_name("CP%.02X", apic_id), aml_arg(1))
        );
        aml_append(method, if_ctx);
    }
    aml_append(sb_scope, method);

    /* build "Name(CPON, Package() { One, One, ..., Zero, Zero, ... })"
     *
     * Note: The ability to create variable-sized packages was first
     * introduced in ACPI 2.0. ACPI 1.0 only allowed fixed-size packages
     * ith up to 255 elements. Windows guests up to win2k8 fail when
     * VarPackageOp is used.
     */
    pkg = x86ms->apic_id_limit <= 255 ? aml_package(x86ms->apic_id_limit) :
                                        aml_varpackage(x86ms->apic_id_limit);

    for (i = 0, apic_idx = 0; i < apic_ids->len; i++) {
        int apic_id = apic_ids->cpus[i].arch_id;

        for (; apic_idx < apic_id; apic_idx++) {
            aml_append(pkg, aml_int(0));
        }
        aml_append(pkg, aml_int(apic_ids->cpus[i].cpu ? 1 : 0));
        apic_idx = apic_id + 1;
    }
    aml_append(sb_scope, aml_name_decl(CPU_ON_BITMAP, pkg));
    aml_append(ctx, sb_scope);

    method = aml_method("\\_GPE._E02", 0, AML_NOTSERIALIZED);
    aml_append(method, aml_call0("\\_SB." CPU_SCAN_METHOD));
    aml_append(ctx, method);
}
