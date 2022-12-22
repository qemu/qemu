/*
 * QEMU PowerPC PowerNV LPC controller
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

#include "qemu/osdep.h"
#include "target/ppc/cpu.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_chip.h"
#include "hw/ppc/pnv_lpc.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/fdt.h"

#include <libfdt.h>

enum {
    ECCB_CTL    = 0,
    ECCB_RESET  = 1,
    ECCB_STAT   = 2,
    ECCB_DATA   = 3,
};

/* OPB Master LS registers */
#define OPB_MASTER_LS_ROUTE0    0x8
#define OPB_MASTER_LS_ROUTE1    0xC
#define OPB_MASTER_LS_IRQ_STAT  0x50
#define   OPB_MASTER_IRQ_LPC            0x00000800
#define OPB_MASTER_LS_IRQ_MASK  0x54
#define OPB_MASTER_LS_IRQ_POL   0x58
#define OPB_MASTER_LS_IRQ_INPUT 0x5c

/* LPC HC registers */
#define LPC_HC_FW_SEG_IDSEL     0x24
#define LPC_HC_FW_RD_ACC_SIZE   0x28
#define   LPC_HC_FW_RD_1B               0x00000000
#define   LPC_HC_FW_RD_2B               0x01000000
#define   LPC_HC_FW_RD_4B               0x02000000
#define   LPC_HC_FW_RD_16B              0x04000000
#define   LPC_HC_FW_RD_128B             0x07000000
#define LPC_HC_IRQSER_CTRL      0x30
#define   LPC_HC_IRQSER_EN              0x80000000
#define   LPC_HC_IRQSER_QMODE           0x40000000
#define   LPC_HC_IRQSER_START_MASK      0x03000000
#define   LPC_HC_IRQSER_START_4CLK      0x00000000
#define   LPC_HC_IRQSER_START_6CLK      0x01000000
#define   LPC_HC_IRQSER_START_8CLK      0x02000000
#define LPC_HC_IRQMASK          0x34    /* same bit defs as LPC_HC_IRQSTAT */
#define LPC_HC_IRQSTAT          0x38
#define   LPC_HC_IRQ_SERIRQ0            0x80000000 /* all bits down to ... */
#define   LPC_HC_IRQ_SERIRQ16           0x00008000 /* IRQ16=IOCHK#, IRQ2=SMI# */
#define   LPC_HC_IRQ_SERIRQ_ALL         0xffff8000
#define   LPC_HC_IRQ_LRESET             0x00000400
#define   LPC_HC_IRQ_SYNC_ABNORM_ERR    0x00000080
#define   LPC_HC_IRQ_SYNC_NORESP_ERR    0x00000040
#define   LPC_HC_IRQ_SYNC_NORM_ERR      0x00000020
#define   LPC_HC_IRQ_SYNC_TIMEOUT_ERR   0x00000010
#define   LPC_HC_IRQ_SYNC_TARG_TAR_ERR  0x00000008
#define   LPC_HC_IRQ_SYNC_BM_TAR_ERR    0x00000004
#define   LPC_HC_IRQ_SYNC_BM0_REQ       0x00000002
#define   LPC_HC_IRQ_SYNC_BM1_REQ       0x00000001
#define LPC_HC_ERROR_ADDRESS    0x40

#define LPC_OPB_SIZE            0x100000000ull

#define ISA_IO_SIZE             0x00010000
#define ISA_MEM_SIZE            0x10000000
#define ISA_FW_SIZE             0x10000000
#define LPC_IO_OPB_ADDR         0xd0010000
#define LPC_IO_OPB_SIZE         0x00010000
#define LPC_MEM_OPB_ADDR        0xe0000000
#define LPC_MEM_OPB_SIZE        0x10000000
#define LPC_FW_OPB_ADDR         0xf0000000
#define LPC_FW_OPB_SIZE         0x10000000

