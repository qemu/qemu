/*
 * ARM GICv3 emulation: Redistributor
 *
 * Copyright (c) 2015 Huawei.
 * Copyright (c) 2016 Linaro Limited.
 * Written by Shlomo Pongratz, Peter Maydell
 *
 * This code is licensed under the GPL, version 2 or (at your option)
 * any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "gicv3_internal.h"

static uint32_t mask_group(GICv3CPUState *cs, MemTxAttrs attrs)
{
    /* Return a 32-bit mask which should be applied for this set of 32
     * interrupts; each bit is 1 if access is permitted by the
     * combination of attrs.secure and GICR_GROUPR. (GICR_NSACR does
     * not affect config register accesses, unlike GICD_NSACR.)
     */
    if (!attrs.secure && !(cs->gic->gicd_ctlr & GICD_CTLR_DS)) {
        /* bits for Group 0 or Secure Group 1 interrupts are RAZ/WI */
        return cs->gicr_igroupr0;
    }
    return 0xFFFFFFFFU;
}

static int gicr_ns_access(GICv3CPUState *cs, int irq)
{
    /* Return the 2 bit NSACR.NS_access field for this SGI */
    assert(irq < 16);
    return extract32(cs->gicr_nsacr, irq * 2, 2);
}

static void gicr_write_set_bitmap_reg(GICv3CPUState *cs, MemTxAttrs attrs,
                                      uint32_t *reg, uint32_t val)
{
    /* Helper routine to implement writing to a "set-bitmap" register */
    val &= mask_group(cs, attrs);
    *reg |= val;
    gicv3_redist_update(cs);
}

static void gicr_write_clear_bitmap_reg(GICv3CPUState *cs, MemTxAttrs attrs,
                                        uint32_t *reg, uint32_t val)
{
    /* Helper routine to implement writing to a "clear-bitmap" register */
    val &= mask_group(cs, attrs);
    *reg &= ~val;
    gicv3_redist_update(cs);
}

static uint32_t gicr_read_bitmap_reg(GICv3CPUState *cs, MemTxAttrs attrs,
                                     uint32_t reg)
{
    reg &= mask_group(cs, attrs);
    return reg;
}

static uint8_t gicr_read_ipriorityr(GICv3CPUState *cs, MemTxAttrs attrs,
                                    int irq)
{
    /* Read the value of GICR_IPRIORITYR<n> for the specified interrupt,
     * honouring security state (these are RAZ/WI for Group 0 or Secure
     * Group 1 interrupts).
     */
    uint32_t prio;

    prio = cs->gicr_ipriorityr[irq];

    if (!attrs.secure && !(cs->gic->gicd_ctlr & GICD_CTLR_DS)) {
        if (!(cs->gicr_igroupr0 & (1U << irq))) {
            /* Fields for Group 0 or Secure Group 1 interrupts are RAZ/WI */
            return 0;
        }
        /* NS view of the interrupt priority */
        prio = (prio << 1) & 0xff;
    }
    return prio;
}

static void gicr_write_ipriorityr(GICv3CPUState *cs, MemTxAttrs attrs, int irq,
                                  uint8_t value)
{
    /* Write the value of GICD_IPRIORITYR<n> for the specified interrupt,
     * honouring security state (these are RAZ/WI for Group 0 or Secure
     * Group 1 interrupts).
     */
    if (!attrs.secure && !(cs->gic->gicd_ctlr & GICD_CTLR_DS)) {
        if (!(cs->gicr_igroupr0 & (1U << irq))) {
            /* Fields for Group 0 or Secure Group 1 interrupts are RAZ/WI */
            return;
        }
        /* NS view of the interrupt priority */
        value = 0x80 | (value >> 1);
    }
    cs->gicr_ipriorityr[irq] = value;
}

static MemTxResult gicr_readb(GICv3CPUState *cs, hwaddr offset,
                              uint64_t *data, MemTxAttrs attrs)
{
    switch (offset) {
    case GICR_IPRIORITYR ... GICR_IPRIORITYR + 0x1f:
        *data = gicr_read_ipriorityr(cs, attrs, offset - GICR_IPRIORITYR);
        return MEMTX_OK;
    default:
        return MEMTX_ERROR;
    }
}

