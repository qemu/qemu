/*
 * Arnewsh 5206 ColdFire system emulation.
 *
 * Copyright (c) 2007 CodeSourcery.
 *
 * This code is licenced under the GPL
 */

#include "hw.h"
#include "pc.h"
#include "mcf.h"
#include "sysemu.h"
#include "boards.h"

#define KERNEL_LOAD_ADDR 0x10000
#define AN5206_MBAR_ADDR 0x10000000
#define AN5206_RAMBAR_ADDR 0x20000000

/* Stub functions for hardware that doesn't exist.  */
void pic_info(Monitor *mon)
{
}

void irq_info(Monitor *mon)
{
}

/* Board init.  */

static void an5206_init(ram_addr_t ram_size, int vga_ram_size,
                     const char *boot_device,
                     const char *kernel_filename, const char *kernel_cmdline,
                     const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    int kernel_size;
    uint64_t elf_entry;
    target_ulong entry;

    if (!cpu_model)
        cpu_model = "m5206";
    env = cpu_init(cpu_model);
    if (!env) {
        cpu_abort(env, "Unable to find m68k CPU definition\n");
    }

    /* Initialize CPU registers.  */
    env->vbr = 0;
    /* TODO: allow changing MBAR and RAMBAR.  */
    env->mbar = AN5206_MBAR_ADDR | 1;
    env->rambar0 = AN5206_RAMBAR_ADDR | 1;

    /* DRAM at address zero */
    cpu_register_physical_memory(0, ram_size,
        qemu_ram_alloc(ram_size) | IO_MEM_RAM);

    /* Internal SRAM.  */
    cpu_register_physical_memory(AN5206_RAMBAR_ADDR, 512,
        qemu_ram_alloc(512) | IO_MEM_RAM);

    mcf5206_init(AN5206_MBAR_ADDR, env);

    /* Load kernel.  */
    if (!kernel_filename) {
        fprintf(stderr, "Kernel image must be specified\n");
        exit(1);
    }

    kernel_size = load_elf(kernel_filename, 0, &elf_entry, NULL, NULL);
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

QEMUMachine an5206_machine = {
    .name = "an5206",
    .desc = "Arnewsh 5206",
    .init = an5206_init,
};
