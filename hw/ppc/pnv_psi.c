/*
 * QEMU PowerPC PowerNV Processor Service Interface (PSI) model
 *
 * Copyright (c) 2015-2017, IBM Corporation.
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
#include "hw/irq.h"
#include "target/ppc/cpu.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "sysemu/reset.h"
#include "qapi/error.h"
#include "monitor/monitor.h"


#include "hw/ppc/fdt.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/pnv_psi.h"

#include <libfdt.h>

#define PSIHB_XSCOM_FIR_RW      0x00
#define PSIHB_XSCOM_FIR_AND     0x01
#define PSIHB_XSCOM_FIR_OR      0x02
#define PSIHB_XSCOM_FIRMASK_RW  0x03
#define PSIHB_XSCOM_FIRMASK_AND 0x04
#define PSIHB_XSCOM_FIRMASK_OR  0x05
#define PSIHB_XSCOM_FIRACT0     0x06
#define PSIHB_XSCOM_FIRACT1     0x07

/* Host Bridge Base Address Register */
#define PSIHB_XSCOM_BAR         0x0a
#define   PSIHB_BAR_EN                  0x0000000000000001ull

/* FSP Base Address Register */
#define PSIHB_XSCOM_FSPBAR      0x0b

/* PSI Host Bridge Control/Status Register */
#define PSIHB_XSCOM_CR          0x0e
#define   PSIHB_CR_FSP_CMD_ENABLE       0x8000000000000000ull
#define   PSIHB_CR_FSP_MMIO_ENABLE      0x4000000000000000ull
#define   PSIHB_CR_FSP_IRQ_ENABLE       0x1000000000000000ull
#define   PSIHB_CR_FSP_ERR_RSP_ENABLE   0x0800000000000000ull
#define   PSIHB_CR_PSI_LINK_ENABLE      0x0400000000000000ull
#define   PSIHB_CR_FSP_RESET            0x0200000000000000ull
#define   PSIHB_CR_PSIHB_RESET          0x0100000000000000ull
#define   PSIHB_CR_PSI_IRQ              0x0000800000000000ull
#define   PSIHB_CR_FSP_IRQ              0x0000400000000000ull
#define   PSIHB_CR_FSP_LINK_ACTIVE      0x0000200000000000ull
#define   PSIHB_CR_IRQ_CMD_EXPECT       0x0000010000000000ull
          /* and more ... */

/* PSIHB Status / Error Mask Register */
#define PSIHB_XSCOM_SEMR        0x0f

/* XIVR, to signal interrupts to the CEC firmware. more XIVR below. */
#define PSIHB_XSCOM_XIVR_FSP    0x10
#define   PSIHB_XIVR_SERVER_SH          40
#define   PSIHB_XIVR_SERVER_MSK         (0xffffull << PSIHB_XIVR_SERVER_SH)
#define   PSIHB_XIVR_PRIO_SH            32
#define   PSIHB_XIVR_PRIO_MSK           (0xffull << PSIHB_XIVR_PRIO_SH)
#define   PSIHB_XIVR_SRC_SH             29
#define   PSIHB_XIVR_SRC_MSK            (0x7ull << PSIHB_XIVR_SRC_SH)
#define   PSIHB_XIVR_PENDING            0x01000000ull

/* PSI Host Bridge Set Control/ Status Register */
#define PSIHB_XSCOM_SCR         0x12

/* PSI Host Bridge Clear Control/ Status Register */
#define PSIHB_XSCOM_CCR         0x13

/* DMA Upper Address Register */
#define PSIHB_XSCOM_DMA_UPADD   0x14

/* Interrupt Status */
#define PSIHB_XSCOM_IRQ_STAT    0x15
#define   PSIHB_IRQ_STAT_OCC            0x0000001000000000ull
#define   PSIHB_IRQ_STAT_FSI            0x0000000800000000ull
#define   PSIHB_IRQ_STAT_LPCI2C         0x0000000400000000ull
#define   PSIHB_IRQ_STAT_LOCERR         0x0000000200000000ull
#define   PSIHB_IRQ_STAT_EXT            0x0000000100000000ull

/* remaining XIVR */
#define PSIHB_XSCOM_XIVR_OCC    0x16
#define PSIHB_XSCOM_XIVR_FSI    0x17
#define PSIHB_XSCOM_XIVR_LPCI2C 0x18
#define PSIHB_XSCOM_XIVR_LOCERR 0x19
#define PSIHB_XSCOM_XIVR_EXT    0x1a

/* Interrupt Requester Source Compare Register */
#define PSIHB_XSCOM_IRSN        0x1b
#define   PSIHB_IRSN_COMP_SH            45
#define   PSIHB_IRSN_COMP_MSK           (0x7ffffull << PSIHB_IRSN_COMP_SH)
#define   PSIHB_IRSN_IRQ_MUX            0x0000000800000000ull
#define   PSIHB_IRSN_IRQ_RESET          0x0000000400000000ull
#define   PSIHB_IRSN_DOWNSTREAM_EN      0x0000000200000000ull
#define   PSIHB_IRSN_UPSTREAM_EN        0x0000000100000000ull
#define   PSIHB_IRSN_COMPMASK_SH        13
#define   PSIHB_IRSN_COMPMASK_MSK       (0x7ffffull << PSIHB_IRSN_COMPMASK_SH)

