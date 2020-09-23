/*
 * nRF51 SoC UART emulation
 *
 * Copyright (c) 2018 Julia Suvorova <jusual@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef NRF51_UART_H
#define NRF51_UART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "hw/registerfields.h"
#include "qom/object.h"

#define UART_FIFO_LENGTH 6
#define UART_SIZE 0x1000

#define TYPE_NRF51_UART "nrf51_soc.uart"
OBJECT_DECLARE_SIMPLE_TYPE(NRF51UARTState, NRF51_UART)

REG32(UART_STARTRX, 0x000)
REG32(UART_STOPRX, 0x004)
REG32(UART_STARTTX, 0x008)
REG32(UART_STOPTX, 0x00C)
REG32(UART_SUSPEND, 0x01C)

REG32(UART_CTS, 0x100)
REG32(UART_NCTS, 0x104)
REG32(UART_RXDRDY, 0x108)
REG32(UART_TXDRDY, 0x11C)
REG32(UART_ERROR, 0x124)
REG32(UART_RXTO, 0x144)

REG32(UART_INTEN, 0x300)
    FIELD(UART_INTEN, CTS, 0, 1)
    FIELD(UART_INTEN, NCTS, 1, 1)
    FIELD(UART_INTEN, RXDRDY, 2, 1)
    FIELD(UART_INTEN, TXDRDY, 7, 1)
    FIELD(UART_INTEN, ERROR, 9, 1)
    FIELD(UART_INTEN, RXTO, 17, 1)
REG32(UART_INTENSET, 0x304)
REG32(UART_INTENCLR, 0x308)
REG32(UART_ERRORSRC, 0x480)
REG32(UART_ENABLE, 0x500)
REG32(UART_PSELRTS, 0x508)
REG32(UART_PSELTXD, 0x50C)
REG32(UART_PSELCTS, 0x510)
REG32(UART_PSELRXD, 0x514)
REG32(UART_RXD, 0x518)
REG32(UART_TXD, 0x51C)
REG32(UART_BAUDRATE, 0x524)
REG32(UART_CONFIG, 0x56C)

struct NRF51UARTState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    CharBackend chr;
    qemu_irq irq;
    guint watch_tag;

    uint8_t rx_fifo[UART_FIFO_LENGTH];
    unsigned int rx_fifo_pos;
    unsigned int rx_fifo_len;

    uint32_t reg[0x56C];

    bool rx_started;
    bool tx_started;
    bool pending_tx_byte;
    bool enabled;
};

#endif