static MemTxResult gicr_writeb(GICv3CPUState *cs, hwaddr offset,
                               uint64_t value, MemTxAttrs attrs)
{
    switch (offset) {
    case GICR_IPRIORITYR ... GICR_IPRIORITYR + 0x1f:
        gicr_write_ipriorityr(cs, attrs, offset - GICR_IPRIORITYR, value);
        gicv3_redist_update(cs);
        return MEMTX_OK;
    default:
        return MEMTX_ERROR;
    }
}

static MemTxResult gicr_readl(GICv3CPUState *cs, hwaddr offset,
                              uint64_t *data, MemTxAttrs attrs)
{
    switch (offset) {
    case GICR_CTLR:
        *data = cs->gicr_ctlr;
        return MEMTX_OK;
    case GICR_IIDR:
        *data = gicv3_iidr();
        return MEMTX_OK;
    case GICR_TYPER:
        *data = extract64(cs->gicr_typer, 0, 32);
        return MEMTX_OK;
    case GICR_TYPER + 4:
        *data = extract64(cs->gicr_typer, 32, 32);
        return MEMTX_OK;
    case GICR_STATUSR:
        /* RAZ/WI for us (this is an optional register and our implementation
         * does not track RO/WO/reserved violations to report them to the guest)
         */
        *data = 0;
        return MEMTX_OK;
    case GICR_WAKER:
        *data = cs->gicr_waker;
        return MEMTX_OK;
    case GICR_PROPBASER:
        *data = extract64(cs->gicr_propbaser, 0, 32);
        return MEMTX_OK;
    case GICR_PROPBASER + 4:
        *data = extract64(cs->gicr_propbaser, 32, 32);
        return MEMTX_OK;
    case GICR_PENDBASER:
        *data = extract64(cs->gicr_pendbaser, 0, 32);
        return MEMTX_OK;
    case GICR_PENDBASER + 4:
        *data = extract64(cs->gicr_pendbaser, 32, 32);
        return MEMTX_OK;
    case GICR_IGROUPR0:
        if (!attrs.secure && !(cs->gic->gicd_ctlr & GICD_CTLR_DS)) {
            *data = 0;
            return MEMTX_OK;
        }
        *data = cs->gicr_igroupr0;
        return MEMTX_OK;
    case GICR_ISENABLER0:
    case GICR_ICENABLER0:
        *data = gicr_read_bitmap_reg(cs, attrs, cs->gicr_ienabler0);
        return MEMTX_OK;
    case GICR_ISPENDR0:
    case GICR_ICPENDR0:
    {
        /* The pending register reads as the logical OR of the pending
         * latch and the input line level for level-triggered interrupts.
         */
        uint32_t val = cs->gicr_ipendr0 | (~cs->edge_trigger & cs->level);
        *data = gicr_read_bitmap_reg(cs, attrs, val);
        return MEMTX_OK;
    }
    case GICR_ISACTIVER0:
    case GICR_ICACTIVER0:
        *data = gicr_read_bitmap_reg(cs, attrs, cs->gicr_iactiver0);
        return MEMTX_OK;
    case GICR_IPRIORITYR ... GICR_IPRIORITYR + 0x1f:
    {
        int i, irq = offset - GICR_IPRIORITYR;
        uint32_t value = 0;

        for (i = irq + 3; i >= irq; i--) {
            value <<= 8;
            value |= gicr_read_ipriorityr(cs, attrs, i);
        }
        *data = value;
        return MEMTX_OK;
    }
    case GICR_ICFGR0:
    case GICR_ICFGR1:
    {
        /* Our edge_trigger bitmap is one bit per irq; take the correct
         * half of it, and spread it out into the odd bits.
         */
        uint32_t value;

        value = cs->edge_trigger & mask_group(cs, attrs);
        value = extract32(value, (offset == GICR_ICFGR1) ? 16 : 0, 16);
        value = half_shuffle32(value) << 1;
        *data = value;
        return MEMTX_OK;
    }
    case GICR_IGRPMODR0:
        if ((cs->gic->gicd_ctlr & GICD_CTLR_DS) || !attrs.secure) {
            /* RAZ/WI if security disabled, or if
             * security enabled and this is an NS access
             */
            *data = 0;
            return MEMTX_OK;
        }
        *data = cs->gicr_igrpmodr0;
        return MEMTX_OK;
    case GICR_NSACR:
        if ((cs->gic->gicd_ctlr & GICD_CTLR_DS) || !attrs.secure) {
            /* RAZ/WI if security disabled, or if
             * security enabled and this is an NS access
             */
            *data = 0;
            return MEMTX_OK;
        }
        *data = cs->gicr_nsacr;
        return MEMTX_OK;
    case GICR_IDREGS ... GICR_IDREGS + 0x2f:
        *data = gicv3_idreg(offset - GICR_IDREGS);
        return MEMTX_OK;
    default:
        return MEMTX_ERROR;
    }
}

