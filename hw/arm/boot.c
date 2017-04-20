/*
 * ARM kernel loader.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include <libfdt.h>
#include "hw/hw.h"
#include "hw/arm/arm.h"
#include "hw/arm/linux-boot-if.h"
#include "sysemu/kvm.h"
#include "sysemu/sysemu.h"
#include "sysemu/numa.h"
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

#define ARM64_TEXT_OFFSET_OFFSET    8
#define ARM64_MAGIC_OFFSET          56

typedef enum {
    FIXUP_NONE = 0,     /* do nothing */
    FIXUP_TERMINATOR,   /* end of insns */
    FIXUP_BOARDID,      /* overwrite with board ID number */
    FIXUP_BOARD_SETUP,  /* overwrite with board specific setup code address */
    FIXUP_ARGPTR,       /* overwrite with pointer to kernel args */
    FIXUP_ENTRYPOINT,   /* overwrite with kernel entry point */
    FIXUP_GIC_CPU_IF,   /* overwrite with GIC CPU interface address */
    FIXUP_BOOTREG,      /* overwrite with boot register address */
    FIXUP_DSB,          /* overwrite with correct DSB insn for cpu */
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

/* A very small bootloader: call the board-setup code (if needed),
 * set r0-r2, then jump to the kernel.
 * If we're not calling boot setup code then we don't copy across
 * the first BOOTLOADER_NO_BOARD_SETUP_OFFSET insns in this array.
 */

