/*
 * Arnewsh 5206 ColdFire system emulation.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licensed under the GPL
 */

#include "hw.h"
#include "mcf.h"
#include "boards.h"
#include "loader.h"
#include "elf.h"
#include "exec-memory.h"

#define KERNEL_LOAD_ADDR 0x10000
#define AN5206_MBAR_ADDR 0x10000000
#define AN5206_RAMBAR_ADDR 0x20000000

/* Board init.  */

static void an5206_init(ram_addr_t ram_size,
                     const char *boot_device,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    int kernel_size;
    uint64_t elf_entry;
    target_phys_addr_t entry;
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ram = g_new(MemoryRegion, 1);
    MemoryRegion *sram = g_new(MemoryRegion, 1);

    if (!cpu_model)
        cpu_model = "m5206";
    env = cpu_init(cpu_model);
    if (!env) {
        hw_error("Unable to find m68k CPU definition\n");
    }

    /* Initialize CPU registers.  */
    env->vbr = 0;
    /* TODO: allow changing MBAR and RAMBAR.  */
    env->mbar = AN5206_MBAR_ADDR | 1;
    env->rambar0 = AN5206_RAMBAR_ADDR | 1;

    /* DRAM at address zero */
    memory_region_init_ram(ram, NULL, "an5206.ram", ram_size);
    memory_region_add_subregion(address_space_mem, 0, ram);

    /* Internal SRAM.  */
    memory_region_init_ram(sram, NULL, "an5206.sram", 512);
    memory_region_add_subregion(address_space_mem, AN5206_RAMBAR_ADDR, sram);

    mcf5206_init(AN5206_MBAR_ADDR, env);

    /* Load kernel.  */
    if (!kernel_filename) {
        fprintf(stderr, "Kernel image must be specified\n");
        exit(1);
    }

    kernel_size = load_elf(kernel_filename, NULL, NULL, &elf_entry,
                           NULL, NULL, 1, ELF_MACHINE, 0);
    entry = elf_entry;
    if (kernel_size < 0) {
        kernel_size = load_uimage(kernel_filename, &entry, NULL, NULL);
    }
    if (kernel_size < 0) {
        kernel_size = load_image_targphys(kernel_filename, KERNEL_LOAD_ADDR,
                                          ram_size - KERNEL_LOAD_ADDR);
        entry = KERNEL_LOAD_ADDR;
    }
    if (kernel_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n", kernel_filename);
        exit(1);
    }

    env->pc = entry;
}

static QEMUMachine an5206_machine = {
    .name = "an5206",
    .desc = "Arnewsh 5206",
    .init = an5206_init,
};

static void an5206_machine_init(void)
{
    qemu_register_machine(&an5206_machine);
}

machine_init(an5206_machine_init);
