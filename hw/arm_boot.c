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
#include "loader.h"
#include "elf.h"

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
  0xe59f0020, /* ldr     r0, privbase */
  0xe3a01001, /* mov     r1, #1 */
  0xe5801100, /* str     r1, [r0, #0x100] */
  0xe3a00201, /* mov     r0, #0x10000000 */
  0xe3800030, /* orr     r0, #0x30 */
  0xe320f003, /* wfi */
  0xe5901000, /* ldr     r1, [r0] */
  0xe1110001, /* tst     r1, r1 */
  0x0afffffb, /* beq     <wfi> */
  0xe12fff11, /* bx      r1 */
  0 /* privbase: Private memory region base address.  */
};

#define WRITE_WORD(p, value) do { \
    stl_phys_notdirty(p, value);  \
    p += 4;                       \
} while (0)

static void set_kernel_args(struct arm_boot_info *info,
                int initrd_size, target_phys_addr_t base)
{
    target_phys_addr_t p;

    p = base + KERNEL_ARGS_ADDR;
    /* ATAG_CORE */
    WRITE_WORD(p, 5);
    WRITE_WORD(p, 0x54410001);
    WRITE_WORD(p, 1);
    WRITE_WORD(p, 0x1000);
    WRITE_WORD(p, 0);
    /* ATAG_MEM */
    /* TODO: handle multiple chips on one ATAG list */
    WRITE_WORD(p, 4);
    WRITE_WORD(p, 0x54410002);
    WRITE_WORD(p, info->ram_size);
    WRITE_WORD(p, info->loader_start);
    if (initrd_size) {
        /* ATAG_INITRD2 */
        WRITE_WORD(p, 4);
        WRITE_WORD(p, 0x54420005);
        WRITE_WORD(p, info->loader_start + INITRD_LOAD_ADDR);
        WRITE_WORD(p, initrd_size);
    }
    if (info->kernel_cmdline && *info->kernel_cmdline) {
        /* ATAG_CMDLINE */
        int cmdline_size;

        cmdline_size = strlen(info->kernel_cmdline);
        cpu_physical_memory_write(p + 8, (void *)info->kernel_cmdline,
                                  cmdline_size + 1);
        cmdline_size = (cmdline_size >> 2) + 1;
        WRITE_WORD(p, cmdline_size + 2);
        WRITE_WORD(p, 0x54410009);
        p += cmdline_size * 4;
    }
    if (info->atag_board) {
        /* ATAG_BOARD */
        int atag_board_len;
        uint8_t atag_board_buf[0x1000];

        atag_board_len = (info->atag_board(info, atag_board_buf) + 3) & ~3;
        WRITE_WORD(p, (atag_board_len + 8) >> 2);
        WRITE_WORD(p, 0x414f4d50);
        cpu_physical_memory_write(p, atag_board_buf, atag_board_len);
        p += atag_board_len;
    }
    /* ATAG_END */
    WRITE_WORD(p, 0);
    WRITE_WORD(p, 0);
}

