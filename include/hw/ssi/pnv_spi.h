/*
 * QEMU PowerPC SPI model
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This model Supports a connection to a single SPI responder.
 * Introduced for P10 to provide access to SPI seeproms, TPM, flash device
 * and an ADC controller.
 */

#ifndef PPC_PNV_SPI_H
#define PPC_PNV_SPI_H

#include "hw/ssi/ssi.h"
#include "hw/sysbus.h"

#define TYPE_PNV_SPI "pnv-spi"
OBJECT_DECLARE_SIMPLE_TYPE(PnvSpi, PNV_SPI)

#define PNV_SPI_REG_SIZE 8
#define PNV_SPI_REGS 7

#define TYPE_PNV_SPI_BUS "pnv-spi-bus"
typedef struct PnvSpi {
    SysBusDevice parent_obj;

    SSIBus *ssi_bus;
    qemu_irq *cs_line;
    MemoryRegion    xscom_spic_regs;
    /* SPI object number */
    uint32_t        spic_num;

    /* SPI registers */
    uint64_t        regs[PNV_SPI_REGS];
    uint8_t         seq_op[PNV_SPI_REG_SIZE];
    uint64_t        status;
} PnvSpi;
#endif /* PPC_PNV_SPI_H */
