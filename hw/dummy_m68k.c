/*
 * Dummy board with just RAM and CPU for use as an ISS.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licenced under the GPL
 */

#include "hw.h"
#include "sysemu.h"
#include "boards.h"

#define KERNEL_LOAD_ADDR 0x10000

/* Board init.  */

static void dummy_m68k_init(ram_addr_t ram_size, int vga_ram_size,
                     const char *boot_device, DisplayState *ds,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    int kernel_size;
    uint64_t elf_entry;
    target_ulong entry;

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
    cpu_register_physical_memory(0, ram_size,
        qemu_ram_alloc(ram_size) | IO_MEM_RAM);

    /* Load kernel.  */
    if (kernel_filename) {
        kernel_size = load_elf(kernel_filename, 0, &elf_entry, NULL, NULL);
        entry = elf_entry;
        if (kernel_size < 0) {
            kernel_size = load_uboot(kernel_filename, &entry, NULL);
        }
        if (kernel_size < 0) {
            kernel_size = load_image(kernel_filename,
                                     phys_ram_base + KERNEL_LOAD_ADDR);
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

QEMUMachine dummy_m68k_machine = {
    .name = "dummy",
    .desc = "Dummy board",
    .init = dummy_m68k_init,
    .max_cpus = 1,
};
