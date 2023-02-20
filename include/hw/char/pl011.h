/*
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

#ifndef HW_PL011_H
#define HW_PL011_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define TYPE_PL011 "pl011"
OBJECT_DECLARE_SIMPLE_TYPE(PL011State, PL011)

/* This shares the same struct (and cast macro) as the base pl011 device */
#define TYPE_PL011_LUMINARY "pl011_luminary"

/* Depth of UART FIFO in bytes, when FIFO mode is enabled (else depth == 1) */
#define PL011_FIFO_DEPTH 16

struct PL011State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t readbuff;
    uint32_t flags;
    uint32_t lcr;
    uint32_t rsr;
    uint32_t cr;
    uint32_t dmacr;
    uint32_t int_enabled;
    uint32_t int_level;
    uint32_t read_fifo[PL011_FIFO_DEPTH];
    uint32_t ilpr;
    uint32_t ibrd;
    uint32_t fbrd;
    uint32_t ifl;
    int read_pos;
    int read_count;
    int read_trigger;
    CharBackend chr;
    qemu_irq irq[6];
    Clock *clk;
    bool migrate_clk;
    const unsigned char *id;
};

DeviceState *pl011_create(hwaddr addr, qemu_irq irq, Chardev *chr);

#endif