static void set_kernel_args_old(struct arm_boot_info *info,
                int initrd_size, target_phys_addr_t base)
{
    target_phys_addr_t p;
    const char *s;


    /* see linux/include/asm-arm/setup.h */
    p = base + KERNEL_ARGS_ADDR;
    /* page_size */
    WRITE_WORD(p, 4096);
    /* nr_pages */
    WRITE_WORD(p, info->ram_size / 4096);
    /* ramdisk_size */
    WRITE_WORD(p, 0);
#define FLAG_READONLY	1
#define FLAG_RDLOAD	4
#define FLAG_RDPROMPT	8
    /* flags */
    WRITE_WORD(p, FLAG_READONLY | FLAG_RDLOAD | FLAG_RDPROMPT);
    /* rootdev */
    WRITE_WORD(p, (31 << 8) | 0);	/* /dev/mtdblock0 */
    /* video_num_cols */
    WRITE_WORD(p, 0);
    /* video_num_rows */
    WRITE_WORD(p, 0);
    /* video_x */
    WRITE_WORD(p, 0);
    /* video_y */
    WRITE_WORD(p, 0);
    /* memc_control_reg */
    WRITE_WORD(p, 0);
    /* unsigned char sounddefault */
    /* unsigned char adfsdrives */
    /* unsigned char bytes_per_char_h */
    /* unsigned char bytes_per_char_v */
    WRITE_WORD(p, 0);
    /* pages_in_bank[4] */
    WRITE_WORD(p, 0);
    WRITE_WORD(p, 0);
    WRITE_WORD(p, 0);
    WRITE_WORD(p, 0);
    /* pages_in_vram */
    WRITE_WORD(p, 0);
    /* initrd_start */
    if (initrd_size)
        WRITE_WORD(p, info->loader_start + INITRD_LOAD_ADDR);
    else
        WRITE_WORD(p, 0);
    /* initrd_size */
    WRITE_WORD(p, initrd_size);
    /* rd_start */
    WRITE_WORD(p, 0);
    /* system_rev */
    WRITE_WORD(p, 0);
    /* system_serial_low */
    WRITE_WORD(p, 0);
    /* system_serial_high */
    WRITE_WORD(p, 0);
    /* mem_fclk_21285 */
    WRITE_WORD(p, 0);
    /* zero unused fields */
    while (p < base + KERNEL_ARGS_ADDR + 256 + 1024) {
        WRITE_WORD(p, 0);
    }
    s = info->kernel_cmdline;
    if (s) {
        cpu_physical_memory_write(p, (void *)s, strlen(s) + 1);
    } else {
        WRITE_WORD(p, 0);
    }
}

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    struct arm_boot_info *info = env->boot_info;

    cpu_reset(env);
    if (info) {
        if (!info->is_linux) {
            /* Jump to the entry point.  */
            env->regs[15] = info->entry & 0xfffffffe;
            env->thumb = info->entry & 1;
        } else {
            env->regs[15] = info->loader_start;
            if (old_param) {
                set_kernel_args_old(info, info->initrd_size,
                                    info->loader_start);
            } else {
                set_kernel_args(info, info->initrd_size, info->loader_start);
            }
        }
    }
    /* TODO:  Reset secondary CPUs.  */
}

void arm_load_kernel(CPUState *env, struct arm_boot_info *info)
{
    int kernel_size;
    int initrd_size;
    int n;
    int is_linux = 0;
    uint64_t elf_entry;
    target_phys_addr_t entry;
    int big_endian;

    /* Load the kernel.  */
    if (!info->kernel_filename) {
        fprintf(stderr, "Kernel image must be specified\n");
        exit(1);
    }

    if (info->nb_cpus == 0)
        info->nb_cpus = 1;
    env->boot_info = info;

#ifdef TARGET_WORDS_BIGENDIAN
    big_endian = 1;
#else
    big_endian = 0;
#endif

    /* Assume that raw images are linux kernels, and ELF images are not.  */
    kernel_size = load_elf(info->kernel_filename, NULL, NULL, &elf_entry,
                           NULL, NULL, big_endian, ELF_MACHINE, 1);
    entry = elf_entry;
    if (kernel_size < 0) {
        kernel_size = load_uimage(info->kernel_filename, &entry, NULL,
                                  &is_linux);
    }
    if (kernel_size < 0) {
        entry = info->loader_start + KERNEL_LOAD_ADDR;
        kernel_size = load_image_targphys(info->kernel_filename, entry,
                                          ram_size - KERNEL_LOAD_ADDR);
        is_linux = 1;
    }
    if (kernel_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n",
                info->kernel_filename);
        exit(1);
    }
    info->entry = entry;
    if (is_linux) {
        if (info->initrd_filename) {
            initrd_size = load_image_targphys(info->initrd_filename,
                                              info->loader_start
                                              + INITRD_LOAD_ADDR,
                                              ram_size - INITRD_LOAD_ADDR);
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
        for (n = 0; n < sizeof(bootloader) / 4; n++) {
            bootloader[n] = tswap32(bootloader[n]);
        }
        rom_add_blob_fixed("bootloader", bootloader, sizeof(bootloader),
                           info->loader_start);
        if (info->nb_cpus > 1) {
            smpboot[10] = info->smp_priv_base;
            for (n = 0; n < sizeof(smpboot) / 4; n++) {
                smpboot[n] = tswap32(smpboot[n]);
            }
            rom_add_blob_fixed("smpboot", smpboot, sizeof(smpboot),
                               info->smp_loader_start);
        }
        info->initrd_size = initrd_size;
    }
    info->is_linux = is_linux;
    qemu_register_reset(main_cpu_reset, env);
}
