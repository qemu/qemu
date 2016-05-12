/*
 * IMX SPI Controller
 *
 * Copyright 2016 Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef IMX_SPI_H
#define IMX_SPI_H

#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/bitops.h"
#include "qemu/fifo32.h"

#define ECSPI_FIFO_SIZE 64

#define ECSPI_RXDATA 0
#define ECSPI_TXDATA 1
#define ECSPI_CONREG 2
#define ECSPI_CONFIGREG 3
#define ECSPI_INTREG 4
#define ECSPI_DMAREG 5
#define ECSPI_STATREG 6
#define ECSPI_PERIODREG 7
#define ECSPI_TESTREG 8
#define ECSPI_MSGDATA 16
#define ECSPI_MAX 17

/* ECSPI_CONREG */
#define ECSPI_CONREG_EN (1 << 0)
#define ECSPI_CONREG_HT (1 << 1)
#define ECSPI_CONREG_XCH (1 << 2)
#define ECSPI_CONREG_SMC (1 << 3)
#define ECSPI_CONREG_CHANNEL_MODE_SHIFT 4
#define ECSPI_CONREG_CHANNEL_MODE_LENGTH 4
#define ECSPI_CONREG_DRCTL_SHIFT 16
#define ECSPI_CONREG_DRCTL_LENGTH 2
#define ECSPI_CONREG_CHANNEL_SELECT_SHIFT 18
#define ECSPI_CONREG_CHANNEL_SELECT_LENGTH 2
#define ECSPI_CONREG_BURST_LENGTH_SHIFT 20
#define ECSPI_CONREG_BURST_LENGTH_LENGTH 12

/* ECSPI_CONFIGREG */
#define ECSPI_CONFIGREG_SS_CTL_SHIFT 8
#define ECSPI_CONFIGREG_SS_CTL_LENGTH 4

/* ECSPI_INTREG */
#define ECSPI_INTREG_TEEN (1 << 0)
#define ECSPI_INTREG_TDREN (1 << 1)
#define ECSPI_INTREG_TFEN (1 << 2)
#define ECSPI_INTREG_RREN (1 << 3)
#define ECSPI_INTREG_RDREN (1 << 4)
#define ECSPI_INTREG_RFEN (1 << 5)
#define ECSPI_INTREG_ROEN (1 << 6)
#define ECSPI_INTREG_TCEN (1 << 7)

/* ECSPI_DMAREG */
#define ECSPI_DMAREG_RXTDEN (1 << 31)
#define ECSPI_DMAREG_RXDEN (1 << 23)
#define ECSPI_DMAREG_TEDEN (1 << 7)
#define ECSPI_DMAREG_RX_THRESHOLD_SHIFT 16
#define ECSPI_DMAREG_RX_THRESHOLD_LENGTH 6

/* ECSPI_STATREG */
#define ECSPI_STATREG_TE (1 << 0)
#define ECSPI_STATREG_TDR (1 << 1)
#define ECSPI_STATREG_TF (1 << 2)
#define ECSPI_STATREG_RR (1 << 3)
#define ECSPI_STATREG_RDR (1 << 4)
#define ECSPI_STATREG_RF (1 << 5)
#define ECSPI_STATREG_RO (1 << 6)
#define ECSPI_STATREG_TC (1 << 7)

#define EXTRACT(value, name) extract32(value, name##_SHIFT, name##_LENGTH)

#define TYPE_IMX_SPI "imx.spi"
#define IMX_SPI(obj) OBJECT_CHECK(IMXSPIState, (obj), TYPE_IMX_SPI)

typedef struct IMXSPIState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion iomem;

    qemu_irq irq;

    qemu_irq cs_lines[4];

    SSIBus *bus;

    uint32_t regs[ECSPI_MAX];

    Fifo32 rx_fifo;
    Fifo32 tx_fifo;

    int16_t burst_length;
} IMXSPIState;

#endif /* IMX_SPI_H */
