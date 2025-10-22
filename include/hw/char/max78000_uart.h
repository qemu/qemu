/*
 * MAX78000 UART
 *
 * Copyright (c) 2025 Jackson Donaldson <jcksn@duck.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MAX78000_UART_H
#define HW_MAX78000_UART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qemu/fifo8.h"
#include "qom/object.h"

#define UART_CTRL       0x0
#define UART_STATUS     0x4
#define UART_INT_EN     0x8
#define UART_INT_FL     0xc
#define UART_CLKDIV     0x10
#define UART_OSR        0x14
#define UART_TXPEEK     0x18
#define UART_PNR        0x1c
#define UART_FIFO       0x20
#define UART_DMA        0x30
#define UART_WKEN       0x34
#define UART_WKFL       0x38

/* CTRL */
#define UART_CTF_DIS    (1 << 7)
#define UART_FLUSH_TX   (1 << 8)
#define UART_FLUSH_RX   (1 << 9)
#define UART_BCLKEN     (1 << 15)
#define UART_BCLKRDY    (1 << 19)

/* STATUS */
#define UART_RX_LVL     8
#define UART_TX_EM      (1 << 6)
#define UART_RX_FULL    (1 << 5)
#define UART_RX_EM      (1 << 4)

/* PNR (Pin Control Register) */
#define UART_CTS        1
#define UART_RTS        (1 << 1)

/* INT_EN / INT_FL */
#define UART_RX_THD     (1 << 4)
#define UART_TX_HE      (1 << 6)

#define UART_RXBUFLEN   0x100
#define TYPE_MAX78000_UART "max78000-uart"
OBJECT_DECLARE_SIMPLE_TYPE(Max78000UartState, MAX78000_UART)

struct Max78000UartState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t ctrl;
    uint32_t status;
    uint32_t int_en;
    uint32_t int_fl;
    uint32_t clkdiv;
    uint32_t osr;
    uint32_t txpeek;
    uint32_t pnr;
    uint32_t fifo;
    uint32_t dma;
    uint32_t wken;
    uint32_t wkfl;

    Fifo8 rx_fifo;

    CharFrontend chr;
    qemu_irq irq;
};
#endif /* HW_STM32F2XX_USART_H */
