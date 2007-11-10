/*
 * Dummy board with just RAM and CPU for use as an ISS.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licenced under the GPL
 */

#include "vl.h"

#define KERNEL_LOAD_ADDR 0x10000

/* Board init.  */

static void dummy_m68k_init(int ram_size, int vga_ram_size, int boot_device,
                     DisplayState *ds, const char **fd_filename, int snapshot,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    int kernel_size;
    uint64_t elf_entry;
    target_ulong entry;

    env = cpu_init();
    if (!cpu_model)
        cpu_model = "cfv4e";
    if (cpu_m68k_set_model(env, cpu_model)) {
        cpu_abort(env, "Unable to find m68k CPU definition\n");
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
    "dummy",
    "Dummy board",
    dummy_m68k_init,
};