#define LPC_OPB_REGS_OPB_ADDR   0xc0010000
#define LPC_OPB_REGS_OPB_SIZE   0x00000060
#define LPC_OPB_REGS_OPBA_ADDR  0xc0011000
#define LPC_OPB_REGS_OPBA_SIZE  0x00000008
#define LPC_HC_REGS_OPB_ADDR    0xc0012000
#define LPC_HC_REGS_OPB_SIZE    0x00000100

static int pnv_lpc_dt_xscom(PnvXScomInterface *dev, void *fdt, int xscom_offset)
{
    const char compat[] = "ibm,power8-lpc\0ibm,lpc";
    char *name;
    int offset;
    uint32_t lpc_pcba = PNV_XSCOM_LPC_BASE;
    uint32_t reg[] = {
        cpu_to_be32(lpc_pcba),
        cpu_to_be32(PNV_XSCOM_LPC_SIZE)
    };

    name = g_strdup_printf("isa@%x", lpc_pcba);
    offset = fdt_add_subnode(fdt, xscom_offset, name);
    _FDT(offset);
    g_free(name);

    _FDT((fdt_setprop(fdt, offset, "reg", reg, sizeof(reg))));
    _FDT((fdt_setprop_cell(fdt, offset, "#address-cells", 2)));
    _FDT((fdt_setprop_cell(fdt, offset, "#size-cells", 1)));
    _FDT((fdt_setprop(fdt, offset, "compatible", compat, sizeof(compat))));
    return 0;
}

