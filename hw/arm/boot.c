/*
 * ARM kernel loader.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include <libfdt.h>
#include "hw/arm/boot.h"
#include "hw/arm/linux-boot-if.h"
#include "sysemu/kvm.h"
#include "sysemu/tcg.h"
#include "sysemu/sysemu.h"
#include "sysemu/numa.h"
#include "hw/boards.h"
#include "sysemu/reset.h"
#include "hw/loader.h"
#include "elf.h"
#include "sysemu/device_tree.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/units.h"

/* Kernel boot protocol is specified in the kernel docs
 * Documentation/arm/Booting and Documentation/arm64/booting.txt
 * They have different preferred image load offsets from system RAM base.
 */
#define KERNEL_ARGS_ADDR   0x100
#define KERNEL_NOLOAD_ADDR 0x02000000
#define KERNEL_LOAD_ADDR   0x00010000
#define KERNEL64_LOAD_ADDR 0x00080000

#define ARM64_TEXT_OFFSET_OFFSET    8
#define ARM64_MAGIC_OFFSET          56

#define BOOTLOADER_MAX_SIZE         (4 * KiB)

AddressSpace *arm_boot_address_space(ARMCPU *cpu,
                                     const struct arm_boot_info *info)
{
    /* Return the address space to use for bootloader reads and writes.
     * We prefer the secure address space if the CPU has it and we're
     * going to boot the guest into it.
     */
    int asidx;
    CPUState *cs = CPU(cpu);

    if (arm_feature(&cpu->env, ARM_FEATURE_EL3) && info->secure_boot) {
        asidx = ARMASIdx_S;
    } else {
        asidx = ARMASIdx_NS;
    }

    return cpu_get_address_space(cs, asidx);
}

static const ARMInsnFixup bootloader_aarch64[] = {
    { 0x580000c0 }, /* ldr x0, arg ; Load the lower 32-bits of DTB */
    { 0xaa1f03e1 }, /* mov x1, xzr */
    { 0xaa1f03e2 }, /* mov x2, xzr */
    { 0xaa1f03e3 }, /* mov x3, xzr */
    { 0x58000084 }, /* ldr x4, entry ; Load the lower 32-bits of kernel entry */
    { 0xd61f0080 }, /* br x4      ; Jump to the kernel entry point */
    { 0, FIXUP_ARGPTR_LO }, /* arg: .word @DTB Lower 32-bits */
    { 0, FIXUP_ARGPTR_HI}, /* .word @DTB Higher 32-bits */
    { 0, FIXUP_ENTRYPOINT_LO }, /* entry: .word @Kernel Entry Lower 32-bits */
    { 0, FIXUP_ENTRYPOINT_HI }, /* .word @Kernel Entry Higher 32-bits */
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
    { 0, FIXUP_ARGPTR_LO },
    { 0, FIXUP_ENTRYPOINT_LO },
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

void arm_write_bootloader(const char *name,
                          AddressSpace *as, hwaddr addr,
                          const ARMInsnFixup *insns,
                          const uint32_t *fixupcontext)
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
        case FIXUP_ARGPTR_LO:
        case FIXUP_ARGPTR_HI:
        case FIXUP_ENTRYPOINT_LO:
        case FIXUP_ENTRYPOINT_HI:
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

    assert((len * sizeof(uint32_t)) < BOOTLOADER_MAX_SIZE);

    rom_add_blob_fixed_as(name, code, len * sizeof(uint32_t), addr, as);

    g_free(code);
}

static void default_write_secondary(ARMCPU *cpu,
                                    const struct arm_boot_info *info)
{
    uint32_t fixupcontext[FIXUP_MAX];
    AddressSpace *as = arm_boot_address_space(cpu, info);

    fixupcontext[FIXUP_GIC_CPU_IF] = info->gic_cpu_if_addr;
    fixupcontext[FIXUP_BOOTREG] = info->smp_bootreg_addr;
    if (arm_feature(&cpu->env, ARM_FEATURE_V7)) {
        fixupcontext[FIXUP_DSB] = DSB_INSN;
    } else {
        fixupcontext[FIXUP_DSB] = CP15_DSB_INSN;
    }

    arm_write_bootloader("smpboot", as, info->smp_loader_start,
                         smpboot, fixupcontext);
}

