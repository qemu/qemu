/*
 * QEMU PowerPC PowerNV (POWER9) PHB4 model
 *
 * Copyright (c) 2018-2020, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "target/ppc/cpu.h"
#include "hw/pci-host/pnv_phb4_regs.h"
#include "hw/pci-host/pnv_phb4.h"
#include "hw/pci/pcie_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qom/object.h"
#include "trace.h"

#define phb_error(phb, fmt, ...)                                        \
    qemu_log_mask(LOG_GUEST_ERROR, "phb4[%d:%d]: " fmt "\n",            \
                  (phb)->chip_id, (phb)->phb_id, ## __VA_ARGS__)

#define phb_pec_error(pec, fmt, ...)                                    \
    qemu_log_mask(LOG_GUEST_ERROR, "phb4_pec[%d:%d]: " fmt "\n",        \
                  (pec)->chip_id, (pec)->index, ## __VA_ARGS__)

static PCIDevice *pnv_phb4_find_cfg_dev(PnvPHB4 *phb)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(phb->phb_base);
    uint64_t addr = phb->regs[PHB_CONFIG_ADDRESS >> 3];
    uint8_t bus, devfn;

    if (!(addr >> 63)) {
        return NULL;
    }
    bus = (addr >> 52) & 0xff;
    devfn = (addr >> 44) & 0xff;

    /* We don't access the root complex this way */
    if (bus == 0 && devfn == 0) {
        return NULL;
    }
    return pci_find_device(pci->bus, bus, devfn);
}

/*
 * The CONFIG_DATA register expects little endian accesses, but as the
 * region is big endian, we have to swap the value.
 */
static void pnv_phb4_config_write(PnvPHB4 *phb, unsigned off,
                                  unsigned size, uint64_t val)
{
    uint32_t cfg_addr, limit;
    PCIDevice *pdev;

    pdev = pnv_phb4_find_cfg_dev(phb);
    if (!pdev) {
        return;
    }
    cfg_addr = (phb->regs[PHB_CONFIG_ADDRESS >> 3] >> 32) & 0xffc;
    cfg_addr |= off;
    limit = pci_config_size(pdev);
    if (limit <= cfg_addr) {
        /*
         * conventional pci device can be behind pcie-to-pci bridge.
         * 256 <= addr < 4K has no effects.
         */
        return;
    }
    switch (size) {
    case 1:
        break;
    case 2:
        val = bswap16(val);
        break;
    case 4:
        val = bswap32(val);
        break;
    default:
        g_assert_not_reached();
    }
    pci_host_config_write_common(pdev, cfg_addr, limit, val, size);
}

static uint64_t pnv_phb4_config_read(PnvPHB4 *phb, unsigned off,
                                     unsigned size)
{
    uint32_t cfg_addr, limit;
    PCIDevice *pdev;
    uint64_t val;

    pdev = pnv_phb4_find_cfg_dev(phb);
    if (!pdev) {
        return ~0ull;
    }
    cfg_addr = (phb->regs[PHB_CONFIG_ADDRESS >> 3] >> 32) & 0xffc;
    cfg_addr |= off;
    limit = pci_config_size(pdev);
    if (limit <= cfg_addr) {
        /*
         * conventional pci device can be behind pcie-to-pci bridge.
         * 256 <= addr < 4K has no effects.
         */
        return ~0ull;
    }
    val = pci_host_config_read_common(pdev, cfg_addr, limit, size);
    switch (size) {
    case 1:
        return val;
    case 2:
        return bswap16(val);
    case 4:
        return bswap32(val);
    default:
        g_assert_not_reached();
    }
}

/*
 * Root complex register accesses are memory mapped.
 */
static void pnv_phb4_rc_config_write(PnvPHB4 *phb, unsigned off,
                                     unsigned size, uint64_t val)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(phb->phb_base);
    PCIDevice *pdev;

    if (size != 4) {
        phb_error(phb, "rc_config_write invalid size %d", size);
        return;
    }

    pdev = pci_find_device(pci->bus, 0, 0);
    if (!pdev) {
        phb_error(phb, "rc_config_write device not found");
        return;
    }

    pci_host_config_write_common(pdev, off, PHB_RC_CONFIG_SIZE,
                                 bswap32(val), 4);
}

static uint64_t pnv_phb4_rc_config_read(PnvPHB4 *phb, unsigned off,
                                        unsigned size)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(phb->phb_base);
    PCIDevice *pdev;
    uint64_t val;

    if (size != 4) {
        phb_error(phb, "rc_config_read invalid size %d", size);
        return ~0ull;
    }

    pdev = pci_find_device(pci->bus, 0, 0);
    if (!pdev) {
        phb_error(phb, "rc_config_read device not found");
        return ~0ull;
    }

    val = pci_host_config_read_common(pdev, off, PHB_RC_CONFIG_SIZE, 4);
    return bswap32(val);
}

static void pnv_phb4_check_mbt(PnvPHB4 *phb, uint32_t index)
{
    uint64_t base, start, size, mbe0, mbe1;
    MemoryRegion *parent;
    char name[64];

    /* Unmap first */
    if (memory_region_is_mapped(&phb->mr_mmio[index])) {
        /* Should we destroy it in RCU friendly way... ? */
        memory_region_del_subregion(phb->mr_mmio[index].container,
                                    &phb->mr_mmio[index]);
    }

    /* Get table entry */
    mbe0 = phb->ioda_MBT[(index << 1)];
    mbe1 = phb->ioda_MBT[(index << 1) + 1];

    if (!(mbe0 & IODA3_MBT0_ENABLE)) {
        return;
    }

    /* Grab geometry from registers */
    base = GETFIELD(IODA3_MBT0_BASE_ADDR, mbe0) << 12;
    size = GETFIELD(IODA3_MBT1_MASK, mbe1) << 12;
    size |= 0xff00000000000000ull;
    size = ~size + 1;

    /* Calculate PCI side start address based on M32/M64 window type */
    if (mbe0 & IODA3_MBT0_TYPE_M32) {
        start = phb->regs[PHB_M32_START_ADDR >> 3];
        if ((start + size) > 0x100000000ull) {
            phb_error(phb, "M32 set beyond 4GB boundary !");
            size = 0x100000000 - start;
        }
    } else {
        start = base | (phb->regs[PHB_M64_UPPER_BITS >> 3]);
    }

    /* TODO: Figure out how to implement/decode AOMASK */

    /* Check if it matches an enabled MMIO region in the PEC stack */
    if (memory_region_is_mapped(&phb->mmbar0) &&
        base >= phb->mmio0_base &&
        (base + size) <= (phb->mmio0_base + phb->mmio0_size)) {
        parent = &phb->mmbar0;
        base -= phb->mmio0_base;
    } else if (memory_region_is_mapped(&phb->mmbar1) &&
        base >= phb->mmio1_base &&
        (base + size) <= (phb->mmio1_base + phb->mmio1_size)) {
        parent = &phb->mmbar1;
        base -= phb->mmio1_base;
    } else {
        phb_error(phb, "PHB MBAR %d out of parent bounds", index);
        return;
    }

    /* Create alias (better name ?) */
    snprintf(name, sizeof(name), "phb4-mbar%d", index);
    memory_region_init_alias(&phb->mr_mmio[index], OBJECT(phb), name,
                             &phb->pci_mmio, start, size);
    memory_region_add_subregion(parent, base, &phb->mr_mmio[index]);
}

static void pnv_phb4_check_all_mbt(PnvPHB4 *phb)
{
    uint64_t i;
    uint32_t num_windows = phb->big_phb ? PNV_PHB4_MAX_MMIO_WINDOWS :
        PNV_PHB4_MIN_MMIO_WINDOWS;

    for (i = 0; i < num_windows; i++) {
        pnv_phb4_check_mbt(phb, i);
    }
}

