/*
 * QEMU SiFive PRCI (Power, Reset, Clock, Interrupt) interface
 *
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

#ifndef HW_SIFIVE_PRCI_H
#define HW_SIFIVE_PRCI_H

#define TYPE_SIFIVE_PRCI "riscv.sifive.prci"

#define SIFIVE_PRCI(obj) \
    OBJECT_CHECK(SiFivePRCIState, (obj), TYPE_SIFIVE_PRCI)

typedef struct SiFivePRCIState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
} SiFivePRCIState;

DeviceState *sifive_prci_create(hwaddr addr);

#endif
