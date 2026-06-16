/*
 * K230 Watchdog Timer
 *
 * K230 Technical Reference Manual V0.3.1 (2024-11-18):
 * https://github.com/revyos/external-docs/blob/master/K230/en-us/K230_Technical_Reference_Manual_V0.3.1_20241118.pdf
 *
 * Copyright (c) 2025 Mig Yang <temashking@foxmail.com>
 * Copyright (c) 2025 Chao Liu <chao.liu.zevorn@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef K230_WDT_H
#define K230_WDT_H

#include "qemu/bitops.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/ptimer.h"
#include "qom/object.h"

#define TYPE_K230_WDT "riscv.k230.wdt"
OBJECT_DECLARE_SIMPLE_TYPE(K230WdtState, K230_WDT)

#define K230_WDT_DEFAULT_FREQ (32768)

/* K230 Watchdog Register Map */
enum K230WdtRegisters {
    K230_WDT_CR      = 0x00,  /* Control Register */
    K230_WDT_TORR    = 0x04,  /* Timeout Range Register */
    K230_WDT_CCVR    = 0x08,  /* Current Counter Value Register */
    K230_WDT_CRR     = 0x0c,  /* Counter Restart Register */
    K230_WDT_STAT    = 0x10,  /* Interrupt Status Register */
    K230_WDT_EOI     = 0x14,  /* Interrupt Clear Register */
    K230_WDT_PROT_LEVEL = 0x1c, /* Protection Level Register */
    K230_WDT_COMP_PARAM_5 = 0xe4, /* Component Parameters Register 5 */
    K230_WDT_COMP_PARAM_4 = 0xe8, /* Component Parameters Register 4 */
    K230_WDT_COMP_PARAM_3 = 0xec, /* Component Parameters Register 3 */
    K230_WDT_COMP_PARAM_2 = 0xf0, /* Component Parameters Register 2 */
    K230_WDT_COMP_PARAM_1 = 0xf4, /* Component Parameters Register 1 */
    K230_WDT_COMP_VERSION = 0xf8, /* Component Version Register */
    K230_WDT_COMP_TYPE = 0xfc, /* Component Type Register */
};

#define K230_WDT_MMIO_SIZE 0x100

/* Control Register (WDT_CR) definitions */
#define K230_WDT_CR_RPL_MASK    0x7        /* Reset Pulse Length */
#define K230_WDT_CR_RPL_SHIFT   2
#define K230_WDT_CR_RMOD        BIT(1)     /* Response Mode */
#define K230_WDT_CR_WDT_EN      BIT(0)     /* Watchdog Enable */

/* Reset Pulse Length values */
#define K230_WDT_RPL_2_CYCLES   0x0
#define K230_WDT_RPL_4_CYCLES   0x1
#define K230_WDT_RPL_8_CYCLES   0x2
#define K230_WDT_RPL_16_CYCLES  0x3
#define K230_WDT_RPL_32_CYCLES  0x4
#define K230_WDT_RPL_64_CYCLES  0x5
#define K230_WDT_RPL_128_CYCLES 0x6
#define K230_WDT_RPL_256_CYCLES 0x7

/* Timeout Range Register (WDT_TORR) definitions */
#define K230_WDT_TORR_TOP_MASK  0xf        /* Timeout Period */

/* Interrupt Status Register (WDT_STAT) definitions */
#define K230_WDT_STAT_INT       BIT(0)     /* Interrupt Status */

/* Counter Restart Register (WDT_CRR) magic value */
#define K230_WDT_CRR_RESTART    0x76       /* Restart command */

/* Component Parameters Register 1 (WDT_COMP_PARAM_1) definitions */
#define K230_WDT_CNT_WIDTH_MASK 0x1f000000 /* Counter Width */
#define K230_WDT_CNT_WIDTH_SHIFT 24
#define K230_WDT_DFLT_TOP_INIT_MASK 0xf00000 /* Default Initial Timeout */
#define K230_WDT_DFLT_TOP_INIT_SHIFT 20
#define K230_WDT_DFLT_TOP_MASK  0xf0000    /* Default Timeout */
#define K230_WDT_DFLT_TOP_SHIFT 16
#define K230_WDT_DFLT_RPL_MASK  0x7        /* Default Reset Pulse Length */
#define K230_WDT_DFLT_RPL_SHIFT 10
#define K230_WDT_APB_DATA_WIDTH_MASK 0x3   /* APB Data Width */
#define K230_WDT_APB_DATA_WIDTH_SHIFT 8
#define K230_WDT_USE_FIX_TOP    BIT(6)     /* Use Fixed Timeout Values */
#define K230_WDT_HC_TOP         BIT(5)     /* Hard-coded Timeout */
#define K230_WDT_HC_RPL         BIT(4)     /* Hard-coded Reset Pulse Length */
#define K230_WDT_HC_RMOD        BIT(3)     /* Hard-coded Response Mode */
#define K230_WDT_DUAL_TOP       BIT(2)     /* Dual Timeout Period */
#define K230_WDT_DFLT_RMOD      BIT(1)     /* Default Response Mode */
#define K230_WDT_ALWAYS_EN      BIT(0)     /* Always Enabled */

/* Component Type Register value */
#define K230_WDT_COMP_TYPE_VAL  0x44570120

/* Component Version Register value */
#define K230_WDT_COMP_VERSION_VAL 0x3131302a  /* "110*" */

struct K230WdtState {
    /* <private> */
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    qemu_irq irq;

    struct ptimer_state *timer;

    /* Register state */
    uint32_t cr;           /* Control Register */
    uint32_t torr;         /* Timeout Range Register */
    uint32_t ccvr;         /* Current Counter Value Register */
    uint32_t stat;         /* Interrupt Status Register */
    uint32_t prot_level;   /* Protection Level Register */

    /* Internal state */
    bool interrupt_pending;
    bool enabled;
    uint32_t timeout_value;
    uint32_t current_count;
};

#endif /* K230_WDT_H */
