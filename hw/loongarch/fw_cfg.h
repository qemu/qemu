/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU fw_cfg helpers (LoongArch specific)
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#ifndef HW_LOONGARCH_FW_CFG_H
#define HW_LOONGARCH_FW_CFG_H

#include "hw/boards.h"
#include "hw/nvram/fw_cfg.h"

FWCfgState *loongarch_fw_cfg_init(ram_addr_t ram_size, MachineState *ms);
#endif
