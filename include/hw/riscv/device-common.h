/*
 * RISC-V board helpers for FDT generation.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RISCV_DEVICE_COMMON_H
#define RISCV_DEVICE_COMMON_H

#include "exec/hwaddr.h"
#include "hw/block/flash.h"
#include "hw/core/qdev.h"

DeviceState *riscv_create_platform_bus(DeviceState *irqchip,
                                       const MemMapEntry *platform_bus_mem,
                                       int platform_bus_base_irq,
                                       int platform_bus_num_irqs);
PFlashCFI01 *riscv_flash_create(Object *parent, const char *name,
                                const char *alias_prop_name,
                                int flash_sector_size);
void riscv_init_flash_map(PFlashCFI01 *flash, hwaddr base, hwaddr size,
                          MemoryRegion *sysmem, int flash_sector_size);
#endif
