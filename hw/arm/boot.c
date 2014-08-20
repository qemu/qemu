/*
 * ARM kernel loader.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "config.h"
#include "hw/hw.h"
#include "hw/arm/arm.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "sysemu/device_tree.h"
#include "qemu/config-file.h"
#include "exec/address-spaces.h"

/* Kernel boot protocol is specified in the kernel docs
 * Documentation/arm/Booting and Documentation/arm64/booting.txt
 * They have different preferred image load offsets from system RAM base.
 */
#define KERNEL_ARGS_ADDR 0x100
#define KERNEL_LOAD_ADDR 0x00010000
#define KERNEL64_LOAD_ADDR 0x00080000

typedef enum {
    FIXUP_NONE = 0,   /* do nothing */
    FIXUP_TERMINATOR, /* end of insns */
    FIXUP_BOARDID,    /* overwrite with board ID number */
    FIXUP_ARGPTR,     /* overwrite with pointer to kernel args */
    FIXUP_ENTRYPOINT, /* overwrite with kernel entry point */
    FIXUP_GIC_CPU_IF, /* overwrite with GIC CPU interface address */
    FIXUP_BOOTREG,    /* overwrite with boot register address */
    FIXUP_DSB,        /* overwrite with correct DSB insn for cpu */
    FIXUP_MAX,
} FixupType;

typedef struct ARMInsnFixup {
    uint32_t insn;
    FixupType fixup;
} ARMInsnFixup;

static const ARMInsnFixup bootloader_aarch64[] = {
    { 0x580000c0 }, /* ldr x0, arg ; Load the lower 32-bits of DTB */
    { 0xaa1f03e1 }, /* mov x1, xzr */
    { 0xaa1f03e2 }, /* mov x2, xzr */
    { 0xaa1f03e3 }, /* mov x3, xzr */
    { 0x58000084 }, /* ldr x4, entry ; Load the lower 32-bits of kernel entry */
    { 0xd61f0080 }, /* br x4      ; Jump to the kernel entry point */
    { 0, FIXUP_ARGPTR }, /* arg: .word @DTB Lower 32-bits */
    { 0 }, /* .word @DTB Higher 32-bits */
    { 0, FIXUP_ENTRYPOINT }, /* entry: .word @Kernel Entry Lower 32-bits */
    { 0 }, /* .word @Kernel Entry Higher 32-bits */
    { 0, FIXUP_TERMINATOR }
};

/* The worlds second smallest bootloader.  Set r0-r2, then jump to kernel.  */
static const ARMInsnFixup bootloader[] = {
    { 0xe3a00000 }, /* mov     r0, #0 */
    { 0xe59f1004 }, /* ldr     r1, [pc, #4] */
    { 0xe59f2004 }, /* ldr     r2, [pc, #4] */
    { 0xe59ff004 }, /* ldr     pc, [pc, #4] */
    { 0, FIXUP_BOARDID },
    { 0, FIXUP_ARGPTR },
    { 0, FIXUP_ENTRYPOINT },
    { 0, FIXUP_TERMINATOR }
};

/* Handling for secondary CPU boot in a multicore system.
 * Unlike the uniprocessor/primary CPU boot, this is platform
 * dependent. The default code here is based on the secondary
 * CPU boot protocol used on realview/vexpress boards, with
 * some parameterisation to increase its flexibility.
 * QEMU platform models for which this code is not appropriate
 * should override write_secondary_boot and secondary_cpu_reset_hook
 * instead.
 *
 * This code enables the interrupt controllers for the secondary
 * CPUs and then puts all the secondary CPUs into a loop waiting
 * for an interprocessor interrupt and polling a configurable
 * location for the kernel secondary CPU entry point.
 */
#define DSB_INSN 0xf57ff04f
#define CP15_DSB_INSN 0xee070f9a /* mcr cp15, 0, r0, c7, c10, 4 */

static const ARMInsnFixup smpboot[] = {
    { 0xe59f2028 }, /* ldr r2, gic_cpu_if */
    { 0xe59f0028 }, /* ldr r0, bootreg_addr */
    { 0xe3a01001 }, /* mov r1, #1 */
    { 0xe5821000 }, /* str r1, [r2] - set GICC_CTLR.Enable */
    { 0xe3a010ff }, /* mov r1, #0xff */
    { 0xe5821004 }, /* str r1, [r2, 4] - set GIC_PMR.Priority to 0xff */
    { 0, FIXUP_DSB },   /* dsb */
    { 0xe320f003 }, /* wfi */
    { 0xe5901000 }, /* ldr     r1, [r0] */
    { 0xe1110001 }, /* tst     r1, r1 */
    { 0x0afffffb }, /* beq     <wfi> */
    { 0xe12fff11 }, /* bx      r1 */
    { 0, FIXUP_GIC_CPU_IF }, /* gic_cpu_if: .word 0x.... */
    { 0, FIXUP_BOOTREG }, /* bootreg_addr: .word 0x.... */
    { 0, FIXUP_TERMINATOR }
};

