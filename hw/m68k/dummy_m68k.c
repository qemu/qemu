/*
 * Dummy board with just RAM and CPU for use as an ISS.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GPL
 */

#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "exec/address-spaces.h"

#define KERNEL_LOAD_ADDR 0x10000

/* Board init.  */

static void dummy_m68k_init(QEMUMachineInitArgs *args)
{
    ram_addr_t ram_size = args->ram_size;
    const char *cpu_model = args->cpu_model;
    const char *kernel_filename = args->kernel_filename;
    CPUM68KState *env;
    MemoryRegion *address_space_mem =  get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    int kernel_size;
    uint64_t elf_entry;
    hwaddr entry;

    if (!cpu_model)
        cpu_model = "cfv4e";
    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find m68k CPU definition\n");
        exit(1);
    }

    /* Initialize CPU registers.  */
    env->vbr = 0;

    /* RAM at address zero */
    memory_region_init_ram(ram, NULL, "dummy_m68k.ram", ram_size);
    vmstate_register_ram_global(ram);
    memory_region_add_subregion(address_space_mem, 0, ram);

    /* Load kernel.  */
    if (kernel_filename) {
        kernel_size = load_elf(kernel_filename, NULL, NULL, &elf_entry,
                               NULL, NULL, 1, ELF_MACHINE, 0);
        entry = elf_entry;
        if (kernel_size < 0) {
            kernel_size = load_uimage(kernel_filename, &entry, NULL, NULL);
        }
        if (kernel_size < 0) {
            kernel_size = load_image_targphys(kernel_filename,
                                              KERNEL_LOAD_ADDR,
                                              ram_size - KERNEL_LOAD_ADDR);
            entry = KERNEL_LOAD_ADDR;
        }
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
    } else {
        entry = 0;
    }
    env->pc = entry;
}

static QEMUMachine dummy_m68k_machine = {
    .name = "dummy",
    .desc = "Dummy board",
    .init = dummy_m68k_init,
};

static void dummy_m68k_machine_init(void)
{
    qemu_register_machine(&dummy_m68k_machine);
}

machine_init(dummy_m68k_machine_init);
