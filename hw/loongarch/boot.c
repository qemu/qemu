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

static const unsigned int slave_boot_code[] = {
                  /* Configure reset ebase.                    */
    0x0400302c,   /* csrwr      $t0, LOONGARCH_CSR_EENTRY      */

                  /* Disable interrupt.                        */
    0x0380100c,   /* ori        $t0, $zero,0x4                 */
    0x04000180,   /* csrxchg    $zero, $t0, LOONGARCH_CSR_CRMD */

                  /* Clear mailbox.                            */
    0x1400002d,   /* lu12i.w    $t1, 1(0x1)                    */
    0x038081ad,   /* ori        $t1, $t1, CORE_BUF_20  */
    0x06481da0,   /* iocsrwr.d  $zero, $t1                     */

                  /* Enable IPI interrupt.                     */
    0x1400002c,   /* lu12i.w    $t0, 1(0x1)                    */
    0x0400118c,   /* csrxchg    $t0, $t0, LOONGARCH_CSR_ECFG   */
    0x02fffc0c,   /* addi.d     $t0, $r0,-1(0xfff)             */
    0x1400002d,   /* lu12i.w    $t1, 1(0x1)                    */
    0x038011ad,   /* ori        $t1, $t1, CORE_EN_OFF          */
    0x064819ac,   /* iocsrwr.w  $t0, $t1                       */
    0x1400002d,   /* lu12i.w    $t1, 1(0x1)                    */
    0x038081ad,   /* ori        $t1, $t1, CORE_BUF_20          */

                  /* Wait for wakeup  <.L11>:                  */
    0x06488000,   /* idle       0x0                            */
    0x03400000,   /* andi       $zero, $zero, 0x0              */
    0x064809ac,   /* iocsrrd.w  $t0, $t1                       */
    0x43fff59f,   /* beqz       $t0, -12(0x7ffff4) # 48 <.L11> */

                  /* Read and clear IPI interrupt.             */
    0x1400002d,   /* lu12i.w    $t1, 1(0x1)                    */
    0x064809ac,   /* iocsrrd.w  $t0, $t1                       */
    0x1400002d,   /* lu12i.w    $t1, 1(0x1)                    */
    0x038031ad,   /* ori        $t1, $t1, CORE_CLEAR_OFF       */
    0x064819ac,   /* iocsrwr.w  $t0, $t1                       */

                  /* Disable  IPI interrupt.                   */
    0x1400002c,   /* lu12i.w    $t0, 1(0x1)                    */
    0x04001180,   /* csrxchg    $zero, $t0, LOONGARCH_CSR_ECFG */

                  /* Read mail buf and jump to specified entry */
    0x1400002d,   /* lu12i.w    $t1, 1(0x1)                    */
    0x038081ad,   /* ori        $t1, $t1, CORE_BUF_20          */
    0x06480dac,   /* iocsrrd.d  $t0, $t1                       */
    0x00150181,   /* move       $ra, $t0                       */
    0x4c000020,   /* jirl       $zero, $ra,0                   */
};

static uint64_t cpu_loongarch_virt_to_phys(void *opaque, uint64_t addr)
{
    return addr & MAKE_64BIT_MASK(0, TARGET_PHYS_ADDR_SPACE_BITS);
}

static int64_t load_kernel_info(struct loongarch_boot_info *info)
{
    uint64_t kernel_entry, kernel_low, kernel_high, initrd_size;
    ram_addr_t initrd_offset;
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

    if (info->initrd_filename) {
        initrd_size = get_image_size(info->initrd_filename);
        if (initrd_size > 0) {
            initrd_offset = ROUND_UP(kernel_high + 4 * kernel_size, 64 * KiB);

            if (initrd_offset + initrd_size > info->ram_size) {
                error_report("memory too small for initial ram disk '%s'",
                             info->initrd_filename);
                exit(1);
            }

            initrd_size = load_image_targphys(info->initrd_filename, initrd_offset,
                                              info->ram_size - initrd_offset);
        }

        if (initrd_size == (target_ulong)-1) {
            error_report("could not load initial ram disk '%s'",
                         info->initrd_filename);
            exit(1);
        }
    } else {
        initrd_size = 0;
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

    /* Load slave boot code at pflash0 . */
    void *boot_code = g_malloc0(VIRT_FLASH0_SIZE);
    memcpy(boot_code, &slave_boot_code, sizeof(slave_boot_code));
    rom_add_blob_fixed("boot_code", boot_code, VIRT_FLASH0_SIZE, VIRT_FLASH0_BASE);

    CPU_FOREACH(cs) {
        lacpu = LOONGARCH_CPU(cs);
        lacpu->env.load_elf = true;
        if (cs == first_cpu) {
            lacpu->env.elf_address = kernel_addr;
        } else {
            lacpu->env.elf_address = VIRT_FLASH0_BASE;
        }
        lacpu->env.boot_info = info;
    }

    g_free(boot_code);
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