#define PSIHB_BAR_MASK                  0x0003fffffff00000ull
#define PSIHB_FSPBAR_MASK               0x0003ffff00000000ull

#define PSIHB9_BAR_MASK                 0x00fffffffff00000ull
#define PSIHB9_FSPBAR_MASK              0x00ffffff00000000ull

#define PSIHB_REG(addr) (((addr) >> 3) + PSIHB_XSCOM_BAR)

static void pnv_psi_set_bar(PnvPsi *psi, uint64_t bar)
{
    PnvPsiClass *ppc = PNV_PSI_GET_CLASS(psi);
    MemoryRegion *sysmem = get_system_memory();
    uint64_t old = psi->regs[PSIHB_XSCOM_BAR];

    psi->regs[PSIHB_XSCOM_BAR] = bar & (ppc->bar_mask | PSIHB_BAR_EN);

    /* Update MR, always remove it first */
    if (old & PSIHB_BAR_EN) {
        memory_region_del_subregion(sysmem, &psi->regs_mr);
    }

    /* Then add it back if needed */
    if (bar & PSIHB_BAR_EN) {
        uint64_t addr = bar & ppc->bar_mask;
        memory_region_add_subregion(sysmem, addr, &psi->regs_mr);
    }
}

static void pnv_psi_update_fsp_mr(PnvPsi *psi)
{
    /* TODO: Update FSP MR if/when we support FSP BAR */
}

static void pnv_psi_set_cr(PnvPsi *psi, uint64_t cr)
{
    uint64_t old = psi->regs[PSIHB_XSCOM_CR];

    psi->regs[PSIHB_XSCOM_CR] = cr;

    /* Check some bit changes */
    if ((old ^ psi->regs[PSIHB_XSCOM_CR]) & PSIHB_CR_FSP_MMIO_ENABLE) {
        pnv_psi_update_fsp_mr(psi);
    }
}

static void pnv_psi_set_irsn(PnvPsi *psi, uint64_t val)
{
    ICSState *ics = &PNV8_PSI(psi)->ics;

    /* In this model we ignore the up/down enable bits for now
     * as SW doesn't use them (other than setting them at boot).
     * We ignore IRQ_MUX, its meaning isn't clear and we don't use
     * it and finally we ignore reset (XXX fix that ?)
     */
    psi->regs[PSIHB_XSCOM_IRSN] = val & (PSIHB_IRSN_COMP_MSK |
                                         PSIHB_IRSN_IRQ_MUX |
                                         PSIHB_IRSN_IRQ_RESET |
                                         PSIHB_IRSN_DOWNSTREAM_EN |
                                         PSIHB_IRSN_UPSTREAM_EN);

    /* We ignore the compare mask as well, our ICS emulation is too
     * simplistic to make any use if it, and we extract the offset
     * from the compare value
     */
    ics->offset = (val & PSIHB_IRSN_COMP_MSK) >> PSIHB_IRSN_COMP_SH;
}

/*
 * FSP and PSI interrupts are muxed under the same number.
 */
static const uint32_t xivr_regs[] = {
    [PSIHB_IRQ_PSI]       = PSIHB_XSCOM_XIVR_FSP,
    [PSIHB_IRQ_FSP]       = PSIHB_XSCOM_XIVR_FSP,
    [PSIHB_IRQ_OCC]       = PSIHB_XSCOM_XIVR_OCC,
    [PSIHB_IRQ_FSI]       = PSIHB_XSCOM_XIVR_FSI,
    [PSIHB_IRQ_LPC_I2C]   = PSIHB_XSCOM_XIVR_LPCI2C,
    [PSIHB_IRQ_LOCAL_ERR] = PSIHB_XSCOM_XIVR_LOCERR,
    [PSIHB_IRQ_EXTERNAL]  = PSIHB_XSCOM_XIVR_EXT,
};

static const uint32_t stat_regs[] = {
    [PSIHB_IRQ_PSI]       = PSIHB_XSCOM_CR,
    [PSIHB_IRQ_FSP]       = PSIHB_XSCOM_CR,
    [PSIHB_IRQ_OCC]       = PSIHB_XSCOM_IRQ_STAT,
    [PSIHB_IRQ_FSI]       = PSIHB_XSCOM_IRQ_STAT,
    [PSIHB_IRQ_LPC_I2C]   = PSIHB_XSCOM_IRQ_STAT,
    [PSIHB_IRQ_LOCAL_ERR] = PSIHB_XSCOM_IRQ_STAT,
    [PSIHB_IRQ_EXTERNAL]  = PSIHB_XSCOM_IRQ_STAT,
};