void arm_write_secure_board_setup_dummy_smc(ARMCPU *cpu,
                                            const struct arm_boot_info *info,
                                            hwaddr mvbar_addr)
{
    AddressSpace *as = arm_boot_address_space(cpu, info);
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
        0xee110f51, /* mrc     p15, 0, r0, c1, c1, 2  ;read NSACR */
        0xe3800b03, /* orr     r0, #0xc00             ;set CP11, CP10 */
        0xee010f51, /* mcr     p15, 0, r0, c1, c1, 2  ;write NSACR */
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
    rom_add_blob_fixed_as("board-setup-mvbar", mvbar_blob, sizeof(mvbar_blob),
                          mvbar_addr, as);

    for (n = 0; n < ARRAY_SIZE(board_setup_blob); n++) {
        board_setup_blob[n] = tswap32(board_setup_blob[n]);
    }
    rom_add_blob_fixed_as("board-setup", board_setup_blob,
                          sizeof(board_setup_blob), info->board_setup_addr, as);
}

static void default_reset_secondary(ARMCPU *cpu,
                                    const struct arm_boot_info *info)
{
    AddressSpace *as = arm_boot_address_space(cpu, info);
    CPUState *cs = CPU(cpu);

    address_space_stl_notdirty(as, info->smp_bootreg_addr,
                               0, MEMTXATTRS_UNSPECIFIED, NULL);
    cpu_set_pc(cs, info->smp_loader_start);
}

static inline bool have_dtb(const struct arm_boot_info *info)
{
    return info->dtb_filename || info->get_dtb;
}

#define WRITE_WORD(p, value) do { \
    address_space_stl_notdirty(as, p, value, \
                               MEMTXATTRS_UNSPECIFIED, NULL);  \
    p += 4;                       \
} while (0)

static void set_kernel_args(const struct arm_boot_info *info, AddressSpace *as)
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
        address_space_write(as, p + 8, MEMTXATTRS_UNSPECIFIED,
                            info->kernel_cmdline, cmdline_size + 1);
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
        address_space_write(as, p, MEMTXATTRS_UNSPECIFIED,
                            atag_board_buf, atag_board_len);
        p += atag_board_len;
    }
    /* ATAG_END */
    WRITE_WORD(p, 0);
    WRITE_WORD(p, 0);
}

static void set_kernel_args_old(const struct arm_boot_info *info,
                                AddressSpace *as)
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
        address_space_write(as, p, MEMTXATTRS_UNSPECIFIED, s, strlen(s) + 1);
    } else {
        WRITE_WORD(p, 0);
    }
}

static int fdt_add_memory_node(void *fdt, uint32_t acells, hwaddr mem_base,
                               uint32_t scells, hwaddr mem_len,
                               int numa_node_id)
{
    char *nodename;
    int ret;

    nodename = g_strdup_printf("/memory@%" PRIx64, mem_base);
    qemu_fdt_add_subnode(fdt, nodename);
    qemu_fdt_setprop_string(fdt, nodename, "device_type", "memory");
    ret = qemu_fdt_setprop_sized_cells(fdt, nodename, "reg", acells, mem_base,
                                       scells, mem_len);
    if (ret < 0) {
        goto out;
    }

    /* only set the NUMA ID if it is specified */
    if (numa_node_id >= 0) {
        ret = qemu_fdt_setprop_cell(fdt, nodename,
                                    "numa-node-id", numa_node_id);
    }
out:
    g_free(nodename);
    return ret;
}

