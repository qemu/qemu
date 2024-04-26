/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch boot helper functions.
 *
 * Copyright (c) 2023 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "target/loongarch/cpu.h"
#include "hw/loongarch/virt.h"
#include "hw/loader.h"
#include "elf.h"
#include "qemu/error-report.h"
#include "sysemu/reset.h"
#include "sysemu/qtest.h"

static uint64_t cpu_loongarch_virt_to_phys(void *opaque, uint64_t addr)
{
    return addr & MAKE_64BIT_MASK(0, TARGET_PHYS_ADDR_SPACE_BITS);
}

static int64_t load_kernel_info(struct loongarch_boot_info *info)
{
    uint64_t kernel_entry, kernel_low, kernel_high;
    ssize_t kernel_size;

    kernel_size = load_elf(info->kernel_filename, NULL,
                           cpu_loongarch_virt_to_phys, NULL,
                           &kernel_entry, &kernel_low,
                           &kernel_high, NULL, 0,
                           EM_LOONGARCH, 1, 0);

    if (kernel_size < 0) {
        error_report("could not load kernel '%s': %s",
                     info->kernel_filename,
                     load_elf_strerror(kernel_size));
        exit(1);
    }
    return kernel_entry;
}

static void reset_load_elf(void *opaque)
{
    LoongArchCPU *cpu = opaque;
    CPULoongArchState *env = &cpu->env;

    cpu_reset(CPU(cpu));
    if (env->load_elf) {
        cpu_set_pc(CPU(cpu), env->elf_address);
    }
}

static void fw_cfg_add_kernel_info(struct loongarch_boot_info *info,
                                   FWCfgState *fw_cfg)
{
    /*
     * Expose the kernel, the command line, and the initrd in fw_cfg.
     * We don't process them here at all, it's all left to the
     * firmware.
     */
    load_image_to_fw_cfg(fw_cfg,
                         FW_CFG_KERNEL_SIZE, FW_CFG_KERNEL_DATA,
                         info->kernel_filename,
                         false);

    if (info->initrd_filename) {
        load_image_to_fw_cfg(fw_cfg,
                             FW_CFG_INITRD_SIZE, FW_CFG_INITRD_DATA,
                             info->initrd_filename, false);
    }

    if (info->kernel_cmdline) {
        fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE,
                       strlen(info->kernel_cmdline) + 1);
        fw_cfg_add_string(fw_cfg, FW_CFG_CMDLINE_DATA,
                          info->kernel_cmdline);
    }
}

static void loongarch_firmware_boot(LoongArchMachineState *lams,
                                    struct loongarch_boot_info *info)
{
    fw_cfg_add_kernel_info(info, lams->fw_cfg);
}

static void loongarch_direct_kernel_boot(struct loongarch_boot_info *info)
{
    int64_t kernel_addr = 0;
    LoongArchCPU *lacpu;
    CPUState *cs;

    if (info->kernel_filename) {
        kernel_addr = load_kernel_info(info);
    } else {
        if(!qtest_enabled()) {
            error_report("Need kernel filename\n");
            exit(1);
        }
    }

    CPU_FOREACH(cs) {
        lacpu = LOONGARCH_CPU(cs);
        lacpu->env.load_elf = true;
        lacpu->env.elf_address = kernel_addr;
    }
}

void loongarch_load_kernel(MachineState *ms, struct loongarch_boot_info *info)
{
    LoongArchMachineState *lams = LOONGARCH_MACHINE(ms);
    int i;

    /* register reset function */
    for (i = 0; i < ms->smp.cpus; i++) {
        qemu_register_reset(reset_load_elf, LOONGARCH_CPU(qemu_get_cpu(i)));
    }

    info->kernel_filename = ms->kernel_filename;
    info->kernel_cmdline = ms->kernel_cmdline;
    info->initrd_filename = ms->initrd_filename;

    if (lams->bios_loaded) {
        loongarch_firmware_boot(lams, info);
    } else {
        loongarch_direct_kernel_boot(info);
    }
}