static MemTxResult gicr_writel(GICv3CPUState *cs, hwaddr offset,
                               uint64_t value, MemTxAttrs attrs)
{
    switch (offset) {
    case GICR_CTLR:
        /* For our implementation, GICR_TYPER.DPGS is 0 and so all
         * the DPG bits are RAZ/WI. We don't do anything asynchronously,
         * so UWP and RWP are RAZ/WI. GICR_TYPER.LPIS is 1 (we
         * implement LPIs) so Enable_LPIs is programmable.
         */
        if (cs->gicr_typer & GICR_TYPER_PLPIS) {
            if (value & GICR_CTLR_ENABLE_LPIS) {
                cs->gicr_ctlr |= GICR_CTLR_ENABLE_LPIS;
                /* Check for any pending interr in pending table */
                gicv3_redist_update_lpi(cs);
            } else {
                cs->gicr_ctlr &= ~GICR_CTLR_ENABLE_LPIS;
                /* cs->hppi might have been an LPI; recalculate */
                gicv3_redist_update(cs);
            }
        }
        return MEMTX_OK;
    case GICR_STATUSR:
        /* RAZ/WI for our implementation */
        return MEMTX_OK;
    case GICR_WAKER:
        /* Only the ProcessorSleep bit is writeable. When the guest sets
         * it it requests that we transition the channel between the
         * redistributor and the cpu interface to quiescent, and that
         * we set the ChildrenAsleep bit once the inteface has reached the
         * quiescent state.
         * Setting the ProcessorSleep to 0 reverses the quiescing, and
         * ChildrenAsleep is cleared once the transition is complete.
         * Since our interface is not asynchronous, we complete these
         * transitions instantaneously, so we set ChildrenAsleep to the
         * same value as ProcessorSleep here.
         */
        value &= GICR_WAKER_ProcessorSleep;
        if (value & GICR_WAKER_ProcessorSleep) {
            value |= GICR_WAKER_ChildrenAsleep;
        }
        cs->gicr_waker = value;
        return MEMTX_OK;
    case GICR_PROPBASER:
        cs->gicr_propbaser = deposit64(cs->gicr_propbaser, 0, 32, value);
        return MEMTX_OK;
    case GICR_PROPBASER + 4:
        cs->gicr_propbaser = deposit64(cs->gicr_propbaser, 32, 32, value);
        return MEMTX_OK;
    case GICR_PENDBASER:
        cs->gicr_pendbaser = deposit64(cs->gicr_pendbaser, 0, 32, value);
        return MEMTX_OK;
    case GICR_PENDBASER + 4:
        cs->gicr_pendbaser = deposit64(cs->gicr_pendbaser, 32, 32, value);
        return MEMTX_OK;
    case GICR_IGROUPR0:
        if (!attrs.secure && !(cs->gic->gicd_ctlr & GICD_CTLR_DS)) {
            return MEMTX_OK;
        }
        cs->gicr_igroupr0 = value;
        gicv3_redist_update(cs);
        return MEMTX_OK;
    case GICR_ISENABLER0:
        gicr_write_set_bitmap_reg(cs, attrs, &cs->gicr_ienabler0, value);
        return MEMTX_OK;
    case GICR_ICENABLER0:
        gicr_write_clear_bitmap_reg(cs, attrs, &cs->gicr_ienabler0, value);
        return MEMTX_OK;
    case GICR_ISPENDR0:
        gicr_write_set_bitmap_reg(cs, attrs, &cs->gicr_ipendr0, value);
        return MEMTX_OK;
    case GICR_ICPENDR0:
        gicr_write_clear_bitmap_reg(cs, attrs, &cs->gicr_ipendr0, value);
        return MEMTX_OK;
    case GICR_ISACTIVER0:
        gicr_write_set_bitmap_reg(cs, attrs, &cs->gicr_iactiver0, value);
        return MEMTX_OK;
    case GICR_ICACTIVER0:
        gicr_write_clear_bitmap_reg(cs, attrs, &cs->gicr_iactiver0, value);
        return MEMTX_OK;
    case GICR_IPRIORITYR ... GICR_IPRIORITYR + 0x1f:
    {
        int i, irq = offset - GICR_IPRIORITYR;

        for (i = irq; i < irq + 4; i++, value >>= 8) {
            gicr_write_ipriorityr(cs, attrs, i, value);
        }
        gicv3_redist_update(cs);
        return MEMTX_OK;
    }
    case GICR_ICFGR0:
        /* Register is all RAZ/WI or RAO/WI bits */
        return MEMTX_OK;
    case GICR_ICFGR1:
    {
        uint32_t mask;

        /* Since our edge_trigger bitmap is one bit per irq, our input
         * 32-bits will compress down into 16 bits which we need
         * to write into the bitmap.
         */
        value = half_unshuffle32(value >> 1) << 16;
        mask = mask_group(cs, attrs) & 0xffff0000U;

        cs->edge_trigger &= ~mask;
        cs->edge_trigger |= (value & mask);

        gicv3_redist_update(cs);
        return MEMTX_OK;
    }
    case GICR_IGRPMODR0:
        if ((cs->gic->gicd_ctlr & GICD_CTLR_DS) || !attrs.secure) {
            /* RAZ/WI if security disabled, or if
             * security enabled and this is an NS access
             */
            return MEMTX_OK;
        }
        cs->gicr_igrpmodr0 = value;
        gicv3_redist_update(cs);
        return MEMTX_OK;
    case GICR_NSACR:
        if ((cs->gic->gicd_ctlr & GICD_CTLR_DS) || !attrs.secure) {
            /* RAZ/WI if security disabled, or if
             * security enabled and this is an NS access
             */
            return MEMTX_OK;
        }
        cs->gicr_nsacr = value;
        /* no update required as this only affects access permission checks */
        return MEMTX_OK;
    case GICR_IIDR:
    case GICR_TYPER:
    case GICR_IDREGS ... GICR_IDREGS + 0x2f:
        /* RO registers, ignore the write */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest write to RO register at offset "
                      TARGET_FMT_plx "\n", __func__, offset);
        return MEMTX_OK;
    default:
        return MEMTX_ERROR;
    }
}

