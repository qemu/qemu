/*
 * QEMU PowerPC PowerNV XSCOM bus definitions
 *
 * Copyright (c) 2016, IBM Corporation.
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PPC_PNV_XSCOM_H
#define PPC_PNV_XSCOM_H

#include "system/memory.h"

typedef struct PnvXScomInterface PnvXScomInterface;
typedef struct PnvChip PnvChip;

#define TYPE_PNV_XSCOM_INTERFACE "pnv-xscom-interface"
#define PNV_XSCOM_INTERFACE(obj) \
    INTERFACE_CHECK(PnvXScomInterface, (obj), TYPE_PNV_XSCOM_INTERFACE)
typedef struct PnvXScomInterfaceClass PnvXScomInterfaceClass;
DECLARE_CLASS_CHECKERS(PnvXScomInterfaceClass, PNV_XSCOM_INTERFACE,
                       TYPE_PNV_XSCOM_INTERFACE)

struct PnvXScomInterfaceClass {
    InterfaceClass parent;
    int (*dt_xscom)(PnvXScomInterface *dev, void *fdt, int offset);
};

/*
 * Layout of the XSCOM PCB addresses of EX core 1 (POWER 8)
 *
 *   GPIO        0x1100xxxx
 *   SCOM        0x1101xxxx
 *   OHA         0x1102xxxx
 *   CLOCK CTL   0x1103xxxx
 *   FIR         0x1104xxxx
 *   THERM       0x1105xxxx
 *   <reserved>  0x1106xxxx
 *               ..
 *               0x110Exxxx
 *   PCB SLAVE   0x110Fxxxx
 */

#define PNV_XSCOM_EX_CORE_BASE    0x10000000ull

#define PNV_XSCOM_EX_BASE(core) \
    (PNV_XSCOM_EX_CORE_BASE | ((uint64_t)(core) << 24))
#define PNV_XSCOM_EX_SIZE         0x100000

#define PNV_XSCOM_LPC_BASE        0xb0020
#define PNV_XSCOM_LPC_SIZE        0x4

#define PNV_XSCOM_PSIHB_BASE      0x2010900
#define PNV_XSCOM_PSIHB_SIZE      0x20

#define PNV_XSCOM_CHIPTOD_BASE    0x0040000
#define PNV_XSCOM_CHIPTOD_SIZE    0x31

#define PNV_XSCOM_OCC_BASE        0x0066000
#define PNV_XSCOM_OCC_SIZE        0x6000

#define PNV_XSCOM_PBA_BASE        0x2013f00
#define PNV_XSCOM_PBA_SIZE        0x40

#define PNV_XSCOM_PBCQ_NEST_BASE  0x2012000
#define PNV_XSCOM_PBCQ_NEST_SIZE  0x46

#define PNV_XSCOM_PBCQ_PCI_BASE   0x9012000
#define PNV_XSCOM_PBCQ_PCI_SIZE   0x15

#define PNV_XSCOM_PBCQ_SPCI_BASE  0x9013c00
#define PNV_XSCOM_PBCQ_SPCI_SIZE  0x5

#define PNV9_XSCOM_ADU_BASE       0x0090000
#define PNV9_XSCOM_ADU_SIZE       0x55

/*
 * Layout of the XSCOM PCB addresses (POWER 9)
 */
#define PNV9_XSCOM_EC_BASE(core) \
    ((uint64_t)(((core) & 0x1F) + 0x20) << 24)
#define PNV9_XSCOM_EC_SIZE        0x100000

#define PNV9_XSCOM_EQ_BASE(core) \
    ((uint64_t)(((core) & 0x1C) + 0x40) << 22)
#define PNV9_XSCOM_EQ_SIZE        0x100000

#define PNV9_XSCOM_I2CM_BASE      0xa0000
#define PNV9_XSCOM_I2CM_SIZE      0x1000

#define PNV9_XSCOM_CHIPTOD_BASE   PNV_XSCOM_CHIPTOD_BASE
#define PNV9_XSCOM_CHIPTOD_SIZE   PNV_XSCOM_CHIPTOD_SIZE

#define PNV9_XSCOM_OCC_BASE       PNV_XSCOM_OCC_BASE
#define PNV9_XSCOM_OCC_SIZE       0x8000

#define PNV9_XSCOM_SBE_CTRL_BASE  0x00050008
#define PNV9_XSCOM_SBE_CTRL_SIZE  0x1

#define PNV9_XSCOM_SBE_MBOX_BASE  0x000D0050
#define PNV9_XSCOM_SBE_MBOX_SIZE  0x16

#define PNV9_XSCOM_PBA_BASE       0x5012b00
#define PNV9_XSCOM_PBA_SIZE       0x40