/* POWER9 only */
int pnv_dt_lpc(PnvChip *chip, void *fdt, int root_offset, uint64_t lpcm_addr,
               uint64_t lpcm_size)
{
    const char compat[] = "ibm,power9-lpcm-opb\0simple-bus";
    const char lpc_compat[] = "ibm,power9-lpc\0ibm,lpc";
    char *name;
    int offset, lpcm_offset;
    uint32_t opb_ranges[8] = { 0,
                               cpu_to_be32(lpcm_addr >> 32),
                               cpu_to_be32((uint32_t)lpcm_addr),
                               cpu_to_be32(lpcm_size / 2),
                               cpu_to_be32(lpcm_size / 2),
                               cpu_to_be32(lpcm_addr >> 32),
                               cpu_to_be32(lpcm_size / 2),
                               cpu_to_be32(lpcm_size / 2),
    };
    uint32_t opb_reg[4] = { cpu_to_be32(lpcm_addr >> 32),
                            cpu_to_be32((uint32_t)lpcm_addr),
                            cpu_to_be32(lpcm_size >> 32),
                            cpu_to_be32((uint32_t)lpcm_size),
    };
    uint32_t lpc_ranges[12] = { 0, 0,
                                cpu_to_be32(LPC_MEM_OPB_ADDR),
                                cpu_to_be32(LPC_MEM_OPB_SIZE),
                                cpu_to_be32(1), 0,
                                cpu_to_be32(LPC_IO_OPB_ADDR),
                                cpu_to_be32(LPC_IO_OPB_SIZE),
                                cpu_to_be32(3), 0,
                                cpu_to_be32(LPC_FW_OPB_ADDR),
                                cpu_to_be32(LPC_FW_OPB_SIZE),
    };
    uint32_t reg[2];

    /*
     * OPB bus
     */
    name = g_strdup_printf("lpcm-opb@%"PRIx64, lpcm_addr);
    lpcm_offset = fdt_add_subnode(fdt, root_offset, name);
    _FDT(lpcm_offset);
    g_free(name);

    _FDT((fdt_setprop(fdt, lpcm_offset, "reg", opb_reg, sizeof(opb_reg))));
    _FDT((fdt_setprop_cell(fdt, lpcm_offset, "#address-cells", 1)));
    _FDT((fdt_setprop_cell(fdt, lpcm_offset, "#size-cells", 1)));
    _FDT((fdt_setprop(fdt, lpcm_offset, "compatible", compat, sizeof(compat))));
    _FDT((fdt_setprop_cell(fdt, lpcm_offset, "ibm,chip-id", chip->chip_id)));
    _FDT((fdt_setprop(fdt, lpcm_offset, "ranges", opb_ranges,
                      sizeof(opb_ranges))));

    /*
     * OPB Master registers
     */
    name = g_strdup_printf("opb-master@%x", LPC_OPB_REGS_OPB_ADDR);
    offset = fdt_add_subnode(fdt, lpcm_offset, name);
    _FDT(offset);
    g_free(name);

    reg[0] = cpu_to_be32(LPC_OPB_REGS_OPB_ADDR);
    reg[1] = cpu_to_be32(LPC_OPB_REGS_OPB_SIZE);
    _FDT((fdt_setprop(fdt, offset, "reg", reg, sizeof(reg))));
    _FDT((fdt_setprop_string(fdt, offset, "compatible",
                             "ibm,power9-lpcm-opb-master")));

    /*
     * OPB arbitrer registers
     */
    name = g_strdup_printf("opb-arbitrer@%x", LPC_OPB_REGS_OPBA_ADDR);
    offset = fdt_add_subnode(fdt, lpcm_offset, name);
    _FDT(offset);
    g_free(name);

    reg[0] = cpu_to_be32(LPC_OPB_REGS_OPBA_ADDR);
    reg[1] = cpu_to_be32(LPC_OPB_REGS_OPBA_SIZE);
    _FDT((fdt_setprop(fdt, offset, "reg", reg, sizeof(reg))));
    _FDT((fdt_setprop_string(fdt, offset, "compatible",
                             "ibm,power9-lpcm-opb-arbiter")));

    /*
     * LPC Host Controller registers
     */
    name = g_strdup_printf("lpc-controller@%x", LPC_HC_REGS_OPB_ADDR);
    offset = fdt_add_subnode(fdt, lpcm_offset, name);
    _FDT(offset);
    g_free(name);

    reg[0] = cpu_to_be32(LPC_HC_REGS_OPB_ADDR);
    reg[1] = cpu_to_be32(LPC_HC_REGS_OPB_SIZE);
    _FDT((fdt_setprop(fdt, offset, "reg", reg, sizeof(reg))));
    _FDT((fdt_setprop_string(fdt, offset, "compatible",
                             "ibm,power9-lpc-controller")));

    name = g_strdup_printf("lpc@0");
    offset = fdt_add_subnode(fdt, lpcm_offset, name);
    _FDT(offset);
    g_free(name);
    _FDT((fdt_setprop_cell(fdt, offset, "#address-cells", 2)));
    _FDT((fdt_setprop_cell(fdt, offset, "#size-cells", 1)));
    _FDT((fdt_setprop(fdt, offset, "compatible", lpc_compat,
                      sizeof(lpc_compat))));
    _FDT((fdt_setprop(fdt, offset, "ranges", lpc_ranges,
                      sizeof(lpc_ranges))));

    return 0;
}

/*
 * These read/write handlers of the OPB address space should be common
 * with the P9 LPC Controller which uses direct MMIOs.
 *
 * TODO: rework to use address_space_stq() and address_space_ldq()
 * instead.
 */
static bool opb_read(PnvLpcController *lpc, uint32_t addr, uint8_t *data,
                     int sz)
{
    /* XXX Handle access size limits and FW read caching here */
    return !address_space_read(&lpc->opb_as, addr, MEMTXATTRS_UNSPECIFIED,
                               data, sz);
}

static bool opb_write(PnvLpcController *lpc, uint32_t addr, uint8_t *data,
                      int sz)
{
    /* XXX Handle access size limits here */
    return !address_space_write(&lpc->opb_as, addr, MEMTXATTRS_UNSPECIFIED,
                                data, sz);
}

