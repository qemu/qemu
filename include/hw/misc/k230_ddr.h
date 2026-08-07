/*
 * Kendryte K230 DDR controller and PHY models
 *
 * Device state and type declarations for the K230 DDRC CFG and K230 DDR PHY
 * registers.
 *
 * Copyright (c) 2026 Junze Cao <caojunze424@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_K230_DDR_H
#define HW_MISC_K230_DDR_H

#include "hw/core/sysbus.h"

#define K230_DDRC_MMIO_SIZE    0x02000000
#define K230_DDR_PHY_MMIO_SIZE 0x00400000

#define K230_DDRC_REG_SIZE  0x328
#define K230_DDRC_REG_COUNT (K230_DDRC_REG_SIZE / sizeof(uint32_t))

#define K230_DDR_PHY_ANIB_COUNT             10
#define K230_DDR_PHY_DBYTE_COUNT             4
#define K230_DDR_PHY_NIBBLES_PER_DBYTE       2

#define TYPE_K230_DDR_CFG "riscv.k230.ddr-cfg"
OBJECT_DECLARE_SIMPLE_TYPE(K230DDRCfgState, K230_DDR_CFG)

#define TYPE_K230_DDR_PHY "riscv.k230.ddr-phy"
OBJECT_DECLARE_SIMPLE_TYPE(K230DDRPhyState, K230_DDR_PHY)

struct K230DDRCfgState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    K230DDRPhyState *phy;
    uint32_t regs[K230_DDRC_REG_COUNT];
    bool initialized;
};

struct K230DDRPhyState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint16_t atx_impedance[K230_DDR_PHY_ANIB_COUNT];
    uint16_t tx_impedance_ctrl1[K230_DDR_PHY_DBYTE_COUNT]
                                [K230_DDR_PHY_NIBBLES_PER_DBYTE];
    uint16_t tx_odt_drv_stren[K230_DDR_PHY_DBYTE_COUNT]
                             [K230_DDR_PHY_NIBBLES_PER_DBYTE];
    uint16_t dfi_init_complete;
    uint16_t vref_in_global;
    uint16_t micro_cont_mux_sel;
    bool training_trigger_seen;
    bool training_complete;
    bool mailbox_message_pending;
};

#endif