static MemTxResult gicr_readll(GICv3CPUState *cs, hwaddr offset,
                               uint64_t *data, MemTxAttrs attrs)
{
    switch (offset) {
    case GICR_TYPER:
        *data = cs->gicr_typer;
        return MEMTX_OK;
    case GICR_PROPBASER:
        *data = cs->gicr_propbaser;
        return MEMTX_OK;
    case GICR_PENDBASER:
        *data = cs->gicr_pendbaser;
        return MEMTX_OK;
    default:
        return MEMTX_ERROR;
    }
}

static MemTxResult gicr_writell(GICv3CPUState *cs, hwaddr offset,
                                uint64_t value, MemTxAttrs attrs)
{
    switch (offset) {
    case GICR_PROPBASER:
        cs->gicr_propbaser = value;
        return MEMTX_OK;
    case GICR_PENDBASER:
        cs->gicr_pendbaser = value;
        return MEMTX_OK;
    case GICR_TYPER:
        /* RO register, ignore the write */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest write to RO register at offset "
                      TARGET_FMT_plx "\n", __func__, offset);
        return MEMTX_OK;
    default:
        return MEMTX_ERROR;
    }
}

MemTxResult gicv3_redist_read(void *opaque, hwaddr offset, uint64_t *data,
                              unsigned size, MemTxAttrs attrs)
{
    GICv3RedistRegion *region = opaque;
    GICv3State *s = region->gic;
    GICv3CPUState *cs;
    MemTxResult r;
    int cpuidx;

    assert((offset & (size - 1)) == 0);

    /*
     * There are (for GICv3) two 64K redistributor pages per CPU.
     * In some cases the redistributor pages for all CPUs are not
     * contiguous (eg on the virt board they are split into two
     * parts if there are too many CPUs to all fit in the same place
     * in the memory map); if so then the GIC has multiple MemoryRegions
     * for the redistributors.
     */
    cpuidx = region->cpuidx + offset / GICV3_REDIST_SIZE;
    offset %= GICV3_REDIST_SIZE;

    cs = &s->cpu[cpuidx];

    switch (size) {
    case 1:
        r = gicr_readb(cs, offset, data, attrs);
        break;
    case 4:
        r = gicr_readl(cs, offset, data, attrs);
        break;
    case 8:
        r = gicr_readll(cs, offset, data, attrs);
        break;
    default:
        r = MEMTX_ERROR;
        break;
    }

    if (r != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest read at offset " TARGET_FMT_plx
                      " size %u\n", __func__, offset, size);
        trace_gicv3_redist_badread(gicv3_redist_affid(cs), offset,
                                   size, attrs.secure);
        /* The spec requires that reserved registers are RAZ/WI;
         * so use MEMTX_ERROR returns from leaf functions as a way to
         * trigger the guest-error logging but don't return it to
         * the caller, or we'll cause a spurious guest data abort.
         */
        r = MEMTX_OK;
        *data = 0;
    } else {
        trace_gicv3_redist_read(gicv3_redist_affid(cs), offset, *data,
                                size, attrs.secure);
    }
    return r;
}