#define ECCB_CTL_READ           PPC_BIT(15)
#define ECCB_CTL_SZ_LSH         (63 - 7)
#define ECCB_CTL_SZ_MASK        PPC_BITMASK(4, 7)
#define ECCB_CTL_ADDR_MASK      PPC_BITMASK(32, 63)

#define ECCB_STAT_OP_DONE       PPC_BIT(52)
#define ECCB_STAT_OP_ERR        PPC_BIT(52)
#define ECCB_STAT_RD_DATA_LSH   (63 - 37)
#define ECCB_STAT_RD_DATA_MASK  (0xffffffff << ECCB_STAT_RD_DATA_LSH)

static void pnv_lpc_do_eccb(PnvLpcController *lpc, uint64_t cmd)
{
    /* XXX Check for magic bits at the top, addr size etc... */
    unsigned int sz = (cmd & ECCB_CTL_SZ_MASK) >> ECCB_CTL_SZ_LSH;
    uint32_t opb_addr = cmd & ECCB_CTL_ADDR_MASK;
    uint8_t data[8];
    bool success;

    if (sz > sizeof(data)) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "ECCB: invalid operation at @0x%08x size %d\n", opb_addr, sz);
        return;
    }

    if (cmd & ECCB_CTL_READ) {
        success = opb_read(lpc, opb_addr, data, sz);
        if (success) {
            lpc->eccb_stat_reg = ECCB_STAT_OP_DONE |
                    (((uint64_t)data[0]) << 24 |
                     ((uint64_t)data[1]) << 16 |
                     ((uint64_t)data[2]) <<  8 |
                     ((uint64_t)data[3])) << ECCB_STAT_RD_DATA_LSH;
        } else {
            lpc->eccb_stat_reg = ECCB_STAT_OP_DONE |
                    (0xffffffffull << ECCB_STAT_RD_DATA_LSH);
        }
    } else {
        data[0] = lpc->eccb_data_reg >> 24;
        data[1] = lpc->eccb_data_reg >> 16;
        data[2] = lpc->eccb_data_reg >>  8;
        data[3] = lpc->eccb_data_reg;

        success = opb_write(lpc, opb_addr, data, sz);
        lpc->eccb_stat_reg = ECCB_STAT_OP_DONE;
    }
    /* XXX Which error bit (if any) to signal OPB error ? */
}

static uint64_t pnv_lpc_xscom_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvLpcController *lpc = PNV_LPC(opaque);
    uint32_t offset = addr >> 3;
    uint64_t val = 0;

    switch (offset & 3) {
    case ECCB_CTL:
    case ECCB_RESET:
        val = 0;
        break;
    case ECCB_STAT:
        val = lpc->eccb_stat_reg;
        lpc->eccb_stat_reg = 0;
        break;
    case ECCB_DATA:
        val = ((uint64_t)lpc->eccb_data_reg) << 32;
        break;
    }
    return val;
}

static void pnv_lpc_xscom_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
    PnvLpcController *lpc = PNV_LPC(opaque);
    uint32_t offset = addr >> 3;

    switch (offset & 3) {
    case ECCB_CTL:
        pnv_lpc_do_eccb(lpc, val);
        break;
    case ECCB_RESET:
        /*  XXXX  */
        break;
    case ECCB_STAT:
        break;
    case ECCB_DATA:
        lpc->eccb_data_reg = val >> 32;
        break;
    }
}

static const MemoryRegionOps pnv_lpc_xscom_ops = {
    .read = pnv_lpc_xscom_read,
    .write = pnv_lpc_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static uint64_t pnv_lpc_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvLpcController *lpc = PNV_LPC(opaque);
    uint64_t val = 0;
    uint32_t opb_addr = addr & ECCB_CTL_ADDR_MASK;
    MemTxResult result;

    switch (size) {
    case 4:
        val = address_space_ldl(&lpc->opb_as, opb_addr, MEMTXATTRS_UNSPECIFIED,
                                &result);
        break;
    case 1:
        val = address_space_ldub(&lpc->opb_as, opb_addr, MEMTXATTRS_UNSPECIFIED,
                                 &result);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "OPB read failed at @0x%"
                      HWADDR_PRIx " invalid size %d\n", addr, size);
        return 0;
    }

    if (result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "OPB read failed at @0x%"
                      HWADDR_PRIx "\n", addr);
    }

    return val;
}

