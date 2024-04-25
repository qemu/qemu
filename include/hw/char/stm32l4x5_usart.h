/*
 * STM32L4X5 USART (Universal Synchronous Asynchronous Receiver Transmitter)
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * The STM32L4X5 USART is heavily inspired by the stm32f2xx_usart
 * by Alistair Francis.
 * The reference used is the STMicroElectronics RM0351 Reference manual
 * for STM32L4x5 and STM32L4x6 advanced Arm ® -based 32-bit MCUs.
 */

#ifndef HW_STM32L4X5_USART_H
#define HW_STM32L4X5_USART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define TYPE_STM32L4X5_USART_BASE "stm32l4x5-usart-base"
#define TYPE_STM32L4X5_USART "stm32l4x5-usart"
#define TYPE_STM32L4X5_UART "stm32l4x5-uart"
#define TYPE_STM32L4X5_LPUART "stm32l4x5-lpuart"
OBJECT_DECLARE_TYPE(Stm32l4x5UsartBaseState, Stm32l4x5UsartBaseClass,
                    STM32L4X5_USART_BASE)

typedef enum {
    STM32L4x5_USART,
    STM32L4x5_UART,
    STM32L4x5_LPUART,
} Stm32l4x5UsartType;

struct Stm32l4x5UsartBaseState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t cr1;
    uint32_t cr2;
    uint32_t cr3;
    uint32_t brr;
    uint32_t gtpr;
    uint32_t rtor;
    /* rqr is write-only */
    uint32_t isr;
    /* icr is a clear register */
    uint32_t rdr;
    uint32_t tdr;

    Clock *clk;
    CharBackend chr;
    qemu_irq irq;
    guint watch_tag;
};

struct Stm32l4x5UsartBaseClass {
    SysBusDeviceClass parent_class;

    Stm32l4x5UsartType type;
};

#endif /* HW_STM32L4X5_USART_H */
