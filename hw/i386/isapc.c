/*
 * QEMU PC System Emulator
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"

#include "qemu/units.h"
#include "qemu/error-report.h"
#include "hw/char/parallel-isa.h"
#include "hw/dma/i8257.h"
#include "hw/i386/pc.h"
#include "hw/ide/isa.h"
#include "hw/ide/ide-bus.h"
#include "system/kvm.h"
#include "hw/i386/kvm/clock.h"
#include "hw/xen/xen-x86.h"
#include "system/xen.h"
#include "hw/rtc/mc146818rtc.h"
#include "target/i386/cpu.h"

static const int ide_iobase[MAX_IDE_BUS] = { 0x1f0, 0x170 };
static const int ide_iobase2[MAX_IDE_BUS] = { 0x3f6, 0x376 };
static const int ide_irq[MAX_IDE_BUS] = { 14, 15 };


static void pc_init_isa(MachineState *machine)
{
    PCMachineState *pcms = PC_MACHINE(machine);
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
    X86MachineState *x86ms = X86_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *system_io = get_system_io();
    ISABus *isa_bus;
    uint32_t irq;
    GSIState *gsi_state;
    MemoryRegion *ram_memory;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    int i;

    /*
     * There is a small chance that someone unintentionally passes "-cpu max"
     * for the isapc machine, which will provide a much more modern 32-bit
     * CPU than would be expected for an ISA-era PC. If the "max" cpu type has
     * been specified, choose the "best" 32-bit cpu possible which we consider
     * be the pentium3 (deliberately choosing an Intel CPU given that the
     * default 486 CPU for the isapc machine is also an Intel CPU).
     */
    if (!strcmp(machine->cpu_type, X86_CPU_TYPE_NAME("max"))) {
        machine->cpu_type = X86_CPU_TYPE_NAME("pentium3");
        warn_report("-cpu max is invalid for isapc machine, using pentium3");
    }

    /*
     * Similarly if someone unintentionally passes "-cpu host" for the isapc
     * machine then display a warning and also switch to the "best" 32-bit
     * cpu possible which we consider to be the pentium3. This is because any
     * host CPU will already be modern than this, but it also ensures any
     * newer CPU flags/features are filtered out for older guests.
     */
    if (!strcmp(machine->cpu_type, X86_CPU_TYPE_NAME("host"))) {
        machine->cpu_type = X86_CPU_TYPE_NAME("pentium3");
        warn_report("-cpu host is invalid for isapc machine, using pentium3");
    }

    if (machine->ram_size > 3.5 * GiB) {
        error_report("Too much memory for this machine: %" PRId64 " MiB, "
                     "maximum 3584 MiB", machine->ram_size / MiB);
        exit(1);
    }

    /*
     * There is no RAM split for the isapc machine
     */
    if (xen_enabled()) {
        xen_hvm_init_pc(pcms, &ram_memory);
    } else {
        ram_memory = machine->ram;

        pcms->max_ram_below_4g = 3.5 * GiB;
        x86ms->above_4g_mem_size = 0;
        x86ms->below_4g_mem_size = machine->ram_size;
    }

    x86_cpus_init(x86ms, pcmc->default_cpu_version);

    if (kvm_enabled()) {
        kvmclock_create(pcmc->kvmclock_create_always);
    }

    /* allocate ram and load rom/bios */
    if (!xen_enabled()) {
        pc_memory_init(pcms, system_memory, system_memory, 0);
    } else {
        assert(machine->ram_size == x86ms->below_4g_mem_size +
                                    x86ms->above_4g_mem_size);

        if (machine->kernel_filename != NULL) {
            /* For xen HVM direct kernel boot, load linux here */
            xen_load_linux(pcms);
        }
    }

    gsi_state = pc_gsi_create(&x86ms->gsi, false);

    isa_bus = isa_bus_new(NULL, system_memory, system_io,
                          &error_abort);
    isa_bus_register_input_irqs(isa_bus, x86ms->gsi);

    x86ms->rtc = isa_new(TYPE_MC146818_RTC);
    qdev_prop_set_int32(DEVICE(x86ms->rtc), "base_year", 2000);
    isa_realize_and_unref(x86ms->rtc, isa_bus, &error_fatal);
    irq = object_property_get_uint(OBJECT(x86ms->rtc), "irq",
                                   &error_fatal);
    isa_connect_gpio_out(ISA_DEVICE(x86ms->rtc), 0, irq);

    i8257_dma_init(OBJECT(machine), isa_bus, 0);
    pcms->hpet_enabled = false;

    if (x86ms->pic == ON_OFF_AUTO_ON || x86ms->pic == ON_OFF_AUTO_AUTO) {
        pc_i8259_create(isa_bus, gsi_state->i8259_irq);
    }

    if (tcg_enabled()) {
        x86_register_ferr_irq(x86ms->gsi[13]);
    }

    pc_vga_init(isa_bus, NULL);

    /* init basic PC hardware */
    pc_basic_device_init(pcms, isa_bus, x86ms->gsi, x86ms->rtc,
                         !MACHINE_CLASS(pcmc)->no_floppy, 0x4);

    pc_nic_init(pcmc, isa_bus, NULL);

    ide_drive_get(hd, ARRAY_SIZE(hd));
    for (i = 0; i < MAX_IDE_BUS; i++) {
        ISADevice *dev;
        char busname[] = "ide.0";
        dev = isa_ide_init(isa_bus, ide_iobase[i], ide_iobase2[i],
                            ide_irq[i],
                            hd[MAX_IDE_DEVS * i], hd[MAX_IDE_DEVS * i + 1]);
        /*
         * The ide bus name is ide.0 for the first bus and ide.1 for the
         * second one.
         */
        busname[4] = '0' + i;
        pcms->idebus[i] = qdev_get_child_bus(DEVICE(dev), busname);
    }
}

static void isapc_machine_options(MachineClass *m)
{
    static const char * const valid_cpu_types[] = {
        X86_CPU_TYPE_NAME("486"),
        X86_CPU_TYPE_NAME("athlon"),
        X86_CPU_TYPE_NAME("kvm32"),
        X86_CPU_TYPE_NAME("pentium"),
        X86_CPU_TYPE_NAME("pentium2"),
        X86_CPU_TYPE_NAME("pentium3"),
        X86_CPU_TYPE_NAME("qemu32"),
        X86_CPU_TYPE_NAME("max"),
        X86_CPU_TYPE_NAME("host"),
        NULL
    };
    PCMachineClass *pcmc = PC_MACHINE_CLASS(m);

    m->desc = "ISA-only PC";
    m->max_cpus = 1;
    m->option_rom_has_mr = true;
    m->rom_file_has_mr = false;
    pcmc->pci_enabled = false;
    pcmc->has_acpi_build = false;
    pcmc->smbios_defaults = false;
    pcmc->gigabyte_align = false;
    pcmc->smbios_legacy_mode = true;
    pcmc->has_reserved_memory = false;
    m->default_nic = "ne2k_isa";
    m->default_cpu_type = X86_CPU_TYPE_NAME("486");
    m->valid_cpu_types = valid_cpu_types;
    m->no_floppy = !module_object_class_by_name(TYPE_ISA_FDC);
    m->no_parallel = !module_object_class_by_name(TYPE_ISA_PARALLEL);
}

DEFINE_PC_MACHINE(isapc, "isapc", pc_init_isa,
                  isapc_machine_options);
