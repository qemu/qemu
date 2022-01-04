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
#include "qemu-common.h"
#include "monitor/monitor.h"
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

/*
 * QEMU version of the GETFIELD/SETFIELD macros
 *
 * These are common with the PnvXive model.
 */
static inline uint64_t GETFIELD(uint64_t mask, uint64_t word)
{
    return (word & mask) >> ctz64(mask);
}

static inline uint64_t SETFIELD(uint64_t mask, uint64_t word,
                                uint64_t value)
{
    return (word & ~mask) | ((value << ctz64(mask)) & mask);
}

static PCIDevice *pnv_phb4_find_cfg_dev(PnvPHB4 *phb)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(phb);
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
    PCIHostState *pci = PCI_HOST_BRIDGE(phb);
    PCIDevice *pdev;

    if (size != 4) {
        phb_error(phb, "rc_config_write invalid size %d\n", size);
        return;
    }

    pdev = pci_find_device(pci->bus, 0, 0);
    assert(pdev);

    pci_host_config_write_common(pdev, off, PHB_RC_CONFIG_SIZE,
                                 bswap32(val), 4);
}

static uint64_t pnv_phb4_rc_config_read(PnvPHB4 *phb, unsigned off,
                                        unsigned size)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(phb);
    PCIDevice *pdev;
    uint64_t val;

    if (size != 4) {
        phb_error(phb, "rc_config_read invalid size %d\n", size);
        return ~0ull;
    }

    pdev = pci_find_device(pci->bus, 0, 0);
    assert(pdev);

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

    /* TODO: Figure out how to implemet/decode AOMASK */

    /* Check if it matches an enabled MMIO region in the PEC stack */
    if (memory_region_is_mapped(&phb->stack->mmbar0) &&
        base >= phb->stack->mmio0_base &&
        (base + size) <= (phb->stack->mmio0_base + phb->stack->mmio0_size)) {
        parent = &phb->stack->mmbar0;
        base -= phb->stack->mmio0_base;
    } else if (memory_region_is_mapped(&phb->stack->mmbar1) &&
        base >= phb->stack->mmio1_base &&
        (base + size) <= (phb->stack->mmio1_base + phb->stack->mmio1_size)) {
        parent = &phb->stack->mmbar1;
        base -= phb->stack->mmio1_base;
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

        /* Copy accross the valid bit to the other half */
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
        return phb->version;

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
        while ((lev--) >= 0) {
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
        }

        /* We exit the loop with TCE being the final TCE */
        tce_mask = ~((1ull << tce_shift) - 1);
        tlb->iova = addr & tce_mask;
        tlb->translated_addr = tce & tce_mask;
        tlb->addr_mask = ~tce_mask;
        tlb->perm = tce & 3;
        if ((is_write & !(tce & 2)) || ((!is_write) && !(tce & 1))) {
            phb_error(ds->phb, "TCE access fault at 0x%"PRIx64, taddr);
            phb_error(ds->phb, " xlate %"PRIx64":%c TVE=%"PRIx64, addr,
                       is_write ? 'W' : 'R', tve);
            phb_error(ds->phb, " tta=%"PRIx64" lev=%d tts=%d tps=%d",
                       tta, lev, tts, tps);
        }
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

    /* TODO: check PE/MSI assignement */

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
        ds = g_malloc0(sizeof(PnvPhb4DMASpace));
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

static void pnv_phb4_instance_init(Object *obj)
{
    PnvPHB4 *phb = PNV_PHB4(obj);

    QLIST_INIT(&phb->dma_spaces);

    /* XIVE interrupt source object */
    object_initialize_child(obj, "source", &phb->xsrc, TYPE_XIVE_SOURCE);

    /* Root Port */
    object_initialize_child(obj, "root", &phb->root, TYPE_PNV_PHB4_ROOT_PORT);

    qdev_prop_set_int32(DEVICE(&phb->root), "addr", PCI_DEVFN(0, 0));
    qdev_prop_set_bit(DEVICE(&phb->root), "multifunction", false);
}

static void pnv_phb4_realize(DeviceState *dev, Error **errp)
{
    PnvPHB4 *phb = PNV_PHB4(dev);
    PCIHostState *pci = PCI_HOST_BRIDGE(dev);
    XiveSource *xsrc = &phb->xsrc;
    int nr_irqs;
    char name[32];

    assert(phb->stack);

    /* Set the "big_phb" flag */
    phb->big_phb = phb->phb_id == 0 || phb->phb_id == 3;

    /* Controller Registers */
    snprintf(name, sizeof(name), "phb4-%d.%d-regs", phb->chip_id,
             phb->phb_id);
    memory_region_init_io(&phb->mr_regs, OBJECT(phb), &pnv_phb4_reg_ops, phb,
                          name, 0x2000);

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

    pci->bus = pci_register_root_bus(dev, dev->id,
                                     pnv_phb4_set_irq, pnv_phb4_map_irq, phb,
                                     &phb->pci_mmio, &phb->pci_io,
                                     0, 4, TYPE_PNV_PHB4_ROOT_BUS);
    pci_setup_iommu(pci->bus, pnv_phb4_dma_iommu, phb);
    pci->bus->flags |= PCI_BUS_EXTENDED_CONFIG_SPACE;

    /* Add a single Root port */
    qdev_prop_set_uint8(DEVICE(&phb->root), "chassis", phb->chip_id);
    qdev_prop_set_uint16(DEVICE(&phb->root), "slot", phb->phb_id);
    qdev_realize(DEVICE(&phb->root), BUS(pci->bus), &error_fatal);

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
}

static const char *pnv_phb4_root_bus_path(PCIHostState *host_bridge,
                                          PCIBus *rootbus)
{
    PnvPHB4 *phb = PNV_PHB4(host_bridge);

    snprintf(phb->bus_path, sizeof(phb->bus_path), "00%02x:%02x",
             phb->chip_id, phb->phb_id);
    return phb->bus_path;
}

static void pnv_phb4_xive_notify(XiveNotifier *xf, uint32_t srcno)
{
    PnvPHB4 *phb = PNV_PHB4(xf);
    uint64_t notif_port = phb->regs[PHB_INT_NOTIFY_ADDR >> 3];
    uint32_t offset = phb->regs[PHB_INT_NOTIFY_INDEX >> 3];
    uint64_t data = XIVE_TRIGGER_PQ | offset | srcno;
    MemTxResult result;

    trace_pnv_phb4_xive_notify(notif_port, data);

    address_space_stq_be(&address_space_memory, notif_port, data,
                         MEMTXATTRS_UNSPECIFIED, &result);
    if (result != MEMTX_OK) {
        phb_error(phb, "trigger failed @%"HWADDR_PRIx "\n", notif_port);
        return;
    }
}

static Property pnv_phb4_properties[] = {
        DEFINE_PROP_UINT32("index", PnvPHB4, phb_id, 0),
        DEFINE_PROP_UINT32("chip-id", PnvPHB4, chip_id, 0),
        DEFINE_PROP_UINT64("version", PnvPHB4, version, 0),
        DEFINE_PROP_LINK("stack", PnvPHB4, stack, TYPE_PNV_PHB4_PEC_STACK,
                         PnvPhb4PecStack *),
        DEFINE_PROP_END_OF_LIST(),
};

static void pnv_phb4_class_init(ObjectClass *klass, void *data)
{
    PCIHostBridgeClass *hc = PCI_HOST_BRIDGE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    XiveNotifierClass *xfc = XIVE_NOTIFIER_CLASS(klass);

    hc->root_bus_path   = pnv_phb4_root_bus_path;
    dc->realize         = pnv_phb4_realize;
    device_class_set_props(dc, pnv_phb4_properties);
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->user_creatable  = false;

    xfc->notify         = pnv_phb4_xive_notify;
}

static const TypeInfo pnv_phb4_type_info = {
    .name          = TYPE_PNV_PHB4,
    .parent        = TYPE_PCIE_HOST_BRIDGE,
    .instance_init = pnv_phb4_instance_init,
    .instance_size = sizeof(PnvPHB4),
    .class_init    = pnv_phb4_class_init,
    .interfaces = (InterfaceInfo[]) {
            { TYPE_XIVE_NOTIFIER },
            { },
    }
};

static void pnv_phb4_root_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    /*
     * PHB4 has only a single root complex. Enforce the limit on the
     * parent bus
     */
    k->max_dev = 1;
}