static uint64_t *pnv_phb4_ioda_access(PnvPHB4 *phb,
                                      unsigned *out_table, unsigned *out_idx)
{
    uint64_t adreg = phb->regs[PHB_IODA_ADDR >> 3];
    unsigned int index = GETFIELD(PHB_IODA_AD_TADR, adreg);
    unsigned int table = GETFIELD(PHB_IODA_AD_TSEL, adreg);
    unsigned int mask;
    uint64_t *tptr = NULL;

    switch (table) {
    case IODA3_TBL_LIST:
        tptr = phb->ioda_LIST;
        mask = 7;
        break;
    case IODA3_TBL_MIST:
        tptr = phb->ioda_MIST;
        mask = phb->big_phb ? PNV_PHB4_MAX_MIST : (PNV_PHB4_MAX_MIST >> 1);
        mask -= 1;
        break;
    case IODA3_TBL_RCAM:
        mask = phb->big_phb ? 127 : 63;
        break;
    case IODA3_TBL_MRT:
        mask = phb->big_phb ? 15 : 7;
        break;
    case IODA3_TBL_PESTA:
    case IODA3_TBL_PESTB:
        mask = phb->big_phb ? PNV_PHB4_MAX_PEs : (PNV_PHB4_MAX_PEs >> 1);
        mask -= 1;
        break;
    case IODA3_TBL_TVT:
        tptr = phb->ioda_TVT;
        mask = phb->big_phb ? PNV_PHB4_MAX_TVEs : (PNV_PHB4_MAX_TVEs >> 1);
        mask -= 1;
        break;
    case IODA3_TBL_TCR:
    case IODA3_TBL_TDR:
        mask = phb->big_phb ? 1023 : 511;
        break;
    case IODA3_TBL_MBT:
        tptr = phb->ioda_MBT;
        mask = phb->big_phb ? PNV_PHB4_MAX_MBEs : (PNV_PHB4_MAX_MBEs >> 1);
        mask -= 1;
        break;
    case IODA3_TBL_MDT:
        tptr = phb->ioda_MDT;
        mask = phb->big_phb ? PNV_PHB4_MAX_PEs : (PNV_PHB4_MAX_PEs >> 1);
        mask -= 1;
        break;
    case IODA3_TBL_PEEV:
        tptr = phb->ioda_PEEV;
        mask = phb->big_phb ? PNV_PHB4_MAX_PEEVs : (PNV_PHB4_MAX_PEEVs >> 1);
        mask -= 1;
        break;
    default:
        phb_error(phb, "invalid IODA table %d", table);
        return NULL;
    }
    index &= mask;
    if (out_idx) {
        *out_idx = index;
    }
    if (out_table) {
        *out_table = table;
    }
    if (tptr) {
        tptr += index;
    }
    if (adreg & PHB_IODA_AD_AUTOINC) {
        index = (index + 1) & mask;
        adreg = SETFIELD(PHB_IODA_AD_TADR, adreg, index);
    }

    phb->regs[PHB_IODA_ADDR >> 3] = adreg;
    return tptr;
}

static uint64_t pnv_phb4_ioda_read(PnvPHB4 *phb)
{
    unsigned table, idx;
    uint64_t *tptr;

    tptr = pnv_phb4_ioda_access(phb, &table, &idx);
    if (!tptr) {
        /* Special PESTA case */
        if (table == IODA3_TBL_PESTA) {
            return ((uint64_t)(phb->ioda_PEST_AB[idx] & 1)) << 63;
        } else if (table == IODA3_TBL_PESTB) {
            return ((uint64_t)(phb->ioda_PEST_AB[idx] & 2)) << 62;
        }
        /* Return 0 on unsupported tables, not ff's */
        return 0;
    }
    return *tptr;
}

static void pnv_phb4_ioda_write(PnvPHB4 *phb, uint64_t val)
{
    unsigned table, idx;
    uint64_t *tptr;

    tptr = pnv_phb4_ioda_access(phb, &table, &idx);
    if (!tptr) {
        /* Special PESTA case */
        if (table == IODA3_TBL_PESTA) {
            phb->ioda_PEST_AB[idx] &= ~1;
            phb->ioda_PEST_AB[idx] |= (val >> 63) & 1;
        } else if (table == IODA3_TBL_PESTB) {
            phb->ioda_PEST_AB[idx] &= ~2;
            phb->ioda_PEST_AB[idx] |= (val >> 62) & 2;
        }
        return;
    }

    /* Handle side effects */
    switch (table) {
    case IODA3_TBL_LIST:
        break;
    case IODA3_TBL_MIST: {
        /* Special mask for MIST partial write */
        uint64_t adreg = phb->regs[PHB_IODA_ADDR >> 3];
        uint32_t mmask = GETFIELD(PHB_IODA_AD_MIST_PWV, adreg);
        uint64_t v = *tptr;
        if (mmask == 0) {
            mmask = 0xf;
        }
        if (mmask & 8) {
            v &= 0x0000ffffffffffffull;
            v |= 0xcfff000000000000ull & val;
        }
        if (mmask & 4) {
            v &= 0xffff0000ffffffffull;
            v |= 0x0000cfff00000000ull & val;
        }
        if (mmask & 2) {
            v &= 0xffffffff0000ffffull;
            v |= 0x00000000cfff0000ull & val;
        }
        if (mmask & 1) {
            v &= 0xffffffffffff0000ull;
            v |= 0x000000000000cfffull & val;
        }
        *tptr = v;
        break;
    }
    case IODA3_TBL_MBT:
        *tptr = val;

        /* Copy across the valid bit to the other half */
        phb->ioda_MBT[idx ^ 1] &= 0x7fffffffffffffffull;
        phb->ioda_MBT[idx ^ 1] |= 0x8000000000000000ull & val;

        /* Update mappings */
        pnv_phb4_check_mbt(phb, idx >> 1);
        break;
    default:
        *tptr = val;
    }
}

static void pnv_phb4_rtc_invalidate(PnvPHB4 *phb, uint64_t val)
{
    PnvPhb4DMASpace *ds;

    /* Always invalidate all for now ... */
    QLIST_FOREACH(ds, &phb->dma_spaces, list) {
        ds->pe_num = PHB_INVALID_PE;
    }
}

static void pnv_phb4_update_msi_regions(PnvPhb4DMASpace *ds)
{
    uint64_t cfg = ds->phb->regs[PHB_PHB4_CONFIG >> 3];

    if (cfg & PHB_PHB4C_32BIT_MSI_EN) {
        if (!memory_region_is_mapped(MEMORY_REGION(&ds->msi32_mr))) {
            memory_region_add_subregion(MEMORY_REGION(&ds->dma_mr),
                                        0xffff0000, &ds->msi32_mr);
        }
    } else {
        if (memory_region_is_mapped(MEMORY_REGION(&ds->msi32_mr))) {
            memory_region_del_subregion(MEMORY_REGION(&ds->dma_mr),
                                        &ds->msi32_mr);
        }
    }

    if (cfg & PHB_PHB4C_64BIT_MSI_EN) {
        if (!memory_region_is_mapped(MEMORY_REGION(&ds->msi64_mr))) {
            memory_region_add_subregion(MEMORY_REGION(&ds->dma_mr),
                                        (1ull << 60), &ds->msi64_mr);
        }
    } else {
        if (memory_region_is_mapped(MEMORY_REGION(&ds->msi64_mr))) {
            memory_region_del_subregion(MEMORY_REGION(&ds->dma_mr),
                                        &ds->msi64_mr);
        }
    }
}

static void pnv_phb4_update_all_msi_regions(PnvPHB4 *phb)
{
    PnvPhb4DMASpace *ds;

    QLIST_FOREACH(ds, &phb->dma_spaces, list) {
        pnv_phb4_update_msi_regions(ds);
    }
}

static void pnv_phb4_update_xsrc(PnvPHB4 *phb)
{
    int shift, flags, i, lsi_base;
    XiveSource *xsrc = &phb->xsrc;

    /* The XIVE source characteristics can be set at run time */
    if (phb->regs[PHB_CTRLR >> 3] & PHB_CTRLR_IRQ_PGSZ_64K) {
        shift = XIVE_ESB_64K;
    } else {
        shift = XIVE_ESB_4K;
    }
    if (phb->regs[PHB_CTRLR >> 3] & PHB_CTRLR_IRQ_STORE_EOI) {
        flags = XIVE_SRC_STORE_EOI;
    } else {
        flags = 0;
    }

    /*
     * When the PQ disable configuration bit is set, the check on the
     * PQ state bits is disabled on the PHB side (for MSI only) and it
     * is performed on the IC side instead.
     */
    if (phb->regs[PHB_CTRLR >> 3] & PHB_CTRLR_IRQ_PQ_DISABLE) {
        flags |= XIVE_SRC_PQ_DISABLE;
    }

    phb->xsrc.esb_shift = shift;
    phb->xsrc.esb_flags = flags;

    lsi_base = GETFIELD(PHB_LSI_SRC_ID, phb->regs[PHB_LSI_SOURCE_ID >> 3]);
    lsi_base <<= 3;

    /* TODO: handle reset values of PHB_LSI_SRC_ID */
    if (!lsi_base) {
        return;
    }

    /* TODO: need a xive_source_irq_reset_lsi() */
    bitmap_zero(xsrc->lsi_map, xsrc->nr_irqs);

    for (i = 0; i < xsrc->nr_irqs; i++) {
        bool msi = (i < lsi_base || i >= (lsi_base + 8));
        if (!msi) {
            xive_source_irq_set_lsi(xsrc, i);
        }
    }
}

