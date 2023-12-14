/*
 * SiFive HiFive1 AON (Always On Domain) interface.
 *
 * Copyright (c) 2022 SiFive, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_SIFIVE_AON_H
#define HW_SIFIVE_AON_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_SIFIVE_E_AON "riscv.sifive.e.aon"
OBJECT_DECLARE_SIMPLE_TYPE(SiFiveEAONState, SIFIVE_E_AON)

#define SIFIVE_E_AON_WDOGKEY (0x51F15E)
#define SIFIVE_E_AON_WDOGFEED (0xD09F00D)
#define SIFIVE_E_LFCLK_DEFAULT_FREQ (32768)

enum {
    SIFIVE_E_AON_WDT    = 0x0,
    SIFIVE_E_AON_RTC    = 0x40,
    SIFIVE_E_AON_LFROSC = 0x70,
    SIFIVE_E_AON_BACKUP = 0x80,
    SIFIVE_E_AON_PMU    = 0x100,
    SIFIVE_E_AON_MAX    = 0x150
};

struct SiFiveEAONState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;

    /*< watchdog timer >*/
    QEMUTimer *wdog_timer;
    qemu_irq wdog_irq;
    uint64_t wdog_restart_time;
    uint64_t wdogclk_freq;

    uint32_t wdogcfg;
    uint16_t wdogcmp0;
    uint32_t wdogcount;
    uint8_t wdogunlock;
};

#endif
