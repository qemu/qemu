/*
 * Arnewsh 5206 ColdFire system emulation.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GPL
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/m68k/mcf.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "qemu/error-report.h"
#include "sysemu/qtest.h"

#define KERNEL_LOAD_ADDR 0x10000
#define AN5206_MBAR_ADDR 0x10000000
#define AN5206_RAMBAR_ADDR 0x20000000

static void mcf5206_init(M68kCPU *cpu, MemoryRegion *sysmem, uint32_t base)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new(TYPE_MCF5206_MBAR);
    object_property_set_link(OBJECT(dev), "m68k-cpu",
                             OBJECT(cpu), &error_abort);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);

    memory_region_add_subregion(sysmem, base, sysbus_mmio_get_region(s, 0));
}

static void an5206_init(MachineState *machine)
{
    ram_addr_t ram_size = machine->ram_size;
    const char *kernel_filename = machine->kernel_filename;
    M68kCPU *cpu;
    CPUM68KState *env;
    int kernel_size;
    uint64_t elf_entry;
    hwaddr entry;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *sram = g_new(MemoryRegion, 1);

    cpu = M68K_CPU(cpu_create(machine->cpu_type));
    env = &cpu->env;

    /* Initialize CPU registers.  */
    env->vbr = 0;
    /* TODO: allow changing MBAR and RAMBAR.  */
    env->mbar = AN5206_MBAR_ADDR | 1;
    env->rambar0 = AN5206_RAMBAR_ADDR | 1;

    /* DRAM at address zero */
    memory_region_add_subregion(address_space_mem, 0, machine->ram);

    /* Internal SRAM.  */
    memory_region_init_ram(sram, NULL, "an5206.sram", 512, &error_fatal);
    memory_region_add_subregion(address_space_mem, AN5206_RAMBAR_ADDR, sram);

    mcf5206_init(cpu, address_space_mem, AN5206_MBAR_ADDR);

    /* Load kernel.  */
    if (!kernel_filename) {
        if (qtest_enabled()) {
            return;
        }
        error_report("Kernel image must be specified");
        exit(1);
    }

    kernel_size = load_elf(kernel_filename, NULL, NULL, NULL, &elf_entry,
                           NULL, NULL, NULL, 1, EM_68K, 0, 0);
    entry = elf_entry;
    if (kernel_size < 0) {
        kernel_size = load_uimage(kernel_filename, &entry, NULL, NULL,
                                  NULL, NULL);
    }
    if (kernel_size < 0) {
        kernel_size = load_image_targphys(kernel_filename, KERNEL_LOAD_ADDR,
                                          ram_size - KERNEL_LOAD_ADDR);
        entry = KERNEL_LOAD_ADDR;
    }
    if (kernel_size < 0) {
        error_report("Could not load kernel '%s'", kernel_filename);
        exit(1);
    }

    env->pc = entry;
}

static void an5206_machine_init(MachineClass *mc)
{
    mc->desc = "Arnewsh 5206";
    mc->init = an5206_init;
    mc->default_cpu_type = M68K_CPU_TYPE_NAME("m5206");
    mc->default_ram_id = "an5206.ram";
}

DEFINE_MACHINE("an5206", an5206_machine_init)