static const uint64_t stat_bits[] = {
    [PSIHB_IRQ_PSI]       = PSIHB_CR_PSI_IRQ,
    [PSIHB_IRQ_FSP]       = PSIHB_CR_FSP_IRQ,
    [PSIHB_IRQ_OCC]       = PSIHB_IRQ_STAT_OCC,
    [PSIHB_IRQ_FSI]       = PSIHB_IRQ_STAT_FSI,
    [PSIHB_IRQ_LPC_I2C]   = PSIHB_IRQ_STAT_LPCI2C,
    [PSIHB_IRQ_LOCAL_ERR] = PSIHB_IRQ_STAT_LOCERR,
    [PSIHB_IRQ_EXTERNAL]  = PSIHB_IRQ_STAT_EXT,
};

void pnv_psi_irq_set(PnvPsi *psi, int irq, bool state)
{
    PNV_PSI_GET_CLASS(psi)->irq_set(psi, irq, state);
}

static void pnv_psi_power8_irq_set(PnvPsi *psi, int irq, bool state)
{
    uint32_t xivr_reg;
    uint32_t stat_reg;
    uint32_t src;
    bool masked;

    if (irq > PSIHB_IRQ_EXTERNAL) {
        qemu_log_mask(LOG_GUEST_ERROR, "PSI: Unsupported irq %d\n", irq);
        return;
    }

    xivr_reg = xivr_regs[irq];
    stat_reg = stat_regs[irq];

    src = (psi->regs[xivr_reg] & PSIHB_XIVR_SRC_MSK) >> PSIHB_XIVR_SRC_SH;
    if (state) {
        psi->regs[stat_reg] |= stat_bits[irq];
        /* TODO: optimization, check mask here. That means
         * re-evaluating when unmasking
         */
        qemu_irq_raise(psi->qirqs[src]);
    } else {
        psi->regs[stat_reg] &= ~stat_bits[irq];

        /* FSP and PSI are muxed so don't lower if either is still set */
        if (stat_reg != PSIHB_XSCOM_CR ||
            !(psi->regs[stat_reg] & (PSIHB_CR_PSI_IRQ | PSIHB_CR_FSP_IRQ))) {
            qemu_irq_lower(psi->qirqs[src]);
        } else {
            state = true;
        }
    }

    /* Note about the emulation of the pending bit: This isn't
     * entirely correct. The pending bit should be cleared when the
     * EOI has been received. However, we don't have callbacks on EOI
     * (especially not under KVM) so no way to emulate that properly,
     * so instead we just set that bit as the logical "output" of the
     * XIVR (ie pending & !masked)
     *
     * CLG: We could define a new ICS object with a custom eoi()
     * handler to clear the pending bit. But I am not sure this would
     * be useful for the software anyhow.
     */
    masked = (psi->regs[xivr_reg] & PSIHB_XIVR_PRIO_MSK) == PSIHB_XIVR_PRIO_MSK;
    if (state && !masked) {
        psi->regs[xivr_reg] |= PSIHB_XIVR_PENDING;
    } else {
        psi->regs[xivr_reg] &= ~PSIHB_XIVR_PENDING;
    }
}

static void pnv_psi_set_xivr(PnvPsi *psi, uint32_t reg, uint64_t val)
{
    ICSState *ics = &PNV8_PSI(psi)->ics;
    uint16_t server;
    uint8_t prio;
    uint8_t src;

    psi->regs[reg] = (psi->regs[reg] & PSIHB_XIVR_PENDING) |
            (val & (PSIHB_XIVR_SERVER_MSK |
                    PSIHB_XIVR_PRIO_MSK |
                    PSIHB_XIVR_SRC_MSK));
    val = psi->regs[reg];
    server = (val & PSIHB_XIVR_SERVER_MSK) >> PSIHB_XIVR_SERVER_SH;
    prio = (val & PSIHB_XIVR_PRIO_MSK) >> PSIHB_XIVR_PRIO_SH;
    src = (val & PSIHB_XIVR_SRC_MSK) >> PSIHB_XIVR_SRC_SH;

    if (src >= PSI_NUM_INTERRUPTS) {
        qemu_log_mask(LOG_GUEST_ERROR, "PSI: Unsupported irq %d\n", src);
        return;
    }

    /* Remove pending bit if the IRQ is masked */
    if ((psi->regs[reg] & PSIHB_XIVR_PRIO_MSK) == PSIHB_XIVR_PRIO_MSK) {
        psi->regs[reg] &= ~PSIHB_XIVR_PENDING;
    }

    /* The low order 2 bits are the link pointer (Type II interrupts).
     * Shift back to get a valid IRQ server.
     */
    server >>= 2;

    /* Now because of source remapping, weird things can happen
     * if you change the source number dynamically, our simple ICS
     * doesn't deal with remapping. So we just poke a different
     * ICS entry based on what source number was written. This will
     * do for now but a more accurate implementation would instead
     * use a fixed server/prio and a remapper of the generated irq.
     */
    ics_write_xive(ics, src, server, prio, prio);
}