MemTxResult gicv3_redist_write(void *opaque, hwaddr offset, uint64_t data,
                               unsigned size, MemTxAttrs attrs)
{
    GICv3RedistRegion *region = opaque;
    GICv3State *s = region->gic;
    GICv3CPUState *cs;
    MemTxResult r;
    int cpuidx;

    assert((offset & (size - 1)) == 0);

    /*
     * There are (for GICv3) two 64K redistributor pages per CPU.
     * In some cases the redistributor pages for all CPUs are not
     * contiguous (eg on the virt board they are split into two
     * parts if there are too many CPUs to all fit in the same place
     * in the memory map); if so then the GIC has multiple MemoryRegions
     * for the redistributors.
     */
    cpuidx = region->cpuidx + offset / GICV3_REDIST_SIZE;
    offset %= GICV3_REDIST_SIZE;

    cs = &s->cpu[cpuidx];

    switch (size) {
    case 1:
        r = gicr_writeb(cs, offset, data, attrs);
        break;
    case 4:
        r = gicr_writel(cs, offset, data, attrs);
        break;
    case 8:
        r = gicr_writell(cs, offset, data, attrs);
        break;
    default:
        r = MEMTX_ERROR;
        break;
    }

    if (r != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest write at offset " TARGET_FMT_plx
                      " size %u\n", __func__, offset, size);
        trace_gicv3_redist_badwrite(gicv3_redist_affid(cs), offset, data,
                                    size, attrs.secure);
        /* The spec requires that reserved registers are RAZ/WI;
         * so use MEMTX_ERROR returns from leaf functions as a way to
         * trigger the guest-error logging but don't return it to
         * the caller, or we'll cause a spurious guest data abort.
         */
        r = MEMTX_OK;
    } else {
        trace_gicv3_redist_write(gicv3_redist_affid(cs), offset, data,
                                 size, attrs.secure);
    }
    return r;
}

