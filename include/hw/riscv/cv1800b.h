/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Sophgo CV1800B SoC
 *
 * Copyright (c) 2026 Kuan-Wei Chiu <visitorckw@gmail.com>
 */

#ifndef HW_RISCV_CV1800B_H
#define HW_RISCV_CV1800B_H

#include "hw/core/boards.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/misc/cv1800b_clk.h"
#include "hw/char/dw8250.h"
#include "hw/sd/sdhci.h"

#define TYPE_CV1800B_SOC "cv1800b-soc"
OBJECT_DECLARE_SIMPLE_TYPE(CV1800BSoCState, CV1800B_SOC)

struct CV1800BSoCState {
    DeviceState parent_obj;

    RISCVHartArrayState cpus;
    MemoryRegion rom;
    DeviceState *plic;
    CV1800BClkState clk;
    DW8250State uart;
    SDHCIState sdhci;
};

#define CV1800B_PLIC_NUM_SOURCES 136
#define CV1800B_PLIC_NUM_PRIORITIES 31

#define CV1800B_UART0_IRQ 44
#define CV1800B_SD0_IRQ   36

enum {
    CV1800B_DEV_TOP_MISC,
    CV1800B_DEV_PINMUX,
    CV1800B_DEV_CLK,
    CV1800B_DEV_RST,
    CV1800B_DEV_WDT,
    CV1800B_DEV_GPIO,
    CV1800B_DEV_UART0,
    CV1800B_DEV_SD0,
    CV1800B_DEV_ROM,
    CV1800B_DEV_RTC_GPIO,
    CV1800B_DEV_RTC_IO,
    CV1800B_DEV_PLIC,
    CV1800B_DEV_CLINT,
    CV1800B_DEV_DRAM,
};

extern const MemMapEntry cv1800b_memmap[];

#endif
