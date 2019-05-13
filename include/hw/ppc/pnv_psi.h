/*
 * QEMU PowerPC PowerNV Processor Service Interface (PSI) model
 *
 * Copyright (c) 2015-2017, IBM Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PPC_PNV_PSI_H
#define PPC_PNV_PSI_H

#include "hw/sysbus.h"
#include "hw/ppc/xics.h"
#include "hw/ppc/xive.h"

#define TYPE_PNV_PSI "pnv-psi"
#define PNV_PSI(obj) \
     OBJECT_CHECK(PnvPsi, (obj), TYPE_PNV_PSI)

#define PSIHB_XSCOM_MAX         0x20

typedef struct PnvPsi {
    SysBusDevice parent;

    MemoryRegion regs_mr;
    uint64_t bar;

    /* FSP region not supported */
    /* MemoryRegion fsp_mr; */
    uint64_t fsp_bar;

    /* Interrupt generation */
    qemu_irq *qirqs;

    /* Registers */
    uint64_t regs[PSIHB_XSCOM_MAX];

    MemoryRegion xscom_regs;
} PnvPsi;

#define TYPE_PNV8_PSI TYPE_PNV_PSI "-POWER8"
#define PNV8_PSI(obj) \
    OBJECT_CHECK(Pnv8Psi, (obj), TYPE_PNV8_PSI)

typedef struct Pnv8Psi {
    PnvPsi   parent;

    ICSState ics;
} Pnv8Psi;

#define TYPE_PNV9_PSI TYPE_PNV_PSI "-POWER9"
#define PNV9_PSI(obj) \
    OBJECT_CHECK(Pnv9Psi, (obj), TYPE_PNV9_PSI)

typedef struct Pnv9Psi {
    PnvPsi   parent;

    XiveSource source;
} Pnv9Psi;

#define PNV_PSI_CLASS(klass) \
     OBJECT_CLASS_CHECK(PnvPsiClass, (klass), TYPE_PNV_PSI)
#define PNV_PSI_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PnvPsiClass, (obj), TYPE_PNV_PSI)

typedef struct PnvPsiClass {
    SysBusDeviceClass parent_class;

    int chip_type;
    uint32_t xscom_pcba;
    uint32_t xscom_size;
    uint64_t bar_mask;

    void (*irq_set)(PnvPsi *psi, int, bool state);
} PnvPsiClass;

/* The PSI and FSP interrupts are muxed on the same IRQ number */
typedef enum PnvPsiIrq {
    PSIHB_IRQ_PSI, /* internal use only */
    PSIHB_IRQ_FSP, /* internal use only */
    PSIHB_IRQ_OCC,
    PSIHB_IRQ_FSI,
    PSIHB_IRQ_LPC_I2C,
    PSIHB_IRQ_LOCAL_ERR,
    PSIHB_IRQ_EXTERNAL,
} PnvPsiIrq;

#define PSI_NUM_INTERRUPTS 6

void pnv_psi_irq_set(PnvPsi *psi, int irq, bool state);

/* P9 PSI Interrupts */
#define PSIHB9_IRQ_PSI          0
#define PSIHB9_IRQ_OCC          1
#define PSIHB9_IRQ_FSI          2
#define PSIHB9_IRQ_LPCHC        3
#define PSIHB9_IRQ_LOCAL_ERR    4
#define PSIHB9_IRQ_GLOBAL_ERR   5
#define PSIHB9_IRQ_TPM          6
#define PSIHB9_IRQ_LPC_SIRQ0    7
#define PSIHB9_IRQ_LPC_SIRQ1    8
#define PSIHB9_IRQ_LPC_SIRQ2    9
#define PSIHB9_IRQ_LPC_SIRQ3    10
#define PSIHB9_IRQ_SBE_I2C      11
#define PSIHB9_IRQ_DIO          12
#define PSIHB9_IRQ_PSU          13
#define PSIHB9_NUM_IRQS         14

void pnv_psi_pic_print_info(Pnv9Psi *psi, Monitor *mon);

#endif /* PPC_PNV_PSI_H */
