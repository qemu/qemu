/*
 * QEMU NS SONIC DP8393x netcard
 *
 * Copyright (c) 2008-2009 Herve Poussineau
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_NET_DP8393X_H
#define HW_NET_DP8393X_H

#include "hw/sysbus.h"
#include "net/net.h"
#include "exec/memory.h"

#define SONIC_REG_COUNT  0x40

#define TYPE_DP8393X "dp8393x"
OBJECT_DECLARE_SIMPLE_TYPE(dp8393xState, DP8393X)

struct dp8393xState {
    SysBusDevice parent_obj;

    /* Hardware */
    uint8_t it_shift;
    bool big_endian;
    bool last_rba_is_full;
    qemu_irq irq;
    int irq_level;
    QEMUTimer *watchdog;
    int64_t wt_last_update;
    NICConf conf;
    NICState *nic;
    MemoryRegion mmio;

    /* Registers */
    uint16_t cam[16][3];
    uint16_t regs[SONIC_REG_COUNT];

    /* Temporaries */
    uint8_t tx_buffer[0x10000];
    int loopback_packet;

    /* Memory access */
    MemoryRegion *dma_mr;
    AddressSpace as;
};

#endif