#define PNV9_XSCOM_PSIHB_BASE     0x5012900
#define PNV9_XSCOM_PSIHB_SIZE     0x100

#define PNV9_XSCOM_XIVE_BASE      0x5013000
#define PNV9_XSCOM_XIVE_SIZE      0x300

#define PNV9_XSCOM_PEC_NEST_BASE  0x4010c00
#define PNV9_XSCOM_PEC_NEST_SIZE  0x100

#define PNV9_XSCOM_PEC_PCI_BASE   0xd010800
#define PNV9_XSCOM_PEC_PCI_SIZE   0x200

#define PNV9_XSCOM_PEC_NEST_CPLT_BASE  0x0d000000

/* XSCOM PCI "pass-through" window to PHB SCOM */
#define PNV9_XSCOM_PEC_PCI_STK0   0x100
#define PNV9_XSCOM_PEC_PCI_STK1   0x140
#define PNV9_XSCOM_PEC_PCI_STK2   0x180

#define PNV10_XSCOM_ADU_BASE      0x0090000
#define PNV10_XSCOM_ADU_SIZE      0x55

/*
 * Layout of the XSCOM PCB addresses (POWER 10)
 */
#define PNV10_XSCOM_EQ_CHIPLET(core)  (0x20 + ((core) >> 2))
#define PNV10_XSCOM_EQ(chiplet)       ((chiplet) << 24)
#define PNV10_XSCOM_EC(proc)                    \
    ((0x2 << 16) | ((1 << (3 - (proc))) << 12))

#define PNV10_XSCOM_QME(chiplet) \
        (PNV10_XSCOM_EQ(chiplet) | (0xE << 16))

/*
 * Make the region larger by 0x1000 (instead of starting at an offset) so the
 * modelled addresses start from 0
 */
#define PNV10_XSCOM_QME_BASE(core)     \
    ((uint64_t) PNV10_XSCOM_QME(PNV10_XSCOM_EQ_CHIPLET(core)))
#define PNV10_XSCOM_QME_SIZE        (0x8000 + 0x1000)

#define PNV10_XSCOM_EQ_BASE(core)     \
    ((uint64_t) PNV10_XSCOM_EQ(PNV10_XSCOM_EQ_CHIPLET(core)))
#define PNV10_XSCOM_EQ_SIZE        0x20000

#define PNV10_XSCOM_EC_BASE(core) \
    ((uint64_t) PNV10_XSCOM_EQ_BASE(core) | PNV10_XSCOM_EC(core & 0x3))
#define PNV10_XSCOM_EC_SIZE        0x1000

#define PNV10_XSCOM_PSIHB_BASE     0x3011D00
#define PNV10_XSCOM_PSIHB_SIZE     0x100

#define PNV10_XSCOM_I2CM_BASE      PNV9_XSCOM_I2CM_BASE
#define PNV10_XSCOM_I2CM_SIZE      PNV9_XSCOM_I2CM_SIZE

#define PNV10_XSCOM_CHIPTOD_BASE   PNV9_XSCOM_CHIPTOD_BASE
#define PNV10_XSCOM_CHIPTOD_SIZE   PNV9_XSCOM_CHIPTOD_SIZE

#define PNV10_XSCOM_OCC_BASE       PNV9_XSCOM_OCC_BASE
#define PNV10_XSCOM_OCC_SIZE       PNV9_XSCOM_OCC_SIZE

#define PNV10_XSCOM_SBE_CTRL_BASE  PNV9_XSCOM_SBE_CTRL_BASE
#define PNV10_XSCOM_SBE_CTRL_SIZE  PNV9_XSCOM_SBE_CTRL_SIZE

#define PNV10_XSCOM_SBE_MBOX_BASE  PNV9_XSCOM_SBE_MBOX_BASE
#define PNV10_XSCOM_SBE_MBOX_SIZE  PNV9_XSCOM_SBE_MBOX_SIZE

#define PNV10_XSCOM_PBA_BASE       0x01010CDA
#define PNV10_XSCOM_PBA_SIZE       0x40

#define PNV10_XSCOM_XIVE2_BASE     0x2010800
#define PNV10_XSCOM_XIVE2_SIZE     0x400

#define PNV10_XSCOM_N1_CHIPLET_CTRL_REGS_BASE      0x3000000
#define PNV10_XSCOM_CHIPLET_CTRL_REGS_SIZE         0x400

#define PNV10_XSCOM_N1_PB_SCOM_EQ_BASE      0x3011000
#define PNV10_XSCOM_N1_PB_SCOM_EQ_SIZE      0x200

#define PNV10_XSCOM_N1_PB_SCOM_ES_BASE      0x3011300
#define PNV10_XSCOM_N1_PB_SCOM_ES_SIZE      0x100