static void pnv_phb4_reg_write(void *opaque, hwaddr off, uint64_t val,
                               unsigned size)
{
    PnvPHB4 *phb = PNV_PHB4(opaque);
    bool changed;

    /* Special case outbound configuration data */
    if ((off & 0xfffc) == PHB_CONFIG_DATA) {
        pnv_phb4_config_write(phb, off & 0x3, size, val);
        return;
    }

    /* Special case RC configuration space */
    if ((off & 0xf800) == PHB_RC_CONFIG_BASE) {
        pnv_phb4_rc_config_write(phb, off & 0x7ff, size, val);
        return;
    }

    /* Other registers are 64-bit only */
    if (size != 8 || off & 0x7) {
        phb_error(phb, "Invalid register access, offset: 0x%"PRIx64" size: %d",
                   off, size);
        return;
    }

    /* Handle masking */
    switch (off) {
    case PHB_LSI_SOURCE_ID:
        val &= PHB_LSI_SRC_ID;
        break;
    case PHB_M64_UPPER_BITS:
        val &= 0xff00000000000000ull;
        break;
    /* TCE Kill */
    case PHB_TCE_KILL:
        /* Clear top 3 bits which HW does to indicate successful queuing */
        val &= ~(PHB_TCE_KILL_ALL | PHB_TCE_KILL_PE | PHB_TCE_KILL_ONE);
        break;
    case PHB_Q_DMA_R:
        /*
         * This is enough logic to make SW happy but we aren't
         * actually quiescing the DMAs
         */
        if (val & PHB_Q_DMA_R_AUTORESET) {
            val = 0;
        } else {
            val &= PHB_Q_DMA_R_QUIESCE_DMA;
        }
        break;
    /* LEM stuff */
    case PHB_LEM_FIR_AND_MASK:
        phb->regs[PHB_LEM_FIR_ACCUM >> 3] &= val;
        return;
    case PHB_LEM_FIR_OR_MASK:
        phb->regs[PHB_LEM_FIR_ACCUM >> 3] |= val;
        return;
    case PHB_LEM_ERROR_AND_MASK:
        phb->regs[PHB_LEM_ERROR_MASK >> 3] &= val;
        return;
    case PHB_LEM_ERROR_OR_MASK:
        phb->regs[PHB_LEM_ERROR_MASK >> 3] |= val;
        return;
    case PHB_LEM_WOF:
        val = 0;
        break;
    /* TODO: More regs ..., maybe create a table with masks... */

    /* Read only registers */
    case PHB_CPU_LOADSTORE_STATUS:
    case PHB_ETU_ERR_SUMMARY:
    case PHB_PHB4_GEN_CAP:
    case PHB_PHB4_TCE_CAP:
    case PHB_PHB4_IRQ_CAP:
    case PHB_PHB4_EEH_CAP:
        return;
    }

    /* Record whether it changed */
    changed = phb->regs[off >> 3] != val;

    /* Store in register cache first */
    phb->regs[off >> 3] = val;

    /* Handle side effects */
    switch (off) {
    case PHB_PHB4_CONFIG:
        if (changed) {
            pnv_phb4_update_all_msi_regions(phb);
        }
        break;
    case PHB_M32_START_ADDR:
    case PHB_M64_UPPER_BITS:
        if (changed) {
            pnv_phb4_check_all_mbt(phb);
        }
        break;

    /* IODA table accesses */
    case PHB_IODA_DATA0:
        pnv_phb4_ioda_write(phb, val);
        break;

    /* RTC invalidation */
    case PHB_RTC_INVALIDATE:
        pnv_phb4_rtc_invalidate(phb, val);
        break;

    /* PHB Control (Affects XIVE source) */
    case PHB_CTRLR:
    case PHB_LSI_SOURCE_ID:
        pnv_phb4_update_xsrc(phb);
        break;

    /* Silent simple writes */
    case PHB_ASN_CMPM:
    case PHB_CONFIG_ADDRESS:
    case PHB_IODA_ADDR:
    case PHB_TCE_KILL:
    case PHB_TCE_SPEC_CTL:
    case PHB_PEST_BAR:
    case PHB_PELTV_BAR:
    case PHB_RTT_BAR:
    case PHB_LEM_FIR_ACCUM:
    case PHB_LEM_ERROR_MASK:
    case PHB_LEM_ACTION0:
    case PHB_LEM_ACTION1:
    case PHB_TCE_TAG_ENABLE:
    case PHB_INT_NOTIFY_ADDR:
    case PHB_INT_NOTIFY_INDEX:
    case PHB_DMARD_SYNC:
       break;

    /* Noise on anything else */
    default:
        qemu_log_mask(LOG_UNIMP, "phb4: reg_write 0x%"PRIx64"=%"PRIx64"\n",
                      off, val);
    }
}

static uint64_t pnv_phb4_reg_read(void *opaque, hwaddr off, unsigned size)
{
    PnvPHB4 *phb = PNV_PHB4(opaque);
    uint64_t val;

    if ((off & 0xfffc) == PHB_CONFIG_DATA) {
        return pnv_phb4_config_read(phb, off & 0x3, size);
    }

    /* Special case RC configuration space */
    if ((off & 0xf800) == PHB_RC_CONFIG_BASE) {
        return pnv_phb4_rc_config_read(phb, off & 0x7ff, size);
    }

    /* Other registers are 64-bit only */
    if (size != 8 || off & 0x7) {
        phb_error(phb, "Invalid register access, offset: 0x%"PRIx64" size: %d",
                   off, size);
        return ~0ull;
    }

    /* Default read from cache */
    val = phb->regs[off >> 3];

    switch (off) {
    case PHB_VERSION:
        return PNV_PHB4_PEC_GET_CLASS(phb->pec)->version;

        /* Read-only */
    case PHB_PHB4_GEN_CAP:
        return 0xe4b8000000000000ull;
    case PHB_PHB4_TCE_CAP:
        return phb->big_phb ? 0x4008440000000400ull : 0x2008440000000200ull;
    case PHB_PHB4_IRQ_CAP:
        return phb->big_phb ? 0x0800000000001000ull : 0x0800000000000800ull;
    case PHB_PHB4_EEH_CAP:
        return phb->big_phb ? 0x2000000000000000ull : 0x1000000000000000ull;

    /* IODA table accesses */
    case PHB_IODA_DATA0:
        return pnv_phb4_ioda_read(phb);

    /* Link training always appears trained */
    case PHB_PCIE_DLP_TRAIN_CTL:
        /* TODO: Do something sensible with speed ? */
        return PHB_PCIE_DLP_INBAND_PRESENCE | PHB_PCIE_DLP_TL_LINKACT;

    /* DMA read sync: make it look like it's complete */
    case PHB_DMARD_SYNC:
        return PHB_DMARD_SYNC_COMPLETE;

    /* Silent simple reads */
    case PHB_LSI_SOURCE_ID:
    case PHB_CPU_LOADSTORE_STATUS:
    case PHB_ASN_CMPM:
    case PHB_PHB4_CONFIG:
    case PHB_M32_START_ADDR:
    case PHB_CONFIG_ADDRESS:
    case PHB_IODA_ADDR:
    case PHB_RTC_INVALIDATE:
    case PHB_TCE_KILL:
    case PHB_TCE_SPEC_CTL:
    case PHB_PEST_BAR:
    case PHB_PELTV_BAR:
    case PHB_RTT_BAR:
    case PHB_M64_UPPER_BITS:
    case PHB_CTRLR:
    case PHB_LEM_FIR_ACCUM:
    case PHB_LEM_ERROR_MASK:
    case PHB_LEM_ACTION0:
    case PHB_LEM_ACTION1:
    case PHB_TCE_TAG_ENABLE:
    case PHB_INT_NOTIFY_ADDR:
    case PHB_INT_NOTIFY_INDEX:
    case PHB_Q_DMA_R:
    case PHB_ETU_ERR_SUMMARY:
        break;

    /* Noise on anything else */
    default:
        qemu_log_mask(LOG_UNIMP, "phb4: reg_read 0x%"PRIx64"=%"PRIx64"\n",
                      off, val);
    }
    return val;
}