static void fdt_add_psci_node(void *fdt)
{
    uint32_t cpu_suspend_fn;
    uint32_t cpu_off_fn;
    uint32_t cpu_on_fn;
    uint32_t migrate_fn;
    ARMCPU *armcpu = ARM_CPU(qemu_get_cpu(0));
    const char *psci_method;
    int64_t psci_conduit;
    int rc;

    psci_conduit = object_property_get_int(OBJECT(armcpu),
                                           "psci-conduit",
                                           &error_abort);
    switch (psci_conduit) {
    case QEMU_PSCI_CONDUIT_DISABLED:
        return;
    case QEMU_PSCI_CONDUIT_HVC:
        psci_method = "hvc";
        break;
    case QEMU_PSCI_CONDUIT_SMC:
        psci_method = "smc";
        break;
    default:
        g_assert_not_reached();
    }

    /*
     * A pre-existing /psci node might specify function ID values
     * that don't match QEMU's PSCI implementation. Delete the whole
     * node and put our own in instead.
     */
    rc = fdt_path_offset(fdt, "/psci");
    if (rc >= 0) {
        qemu_fdt_nop_node(fdt, "/psci");
    }

    qemu_fdt_add_subnode(fdt, "/psci");
    if (armcpu->psci_version >= QEMU_PSCI_VERSION_0_2) {
        if (armcpu->psci_version < QEMU_PSCI_VERSION_1_0) {
            const char comp[] = "arm,psci-0.2\0arm,psci";
            qemu_fdt_setprop(fdt, "/psci", "compatible", comp, sizeof(comp));
        } else {
            const char comp[] = "arm,psci-1.0\0arm,psci-0.2\0arm,psci";
            qemu_fdt_setprop(fdt, "/psci", "compatible", comp, sizeof(comp));
        }

        cpu_off_fn = QEMU_PSCI_0_2_FN_CPU_OFF;
        if (arm_feature(&armcpu->env, ARM_FEATURE_AARCH64)) {
            cpu_suspend_fn = QEMU_PSCI_0_2_FN64_CPU_SUSPEND;
            cpu_on_fn = QEMU_PSCI_0_2_FN64_CPU_ON;
            migrate_fn = QEMU_PSCI_0_2_FN64_MIGRATE;
        } else {
            cpu_suspend_fn = QEMU_PSCI_0_2_FN_CPU_SUSPEND;
            cpu_on_fn = QEMU_PSCI_0_2_FN_CPU_ON;
            migrate_fn = QEMU_PSCI_0_2_FN_MIGRATE;
        }
    } else {
        qemu_fdt_setprop_string(fdt, "/psci", "compatible", "arm,psci");

        cpu_suspend_fn = QEMU_PSCI_0_1_FN_CPU_SUSPEND;
        cpu_off_fn = QEMU_PSCI_0_1_FN_CPU_OFF;
        cpu_on_fn = QEMU_PSCI_0_1_FN_CPU_ON;
        migrate_fn = QEMU_PSCI_0_1_FN_MIGRATE;
    }

    /* We adopt the PSCI spec's nomenclature, and use 'conduit' to refer
     * to the instruction that should be used to invoke PSCI functions.
     * However, the device tree binding uses 'method' instead, so that is
     * what we should use here.
     */
    qemu_fdt_setprop_string(fdt, "/psci", "method", psci_method);

    qemu_fdt_setprop_cell(fdt, "/psci", "cpu_suspend", cpu_suspend_fn);
    qemu_fdt_setprop_cell(fdt, "/psci", "cpu_off", cpu_off_fn);
    qemu_fdt_setprop_cell(fdt, "/psci", "cpu_on", cpu_on_fn);
    qemu_fdt_setprop_cell(fdt, "/psci", "migrate", migrate_fn);
}

