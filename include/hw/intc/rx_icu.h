/*
 * RX Interrupt Control Unit
 *
 * Copyright (c) 2019 Yoshinori Sato
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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

#ifndef HW_INTC_RX_ICU_H
#define HW_INTC_RX_ICU_H

#include "hw/sysbus.h"

enum TRG_MODE {
    TRG_LEVEL = 0,
    TRG_NEDGE = 1,      /* Falling */
    TRG_PEDGE = 2,      /* Raising */
    TRG_BEDGE = 3,      /* Both */
};

struct IRQSource {
    enum TRG_MODE sense;
    int level;
};

enum {
    /* Software interrupt request */
    SWI = 27,
    NR_IRQS = 256
};

struct RXICUState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion memory;
    struct IRQSource src[NR_IRQS];
    uint32_t nr_irqs;
    uint8_t *map;
    uint32_t nr_sense;
    uint8_t *init_sense;

    uint8_t ir[NR_IRQS];
    uint8_t dtcer[NR_IRQS];
    uint8_t ier[NR_IRQS / 8];
    uint8_t ipr[142];
    uint8_t dmasr[4];
    uint16_t fir;
    uint8_t nmisr;
    uint8_t nmier;
    uint8_t nmiclr;
    uint8_t nmicr;
    int16_t req_irq;
    qemu_irq _irq;
    qemu_irq _fir;
    qemu_irq _swi;
};
typedef struct RXICUState RXICUState;

#define TYPE_RX_ICU "rx-icu"
#define RX_ICU(obj) OBJECT_CHECK(RXICUState, (obj), TYPE_RX_ICU)

#endif /* RX_ICU_H */