static void pnv_lpc_mmio_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
    PnvLpcController *lpc = PNV_LPC(opaque);
    uint32_t opb_addr = addr & ECCB_CTL_ADDR_MASK;
    MemTxResult result;

    switch (size) {
    case 4:
        address_space_stl(&lpc->opb_as, opb_addr, val, MEMTXATTRS_UNSPECIFIED,
                          &result);
         break;
    case 1:
        address_space_stb(&lpc->opb_as, opb_addr, val, MEMTXATTRS_UNSPECIFIED,
                          &result);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "OPB write failed at @0x%"
                      HWADDR_PRIx " invalid size %d\n", addr, size);
        return;
    }

    if (result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "OPB write failed at @0x%"
                      HWADDR_PRIx "\n", addr);
    }
}

static const MemoryRegionOps pnv_lpc_mmio_ops = {
    .read = pnv_lpc_mmio_read,
    .write = pnv_lpc_mmio_write,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .endianness = DEVICE_BIG_ENDIAN,
};

static void pnv_lpc_eval_irqs(PnvLpcController *lpc)
{
    bool lpc_to_opb_irq = false;

    /* Update LPC controller to OPB line */
    if (lpc->lpc_hc_irqser_ctrl & LPC_HC_IRQSER_EN) {
        uint32_t irqs;

        irqs = lpc->lpc_hc_irqstat & lpc->lpc_hc_irqmask;
        lpc_to_opb_irq = (irqs != 0);
    }

    /* We don't honor the polarity register, it's pointless and unused
     * anyway
     */
    if (lpc_to_opb_irq) {
        lpc->opb_irq_input |= OPB_MASTER_IRQ_LPC;
    } else {
        lpc->opb_irq_input &= ~OPB_MASTER_IRQ_LPC;
    }

    /* Update OPB internal latch */
    lpc->opb_irq_stat |= lpc->opb_irq_input & lpc->opb_irq_mask;

    /* Reflect the interrupt */
    qemu_set_irq(lpc->psi_irq, lpc->opb_irq_stat != 0);
}

static uint64_t lpc_hc_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvLpcController *lpc = opaque;
    uint64_t val = 0xfffffffffffffffful;

    switch (addr) {
    case LPC_HC_FW_SEG_IDSEL:
        val =  lpc->lpc_hc_fw_seg_idsel;
        break;
    case LPC_HC_FW_RD_ACC_SIZE:
        val =  lpc->lpc_hc_fw_rd_acc_size;
        break;
    case LPC_HC_IRQSER_CTRL:
        val =  lpc->lpc_hc_irqser_ctrl;
        break;
    case LPC_HC_IRQMASK:
        val =  lpc->lpc_hc_irqmask;
        break;
    case LPC_HC_IRQSTAT:
        val =  lpc->lpc_hc_irqstat;
        break;
    case LPC_HC_ERROR_ADDRESS:
        val =  lpc->lpc_hc_error_addr;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "LPC HC Unimplemented register: 0x%"
                      HWADDR_PRIx "\n", addr);
    }
    return val;
}