static const MemoryRegionOps pnv_phb4_reg_ops = {
    .read = pnv_phb4_reg_read,
    .write = pnv_phb4_reg_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static uint64_t pnv_phb4_xscom_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvPHB4 *phb = PNV_PHB4(opaque);
    uint32_t reg = addr >> 3;
    uint64_t val;
    hwaddr offset;

    switch (reg) {
    case PHB_SCOM_HV_IND_ADDR:
        return phb->scom_hv_ind_addr_reg;

    case PHB_SCOM_HV_IND_DATA:
        if (!(phb->scom_hv_ind_addr_reg & PHB_SCOM_HV_IND_ADDR_VALID)) {
            phb_error(phb, "Invalid indirect address");
            return ~0ull;
        }
        size = (phb->scom_hv_ind_addr_reg & PHB_SCOM_HV_IND_ADDR_4B) ? 4 : 8;
        offset = GETFIELD(PHB_SCOM_HV_IND_ADDR_ADDR, phb->scom_hv_ind_addr_reg);
        val = pnv_phb4_reg_read(phb, offset, size);
        if (phb->scom_hv_ind_addr_reg & PHB_SCOM_HV_IND_ADDR_AUTOINC) {
            offset += size;
            offset &= 0x3fff;
            phb->scom_hv_ind_addr_reg = SETFIELD(PHB_SCOM_HV_IND_ADDR_ADDR,
                                                 phb->scom_hv_ind_addr_reg,
                                                 offset);
        }
        return val;
    case PHB_SCOM_ETU_LEM_FIR:
    case PHB_SCOM_ETU_LEM_FIR_AND:
    case PHB_SCOM_ETU_LEM_FIR_OR:
    case PHB_SCOM_ETU_LEM_FIR_MSK:
    case PHB_SCOM_ETU_LEM_ERR_MSK_AND:
    case PHB_SCOM_ETU_LEM_ERR_MSK_OR:
    case PHB_SCOM_ETU_LEM_ACT0:
    case PHB_SCOM_ETU_LEM_ACT1:
    case PHB_SCOM_ETU_LEM_WOF:
        offset = ((reg - PHB_SCOM_ETU_LEM_FIR) << 3) + PHB_LEM_FIR_ACCUM;
        return pnv_phb4_reg_read(phb, offset, size);
    case PHB_SCOM_ETU_PMON_CONFIG:
    case PHB_SCOM_ETU_PMON_CTR0:
    case PHB_SCOM_ETU_PMON_CTR1:
    case PHB_SCOM_ETU_PMON_CTR2:
    case PHB_SCOM_ETU_PMON_CTR3:
        offset = ((reg - PHB_SCOM_ETU_PMON_CONFIG) << 3) + PHB_PERFMON_CONFIG;
        return pnv_phb4_reg_read(phb, offset, size);

    default:
        qemu_log_mask(LOG_UNIMP, "phb4: xscom_read 0x%"HWADDR_PRIx"\n", addr);
        return ~0ull;
    }
}

static void pnv_phb4_xscom_write(void *opaque, hwaddr addr,
                                 uint64_t val, unsigned size)
{
    PnvPHB4 *phb = PNV_PHB4(opaque);
    uint32_t reg = addr >> 3;
    hwaddr offset;

    switch (reg) {
    case PHB_SCOM_HV_IND_ADDR:
        phb->scom_hv_ind_addr_reg = val & 0xe000000000001fff;
        break;
    case PHB_SCOM_HV_IND_DATA:
        if (!(phb->scom_hv_ind_addr_reg & PHB_SCOM_HV_IND_ADDR_VALID)) {
            phb_error(phb, "Invalid indirect address");
            break;
        }
        size = (phb->scom_hv_ind_addr_reg & PHB_SCOM_HV_IND_ADDR_4B) ? 4 : 8;
        offset = GETFIELD(PHB_SCOM_HV_IND_ADDR_ADDR, phb->scom_hv_ind_addr_reg);
        pnv_phb4_reg_write(phb, offset, val, size);
        if (phb->scom_hv_ind_addr_reg & PHB_SCOM_HV_IND_ADDR_AUTOINC) {
            offset += size;
            offset &= 0x3fff;
            phb->scom_hv_ind_addr_reg = SETFIELD(PHB_SCOM_HV_IND_ADDR_ADDR,
                                                 phb->scom_hv_ind_addr_reg,
                                                 offset);
        }
        break;
    case PHB_SCOM_ETU_LEM_FIR:
    case PHB_SCOM_ETU_LEM_FIR_AND:
    case PHB_SCOM_ETU_LEM_FIR_OR:
    case PHB_SCOM_ETU_LEM_FIR_MSK:
    case PHB_SCOM_ETU_LEM_ERR_MSK_AND:
    case PHB_SCOM_ETU_LEM_ERR_MSK_OR:
    case PHB_SCOM_ETU_LEM_ACT0:
    case PHB_SCOM_ETU_LEM_ACT1:
    case PHB_SCOM_ETU_LEM_WOF:
        offset = ((reg - PHB_SCOM_ETU_LEM_FIR) << 3) + PHB_LEM_FIR_ACCUM;
        pnv_phb4_reg_write(phb, offset, val, size);
        break;
    case PHB_SCOM_ETU_PMON_CONFIG:
    case PHB_SCOM_ETU_PMON_CTR0:
    case PHB_SCOM_ETU_PMON_CTR1:
    case PHB_SCOM_ETU_PMON_CTR2:
    case PHB_SCOM_ETU_PMON_CTR3:
        offset = ((reg - PHB_SCOM_ETU_PMON_CONFIG) << 3) + PHB_PERFMON_CONFIG;
        pnv_phb4_reg_write(phb, offset, val, size);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "phb4: xscom_write 0x%"HWADDR_PRIx
                      "=%"PRIx64"\n", addr, val);
    }
}

