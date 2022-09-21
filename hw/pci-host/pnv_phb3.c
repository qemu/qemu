/*
 * QEMU PowerPC PowerNV (POWER8) PHB3 model
 *
 * Copyright (c) 2014-2020, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "hw/pci-host/pnv_phb3_regs.h"
#include "hw/pci-host/pnv_phb.h"
#include "hw/pci-host/pnv_phb3.h"
#include "hw/pci/pcie_host.h"
#include "hw/pci/pcie_port.h"
#include "hw/ppc/pnv.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qom/object.h"
#include "sysemu/sysemu.h"

#define phb3_error(phb, fmt, ...)                                       \
    qemu_log_mask(LOG_GUEST_ERROR, "phb3[%d:%d]: " fmt "\n",            \
                  (phb)->chip_id, (phb)->phb_id, ## __VA_ARGS__)

static PCIDevice *pnv_phb3_find_cfg_dev(PnvPHB3 *phb)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(phb->phb_base);
    uint64_t addr = phb->regs[PHB_CONFIG_ADDRESS >> 3];
    uint8_t bus, devfn;

    if (!(addr >> 63)) {
        return NULL;
    }
    bus = (addr >> 52) & 0xff;
    devfn = (addr >> 44) & 0xff;

    return pci_find_device(pci->bus, bus, devfn);
}

/*
 * The CONFIG_DATA register expects little endian accesses, but as the
 * region is big endian, we have to swap the value.
 */