static const TypeInfo pnv_phb4_root_bus_info = {
    .name = TYPE_PNV_PHB4_ROOT_BUS,
    .parent = TYPE_PCIE_BUS,
    .class_init = pnv_phb4_root_bus_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void pnv_phb4_root_port_reset(DeviceState *dev)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(dev);
    PCIDevice *d = PCI_DEVICE(dev);
    uint8_t *conf = d->config;

    rpc->parent_reset(dev);

    pci_byte_test_and_set_mask(conf + PCI_IO_BASE,
                               PCI_IO_RANGE_MASK & 0xff);
    pci_byte_test_and_clear_mask(conf + PCI_IO_LIMIT,
                                 PCI_IO_RANGE_MASK & 0xff);
    pci_set_word(conf + PCI_MEMORY_BASE, 0);
    pci_set_word(conf + PCI_MEMORY_LIMIT, 0xfff0);
    pci_set_word(conf + PCI_PREF_MEMORY_BASE, 0x1);
    pci_set_word(conf + PCI_PREF_MEMORY_LIMIT, 0xfff1);
    pci_set_long(conf + PCI_PREF_BASE_UPPER32, 0x1); /* Hack */
    pci_set_long(conf + PCI_PREF_LIMIT_UPPER32, 0xffffffff);
}

