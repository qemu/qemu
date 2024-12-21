/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU fw_cfg helpers (LoongArch specific)
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/loongarch/fw_cfg.h"
#include "hw/loongarch/virt.h"
#include "hw/nvram/fw_cfg.h"
#include "system/system.h"

static void fw_cfg_boot_set(void *opaque, const char *boot_device,
                            Error **errp)
{
    fw_cfg_modify_i16(opaque, FW_CFG_BOOT_DEVICE, boot_device[0]);
}

FWCfgState *virt_fw_cfg_init(ram_addr_t ram_size, MachineState *ms)
{
    FWCfgState *fw_cfg;
    int max_cpus = ms->smp.max_cpus;
    int smp_cpus = ms->smp.cpus;

    fw_cfg = fw_cfg_init_mem_wide(VIRT_FWCFG_BASE + 8, VIRT_FWCFG_BASE, 8,
                                  VIRT_FWCFG_BASE + 16, &address_space_memory);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, (uint16_t)max_cpus);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)ram_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, (uint16_t)smp_cpus);

    qemu_register_boot_set(fw_cfg_boot_set, fw_cfg);
    return fw_cfg;
}
