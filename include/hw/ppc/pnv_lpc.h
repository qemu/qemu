/*
 * QEMU PowerPC PowerNV LPC controller
 *
 * Copyright (c) 2016-2022, IBM Corporation.
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

#ifndef PPC_PNV_LPC_H
#define PPC_PNV_LPC_H

#include "system/memory.h"
#include "hw/ppc/pnv.h"
#include "hw/qdev-core.h"
#include "hw/isa/isa.h" /* For ISA_NUM_IRQS */

#define TYPE_PNV_LPC "pnv-lpc"
typedef struct PnvLpcClass PnvLpcClass;
typedef struct PnvLpcController PnvLpcController;
DECLARE_OBJ_CHECKERS(PnvLpcController, PnvLpcClass,
                     PNV_LPC, TYPE_PNV_LPC)
#define TYPE_PNV8_LPC TYPE_PNV_LPC "-POWER8"
DECLARE_INSTANCE_CHECKER(PnvLpcController, PNV8_LPC,
                         TYPE_PNV8_LPC)

#define TYPE_PNV9_LPC TYPE_PNV_LPC "-POWER9"
DECLARE_INSTANCE_CHECKER(PnvLpcController, PNV9_LPC,
                         TYPE_PNV9_LPC)

#define TYPE_PNV10_LPC TYPE_PNV_LPC "-POWER10"
DECLARE_INSTANCE_CHECKER(PnvLpcController, PNV10_LPC,
                         TYPE_PNV10_LPC)

struct PnvLpcController {
    DeviceState parent;

    uint64_t eccb_stat_reg;
    uint32_t eccb_data_reg;

    /* OPB bus */
    MemoryRegion opb_mr;
    AddressSpace opb_as;

    /* ISA IO and Memory space */
    MemoryRegion isa_io;
    MemoryRegion isa_mem;
    MemoryRegion isa_fw;

    /* Windows from OPB to ISA (aliases) */
    MemoryRegion opb_isa_io;
    MemoryRegion opb_isa_mem;
    MemoryRegion opb_isa_fw;

    /* Registers */
    MemoryRegion lpc_hc_regs;
    MemoryRegion opb_master_regs;

    /* OPB Master LS registers */
    uint32_t opb_irq_route0;
    uint32_t opb_irq_route1;
    uint32_t opb_irq_stat;
    uint32_t opb_irq_mask;
    uint32_t opb_irq_pol;
    uint32_t opb_irq_input;

    /* LPC device IRQ state */
    uint32_t lpc_hc_irq_inputs;

    /* LPC HC registers */
    uint32_t lpc_hc_fw_seg_idsel;
    uint32_t lpc_hc_fw_rd_acc_size;
    uint32_t lpc_hc_irqser_ctrl;
    uint32_t lpc_hc_irqmask;
    uint32_t lpc_hc_irqstat;
    uint32_t lpc_hc_error_addr;

    /* XSCOM registers */
    MemoryRegion xscom_regs;

    /*
     * In P8, ISA irqs are combined with internal sources to drive the
     * LPCHC interrupt output. P9 ISA irqs raise one of 4 lines that
     * drive PSI SERIRQ irqs, routing according to OPB routing registers.
     */
    bool psi_has_serirq;

    /* PSI to generate interrupts */
    qemu_irq psi_irq_lpchc;

    /* P9 serirq lines and irq routing table */
    qemu_irq psi_irq_serirq[4];
    int irq_to_serirq_route[ISA_NUM_IRQS];
};

struct PnvLpcClass {
    DeviceClass parent_class;

    DeviceRealize parent_realize;
};

bool pnv_lpc_opb_read(PnvLpcController *lpc, uint32_t addr,
                      uint8_t *data, int sz);
bool pnv_lpc_opb_write(PnvLpcController *lpc, uint32_t addr,
                       uint8_t *data, int sz);

ISABus *pnv_lpc_isa_create(PnvLpcController *lpc, bool use_cpld, Error **errp);
int pnv_dt_lpc(PnvChip *chip, void *fdt, int root_offset,
               uint64_t lpcm_addr, uint64_t lpcm_size);

#endif /* PPC_PNV_LPC_H */