int arm_load_dtb(hwaddr addr, const struct arm_boot_info *binfo,
                 hwaddr addr_limit, AddressSpace *as, MachineState *ms)
{
    void *fdt = NULL;
    int size, rc, n = 0;
    uint32_t acells, scells;
    unsigned int i;
    hwaddr mem_base, mem_len;
    char **node_path;
    Error *err = NULL;

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

    if (scells < 2 && binfo->ram_size >= 4 * GiB) {
        /* This is user error so deserves a friendlier error message
         * than the failure of setprop_sized_cells would provide
         */
        fprintf(stderr, "qemu: dtb file not compatible with "
                "RAM size > 4GB\n");
        goto fail;
    }

    /* nop all root nodes matching /memory or /memory@unit-address */
    node_path = qemu_fdt_node_unit_path(fdt, "memory", &err);
    if (err) {
        error_report_err(err);
        goto fail;
    }
    while (node_path[n]) {
        if (g_str_has_prefix(node_path[n], "/memory")) {
            qemu_fdt_nop_node(fdt, node_path[n]);
        }
        n++;
    }
    g_strfreev(node_path);

    /*
     * We drop all the memory nodes which correspond to empty NUMA nodes
     * from the device tree, because the Linux NUMA binding document
     * states they should not be generated. Linux will get the NUMA node
     * IDs of the empty NUMA nodes from the distance map if they are needed.
     * This means QEMU users may be obliged to provide command lines which
     * configure distance maps when the empty NUMA node IDs are needed and
     * Linux's default distance map isn't sufficient.
     */
    if (ms->numa_state != NULL && ms->numa_state->num_nodes > 0) {
        mem_base = binfo->loader_start;
        for (i = 0; i < ms->numa_state->num_nodes; i++) {
            mem_len = ms->numa_state->nodes[i].node_mem;
            if (!mem_len) {
                continue;
            }

            rc = fdt_add_memory_node(fdt, acells, mem_base,
                                     scells, mem_len, i);
            if (rc < 0) {
                fprintf(stderr, "couldn't add /memory@%"PRIx64" node\n",
                        mem_base);
                goto fail;
            }

            mem_base += mem_len;
        }
    } else {
        rc = fdt_add_memory_node(fdt, acells, binfo->loader_start,
                                 scells, binfo->ram_size, -1);
        if (rc < 0) {
            fprintf(stderr, "couldn't add /memory@%"PRIx64" node\n",
                    binfo->loader_start);
            goto fail;
        }
    }

    rc = fdt_path_offset(fdt, "/chosen");
    if (rc < 0) {
        qemu_fdt_add_subnode(fdt, "/chosen");
    }

    if (ms->kernel_cmdline && *ms->kernel_cmdline) {
        rc = qemu_fdt_setprop_string(fdt, "/chosen", "bootargs",
                                     ms->kernel_cmdline);
        if (rc < 0) {
            fprintf(stderr, "couldn't set /chosen/bootargs\n");
            goto fail;
        }
    }

    if (binfo->initrd_size) {
        rc = qemu_fdt_setprop_sized_cells(fdt, "/chosen", "linux,initrd-start",
                                          acells, binfo->initrd_start);
        if (rc < 0) {
            fprintf(stderr, "couldn't set /chosen/linux,initrd-start\n");
            goto fail;
        }

        rc = qemu_fdt_setprop_sized_cells(fdt, "/chosen", "linux,initrd-end",
                                          acells,
                                          binfo->initrd_start +
                                          binfo->initrd_size);
        if (rc < 0) {
            fprintf(stderr, "couldn't set /chosen/linux,initrd-end\n");
            goto fail;
        }
    }

    fdt_add_psci_node(fdt);

    if (binfo->modify_dtb) {
        binfo->modify_dtb(binfo, fdt);
    }

    qemu_fdt_dumpdtb(fdt, size);

    /* Put the DTB into the memory map as a ROM image: this will ensure
     * the DTB is copied again upon reset, even if addr points into RAM.
     */
    rom_add_blob_fixed_as("dtb", fdt, size, addr, as);
    qemu_register_reset_nosnapshotload(qemu_fdt_randomize_seeds,
                                       rom_ptr_for_as(as, addr, size));

    if (fdt != ms->fdt) {
        g_free(ms->fdt);
        ms->fdt = fdt;
    }

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
                    if (cpu_isar_feature(aa64_pauth, cpu)) {
                        env->cp15.scr_el3 |= SCR_API | SCR_APK;
                    }
                    if (cpu_isar_feature(aa64_mte, cpu)) {
                        env->cp15.scr_el3 |= SCR_ATA;
                    }
                    if (cpu_isar_feature(aa64_sve, cpu)) {
                        env->cp15.cptr_el[3] |= R_CPTR_EL3_EZ_MASK;
                        env->vfp.zcr_el[3] = 0xf;
                    }
                    if (cpu_isar_feature(aa64_sme, cpu)) {
                        env->cp15.cptr_el[3] |= R_CPTR_EL3_ESM_MASK;
                        env->cp15.scr_el3 |= SCR_ENTP2;
                        env->vfp.smcr_el[3] = 0xf;
                    }
                    if (cpu_isar_feature(aa64_hcx, cpu)) {
                        env->cp15.scr_el3 |= SCR_HXEN;
                    }
                    /* AArch64 kernels never boot in secure mode */
                    assert(!info->secure_boot);
                    /* This hook is only supported for AArch32 currently:
                     * bootloader_aarch64[] will not call the hook, and
                     * the code above has already dropped us into EL2 or EL1.
                     */
                    assert(!info->secure_board_setup);
                }

                if (arm_feature(env, ARM_FEATURE_EL2)) {
                    /* If we have EL2 then Linux expects the HVC insn to work */
                    env->cp15.scr_el3 |= SCR_HCE;
                }

                /* Set to non-secure if not a secure boot */
                if (!info->secure_boot &&
                    (cs != first_cpu || !info->secure_board_setup)) {
                    /* Linux expects non-secure state */
                    env->cp15.scr_el3 |= SCR_NS;
                    /* Set NSACR.{CP11,CP10} so NS can access the FPU */
                    env->cp15.nsacr |= 3 << 10;
                }
            }

            if (!env->aarch64 && !info->secure_boot &&
                arm_feature(env, ARM_FEATURE_EL2)) {
                /*
                 * This is an AArch32 boot not to Secure state, and
                 * we have Hyp mode available, so boot the kernel into
                 * Hyp mode. This is not how the CPU comes out of reset,
                 * so we need to manually put it there.
                 */
                cpsr_write(env, ARM_CPU_MODE_HYP, CPSR_M, CPSRWriteRaw);
            }

            if (cs == first_cpu) {
                AddressSpace *as = arm_boot_address_space(cpu, info);

                cpu_set_pc(cs, info->loader_start);

                if (!have_dtb(info)) {
                    if (old_param) {
                        set_kernel_args_old(info, as);
                    } else {
                        set_kernel_args(info, as);
                    }
                }
            } else if (info->secondary_cpu_reset_hook) {
                info->secondary_cpu_reset_hook(cpu, info);
            }
        }

        if (tcg_enabled()) {
            arm_rebuild_hflags(env);
        }
    }
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

