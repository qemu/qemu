/*
 * AVR USART
 *
 * Copyright (c) 2018 University of Kent
 * Author: Sarah Harris
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#ifndef HW_CHAR_AVR_USART_H
#define HW_CHAR_AVR_USART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

/* Offsets of registers. */
#define USART_DR   0x06
#define USART_CSRA  0x00
#define USART_CSRB  0x01
#define USART_CSRC  0x02
#define USART_BRRH 0x05
#define USART_BRRL 0x04

/* Relevant bits in registers. */
#define USART_CSRA_RXC    (1 << 7)
#define USART_CSRA_TXC    (1 << 6)
#define USART_CSRA_DRE    (1 << 5)
#define USART_CSRA_MPCM   (1 << 0)

#define USART_CSRB_RXCIE  (1 << 7)
#define USART_CSRB_TXCIE  (1 << 6)
#define USART_CSRB_DREIE  (1 << 5)
#define USART_CSRB_RXEN   (1 << 4)
#define USART_CSRB_TXEN   (1 << 3)
#define USART_CSRB_CSZ2   (1 << 2)
#define USART_CSRB_RXB8   (1 << 1)
#define USART_CSRB_TXB8   (1 << 0)

#define USART_CSRC_MSEL1  (1 << 7)
#define USART_CSRC_MSEL0  (1 << 6)
#define USART_CSRC_PM1    (1 << 5)
#define USART_CSRC_PM0    (1 << 4)
#define USART_CSRC_CSZ1   (1 << 2)
#define USART_CSRC_CSZ0   (1 << 1)

#define TYPE_AVR_USART "avr-usart"
OBJECT_DECLARE_SIMPLE_TYPE(AVRUsartState, AVR_USART)

struct AVRUsartState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;

    CharFrontend chr;

    bool enabled;

    uint8_t data;
    bool data_valid;
    uint8_t char_mask;
    /* Control and Status Registers */
    uint8_t csra;
    uint8_t csrb;
    uint8_t csrc;
    /* Baud Rate Registers (low/high byte) */
    uint8_t brrh;
    uint8_t brrl;

    /* Receive Complete */
    qemu_irq rxc_irq;
    /* Transmit Complete */
    qemu_irq txc_irq;
    /* Data Register Empty */
    qemu_irq dre_irq;
};

#endif /* HW_CHAR_AVR_USART_H */