static void pnv_phb4_root_port_realize(DeviceState *dev, Error **errp)
{
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_GET_CLASS(dev);
    Error *local_err = NULL;

    rpc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}

static void pnv_phb4_root_port_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    PCIERootPortClass *rpc = PCIE_ROOT_PORT_CLASS(klass);

    dc->desc     = "IBM PHB4 PCIE Root Port";
    dc->user_creatable = false;

    device_class_set_parent_realize(dc, pnv_phb4_root_port_realize,
                                    &rpc->parent_realize);
    device_class_set_parent_reset(dc, pnv_phb4_root_port_reset,
                                  &rpc->parent_reset);

    k->vendor_id = PCI_VENDOR_ID_IBM;
    k->device_id = PNV_PHB4_DEVICE_ID;
    k->revision  = 0;

    rpc->exp_offset = 0x48;
    rpc->aer_offset = 0x100;

    dc->reset = &pnv_phb4_root_port_reset;
}

static const TypeInfo pnv_phb4_root_port_info = {
    .name          = TYPE_PNV_PHB4_ROOT_PORT,
    .parent        = TYPE_PCIE_ROOT_PORT,
    .instance_size = sizeof(PnvPHB4RootPort),
    .class_init    = pnv_phb4_root_port_class_init,
};

static void pnv_phb4_register_types(void)
{
    type_register_static(&pnv_phb4_root_bus_info);
    type_register_static(&pnv_phb4_root_port_info);
    type_register_static(&pnv_phb4_type_info);
    type_register_static(&pnv_phb4_iommu_memory_region_info);
}

type_init(pnv_phb4_register_types);

void pnv_phb4_update_regions(PnvPhb4PecStack *stack)
{
    PnvPHB4 *phb = &stack->phb;

    /* Unmap first always */
    if (memory_region_is_mapped(&phb->mr_regs)) {
        memory_region_del_subregion(&stack->phbbar, &phb->mr_regs);
    }
    if (memory_region_is_mapped(&phb->xsrc.esb_mmio)) {
        memory_region_del_subregion(&stack->intbar, &phb->xsrc.esb_mmio);
    }

    /* Map registers if enabled */
    if (memory_region_is_mapped(&stack->phbbar)) {
        memory_region_add_subregion(&stack->phbbar, 0, &phb->mr_regs);
    }

    /* Map ESB if enabled */
    if (memory_region_is_mapped(&stack->intbar)) {
        memory_region_add_subregion(&stack->intbar, 0, &phb->xsrc.esb_mmio);
    }

    /* Check/update m32 */
    pnv_phb4_check_all_mbt(phb);
}

void pnv_phb4_pic_print_info(PnvPHB4 *phb, Monitor *mon)
{
    uint32_t offset = phb->regs[PHB_INT_NOTIFY_INDEX >> 3];

    monitor_printf(mon, "PHB4[%x:%x] Source %08x .. %08x\n",
                   phb->chip_id, phb->phb_id,
                   offset, offset + phb->xsrc.nr_irqs - 1);
    xive_source_pic_print_info(&phb->xsrc, 0, mon);
}