static ssize_t arm_load_elf(struct arm_boot_info *info, uint64_t *pentry,
                            uint64_t *lowaddr, uint64_t *highaddr,
                            int elf_machine, AddressSpace *as)
{
    bool elf_is64;
    union {
        Elf32_Ehdr h32;
        Elf64_Ehdr h64;
    } elf_header;
    int data_swab = 0;
    bool big_endian;
    ssize_t ret = -1;
    Error *err = NULL;


    load_elf_hdr(info->kernel_filename, &elf_header, &elf_is64, &err);
    if (err) {
        error_free(err);
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

    ret = load_elf_as(info->kernel_filename, NULL, NULL, NULL,
                      pentry, lowaddr, highaddr, NULL, big_endian, elf_machine,
                      1, data_swab, as);
    if (ret <= 0) {
        /* The header loaded but the image didn't */
        exit(1);
    }

    return ret;
}

static uint64_t load_aarch64_image(const char *filename, hwaddr mem_base,
                                   hwaddr *entry, AddressSpace *as)
{
    hwaddr kernel_load_offset = KERNEL64_LOAD_ADDR;
    uint64_t kernel_size = 0;
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

        /* Unpack the image if it is a EFI zboot image */
        if (unpack_efi_zboot_image(&buffer, &size) < 0) {
            g_free(buffer);
            return -1;
        }
    }

    /* check the arm64 magic header value -- very old kernels may not have it */
    if (size > ARM64_MAGIC_OFFSET + 4 &&
        memcmp(buffer + ARM64_MAGIC_OFFSET, "ARM\x64", 4) == 0) {
        uint64_t hdrvals[2];

        /* The arm64 Image header has text_offset and image_size fields at 8 and
         * 16 bytes into the Image header, respectively. The text_offset field
         * is only valid if the image_size is non-zero.
         */
        memcpy(&hdrvals, buffer + ARM64_TEXT_OFFSET_OFFSET, sizeof(hdrvals));

        kernel_size = le64_to_cpu(hdrvals[1]);

        if (kernel_size != 0) {
            kernel_load_offset = le64_to_cpu(hdrvals[0]);

            /*
             * We write our startup "bootloader" at the very bottom of RAM,
             * so that bit can't be used for the image. Luckily the Image
             * format specification is that the image requests only an offset
             * from a 2MB boundary, not an absolute load address. So if the
             * image requests an offset that might mean it overlaps with the
             * bootloader, we can just load it starting at 2MB+offset rather
             * than 0MB + offset.
             */
            if (kernel_load_offset < BOOTLOADER_MAX_SIZE) {
                kernel_load_offset += 2 * MiB;
            }
        }
    }

    /*
     * Kernels before v3.17 don't populate the image_size field, and
     * raw images have no header. For those our best guess at the size
     * is the size of the Image file itself.
     */
    if (kernel_size == 0) {
        kernel_size = size;
    }

    *entry = mem_base + kernel_load_offset;
    rom_add_blob_fixed_as(filename, buffer, size, *entry, as);

    g_free(buffer);

    return kernel_size;
}

static void arm_setup_direct_kernel_boot(ARMCPU *cpu,
                                         struct arm_boot_info *info)
{
    /* Set up for a direct boot of a kernel image file. */
    CPUState *cs;
    AddressSpace *as = arm_boot_address_space(cpu, info);
    ssize_t kernel_size;
    int initrd_size;
    int is_linux = 0;
    uint64_t elf_entry;
    /* Addresses of first byte used and first byte not used by the image */
    uint64_t image_low_addr = 0, image_high_addr = 0;
    int elf_machine;
    hwaddr entry;
    static const ARMInsnFixup *primary_loader;
    uint64_t ram_end = info->loader_start + info->ram_size;

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

