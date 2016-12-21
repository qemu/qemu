/*
 * QEMU PowerPC PowerNV LPC controller
 *
 * Copyright (c) 2016, IBM Corporation.
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

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "target/ppc/cpu.h"
#include "qapi/error.h"
#include "qemu/log.h"

#include "hw/ppc/pnv.h"
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
#define LPC_IO_OPB_ADDR         0xd0010000
#define LPC_IO_OPB_SIZE         0x00010000
#define LPC_MEM_OPB_ADDR        0xe0010000
#define LPC_MEM_OPB_SIZE        0x10000000
#define LPC_FW_OPB_ADDR         0xf0000000
#define LPC_FW_OPB_SIZE         0x10000000

#define LPC_OPB_REGS_OPB_ADDR   0xc0010000
#define LPC_OPB_REGS_OPB_SIZE   0x00002000
#define LPC_HC_REGS_OPB_ADDR    0xc0012000
#define LPC_HC_REGS_OPB_SIZE    0x00001000


/*
 * TODO: the "primary" cell should only be added on chip 0. This is
 * how skiboot chooses the default LPC controller on multichip
 * systems.
 *
 * It would be easly done if we can change the populate() interface to
 * replace the PnvXScomInterface parameter by a PnvChip one
 */
static int pnv_lpc_populate(PnvXScomInterface *dev, void *fdt, int xscom_offset)
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
    _FDT((fdt_setprop(fdt, offset, "primary", NULL, 0)));
    _FDT((fdt_setprop(fdt, offset, "compatible", compat, sizeof(compat))));
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
    bool success;

    /* XXX Handle access size limits and FW read caching here */
    success = !address_space_rw(&lpc->opb_as, addr, MEMTXATTRS_UNSPECIFIED,
                                data, sz, false);

    return success;
}

static bool opb_write(PnvLpcController *lpc, uint32_t addr, uint8_t *data,
                      int sz)
{
    bool success;

    /* XXX Handle access size limits here */
    success = !address_space_rw(&lpc->opb_as, addr, MEMTXATTRS_UNSPECIFIED,
                                data, sz, true);

    return success;
}

#define ECCB_CTL_READ           (1ull << (63 - 15))
#define ECCB_CTL_SZ_LSH         (63 - 7)
#define ECCB_CTL_SZ_MASK        (0xfull << ECCB_CTL_SZ_LSH)
#define ECCB_CTL_ADDR_MASK      0xffffffffu;

#define ECCB_STAT_OP_DONE       (1ull << (63 - 52))
#define ECCB_STAT_OP_ERR        (1ull << (63 - 52))
#define ECCB_STAT_RD_DATA_LSH   (63 - 37)
#define ECCB_STAT_RD_DATA_MASK  (0xffffffff << ECCB_STAT_RD_DATA_LSH)

static void pnv_lpc_do_eccb(PnvLpcController *lpc, uint64_t cmd)
{
    /* XXX Check for magic bits at the top, addr size etc... */
    unsigned int sz = (cmd & ECCB_CTL_SZ_MASK) >> ECCB_CTL_SZ_LSH;
    uint32_t opb_addr = cmd & ECCB_CTL_ADDR_MASK;
    uint8_t data[4];
    bool success;

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
        qemu_log_mask(LOG_UNIMP, "LPC HC Unimplemented register: Ox%"
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
        break;
    case LPC_HC_IRQMASK:
        lpc->lpc_hc_irqmask = val;
        break;
    case LPC_HC_IRQSTAT:
        lpc->lpc_hc_irqstat &= ~val;
        break;
    case LPC_HC_ERROR_ADDRESS:
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "LPC HC Unimplemented register: Ox%"
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
        qemu_log_mask(LOG_UNIMP, "OPB MASTER Unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr);
    }

    return val;
}

static void opb_master_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
    PnvLpcController *lpc = opaque;

    switch (addr) {
    case OPB_MASTER_LS_IRQ_STAT:
        lpc->opb_irq_stat &= ~val;
        break;
    case OPB_MASTER_LS_IRQ_MASK:
        /* XXX Filter out reserved bits */
        lpc->opb_irq_mask = val;
        break;
    case OPB_MASTER_LS_IRQ_POL:
        /* XXX Filter out reserved bits */
        lpc->opb_irq_pol = val;
        break;
    case OPB_MASTER_LS_IRQ_INPUT:
        /* Read only */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "OPB MASTER Unimplemented register: Ox%"
                      HWADDR_PRIx "\n", addr);
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
                             &lpc->isa_mem, 0, LPC_FW_OPB_SIZE);
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

    /* XScom region for LPC registers */
    pnv_xscom_region_init(&lpc->xscom_regs, OBJECT(dev),
                          &pnv_lpc_xscom_ops, lpc, "xscom-lpc",
                          PNV_XSCOM_LPC_SIZE);
}

static void pnv_lpc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xdc = PNV_XSCOM_INTERFACE_CLASS(klass);

    xdc->populate = pnv_lpc_populate;

    dc->realize = pnv_lpc_realize;
}

static const TypeInfo pnv_lpc_info = {
    .name          = TYPE_PNV_LPC,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvLpcController),
    .class_init    = pnv_lpc_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_lpc_register_types(void)
{
    type_register_static(&pnv_lpc_info);
}

type_init(pnv_lpc_register_types)
