/*
 * BCM2838 Gigabit Ethernet emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BCM2838_GENET_H
#define BCM2838_GENET_H

#include "net/net.h"
#include "hw/core/sysbus.h"

#define TYPE_BCM2838_GENET "bcm2838-genet"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2838GenetState, BCM2838_GENET)

#define BCM2838_GENET_REV_MAJOR         6
#define BCM2838_GENET_REV_MINOR         0

typedef struct {
    uint32_t rev_ctrl;
    uint32_t port_ctrl;
    uint32_t rbuf_flush_ctrl;
    uint32_t tbuf_flush_ctrl;
    uint32_t reserved_0x10[12];
} BCM2838GenetRegsSys;

typedef struct {
    uint32_t reserved_0x0[16];
} BCM2838GenetRegsGrBridge;

typedef struct {
    uint32_t pwr_mgmt;
    uint32_t reserved_0x4[2];
    uint32_t rgmii_oob_ctrl;
    uint32_t reserved_0x10[3];
    uint32_t gphy_ctrl;
    uint32_t reserved_0x20[24];
} BCM2838GenetRegsExt;

typedef struct {
    uint32_t stat;
    uint32_t set;
    uint32_t clear;
    uint32_t mask_status;
    uint32_t mask_set;
    uint32_t mask_clear;
    uint32_t reserved_0x18[10];
} BCM2838GenetRegsIntrl0;

typedef struct {
    uint32_t stat;
    uint32_t set;
    uint32_t clear;
    uint32_t mask_status;
    uint32_t mask_set;
    uint32_t mask_clear;
    uint32_t reserved_0x18[10];
} BCM2838GenetRegsIntrl1;

typedef struct {
    uint32_t ctrl;
    uint32_t reserved_0x4[2];
    uint32_t status;
    uint32_t reserved_0x10;
    uint32_t chk_ctrl;
    uint32_t reserved_0x18[31];
    uint32_t ovfl_cnt;
    uint32_t err_cnt;
    uint32_t energy_ctrl;
    uint32_t reserved_0xA0[5];
    uint32_t size_ctrl;
    uint32_t reserved_0xB8[18];
} BCM2838GenetRegsRbuf;

typedef struct {
    uint32_t ctrl;
    uint32_t reserved_0x4[2];
    uint32_t bp_mc;
    uint32_t reserved_0x10;
    uint32_t energy_ctrl;
    uint32_t reserved_0x18[58];
} BCM2838GenetRegsTbuf;

typedef struct {
    BCM2838GenetRegsSys sys;
    BCM2838GenetRegsGrBridge gr_bridge;
    BCM2838GenetRegsExt ext;
    uint32_t reserved_0x100[64];
    BCM2838GenetRegsIntrl0 intrl0;
    BCM2838GenetRegsIntrl1 intrl1;
    uint32_t reserved_0x280[32];
    BCM2838GenetRegsRbuf rbuf;
    uint32_t reserved_0x400[128];
    BCM2838GenetRegsTbuf tbuf;
    uint32_t reserved_0x700[64];
} BCM2838GenetRegs;

struct BCM2838GenetState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/

    MemoryRegion regs_mr;
    AddressSpace dma_as;

    BCM2838GenetRegs regs;

    qemu_irq irq_default;
    qemu_irq irq_prio;
};

#endif /* BCM2838_GENET_H */