    /* Assume that raw images are linux kernels, and ELF images are not.  */
    kernel_size = arm_load_elf(info, &elf_entry, &image_low_addr,
                               &image_high_addr, elf_machine, as);
    if (kernel_size > 0 && have_dtb(info)) {
        /*
         * If there is still some room left at the base of RAM, try and put
         * the DTB there like we do for images loaded with -bios or -pflash.
         */
        if (image_low_addr > info->loader_start
            || image_high_addr < info->loader_start) {
            /*
             * Set image_low_addr as address limit for arm_load_dtb if it may be
             * pointing into RAM, otherwise pass '0' (no limit)
             */
            if (image_low_addr < info->loader_start) {
                image_low_addr = 0;
            }
            info->dtb_start = info->loader_start;
            info->dtb_limit = image_low_addr;
        }
    }
    entry = elf_entry;
    if (kernel_size < 0) {
        uint64_t loadaddr = info->loader_start + KERNEL_NOLOAD_ADDR;
        kernel_size = load_uimage_as(info->kernel_filename, &entry, &loadaddr,
                                     &is_linux, NULL, NULL, as);
        if (kernel_size >= 0) {
            image_low_addr = loadaddr;
            image_high_addr = image_low_addr + kernel_size;
        }
    }
    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64) && kernel_size < 0) {
        kernel_size = load_aarch64_image(info->kernel_filename,
                                         info->loader_start, &entry, as);
        is_linux = 1;
        if (kernel_size >= 0) {
            image_low_addr = entry;
            image_high_addr = image_low_addr + kernel_size;
        }
    } else if (kernel_size < 0) {
        /* 32-bit ARM */
        entry = info->loader_start + KERNEL_LOAD_ADDR;
        kernel_size = load_image_targphys_as(info->kernel_filename, entry,
                                             ram_end - KERNEL_LOAD_ADDR, as);
        is_linux = 1;
        if (kernel_size >= 0) {
            image_low_addr = entry;
            image_high_addr = image_low_addr + kernel_size;
        }
    }
    if (kernel_size < 0) {
        error_report("could not load kernel '%s'", info->kernel_filename);
        exit(1);
    }

    if (kernel_size > info->ram_size) {
        error_report("kernel '%s' is too large to fit in RAM "
                     "(kernel size %zd, RAM size %" PRId64 ")",
                     info->kernel_filename, kernel_size, info->ram_size);
        exit(1);
    }

    info->entry = entry;

    /*
     * We want to put the initrd far enough into RAM that when the
     * kernel is uncompressed it will not clobber the initrd. However
     * on boards without much RAM we must ensure that we still leave
     * enough room for a decent sized initrd, and on boards with large
     * amounts of RAM we must avoid the initrd being so far up in RAM
     * that it is outside lowmem and inaccessible to the kernel.
     * So for boards with less  than 256MB of RAM we put the initrd
     * halfway into RAM, and for boards with 256MB of RAM or more we put
     * the initrd at 128MB.
     * We also refuse to put the initrd somewhere that will definitely
     * overlay the kernel we just loaded, though for kernel formats which
     * don't tell us their exact size (eg self-decompressing 32-bit kernels)
     * we might still make a bad choice here.
     */
    info->initrd_start = info->loader_start +
        MIN(info->ram_size / 2, 128 * MiB);
    if (image_high_addr) {
        info->initrd_start = MAX(info->initrd_start, image_high_addr);
    }
    info->initrd_start = TARGET_PAGE_ALIGN(info->initrd_start);

    if (is_linux) {
        uint32_t fixupcontext[FIXUP_MAX];

        if (info->initrd_filename) {

            if (info->initrd_start >= ram_end) {
                error_report("not enough space after kernel to load initrd");
                exit(1);
            }

            initrd_size = load_ramdisk_as(info->initrd_filename,
                                          info->initrd_start,
                                          ram_end - info->initrd_start, as);
            if (initrd_size < 0) {
                initrd_size = load_image_targphys_as(info->initrd_filename,
                                                     info->initrd_start,
                                                     ram_end -
                                                     info->initrd_start,
                                                     as);
            }
            if (initrd_size < 0) {
                error_report("could not load initrd '%s'",
                             info->initrd_filename);
                exit(1);
            }
            if (info->initrd_start + initrd_size > ram_end) {
                error_report("could not load initrd '%s': "
                             "too big to fit into RAM after the kernel",
                             info->initrd_filename);
                exit(1);
            }
        } else {
            initrd_size = 0;
        }
        info->initrd_size = initrd_size;

        fixupcontext[FIXUP_BOARDID] = info->board_id;
        fixupcontext[FIXUP_BOARD_SETUP] = info->board_setup_addr;

        /*
         * for device tree boot, we pass the DTB directly in r2. Otherwise
         * we point to the kernel args.
         */
        if (have_dtb(info)) {
            hwaddr align;

            if (elf_machine == EM_AARCH64) {
                /*
                 * Some AArch64 kernels on early bootup map the fdt region as
                 *
                 *   [ ALIGN_DOWN(fdt, 2MB) ... ALIGN_DOWN(fdt, 2MB) + 2MB ]
                 *
                 * Let's play safe and prealign it to 2MB to give us some space.
                 */
                align = 2 * MiB;
            } else {
                /*
                 * Some 32bit kernels will trash anything in the 4K page the
                 * initrd ends in, so make sure the DTB isn't caught up in that.
                 */
                align = 4 * KiB;
            }

            /* Place the DTB after the initrd in memory with alignment. */
            info->dtb_start = QEMU_ALIGN_UP(info->initrd_start + initrd_size,
                                           align);
            if (info->dtb_start >= ram_end) {
                error_report("Not enough space for DTB after kernel/initrd");
                exit(1);
            }
            fixupcontext[FIXUP_ARGPTR_LO] = info->dtb_start;
            fixupcontext[FIXUP_ARGPTR_HI] = info->dtb_start >> 32;
        } else {
            fixupcontext[FIXUP_ARGPTR_LO] =
                info->loader_start + KERNEL_ARGS_ADDR;
            fixupcontext[FIXUP_ARGPTR_HI] =
                (info->loader_start + KERNEL_ARGS_ADDR) >> 32;
            if (info->ram_size >= 4 * GiB) {
                error_report("RAM size must be less than 4GB to boot"
                             " Linux kernel using ATAGS (try passing a device tree"
                             " using -dtb)");
                exit(1);
            }
        }
        fixupcontext[FIXUP_ENTRYPOINT_LO] = entry;
        fixupcontext[FIXUP_ENTRYPOINT_HI] = entry >> 32;

        arm_write_bootloader("bootloader", as, info->loader_start,
                             primary_loader, fixupcontext);

        if (info->write_board_setup) {
            info->write_board_setup(cpu, info);
        }

        /*
         * Notify devices which need to fake up firmware initialization
         * that we're doing a direct kernel boot.
         */
        object_child_foreach_recursive(object_get_root(),
                                       do_arm_linux_init, info);
    }
    info->is_linux = is_linux;

    for (cs = first_cpu; cs; cs = CPU_NEXT(cs)) {
        ARM_CPU(cs)->env.boot_info = info;
    }
}

