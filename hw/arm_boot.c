/* 
 * ARM kernel loader.
 *
 * Copyright (c) 2006 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

#include "vl.h"

#define KERNEL_ARGS_ADDR 0x100
#define KERNEL_LOAD_ADDR 0x00010000
#define INITRD_LOAD_ADDR 0x00800000

/* The worlds second smallest bootloader.  Set r0-r2, then jump to kernel.  */
static uint32_t bootloader[] = {
  0xe3a00000, /* mov     r0, #0 */
  0xe3a01000, /* mov     r1, #0x?? */
  0xe3811c00, /* orr     r1, r1, #0x??00 */
  0xe59f2000, /* ldr     r2, [pc, #0] */
  0xe59ff000, /* ldr     pc, [pc, #0] */
  0, /* Address of kernel args.  Set by integratorcp_init.  */
  0  /* Kernel entry point.  Set by integratorcp_init.  */
};

static void set_kernel_args(uint32_t ram_size, int initrd_size,
                            const char *kernel_cmdline)
{
    uint32_t *p;

    p = (uint32_t *)(phys_ram_base + KERNEL_ARGS_ADDR);
    /* ATAG_CORE */
    stl_raw(p++, 5);
    stl_raw(p++, 0x54410001);
    stl_raw(p++, 1);
    stl_raw(p++, 0x1000);
    stl_raw(p++, 0);
    /* ATAG_MEM */
    stl_raw(p++, 4);
    stl_raw(p++, 0x54410002);
    stl_raw(p++, ram_size);
    stl_raw(p++, 0);
    if (initrd_size) {
        /* ATAG_INITRD2 */
        stl_raw(p++, 4);
        stl_raw(p++, 0x54420005);
        stl_raw(p++, INITRD_LOAD_ADDR);
        stl_raw(p++, initrd_size);
    }
    if (kernel_cmdline && *kernel_cmdline) {
        /* ATAG_CMDLINE */
        int cmdline_size;

        cmdline_size = strlen(kernel_cmdline);
        memcpy (p + 2, kernel_cmdline, cmdline_size + 1);
        cmdline_size = (cmdline_size >> 2) + 1;
        stl_raw(p++, cmdline_size + 2);
        stl_raw(p++, 0x54410009);
        p += cmdline_size;
    }
    /* ATAG_END */
    stl_raw(p++, 0);
    stl_raw(p++, 0);
}

void arm_load_kernel(int ram_size, const char *kernel_filename,
                     const char *kernel_cmdline, const char *initrd_filename,
                     int board_id)
{
    int kernel_size;
    int initrd_size;
    int n;

    /* Load the kernel.  */
    if (!kernel_filename) {
        fprintf(stderr, "Kernel image must be specified\n");
        exit(1);
    }
    kernel_size = load_image(kernel_filename,
                             phys_ram_base + KERNEL_LOAD_ADDR);
    if (kernel_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n", kernel_filename);
        exit(1);
    }
    if (initrd_filename) {
        initrd_size = load_image(initrd_filename,
                                 phys_ram_base + INITRD_LOAD_ADDR);
        if (initrd_size < 0) {
            fprintf(stderr, "qemu: could not load initrd '%s'\n",
                    initrd_filename);
            exit(1);
        }
    } else {
        initrd_size = 0;
    }
    bootloader[1] |= board_id & 0xff;
    bootloader[2] |= (board_id >> 8) & 0xff;
    bootloader[5] = KERNEL_ARGS_ADDR;
    bootloader[6] = KERNEL_LOAD_ADDR;
    for (n = 0; n < sizeof(bootloader) / 4; n++)
        stl_raw(phys_ram_base + (n * 4), bootloader[n]);
    set_kernel_args(ram_size, initrd_size, kernel_cmdline);
}