const MemoryRegionOps pnv_phb4_xscom_ops = {
    .read = pnv_phb4_xscom_read,
    .write = pnv_phb4_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static uint64_t pnv_pec_stk_nest_xscom_read(void *opaque, hwaddr addr,
                                            unsigned size)
{
    PnvPHB4 *phb = PNV_PHB4(opaque);
    uint32_t reg = addr >> 3;

    /* All registers are read-able */
    return phb->nest_regs[reg];
}

/*
 * Return the 'stack_no' of a PHB4. 'stack_no' is the order
 * the PHB4 occupies in the PEC. This is the reverse of what
 * pnv_phb4_pec_get_phb_id() does.
 *
 * E.g. a phb with phb_id = 4 and pec->index = 1 (PEC1) will
 * be the second phb (stack_no = 1) of the PEC.
 */
static int pnv_phb4_get_phb_stack_no(PnvPHB4 *phb)
{
    PnvPhb4PecState *pec = phb->pec;
    PnvPhb4PecClass *pecc = PNV_PHB4_PEC_GET_CLASS(pec);
    int index = pec->index;
    int stack_no = phb->phb_id;

    while (index--) {
        stack_no -= pecc->num_phbs[index];
    }

    return stack_no;
}

static void pnv_phb4_update_regions(PnvPHB4 *phb)
{
    /* Unmap first always */
    if (memory_region_is_mapped(&phb->mr_regs)) {
        memory_region_del_subregion(&phb->phbbar, &phb->mr_regs);
    }
    if (memory_region_is_mapped(&phb->xsrc.esb_mmio)) {
        memory_region_del_subregion(&phb->intbar, &phb->xsrc.esb_mmio);
    }

    /* Map registers if enabled */
    if (memory_region_is_mapped(&phb->phbbar)) {
        memory_region_add_subregion(&phb->phbbar, 0, &phb->mr_regs);
    }

    /* Map ESB if enabled */
    if (memory_region_is_mapped(&phb->intbar)) {
        memory_region_add_subregion(&phb->intbar, 0, &phb->xsrc.esb_mmio);
    }

    /* Check/update m32 */
    pnv_phb4_check_all_mbt(phb);
}

static void pnv_pec_phb_update_map(PnvPHB4 *phb)
{
    PnvPhb4PecState *pec = phb->pec;
    MemoryRegion *sysmem = get_system_memory();
    uint64_t bar_en = phb->nest_regs[PEC_NEST_STK_BAR_EN];
    int stack_no = pnv_phb4_get_phb_stack_no(phb);
    uint64_t bar, mask, size;
    char name[64];

    /*
     * NOTE: This will really not work well if those are remapped
     * after the PHB has created its sub regions. We could do better
     * if we had a way to resize regions but we don't really care
     * that much in practice as the stuff below really only happens
     * once early during boot
     */

    /* Handle unmaps */
    if (memory_region_is_mapped(&phb->mmbar0) &&
        !(bar_en & PEC_NEST_STK_BAR_EN_MMIO0)) {
        memory_region_del_subregion(sysmem, &phb->mmbar0);
    }
    if (memory_region_is_mapped(&phb->mmbar1) &&
        !(bar_en & PEC_NEST_STK_BAR_EN_MMIO1)) {
        memory_region_del_subregion(sysmem, &phb->mmbar1);
    }
    if (memory_region_is_mapped(&phb->phbbar) &&
        !(bar_en & PEC_NEST_STK_BAR_EN_PHB)) {
        memory_region_del_subregion(sysmem, &phb->phbbar);
    }
    if (memory_region_is_mapped(&phb->intbar) &&
        !(bar_en & PEC_NEST_STK_BAR_EN_INT)) {
        memory_region_del_subregion(sysmem, &phb->intbar);
    }

    /* Update PHB */
    pnv_phb4_update_regions(phb);

    /* Handle maps */
    if (!memory_region_is_mapped(&phb->mmbar0) &&
        (bar_en & PEC_NEST_STK_BAR_EN_MMIO0)) {
        bar = phb->nest_regs[PEC_NEST_STK_MMIO_BAR0] >> 8;
        mask = phb->nest_regs[PEC_NEST_STK_MMIO_BAR0_MASK];
        size = ((~mask) >> 8) + 1;
        snprintf(name, sizeof(name), "pec-%d.%d-phb-%d-mmio0",
                 pec->chip_id, pec->index, stack_no);
        memory_region_init(&phb->mmbar0, OBJECT(phb), name, size);
        memory_region_add_subregion(sysmem, bar, &phb->mmbar0);
        phb->mmio0_base = bar;
        phb->mmio0_size = size;
    }
    if (!memory_region_is_mapped(&phb->mmbar1) &&
        (bar_en & PEC_NEST_STK_BAR_EN_MMIO1)) {
        bar = phb->nest_regs[PEC_NEST_STK_MMIO_BAR1] >> 8;
        mask = phb->nest_regs[PEC_NEST_STK_MMIO_BAR1_MASK];
        size = ((~mask) >> 8) + 1;
        snprintf(name, sizeof(name), "pec-%d.%d-phb-%d-mmio1",
                 pec->chip_id, pec->index, stack_no);
        memory_region_init(&phb->mmbar1, OBJECT(phb), name, size);
        memory_region_add_subregion(sysmem, bar, &phb->mmbar1);
        phb->mmio1_base = bar;
        phb->mmio1_size = size;
    }
    if (!memory_region_is_mapped(&phb->phbbar) &&
        (bar_en & PEC_NEST_STK_BAR_EN_PHB)) {
        bar = phb->nest_regs[PEC_NEST_STK_PHB_REGS_BAR] >> 8;
        size = PNV_PHB4_NUM_REGS << 3;
        snprintf(name, sizeof(name), "pec-%d.%d-phb-%d",
                 pec->chip_id, pec->index, stack_no);
        memory_region_init(&phb->phbbar, OBJECT(phb), name, size);
        memory_region_add_subregion(sysmem, bar, &phb->phbbar);
    }
    if (!memory_region_is_mapped(&phb->intbar) &&
        (bar_en & PEC_NEST_STK_BAR_EN_INT)) {
        bar = phb->nest_regs[PEC_NEST_STK_INT_BAR] >> 8;
        size = PNV_PHB4_MAX_INTs << 16;
        snprintf(name, sizeof(name), "pec-%d.%d-phb-%d-int",
                 phb->pec->chip_id, phb->pec->index, stack_no);
        memory_region_init(&phb->intbar, OBJECT(phb), name, size);
        memory_region_add_subregion(sysmem, bar, &phb->intbar);
    }

    /* Update PHB */
    pnv_phb4_update_regions(phb);
}

static void pnv_pec_stk_nest_xscom_write(void *opaque, hwaddr addr,
                                         uint64_t val, unsigned size)
{
    PnvPHB4 *phb = PNV_PHB4(opaque);
    PnvPhb4PecState *pec = phb->pec;
    uint32_t reg = addr >> 3;

    switch (reg) {
    case PEC_NEST_STK_PCI_NEST_FIR:
        phb->nest_regs[PEC_NEST_STK_PCI_NEST_FIR] = val & PPC_BITMASK(0, 27);
        break;
    case PEC_NEST_STK_PCI_NEST_FIR_CLR:
        phb->nest_regs[PEC_NEST_STK_PCI_NEST_FIR] &= val;
        break;
    case PEC_NEST_STK_PCI_NEST_FIR_SET:
        phb->nest_regs[PEC_NEST_STK_PCI_NEST_FIR] |= val;
        break;
    case PEC_NEST_STK_PCI_NEST_FIR_MSK:
        phb->nest_regs[PEC_NEST_STK_PCI_NEST_FIR_MSK] = val &
                                                        PPC_BITMASK(0, 27);
        break;
    case PEC_NEST_STK_PCI_NEST_FIR_MSKC:
        phb->nest_regs[PEC_NEST_STK_PCI_NEST_FIR_MSK] &= val;
        break;
    case PEC_NEST_STK_PCI_NEST_FIR_MSKS:
        phb->nest_regs[PEC_NEST_STK_PCI_NEST_FIR_MSK] |= val;
        break;
    case PEC_NEST_STK_PCI_NEST_FIR_ACT0:
    case PEC_NEST_STK_PCI_NEST_FIR_ACT1:
        phb->nest_regs[reg] = val & PPC_BITMASK(0, 27);
        break;
    case PEC_NEST_STK_PCI_NEST_FIR_WOF:
        phb->nest_regs[reg] = 0;
        break;
    case PEC_NEST_STK_ERR_REPORT_0:
    case PEC_NEST_STK_ERR_REPORT_1:
    case PEC_NEST_STK_PBCQ_GNRL_STATUS:
        /* Flag error ? */
        break;
    case PEC_NEST_STK_PBCQ_MODE:
        phb->nest_regs[reg] = val & PPC_BITMASK(0, 7);
        break;
    case PEC_NEST_STK_MMIO_BAR0:
    case PEC_NEST_STK_MMIO_BAR0_MASK:
    case PEC_NEST_STK_MMIO_BAR1:
    case PEC_NEST_STK_MMIO_BAR1_MASK:
        if (phb->nest_regs[PEC_NEST_STK_BAR_EN] &
            (PEC_NEST_STK_BAR_EN_MMIO0 |
             PEC_NEST_STK_BAR_EN_MMIO1)) {
            phb_pec_error(pec, "Changing enabled BAR unsupported");
        }
        phb->nest_regs[reg] = val & PPC_BITMASK(0, 39);
        break;
    case PEC_NEST_STK_PHB_REGS_BAR:
        if (phb->nest_regs[PEC_NEST_STK_BAR_EN] & PEC_NEST_STK_BAR_EN_PHB) {
            phb_pec_error(pec, "Changing enabled BAR unsupported");
        }
        phb->nest_regs[reg] = val & PPC_BITMASK(0, 41);
        break;
    case PEC_NEST_STK_INT_BAR:
        if (phb->nest_regs[PEC_NEST_STK_BAR_EN] & PEC_NEST_STK_BAR_EN_INT) {
            phb_pec_error(pec, "Changing enabled BAR unsupported");
        }
        phb->nest_regs[reg] = val & PPC_BITMASK(0, 27);
        break;
    case PEC_NEST_STK_BAR_EN:
        phb->nest_regs[reg] = val & PPC_BITMASK(0, 3);
        pnv_pec_phb_update_map(phb);
        break;
    case PEC_NEST_STK_DATA_FRZ_TYPE:
        /* Not used for now */
        phb->nest_regs[reg] = val & PPC_BITMASK(0, 27);
        break;
    case PEC_NEST_STK_PBCQ_SPARSE_PAGE:
        phb->nest_regs[reg] = val & PPC_BITMASK(3, 5);
        break;
    case PEC_NEST_STK_PBCQ_CACHE_INJ:
        phb->nest_regs[reg] = val & PPC_BITMASK(0, 7);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "phb4_pec: nest_xscom_write 0x%"HWADDR_PRIx
                      "=%"PRIx64"\n", addr, val);
    }
}

static const MemoryRegionOps pnv_pec_stk_nest_xscom_ops = {
    .read = pnv_pec_stk_nest_xscom_read,
    .write = pnv_pec_stk_nest_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static uint64_t pnv_pec_stk_pci_xscom_read(void *opaque, hwaddr addr,
                                           unsigned size)
{
    PnvPHB4 *phb = PNV_PHB4(opaque);
    uint32_t reg = addr >> 3;

    /* All registers are read-able */
    return phb->pci_regs[reg];
}

static void pnv_pec_stk_pci_xscom_write(void *opaque, hwaddr addr,
                                        uint64_t val, unsigned size)
{
    PnvPHB4 *phb = PNV_PHB4(opaque);
    uint32_t reg = addr >> 3;
    switch (reg) {
    case PEC_PCI_STK_PCI_FIR:
        phb->pci_regs[reg] = val & PPC_BITMASK(0, 5);
        break;
    case PEC_PCI_STK_PCI_FIR_CLR:
        phb->pci_regs[PEC_PCI_STK_PCI_FIR] &= val;
        break;
    case PEC_PCI_STK_PCI_FIR_SET:
        phb->pci_regs[PEC_PCI_STK_PCI_FIR] |= val;
        break;
    case PEC_PCI_STK_PCI_FIR_MSK:
        phb->pci_regs[reg] = val & PPC_BITMASK(0, 5);
        break;
    case PEC_PCI_STK_PCI_FIR_MSKC:
        phb->pci_regs[PEC_PCI_STK_PCI_FIR_MSK] &= val;
        break;
    case PEC_PCI_STK_PCI_FIR_MSKS:
        phb->pci_regs[PEC_PCI_STK_PCI_FIR_MSK] |= val;
        break;
    case PEC_PCI_STK_PCI_FIR_ACT0:
    case PEC_PCI_STK_PCI_FIR_ACT1:
        phb->pci_regs[reg] = val & PPC_BITMASK(0, 5);
        break;
    case PEC_PCI_STK_PCI_FIR_WOF:
        phb->pci_regs[reg] = 0;
        break;
    case PEC_PCI_STK_ETU_RESET:
        phb->pci_regs[reg] = val & PPC_BIT(0);
        /* TODO: Implement reset */
        break;
    case PEC_PCI_STK_PBAIB_ERR_REPORT:
        break;
    case PEC_PCI_STK_PBAIB_TX_CMD_CRED:
        phb->pci_regs[reg] = val &
                                 ((PPC_BITMASK(0, 2) | PPC_BITMASK(10, 18)
                                   | PPC_BITMASK(26, 34) | PPC_BITMASK(41, 50)
                                   | PPC_BITMASK(58, 63)));
        break;
    case PEC_PCI_STK_PBAIB_TX_DAT_CRED:
        phb->pci_regs[reg] = val & (PPC_BITMASK(33, 34) | PPC_BITMASK(44, 47));
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "phb4_pec_stk: pci_xscom_write 0x%"HWADDR_PRIx
                      "=%"PRIx64"\n", addr, val);
    }
}