static void arm_setup_firmware_boot(ARMCPU *cpu, struct arm_boot_info *info)
{
    /* Set up for booting firmware (which might load a kernel via fw_cfg) */

    if (have_dtb(info)) {
        /*
         * If we have a device tree blob, but no kernel to supply it to (or
         * the kernel is supposed to be loaded by the bootloader), copy the
         * DTB to the base of RAM for the bootloader to pick up.
         */
        info->dtb_start = info->loader_start;
    }

    if (info->kernel_filename) {
        FWCfgState *fw_cfg;
        bool try_decompressing_kernel;

        fw_cfg = fw_cfg_find();

        if (!fw_cfg) {
            error_report("This machine type does not support loading both "
                         "a guest firmware/BIOS image and a guest kernel at "
                         "the same time. You should change your QEMU command "
                         "line to specify one or the other, but not both.");
            exit(1);
        }

        try_decompressing_kernel = arm_feature(&cpu->env,
                                               ARM_FEATURE_AARCH64);

        /*
         * Expose the kernel, the command line, and the initrd in fw_cfg.
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

    /*
     * We will start from address 0 (typically a boot ROM image) in the
     * same way as hardware. Leave env->boot_info NULL, so that
     * do_cpu_reset() knows it does not need to alter the PC on reset.
     */
}

void arm_load_kernel(ARMCPU *cpu, MachineState *ms, struct arm_boot_info *info)
{
    CPUState *cs;
    AddressSpace *as = arm_boot_address_space(cpu, info);
    int boot_el;
    CPUARMState *env = &cpu->env;
    int nb_cpus = 0;

    /*
     * CPU objects (unlike devices) are not automatically reset on system
     * reset, so we must always register a handler to do so. If we're
     * actually loading a kernel, the handler is also responsible for
     * arranging that we start it correctly.
     */
    for (cs = first_cpu; cs; cs = CPU_NEXT(cs)) {
        qemu_register_reset(do_cpu_reset, ARM_CPU(cs));
        nb_cpus++;
    }

    /*
     * The board code is not supposed to set secure_board_setup unless
     * running its code in secure mode is actually possible, and KVM
     * doesn't support secure.
     */
    assert(!(info->secure_board_setup && kvm_enabled()));
    info->kernel_filename = ms->kernel_filename;
    info->kernel_cmdline = ms->kernel_cmdline;
    info->initrd_filename = ms->initrd_filename;
    info->dtb_filename = ms->dtb;
    info->dtb_limit = 0;

    /* Load the kernel.  */
    if (!info->kernel_filename || info->firmware_loaded) {
        arm_setup_firmware_boot(cpu, info);
    } else {
        arm_setup_direct_kernel_boot(cpu, info);
    }

    /*
     * Disable the PSCI conduit if it is set up to target the same
     * or a lower EL than the one we're going to start the guest code in.
     * This logic needs to agree with the code in do_cpu_reset() which
     * decides whether we're going to boot the guest in the highest
     * supported exception level or in a lower one.
     */

    /*
     * If PSCI is enabled, then SMC calls all go to the PSCI handler and
     * are never emulated to trap into guest code. It therefore does not
     * make sense for the board to have a setup code fragment that runs
     * in Secure, because this will probably need to itself issue an SMC of some
     * kind as part of its operation.
     */
    assert(info->psci_conduit == QEMU_PSCI_CONDUIT_DISABLED ||
           !info->secure_board_setup);

    /* Boot into highest supported EL ... */
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        boot_el = 3;
    } else if (arm_feature(env, ARM_FEATURE_EL2)) {
        boot_el = 2;
    } else {
        boot_el = 1;
    }
    /* ...except that if we're booting Linux we adjust the EL we boot into */
    if (info->is_linux && !info->secure_boot) {
        boot_el = arm_feature(env, ARM_FEATURE_EL2) ? 2 : 1;
    }

    if ((info->psci_conduit == QEMU_PSCI_CONDUIT_HVC && boot_el >= 2) ||
        (info->psci_conduit == QEMU_PSCI_CONDUIT_SMC && boot_el == 3)) {
        info->psci_conduit = QEMU_PSCI_CONDUIT_DISABLED;
    }

    if (info->psci_conduit != QEMU_PSCI_CONDUIT_DISABLED) {
        for (cs = first_cpu; cs; cs = CPU_NEXT(cs)) {
            Object *cpuobj = OBJECT(cs);

            object_property_set_int(cpuobj, "psci-conduit", info->psci_conduit,
                                    &error_abort);
            /*
             * Secondary CPUs start in PSCI powered-down state. Like the
             * code in do_cpu_reset(), we assume first_cpu is the primary
             * CPU.
             */
            if (cs != first_cpu) {
                object_property_set_bool(cpuobj, "start-powered-off", true,
                                         &error_abort);
            }
        }
    }

    if (info->psci_conduit == QEMU_PSCI_CONDUIT_DISABLED &&
        info->is_linux && nb_cpus > 1) {
        /*
         * We're booting Linux but not using PSCI, so for SMP we need
         * to write a custom secondary CPU boot loader stub, and arrange
         * for the secondary CPU reset to make the accompanying initialization.
         */
        if (!info->secondary_cpu_reset_hook) {
            info->secondary_cpu_reset_hook = default_reset_secondary;
        }
        if (!info->write_secondary_boot) {
            info->write_secondary_boot = default_write_secondary;
        }
        info->write_secondary_boot(cpu, info);
    } else {
        /*
         * No secondary boot stub; don't use the reset hook that would
         * have set the CPU up to call it
         */
        info->write_secondary_boot = NULL;
        info->secondary_cpu_reset_hook = NULL;
    }

    /*
     * arm_load_dtb() may add a PSCI node so it must be called after we have
     * decided whether to enable PSCI and set the psci-conduit CPU properties.
     */
    if (!info->skip_dtb_autoload && have_dtb(info)) {
        if (arm_load_dtb(info->dtb_start, info, info->dtb_limit, as, ms) < 0) {
            exit(1);
        }
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
