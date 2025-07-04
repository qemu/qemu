/*
 * MAX78000 SOC
 *
 * Copyright (c) 2025 Jackson Donaldson <jcksn@duck.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_MAX78000_SOC_H
#define HW_ARM_MAX78000_SOC_H

#include "hw/or-irq.h"
#include "hw/arm/armv7m.h"
#include "hw/misc/max78000_icc.h"
#include "qom/object.h"

#define TYPE_MAX78000_SOC "max78000-soc"
OBJECT_DECLARE_SIMPLE_TYPE(MAX78000State, MAX78000_SOC)

#define FLASH_BASE_ADDRESS 0x10000000
#define FLASH_SIZE (512 * 1024)
#define SRAM_BASE_ADDRESS 0x20000000
#define SRAM_SIZE (128 * 1024)

/* The MAX78k has 2 instruction caches; only icc0 matters, icc1 is for RISC */
#define MAX78000_NUM_ICC 2

struct MAX78000State {
    SysBusDevice parent_obj;

    ARMv7MState armv7m;

    MemoryRegion sram;
    MemoryRegion flash;

    Max78000IccState icc[MAX78000_NUM_ICC];

    Clock *sysclk;
};

#endif
