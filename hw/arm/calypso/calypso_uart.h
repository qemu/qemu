/*
 * calypso_uart.h — Calypso UART QOM device
 *
 * TI Calypso 16550-like UART with 64-byte FIFO, DLAB routing,
 * enhanced mode (EFR), and level-sensitive IRQ.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_CHAR_CALYPSO_UART_H
#define HW_CHAR_CALYPSO_UART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define TYPE_CALYPSO_UART "calypso-uart"
OBJECT_DECLARE_SIMPLE_TYPE(CalypsoUARTState, CALYPSO_UART)

#define CALYPSO_UART_FIFO_SIZE  64  /* Hardware FIFO depth */

struct CalypsoUARTState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    CharBackend  chr;          /* QOM property: chardev backend */
    qemu_irq     irq;

    /* Identification (for debug logs) */
    char *name;

    /* RX FIFO (circular buffer) */
    uint8_t rx_fifo[CALYPSO_UART_FIFO_SIZE];
    int     rx_head;
    int     rx_tail;
    int     rx_count;

    /* Standard UART registers */
    uint8_t ier;       /* Interrupt Enable Register          */
    uint8_t lcr;       /* Line Control Register              */
    uint8_t mcr;       /* Modem Control Register             */
    uint8_t msr;       /* Modem Status Register              */
    uint8_t scr;       /* Scratch Pad Register (SPR)         */
    uint8_t mdr1;      /* Mode Definition Register 1         */
    uint8_t dll;       /* Divisor Latch Low                  */
    uint8_t dlh;       /* Divisor Latch High                 */
    uint8_t efr;       /* Enhanced Feature Register          */
    uint8_t tlr;       /* Trigger Level Register             */
    uint8_t fcr;       /* FIFO Control (write-only shadow)   */
    uint8_t xon1, xon2;
    uint8_t xoff1, xoff2;

    /* IRQ state tracking */
    bool    thri_pending;  /* THR empty interrupt pending */
    bool    irq_raised;
};

#endif /* HW_CHAR_CALYPSO_UART_H */
