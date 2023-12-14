/*
 * QEMU model of ZynqMP APU Control.
 *
 * Copyright (c) 2013-2022 Xilinx Inc
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com> and
 * Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 */
#ifndef HW_MISC_XLNX_ZYNQMP_APU_CTRL_H
#define HW_MISC_XLNX_ZYNQMP_APU_CTRL_H

#include "hw/sysbus.h"
#include "hw/register.h"
#include "target/arm/cpu-qom.h"

#define TYPE_XLNX_ZYNQMP_APU_CTRL "xlnx.apu-ctrl"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxZynqMPAPUCtrl, XLNX_ZYNQMP_APU_CTRL)

REG32(APU_ERR_CTRL, 0x0)
    FIELD(APU_ERR_CTRL, PSLVERR, 0, 1)
REG32(ISR, 0x10)
    FIELD(ISR, INV_APB, 0, 1)
REG32(IMR, 0x14)
    FIELD(IMR, INV_APB, 0, 1)
REG32(IEN, 0x18)
    FIELD(IEN, INV_APB, 0, 1)
REG32(IDS, 0x1c)
    FIELD(IDS, INV_APB, 0, 1)
REG32(CONFIG_0, 0x20)
    FIELD(CONFIG_0, CFGTE, 24, 4)
    FIELD(CONFIG_0, CFGEND, 16, 4)
    FIELD(CONFIG_0, VINITHI, 8, 4)
    FIELD(CONFIG_0, AA64NAA32, 0, 4)
REG32(CONFIG_1, 0x24)
    FIELD(CONFIG_1, L2RSTDISABLE, 29, 1)
    FIELD(CONFIG_1, L1RSTDISABLE, 28, 1)
    FIELD(CONFIG_1, CP15DISABLE, 0, 4)
REG32(RVBARADDR0L, 0x40)
    FIELD(RVBARADDR0L, ADDR, 2, 30)
REG32(RVBARADDR0H, 0x44)
    FIELD(RVBARADDR0H, ADDR, 0, 8)
REG32(RVBARADDR1L, 0x48)
    FIELD(RVBARADDR1L, ADDR, 2, 30)
REG32(RVBARADDR1H, 0x4c)
    FIELD(RVBARADDR1H, ADDR, 0, 8)
REG32(RVBARADDR2L, 0x50)
    FIELD(RVBARADDR2L, ADDR, 2, 30)
REG32(RVBARADDR2H, 0x54)
    FIELD(RVBARADDR2H, ADDR, 0, 8)
REG32(RVBARADDR3L, 0x58)
    FIELD(RVBARADDR3L, ADDR, 2, 30)
REG32(RVBARADDR3H, 0x5c)
    FIELD(RVBARADDR3H, ADDR, 0, 8)
REG32(ACE_CTRL, 0x60)
    FIELD(ACE_CTRL, AWQOS, 16, 4)
    FIELD(ACE_CTRL, ARQOS, 0, 4)
REG32(SNOOP_CTRL, 0x80)
    FIELD(SNOOP_CTRL, ACE_INACT, 4, 1)
    FIELD(SNOOP_CTRL, ACP_INACT, 0, 1)
REG32(PWRCTL, 0x90)
    FIELD(PWRCTL, CLREXMONREQ, 17, 1)
    FIELD(PWRCTL, L2FLUSHREQ, 16, 1)
    FIELD(PWRCTL, CPUPWRDWNREQ, 0, 4)
REG32(PWRSTAT, 0x94)
    FIELD(PWRSTAT, CLREXMONACK, 17, 1)
    FIELD(PWRSTAT, L2FLUSHDONE, 16, 1)
    FIELD(PWRSTAT, DBGNOPWRDWN, 0, 4)

#define APU_R_MAX ((R_PWRSTAT) + 1)

#define APU_MAX_CPU    4

struct XlnxZynqMPAPUCtrl {
    SysBusDevice busdev;

    ARMCPU *cpus[APU_MAX_CPU];
    /* WFIs towards PMU. */
    qemu_irq wfi_out[4];
    /* CPU Power status towards INTC Redirect. */
    qemu_irq cpu_power_status[4];
    qemu_irq irq_imr;

    uint8_t cpu_pwrdwn_req;
    uint8_t cpu_in_wfi;

    RegisterInfoArray *reg_array;
    uint32_t regs[APU_R_MAX];
    RegisterInfo regs_info[APU_R_MAX];
};

#endif