static void write_bootloader(const char *name, hwaddr addr,
                             const ARMInsnFixup *insns, uint32_t *fixupcontext)
{
    /* Fix up the specified bootloader fragment and write it into
     * guest memory using rom_add_blob_fixed(). fixupcontext is
     * an array giving the values to write in for the fixup types
     * which write a value into the code array.
     */
    int i, len;
    uint32_t *code;

    len = 0;
    while (insns[len].fixup != FIXUP_TERMINATOR) {
        len++;
    }

    code = g_new0(uint32_t, len);

    for (i = 0; i < len; i++) {
        uint32_t insn = insns[i].insn;
        FixupType fixup = insns[i].fixup;

        switch (fixup) {
        case FIXUP_NONE:
            break;
        case FIXUP_BOARDID:
        case FIXUP_ARGPTR:
        case FIXUP_ENTRYPOINT:
        case FIXUP_GIC_CPU_IF:
        case FIXUP_BOOTREG:
        case FIXUP_DSB:
            insn = fixupcontext[fixup];
            break;
        default:
            abort();
        }
        code[i] = tswap32(insn);
    }

    rom_add_blob_fixed(name, code, len * sizeof(uint32_t), addr);

    g_free(code);
}

static void default_write_secondary(ARMCPU *cpu,
                                    const struct arm_boot_info *info)
{
    uint32_t fixupcontext[FIXUP_MAX];

    fixupcontext[FIXUP_GIC_CPU_IF] = info->gic_cpu_if_addr;
    fixupcontext[FIXUP_BOOTREG] = info->smp_bootreg_addr;
    if (arm_feature(&cpu->env, ARM_FEATURE_V7)) {
        fixupcontext[FIXUP_DSB] = DSB_INSN;
    } else {
        fixupcontext[FIXUP_DSB] = CP15_DSB_INSN;
    }

    write_bootloader("smpboot", info->smp_loader_start,
                     smpboot, fixupcontext);
}

static void default_reset_secondary(ARMCPU *cpu,
                                    const struct arm_boot_info *info)
{
    CPUARMState *env = &cpu->env;

    stl_phys_notdirty(&address_space_memory, info->smp_bootreg_addr, 0);
    env->regs[15] = info->smp_loader_start;
}

static inline bool have_dtb(const struct arm_boot_info *info)
{
    return info->dtb_filename || info->get_dtb;
}

#define WRITE_WORD(p, value) do { \
    stl_phys_notdirty(&address_space_memory, p, value);  \
    p += 4;                       \
} while (0)