static uint64_t pnv_psi_reg_read(PnvPsi *psi, uint32_t offset, bool mmio)
{
    uint64_t val = 0xffffffffffffffffull;

    switch (offset) {
    case PSIHB_XSCOM_FIR_RW:
    case PSIHB_XSCOM_FIRACT0:
    case PSIHB_XSCOM_FIRACT1:
    case PSIHB_XSCOM_BAR:
    case PSIHB_XSCOM_FSPBAR:
    case PSIHB_XSCOM_CR:
    case PSIHB_XSCOM_XIVR_FSP:
    case PSIHB_XSCOM_XIVR_OCC:
    case PSIHB_XSCOM_XIVR_FSI:
    case PSIHB_XSCOM_XIVR_LPCI2C:
    case PSIHB_XSCOM_XIVR_LOCERR:
    case PSIHB_XSCOM_XIVR_EXT:
    case PSIHB_XSCOM_IRQ_STAT:
    case PSIHB_XSCOM_SEMR:
    case PSIHB_XSCOM_DMA_UPADD:
    case PSIHB_XSCOM_IRSN:
        val = psi->regs[offset];
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "PSI: read at 0x%" PRIx32 "\n", offset);
    }
    return val;
}

static void pnv_psi_reg_write(PnvPsi *psi, uint32_t offset, uint64_t val,
                              bool mmio)
{
    switch (offset) {
    case PSIHB_XSCOM_FIR_RW:
    case PSIHB_XSCOM_FIRACT0:
    case PSIHB_XSCOM_FIRACT1:
    case PSIHB_XSCOM_SEMR:
    case PSIHB_XSCOM_DMA_UPADD:
        psi->regs[offset] = val;
        break;
    case PSIHB_XSCOM_FIR_OR:
        psi->regs[PSIHB_XSCOM_FIR_RW] |= val;
        break;
    case PSIHB_XSCOM_FIR_AND:
        psi->regs[PSIHB_XSCOM_FIR_RW] &= val;
        break;
    case PSIHB_XSCOM_BAR:
        /* Only XSCOM can write this one */
        if (!mmio) {
            pnv_psi_set_bar(psi, val);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "PSI: invalid write of BAR\n");
        }
        break;
    case PSIHB_XSCOM_FSPBAR:
        psi->regs[PSIHB_XSCOM_FSPBAR] = val & PSIHB_FSPBAR_MASK;
        pnv_psi_update_fsp_mr(psi);
        break;
    case PSIHB_XSCOM_CR:
        pnv_psi_set_cr(psi, val);
        break;
    case PSIHB_XSCOM_SCR:
        pnv_psi_set_cr(psi, psi->regs[PSIHB_XSCOM_CR] | val);
        break;
    case PSIHB_XSCOM_CCR:
        pnv_psi_set_cr(psi, psi->regs[PSIHB_XSCOM_CR] & ~val);
        break;
    case PSIHB_XSCOM_XIVR_FSP:
    case PSIHB_XSCOM_XIVR_OCC:
    case PSIHB_XSCOM_XIVR_FSI:
    case PSIHB_XSCOM_XIVR_LPCI2C:
    case PSIHB_XSCOM_XIVR_LOCERR:
    case PSIHB_XSCOM_XIVR_EXT:
        pnv_psi_set_xivr(psi, offset, val);
        break;
    case PSIHB_XSCOM_IRQ_STAT:
        /* Read only */
        qemu_log_mask(LOG_GUEST_ERROR, "PSI: invalid write of IRQ_STAT\n");
        break;
    case PSIHB_XSCOM_IRSN:
        pnv_psi_set_irsn(psi, val);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "PSI: write at 0x%" PRIx32 "\n", offset);
    }
}

/*
 * The values of the registers when accessed through the MMIO region
 * follow the relation : xscom = (mmio + 0x50) >> 3
 */
static uint64_t pnv_psi_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pnv_psi_reg_read(opaque, PSIHB_REG(addr), true);
}

static void pnv_psi_mmio_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned size)
{
    pnv_psi_reg_write(opaque, PSIHB_REG(addr), val, true);
}

static const MemoryRegionOps psi_mmio_ops = {
    .read = pnv_psi_mmio_read,
    .write = pnv_psi_mmio_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

static uint64_t pnv_psi_xscom_read(void *opaque, hwaddr addr, unsigned size)
{
    return pnv_psi_reg_read(opaque, addr >> 3, false);
}

static void pnv_psi_xscom_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
    pnv_psi_reg_write(opaque, addr >> 3, val, false);
}

static const MemoryRegionOps pnv_psi_xscom_ops = {
    .read = pnv_psi_xscom_read,
    .write = pnv_psi_xscom_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    }
};

static void pnv_psi_reset(DeviceState *dev)
{
    PnvPsi *psi = PNV_PSI(dev);

    memset(psi->regs, 0x0, sizeof(psi->regs));

    psi->regs[PSIHB_XSCOM_BAR] = psi->bar | PSIHB_BAR_EN;
}

static void pnv_psi_reset_handler(void *dev)
{
    device_cold_reset(DEVICE(dev));
}

static void pnv_psi_realize(DeviceState *dev, Error **errp)
{
    PnvPsi *psi = PNV_PSI(dev);

    /* Default BAR for MMIO region */
    pnv_psi_set_bar(psi, psi->bar | PSIHB_BAR_EN);

    qemu_register_reset(pnv_psi_reset_handler, dev);
}