static void gicv3_redist_check_lpi_priority(GICv3CPUState *cs, int irq)
{
    AddressSpace *as = &cs->gic->dma_as;
    uint64_t lpict_baddr;
    uint8_t lpite;
    uint8_t prio;

    lpict_baddr = cs->gicr_propbaser & R_GICR_PROPBASER_PHYADDR_MASK;

    address_space_read(as, lpict_baddr + ((irq - GICV3_LPI_INTID_START) *
                       sizeof(lpite)), MEMTXATTRS_UNSPECIFIED, &lpite,
                       sizeof(lpite));

    if (!(lpite & LPI_CTE_ENABLED)) {
        return;
    }

    if (cs->gic->gicd_ctlr & GICD_CTLR_DS) {
        prio = lpite & LPI_PRIORITY_MASK;
    } else {
        prio = ((lpite & LPI_PRIORITY_MASK) >> 1) | 0x80;
    }

    if ((prio < cs->hpplpi.prio) ||
        ((prio == cs->hpplpi.prio) && (irq <= cs->hpplpi.irq))) {
        cs->hpplpi.irq = irq;
        cs->hpplpi.prio = prio;
        /* LPIs are always non-secure Grp1 interrupts */
        cs->hpplpi.grp = GICV3_G1NS;
    }
}

void gicv3_redist_update_lpi_only(GICv3CPUState *cs)
{
    /*
     * This function scans the LPI pending table and for each pending
     * LPI, reads the corresponding entry from LPI configuration table
     * to extract the priority info and determine if the current LPI
     * priority is lower than the last computed high priority lpi interrupt.
     * If yes, replace current LPI as the new high priority lpi interrupt.
     */
    AddressSpace *as = &cs->gic->dma_as;
    uint64_t lpipt_baddr;
    uint32_t pendt_size = 0;
    uint8_t pend;
    int i, bit;
    uint64_t idbits;

    idbits = MIN(FIELD_EX64(cs->gicr_propbaser, GICR_PROPBASER, IDBITS),
                 GICD_TYPER_IDBITS);

    if (!(cs->gicr_ctlr & GICR_CTLR_ENABLE_LPIS)) {
        return;
    }

    cs->hpplpi.prio = 0xff;

    lpipt_baddr = cs->gicr_pendbaser & R_GICR_PENDBASER_PHYADDR_MASK;

    /* Determine the highest priority pending interrupt among LPIs */
    pendt_size = (1ULL << (idbits + 1));

    for (i = GICV3_LPI_INTID_START / 8; i < pendt_size / 8; i++) {
        address_space_read(as, lpipt_baddr + i, MEMTXATTRS_UNSPECIFIED, &pend,
                           sizeof(pend));

        while (pend) {
            bit = ctz32(pend);
            gicv3_redist_check_lpi_priority(cs, i * 8 + bit);
            pend &= ~(1 << bit);
        }
    }
}

void gicv3_redist_update_lpi(GICv3CPUState *cs)
{
    gicv3_redist_update_lpi_only(cs);
    gicv3_redist_update(cs);
}