static void pnv_phb3_config_write(PnvPHB3 *phb, unsigned off,
                                  unsigned size, uint64_t val)
{
    uint32_t cfg_addr, limit;
    PCIDevice *pdev;

    pdev = pnv_phb3_find_cfg_dev(phb);
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

static uint64_t pnv_phb3_config_read(PnvPHB3 *phb, unsigned off,
                                     unsigned size)
{
    uint32_t cfg_addr, limit;
    PCIDevice *pdev;
    uint64_t val;

    pdev = pnv_phb3_find_cfg_dev(phb);
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

static void pnv_phb3_check_m32(PnvPHB3 *phb)
{
    uint64_t base, start, size;
    MemoryRegion *parent;
    PnvPBCQState *pbcq = &phb->pbcq;

    if (memory_region_is_mapped(&phb->mr_m32)) {
        memory_region_del_subregion(phb->mr_m32.container, &phb->mr_m32);
    }

    if (!(phb->regs[PHB_PHB3_CONFIG >> 3] & PHB_PHB3C_M32_EN)) {
        return;
    }

    /* Grab geometry from registers */
    base = phb->regs[PHB_M32_BASE_ADDR >> 3];
    start = phb->regs[PHB_M32_START_ADDR >> 3];
    size = ~(phb->regs[PHB_M32_BASE_MASK >> 3] | 0xfffc000000000000ull) + 1;

    /* Check if it matches an enabled MMIO region in the PBCQ */
    if (memory_region_is_mapped(&pbcq->mmbar0) &&
        base >= pbcq->mmio0_base &&
        (base + size) <= (pbcq->mmio0_base + pbcq->mmio0_size)) {
        parent = &pbcq->mmbar0;
        base -= pbcq->mmio0_base;
    } else if (memory_region_is_mapped(&pbcq->mmbar1) &&
               base >= pbcq->mmio1_base &&
               (base + size) <= (pbcq->mmio1_base + pbcq->mmio1_size)) {
        parent = &pbcq->mmbar1;
        base -= pbcq->mmio1_base;
    } else {
        return;
    }

    /* Create alias */
    memory_region_init_alias(&phb->mr_m32, OBJECT(phb), "phb3-m32",
                             &phb->pci_mmio, start, size);
    memory_region_add_subregion(parent, base, &phb->mr_m32);
}

static void pnv_phb3_check_m64(PnvPHB3 *phb, uint32_t index)
{
    uint64_t base, start, size, m64;
    MemoryRegion *parent;
    PnvPBCQState *pbcq = &phb->pbcq;

    if (memory_region_is_mapped(&phb->mr_m64[index])) {
        /* Should we destroy it in RCU friendly way... ? */
        memory_region_del_subregion(phb->mr_m64[index].container,
                                    &phb->mr_m64[index]);
    }

    /* Get table entry */
    m64 = phb->ioda_M64BT[index];

    if (!(m64 & IODA2_M64BT_ENABLE)) {
        return;
    }

    /* Grab geometry from registers */
    base = GETFIELD(IODA2_M64BT_BASE, m64) << 20;
    if (m64 & IODA2_M64BT_SINGLE_PE) {
        base &= ~0x1ffffffull;
    }
    size = GETFIELD(IODA2_M64BT_MASK, m64) << 20;
    size |= 0xfffc000000000000ull;
    size = ~size + 1;
    start = base | (phb->regs[PHB_M64_UPPER_BITS >> 3]);

    /* Check if it matches an enabled MMIO region in the PBCQ */
    if (memory_region_is_mapped(&pbcq->mmbar0) &&
        base >= pbcq->mmio0_base &&
        (base + size) <= (pbcq->mmio0_base + pbcq->mmio0_size)) {
        parent = &pbcq->mmbar0;
        base -= pbcq->mmio0_base;
    } else if (memory_region_is_mapped(&pbcq->mmbar1) &&
               base >= pbcq->mmio1_base &&
               (base + size) <= (pbcq->mmio1_base + pbcq->mmio1_size)) {
        parent = &pbcq->mmbar1;
        base -= pbcq->mmio1_base;
    } else {
        return;
    }

    /* Create alias */
    memory_region_init_alias(&phb->mr_m64[index], OBJECT(phb), "phb3-m64",
                             &phb->pci_mmio, start, size);
    memory_region_add_subregion(parent, base, &phb->mr_m64[index]);
}

static void pnv_phb3_check_all_m64s(PnvPHB3 *phb)
{
    uint64_t i;

    for (i = 0; i < PNV_PHB3_NUM_M64; i++) {
        pnv_phb3_check_m64(phb, i);
    }
}

static void pnv_phb3_lxivt_write(PnvPHB3 *phb, unsigned idx, uint64_t val)
{
    uint8_t server, prio;

    phb->ioda_LXIVT[idx] = val & (IODA2_LXIVT_SERVER |
                                  IODA2_LXIVT_PRIORITY |
                                  IODA2_LXIVT_NODE_ID);
    server = GETFIELD(IODA2_LXIVT_SERVER, val);
    prio = GETFIELD(IODA2_LXIVT_PRIORITY, val);

    /*
     * The low order 2 bits are the link pointer (Type II interrupts).
     * Shift back to get a valid IRQ server.
     */
    server >>= 2;

    ics_write_xive(&phb->lsis, idx, server, prio, prio);
}

static uint64_t *pnv_phb3_ioda_access(PnvPHB3 *phb,
                                      unsigned *out_table, unsigned *out_idx)
{
    uint64_t adreg = phb->regs[PHB_IODA_ADDR >> 3];
    unsigned int index = GETFIELD(PHB_IODA_AD_TADR, adreg);
    unsigned int table = GETFIELD(PHB_IODA_AD_TSEL, adreg);
    unsigned int mask;
    uint64_t *tptr = NULL;

    switch (table) {
    case IODA2_TBL_LIST:
        tptr = phb->ioda_LIST;
        mask = 7;
        break;
    case IODA2_TBL_LXIVT:
        tptr = phb->ioda_LXIVT;
        mask = 7;
        break;
    case IODA2_TBL_IVC_CAM:
    case IODA2_TBL_RBA:
        mask = 31;
        break;
    case IODA2_TBL_RCAM:
        mask = 63;
        break;
    case IODA2_TBL_MRT:
        mask = 7;
        break;
    case IODA2_TBL_PESTA:
    case IODA2_TBL_PESTB:
        mask = 255;
        break;
    case IODA2_TBL_TVT:
        tptr = phb->ioda_TVT;
        mask = 511;
        break;
    case IODA2_TBL_TCAM:
    case IODA2_TBL_TDR:
        mask = 63;
        break;
    case IODA2_TBL_M64BT:
        tptr = phb->ioda_M64BT;
        mask = 15;
        break;
    case IODA2_TBL_M32DT:
        tptr = phb->ioda_MDT;
        mask = 255;
        break;
    case IODA2_TBL_PEEV:
        tptr = phb->ioda_PEEV;
        mask = 3;
        break;
    default:
        phb3_error(phb, "invalid IODA table %d", table);
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

static uint64_t pnv_phb3_ioda_read(PnvPHB3 *phb)
{
        unsigned table;
        uint64_t *tptr;

        tptr = pnv_phb3_ioda_access(phb, &table, NULL);
        if (!tptr) {
            /* Return 0 on unsupported tables, not ff's */
            return 0;
        }
        return *tptr;
}

static void pnv_phb3_ioda_write(PnvPHB3 *phb, uint64_t val)
{
        unsigned table, idx;
        uint64_t *tptr;

        tptr = pnv_phb3_ioda_access(phb, &table, &idx);
        if (!tptr) {
            return;
        }

        /* Handle side effects */
        switch (table) {
        case IODA2_TBL_LXIVT:
            pnv_phb3_lxivt_write(phb, idx, val);
            break;
        case IODA2_TBL_M64BT:
            *tptr = val;
            pnv_phb3_check_m64(phb, idx);
            break;
        default:
            *tptr = val;
        }
}

/*
 * This is called whenever the PHB LSI, MSI source ID register or
 * the PBCQ irq filters are written.
 */
void pnv_phb3_remap_irqs(PnvPHB3 *phb)
{
    ICSState *ics = &phb->lsis;
    uint32_t local, global, count, mask, comp;
    uint64_t baren;
    PnvPBCQState *pbcq = &phb->pbcq;

    /*
     * First check if we are enabled. Unlike real HW we don't separate
     * TX and RX so we enable if both are set
     */
    baren = pbcq->nest_regs[PBCQ_NEST_BAR_EN];
    if (!(baren & PBCQ_NEST_BAR_EN_IRSN_RX) ||
        !(baren & PBCQ_NEST_BAR_EN_IRSN_TX)) {
        ics->offset = 0;
        return;
    }

    /* Grab local LSI source ID */
    local = GETFIELD(PHB_LSI_SRC_ID, phb->regs[PHB_LSI_SOURCE_ID >> 3]) << 3;

    /* Grab global one and compare */
    global = GETFIELD(PBCQ_NEST_LSI_SRC,
                      pbcq->nest_regs[PBCQ_NEST_LSI_SRC_ID]) << 3;
    if (global != local) {
        /*
         * This happens during initialization, let's come back when we
         * are properly configured
         */
        ics->offset = 0;
        return;
    }

    /* Get the base on the powerbus */
    comp = GETFIELD(PBCQ_NEST_IRSN_COMP,
                    pbcq->nest_regs[PBCQ_NEST_IRSN_COMPARE]);
    mask = GETFIELD(PBCQ_NEST_IRSN_COMP,
                    pbcq->nest_regs[PBCQ_NEST_IRSN_MASK]);
    count = ((~mask) + 1) & 0x7ffff;
    phb->total_irq = count;

    /* Sanity checks */
    if ((global + PNV_PHB3_NUM_LSI) > count) {
        phb3_error(phb, "LSIs out of reach: LSI base=%d total irq=%d", global,
                   count);
    }

    if (count > 2048) {
        phb3_error(phb, "More interrupts than supported: %d", count);
    }

    if ((comp & mask) != comp) {
        phb3_error(phb, "IRQ compare bits not in mask: comp=0x%x mask=0x%x",
                   comp, mask);
        comp &= mask;
    }
    /* Setup LSI offset */
    ics->offset = comp + global;

    /* Setup MSI offset */
    pnv_phb3_msi_update_config(&phb->msis, comp, count - PNV_PHB3_NUM_LSI);
}

static void pnv_phb3_lsi_src_id_write(PnvPHB3 *phb, uint64_t val)
{
    /* Sanitize content */
    val &= PHB_LSI_SRC_ID;
    phb->regs[PHB_LSI_SOURCE_ID >> 3] = val;
    pnv_phb3_remap_irqs(phb);
}

static void pnv_phb3_rtc_invalidate(PnvPHB3 *phb, uint64_t val)
{
    PnvPhb3DMASpace *ds;

    /* Always invalidate all for now ... */
    QLIST_FOREACH(ds, &phb->dma_spaces, list) {
        ds->pe_num = PHB_INVALID_PE;
    }
}


static void pnv_phb3_update_msi_regions(PnvPhb3DMASpace *ds)
{
    uint64_t cfg = ds->phb->regs[PHB_PHB3_CONFIG >> 3];

    if (cfg & PHB_PHB3C_32BIT_MSI_EN) {
        if (!memory_region_is_mapped(&ds->msi32_mr)) {
            memory_region_add_subregion(MEMORY_REGION(&ds->dma_mr),
                                        0xffff0000, &ds->msi32_mr);
        }
    } else {
        if (memory_region_is_mapped(&ds->msi32_mr)) {
            memory_region_del_subregion(MEMORY_REGION(&ds->dma_mr),
                                        &ds->msi32_mr);
        }
    }

    if (cfg & PHB_PHB3C_64BIT_MSI_EN) {
        if (!memory_region_is_mapped(&ds->msi64_mr)) {
            memory_region_add_subregion(MEMORY_REGION(&ds->dma_mr),
                                        (1ull << 60), &ds->msi64_mr);
        }
    } else {
        if (memory_region_is_mapped(&ds->msi64_mr)) {
            memory_region_del_subregion(MEMORY_REGION(&ds->dma_mr),
                                        &ds->msi64_mr);
        }
    }
}

static void pnv_phb3_update_all_msi_regions(PnvPHB3 *phb)
{
    PnvPhb3DMASpace *ds;

    QLIST_FOREACH(ds, &phb->dma_spaces, list) {
        pnv_phb3_update_msi_regions(ds);
    }
}

void pnv_phb3_reg_write(void *opaque, hwaddr off, uint64_t val, unsigned size)
{
    PnvPHB3 *phb = opaque;
    bool changed;

    /* Special case configuration data */
    if ((off & 0xfffc) == PHB_CONFIG_DATA) {
        pnv_phb3_config_write(phb, off & 0x3, size, val);
        return;
    }

    /* Other registers are 64-bit only */
    if (size != 8 || off & 0x7) {
        phb3_error(phb, "Invalid register access, offset: 0x%"PRIx64" size: %d",
                   off, size);
        return;
    }

    /* Handle masking & filtering */
    switch (off) {
    case PHB_M64_UPPER_BITS:
        val &= 0xfffc000000000000ull;
        break;
    case PHB_Q_DMA_R:
        /*
         * This is enough logic to make SW happy but we aren't actually
         * quiescing the DMAs
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
    }

    /* Record whether it changed */
    changed = phb->regs[off >> 3] != val;

    /* Store in register cache first */
    phb->regs[off >> 3] = val;

    /* Handle side effects */
    switch (off) {
    case PHB_PHB3_CONFIG:
        if (changed) {
            pnv_phb3_update_all_msi_regions(phb);
        }
        /* fall through */
    case PHB_M32_BASE_ADDR:
    case PHB_M32_BASE_MASK:
    case PHB_M32_START_ADDR:
        if (changed) {
            pnv_phb3_check_m32(phb);
        }
        break;
    case PHB_M64_UPPER_BITS:
        if (changed) {
            pnv_phb3_check_all_m64s(phb);
        }
        break;
    case PHB_LSI_SOURCE_ID:
        if (changed) {
            pnv_phb3_lsi_src_id_write(phb, val);
        }
        break;

    /* IODA table accesses */
    case PHB_IODA_DATA0:
        pnv_phb3_ioda_write(phb, val);
        break;

    /* RTC invalidation */
    case PHB_RTC_INVALIDATE:
        pnv_phb3_rtc_invalidate(phb, val);
        break;

    /* FFI request */
    case PHB_FFI_REQUEST:
        pnv_phb3_msi_ffi(&phb->msis, val);
        break;

    /* Silent simple writes */
    case PHB_CONFIG_ADDRESS:
    case PHB_IODA_ADDR:
    case PHB_TCE_KILL:
    case PHB_TCE_SPEC_CTL:
    case PHB_PEST_BAR:
    case PHB_PELTV_BAR:
    case PHB_RTT_BAR:
    case PHB_RBA_BAR:
    case PHB_IVT_BAR:
    case PHB_FFI_LOCK:
    case PHB_LEM_FIR_ACCUM:
    case PHB_LEM_ERROR_MASK:
    case PHB_LEM_ACTION0:
    case PHB_LEM_ACTION1:
        break;

    /* Noise on anything else */
    default:
        qemu_log_mask(LOG_UNIMP, "phb3: reg_write 0x%"PRIx64"=%"PRIx64"\n",
                      off, val);
    }
}

uint64_t pnv_phb3_reg_read(void *opaque, hwaddr off, unsigned size)
{
    PnvPHB3 *phb = opaque;
    PCIHostState *pci = PCI_HOST_BRIDGE(phb->phb_base);
    uint64_t val;

    if ((off & 0xfffc) == PHB_CONFIG_DATA) {
        return pnv_phb3_config_read(phb, off & 0x3, size);
    }

    /* Other registers are 64-bit only */
    if (size != 8 || off & 0x7) {
        phb3_error(phb, "Invalid register access, offset: 0x%"PRIx64" size: %d",
                   off, size);
        return ~0ull;
    }

    /* Default read from cache */
    val = phb->regs[off >> 3];

    switch (off) {
    /* Simulate venice DD2.0 */
    case PHB_VERSION:
        return 0x000000a300000005ull;
    case PHB_PCIE_SYSTEM_CONFIG:
        return 0x441100fc30000000;

    /* IODA table accesses */
    case PHB_IODA_DATA0:
        return pnv_phb3_ioda_read(phb);

    /* Link training always appears trained */
    case PHB_PCIE_DLP_TRAIN_CTL:
        if (!pci_find_device(pci->bus, 1, 0)) {
            return 0;
        }
        return PHB_PCIE_DLP_INBAND_PRESENCE | PHB_PCIE_DLP_TC_DL_LINKACT;

    /* FFI Lock */
    case PHB_FFI_LOCK:
        /* Set lock and return previous value */
        phb->regs[off >> 3] |= PHB_FFI_LOCK_STATE;
        return val;

    /* DMA read sync: make it look like it's complete */
    case PHB_DMARD_SYNC:
        return PHB_DMARD_SYNC_COMPLETE;

    /* Silent simple reads */
    case PHB_PHB3_CONFIG:
    case PHB_M32_BASE_ADDR:
    case PHB_M32_BASE_MASK:
    case PHB_M32_START_ADDR:
    case PHB_CONFIG_ADDRESS:
    case PHB_IODA_ADDR:
    case PHB_RTC_INVALIDATE:
    case PHB_TCE_KILL:
    case PHB_TCE_SPEC_CTL:
    case PHB_PEST_BAR:
    case PHB_PELTV_BAR:
    case PHB_RTT_BAR:
    case PHB_RBA_BAR:
    case PHB_IVT_BAR:
    case PHB_M64_UPPER_BITS:
    case PHB_LEM_FIR_ACCUM:
    case PHB_LEM_ERROR_MASK:
    case PHB_LEM_ACTION0:
    case PHB_LEM_ACTION1:
        break;

    /* Noise on anything else */
    default:
        qemu_log_mask(LOG_UNIMP, "phb3: reg_read 0x%"PRIx64"=%"PRIx64"\n",
                      off, val);
    }
    return val;
}

static const MemoryRegionOps pnv_phb3_reg_ops = {
    .read = pnv_phb3_reg_read,
    .write = pnv_phb3_reg_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
    .endianness = DEVICE_BIG_ENDIAN,
};

static int pnv_phb3_map_irq(PCIDevice *pci_dev, int irq_num)
{
    /* Check that out properly ... */
    return irq_num & 3;
}

static void pnv_phb3_set_irq(void *opaque, int irq_num, int level)
{
    PnvPHB3 *phb = opaque;

    /* LSI only ... */
    if (irq_num > 3) {
        phb3_error(phb, "Unknown IRQ to set %d", irq_num);
    }
    qemu_set_irq(phb->qirqs[irq_num], level);
}

static bool pnv_phb3_resolve_pe(PnvPhb3DMASpace *ds)
{
    uint64_t rtt, addr;
    uint16_t rte;
    int bus_num;

    /* Already resolved ? */
    if (ds->pe_num != PHB_INVALID_PE) {
        return true;
    }

    /* We need to lookup the RTT */
    rtt = ds->phb->regs[PHB_RTT_BAR >> 3];
    if (!(rtt & PHB_RTT_BAR_ENABLE)) {
        phb3_error(ds->phb, "DMA with RTT BAR disabled !");
        /* Set error bits ? fence ? ... */
        return false;
    }

    /* Read RTE */
    bus_num = pci_bus_num(ds->bus);
    addr = rtt & PHB_RTT_BASE_ADDRESS_MASK;
    addr += 2 * ((bus_num << 8) | ds->devfn);
    if (dma_memory_read(&address_space_memory, addr, &rte,
                        sizeof(rte), MEMTXATTRS_UNSPECIFIED)) {
        phb3_error(ds->phb, "Failed to read RTT entry at 0x%"PRIx64, addr);
        /* Set error bits ? fence ? ... */
        return false;
    }
    rte = be16_to_cpu(rte);

    /* Fail upon reading of invalid PE# */
    if (rte >= PNV_PHB3_NUM_PE) {
        phb3_error(ds->phb, "RTE for RID 0x%x invalid (%04x", ds->devfn, rte);
        /* Set error bits ? fence ? ... */
        return false;
    }
    ds->pe_num = rte;
    return true;
}

static void pnv_phb3_translate_tve(PnvPhb3DMASpace *ds, hwaddr addr,
                                   bool is_write, uint64_t tve,
                                   IOMMUTLBEntry *tlb)
{
    uint64_t tta = GETFIELD(IODA2_TVT_TABLE_ADDR, tve);
    int32_t  lev = GETFIELD(IODA2_TVT_NUM_LEVELS, tve);
    uint32_t tts = GETFIELD(IODA2_TVT_TCE_TABLE_SIZE, tve);
    uint32_t tps = GETFIELD(IODA2_TVT_IO_PSIZE, tve);
    PnvPHB3 *phb = ds->phb;

    /* Invalid levels */
    if (lev > 4) {
        phb3_error(phb, "Invalid #levels in TVE %d", lev);
        return;
    }

    /* IO Page Size of 0 means untranslated, else use TCEs */
    if (tps == 0) {
        /*
         * We only support non-translate in top window.
         *
         * TODO: Venice/Murano support it on bottom window above 4G and
         * Naples suports it on everything
         */
        if (!(tve & PPC_BIT(51))) {
            phb3_error(phb, "xlate for invalid non-translate TVE");
            return;
        }
        /* TODO: Handle boundaries */

        /* Use 4k pages like q35 ... for now */
        tlb->iova = addr & 0xfffffffffffff000ull;
        tlb->translated_addr = addr & 0x0003fffffffff000ull;
        tlb->addr_mask = 0xfffull;
        tlb->perm = IOMMU_RW;
    } else {
        uint32_t tce_shift, tbl_shift, sh;
        uint64_t base, taddr, tce, tce_mask;

        /* TVE disabled ? */
        if (tts == 0) {
            phb3_error(phb, "xlate for invalid translated TVE");
            return;
        }

        /* Address bits per bottom level TCE entry */
        tce_shift = tps + 11;

        /* Address bits per table level */
        tbl_shift = tts + 8;

        /* Top level table base address */
        base = tta << 12;

        /* Total shift to first level */
        sh = tbl_shift * lev + tce_shift;

        /* TODO: Multi-level untested */
        do {
            lev--;

            /* Grab the TCE address */
            taddr = base | (((addr >> sh) & ((1ul << tbl_shift) - 1)) << 3);
            if (dma_memory_read(&address_space_memory, taddr, &tce,
                                sizeof(tce), MEMTXATTRS_UNSPECIFIED)) {
                phb3_error(phb, "Failed to read TCE at 0x%"PRIx64, taddr);
                return;
            }
            tce = be64_to_cpu(tce);

            /* Check permission for indirect TCE */
            if ((lev >= 0) && !(tce & 3)) {
                phb3_error(phb, "Invalid indirect TCE at 0x%"PRIx64, taddr);
                phb3_error(phb, " xlate %"PRIx64":%c TVE=%"PRIx64, addr,
                           is_write ? 'W' : 'R', tve);
                phb3_error(phb, " tta=%"PRIx64" lev=%d tts=%d tps=%d",
                           tta, lev, tts, tps);
                return;
            }
            sh -= tbl_shift;
            base = tce & ~0xfffull;
        } while (lev >= 0);

        /* We exit the loop with TCE being the final TCE */
        if ((is_write & !(tce & 2)) || ((!is_write) && !(tce & 1))) {
            phb3_error(phb, "TCE access fault at 0x%"PRIx64, taddr);
            phb3_error(phb, " xlate %"PRIx64":%c TVE=%"PRIx64, addr,
                       is_write ? 'W' : 'R', tve);
            phb3_error(phb, " tta=%"PRIx64" lev=%d tts=%d tps=%d",
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

static IOMMUTLBEntry pnv_phb3_translate_iommu(IOMMUMemoryRegion *iommu,
                                              hwaddr addr,
                                              IOMMUAccessFlags flag,
                                              int iommu_idx)
{
    PnvPhb3DMASpace *ds = container_of(iommu, PnvPhb3DMASpace, dma_mr);
    int tve_sel;
    uint64_t tve, cfg;
    IOMMUTLBEntry ret = {
        .target_as = &address_space_memory,
        .iova = addr,
        .translated_addr = 0,
        .addr_mask = ~(hwaddr)0,
        .perm = IOMMU_NONE,
    };
    PnvPHB3 *phb = ds->phb;

    /* Resolve PE# */
    if (!pnv_phb3_resolve_pe(ds)) {
        phb3_error(phb, "Failed to resolve PE# for bus @%p (%d) devfn 0x%x",
                   ds->bus, pci_bus_num(ds->bus), ds->devfn);
        return ret;
    }

    /* Check top bits */
    switch (addr >> 60) {
    case 00:
        /* DMA or 32-bit MSI ? */
        cfg = ds->phb->regs[PHB_PHB3_CONFIG >> 3];
        if ((cfg & PHB_PHB3C_32BIT_MSI_EN) &&
            ((addr & 0xffffffffffff0000ull) == 0xffff0000ull)) {
            phb3_error(phb, "xlate on 32-bit MSI region");
            return ret;
        }
        /* Choose TVE XXX Use PHB3 Control Register */
        tve_sel = (addr >> 59) & 1;
        tve = ds->phb->ioda_TVT[ds->pe_num * 2 + tve_sel];
        pnv_phb3_translate_tve(ds, addr, flag & IOMMU_WO, tve, &ret);
        break;
    case 01:
        phb3_error(phb, "xlate on 64-bit MSI region");
        break;
    default:
        phb3_error(phb, "xlate on unsupported address 0x%"PRIx64, addr);
    }
    return ret;
}

#define TYPE_PNV_PHB3_IOMMU_MEMORY_REGION "pnv-phb3-iommu-memory-region"
DECLARE_INSTANCE_CHECKER(IOMMUMemoryRegion, PNV_PHB3_IOMMU_MEMORY_REGION,
                         TYPE_PNV_PHB3_IOMMU_MEMORY_REGION)

static void pnv_phb3_iommu_memory_region_class_init(ObjectClass *klass,
                                                    void *data)
{
    IOMMUMemoryRegionClass *imrc = IOMMU_MEMORY_REGION_CLASS(klass);

    imrc->translate = pnv_phb3_translate_iommu;
}

static const TypeInfo pnv_phb3_iommu_memory_region_info = {
    .parent = TYPE_IOMMU_MEMORY_REGION,
    .name = TYPE_PNV_PHB3_IOMMU_MEMORY_REGION,
    .class_init = pnv_phb3_iommu_memory_region_class_init,
};

/*
 * MSI/MSIX memory region implementation.
 * The handler handles both MSI and MSIX.
 */
static void pnv_phb3_msi_write(void *opaque, hwaddr addr,
                               uint64_t data, unsigned size)
{
    PnvPhb3DMASpace *ds = opaque;

    /* Resolve PE# */
    if (!pnv_phb3_resolve_pe(ds)) {
        phb3_error(ds->phb, "Failed to resolve PE# for bus @%p (%d) devfn 0x%x",
                   ds->bus, pci_bus_num(ds->bus), ds->devfn);
        return;
    }

    pnv_phb3_msi_send(&ds->phb->msis, addr, data, ds->pe_num);
}

/* There is no .read as the read result is undefined by PCI spec */
static uint64_t pnv_phb3_msi_read(void *opaque, hwaddr addr, unsigned size)
{
    PnvPhb3DMASpace *ds = opaque;

    phb3_error(ds->phb, "invalid read @ 0x%" HWADDR_PRIx, addr);
    return -1;
}

static const MemoryRegionOps pnv_phb3_msi_ops = {
    .read = pnv_phb3_msi_read,
    .write = pnv_phb3_msi_write,
    .endianness = DEVICE_LITTLE_ENDIAN
};

static AddressSpace *pnv_phb3_dma_iommu(PCIBus *bus, void *opaque, int devfn)
{
    PnvPHB3 *phb = opaque;
    PnvPhb3DMASpace *ds;

    QLIST_FOREACH(ds, &phb->dma_spaces, list) {
        if (ds->bus == bus && ds->devfn == devfn) {
            break;
        }
    }

    if (ds == NULL) {
        ds = g_new0(PnvPhb3DMASpace, 1);
        ds->bus = bus;
        ds->devfn = devfn;
        ds->pe_num = PHB_INVALID_PE;
        ds->phb = phb;
        memory_region_init_iommu(&ds->dma_mr, sizeof(ds->dma_mr),
                                 TYPE_PNV_PHB3_IOMMU_MEMORY_REGION,
                                 OBJECT(phb), "phb3_iommu", UINT64_MAX);
        address_space_init(&ds->dma_as, MEMORY_REGION(&ds->dma_mr),
                           "phb3_iommu");
        memory_region_init_io(&ds->msi32_mr, OBJECT(phb), &pnv_phb3_msi_ops,
                              ds, "msi32", 0x10000);
        memory_region_init_io(&ds->msi64_mr, OBJECT(phb), &pnv_phb3_msi_ops,
                              ds, "msi64", 0x100000);
        pnv_phb3_update_msi_regions(ds);

        QLIST_INSERT_HEAD(&phb->dma_spaces, ds, list);
    }
    return &ds->dma_as;
}

static void pnv_phb3_instance_init(Object *obj)
{
    PnvPHB3 *phb = PNV_PHB3(obj);

    QLIST_INIT(&phb->dma_spaces);

    /* LSI sources */
    object_initialize_child(obj, "lsi", &phb->lsis, TYPE_ICS);

    /* Default init ... will be fixed by HW inits */
    phb->lsis.offset = 0;

    /* MSI sources */
    object_initialize_child(obj, "msi", &phb->msis, TYPE_PHB3_MSI);

    /* Power Bus Common Queue */
    object_initialize_child(obj, "pbcq", &phb->pbcq, TYPE_PNV_PBCQ);

}

void pnv_phb3_bus_init(DeviceState *dev, PnvPHB3 *phb)
{
    PCIHostState *pci = PCI_HOST_BRIDGE(dev);

    /*
     * PHB3 doesn't support IO space. However, qemu gets very upset if
     * we don't have an IO region to anchor IO BARs onto so we just
     * initialize one which we never hook up to anything
     */
    memory_region_init(&phb->pci_io, OBJECT(phb), "pci-io", 0x10000);
    memory_region_init(&phb->pci_mmio, OBJECT(phb), "pci-mmio",
                       PCI_MMIO_TOTAL_SIZE);

    pci->bus = pci_register_root_bus(dev,
                                     dev->id ? dev->id : NULL,
                                     pnv_phb3_set_irq, pnv_phb3_map_irq, phb,
                                     &phb->pci_mmio, &phb->pci_io,
                                     0, 4, TYPE_PNV_PHB3_ROOT_BUS);

    object_property_set_int(OBJECT(pci->bus), "phb-id", phb->phb_id,
                            &error_abort);
    object_property_set_int(OBJECT(pci->bus), "chip-id", phb->chip_id,
                            &error_abort);

    pci_setup_iommu(pci->bus, pnv_phb3_dma_iommu, phb);
}

static void pnv_phb3_realize(DeviceState *dev, Error **errp)
{
    PnvPHB3 *phb = PNV_PHB3(dev);
    PnvMachineState *pnv = PNV_MACHINE(qdev_get_machine());
    int i;

    if (phb->phb_id >= PNV_CHIP_GET_CLASS(phb->chip)->num_phbs) {
        error_setg(errp, "invalid PHB index: %d", phb->phb_id);
        return;
    }

    /* LSI sources */
    object_property_set_link(OBJECT(&phb->lsis), "xics", OBJECT(pnv),
                             &error_abort);
    object_property_set_int(OBJECT(&phb->lsis), "nr-irqs", PNV_PHB3_NUM_LSI,
                            &error_abort);
    if (!qdev_realize(DEVICE(&phb->lsis), NULL, errp)) {
        return;
    }

    for (i = 0; i < phb->lsis.nr_irqs; i++) {
        ics_set_irq_type(&phb->lsis, i, true);
    }

    phb->qirqs = qemu_allocate_irqs(ics_set_irq, &phb->lsis, phb->lsis.nr_irqs);

    /* MSI sources */
    object_property_set_link(OBJECT(&phb->msis), "phb", OBJECT(phb),
                             &error_abort);
    object_property_set_link(OBJECT(&phb->msis), "xics", OBJECT(pnv),
                             &error_abort);
    object_property_set_int(OBJECT(&phb->msis), "nr-irqs", PHB3_MAX_MSI,
                            &error_abort);
    if (!qdev_realize(DEVICE(&phb->msis), NULL, errp)) {
        return;
    }

    /* Power Bus Common Queue */
    object_property_set_link(OBJECT(&phb->pbcq), "phb", OBJECT(phb),
                             &error_abort);
    if (!qdev_realize(DEVICE(&phb->pbcq), NULL, errp)) {
        return;
    }

    /* Controller Registers */
    memory_region_init_io(&phb->mr_regs, OBJECT(phb), &pnv_phb3_reg_ops, phb,
                          "phb3-regs", 0x1000);
}

void pnv_phb3_update_regions(PnvPHB3 *phb)
{
    PnvPBCQState *pbcq = &phb->pbcq;

    /* Unmap first always */
    if (memory_region_is_mapped(&phb->mr_regs)) {
        memory_region_del_subregion(&pbcq->phbbar, &phb->mr_regs);
    }

    /* Map registers if enabled */
    if (memory_region_is_mapped(&pbcq->phbbar)) {
        /* TODO: We should use the PHB BAR 2 register but we don't ... */
        memory_region_add_subregion(&pbcq->phbbar, 0, &phb->mr_regs);
    }

    /* Check/update m32 */
    if (memory_region_is_mapped(&phb->mr_m32)) {
        pnv_phb3_check_m32(phb);
    }
    pnv_phb3_check_all_m64s(phb);
}

static Property pnv_phb3_properties[] = {
    DEFINE_PROP_UINT32("index", PnvPHB3, phb_id, 0),
    DEFINE_PROP_UINT32("chip-id", PnvPHB3, chip_id, 0),
    DEFINE_PROP_LINK("chip", PnvPHB3, chip, TYPE_PNV_CHIP, PnvChip *),
    DEFINE_PROP_LINK("phb-base", PnvPHB3, phb_base, TYPE_PNV_PHB, PnvPHB *),
    DEFINE_PROP_END_OF_LIST(),
};

static void pnv_phb3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = pnv_phb3_realize;
    device_class_set_props(dc, pnv_phb3_properties);
    dc->user_creatable = false;
}

static const TypeInfo pnv_phb3_type_info = {
    .name          = TYPE_PNV_PHB3,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(PnvPHB3),
    .class_init    = pnv_phb3_class_init,
    .instance_init = pnv_phb3_instance_init,
};

static void pnv_phb3_root_bus_get_prop(Object *obj, Visitor *v,
                                       const char *name,
                                       void *opaque, Error **errp)
{
    PnvPHB3RootBus *bus = PNV_PHB3_ROOT_BUS(obj);
    uint64_t value = 0;

    if (strcmp(name, "phb-id") == 0) {
        value = bus->phb_id;
    } else {
        value = bus->chip_id;
    }

    visit_type_size(v, name, &value, errp);
}

static void pnv_phb3_root_bus_set_prop(Object *obj, Visitor *v,
                                       const char *name,
                                       void *opaque, Error **errp)

{
    PnvPHB3RootBus *bus = PNV_PHB3_ROOT_BUS(obj);
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

static void pnv_phb3_root_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    object_class_property_add(klass, "phb-id", "int",
                              pnv_phb3_root_bus_get_prop,
                              pnv_phb3_root_bus_set_prop,
                              NULL, NULL);

    object_class_property_add(klass, "chip-id", "int",
                              pnv_phb3_root_bus_get_prop,
                              pnv_phb3_root_bus_set_prop,
                              NULL, NULL);

    /*
     * PHB3 has only a single root complex. Enforce the limit on the
     * parent bus
     */
    k->max_dev = 1;
}

static const TypeInfo pnv_phb3_root_bus_info = {
    .name = TYPE_PNV_PHB3_ROOT_BUS,
    .parent = TYPE_PCIE_BUS,
    .instance_size = sizeof(PnvPHB3RootBus),
    .class_init = pnv_phb3_root_bus_class_init,
};

static void pnv_phb3_register_types(void)
{
    type_register_static(&pnv_phb3_root_bus_info);
    type_register_static(&pnv_phb3_type_info);
    type_register_static(&pnv_phb3_iommu_memory_region_info);
}

type_init(pnv_phb3_register_types)
