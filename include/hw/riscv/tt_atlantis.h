/*
 * Tenstorrent Atlantis RISC-V System on Chip
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright 2025 Tenstorrent, Joel Stanley <joel@jms.id.au>
 */

#ifndef HW_RISCV_TT_ATLANTIS_H
#define HW_RISCV_TT_ATLANTIS_H

#include "hw/core/boards.h"
#include "hw/core/sysbus.h"
#include "hw/i2c/designware_i2c.h"
#include "hw/intc/riscv_imsic.h"
#include "hw/misc/unimp.h"
#include "hw/misc/tt_atlantis_prcm.h"
#include "hw/riscv/riscv_hart.h"

#define TYPE_TT_ATLANTIS_MACHINE MACHINE_TYPE_NAME("tt-atlantis")
OBJECT_DECLARE_SIMPLE_TYPE(TTAtlantisState, TT_ATLANTIS_MACHINE)

#define TYPE_TT_ATLANTIS_SOC "tt-atlantis-soc"
OBJECT_DECLARE_SIMPLE_TYPE(TTAtlantisSoCState, TT_ATLANTIS_SOC)

#define TT_ATL_NUM_I2C 5
#define TT_ATL_NUM_PRCM 4

struct TTAtlantisSoCState {
    /*< private >*/
    DeviceState parent;

    /*< public >*/
    const MemMapEntry *memmap;

    MemoryRegion *memory;
    MemoryRegion *dram;
    MemoryRegion ram_hi;
    MemoryRegion ram_lo;

    RISCVHartArrayState cpus;
    DeviceState *irqchip;
    DesignWareI2CState i2c[TT_ATL_NUM_I2C];
    UnimplementedDeviceState uart1;
    MemoryRegion bootrom;
    TTAtlantisPRCMState prcm[TT_ATL_NUM_PRCM];

    uint32_t num_harts;
    char *cpu_type;
};

struct TTAtlantisState {
    /*< private >*/
    MachineState parent;

    /*< public >*/
    Notifier machine_done;

    MemoryRegion soc_memory;
    TTAtlantisSoCState soc;
};

enum {
    TT_ATL_I2C0_IRQ = 33,
    TT_ATL_I2C1_IRQ = 34,
    TT_ATL_I2C2_IRQ = 35,
    TT_ATL_I2C3_IRQ = 36,
    TT_ATL_I2C4_IRQ = 37,
    TT_ATL_UART1_IRQ = 39,
};

enum {
    TT_ATL_ACLINT,
    TT_ATL_BOOTROM,
    TT_ATL_DDR_LO,
    TT_ATL_DDR_HI,
    TT_ATL_I2C0,
    TT_ATL_I2C1,
    TT_ATL_I2C2,
    TT_ATL_I2C3,
    TT_ATL_I2C4,
    TT_ATL_MAPLIC,
    TT_ATL_MIMSIC,
    TT_ATL_SAPLIC,
    TT_ATL_SIMSIC,
    TT_ATL_UART1,
    TT_ATL_PRCM_RCPU,
    TT_ATL_PRCM_HSIO,
    TT_ATL_PRCM_PCIE,
    TT_ATL_PRCM_MM,
};

/*
 * RCPU PRCM Clock IDs, sourced from linux dt-bindings
 * include/dt-bindings/clock/tenstorrent,atlantis-prcm-rcpu.h
 */
enum {
    TT_ATL_CLK_RCPU_PLL = 0,
    TT_ATL_CLK_RCPU_ROOT = 1,
    TT_ATL_CLK_NOC_PLL = 25,
    TT_ATL_CLK_NOCC_CLK = 26,
    TT_ATL_CLK_I2C0_PCLK = 33,
    TT_ATL_CLK_I2C1_PCLK = 34,
    TT_ATL_CLK_I2C2_PCLK = 35,
    TT_ATL_CLK_I2C3_PCLK = 36,
    TT_ATL_CLK_I2C4_PCLK = 37,
    TT_ATL_CLK_UART1_PCLK = 39,
    TT_ATL_CLK_HSIO_PLL = 54,
    TT_ATL_CLK_PCIE_PLL = 55,
    TT_ATL_CLK_MM_PLL0 = 56,
    TT_ATL_CLK_MM_PLL1 = 57,
};

#endif
