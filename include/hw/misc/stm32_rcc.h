/*
 * STM32 RCC (only reset and enable registers are implemented)
 *
 * Copyright (c) 2024 Román Cárdenas <rcardenas.rod@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef HW_STM32_RCC_H
#define HW_STM32_RCC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define STM32_RCC_CR 0x00
#define STM32_RCC_PLL_CFGR 0x04
#define STM32_RCC_CFGR 0x08
#define STM32_RCC_CIR 0x0C
#define STM32_RCC_AHB1_RSTR 0x10
#define STM32_RCC_AHB2_RSTR 0x14
#define STM32_RCC_AHB3_RSTR 0x18

#define STM32_RCC_APB1_RSTR 0x20
#define STM32_RCC_APB2_RSTR 0x24

#define STM32_RCC_AHB1_ENR 0x30
#define STM32_RCC_AHB2_ENR 0x34
#define STM32_RCC_AHB3_ENR 0x38

#define STM32_RCC_APB1_ENR 0x40
#define STM32_RCC_APB2_ENR 0x44

#define STM32_RCC_AHB1_LPENR 0x50
#define STM32_RCC_AHB2_LPENR 0x54
#define STM32_RCC_AHB3_LPENR 0x58

#define STM32_RCC_APB1_LPENR 0x60
#define STM32_RCC_APB2_LPENR 0x64

#define STM32_RCC_BDCR 0x70
#define STM32_RCC_CSR 0x74

#define STM32_RCC_SSCGR 0x80
#define STM32_RCC_PLLI2SCFGR 0x84
#define STM32_RCC_PLLSAI_CFGR 0x88
#define STM32_RCC_DCKCFGR 0x8C
#define STM32_RCC_CKGATENR 0x90
#define STM32_RCC_DCKCFGR2 0x94

#define STM32_RCC_NREGS ((STM32_RCC_DCKCFGR2 >> 2) + 1)
#define STM32_RCC_PERIPHERAL_SIZE 0x400
#define STM32_RCC_NIRQS (32 * 5) /* 32 bits per reg, 5 en/rst regs */

#define STM32_RCC_GPIO_IRQ_OFFSET 0

#define TYPE_STM32_RCC "stm32.rcc"

typedef struct STM32RccState STM32RccState;

DECLARE_INSTANCE_CHECKER(STM32RccState, STM32_RCC, TYPE_STM32_RCC)

#define NUM_GPIO_EVENT_IN_LINES 16

struct STM32RccState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t regs[STM32_RCC_NREGS];

    qemu_irq enable_irq[STM32_RCC_NIRQS];
    qemu_irq reset_irq[STM32_RCC_NIRQS];
};

#endif /* HW_STM32_RCC_H */
