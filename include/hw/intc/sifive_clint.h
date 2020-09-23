/*
 * SiFive CLINT (Core Local Interruptor) interface
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017 SiFive, Inc.
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

#ifndef HW_SIFIVE_CLINT_H
#define HW_SIFIVE_CLINT_H

#include "hw/sysbus.h"

#define TYPE_SIFIVE_CLINT "riscv.sifive.clint"

#define SIFIVE_CLINT(obj) \
    OBJECT_CHECK(SiFiveCLINTState, (obj), TYPE_SIFIVE_CLINT)

typedef struct SiFiveCLINTState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    uint32_t hartid_base;
    uint32_t num_harts;
    uint32_t sip_base;
    uint32_t timecmp_base;
    uint32_t time_base;
    uint32_t aperture_size;
    uint32_t timebase_freq;
} SiFiveCLINTState;

DeviceState *sifive_clint_create(hwaddr addr, hwaddr size,
    uint32_t hartid_base, uint32_t num_harts, uint32_t sip_base,
    uint32_t timecmp_base, uint32_t time_base, uint32_t timebase_freq,
    bool provide_rdtime);

enum {
    SIFIVE_SIP_BASE     = 0x0,
    SIFIVE_TIMECMP_BASE = 0x4000,
    SIFIVE_TIME_BASE    = 0xBFF8
};

enum {
    SIFIVE_CLINT_TIMEBASE_FREQ = 10000000
};

#endif