static void lpc_hc_write(void *opaque, hwaddr addr, uint64_t val,
                         unsigned size)
{
    PnvLpcController *lpc = opaque;

    /* XXX Filter out reserved bits */

    switch (addr) {
    case LPC_HC_FW_SEG_IDSEL:
        /* XXX Actually figure out how that works as this impact
         * memory regions/aliases
         */
        lpc->lpc_hc_fw_seg_idsel = val;
        break;
    case LPC_HC_FW_RD_ACC_SIZE:
        lpc->lpc_hc_fw_rd_acc_size = val;
        break;
    case LPC_HC_IRQSER_CTRL:
        lpc->lpc_hc_irqser_ctrl = val;
        pnv_lpc_eval_irqs(lpc);
        break;
    case LPC_HC_IRQMASK:
        lpc->lpc_hc_irqmask = val;
        pnv_lpc_eval_irqs(lpc);
        break;
    case LPC_HC_IRQSTAT:
        lpc->lpc_hc_irqstat &= ~val;
        pnv_lpc_eval_irqs(lpc);
        break;
    case LPC_HC_ERROR_ADDRESS:
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "LPC HC Unimplemented register: 0x%"
                      HWADDR_PRIx "\n", addr);
    }
}

static const MemoryRegionOps lpc_hc_ops = {
    .read = lpc_hc_read,
    .write = lpc_hc_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t opb_master_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvLpcController *lpc = opaque;
    uint64_t val = 0xfffffffffffffffful;

    switch (addr) {
    case OPB_MASTER_LS_ROUTE0: /* TODO */
        val = lpc->opb_irq_route0;
        break;
    case OPB_MASTER_LS_ROUTE1: /* TODO */
        val = lpc->opb_irq_route1;
        break;
    case OPB_MASTER_LS_IRQ_STAT:
        val = lpc->opb_irq_stat;
        break;
    case OPB_MASTER_LS_IRQ_MASK:
        val = lpc->opb_irq_mask;
        break;
    case OPB_MASTER_LS_IRQ_POL:
        val = lpc->opb_irq_pol;
        break;
    case OPB_MASTER_LS_IRQ_INPUT:
        val = lpc->opb_irq_input;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "OPBM: read on unimplemented register: 0x%"
                      HWADDR_PRIx "\n", addr);
    }

    return val;
}

static void opb_master_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    PnvLpcController *lpc = opaque;

    switch (addr) {
    case OPB_MASTER_LS_ROUTE0: /* TODO */
        lpc->opb_irq_route0 = val;
        break;
    case OPB_MASTER_LS_ROUTE1: /* TODO */
        lpc->opb_irq_route1 = val;
        break;
    case OPB_MASTER_LS_IRQ_STAT:
        lpc->opb_irq_stat &= ~val;
        pnv_lpc_eval_irqs(lpc);
        break;
    case OPB_MASTER_LS_IRQ_MASK:
        lpc->opb_irq_mask = val;
        pnv_lpc_eval_irqs(lpc);
        break;
    case OPB_MASTER_LS_IRQ_POL:
        lpc->opb_irq_pol = val;
        pnv_lpc_eval_irqs(lpc);
        break;
    case OPB_MASTER_LS_IRQ_INPUT:
        /* Read only */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "OPBM: write on unimplemented register: 0x%"
                      HWADDR_PRIx " val=0x%08"PRIx64"\n", addr, val);
    }
}

static const MemoryRegionOps opb_master_ops = {
    .read = opb_master_read,
    .write = opb_master_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pnv_lpc_power8_realize(DeviceState *dev, Error **errp)
{
    PnvLpcController *lpc = PNV_LPC(dev);
    PnvLpcClass *plc = PNV_LPC_GET_CLASS(dev);
    Error *local_err = NULL;

    plc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* P8 uses a XSCOM region for LPC registers */
    pnv_xscom_region_init(&lpc->xscom_regs, OBJECT(lpc),
                          &pnv_lpc_xscom_ops, lpc, "xscom-lpc",
                          PNV_XSCOM_LPC_SIZE);
}

static void pnv_lpc_power8_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xdc = PNV_XSCOM_INTERFACE_CLASS(klass);
    PnvLpcClass *plc = PNV_LPC_CLASS(klass);

    dc->desc = "PowerNV LPC Controller POWER8";

    xdc->dt_xscom = pnv_lpc_dt_xscom;

    device_class_set_parent_realize(dc, pnv_lpc_power8_realize,
                                    &plc->parent_realize);
}