static const MemoryRegionOps pnv_pec_stk_pci_xscom_ops = {
    .read = pnv_pec_stk_pci_xscom_read,
    .write = pnv_pec_stk_pci_xscom_write,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static int pnv_phb4_map_irq(PCIDevice *pci_dev, int irq_num)
{
    /* Check that out properly ... */
    return irq_num & 3;
}

static void pnv_phb4_set_irq(void *opaque, int irq_num, int level)
{
    PnvPHB4 *phb = PNV_PHB4(opaque);
    uint32_t lsi_base;

    /* LSI only ... */
    if (irq_num > 3) {
        phb_error(phb, "IRQ %x is not an LSI", irq_num);
    }
    lsi_base = GETFIELD(PHB_LSI_SRC_ID, phb->regs[PHB_LSI_SOURCE_ID >> 3]);
    lsi_base <<= 3;
    qemu_set_irq(phb->qirqs[lsi_base + irq_num], level);
}

static bool pnv_phb4_resolve_pe(PnvPhb4DMASpace *ds)
{
    uint64_t rtt, addr;
    uint16_t rte;
    int bus_num;
    int num_PEs;

    /* Already resolved ? */
    if (ds->pe_num != PHB_INVALID_PE) {
        return true;
    }

    /* We need to lookup the RTT */
    rtt = ds->phb->regs[PHB_RTT_BAR >> 3];
    if (!(rtt & PHB_RTT_BAR_ENABLE)) {
        phb_error(ds->phb, "DMA with RTT BAR disabled !");
        /* Set error bits ? fence ? ... */
        return false;
    }

    /* Read RTE */
    bus_num = pci_bus_num(ds->bus);
    addr = rtt & PHB_RTT_BASE_ADDRESS_MASK;
    addr += 2 * PCI_BUILD_BDF(bus_num, ds->devfn);
    if (dma_memory_read(&address_space_memory, addr, &rte,
                        sizeof(rte), MEMTXATTRS_UNSPECIFIED)) {
        phb_error(ds->phb, "Failed to read RTT entry at 0x%"PRIx64, addr);
        /* Set error bits ? fence ? ... */
        return false;
    }
    rte = be16_to_cpu(rte);

    /* Fail upon reading of invalid PE# */
    num_PEs = ds->phb->big_phb ? PNV_PHB4_MAX_PEs : (PNV_PHB4_MAX_PEs >> 1);
    if (rte >= num_PEs) {
        phb_error(ds->phb, "RTE for RID 0x%x invalid (%04x", ds->devfn, rte);
        rte &= num_PEs - 1;
    }
    ds->pe_num = rte;
    return true;
}

static void pnv_phb4_translate_tve(PnvPhb4DMASpace *ds, hwaddr addr,
                                   bool is_write, uint64_t tve,
                                   IOMMUTLBEntry *tlb)
{
    uint64_t tta = GETFIELD(IODA3_TVT_TABLE_ADDR, tve);
    int32_t  lev = GETFIELD(IODA3_TVT_NUM_LEVELS, tve);
    uint32_t tts = GETFIELD(IODA3_TVT_TCE_TABLE_SIZE, tve);
    uint32_t tps = GETFIELD(IODA3_TVT_IO_PSIZE, tve);

    /* Invalid levels */
    if (lev > 4) {
        phb_error(ds->phb, "Invalid #levels in TVE %d", lev);
        return;
    }

    /* Invalid entry */
    if (tts == 0) {
        phb_error(ds->phb, "Access to invalid TVE");
        return;
    }

    /* IO Page Size of 0 means untranslated, else use TCEs */
    if (tps == 0) {
        /* TODO: Handle boundaries */

        /* Use 4k pages like q35 ... for now */
        tlb->iova = addr & 0xfffffffffffff000ull;
        tlb->translated_addr = addr & 0x0003fffffffff000ull;
        tlb->addr_mask = 0xfffull;
        tlb->perm = IOMMU_RW;
    } else {
        uint32_t tce_shift, tbl_shift, sh;
        uint64_t base, taddr, tce, tce_mask;

        /* Address bits per bottom level TCE entry */
        tce_shift = tps + 11;

        /* Address bits per table level */
        tbl_shift = tts + 8;

        /* Top level table base address */
        base = tta << 12;

        /* Total shift to first level */
        sh = tbl_shift * lev + tce_shift;

        /* TODO: Limit to support IO page sizes */

        /* TODO: Multi-level untested */
        do {
            lev--;

            /* Grab the TCE address */
            taddr = base | (((addr >> sh) & ((1ul << tbl_shift) - 1)) << 3);
            if (dma_memory_read(&address_space_memory, taddr, &tce,
                                sizeof(tce), MEMTXATTRS_UNSPECIFIED)) {
                phb_error(ds->phb, "Failed to read TCE at 0x%"PRIx64, taddr);
                return;
            }
            tce = be64_to_cpu(tce);

            /* Check permission for indirect TCE */
            if ((lev >= 0) && !(tce & 3)) {
                phb_error(ds->phb, "Invalid indirect TCE at 0x%"PRIx64, taddr);
                phb_error(ds->phb, " xlate %"PRIx64":%c TVE=%"PRIx64, addr,
                           is_write ? 'W' : 'R', tve);
                phb_error(ds->phb, " tta=%"PRIx64" lev=%d tts=%d tps=%d",
                           tta, lev, tts, tps);
                return;
            }
            sh -= tbl_shift;
            base = tce & ~0xfffull;
        } while (lev >= 0);

        /* We exit the loop with TCE being the final TCE */
        if ((is_write & !(tce & 2)) || ((!is_write) && !(tce & 1))) {
            phb_error(ds->phb, "TCE access fault at 0x%"PRIx64, taddr);
            phb_error(ds->phb, " xlate %"PRIx64":%c TVE=%"PRIx64, addr,
                       is_write ? 'W' : 'R', tve);
            phb_error(ds->phb, " tta=%"PRIx64" lev=%d tts=%d tps=%d",
                       tta, lev, tts, tps);
            return;
        }
        tce_mask = ~((1ull << tce_shift) - 1);
        tlb->iova = addr & tce_mask;
        tlb->translated_addr = tce & tce_mask;
        tlb->addr_mask = ~tce_mask;
        tlb->perm = tce & 3;
    }
}

static IOMMUTLBEntry pnv_phb4_translate_iommu(IOMMUMemoryRegion *iommu,
                                              hwaddr addr,
                                              IOMMUAccessFlags flag,
                                              int iommu_idx)
{
    PnvPhb4DMASpace *ds = container_of(iommu, PnvPhb4DMASpace, dma_mr);
    int tve_sel;
    uint64_t tve, cfg;
    IOMMUTLBEntry ret = {
        .target_as = &address_space_memory,
        .iova = addr,
        .translated_addr = 0,
        .addr_mask = ~(hwaddr)0,
        .perm = IOMMU_NONE,
    };

    /* Resolve PE# */
    if (!pnv_phb4_resolve_pe(ds)) {
        phb_error(ds->phb, "Failed to resolve PE# for bus @%p (%d) devfn 0x%x",
                   ds->bus, pci_bus_num(ds->bus), ds->devfn);
        return ret;
    }

    /* Check top bits */
    switch (addr >> 60) {
    case 00:
        /* DMA or 32-bit MSI ? */
        cfg = ds->phb->regs[PHB_PHB4_CONFIG >> 3];
        if ((cfg & PHB_PHB4C_32BIT_MSI_EN) &&
            ((addr & 0xffffffffffff0000ull) == 0xffff0000ull)) {
            phb_error(ds->phb, "xlate on 32-bit MSI region");
            return ret;
        }
        /* Choose TVE XXX Use PHB4 Control Register */
        tve_sel = (addr >> 59) & 1;
        tve = ds->phb->ioda_TVT[ds->pe_num * 2 + tve_sel];
        pnv_phb4_translate_tve(ds, addr, flag & IOMMU_WO, tve, &ret);
        break;
    case 01:
        phb_error(ds->phb, "xlate on 64-bit MSI region");
        break;
    default:
        phb_error(ds->phb, "xlate on unsupported address 0x%"PRIx64, addr);
    }
    return ret;
}

#define TYPE_PNV_PHB4_IOMMU_MEMORY_REGION "pnv-phb4-iommu-memory-region"
DECLARE_INSTANCE_CHECKER(IOMMUMemoryRegion, PNV_PHB4_IOMMU_MEMORY_REGION,
                         TYPE_PNV_PHB4_IOMMU_MEMORY_REGION)

static void pnv_phb4_iommu_memory_region_class_init(ObjectClass *klass,
                                                    void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = pnv_phb4_translate_iommu;
}

static const TypeInfo pnv_phb4_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_PNV_PHB4_IOMMU_MEMORY_REGION,
    .class_init = pnv_phb4_iommu_memory_region_class_init,
};

