/*
 * Copyright (c) 2017, Impinj, Inc.
 *
 * i.MX2 Watchdog IP block
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef IMX2_WDT_H
#define IMX2_WDT_H

#include "qemu/bitops.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/ptimer.h"

#define TYPE_IMX2_WDT "imx2.wdt"
#define IMX2_WDT(obj) OBJECT_CHECK(IMX2WdtState, (obj), TYPE_IMX2_WDT)

enum IMX2WdtRegisters {
    IMX2_WDT_WCR  = 0x0000, /* Control Register */
    IMX2_WDT_WSR  = 0x0002, /* Service Register */
    IMX2_WDT_WRSR = 0x0004, /* Reset Status Register */
    IMX2_WDT_WICR = 0x0006, /* Interrupt Control Register */
    IMX2_WDT_WMCR = 0x0008, /* Misc Register */
};

#define IMX2_WDT_MMIO_SIZE 0x000a

/* Control Register definitions */
#define IMX2_WDT_WCR_WT         (0xFF << 8) /* Watchdog Timeout Field */
#define IMX2_WDT_WCR_WDW        BIT(7)      /* WDOG Disable for Wait */
#define IMX2_WDT_WCR_WDA        BIT(5)      /* WDOG Assertion */
#define IMX2_WDT_WCR_SRS        BIT(4)      /* Software Reset Signal */
#define IMX2_WDT_WCR_WDT        BIT(3)      /* WDOG Timeout Assertion */
#define IMX2_WDT_WCR_WDE        BIT(2)      /* Watchdog Enable */
#define IMX2_WDT_WCR_WDBG       BIT(1)      /* Watchdog Debug Enable */
#define IMX2_WDT_WCR_WDZST      BIT(0)      /* Watchdog Timer Suspend */

#define IMX2_WDT_WCR_LOCK_MASK  (IMX2_WDT_WCR_WDZST | IMX2_WDT_WCR_WDBG \
                                 | IMX2_WDT_WCR_WDW)

/* Service Register definitions */
#define IMX2_WDT_SEQ1           0x5555      /* service sequence 1 */
#define IMX2_WDT_SEQ2           0xAAAA      /* service sequence 2 */

/* Reset Status Register definitions */
#define IMX2_WDT_WRSR_TOUT      BIT(1)      /* Reset due to Timeout */
#define IMX2_WDT_WRSR_SFTW      BIT(0)      /* Reset due to software reset */

/* Interrupt Control Register definitions */
#define IMX2_WDT_WICR_WIE       BIT(15)     /* Interrupt Enable */
#define IMX2_WDT_WICR_WTIS      BIT(14)     /* Interrupt Status */
#define IMX2_WDT_WICR_WICT      0xff        /* Interrupt Timeout */
#define IMX2_WDT_WICR_WICT_DEF  0x04        /* Default interrupt timeout (2s) */

#define IMX2_WDT_WICR_LOCK_MASK (IMX2_WDT_WICR_WIE | IMX2_WDT_WICR_WICT)

/* Misc Control Register definitions */
#define IMX2_WDT_WMCR_PDE       BIT(0)      /* Power-Down Enable */

typedef struct IMX2WdtState {
    /* <private> */
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    qemu_irq irq;

    struct ptimer_state *timer;
    struct ptimer_state *itimer;

    bool pretimeout_support;
    bool wicr_locked;

    uint16_t wcr;
    uint16_t wsr;
    uint16_t wrsr;
    uint16_t wicr;
    uint16_t wmcr;

    bool wcr_locked;            /* affects WDZST, WDBG, and WDW */
    bool wcr_wde_locked;        /* affects WDE */
    bool wcr_wdt_locked;        /* affects WDT (never cleared) */
} IMX2WdtState;

#endif /* IMX2_WDT_H */
