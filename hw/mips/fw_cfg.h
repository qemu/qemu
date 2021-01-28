/*
 * QEMU fw_cfg helpers (MIPS specific)
 *
 * Copyright (c) 2020 Huacai Chen
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef HW_MIPS_FW_CFG_H
#define HW_MIPS_FW_CFG_H

#include "hw/boards.h"
#include "hw/nvram/fw_cfg.h"

/* Data for BIOS to identify machine */
#define FW_CFG_MACHINE_VERSION  (FW_CFG_ARCH_LOCAL + 0)
#define FW_CFG_CPU_FREQ         (FW_CFG_ARCH_LOCAL + 1)

#endif
