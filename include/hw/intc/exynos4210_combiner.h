/*
 * Samsung exynos4210 Interrupt Combiner
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd.
 * All rights reserved.
 *
 * Evgeny Voevodin <e.voevodin@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_INTC_EXYNOS4210_COMBINER_H
#define HW_INTC_EXYNOS4210_COMBINER_H

#include "hw/sysbus.h"

/*
 * State for each output signal of internal combiner
 */
typedef struct CombinerGroupState {
    uint8_t src_mask;            /* 1 - source enabled, 0 - disabled */
    uint8_t src_pending;        /* Pending source interrupts before masking */
} CombinerGroupState;

#define TYPE_EXYNOS4210_COMBINER "exynos4210.combiner"
OBJECT_DECLARE_SIMPLE_TYPE(Exynos4210CombinerState, EXYNOS4210_COMBINER)

/* Number of groups and total number of interrupts for the internal combiner */
#define IIC_NGRP 64
#define IIC_NIRQ (IIC_NGRP * 8)
#define IIC_REGSET_SIZE 0x41

struct Exynos4210CombinerState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    struct CombinerGroupState group[IIC_NGRP];
    uint32_t reg_set[IIC_REGSET_SIZE];
    uint32_t icipsr[2];
    uint32_t external;          /* 1 means that this combiner is external */

    qemu_irq output_irq[IIC_NGRP];
};

#endif
