/*
 * QEMU SiFive E PRCI (Power, Reset, Clock, Interrupt) interface
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

#ifndef HW_SIFIVE_E_PRCI_H
#define HW_SIFIVE_E_PRCI_H

enum {
    SIFIVE_E_PRCI_HFROSCCFG = 0x0,
    SIFIVE_E_PRCI_HFXOSCCFG = 0x4,
    SIFIVE_E_PRCI_PLLCFG    = 0x8,
    SIFIVE_E_PRCI_PLLOUTDIV = 0xC
};

enum {
    SIFIVE_E_PRCI_HFROSCCFG_RDY = (1 << 31),
    SIFIVE_E_PRCI_HFROSCCFG_EN  = (1 << 30)
};

enum {
    SIFIVE_E_PRCI_HFXOSCCFG_RDY = (1 << 31),
    SIFIVE_E_PRCI_HFXOSCCFG_EN  = (1 << 30)
};

enum {
    SIFIVE_E_PRCI_PLLCFG_PLLSEL = (1 << 16),
    SIFIVE_E_PRCI_PLLCFG_REFSEL = (1 << 17),
    SIFIVE_E_PRCI_PLLCFG_BYPASS = (1 << 18),
    SIFIVE_E_PRCI_PLLCFG_LOCK   = (1 << 31)
};

enum {
    SIFIVE_E_PRCI_PLLOUTDIV_DIV1 = (1 << 8)
};

#define SIFIVE_E_PRCI_REG_SIZE  0x1000

#define TYPE_SIFIVE_E_PRCI      "riscv.sifive.e.prci"

#define SIFIVE_E_PRCI(obj) \
    OBJECT_CHECK(SiFiveEPRCIState, (obj), TYPE_SIFIVE_E_PRCI)

typedef struct SiFiveEPRCIState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    uint32_t hfrosccfg;
    uint32_t hfxosccfg;
    uint32_t pllcfg;
    uint32_t plloutdiv;
} SiFiveEPRCIState;

DeviceState *sifive_e_prci_create(hwaddr addr);

#endif