void gicv3_redist_lpi_pending(GICv3CPUState *cs, int irq, int level)
{
    /*
     * This function updates the pending bit in lpi pending table for
     * the irq being activated or deactivated.
     */
    AddressSpace *as = &cs->gic->dma_as;
    uint64_t lpipt_baddr;
    bool ispend = false;
    uint8_t pend;

    /*
     * get the bit value corresponding to this irq in the
     * lpi pending table
     */
    lpipt_baddr = cs->gicr_pendbaser & R_GICR_PENDBASER_PHYADDR_MASK;

    address_space_read(as, lpipt_baddr + ((irq / 8) * sizeof(pend)),
                       MEMTXATTRS_UNSPECIFIED, &pend, sizeof(pend));

    ispend = extract32(pend, irq % 8, 1);

    /* no change in the value of pending bit, return */
    if (ispend == level) {
        return;
    }
    pend = deposit32(pend, irq % 8, 1, level ? 1 : 0);

    address_space_write(as, lpipt_baddr + ((irq / 8) * sizeof(pend)),
                        MEMTXATTRS_UNSPECIFIED, &pend, sizeof(pend));

    /*
     * check if this LPI is better than the current hpplpi, if yes
     * just set hpplpi.prio and .irq without doing a full rescan
     */
    if (level) {
        gicv3_redist_check_lpi_priority(cs, irq);
        gicv3_redist_update(cs);
    } else {
        if (irq == cs->hpplpi.irq) {
            gicv3_redist_update_lpi(cs);
        }
    }
}

void gicv3_redist_process_lpi(GICv3CPUState *cs, int irq, int level)
{
    uint64_t idbits;

    idbits = MIN(FIELD_EX64(cs->gicr_propbaser, GICR_PROPBASER, IDBITS),
                 GICD_TYPER_IDBITS);

    if (!(cs->gicr_ctlr & GICR_CTLR_ENABLE_LPIS) ||
        (irq > (1ULL << (idbits + 1)) - 1) || irq < GICV3_LPI_INTID_START) {
        return;
    }

    /* set/clear the pending bit for this irq */
    gicv3_redist_lpi_pending(cs, irq, level);
}

void gicv3_redist_mov_lpi(GICv3CPUState *src, GICv3CPUState *dest, int irq)
{
    /*
     * Move the specified LPI's pending state from the source redistributor
     * to the destination.
     *
     * If LPIs are disabled on dest this is CONSTRAINED UNPREDICTABLE:
     * we choose to NOP. If LPIs are disabled on source there's nothing
     * to be transferred anyway.
     */
    AddressSpace *as = &src->gic->dma_as;
    uint64_t idbits;
    uint32_t pendt_size;
    uint64_t src_baddr;
    uint8_t src_pend;

    if (!(src->gicr_ctlr & GICR_CTLR_ENABLE_LPIS) ||
        !(dest->gicr_ctlr & GICR_CTLR_ENABLE_LPIS)) {
        return;
    }

    idbits = MIN(FIELD_EX64(src->gicr_propbaser, GICR_PROPBASER, IDBITS),
                 GICD_TYPER_IDBITS);
    idbits = MIN(FIELD_EX64(dest->gicr_propbaser, GICR_PROPBASER, IDBITS),
                 idbits);

    pendt_size = 1ULL << (idbits + 1);
    if ((irq / 8) >= pendt_size) {
        return;
    }

    src_baddr = src->gicr_pendbaser & R_GICR_PENDBASER_PHYADDR_MASK;

    address_space_read(as, src_baddr + (irq / 8),
                       MEMTXATTRS_UNSPECIFIED, &src_pend, sizeof(src_pend));
    if (!extract32(src_pend, irq % 8, 1)) {
        /* Not pending on source, nothing to do */
        return;
    }
    src_pend &= ~(1 << (irq % 8));
    address_space_write(as, src_baddr + (irq / 8),
                        MEMTXATTRS_UNSPECIFIED, &src_pend, sizeof(src_pend));
    if (irq == src->hpplpi.irq) {
        /*
         * We just made this LPI not-pending so only need to update
         * if it was previously the highest priority pending LPI
         */
        gicv3_redist_update_lpi(src);
    }
    /* Mark it pending on the destination */
    gicv3_redist_lpi_pending(dest, irq, 1);
}

