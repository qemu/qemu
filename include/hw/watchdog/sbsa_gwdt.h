/*
 * Copyright (c) 2020 Linaro Limited
 *
 * Authors:
 *  Shashi Mallela <shashi.mallela@linaro.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#ifndef WDT_SBSA_GWDT_H
#define WDT_SBSA_GWDT_H

#include "qemu/bitops.h"
#include "hw/sysbus.h"
#include "hw/irq.h"

#define TYPE_WDT_SBSA "sbsa_gwdt"
#define SBSA_GWDT(obj) \
    OBJECT_CHECK(SBSA_GWDTState, (obj), TYPE_WDT_SBSA)
#define SBSA_GWDT_CLASS(klass) \
    OBJECT_CLASS_CHECK(SBSA_GWDTClass, (klass), TYPE_WDT_SBSA)
#define SBSA_GWDT_GET_CLASS(obj) \
    OBJECT_GET_CLASS(SBSA_GWDTClass, (obj), TYPE_WDT_SBSA)

/* SBSA Generic Watchdog register definitions */
/* refresh frame */
#define SBSA_GWDT_WRR       0x000

/* control frame */
#define SBSA_GWDT_WCS       0x000
#define SBSA_GWDT_WOR       0x008
#define SBSA_GWDT_WORU      0x00C
#define SBSA_GWDT_WCV       0x010
#define SBSA_GWDT_WCVU      0x014

/* Watchdog Interface Identification Register */
#define SBSA_GWDT_W_IIDR    0xFCC

/* Watchdog Control and Status Register Bits */
#define SBSA_GWDT_WCS_EN    BIT(0)
#define SBSA_GWDT_WCS_WS0   BIT(1)
#define SBSA_GWDT_WCS_WS1   BIT(2)

#define SBSA_GWDT_WOR_MASK  0x0000FFFF

/*
 * Watchdog Interface Identification Register definition
 * considering JEP106 code for ARM in Bits [11:0]
 */
#define SBSA_GWDT_ID        0x1043B

/* 2 Separate memory regions for each of refresh & control register frames */
#define SBSA_GWDT_RMMIO_SIZE 0x1000
#define SBSA_GWDT_CMMIO_SIZE 0x1000

#define SBSA_TIMER_FREQ      62500000 /* Hz */

typedef struct SBSA_GWDTState {
    /* <private> */
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion rmmio;
    MemoryRegion cmmio;
    qemu_irq irq;

    QEMUTimer *timer;

    uint32_t id;
    uint32_t wcs;
    uint32_t worl;
    uint32_t woru;
    uint32_t wcvl;
    uint32_t wcvu;
} SBSA_GWDTState;

#endif /* WDT_SBSA_GWDT_H */