static void pnv_psi_power8_instance_init(Object *obj)
{
    Pnv8Psi *psi8 = PNV8_PSI(obj);

    object_initialize_child(obj, "ics-psi", &psi8->ics, TYPE_ICS);
    object_property_add_alias(obj, ICS_PROP_XICS, OBJECT(&psi8->ics),
                              ICS_PROP_XICS);
}

static const uint8_t irq_to_xivr[] = {
    PSIHB_XSCOM_XIVR_FSP,
    PSIHB_XSCOM_XIVR_OCC,
    PSIHB_XSCOM_XIVR_FSI,
    PSIHB_XSCOM_XIVR_LPCI2C,
    PSIHB_XSCOM_XIVR_LOCERR,
    PSIHB_XSCOM_XIVR_EXT,
};

static void pnv_psi_power8_realize(DeviceState *dev, Error **errp)
{
    PnvPsi *psi = PNV_PSI(dev);
    ICSState *ics = &PNV8_PSI(psi)->ics;
    unsigned int i;

    /* Create PSI interrupt control source */
    if (!object_property_set_int(OBJECT(ics), "nr-irqs", PSI_NUM_INTERRUPTS,
                                 errp)) {
        return;
    }
    if (!qdev_realize(DEVICE(ics), NULL, errp)) {
        return;
    }

    for (i = 0; i < ics->nr_irqs; i++) {
        ics_set_irq_type(ics, i, true);
    }

    psi->qirqs = qemu_allocate_irqs(ics_set_irq, ics, ics->nr_irqs);

    /* XSCOM region for PSI registers */
    pnv_xscom_region_init(&psi->xscom_regs, OBJECT(dev), &pnv_psi_xscom_ops,
                psi, "xscom-psi", PNV_XSCOM_PSIHB_SIZE);

    /* Initialize MMIO region */
    memory_region_init_io(&psi->regs_mr, OBJECT(dev), &psi_mmio_ops, psi,
                          "psihb", PNV_PSIHB_SIZE);

    /* Default sources in XIVR */
    for (i = 0; i < PSI_NUM_INTERRUPTS; i++) {
        uint8_t xivr = irq_to_xivr[i];
        psi->regs[xivr] = PSIHB_XIVR_PRIO_MSK |
            ((uint64_t) i << PSIHB_XIVR_SRC_SH);
    }

    pnv_psi_realize(dev, errp);
}

static int pnv_psi_dt_xscom(PnvXScomInterface *dev, void *fdt, int xscom_offset)
{
    PnvPsiClass *ppc = PNV_PSI_GET_CLASS(dev);
    char *name;
    int offset;
    uint32_t reg[] = {
        cpu_to_be32(ppc->xscom_pcba),
        cpu_to_be32(ppc->xscom_size)
    };

    name = g_strdup_printf("psihb@%x", ppc->xscom_pcba);
    offset = fdt_add_subnode(fdt, xscom_offset, name);
    _FDT(offset);
    g_free(name);

    _FDT(fdt_setprop(fdt, offset, "reg", reg, sizeof(reg)));
    _FDT(fdt_setprop_cell(fdt, offset, "#address-cells", 2));
    _FDT(fdt_setprop_cell(fdt, offset, "#size-cells", 1));
    _FDT(fdt_setprop(fdt, offset, "compatible", ppc->compat,
                     ppc->compat_size));
    return 0;
}