/*
 * Return the index/phb-id of a PHB4 that belongs to a
 * pec->stacks[stack_index] stack.
 */
int pnv_phb4_pec_get_phb_id(PnvPhb4PecState *pec, int stack_index)
{
    PnvPhb4PecClass *pecc = PNV_PHB4_PEC_GET_CLASS(pec);
    int index = pec->index;
    int offset = 0;

    while (index--) {
        offset += pecc->num_phbs[index];
    }

    return offset + stack_index;
}

/*
 * MSI/MSIX memory region implementation.
 * The handler handles both MSI and MSIX.
 */
static void pnv_phb4_msi_write(void *opaque, hwaddr addr,
                               uint64_t data, unsigned size)
{
    PnvPhb4DMASpace *ds = opaque;
    PnvPHB4 *phb = ds->phb;

    uint32_t src = ((addr >> 4) & 0xffff) | (data & 0x1f);

    /* Resolve PE# */
    if (!pnv_phb4_resolve_pe(ds)) {
        phb_error(phb, "Failed to resolve PE# for bus @%p (%d) devfn 0x%x",
                   ds->bus, pci_bus_num(ds->bus), ds->devfn);
        return;
    }

    /* TODO: Check it doesn't collide with LSIs */
    if (src >= phb->xsrc.nr_irqs) {
        phb_error(phb, "MSI %d out of bounds", src);
        return;
    }

    /* TODO: check PE/MSI assignment */

    qemu_irq_pulse(phb->qirqs[src]);
}

/* There is no .read as the read result is undefined by PCI spec */
static uint64_t pnv_phb4_msi_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvPhb4DMASpace *ds = opaque;

    phb_error(ds->phb, "Invalid MSI read @ 0x%" HWADDR_PRIx, addr);
    return -1;
}

static const MemoryRegionOps pnv_phb4_msi_ops = {
    .read = pnv_phb4_msi_read,
    .write = pnv_phb4_msi_write,
    .endianness = DEVICE_LITTLE_ENDIAN
};

static PnvPhb4DMASpace *pnv_phb4_dma_find(PnvPHB4 *phb, PCIBus *bus, int devfn)
{
    PnvPhb4DMASpace *ds;

    QLIST_FOREACH(ds, &phb->dma_spaces, list) {
        if (ds->bus == bus && ds->devfn == devfn) {
            break;
        }
    }
    return ds;
}

static AddressSpace *pnv_phb4_dma_iommu(PCIBus *bus, void *opaque, int devfn)
{
    PnvPHB4 *phb = opaque;
    PnvPhb4DMASpace *ds;
    char name[32];

    ds = pnv_phb4_dma_find(phb, bus, devfn);

    if (ds == NULL) {
        ds = g_new0(PnvPhb4DMASpace, 1);
        ds->bus = bus;
        ds->devfn = devfn;
        ds->pe_num = PHB_INVALID_PE;
        ds->phb = phb;
        snprintf(name, sizeof(name), "phb4-%d.%d-iommu", phb->chip_id,
                 phb->phb_id);
        memory_region_init_iommu(&ds->dma_mr, sizeof(ds->dma_mr),
                                 TYPE_PNV_PHB4_IOMMU_MEMORY_REGION,
                                 OBJECT(phb), name, UINT64_MAX);
        address_space_init(&ds->dma_as, MEMORY_REGION(&ds->dma_mr),
                           name);
        memory_region_init_io(&ds->msi32_mr, OBJECT(phb), &pnv_phb4_msi_ops,
                              ds, "msi32", 0x10000);
        memory_region_init_io(&ds->msi64_mr, OBJECT(phb), &pnv_phb4_msi_ops,
                              ds, "msi64", 0x100000);
        pnv_phb4_update_msi_regions(ds);

        QLIST_INSERT_HEAD(&phb->dma_spaces, ds, list);
    }
    return &ds->dma_as;
}

static void pnv_phb4_xscom_realize(PnvPHB4 *phb)
{
    PnvPhb4PecState *pec = phb->pec;
    PnvPhb4PecClass *pecc = PNV_PHB4_PEC_GET_CLASS(pec);
    int stack_no = pnv_phb4_get_phb_stack_no(phb);
    uint32_t pec_nest_base;
    uint32_t pec_pci_base;
    char name[64];

    assert(pec);

    /* Initialize the XSCOM regions for the stack registers */
    snprintf(name, sizeof(name), "xscom-pec-%d.%d-nest-phb-%d",
             pec->chip_id, pec->index, stack_no);
    pnv_xscom_region_init(&phb->nest_regs_mr, OBJECT(phb),
                          &pnv_pec_stk_nest_xscom_ops, phb, name,
                          PHB4_PEC_NEST_STK_REGS_COUNT);

    snprintf(name, sizeof(name), "xscom-pec-%d.%d-pci-phb-%d",
             pec->chip_id, pec->index, stack_no);
    pnv_xscom_region_init(&phb->pci_regs_mr, OBJECT(phb),
                          &pnv_pec_stk_pci_xscom_ops, phb, name,
                          PHB4_PEC_PCI_STK_REGS_COUNT);

    /* PHB pass-through */
    snprintf(name, sizeof(name), "xscom-pec-%d.%d-phb-%d",
             pec->chip_id, pec->index, stack_no);
    pnv_xscom_region_init(&phb->phb_regs_mr, OBJECT(phb),
                          &pnv_phb4_xscom_ops, phb, name, 0x40);

    pec_nest_base = pecc->xscom_nest_base(pec);
    pec_pci_base = pecc->xscom_pci_base(pec);

    /* Populate the XSCOM address space. */
    pnv_xscom_add_subregion(pec->chip,
                            pec_nest_base + 0x40 * (stack_no + 1),
                            &phb->nest_regs_mr);
    pnv_xscom_add_subregion(pec->chip,
                            pec_pci_base + 0x40 * (stack_no + 1),
                            &phb->pci_regs_mr);
    pnv_xscom_add_subregion(pec->chip,
                            pec_pci_base + PNV9_XSCOM_PEC_PCI_STK0 +
                            0x40 * stack_no,
                            &phb->phb_regs_mr);
}

static PCIIOMMUOps pnv_phb4_iommu_ops = {
    .get_address_space = pnv_phb4_dma_iommu,
};

static void pnv_phb4_instance_init(Object *obj)
{
    PnvPHB4 *phb = PNV_PHB4(obj);

    QLIST_INIT(&phb->dma_spaces);

    /* XIVE interrupt source object */
    object_initialize_child(obj, "source", &phb->xsrc, TYPE_XIVE_SOURCE);
}

void pnv_phb4_bus_init(DeviceState *dev, PnvPHB4 *phb)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(dev);
    char name[32];

    /*
     * PHB4 doesn't support IO space. However, qemu gets very upset if
     * we don't have an IO region to anchor IO BARs onto so we just
     * initialize one which we never hook up to anything
     */
    snprintf(name, sizeof(name), "phb4-%d.%d-pci-io", phb->chip_id,
             phb->phb_id);
    memory_region_init(&phb->pci_io, OBJECT(phb), name, 0x10000);

    snprintf(name, sizeof(name), "phb4-%d.%d-pci-mmio", phb->chip_id,
             phb->phb_id);
    memory_region_init(&phb->pci_mmio, OBJECT(phb), name,
                       PCI_MMIO_TOTAL_SIZE);

    pci->bus = pci_register_root_bus(dev, dev->id ? dev->id : NULL,
                                     pnv_phb4_set_irq, pnv_phb4_map_irq, phb,
                                     &phb->pci_mmio, &phb->pci_io,
                                     0, 4, TYPE_PNV_PHB4_ROOT_BUS);

    object_property_set_int(OBJECT(pci->bus), "phb-id", phb->phb_id,
                            &error_abort);
    object_property_set_int(OBJECT(pci->bus), "chip-id", phb->chip_id,
                            &error_abort);

    pci_setup_iommu(pci->bus, &pnv_phb4_iommu_ops, phb);
    pci->bus->flags |= PCI_BUS_EXTENDED_CONFIG_SPACE;
}

