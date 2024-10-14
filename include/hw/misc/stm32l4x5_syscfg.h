/*
 * STM32L4x5 SYSCFG (System Configuration Controller)
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * This work is based on the stm32f4xx_syscfg by Alistair Francis.
 * Original code is licensed under the MIT License:
 *
 * Copyright (c) 2014 Alistair Francis <alistair@alistair23.me>
 */

/*
 * The reference used is the STMicroElectronics RM0351 Reference manual
 * for STM32L4x5 and STM32L4x6 advanced Arm ® -based 32-bit MCUs.
 * https://www.st.com/en/microcontrollers-microprocessors/stm32l4x5/documentation.html
 */

#ifndef HW_STM32L4X5_SYSCFG_H
#define HW_STM32L4X5_SYSCFG_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "hw/gpio/stm32l4x5_gpio.h"

#define TYPE_STM32L4X5_SYSCFG "stm32l4x5-syscfg"
OBJECT_DECLARE_SIMPLE_TYPE(Stm32l4x5SyscfgState, STM32L4X5_SYSCFG)

#define SYSCFG_NUM_EXTICR 4

struct Stm32l4x5SyscfgState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t memrmp;
    uint32_t cfgr1;
    uint32_t exticr[SYSCFG_NUM_EXTICR];
    uint32_t scsr;
    uint32_t cfgr2;
    uint32_t swpr;
    uint32_t skr;
    uint32_t swpr2;

    qemu_irq gpio_out[GPIO_NUM_PINS];
    Clock *clk;
};

#endif