static Property pnv_psi_properties[] = {
    DEFINE_PROP_UINT64("bar", PnvPsi, bar, 0),
    DEFINE_PROP_UINT64("fsp-bar", PnvPsi, fsp_bar, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_psi_power8_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvPsiClass *ppc = PNV_PSI_CLASS(klass);
    static const char compat[] = "ibm,power8-psihb-x\0ibm,psihb-x";

    dc->desc    = "PowerNV PSI Controller POWER8";
    dc->realize = pnv_psi_power8_realize;

    ppc->xscom_pcba = PNV_XSCOM_PSIHB_BASE;
    ppc->xscom_size = PNV_XSCOM_PSIHB_SIZE;
    ppc->bar_mask   = PSIHB_BAR_MASK;
    ppc->irq_set    = pnv_psi_power8_irq_set;
    ppc->compat     = compat;
    ppc->compat_size = sizeof(compat);
}

static const TypeInfo pnv_psi_power8_info = {
    .name          = TYPE_PNV8_PSI,
    .parent        = TYPE_PNV_PSI,
    .instance_size = sizeof(Pnv8Psi),
    .instance_init = pnv_psi_power8_instance_init,
    .class_init    = pnv_psi_power8_class_init,
};


/* Common registers */

#define PSIHB9_CR                       0x20
#define PSIHB9_SEMR                     0x28

/* P9 registers */

#define PSIHB9_INTERRUPT_CONTROL        0x58
#define   PSIHB9_IRQ_METHOD             PPC_BIT(0)
#define   PSIHB9_IRQ_RESET              PPC_BIT(1)
#define PSIHB9_ESB_CI_BASE              0x60
#define   PSIHB9_ESB_CI_ADDR_MASK       PPC_BITMASK(8, 47)
#define   PSIHB9_ESB_CI_VALID           PPC_BIT(63)
#define PSIHB9_ESB_NOTIF_ADDR           0x68
#define   PSIHB9_ESB_NOTIF_ADDR_MASK    PPC_BITMASK(8, 60)
#define   PSIHB9_ESB_NOTIF_VALID        PPC_BIT(63)
#define PSIHB9_IVT_OFFSET               0x70
#define   PSIHB9_IVT_OFF_SHIFT          32

#define PSIHB9_IRQ_LEVEL                0x78 /* assertion */
#define   PSIHB9_IRQ_LEVEL_PSI          PPC_BIT(0)
#define   PSIHB9_IRQ_LEVEL_OCC          PPC_BIT(1)
#define   PSIHB9_IRQ_LEVEL_FSI          PPC_BIT(2)
#define   PSIHB9_IRQ_LEVEL_LPCHC        PPC_BIT(3)
#define   PSIHB9_IRQ_LEVEL_LOCAL_ERR    PPC_BIT(4)
#define   PSIHB9_IRQ_LEVEL_GLOBAL_ERR   PPC_BIT(5)
#define   PSIHB9_IRQ_LEVEL_TPM          PPC_BIT(6)
#define   PSIHB9_IRQ_LEVEL_LPC_SIRQ1    PPC_BIT(7)
#define   PSIHB9_IRQ_LEVEL_LPC_SIRQ2    PPC_BIT(8)
#define   PSIHB9_IRQ_LEVEL_LPC_SIRQ3    PPC_BIT(9)
#define   PSIHB9_IRQ_LEVEL_LPC_SIRQ4    PPC_BIT(10)
#define   PSIHB9_IRQ_LEVEL_SBE_I2C      PPC_BIT(11)
#define   PSIHB9_IRQ_LEVEL_DIO          PPC_BIT(12)
#define   PSIHB9_IRQ_LEVEL_PSU          PPC_BIT(13)
#define   PSIHB9_IRQ_LEVEL_I2C_C        PPC_BIT(14)
#define   PSIHB9_IRQ_LEVEL_I2C_D        PPC_BIT(15)
#define   PSIHB9_IRQ_LEVEL_I2C_E        PPC_BIT(16)
#define   PSIHB9_IRQ_LEVEL_SBE          PPC_BIT(19)

#define PSIHB9_IRQ_STAT                 0x80 /* P bit */
#define   PSIHB9_IRQ_STAT_PSI           PPC_BIT(0)
#define   PSIHB9_IRQ_STAT_OCC           PPC_BIT(1)
#define   PSIHB9_IRQ_STAT_FSI           PPC_BIT(2)
#define   PSIHB9_IRQ_STAT_LPCHC         PPC_BIT(3)
#define   PSIHB9_IRQ_STAT_LOCAL_ERR     PPC_BIT(4)
#define   PSIHB9_IRQ_STAT_GLOBAL_ERR    PPC_BIT(5)
#define   PSIHB9_IRQ_STAT_TPM           PPC_BIT(6)
#define   PSIHB9_IRQ_STAT_LPC_SIRQ1     PPC_BIT(7)
#define   PSIHB9_IRQ_STAT_LPC_SIRQ2     PPC_BIT(8)
#define   PSIHB9_IRQ_STAT_LPC_SIRQ3     PPC_BIT(9)
#define   PSIHB9_IRQ_STAT_LPC_SIRQ4     PPC_BIT(10)
#define   PSIHB9_IRQ_STAT_SBE_I2C       PPC_BIT(11)
#define   PSIHB9_IRQ_STAT_DIO           PPC_BIT(12)
#define   PSIHB9_IRQ_STAT_PSU           PPC_BIT(13)

/* P10 register extensions */

#define PSIHB10_CR                       PSIHB9_CR
#define    PSIHB10_CR_STORE_EOI          PPC_BIT(12)

#define PSIHB10_ESB_CI_BASE              PSIHB9_ESB_CI_BASE
#define   PSIHB10_ESB_CI_64K             PPC_BIT(1)

static void pnv_psi_notify(XiveNotifier *xf, uint32_t srcno, bool pq_checked)
{
    PnvPsi *psi = PNV_PSI(xf);
    uint64_t notif_port = psi->regs[PSIHB_REG(PSIHB9_ESB_NOTIF_ADDR)];
    bool valid = notif_port & PSIHB9_ESB_NOTIF_VALID;
    uint64_t notify_addr = notif_port & ~PSIHB9_ESB_NOTIF_VALID;

    uint32_t offset =
        (psi->regs[PSIHB_REG(PSIHB9_IVT_OFFSET)] >> PSIHB9_IVT_OFF_SHIFT);
    uint64_t data = offset | srcno;
    MemTxResult result;

    if (pq_checked) {
        data |= XIVE_TRIGGER_PQ;
    }

    if (!valid) {
        return;
    }

    address_space_stq_be(&address_space_memory, notify_addr, data,
                         MEMTXATTRS_UNSPECIFIED, &result);
    if (result != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: trigger failed @%"
                      HWADDR_PRIx "\n", __func__, notif_port);
        return;
    }
}

