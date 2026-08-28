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
#include "hw/core/qdev.h"

DeviceState *riscv_create_platform_bus(DeviceState *irqchip,
                                       const MemMapEntry *platform_bus_mem,
                                       int platform_bus_base_irq,
                                       int platform_bus_num_irqs);
#endif