static void pnv_phb4_realize(DeviceState *dev, Error **errp)
{
    PnvPHB4 *phb = PNV_PHB4(dev);
    XiveSource *xsrc = &phb->xsrc;
    int nr_irqs;
    char name[32];

    /* Set the "big_phb" flag */
    phb->big_phb = phb->phb_id == 0 || phb->phb_id == 3;

    /* Controller Registers */
    snprintf(name, sizeof(name), "phb4-%d.%d-regs", phb->chip_id,
             phb->phb_id);
    memory_region_init_io(&phb->mr_regs, OBJECT(phb), &pnv_phb4_reg_ops, phb,
                          name, 0x2000);

    /* Setup XIVE Source */
    if (phb->big_phb) {
        nr_irqs = PNV_PHB4_MAX_INTs;
    } else {
        nr_irqs = PNV_PHB4_MAX_INTs >> 1;
    }
    object_property_set_int(OBJECT(xsrc), "nr-irqs", nr_irqs, &error_fatal);
    object_property_set_link(OBJECT(xsrc), "xive", OBJECT(phb), &error_fatal);
    if (!qdev_realize(DEVICE(xsrc), NULL, errp)) {
        return;
    }

    pnv_phb4_update_xsrc(phb);

    phb->qirqs = qemu_allocate_irqs(xive_source_set_irq, xsrc, xsrc->nr_irqs);

    pnv_phb4_xscom_realize(phb);
}

/*
 * Address base trigger mode (POWER10)
 *
 * Trigger directly the IC ESB page
 */
static void pnv_phb4_xive_notify_abt(PnvPHB4 *phb, uint32_t srcno,
                                     bool pq_checked)
{
    uint64_t notif_port = phb->regs[PHB_INT_NOTIFY_ADDR >> 3];
    uint64_t data = 0; /* trigger data : don't care */
    hwaddr addr;
    MemTxResult result;
    int esb_shift;

    if (notif_port & PHB_INT_NOTIFY_ADDR_64K) {
        esb_shift = 16;
    } else {
        esb_shift = 12;
    }

    /* Compute the address of the IC ESB management page */
    addr = (notif_port & ~PHB_INT_NOTIFY_ADDR_64K);
    addr |= (1ull << (esb_shift + 1)) * srcno;
    addr |= (1ull << esb_shift);

    /*
     * When the PQ state bits are checked on the PHB, the associated
     * PQ state bits on the IC should be ignored. Use the unconditional
     * trigger offset to inject a trigger on the IC. This is always
     * the case for LSIs
     */
    if (pq_checked) {
        addr |= XIVE_ESB_INJECT;
    }

    trace_pnv_phb4_xive_notify_ic(addr, data);

    address_space_stq_be(&address_space_memory, addr, data,
                         MEMTXATTRS_UNSPECIFIED, &result);
    if (result != MEMTX_OK) {
        phb_error(phb, "trigger failed @%"HWADDR_PRIx "\n", addr);
        return;
    }
}

static void pnv_phb4_xive_notify_ic(PnvPHB4 *phb, uint32_t srcno,
                                    bool pq_checked)
{
    uint64_t notif_port = phb->regs[PHB_INT_NOTIFY_ADDR >> 3];
    uint32_t offset = phb->regs[PHB_INT_NOTIFY_INDEX >> 3];
    uint64_t data = offset | srcno;
    MemTxResult result;

    if (pq_checked) {
        data |= XIVE_TRIGGER_PQ;
    }

    trace_pnv_phb4_xive_notify_ic(notif_port, data);

    address_space_stq_be(&address_space_memory, notif_port, data,
                         MEMTXATTRS_UNSPECIFIED, &result);
    if (result != MEMTX_OK) {
        phb_error(phb, "trigger failed @%"HWADDR_PRIx "\n", notif_port);
        return;
    }
}

static void pnv_phb4_xive_notify(XiveNotifier *xf, uint32_t srcno,
                                 bool pq_checked)
{
    PnvPHB4 *phb = PNV_PHB4(xf);

    if (phb->regs[PHB_CTRLR >> 3] & PHB_CTRLR_IRQ_ABT_MODE) {
        pnv_phb4_xive_notify_abt(phb, srcno, pq_checked);
    } else {
        pnv_phb4_xive_notify_ic(phb, srcno, pq_checked);
    }
}

static const Property pnv_phb4_properties[] = {
    DEFINE_PROP_UINT32("index", PnvPHB4, phb_id, 0),
    DEFINE_PROP_UINT32("chip-id", PnvPHB4, chip_id, 0),
    DEFINE_PROP_LINK("pec", PnvPHB4, pec, TYPE_PNV_PHB4_PEC,
                     PnvPhb4PecState *),
    DEFINE_PROP_LINK("phb-base", PnvPHB4, phb_base, TYPE_PNV_PHB, PnvPHB *),
};

static void pnv_phb4_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XiveNotifierClass *xfc = XIVE_NOTIFIER_CLASS(klass);

    dc->realize         = pnv_phb4_realize;
    device_class_set_props(dc, pnv_phb4_properties);
    dc->user_creatable  = false;

    xfc->notify         = pnv_phb4_xive_notify;
}

static const TypeInfo pnv_phb4_type_info = {
    .name          = TYPE_PNV_PHB4,
    .parent        = TYPE_DEVICE,
    .instance_init = pnv_phb4_instance_init,
    .instance_size = sizeof(PnvPHB4),
    .class_init    = pnv_phb4_class_init,
    .interfaces = (InterfaceInfo[]) {
            { TYPE_XIVE_NOTIFIER },
            { },
    }
};

static const TypeInfo pnv_phb5_type_info = {
    .name          = TYPE_PNV_PHB5,
    .parent        = TYPE_PNV_PHB4,
    .instance_size = sizeof(PnvPHB4),
};

static void pnv_phb4_root_bus_get_prop(Object *obj, Visitor *v,
                                       const char *name,
                                       void *opaque, Error **errp)
{
    PnvPHB4RootBus *bus = PNV_PHB4_ROOT_BUS(obj);
    uint64_t value = 0;

    if (strcmp(name, "phb-id") == 0) {
        value = bus->phb_id;
    } else {
        value = bus->chip_id;
    }

    visit_type_size(v, name, &value, errp);
}

static void pnv_phb4_root_bus_set_prop(Object *obj, Visitor *v,
                                       const char *name,
                                       void *opaque, Error **errp)

{
    PnvPHB4RootBus *bus = PNV_PHB4_ROOT_BUS(obj);
    uint64_t value;

    if (!visit_type_size(v, name, &value, errp)) {
        return;
    }

    if (strcmp(name, "phb-id") == 0) {
        bus->phb_id = value;
    } else {
        bus->chip_id = value;
    }
}

static void pnv_phb4_root_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    object_class_property_add(klass, "phb-id", "int",
                              pnv_phb4_root_bus_get_prop,
                              pnv_phb4_root_bus_set_prop,
                              NULL, NULL);

    object_class_property_add(klass, "chip-id", "int",
                              pnv_phb4_root_bus_get_prop,
                              pnv_phb4_root_bus_set_prop,
                              NULL, NULL);

    /*
     * PHB4 has only a single root complex. Enforce the limit on the
     * parent bus
     */
    k->max_dev = 1;
}

static const TypeInfo pnv_phb4_root_bus_info = {
    .name = TYPE_PNV_PHB4_ROOT_BUS,
    .parent = TYPE_PCIE_BUS,
    .instance_size = sizeof(PnvPHB4RootBus),
    .class_init = pnv_phb4_root_bus_class_init,
};

static void pnv_phb4_register_types(void)
{
    type_register_static(&pnv_phb4_root_bus_info);
    type_register_static(&pnv_phb4_type_info);
    type_register_static(&pnv_phb5_type_info);
    type_register_static(&pnv_phb4_iommu_memory_region_info);
}

type_init(pnv_phb4_register_types);

void pnv_phb4_pic_print_info(PnvPHB4 *phb, GString *buf)
{
    uint64_t notif_port =
        phb->regs[PHB_INT_NOTIFY_ADDR >> 3] & ~PHB_INT_NOTIFY_ADDR_64K;
    uint32_t offset = phb->regs[PHB_INT_NOTIFY_INDEX >> 3];
    bool abt = !!(phb->regs[PHB_CTRLR >> 3] & PHB_CTRLR_IRQ_ABT_MODE);

    g_string_append_printf(buf,
                           "PHB4[%x:%x] Source %08x .. %08x "
                           "%s @%"HWADDR_PRIx"\n",
                           phb->chip_id, phb->phb_id,
                           offset, offset + phb->xsrc.nr_irqs - 1,
                           abt ? "ABT" : "",
                           notif_port);
    xive_source_pic_print_info(&phb->xsrc, 0, buf);
}