static uint64_t pnv_psi_p9_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvPsi *psi = PNV_PSI(opaque);
    uint32_t reg = PSIHB_REG(addr);
    uint64_t val = -1;

    switch (addr) {
    case PSIHB9_CR:
    case PSIHB9_SEMR:
        /* FSP stuff */
    case PSIHB9_INTERRUPT_CONTROL:
    case PSIHB9_ESB_CI_BASE:
    case PSIHB9_ESB_NOTIF_ADDR:
    case PSIHB9_IVT_OFFSET:
        val = psi->regs[reg];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "PSI: read at 0x%" PRIx64 "\n", addr);
    }

    return val;
}

static void pnv_psi_p9_mmio_write(void *opaque, hwaddr addr,
                                  uint64_t val, unsigned size)
{
    PnvPsi *psi = PNV_PSI(opaque);
    Pnv9Psi *psi9 = PNV9_PSI(psi);
    uint32_t reg = PSIHB_REG(addr);
    MemoryRegion *sysmem = get_system_memory();

    switch (addr) {
    case PSIHB9_CR:
        if (val & PSIHB10_CR_STORE_EOI) {
            psi9->source.esb_flags |= XIVE_SRC_STORE_EOI;
        } else {
            psi9->source.esb_flags &= ~XIVE_SRC_STORE_EOI;
        }
        break;

    case PSIHB9_SEMR:
        /* FSP stuff */
        break;
    case PSIHB9_INTERRUPT_CONTROL:
        if (val & PSIHB9_IRQ_RESET) {
            device_cold_reset(DEVICE(&psi9->source));
        }
        psi->regs[reg] = val;
        break;

    case PSIHB9_ESB_CI_BASE:
        if (val & PSIHB10_ESB_CI_64K) {
            psi9->source.esb_shift = XIVE_ESB_64K;
        } else {
            psi9->source.esb_shift = XIVE_ESB_4K;
        }
        if (!(val & PSIHB9_ESB_CI_VALID)) {
            if (psi->regs[reg] & PSIHB9_ESB_CI_VALID) {
                memory_region_del_subregion(sysmem, &psi9->source.esb_mmio);
            }
        } else {
            if (!(psi->regs[reg] & PSIHB9_ESB_CI_VALID)) {
                hwaddr addr = val & ~(PSIHB9_ESB_CI_VALID | PSIHB10_ESB_CI_64K);
                memory_region_add_subregion(sysmem, addr,
                                            &psi9->source.esb_mmio);
            }
        }
        psi->regs[reg] = val;
        break;

    case PSIHB9_ESB_NOTIF_ADDR:
        psi->regs[reg] = val;
        break;
    case PSIHB9_IVT_OFFSET:
        psi->regs[reg] = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "PSI: write at 0x%" PRIx64 "\n", addr);
    }
}

static const MemoryRegionOps pnv_psi_p9_mmio_ops = {
    .read = pnv_psi_p9_mmio_read,
    .write = pnv_psi_p9_mmio_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

static uint64_t pnv_psi_p9_xscom_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No read are expected */
    qemu_log_mask(LOG_GUEST_ERROR, "PSI: xscom read at 0x%" PRIx64 "\n", addr);
    return -1;
}

static void pnv_psi_p9_xscom_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
    PnvPsi *psi = PNV_PSI(opaque);

    /* XSCOM is only used to set the PSIHB MMIO region */
    switch (addr >> 3) {
    case PSIHB_XSCOM_BAR:
        pnv_psi_set_bar(psi, val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "PSI: xscom write at 0x%" PRIx64 "\n",
                      addr);
    }
}

static const MemoryRegionOps pnv_psi_p9_xscom_ops = {
    .read = pnv_psi_p9_xscom_read,
    .write = pnv_psi_p9_xscom_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    }
};

static void pnv_psi_power9_irq_set(PnvPsi *psi, int irq, bool state)
{
    uint64_t irq_method = psi->regs[PSIHB_REG(PSIHB9_INTERRUPT_CONTROL)];

    if (irq > PSIHB9_NUM_IRQS) {
        qemu_log_mask(LOG_GUEST_ERROR, "PSI: Unsupported irq %d\n", irq);
        return;
    }

    if (irq_method & PSIHB9_IRQ_METHOD) {
        qemu_log_mask(LOG_GUEST_ERROR, "PSI: LSI IRQ method no supported\n");
        return;
    }

    /* Update LSI levels */
    if (state) {
        psi->regs[PSIHB_REG(PSIHB9_IRQ_LEVEL)] |= PPC_BIT(irq);
    } else {
        psi->regs[PSIHB_REG(PSIHB9_IRQ_LEVEL)] &= ~PPC_BIT(irq);
    }

    qemu_set_irq(psi->qirqs[irq], state);
}

static void pnv_psi_power9_reset(DeviceState *dev)
{
    Pnv9Psi *psi = PNV9_PSI(dev);

    pnv_psi_reset(dev);

    if (memory_region_is_mapped(&psi->source.esb_mmio)) {
        memory_region_del_subregion(get_system_memory(), &psi->source.esb_mmio);
    }
}

static void pnv_psi_power9_instance_init(Object *obj)
{
    Pnv9Psi *psi = PNV9_PSI(obj);

    object_initialize_child(obj, "source", &psi->source, TYPE_XIVE_SOURCE);
    object_property_add_alias(obj, "shift", OBJECT(&psi->source), "shift");
}