static const ARMInsnFixup bootloader[] = {
    { 0xe28fe004 }, /* add     lr, pc, #4 */
    { 0xe51ff004 }, /* ldr     pc, [pc, #-4] */
    { 0, FIXUP_BOARD_SETUP },
#define BOOTLOADER_NO_BOARD_SETUP_OFFSET 3
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
        case FIXUP_BOARD_SETUP:
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

void arm_write_secure_board_setup_dummy_smc(ARMCPU *cpu,
                                            const struct arm_boot_info *info,
                                            hwaddr mvbar_addr)
{
    int n;
    uint32_t mvbar_blob[] = {
        /* mvbar_addr: secure monitor vectors
         * Default unimplemented and unused vectors to spin. Makes it
         * easier to debug (as opposed to the CPU running away).
         */
        0xeafffffe, /* (spin) */
        0xeafffffe, /* (spin) */
        0xe1b0f00e, /* movs pc, lr ;SMC exception return */
        0xeafffffe, /* (spin) */
        0xeafffffe, /* (spin) */
        0xeafffffe, /* (spin) */
        0xeafffffe, /* (spin) */
        0xeafffffe, /* (spin) */
    };
    uint32_t board_setup_blob[] = {
        /* board setup addr */
        0xe3a00e00 + (mvbar_addr >> 4), /* mov r0, #mvbar_addr */
        0xee0c0f30, /* mcr     p15, 0, r0, c12, c0, 1 ;set MVBAR */
        0xee110f11, /* mrc     p15, 0, r0, c1 , c1, 0 ;read SCR */
        0xe3800031, /* orr     r0, #0x31              ;enable AW, FW, NS */
        0xee010f11, /* mcr     p15, 0, r0, c1, c1, 0  ;write SCR */
        0xe1a0100e, /* mov     r1, lr                 ;save LR across SMC */
        0xe1600070, /* smc     #0                     ;call monitor to flush SCR */
        0xe1a0f001, /* mov     pc, r1                 ;return */
    };

    /* check that mvbar_addr is correctly aligned and relocatable (using MOV) */
    assert((mvbar_addr & 0x1f) == 0 && (mvbar_addr >> 4) < 0x100);

    /* check that these blobs don't overlap */
    assert((mvbar_addr + sizeof(mvbar_blob) <= info->board_setup_addr)
          || (info->board_setup_addr + sizeof(board_setup_blob) <= mvbar_addr));

    for (n = 0; n < ARRAY_SIZE(mvbar_blob); n++) {
        mvbar_blob[n] = tswap32(mvbar_blob[n]);
    }
    rom_add_blob_fixed("board-setup-mvbar", mvbar_blob, sizeof(mvbar_blob),
                       mvbar_addr);

    for (n = 0; n < ARRAY_SIZE(board_setup_blob); n++) {
        board_setup_blob[n] = tswap32(board_setup_blob[n]);
    }
    rom_add_blob_fixed("board-setup", board_setup_blob,
                       sizeof(board_setup_blob), info->board_setup_addr);
}

static void default_reset_secondary(ARMCPU *cpu,
                                    const struct arm_boot_info *info)
{
    CPUState *cs = CPU(cpu);

    address_space_stl_notdirty(&address_space_memory, info->smp_bootreg_addr,
                               0, MEMTXATTRS_UNSPECIFIED, NULL);
    cpu_set_pc(cs, info->smp_loader_start);
}

static inline bool have_dtb(const struct arm_boot_info *info)
{
    return info->dtb_filename || info->get_dtb;
}

#define WRITE_WORD(p, value) do { \
    address_space_stl_notdirty(&address_space_memory, p, value, \
                               MEMTXATTRS_UNSPECIFIED, NULL);  \
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

/**
 * load_dtb() - load a device tree binary image into memory
 * @addr:       the address to load the image at
 * @binfo:      struct describing the boot environment
 * @addr_limit: upper limit of the available memory area at @addr
 *
 * Load a device tree supplied by the machine or by the user  with the
 * '-dtb' command line option, and put it at offset @addr in target
 * memory.
 *
 * If @addr_limit contains a meaningful value (i.e., it is strictly greater
 * than @addr), the device tree is only loaded if its size does not exceed
 * the limit.
 *
 * Returns: the size of the device tree image on success,
 *          0 if the image size exceeds the limit,
 *          -1 on errors.
 *
 * Note: Must not be called unless have_dtb(binfo) is true.
 */
static int load_dtb(hwaddr addr, const struct arm_boot_info *binfo,
                    hwaddr addr_limit)
{
    void *fdt = NULL;
    int size, rc;
    uint32_t acells, scells;
    char *nodename;
    unsigned int i;
    hwaddr mem_base, mem_len;

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
    } else {
        fdt = binfo->get_dtb(binfo, &size);
        if (!fdt) {
            fprintf(stderr, "Board was unable to create a dtb blob\n");
            goto fail;
        }
    }

    if (addr_limit > addr && size > (addr_limit - addr)) {
        /* Installing the device tree blob at addr would exceed addr_limit.
         * Whether this constitutes failure is up to the caller to decide,
         * so just return 0 as size, i.e., no error.
         */
        g_free(fdt);
        return 0;
    }

    acells = qemu_fdt_getprop_cell(fdt, "/", "#address-cells",
                                   NULL, &error_fatal);
    scells = qemu_fdt_getprop_cell(fdt, "/", "#size-cells",
                                   NULL, &error_fatal);
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

    if (nb_numa_nodes > 0) {
        /*
         * Turn the /memory node created before into a NOP node, then create
         * /memory@addr nodes for all numa nodes respectively.
         */
        qemu_fdt_nop_node(fdt, "/memory");
        mem_base = binfo->loader_start;
        for (i = 0; i < nb_numa_nodes; i++) {
            mem_len = numa_info[i].node_mem;
            nodename = g_strdup_printf("/memory@%" PRIx64, mem_base);
            qemu_fdt_add_subnode(fdt, nodename);
            qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
            rc = qemu_fdt_setprop_sized_cells(fdt, nodename, "reg",
                                              acells, mem_base,
                                              scells, mem_len);
            if (rc < 0) {
                fprintf(stderr, "couldn't set %s/reg for node %d\n", nodename,
                        i);
                goto fail;
            }

            qemu_fdt_setprop_cell(fdt, nodename, "numa-node-id", i);
            mem_base += mem_len;
            g_free(nodename);
        }
    } else {
        Error *err = NULL;

        rc = fdt_path_offset(fdt, "/memory");
        if (rc < 0) {
            qemu_fdt_add_subnode(fdt, "/memory");
        }

        if (!qemu_fdt_getprop(fdt, "/memory", "device_type", NULL, &err)) {
            qemu_fdt_setprop_string(fdt, "/memory", "device_type", "memory");
        }

        rc = qemu_fdt_setprop_sized_cells(fdt, "/memory", "reg",
                                          acells, binfo->loader_start,
                                          scells, binfo->ram_size);
        if (rc < 0) {
            fprintf(stderr, "couldn't set /memory/reg\n");
            goto fail;
        }
    }

    rc = fdt_path_offset(fdt, "/chosen");
    if (rc < 0) {
        qemu_fdt_add_subnode(fdt, "/chosen");
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

    /* Put the DTB into the memory map as a ROM image: this will ensure
     * the DTB is copied again upon reset, even if addr points into RAM.
     */
    rom_add_blob_fixed("dtb", fdt, size, addr);

    g_free(fdt);

    return size;

fail:
    g_free(fdt);
    return -1;
}

static void do_cpu_reset(void *opaque)
{
    ARMCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;
    const struct arm_boot_info *info = env->boot_info;

    cpu_reset(cs);
    if (info) {
        if (!info->is_linux) {
            int i;
            /* Jump to the entry point.  */
            uint64_t entry = info->entry;

            switch (info->endianness) {
            case ARM_ENDIANNESS_LE:
                env->cp15.sctlr_el[1] &= ~SCTLR_E0E;
                for (i = 1; i < 4; ++i) {
                    env->cp15.sctlr_el[i] &= ~SCTLR_EE;
                }
                env->uncached_cpsr &= ~CPSR_E;
                break;
            case ARM_ENDIANNESS_BE8:
                env->cp15.sctlr_el[1] |= SCTLR_E0E;
                for (i = 1; i < 4; ++i) {
                    env->cp15.sctlr_el[i] |= SCTLR_EE;
                }
                env->uncached_cpsr |= CPSR_E;
                break;
            case ARM_ENDIANNESS_BE32:
                env->cp15.sctlr_el[1] |= SCTLR_B;
                break;
            case ARM_ENDIANNESS_UNKNOWN:
                break; /* Board's decision */
            default:
                g_assert_not_reached();
            }

            if (!env->aarch64) {
                env->thumb = info->entry & 1;
                entry &= 0xfffffffe;
            }
            cpu_set_pc(cs, entry);
        } else {
            /* If we are booting Linux then we need to check whether we are
             * booting into secure or non-secure state and adjust the state
             * accordingly.  Out of reset, ARM is defined to be in secure state
             * (SCR.NS = 0), we change that here if non-secure boot has been
             * requested.
             */
            if (arm_feature(env, ARM_FEATURE_EL3)) {
                /* AArch64 is defined to come out of reset into EL3 if enabled.
                 * If we are booting Linux then we need to adjust our EL as
                 * Linux expects us to be in EL2 or EL1.  AArch32 resets into
                 * SVC, which Linux expects, so no privilege/exception level to
                 * adjust.
                 */
                if (env->aarch64) {
                    env->cp15.scr_el3 |= SCR_RW;
                    if (arm_feature(env, ARM_FEATURE_EL2)) {
                        env->cp15.hcr_el2 |= HCR_RW;
                        env->pstate = PSTATE_MODE_EL2h;
                    } else {
                        env->pstate = PSTATE_MODE_EL1h;
                    }
                }

                /* Set to non-secure if not a secure boot */
                if (!info->secure_boot &&
                    (cs != first_cpu || !info->secure_board_setup)) {
                    /* Linux expects non-secure state */
                    env->cp15.scr_el3 |= SCR_NS;
                }
            }

            if (cs == first_cpu) {
                cpu_set_pc(cs, info->loader_start);

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

/**
 * load_image_to_fw_cfg() - Load an image file into an fw_cfg entry identified
 *                          by key.
 * @fw_cfg:         The firmware config instance to store the data in.
 * @size_key:       The firmware config key to store the size of the loaded
 *                  data under, with fw_cfg_add_i32().
 * @data_key:       The firmware config key to store the loaded data under,
 *                  with fw_cfg_add_bytes().
 * @image_name:     The name of the image file to load. If it is NULL, the
 *                  function returns without doing anything.
 * @try_decompress: Whether the image should be decompressed (gunzipped) before
 *                  adding it to fw_cfg. If decompression fails, the image is
 *                  loaded as-is.
 *
 * In case of failure, the function prints an error message to stderr and the
 * process exits with status 1.
 */
static void load_image_to_fw_cfg(FWCfgState *fw_cfg, uint16_t size_key,
                                 uint16_t data_key, const char *image_name,
                                 bool try_decompress)
{
    size_t size = -1;
    uint8_t *data;

    if (image_name == NULL) {
        return;
    }

    if (try_decompress) {
        size = load_image_gzipped_buffer(image_name,
                                         LOAD_IMAGE_MAX_GUNZIP_BYTES, &data);
    }

    if (size == (size_t)-1) {
        gchar *contents;
        gsize length;

        if (!g_file_get_contents(image_name, &contents, &length, NULL)) {
            fprintf(stderr, "failed to load \"%s\"\n", image_name);
            exit(1);
        }
        size = length;
        data = (uint8_t *)contents;
    }

    fw_cfg_add_i32(fw_cfg, size_key, size);
    fw_cfg_add_bytes(fw_cfg, data_key, data, size);
}

static int do_arm_linux_init(Object *obj, void *opaque)
{
    if (object_dynamic_cast(obj, TYPE_ARM_LINUX_BOOT_IF)) {
        ARMLinuxBootIf *albif = ARM_LINUX_BOOT_IF(obj);
        ARMLinuxBootIfClass *albifc = ARM_LINUX_BOOT_IF_GET_CLASS(obj);
        struct arm_boot_info *info = opaque;

        if (albifc->arm_linux_init) {
            albifc->arm_linux_init(albif, info->secure_boot);
        }
    }
    return 0;
}

static uint64_t arm_load_elf(struct arm_boot_info *info, uint64_t *pentry,
                             uint64_t *lowaddr, uint64_t *highaddr,
                             int elf_machine)
{
    bool elf_is64;
    union {
        Elf32_Ehdr h32;
        Elf64_Ehdr h64;
    } elf_header;
    int data_swab = 0;
    bool big_endian;
    uint64_t ret = -1;
    Error *err = NULL;


    load_elf_hdr(info->kernel_filename, &elf_header, &elf_is64, &err);
    if (err) {
        return ret;
    }

    if (elf_is64) {
        big_endian = elf_header.h64.e_ident[EI_DATA] == ELFDATA2MSB;
        info->endianness = big_endian ? ARM_ENDIANNESS_BE8
                                      : ARM_ENDIANNESS_LE;
    } else {
        big_endian = elf_header.h32.e_ident[EI_DATA] == ELFDATA2MSB;
        if (big_endian) {
            if (bswap32(elf_header.h32.e_flags) & EF_ARM_BE8) {
                info->endianness = ARM_ENDIANNESS_BE8;
            } else {
                info->endianness = ARM_ENDIANNESS_BE32;
                /* In BE32, the CPU has a different view of the per-byte
                 * address map than the rest of the system. BE32 ELF files
                 * are organised such that they can be programmed through
                 * the CPU's per-word byte-reversed view of the world. QEMU
                 * however loads ELF files independently of the CPU. So
                 * tell the ELF loader to byte reverse the data for us.
                 */
                data_swab = 2;
            }
        } else {
            info->endianness = ARM_ENDIANNESS_LE;
        }
    }

    ret = load_elf(info->kernel_filename, NULL, NULL,
                   pentry, lowaddr, highaddr, big_endian, elf_machine,
                   1, data_swab);
    if (ret <= 0) {
        /* The header loaded but the image didn't */
        exit(1);
    }

    return ret;
}

static uint64_t load_aarch64_image(const char *filename, hwaddr mem_base,
                                   hwaddr *entry)
{
    hwaddr kernel_load_offset = KERNEL64_LOAD_ADDR;
    uint8_t *buffer;
    int size;

    /* On aarch64, it's the bootloader's job to uncompress the kernel. */
    size = load_image_gzipped_buffer(filename, LOAD_IMAGE_MAX_GUNZIP_BYTES,
                                     &buffer);

    if (size < 0) {
        gsize len;

        /* Load as raw file otherwise */
        if (!g_file_get_contents(filename, (char **)&buffer, &len, NULL)) {
            return -1;
        }
        size = len;
    }

    /* check the arm64 magic header value -- very old kernels may not have it */
    if (memcmp(buffer + ARM64_MAGIC_OFFSET, "ARM\x64", 4) == 0) {
        uint64_t hdrvals[2];

        /* The arm64 Image header has text_offset and image_size fields at 8 and
         * 16 bytes into the Image header, respectively. The text_offset field
         * is only valid if the image_size is non-zero.
         */
        memcpy(&hdrvals, buffer + ARM64_TEXT_OFFSET_OFFSET, sizeof(hdrvals));
        if (hdrvals[1] != 0) {
            kernel_load_offset = le64_to_cpu(hdrvals[0]);
        }
    }

    *entry = mem_base + kernel_load_offset;
    rom_add_blob_fixed(filename, buffer, size, *entry);

    g_free(buffer);

    return size;
}

static void arm_load_kernel_notify(Notifier *notifier, void *data)
{
    CPUState *cs;
    int kernel_size;
    int initrd_size;
    int is_linux = 0;
    uint64_t elf_entry, elf_low_addr, elf_high_addr;
    int elf_machine;
    hwaddr entry;
    static const ARMInsnFixup *primary_loader;
    ArmLoadKernelNotifier *n = DO_UPCAST(ArmLoadKernelNotifier,
                                         notifier, notifier);
    ARMCPU *cpu = n->cpu;
    struct arm_boot_info *info =
        container_of(n, struct arm_boot_info, load_kernel_notifier);

    /* The board code is not supposed to set secure_board_setup unless
     * running its code in secure mode is actually possible, and KVM
     * doesn't support secure.
     */
    assert(!(info->secure_board_setup && kvm_enabled()));

    info->dtb_filename = qemu_opt_get(qemu_get_machine_opts(), "dtb");

    /* Load the kernel.  */
    if (!info->kernel_filename || info->firmware_loaded) {

        if (have_dtb(info)) {
            /* If we have a device tree blob, but no kernel to supply it to (or
             * the kernel is supposed to be loaded by the bootloader), copy the
             * DTB to the base of RAM for the bootloader to pick up.
             */
            if (load_dtb(info->loader_start, info, 0) < 0) {
                exit(1);
            }
        }

        if (info->kernel_filename) {
            FWCfgState *fw_cfg;
            bool try_decompressing_kernel;

            fw_cfg = fw_cfg_find();
            try_decompressing_kernel = arm_feature(&cpu->env,
                                                   ARM_FEATURE_AARCH64);

            /* Expose the kernel, the command line, and the initrd in fw_cfg.
             * We don't process them here at all, it's all left to the
             * firmware.
             */
            load_image_to_fw_cfg(fw_cfg,
                                 FW_CFG_KERNEL_SIZE, FW_CFG_KERNEL_DATA,
                                 info->kernel_filename,
                                 try_decompressing_kernel);
            load_image_to_fw_cfg(fw_cfg,
                                 FW_CFG_INITRD_SIZE, FW_CFG_INITRD_DATA,
                                 info->initrd_filename, false);

            if (info->kernel_cmdline) {
                fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE,
                               strlen(info->kernel_cmdline) + 1);
                fw_cfg_add_string(fw_cfg, FW_CFG_CMDLINE_DATA,
                                  info->kernel_cmdline);
            }
        }

        /* We will start from address 0 (typically a boot ROM image) in the
         * same way as hardware.
         */
        return;
    }

    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
        primary_loader = bootloader_aarch64;
        elf_machine = EM_AARCH64;
    } else {
        primary_loader = bootloader;
        if (!info->write_board_setup) {
            primary_loader += BOOTLOADER_NO_BOARD_SETUP_OFFSET;
        }
        elf_machine = EM_ARM;
    }

    if (!info->secondary_cpu_reset_hook) {
        info->secondary_cpu_reset_hook = default_reset_secondary;
    }
    if (!info->write_secondary_boot) {
        info->write_secondary_boot = default_write_secondary;
    }

    if (info->nb_cpus == 0)
        info->nb_cpus = 1;

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
    kernel_size = arm_load_elf(info, &elf_entry, &elf_low_addr,
                               &elf_high_addr, elf_machine);
    if (kernel_size > 0 && have_dtb(info)) {
        /* If there is still some room left at the base of RAM, try and put
         * the DTB there like we do for images loaded with -bios or -pflash.
         */
        if (elf_low_addr > info->loader_start
            || elf_high_addr < info->loader_start) {
            /* Pass elf_low_addr as address limit to load_dtb if it may be
             * pointing into RAM, otherwise pass '0' (no limit)
             */
            if (elf_low_addr < info->loader_start) {
                elf_low_addr = 0;
            }
            if (load_dtb(info->loader_start, info, elf_low_addr) < 0) {
                exit(1);
            }
        }
    }
    entry = elf_entry;
    if (kernel_size < 0) {
        kernel_size = load_uimage(info->kernel_filename, &entry, NULL,
                                  &is_linux, NULL, NULL);
    }
    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64) && kernel_size < 0) {
        kernel_size = load_aarch64_image(info->kernel_filename,
                                         info->loader_start, &entry);
        is_linux = 1;
    } else if (kernel_size < 0) {
        /* 32-bit ARM */
        entry = info->loader_start + KERNEL_LOAD_ADDR;
        kernel_size = load_image_targphys(info->kernel_filename, entry,
                                          info->ram_size - KERNEL_LOAD_ADDR);
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
        fixupcontext[FIXUP_BOARD_SETUP] = info->board_setup_addr;

        /* for device tree boot, we pass the DTB directly in r2. Otherwise
         * we point to the kernel args.
         */
        if (have_dtb(info)) {
            hwaddr align;
            hwaddr dtb_start;

            if (elf_machine == EM_AARCH64) {
                /*
                 * Some AArch64 kernels on early bootup map the fdt region as
                 *
                 *   [ ALIGN_DOWN(fdt, 2MB) ... ALIGN_DOWN(fdt, 2MB) + 2MB ]
                 *
                 * Let's play safe and prealign it to 2MB to give us some space.
                 */
                align = 2 * 1024 * 1024;
            } else {
                /*
                 * Some 32bit kernels will trash anything in the 4K page the
                 * initrd ends in, so make sure the DTB isn't caught up in that.
                 */
                align = 4096;
            }

            /* Place the DTB after the initrd in memory with alignment. */
            dtb_start = QEMU_ALIGN_UP(info->initrd_start + initrd_size, align);
            if (load_dtb(dtb_start, info, 0) < 0) {
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
        if (info->write_board_setup) {
            info->write_board_setup(cpu, info);
        }

        /* Notify devices which need to fake up firmware initialization
         * that we're doing a direct kernel boot.
         */
        object_child_foreach_recursive(object_get_root(),
                                       do_arm_linux_init, info);
    }
    info->is_linux = is_linux;

    for (cs = CPU(cpu); cs; cs = CPU_NEXT(cs)) {
        ARM_CPU(cs)->env.boot_info = info;
    }
}

void arm_load_kernel(ARMCPU *cpu, struct arm_boot_info *info)
{
    CPUState *cs;

    info->load_kernel_notifier.cpu = cpu;
    info->load_kernel_notifier.notifier.notify = arm_load_kernel_notify;
    qemu_add_machine_init_done_notifier(&info->load_kernel_notifier.notifier);

    /* CPU objects (unlike devices) are not automatically reset on system
     * reset, so we must always register a handler to do so. If we're
     * actually loading a kernel, the handler is also responsible for
     * arranging that we start it correctly.
     */
    for (cs = CPU(cpu); cs; cs = CPU_NEXT(cs)) {
        qemu_register_reset(do_cpu_reset, ARM_CPU(cs));
    }
}

static const TypeInfo arm_linux_boot_if_info = {
    .name = TYPE_ARM_LINUX_BOOT_IF,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(ARMLinuxBootIfClass),
};

static void arm_linux_boot_register_types(void)
{
    type_register_static(&arm_linux_boot_if_info);
}

type_init(arm_linux_boot_register_types)
