/*
 * ARM kernel loader.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

#include "hw.h"
#include "arm-misc.h"
#include "sysemu.h"

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

/* Entry point for secondary CPUs.  Enable interrupt controller and
   Issue WFI until start address is written to system controller.  */
static uint32_t smpboot[] = {
  0xe3a00201, /* mov     r0, #0x10000000 */
  0xe3800601, /* orr     r0, r0, #0x001000000 */
  0xe3a01001, /* mov     r1, #1 */
  0xe5801100, /* str     r1, [r0, #0x100] */
  0xe3a00201, /* mov     r0, #0x10000000 */
  0xe3800030, /* orr     r0, #0x30 */
  0xe320f003, /* wfi */
  0xe5901000, /* ldr     r1, [r0] */
  0xe3110003, /* tst     r1, #3 */
  0x1afffffb, /* bne     <wfi> */
  0xe12fff11  /* bx      r1 */
};

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;

    cpu_reset(env);
    if (env->boot_info)
        arm_load_kernel(env, env->boot_info);

    /* TODO:  Reset secondary CPUs.  */
}

static void set_kernel_args(struct arm_boot_info *info,
                int initrd_size, void *base)
{
    uint32_t *p;

    p = (uint32_t *)(base + KERNEL_ARGS_ADDR);
    /* ATAG_CORE */
    stl_raw(p++, 5);
    stl_raw(p++, 0x54410001);
    stl_raw(p++, 1);
    stl_raw(p++, 0x1000);
    stl_raw(p++, 0);
    /* ATAG_MEM */
    /* TODO: handle multiple chips on one ATAG list */
    stl_raw(p++, 4);
    stl_raw(p++, 0x54410002);
    stl_raw(p++, info->ram_size);
    stl_raw(p++, info->loader_start);
    if (initrd_size) {
        /* ATAG_INITRD2 */
        stl_raw(p++, 4);
        stl_raw(p++, 0x54420005);
        stl_raw(p++, info->loader_start + INITRD_LOAD_ADDR);
        stl_raw(p++, initrd_size);
    }
    if (info->kernel_cmdline && *info->kernel_cmdline) {
        /* ATAG_CMDLINE */
        int cmdline_size;

        cmdline_size = strlen(info->kernel_cmdline);
        memcpy(p + 2, info->kernel_cmdline, cmdline_size + 1);
        cmdline_size = (cmdline_size >> 2) + 1;
        stl_raw(p++, cmdline_size + 2);
        stl_raw(p++, 0x54410009);
        p += cmdline_size;
    }
    if (info->atag_board) {
        /* ATAG_BOARD */
        int atag_board_len;

        atag_board_len = (info->atag_board(info, p + 2) + 3) >> 2;
        stl_raw(p++, 2 + atag_board_len);
        stl_raw(p++, 0x414f4d50);
        p += atag_board_len;
    }
    /* ATAG_END */
    stl_raw(p++, 0);
    stl_raw(p++, 0);
}