static void pnv_psi_power9_realize(DeviceState *dev, Error **errp)
{
    PnvPsi *psi = PNV_PSI(dev);
    XiveSource *xsrc = &PNV9_PSI(psi)->source;
    int i;

    object_property_set_int(OBJECT(xsrc), "nr-irqs", PSIHB9_NUM_IRQS,
                            &error_fatal);
    object_property_set_link(OBJECT(xsrc), "xive", OBJECT(psi), &error_abort);
    if (!qdev_realize(DEVICE(xsrc), NULL, errp)) {
        return;
    }

    for (i = 0; i < xsrc->nr_irqs; i++) {
        xive_source_irq_set_lsi(xsrc, i);
    }

    psi->qirqs = qemu_allocate_irqs(xive_source_set_irq, xsrc, xsrc->nr_irqs);

    /* XSCOM region for PSI registers */
    pnv_xscom_region_init(&psi->xscom_regs, OBJECT(dev), &pnv_psi_p9_xscom_ops,
                psi, "xscom-psi", PNV9_XSCOM_PSIHB_SIZE);

    /* MMIO region for PSI registers */
    memory_region_init_io(&psi->regs_mr, OBJECT(dev), &pnv_psi_p9_mmio_ops, psi,
                          "psihb", PNV9_PSIHB_SIZE);

    pnv_psi_realize(dev, errp);
}

static void pnv_psi_power9_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvPsiClass *ppc = PNV_PSI_CLASS(klass);
    XiveNotifierClass *xfc = XIVE_NOTIFIER_CLASS(klass);
    static const char compat[] = "ibm,power9-psihb-x\0ibm,psihb-x";

    dc->desc    = "PowerNV PSI Controller POWER9";
    dc->realize = pnv_psi_power9_realize;
    dc->reset   = pnv_psi_power9_reset;

    ppc->xscom_pcba = PNV9_XSCOM_PSIHB_BASE;
    ppc->xscom_size = PNV9_XSCOM_PSIHB_SIZE;
    ppc->bar_mask   = PSIHB9_BAR_MASK;
    ppc->irq_set    = pnv_psi_power9_irq_set;
    ppc->compat     = compat;
    ppc->compat_size = sizeof(compat);

    xfc->notify      = pnv_psi_notify;
}

static const TypeInfo pnv_psi_power9_info = {
    .name          = TYPE_PNV9_PSI,
    .parent        = TYPE_PNV_PSI,
    .instance_size = sizeof(Pnv9Psi),
    .instance_init = pnv_psi_power9_instance_init,
    .class_init    = pnv_psi_power9_class_init,
    .interfaces = (InterfaceInfo[]) {
            { TYPE_XIVE_NOTIFIER },
            { },
    },
};

static void pnv_psi_power10_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvPsiClass *ppc = PNV_PSI_CLASS(klass);
    static const char compat[] = "ibm,power10-psihb-x\0ibm,psihb-x";

    dc->desc    = "PowerNV PSI Controller POWER10";

    ppc->xscom_pcba = PNV10_XSCOM_PSIHB_BASE;
    ppc->xscom_size = PNV10_XSCOM_PSIHB_SIZE;
    ppc->compat     = compat;
    ppc->compat_size = sizeof(compat);
}

static const TypeInfo pnv_psi_power10_info = {
    .name          = TYPE_PNV10_PSI,
    .parent        = TYPE_PNV9_PSI,
    .class_init    = pnv_psi_power10_class_init,
};

static void pnv_psi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvXScomInterfaceClass *xdc = PNV_XSCOM_INTERFACE_CLASS(klass);

    xdc->dt_xscom = pnv_psi_dt_xscom;

    dc->desc = "PowerNV PSI Controller";
    device_class_set_props(dc, pnv_psi_properties);
    dc->reset = pnv_psi_reset;
    dc->user_creatable = false;
}

static const TypeInfo pnv_psi_info = {
    .name          = TYPE_PNV_PSI,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvPsi),
    .class_init    = pnv_psi_class_init,
    .class_size    = sizeof(PnvPsiClass),
    .abstract      = true,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_PNV_XSCOM_INTERFACE },
        { }
    }
};

static void pnv_psi_register_types(void)
{
    type_register_static(&pnv_psi_info);
    type_register_static(&pnv_psi_power8_info);
    type_register_static(&pnv_psi_power9_info);
    type_register_static(&pnv_psi_power10_info);
}

type_init(pnv_psi_register_types);

void pnv_psi_pic_print_info(Pnv9Psi *psi9, Monitor *mon)
{
    PnvPsi *psi = PNV_PSI(psi9);

    uint32_t offset =
        (psi->regs[PSIHB_REG(PSIHB9_IVT_OFFSET)] >> PSIHB9_IVT_OFF_SHIFT);

    monitor_printf(mon, "PSIHB Source %08x .. %08x\n",
                  offset, offset + psi9->source.nr_irqs - 1);
    xive_source_pic_print_info(&psi9->source, offset, mon);
}
