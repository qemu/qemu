
/*
 * QEMU model of the Ibex SPI Controller
 * SPEC Reference: https://docs.opentitan.org/hw/ip/spi_host/doc/
 *
 * Copyright (C) 2022 Western Digital
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

#ifndef IBEX_SPI_HOST_H
#define IBEX_SPI_HOST_H

#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/fifo8.h"
#include "qom/object.h"
#include "qemu/timer.h"

#define TYPE_IBEX_SPI_HOST "ibex-spi"
#define IBEX_SPI_HOST(obj) \
    OBJECT_CHECK(IbexSPIHostState, (obj), TYPE_IBEX_SPI_HOST)

/* SPI Registers */
#define IBEX_SPI_HOST_INTR_STATE         (0x00 / 4)  /* rw1c */
#define IBEX_SPI_HOST_INTR_ENABLE        (0x04 / 4)  /* rw */
#define IBEX_SPI_HOST_INTR_TEST          (0x08 / 4)  /* wo */
#define IBEX_SPI_HOST_ALERT_TEST         (0x0c / 4)  /* wo */
#define IBEX_SPI_HOST_CONTROL            (0x10 / 4)  /* rw */
#define IBEX_SPI_HOST_STATUS             (0x14 / 4)  /* ro */
#define IBEX_SPI_HOST_CONFIGOPTS         (0x18 / 4)  /* rw */
#define IBEX_SPI_HOST_CSID               (0x1c / 4)  /* rw */
#define IBEX_SPI_HOST_COMMAND            (0x20 / 4)  /* wo */
/* RX/TX Modelled by FIFO */
#define IBEX_SPI_HOST_RXDATA             (0x24 / 4)
#define IBEX_SPI_HOST_TXDATA             (0x28 / 4)

#define IBEX_SPI_HOST_ERROR_ENABLE       (0x2c / 4)  /* rw */
#define IBEX_SPI_HOST_ERROR_STATUS       (0x30 / 4)  /* rw1c */
#define IBEX_SPI_HOST_EVENT_ENABLE       (0x34 / 4)  /* rw */

/* FIFO Len in Bytes */
#define IBEX_SPI_HOST_TXFIFO_LEN         288
#define IBEX_SPI_HOST_RXFIFO_LEN         256

/*  Max Register (Based on addr) */
#define IBEX_SPI_HOST_MAX_REGS           (IBEX_SPI_HOST_EVENT_ENABLE + 1)

/* MISC */
#define TX_INTERRUPT_TRIGGER_DELAY_NS    100
#define BIDIRECTIONAL_TRANSFER           3

typedef struct {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;
    uint32_t regs[IBEX_SPI_HOST_MAX_REGS];
    /* Multi-reg that sets config opts per CS */
    uint32_t *config_opts;
    Fifo8 rx_fifo;
    Fifo8 tx_fifo;
    QEMUTimer *fifo_trigger_handle;

    qemu_irq event;
    qemu_irq host_err;
    uint32_t num_cs;
    qemu_irq *cs_lines;
    SSIBus *ssi;

    /* Used to track the init status, for replicating TXDATA ghost writes */
    bool init_status;
} IbexSPIHostState;

#endif
