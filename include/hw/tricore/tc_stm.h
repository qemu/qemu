/*
 * Infineon TriCore STM (System Timer Module) device model
 *
 * Copyright (c) 2024
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_TRICORE_TC_STM_H
#define HW_TRICORE_TC_STM_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_TC_STM "tc-stm"
OBJECT_DECLARE_SIMPLE_TYPE(TcStmState, TC_STM)

/* STM Register offsets */
#define STM_CLC         0x00    /* Clock Control Register */
#define STM_ID          0x08    /* Module Identification Register */
#define STM_TIM0        0x10    /* Timer Register 0 (bits 31:0) */
#define STM_TIM1        0x14    /* Timer Register 1 (bits 35:4) */
#define STM_TIM2        0x18    /* Timer Register 2 (bits 39:8) */
#define STM_TIM3        0x1C    /* Timer Register 3 (bits 47:16) */
#define STM_TIM4        0x20    /* Timer Register 4 (bits 51:20) */
#define STM_TIM5        0x24    /* Timer Register 5 (bits 55:24) */
#define STM_TIM6        0x28    /* Timer Register 6 (bits 63:32) */
#define STM_CAP         0x2C    /* Capture Register */
#define STM_CMP0        0x30    /* Compare Register 0 */
#define STM_CMP1        0x34    /* Compare Register 1 */
#define STM_CMCON       0x38    /* Compare Match Control Register */
#define STM_ICR         0x3C    /* Interrupt Control Register */
#define STM_ISCR        0x40    /* Interrupt Set/Clear Register */
#define STM_OCS         0xE8    /* OCDS Control and Status Register */
#define STM_KRSTCLR     0xEC    /* Kernel Reset Status Clear Register */
#define STM_KRST1       0xF0    /* Kernel Reset Register 1 */
#define STM_KRST0       0xF4    /* Kernel Reset Register 0 */
#define STM_ACCEN1      0xF8    /* Access Enable Register 1 */
#define STM_ACCEN0      0xFC    /* Access Enable Register 0 */

/* STM_ICR bits */
#define STM_ICR_CMP0EN  (1 << 0)    /* Compare 0 Interrupt Enable */
#define STM_ICR_CMP0IR  (1 << 1)    /* Compare 0 Interrupt Request */
#define STM_ICR_CMP0OS  (1 << 2)    /* Compare 0 Output Selection */
#define STM_ICR_CMP1EN  (1 << 4)    /* Compare 1 Interrupt Enable */
#define STM_ICR_CMP1IR  (1 << 5)    /* Compare 1 Interrupt Request */
#define STM_ICR_CMP1OS  (1 << 6)    /* Compare 1 Output Selection */

/* STM_CMCON bits */
#define STM_CMCON_MSIZE0_MASK   0x1F        /* Compare 0 Size */
#define STM_CMCON_MSTART0_MASK  0x1F00      /* Compare 0 Start Bit */
#define STM_CMCON_MSTART0_SHIFT 8
#define STM_CMCON_MSIZE1_MASK   0x1F0000    /* Compare 1 Size */
#define STM_CMCON_MSIZE1_SHIFT  16
#define STM_CMCON_MSTART1_MASK  0x1F000000  /* Compare 1 Start Bit */
#define STM_CMCON_MSTART1_SHIFT 24

/* STM_ISCR bits */
#define STM_ISCR_CMP0IRR    (1 << 0)    /* Compare 0 Interrupt Reset */
#define STM_ISCR_CMP0IRS    (1 << 1)    /* Compare 0 Interrupt Set */
#define STM_ISCR_CMP1IRR    (1 << 2)    /* Compare 1 Interrupt Reset */
#define STM_ISCR_CMP1IRS    (1 << 3)    /* Compare 1 Interrupt Set */

struct TcStmState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    /* 64-bit free-running counter */
    uint64_t counter;

    /* Capture register (high part of counter when TIM0-5 is read) */
    uint32_t cap;

    /* Compare registers */
    uint32_t cmp0;
    uint32_t cmp1;

    /* Control registers */
    uint32_t clc;
    uint32_t cmcon;
    uint32_t icr;
    uint32_t ocs;
    uint32_t accen0;
    uint32_t accen1;

    /* Timer for periodic updates */
    QEMUTimer *timer;

    /* Clock frequency in Hz (typically 100 MHz for TC3xx) */
    uint32_t freq_hz;

    /* IRQ outputs for compare match interrupts */
    qemu_irq irq_cmp0;
    qemu_irq irq_cmp1;
};

#endif /* HW_TRICORE_TC_STM_H */
