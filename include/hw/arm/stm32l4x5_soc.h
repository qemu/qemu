/*
 * STM32L4x5 SoC family
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * This work is heavily inspired by the stm32f405_soc by Alistair Francis.
 * Original code is licensed under the MIT License:
 *
 * Copyright (c) 2014 Alistair Francis <alistair@alistair23.me>
 */

/*
 * The reference used is the STMicroElectronics RM0351 Reference manual
 * for STM32L4x5 and STM32L4x6 advanced Arm ® -based 32-bit MCUs.
 * https://www.st.com/en/microcontrollers-microprocessors/stm32l4x5/documentation.html
 */

#ifndef HW_ARM_STM32L4x5_SOC_H
#define HW_ARM_STM32L4x5_SOC_H

#include "system/memory.h"
#include "hw/arm/armv7m.h"
#include "hw/or-irq.h"
#include "hw/misc/stm32l4x5_syscfg.h"
#include "hw/misc/stm32l4x5_exti.h"
#include "hw/misc/stm32l4x5_rcc.h"
#include "hw/gpio/stm32l4x5_gpio.h"
#include "hw/char/stm32l4x5_usart.h"
#include "qom/object.h"

#define TYPE_STM32L4X5_SOC "stm32l4x5-soc"
#define TYPE_STM32L4X5XC_SOC "stm32l4x5xc-soc"
#define TYPE_STM32L4X5XE_SOC "stm32l4x5xe-soc"
#define TYPE_STM32L4X5XG_SOC "stm32l4x5xg-soc"
OBJECT_DECLARE_TYPE(Stm32l4x5SocState, Stm32l4x5SocClass, STM32L4X5_SOC)

#define NUM_EXTI_OR_GATES 4

#define STM_NUM_USARTS 3
#define STM_NUM_UARTS 2

struct Stm32l4x5SocState {
    SysBusDevice parent_obj;

    ARMv7MState armv7m;

    Stm32l4x5ExtiState exti;
    OrIRQState exti_or_gates[NUM_EXTI_OR_GATES];
    Stm32l4x5SyscfgState syscfg;
    Stm32l4x5RccState rcc;
    Stm32l4x5GpioState gpio[NUM_GPIOS];
    Stm32l4x5UsartBaseState usart[STM_NUM_USARTS];
    Stm32l4x5UsartBaseState uart[STM_NUM_UARTS];
    Stm32l4x5UsartBaseState lpuart;

    MemoryRegion sram1;
    MemoryRegion sram2;
    MemoryRegion flash;
    MemoryRegion flash_alias;
};

struct Stm32l4x5SocClass {
    SysBusDeviceClass parent_class;

    size_t flash_size;
};

#endif