static void set_kernel_args(const struct arm_boot_info *info)
{
    int initrd_size = info->initrd_size;
    hwaddr base = info->loader_start;
    hwaddr p;

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
        WRITE_WORD(p, info->initrd_start);
        WRITE_WORD(p, initrd_size);
    }
    if (info->kernel_cmdline && *info->kernel_cmdline) {
        /* ATAG_CMDLINE */
        int cmdline_size;

        cmdline_size = strlen(info->kernel_cmdline);
        cpu_physical_memory_write(p + 8, info->kernel_cmdline,
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

static void set_kernel_args_old(const struct arm_boot_info *info)
{
    hwaddr p;
    const char *s;
    int initrd_size = info->initrd_size;
    hwaddr base = info->loader_start;

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
    if (initrd_size) {
        WRITE_WORD(p, info->initrd_start);
    } else {
        WRITE_WORD(p, 0);
    }
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
        cpu_physical_memory_write(p, s, strlen(s) + 1);
    } else {
        WRITE_WORD(p, 0);
    }
}

static int load_dtb(hwaddr addr, const struct arm_boot_info *binfo)
{
    void *fdt = NULL;
    int size, rc;
    uint32_t acells, scells;

    if (binfo->dtb_filename) {
        char *filename;
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, binfo->dtb_filename);
        if (!filename) {
            fprintf(stderr, "Couldn't open dtb file %s\n", binfo->dtb_filename);
            goto fail;
        }

        fdt = load_device_tree(filename, &size);
        if (!fdt) {
            fprintf(stderr, "Couldn't open dtb file %s\n", filename);
            g_free(filename);
            goto fail;
        }
        g_free(filename);
    } else if (binfo->get_dtb) {
        fdt = binfo->get_dtb(binfo, &size);
        if (!fdt) {
            fprintf(stderr, "Board was unable to create a dtb blob\n");
            goto fail;
        }
    }

    acells = qemu_fdt_getprop_cell(fdt, "/", "#address-cells");
    scells = qemu_fdt_getprop_cell(fdt, "/", "#size-cells");
    if (acells == 0 || scells == 0) {
        fprintf(stderr, "dtb file invalid (#address-cells or #size-cells 0)\n");
        goto fail;
    }

    if (scells < 2 && binfo->ram_size >= (1ULL << 32)) {
        /* This is user error so deserves a friendlier error message
         * than the failure of setprop_sized_cells would provide
         */
        fprintf(stderr, "qemu: dtb file not compatible with "
                "RAM size > 4GB\n");
        goto fail;
    }

    rc = qemu_fdt_setprop_sized_cells(fdt, "/memory", "reg",
                                      acells, binfo->loader_start,
                                      scells, binfo->ram_size);
    if (rc < 0) {
        fprintf(stderr, "couldn't set /memory/reg\n");
        goto fail;
    }

    if (binfo->kernel_cmdline && *binfo->kernel_cmdline) {
        rc = qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
                                     binfo->kernel_cmdline);
        if (rc < 0) {
            fprintf(stderr, "couldn't set /chosen/bootargs\n");
            goto fail;
        }
    }

    if (binfo->initrd_size) {
        rc = qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-start",
                                   binfo->initrd_start);
        if (rc < 0) {
            fprintf(stderr, "couldn't set /chosen/linux,initrd-start\n");
            goto fail;
        }

        rc = qemu_fdt_setprop_cell(fdt, "/chosen", "linux,initrd-end",
                                   binfo->initrd_start + binfo->initrd_size);
        if (rc < 0) {
            fprintf(stderr, "couldn't set /chosen/linux,initrd-end\n");
            goto fail;
        }
    }

    if (binfo->modify_dtb) {
        binfo->modify_dtb(binfo, fdt);
    }

    qemu_fdt_dumpdtb(fdt, size);

    cpu_physical_memory_write(addr, fdt, size);

    g_free(fdt);

    return 0;

fail:
    g_free(fdt);
    return -1;
}

static void do_cpu_reset(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;
    const struct arm_boot_info *info = env->boot_info;

    cpu_reset(CPU(cpu));
    if (info) {
        if (!info->is_linux) {
            /* Jump to the entry point.  */
            if (env->aarch64) {
                env->pc = info->entry;
            } else {
                env->regs[15] = info->entry & 0xfffffffe;
                env->thumb = info->entry & 1;
            }
        } else {
            if (CPU(cpu) == first_cpu) {
                if (env->aarch64) {
                    env->pc = info->loader_start;
                } else {
                    env->regs[15] = info->loader_start;
                }

                if (!have_dtb(info)) {
                    if (old_param) {
                        set_kernel_args_old(info);
                    } else {
                        set_kernel_args(info);
                    }
                }
            } else {
                info->secondary_cpu_reset_hook(cpu, info);
            }
        }
    }
}

void arm_load_kernel(ARMCPU *cpu, struct arm_boot_info *info)
{
    CPUState *cs = CPU(cpu);
    int kernel_size;
    int initrd_size;
    int is_linux = 0;
    uint64_t elf_entry;
    int elf_machine;
    hwaddr entry, kernel_load_offset;
    int big_endian;
    static const ARMInsnFixup *primary_loader;

    /* Load the kernel.  */
    if (!info->kernel_filename) {
        /* If no kernel specified, do nothing; we will start from address 0
         * (typically a boot ROM image) in the same way as hardware.
         */
        return;
    }

    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
        primary_loader = bootloader_aarch64;
        kernel_load_offset = KERNEL64_LOAD_ADDR;
        elf_machine = EM_AARCH64;
    } else {
        primary_loader = bootloader;
        kernel_load_offset = KERNEL_LOAD_ADDR;
        elf_machine = EM_ARM;
    }

    info->dtb_filename = qemu_opt_get(qemu_get_machine_opts(), "dtb");

    if (!info->secondary_cpu_reset_hook) {
        info->secondary_cpu_reset_hook = default_reset_secondary;
    }
    if (!info->write_secondary_boot) {
        info->write_secondary_boot = default_write_secondary;
    }

    if (info->nb_cpus == 0)
        info->nb_cpus = 1;

