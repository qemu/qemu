/*
 * QEMU PowerPC PowerNV Processor I2C model
 *
 * Copyright (c) 2019-2023, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PPC_PNV_I2C_H
#define PPC_PNV_I2C_H

#include "hw/ppc/pnv.h"
#include "hw/i2c/i2c.h"
#include "qemu/fifo8.h"

#define TYPE_PNV_I2C "pnv-i2c"
#define PNV_I2C(obj) OBJECT_CHECK(PnvI2C, (obj), TYPE_PNV_I2C)

#define PNV_I2C_REGS 0x20

typedef struct PnvI2C {
    DeviceState parent;

    struct PnvChip *chip;

    qemu_irq psi_irq;

    uint64_t regs[PNV_I2C_REGS];
    uint32_t engine;
    uint32_t num_busses;
    I2CBus **busses;

    MemoryRegion xscom_regs;

    Fifo8 fifo;
} PnvI2C;

#endif /* PPC_PNV_I2C_H */
