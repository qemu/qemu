/*
 * fw_cfg helpers (PPC specific)
 *
 * Copyright (c) 2019 Red Hat, Inc.
 *
 * Author:
 *   Philippe Mathieu-Daudé <philmd@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/ppc/ppc.h"
#include "hw/nvram/fw_cfg.h"

const char *fw_cfg_arch_key_name(uint16_t key)
{
    static const struct {
        uint16_t key;
        const char *name;
    } fw_cfg_arch_wellknown_keys[] = {
        {FW_CFG_PPC_WIDTH, "width"},
        {FW_CFG_PPC_HEIGHT, "height"},
        {FW_CFG_PPC_DEPTH, "depth"},
        {FW_CFG_PPC_TBFREQ, "tbfreq"},
        {FW_CFG_PPC_CLOCKFREQ, "clockfreq"},
        {FW_CFG_PPC_IS_KVM, "is_kvm"},
        {FW_CFG_PPC_KVM_HC, "kvm_hc"},
        {FW_CFG_PPC_KVM_PID, "pid"},
        {FW_CFG_PPC_NVRAM_ADDR, "nvram_addr"},
        {FW_CFG_PPC_BUSFREQ, "busfreq"},
        {FW_CFG_PPC_NVRAM_FLAT, "nvram_flat"},
        {FW_CFG_PPC_VIACONFIG, "viaconfig"},
    };

    for (size_t i = 0; i < ARRAY_SIZE(fw_cfg_arch_wellknown_keys); i++) {
        if (fw_cfg_arch_wellknown_keys[i].key == key) {
            return fw_cfg_arch_wellknown_keys[i].name;
        }
    }
    return NULL;
}