void gicv3_redist_movall_lpis(GICv3CPUState *src, GICv3CPUState *dest)
{
    /*
     * We must move all pending LPIs from the source redistributor
     * to the destination. That is, for every pending LPI X on
     * src, we must set it not-pending on src and pending on dest.
     * LPIs that are already pending on dest are not cleared.
     *
     * If LPIs are disabled on dest this is CONSTRAINED UNPREDICTABLE:
     * we choose to NOP. If LPIs are disabled on source there's nothing
     * to be transferred anyway.
     */
    AddressSpace *as = &src->gic->dma_as;
    uint64_t idbits;
    uint32_t pendt_size;
    uint64_t src_baddr, dest_baddr;
    int i;

    if (!(src->gicr_ctlr & GICR_CTLR_ENABLE_LPIS) ||
        !(dest->gicr_ctlr & GICR_CTLR_ENABLE_LPIS)) {
        return;
    }

    idbits = MIN(FIELD_EX64(src->gicr_propbaser, GICR_PROPBASER, IDBITS),
                 GICD_TYPER_IDBITS);
    idbits = MIN(FIELD_EX64(dest->gicr_propbaser, GICR_PROPBASER, IDBITS),
                 idbits);

    pendt_size = 1ULL << (idbits + 1);
    src_baddr = src->gicr_pendbaser & R_GICR_PENDBASER_PHYADDR_MASK;
    dest_baddr = dest->gicr_pendbaser & R_GICR_PENDBASER_PHYADDR_MASK;

    for (i = GICV3_LPI_INTID_START / 8; i < pendt_size / 8; i++) {
        uint8_t src_pend, dest_pend;

        address_space_read(as, src_baddr + i, MEMTXATTRS_UNSPECIFIED,
                           &src_pend, sizeof(src_pend));
        if (!src_pend) {
            continue;
        }
        address_space_read(as, dest_baddr + i, MEMTXATTRS_UNSPECIFIED,
                           &dest_pend, sizeof(dest_pend));
        dest_pend |= src_pend;
        src_pend = 0;
        address_space_write(as, src_baddr + i, MEMTXATTRS_UNSPECIFIED,
                            &src_pend, sizeof(src_pend));
        address_space_write(as, dest_baddr + i, MEMTXATTRS_UNSPECIFIED,
                            &dest_pend, sizeof(dest_pend));
    }

    gicv3_redist_update_lpi(src);
    gicv3_redist_update_lpi(dest);
}

void gicv3_redist_set_irq(GICv3CPUState *cs, int irq, int level)
{
    /* Update redistributor state for a change in an external PPI input line */
    if (level == extract32(cs->level, irq, 1)) {
        return;
    }

    trace_gicv3_redist_set_irq(gicv3_redist_affid(cs), irq, level);

    cs->level = deposit32(cs->level, irq, 1, level);

    if (level) {
        /* 0->1 edges latch the pending bit for edge-triggered interrupts */
        if (extract32(cs->edge_trigger, irq, 1)) {
            cs->gicr_ipendr0 = deposit32(cs->gicr_ipendr0, irq, 1, 1);
        }
    }

    gicv3_redist_update(cs);
}

void gicv3_redist_send_sgi(GICv3CPUState *cs, int grp, int irq, bool ns)
{
    /* Update redistributor state for a generated SGI */
    int irqgrp = gicv3_irq_group(cs->gic, cs, irq);

    /* If we are asked for a Secure Group 1 SGI and it's actually
     * configured as Secure Group 0 this is OK (subject to the usual
     * NSACR checks).
     */
    if (grp == GICV3_G1 && irqgrp == GICV3_G0) {
        grp = GICV3_G0;
    }

    if (grp != irqgrp) {
        return;
    }

    if (ns && !(cs->gic->gicd_ctlr & GICD_CTLR_DS)) {
        /* If security is enabled we must test the NSACR bits */
        int nsaccess = gicr_ns_access(cs, irq);

        if ((irqgrp == GICV3_G0 && nsaccess < 1) ||
            (irqgrp == GICV3_G1 && nsaccess < 2)) {
            return;
        }
    }

    /* OK, we can accept the SGI */
    trace_gicv3_redist_send_sgi(gicv3_redist_affid(cs), irq);
    cs->gicr_ipendr0 = deposit32(cs->gicr_ipendr0, irq, 1, 1);
    gicv3_redist_update(cs);
}
