/*
 * QEMU fw_cfg helpers (MIPS specific)
 *
 * Copyright (c) 2020 Lemote, Inc.
 *
 * Author:
 *   Huacai Chen (chenhc@lemote.com)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/mips/fw_cfg.h"
#include "hw/nvram/fw_cfg.h"

const char *fw_cfg_arch_key_name(uint16_t key)
{
    static const struct {
        uint16_t key;
        const char *name;
    } fw_cfg_arch_wellknown_keys[] = {
        {FW_CFG_MACHINE_VERSION, "machine_version"},
        {FW_CFG_CPU_FREQ, "cpu_frequency"},
    };

    for (size_t i = 0; i < ARRAY_SIZE(fw_cfg_arch_wellknown_keys); i++) {
        if (fw_cfg_arch_wellknown_keys[i].key == key) {
            return fw_cfg_arch_wellknown_keys[i].name;
        }
    }
    return NULL;
}