static const TypeInfo pnv_lpc_power8_info = {
    .name          = TYPE_PNV8_LPC,
    .parent        = TYPE_PNV_LPC,
    .class_init    = pnv_lpc_power8_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_lpc_power9_realize(DeviceState *dev, Error **errp)
{
    PnvLpcController *lpc = PNV_LPC(dev);
    PnvLpcClass *plc = PNV_LPC_GET_CLASS(dev);
    Error *local_err = NULL;

    plc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* P9 uses a MMIO region */
    memory_region_init_io(&lpc->xscom_regs, OBJECT(lpc), &pnv_lpc_mmio_ops,
                          lpc, "lpcm", PNV9_LPCM_SIZE);
}

static void pnv_lpc_power9_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvLpcClass *plc = PNV_LPC_CLASS(klass);

    dc->desc = "PowerNV LPC Controller POWER9";

    device_class_set_parent_realize(dc, pnv_lpc_power9_realize,
                                    &plc->parent_realize);
}

static const TypeInfo pnv_lpc_power9_info = {
    .name          = TYPE_PNV9_LPC,
    .parent        = TYPE_PNV_LPC,
    .class_init    = pnv_lpc_power9_class_init,
};

static void pnv_lpc_power10_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "PowerNV LPC Controller POWER10";
}

static const TypeInfo pnv_lpc_power10_info = {
    .name          = TYPE_PNV10_LPC,
    .parent        = TYPE_PNV9_LPC,
    .class_init    = pnv_lpc_power10_class_init,
};

static void pnv_lpc_realize(DeviceState *dev, Error **errp)
{
    PnvLpcController *lpc = PNV_LPC(dev);

    /* Reg inits */
    lpc->lpc_hc_fw_rd_acc_size = LPC_HC_FW_RD_4B;

    /* Create address space and backing MR for the OPB bus */
    memory_region_init(&lpc->opb_mr, OBJECT(dev), "lpc-opb", 0x100000000ull);
    address_space_init(&lpc->opb_as, &lpc->opb_mr, "lpc-opb");

    /* Create ISA IO and Mem space regions which are the root of
     * the ISA bus (ie, ISA address spaces). We don't create a
     * separate one for FW which we alias to memory.
     */
    memory_region_init(&lpc->isa_io, OBJECT(dev), "isa-io", ISA_IO_SIZE);
    memory_region_init(&lpc->isa_mem, OBJECT(dev), "isa-mem", ISA_MEM_SIZE);
    memory_region_init(&lpc->isa_fw, OBJECT(dev),  "isa-fw", ISA_FW_SIZE);

    /* Create windows from the OPB space to the ISA space */
    memory_region_init_alias(&lpc->opb_isa_io, OBJECT(dev), "lpc-isa-io",
                             &lpc->isa_io, 0, LPC_IO_OPB_SIZE);
    memory_region_add_subregion(&lpc->opb_mr, LPC_IO_OPB_ADDR,
                                &lpc->opb_isa_io);
    memory_region_init_alias(&lpc->opb_isa_mem, OBJECT(dev), "lpc-isa-mem",
                             &lpc->isa_mem, 0, LPC_MEM_OPB_SIZE);
    memory_region_add_subregion(&lpc->opb_mr, LPC_MEM_OPB_ADDR,
                                &lpc->opb_isa_mem);
    memory_region_init_alias(&lpc->opb_isa_fw, OBJECT(dev), "lpc-isa-fw",
                             &lpc->isa_fw, 0, LPC_FW_OPB_SIZE);
    memory_region_add_subregion(&lpc->opb_mr, LPC_FW_OPB_ADDR,
                                &lpc->opb_isa_fw);

    /* Create MMIO regions for LPC HC and OPB registers */
    memory_region_init_io(&lpc->opb_master_regs, OBJECT(dev), &opb_master_ops,
                          lpc, "lpc-opb-master", LPC_OPB_REGS_OPB_SIZE);
    memory_region_add_subregion(&lpc->opb_mr, LPC_OPB_REGS_OPB_ADDR,
                                &lpc->opb_master_regs);
    memory_region_init_io(&lpc->lpc_hc_regs, OBJECT(dev), &lpc_hc_ops, lpc,
                          "lpc-hc", LPC_HC_REGS_OPB_SIZE);
    memory_region_add_subregion(&lpc->opb_mr, LPC_HC_REGS_OPB_ADDR,
                                &lpc->lpc_hc_regs);

    qdev_init_gpio_out(DEVICE(dev), &lpc->psi_irq, 1);
}