static void set_kernel_args_old(struct arm_boot_info *info,
                int initrd_size, void *base)
{
    uint32_t *p;
    char *s;

    /* see linux/include/asm-arm/setup.h */
    p = (uint32_t *)(base + KERNEL_ARGS_ADDR);
    /* page_size */
    stl_raw(p++, 4096);
    /* nr_pages */
    stl_raw(p++, info->ram_size / 4096);
    /* ramdisk_size */
    stl_raw(p++, 0);
#define FLAG_READONLY	1
#define FLAG_RDLOAD	4
#define FLAG_RDPROMPT	8
    /* flags */
    stl_raw(p++, FLAG_READONLY | FLAG_RDLOAD | FLAG_RDPROMPT);
    /* rootdev */
    stl_raw(p++, (31 << 8) | 0);	/* /dev/mtdblock0 */
    /* video_num_cols */
    stl_raw(p++, 0);
    /* video_num_rows */
    stl_raw(p++, 0);
    /* video_x */
    stl_raw(p++, 0);
    /* video_y */
    stl_raw(p++, 0);
    /* memc_control_reg */
    stl_raw(p++, 0);
    /* unsigned char sounddefault */
    /* unsigned char adfsdrives */
    /* unsigned char bytes_per_char_h */
    /* unsigned char bytes_per_char_v */
    stl_raw(p++, 0);
    /* pages_in_bank[4] */
    stl_raw(p++, 0);
    stl_raw(p++, 0);
    stl_raw(p++, 0);
    stl_raw(p++, 0);
    /* pages_in_vram */
    stl_raw(p++, 0);
    /* initrd_start */
    if (initrd_size)
        stl_raw(p++, info->loader_start + INITRD_LOAD_ADDR);
    else
        stl_raw(p++, 0);
    /* initrd_size */
    stl_raw(p++, initrd_size);
    /* rd_start */
    stl_raw(p++, 0);
    /* system_rev */
    stl_raw(p++, 0);
    /* system_serial_low */
    stl_raw(p++, 0);
    /* system_serial_high */
    stl_raw(p++, 0);
    /* mem_fclk_21285 */
    stl_raw(p++, 0);
    /* zero unused fields */
    memset(p, 0, 256 + 1024 -
           (p - ((uint32_t *)(base + KERNEL_ARGS_ADDR))));
    s = base + KERNEL_ARGS_ADDR + 256 + 1024;
    if (info->kernel_cmdline)
        strcpy (s, info->kernel_cmdline);
    else
        stb_raw(s, 0);
}

void arm_load_kernel(CPUState *env, struct arm_boot_info *info)
{
    int kernel_size;
    int initrd_size;
    int n;
    int is_linux = 0;
    uint64_t elf_entry;
    target_ulong entry;
    uint32_t pd;
    void *loader_phys;

    /* Load the kernel.  */
    if (!info->kernel_filename) {
        fprintf(stderr, "Kernel image must be specified\n");
        exit(1);
    }

    if (!env->boot_info) {
        if (info->nb_cpus == 0)
            info->nb_cpus = 1;
        env->boot_info = info;
        qemu_register_reset(main_cpu_reset, env);
    }

    pd = cpu_get_physical_page_desc(info->loader_start);
    loader_phys = phys_ram_base + (pd & TARGET_PAGE_MASK) +
            (info->loader_start & ~TARGET_PAGE_MASK);

    /* Assume that raw images are linux kernels, and ELF images are not.  */
    kernel_size = load_elf(info->kernel_filename, 0, &elf_entry, NULL, NULL);
    entry = elf_entry;
    if (kernel_size < 0) {
        kernel_size = load_uimage(info->kernel_filename, &entry, NULL,
                                  &is_linux);
    }
    if (kernel_size < 0) {
        kernel_size = load_image(info->kernel_filename,
                                 loader_phys + KERNEL_LOAD_ADDR);
        entry = info->loader_start + KERNEL_LOAD_ADDR;
        is_linux = 1;
    }
    if (kernel_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n",
                info->kernel_filename);
        exit(1);
    }
    if (!is_linux) {
        /* Jump to the entry point.  */
        env->regs[15] = entry & 0xfffffffe;
        env->thumb = entry & 1;
    } else {
        if (info->initrd_filename) {
            initrd_size = load_image(info->initrd_filename,
                                     loader_phys + INITRD_LOAD_ADDR);
            if (initrd_size < 0) {
                fprintf(stderr, "qemu: could not load initrd '%s'\n",
                        info->initrd_filename);
                exit(1);
            }
        } else {
            initrd_size = 0;
        }
        bootloader[1] |= info->board_id & 0xff;
        bootloader[2] |= (info->board_id >> 8) & 0xff;
        bootloader[5] = info->loader_start + KERNEL_ARGS_ADDR;
        bootloader[6] = entry;
        for (n = 0; n < sizeof(bootloader) / 4; n++)
            stl_raw(loader_phys + (n * 4), bootloader[n]);
        if (info->nb_cpus > 1)
            for (n = 0; n < sizeof(smpboot) / 4; n++)
                stl_raw(loader_phys + info->ram_size + (n * 4), smpboot[n]);
        if (old_param)
            set_kernel_args_old(info, initrd_size, loader_phys);
        else
            set_kernel_args(info, initrd_size, loader_phys);
    }
}