#ifdef TARGET_WORDS_BIGENDIAN
    big_endian = 1;
#else
    big_endian = 0;
#endif

    /* We want to put the initrd far enough into RAM that when the
     * kernel is uncompressed it will not clobber the initrd. However
     * on boards without much RAM we must ensure that we still leave
     * enough room for a decent sized initrd, and on boards with large
     * amounts of RAM we must avoid the initrd being so far up in RAM
     * that it is outside lowmem and inaccessible to the kernel.
     * So for boards with less  than 256MB of RAM we put the initrd
     * halfway into RAM, and for boards with 256MB of RAM or more we put
     * the initrd at 128MB.
     */
    info->initrd_start = info->loader_start +
        MIN(info->ram_size / 2, 128 * 1024 * 1024);

    /* Assume that raw images are linux kernels, and ELF images are not.  */
    kernel_size = load_elf(info->kernel_filename, NULL, NULL, &elf_entry,
                           NULL, NULL, big_endian, elf_machine, 1);
    entry = elf_entry;
    if (kernel_size < 0) {
        kernel_size = load_uimage(info->kernel_filename, &entry, NULL,
                                  &is_linux);
    }
    /* On aarch64, it's the bootloader's job to uncompress the kernel. */
    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64) && kernel_size < 0) {
        entry = info->loader_start + kernel_load_offset;
        kernel_size = load_image_gzipped(info->kernel_filename, entry,
                                         info->ram_size - kernel_load_offset);
        is_linux = 1;
    }
    if (kernel_size < 0) {
        entry = info->loader_start + kernel_load_offset;
        kernel_size = load_image_targphys(info->kernel_filename, entry,
                                          info->ram_size - kernel_load_offset);
        is_linux = 1;
    }
    if (kernel_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n",
                info->kernel_filename);
        exit(1);
    }
    info->entry = entry;
    if (is_linux) {
        uint32_t fixupcontext[FIXUP_MAX];

        if (info->initrd_filename) {
            initrd_size = load_ramdisk(info->initrd_filename,
                                       info->initrd_start,
                                       info->ram_size -
                                       info->initrd_start);
            if (initrd_size < 0) {
                initrd_size = load_image_targphys(info->initrd_filename,
                                                  info->initrd_start,
                                                  info->ram_size -
                                                  info->initrd_start);
            }
            if (initrd_size < 0) {
                fprintf(stderr, "qemu: could not load initrd '%s'\n",
                        info->initrd_filename);
                exit(1);
            }
        } else {
            initrd_size = 0;
        }
        info->initrd_size = initrd_size;

        fixupcontext[FIXUP_BOARDID] = info->board_id;

        /* for device tree boot, we pass the DTB directly in r2. Otherwise
         * we point to the kernel args.
         */
        if (have_dtb(info)) {
            /* Place the DTB after the initrd in memory. Note that some
             * kernels will trash anything in the 4K page the initrd
             * ends in, so make sure the DTB isn't caught up in that.
             */
            hwaddr dtb_start = QEMU_ALIGN_UP(info->initrd_start + initrd_size,
                                             4096);
            if (load_dtb(dtb_start, info)) {
                exit(1);
            }
            fixupcontext[FIXUP_ARGPTR] = dtb_start;
        } else {
            fixupcontext[FIXUP_ARGPTR] = info->loader_start + KERNEL_ARGS_ADDR;
            if (info->ram_size >= (1ULL << 32)) {
                fprintf(stderr, "qemu: RAM size must be less than 4GB to boot"
                        " Linux kernel using ATAGS (try passing a device tree"
                        " using -dtb)\n");
                exit(1);
            }
        }
        fixupcontext[FIXUP_ENTRYPOINT] = entry;

        write_bootloader("bootloader", info->loader_start,
                         primary_loader, fixupcontext);

        if (info->nb_cpus > 1) {
            info->write_secondary_boot(cpu, info);
        }
    }
    info->is_linux = is_linux;

    for (; cs; cs = CPU_NEXT(cs)) {
        cpu = ARM_CPU(cs);
        cpu->env.boot_info = info;
        qemu_register_reset(do_cpu_reset, cpu);
    }
}