#define PNV10_XSCOM_PEC_NEST_BASE  0x3011800 /* index goes downwards ... */
#define PNV10_XSCOM_PEC_NEST_SIZE  0x100

#define PNV10_XSCOM_PEC_NEST_CPLT_BASE 0x08000000

#define PNV10_XSCOM_PEC_PCI_BASE   0x8010800 /* index goes upwards ... */
#define PNV10_XSCOM_PEC_PCI_SIZE   0x200

#define PNV10_XSCOM_PIB_SPIC_BASE 0xc0000
#define PNV10_XSCOM_PIB_SPIC_SIZE 0x20

/*
 * Power11 core is same as Power10
 */
#define PNV11_XSCOM_EC_BASE(core)  PNV10_XSCOM_EC_BASE(core)

#define PNV11_XSCOM_ADU_BASE       PNV10_XSCOM_ADU_BASE
#define PNV11_XSCOM_ADU_SIZE       PNV10_XSCOM_ADU_SIZE

#define PNV11_XSCOM_QME_BASE(core) PNV10_XSCOM_QME_BASE(core)

#define PNV11_XSCOM_EQ_BASE(core)  PNV10_XSCOM_EQ_BASE(core)

#define PNV11_XSCOM_PSIHB_BASE     PNV10_XSCOM_PSIHB_BASE
#define PNV11_XSCOM_PSIHB_SIZE     PNV10_XSCOM_PSIHB_SIZE

#define PNV11_XSCOM_I2CM_BASE      PNV10_XSCOM_I2CM_BASE
#define PNV11_XSCOM_I2CM_SIZE      PNV10_XSCOM_I2CM_SIZE

#define PNV11_XSCOM_CHIPTOD_BASE   PNV10_XSCOM_CHIPTOD_BASE
#define PNV11_XSCOM_CHIPTOD_SIZE   PNV10_XSCOM_CHIPTOD_SIZE

#define PNV11_XSCOM_OCC_BASE       PNV10_XSCOM_OCC_BASE
#define PNV11_XSCOM_OCC_SIZE       PNV10_XSCOM_OCC_SIZE

#define PNV11_XSCOM_SBE_CTRL_BASE  PNV10_XSCOM_SBE_CTRL_BASE
#define PNV11_XSCOM_SBE_CTRL_SIZE  PNV10_XSCOM_SBE_CTRL_SIZE

#define PNV11_XSCOM_SBE_MBOX_BASE  PNV10_XSCOM_SBE_MBOX_BASE
#define PNV11_XSCOM_SBE_MBOX_SIZE  PNV10_XSCOM_SBE_MBOX_SIZE

#define PNV11_XSCOM_PBA_BASE       PNV10_XSCOM_PBA_BASE
#define PNV11_XSCOM_PBA_SIZE       PNV10_XSCOM_PBA_SIZE

#define PNV11_XSCOM_XIVE2_BASE     PNV10_XSCOM_XIVE2_BASE
#define PNV11_XSCOM_XIVE2_SIZE     PNV10_XSCOM_XIVE2_SIZE

#define PNV11_XSCOM_N1_CHIPLET_CTRL_REGS_BASE  \
    PNV10_XSCOM_N1_CHIPLET_CTRL_REGS_BASE
#define PNV11_XSCOM_CHIPLET_CTRL_REGS_SIZE   PNV10_XSCOM_CHIPLET_CTRL_REGS_SIZE

#define PNV11_XSCOM_N1_PB_SCOM_EQ_BASE  PNV10_XSCOM_N1_PB_SCOM_EQ_BASE
#define PNV11_XSCOM_N1_PB_SCOM_EQ_SIZE  PNV10_XSCOM_N1_PB_SCOM_EQ_SIZE

#define PNV11_XSCOM_N1_PB_SCOM_ES_BASE  PNV10_XSCOM_N1_PB_SCOM_ES_BASE
#define PNV11_XSCOM_N1_PB_SCOM_ES_SIZE  PNV10_XSCOM_N1_PB_SCOM_ES_SIZE

#define PNV11_XSCOM_PIB_SPIC_BASE  PNV10_XSCOM_PIB_SPIC_BASE
#define PNV11_XSCOM_PIB_SPIC_SIZE  PNV10_XSCOM_PIB_SPIC_SIZE

void pnv_xscom_init(PnvChip *chip, uint64_t size, hwaddr addr);
int pnv_dt_xscom(PnvChip *chip, void *fdt, int root_offset,
                 uint64_t xscom_base, uint64_t xscom_size,
                 const char *compat, int compat_size);

void pnv_xscom_add_subregion(PnvChip *chip, hwaddr offset,
                             MemoryRegion *mr);
void pnv_xscom_region_init(MemoryRegion *mr,
                           Object *owner,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size);

#endif /* PPC_PNV_XSCOM_H */