static void pnv_lpc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pnv_lpc_realize;
    dc->desc = "PowerNV LPC Controller";
    dc->user_creatable = false;
}

static const TypeInfo pnv_lpc_info = {
    .name          = TYPE_PNV_LPC,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvLpcController),
    .class_init    = pnv_lpc_class_init,
    .class_size    = sizeof(PnvLpcClass),
    .abstract      = true,
};

static void pnv_lpc_register_types(void)
{
    type_register_static(&pnv_lpc_info);
    type_register_static(&pnv_lpc_power8_info);
    type_register_static(&pnv_lpc_power9_info);
    type_register_static(&pnv_lpc_power10_info);
}

type_init(pnv_lpc_register_types)

/* If we don't use the built-in LPC interrupt deserializer, we need
 * to provide a set of qirqs for the ISA bus or things will go bad.
 *
 * Most machines using pre-Naples chips (without said deserializer)
 * have a CPLD that will collect the SerIRQ and shoot them as a
 * single level interrupt to the P8 chip. So let's setup a hook
 * for doing just that.
 */
static void pnv_lpc_isa_irq_handler_cpld(void *opaque, int n, int level)
{
    PnvMachineState *pnv = PNV_MACHINE(qdev_get_machine());
    uint32_t old_state = pnv->cpld_irqstate;
    PnvLpcController *lpc = PNV_LPC(opaque);

    if (level) {
        pnv->cpld_irqstate |= 1u << n;
    } else {
        pnv->cpld_irqstate &= ~(1u << n);
    }

    if (pnv->cpld_irqstate != old_state) {
        qemu_set_irq(lpc->psi_irq, pnv->cpld_irqstate != 0);
    }
}

static void pnv_lpc_isa_irq_handler(void *opaque, int n, int level)
{
    PnvLpcController *lpc = PNV_LPC(opaque);

    /* The Naples HW latches the 1 levels, clearing is done by SW */
    if (level) {
        lpc->lpc_hc_irqstat |= LPC_HC_IRQ_SERIRQ0 >> n;
        pnv_lpc_eval_irqs(lpc);
    }
}

ISABus *pnv_lpc_isa_create(PnvLpcController *lpc, bool use_cpld, Error **errp)
{
    Error *local_err = NULL;
    ISABus *isa_bus;
    qemu_irq *irqs;
    qemu_irq_handler handler;

    /* let isa_bus_new() create its own bridge on SysBus otherwise
     * devices specified on the command line won't find the bus and
     * will fail to create.
     */
    isa_bus = isa_bus_new(NULL, &lpc->isa_mem, &lpc->isa_io, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return NULL;
    }

    /* Not all variants have a working serial irq decoder. If not,
     * handling of LPC interrupts becomes a platform issue (some
     * platforms have a CPLD to do it).
     */
    if (use_cpld) {
        handler = pnv_lpc_isa_irq_handler_cpld;
    } else {
        handler = pnv_lpc_isa_irq_handler;
    }

    irqs = qemu_allocate_irqs(handler, lpc, ISA_NUM_IRQS);

    isa_bus_irqs(isa_bus, irqs);

    return isa_bus;
}
