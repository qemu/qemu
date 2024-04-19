/*
 * ARM GICv3 emulation: Distributor
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

/* The GICD_NSACR registers contain a two bit field for each interrupt which
 * allows the guest to give NonSecure code access to registers controlling
 * Secure interrupts:
 *  0b00: no access (NS accesses to bits for Secure interrupts will RAZ/WI)
 *  0b01: NS r/w accesses permitted to ISPENDR, SETSPI_NSR, SGIR
 *  0b10: as 0b01, and also r/w to ICPENDR, r/o to ISACTIVER/ICACTIVER,
 *        and w/o to CLRSPI_NSR
 *  0b11: as 0b10, and also r/w to IROUTER and ITARGETSR
 *
 * Given a (multiple-of-32) interrupt number, these mask functions return
 * a mask word where each bit is 1 if the NSACR settings permit access
 * to the interrupt. The mask returned can then be ORed with the GICD_GROUP
 * word for this set of interrupts to give an overall mask.
 */

typedef uint32_t maskfn(GICv3State *s, int irq);

static uint32_t mask_nsacr_ge1(GICv3State *s, int irq)
{
    /* Return a mask where each bit is set if the NSACR field is >= 1 */
    uint64_t raw_nsacr = s->gicd_nsacr[irq / 16 + 1];

    raw_nsacr = raw_nsacr << 32 | s->gicd_nsacr[irq / 16];
    raw_nsacr = (raw_nsacr >> 1) | raw_nsacr;
    return half_unshuffle64(raw_nsacr);
}

static uint32_t mask_nsacr_ge2(GICv3State *s, int irq)
{
    /* Return a mask where each bit is set if the NSACR field is >= 2 */
    uint64_t raw_nsacr = s->gicd_nsacr[irq / 16 + 1];

    raw_nsacr = raw_nsacr << 32 | s->gicd_nsacr[irq / 16];
    raw_nsacr = raw_nsacr >> 1;
    return half_unshuffle64(raw_nsacr);
}

/* We don't need a mask_nsacr_ge3() because IROUTER<n> isn't a bitmap register,
 * but it would be implemented using:
 *  raw_nsacr = (raw_nsacr >> 1) & raw_nsacr;
 */

static uint32_t mask_group_and_nsacr(GICv3State *s, MemTxAttrs attrs,
                                     maskfn *maskfn, int irq)
{
    /* Return a 32-bit mask which should be applied for this set of 32
     * interrupts; each bit is 1 if access is permitted by the
     * combination of attrs.secure, GICD_GROUPR and GICD_NSACR.
     */
    uint32_t mask;

    if (!attrs.secure && !(s->gicd_ctlr & GICD_CTLR_DS)) {
        /* bits for Group 0 or Secure Group 1 interrupts are RAZ/WI
         * unless the NSACR bits permit access.
         */
        mask = *gic_bmp_ptr32(s->group, irq);
        if (maskfn) {
            mask |= maskfn(s, irq);
        }
        return mask;
    }
    return 0xFFFFFFFFU;
}

static int gicd_ns_access(GICv3State *s, int irq)
{
    /* Return the 2 bit NS_access<x> field from GICD_NSACR<n> for the
     * specified interrupt.
     */
    if (irq < GIC_INTERNAL || irq >= s->num_irq) {
        return 0;
    }
    return extract32(s->gicd_nsacr[irq / 16], (irq % 16) * 2, 2);
}

static void gicd_write_bitmap_reg(GICv3State *s, MemTxAttrs attrs,
                                  uint32_t *bmp, maskfn *maskfn,
                                  int offset, uint32_t val)
{
    /*
     * Helper routine to implement writing to a "set" register
     * (GICD_INMIR, etc).
     * Semantics implemented here:
     * RAZ/WI for SGIs, PPIs, unimplemented IRQs
     * Bits corresponding to Group 0 or Secure Group 1 interrupts RAZ/WI.
     * offset should be the offset in bytes of the register from the start
     * of its group.
     */
    int irq = offset * 8;

    if (irq < GIC_INTERNAL || irq >= s->num_irq) {
        return;
    }
    val &= mask_group_and_nsacr(s, attrs, maskfn, irq);
    *gic_bmp_ptr32(bmp, irq) = val;
    gicv3_update(s, irq, 32);
}

static void gicd_write_set_bitmap_reg(GICv3State *s, MemTxAttrs attrs,
                                      uint32_t *bmp,
                                      maskfn *maskfn,
                                      int offset, uint32_t val)
{
    /* Helper routine to implement writing to a "set-bitmap" register
     * (GICD_ISENABLER, GICD_ISPENDR, etc).
     * Semantics implemented here:
     * RAZ/WI for SGIs, PPIs, unimplemented IRQs
     * Bits corresponding to Group 0 or Secure Group 1 interrupts RAZ/WI.
     * Writing 1 means "set bit in bitmap"; writing 0 is ignored.
     * offset should be the offset in bytes of the register from the start
     * of its group.
     */
    int irq = offset * 8;

    if (irq < GIC_INTERNAL || irq >= s->num_irq) {
        return;
    }
    val &= mask_group_and_nsacr(s, attrs, maskfn, irq);
    *gic_bmp_ptr32(bmp, irq) |= val;
    gicv3_update(s, irq, 32);
}

static void gicd_write_clear_bitmap_reg(GICv3State *s, MemTxAttrs attrs,
                                        uint32_t *bmp,
                                        maskfn *maskfn,
                                        int offset, uint32_t val)
{
    /* Helper routine to implement writing to a "clear-bitmap" register
     * (GICD_ICENABLER, GICD_ICPENDR, etc).
     * Semantics implemented here:
     * RAZ/WI for SGIs, PPIs, unimplemented IRQs
     * Bits corresponding to Group 0 or Secure Group 1 interrupts RAZ/WI.
     * Writing 1 means "clear bit in bitmap"; writing 0 is ignored.
     * offset should be the offset in bytes of the register from the start
     * of its group.
     */
    int irq = offset * 8;

    if (irq < GIC_INTERNAL || irq >= s->num_irq) {
        return;
    }
    val &= mask_group_and_nsacr(s, attrs, maskfn, irq);
    *gic_bmp_ptr32(bmp, irq) &= ~val;
    gicv3_update(s, irq, 32);
}

static uint32_t gicd_read_bitmap_reg(GICv3State *s, MemTxAttrs attrs,
                                     uint32_t *bmp,
                                     maskfn *maskfn,
                                     int offset)
{
    /* Helper routine to implement reading a "set/clear-bitmap" register
     * (GICD_ICENABLER, GICD_ISENABLER, GICD_ICPENDR, etc).
     * Semantics implemented here:
     * RAZ/WI for SGIs, PPIs, unimplemented IRQs
     * Bits corresponding to Group 0 or Secure Group 1 interrupts RAZ/WI.
     * offset should be the offset in bytes of the register from the start
     * of its group.
     */
    int irq = offset * 8;
    uint32_t val;

    if (irq < GIC_INTERNAL || irq >= s->num_irq) {
        return 0;
    }
    val = *gic_bmp_ptr32(bmp, irq);
    if (bmp == s->pending) {
        /* The PENDING register is a special case -- for level triggered
         * interrupts, the PENDING state is the logical OR of the state of
         * the PENDING latch with the input line level.
         */
        uint32_t edge = *gic_bmp_ptr32(s->edge_trigger, irq);
        uint32_t level = *gic_bmp_ptr32(s->level, irq);
        val |= (~edge & level);
    }
    val &= mask_group_and_nsacr(s, attrs, maskfn, irq);
    return val;
}

static uint8_t gicd_read_ipriorityr(GICv3State *s, MemTxAttrs attrs, int irq)
{
    /* Read the value of GICD_IPRIORITYR<n> for the specified interrupt,
     * honouring security state (these are RAZ/WI for Group 0 or Secure
     * Group 1 interrupts).
     */
    uint32_t prio;

    if (irq < GIC_INTERNAL || irq >= s->num_irq) {
        return 0;
    }

    prio = s->gicd_ipriority[irq];

    if (!attrs.secure && !(s->gicd_ctlr & GICD_CTLR_DS)) {
        if (!gicv3_gicd_group_test(s, irq)) {
            /* Fields for Group 0 or Secure Group 1 interrupts are RAZ/WI */
            return 0;
        }
        /* NS view of the interrupt priority */
        prio = (prio << 1) & 0xff;
    }
    return prio;
}

static void gicd_write_ipriorityr(GICv3State *s, MemTxAttrs attrs, int irq,
                                  uint8_t value)
{
    /* Write the value of GICD_IPRIORITYR<n> for the specified interrupt,
     * honouring security state (these are RAZ/WI for Group 0 or Secure
     * Group 1 interrupts).
     */
    if (irq < GIC_INTERNAL || irq >= s->num_irq) {
        return;
    }

    if (!attrs.secure && !(s->gicd_ctlr & GICD_CTLR_DS)) {
        if (!gicv3_gicd_group_test(s, irq)) {
            /* Fields for Group 0 or Secure Group 1 interrupts are RAZ/WI */
            return;
        }
        /* NS view of the interrupt priority */
        value = 0x80 | (value >> 1);
    }
    s->gicd_ipriority[irq] = value;
}

static uint64_t gicd_read_irouter(GICv3State *s, MemTxAttrs attrs, int irq)
{
    /* Read the value of GICD_IROUTER<n> for the specified interrupt,
     * honouring security state.
     */
    if (irq < GIC_INTERNAL || irq >= s->num_irq) {
        return 0;
    }

    if (!attrs.secure && !(s->gicd_ctlr & GICD_CTLR_DS)) {
        /* RAZ/WI for NS accesses to secure interrupts */
        if (!gicv3_gicd_group_test(s, irq)) {
            if (gicd_ns_access(s, irq) != 3) {
                return 0;
            }
        }
    }

    return s->gicd_irouter[irq];
}

static void gicd_write_irouter(GICv3State *s, MemTxAttrs attrs, int irq,
                               uint64_t val)
{
    /* Write the value of GICD_IROUTER<n> for the specified interrupt,
     * honouring security state.
     */
    if (irq < GIC_INTERNAL || irq >= s->num_irq) {
        return;
    }

    if (!attrs.secure && !(s->gicd_ctlr & GICD_CTLR_DS)) {
        /* RAZ/WI for NS accesses to secure interrupts */
        if (!gicv3_gicd_group_test(s, irq)) {
            if (gicd_ns_access(s, irq) != 3) {
                return;
            }
        }
    }

    s->gicd_irouter[irq] = val;
    gicv3_cache_target_cpustate(s, irq);
    gicv3_update(s, irq, 1);
}

/**
 * gicd_readb
 * gicd_readw
 * gicd_readl
 * gicd_readq
 * gicd_writeb
 * gicd_writew
 * gicd_writel
 * gicd_writeq
 *
 * Return %true if the operation succeeded, %false otherwise.
 */

static bool gicd_readb(GICv3State *s, hwaddr offset,
                       uint64_t *data, MemTxAttrs attrs)
{
    /* Most GICv3 distributor registers do not support byte accesses. */
    switch (offset) {
    case GICD_CPENDSGIR ... GICD_CPENDSGIR + 0xf:
    case GICD_SPENDSGIR ... GICD_SPENDSGIR + 0xf:
    case GICD_ITARGETSR ... GICD_ITARGETSR + 0x3ff:
        /* This GIC implementation always has affinity routing enabled,
         * so these registers are all RAZ/WI.
         */
        return true;
    case GICD_IPRIORITYR ... GICD_IPRIORITYR + 0x3ff:
        *data = gicd_read_ipriorityr(s, attrs, offset - GICD_IPRIORITYR);
        return true;
    default:
        return false;
    }
}

static bool gicd_writeb(GICv3State *s, hwaddr offset,
                        uint64_t value, MemTxAttrs attrs)
{
    /* Most GICv3 distributor registers do not support byte accesses. */
    switch (offset) {
    case GICD_CPENDSGIR ... GICD_CPENDSGIR + 0xf:
    case GICD_SPENDSGIR ... GICD_SPENDSGIR + 0xf:
    case GICD_ITARGETSR ... GICD_ITARGETSR + 0x3ff:
        /* This GIC implementation always has affinity routing enabled,
         * so these registers are all RAZ/WI.
         */
        return true;
    case GICD_IPRIORITYR ... GICD_IPRIORITYR + 0x3ff:
    {
        int irq = offset - GICD_IPRIORITYR;

        if (irq < GIC_INTERNAL || irq >= s->num_irq) {
            return true;
        }
        gicd_write_ipriorityr(s, attrs, irq, value);
        gicv3_update(s, irq, 1);
        return true;
    }
    default:
        return false;
    }
}

static bool gicd_readw(GICv3State *s, hwaddr offset,
                       uint64_t *data, MemTxAttrs attrs)
{
    /* Only GICD_SETSPI_NSR, GICD_CLRSPI_NSR, GICD_SETSPI_SR and GICD_SETSPI_NSR
     * support 16 bit accesses, and those registers are all part of the
     * optional message-based SPI feature which this GIC does not currently
     * implement (ie for us GICD_TYPER.MBIS == 0), so for us they are
     * reserved.
     */
    return false;
}

static bool gicd_writew(GICv3State *s, hwaddr offset,
                        uint64_t value, MemTxAttrs attrs)
{
    /* Only GICD_SETSPI_NSR, GICD_CLRSPI_NSR, GICD_SETSPI_SR and GICD_SETSPI_NSR
     * support 16 bit accesses, and those registers are all part of the
     * optional message-based SPI feature which this GIC does not currently
     * implement (ie for us GICD_TYPER.MBIS == 0), so for us they are
     * reserved.
     */
    return false;
}

static bool gicd_readl(GICv3State *s, hwaddr offset,
                       uint64_t *data, MemTxAttrs attrs)
{
    /* Almost all GICv3 distributor registers are 32-bit.
     * Note that WO registers must return an UNKNOWN value on reads,
     * not an abort.
     */

    switch (offset) {
    case GICD_CTLR:
        if (!attrs.secure && !(s->gicd_ctlr & GICD_CTLR_DS)) {
            /* The NS view of the GICD_CTLR sees only certain bits:
             * + bit [31] (RWP) is an alias of the Secure bit [31]
             * + bit [4] (ARE_NS) is an alias of Secure bit [5]
             * + bit [1] (EnableGrp1A) is an alias of Secure bit [1] if
             *   NS affinity routing is enabled, otherwise RES0
             * + bit [0] (EnableGrp1) is an alias of Secure bit [1] if
             *   NS affinity routing is not enabled, otherwise RES0
             * Since for QEMU affinity routing is always enabled
             * for both S and NS this means that bits [4] and [5] are
             * both always 1, and we can simply make the NS view
             * be bits 31, 4 and 1 of the S view.
             */
            *data = s->gicd_ctlr & (GICD_CTLR_ARE_S |
                                    GICD_CTLR_EN_GRP1NS |
                                    GICD_CTLR_RWP);
        } else {
            *data = s->gicd_ctlr;
        }
        return true;
    case GICD_TYPER:
    {
        /* For this implementation:
         * No1N == 1 (1-of-N SPI interrupts not supported)
         * A3V == 1 (non-zero values of Affinity level 3 supported)
         * IDbits == 0xf (we support 16-bit interrupt identifiers)
         * DVIS == 1 (Direct virtual LPI injection supported) if GICv4
         * LPIS == 1 (LPIs are supported if affinity routing is enabled)
         * num_LPIs == 0b00000 (bits [15:11],Number of LPIs as indicated
         *                      by GICD_TYPER.IDbits)
         * MBIS == 0 (message-based SPIs not supported)
         * SecurityExtn == 1 if security extns supported
         * NMI = 1 if Non-maskable interrupt property is supported
         * CPUNumber == 0 since for us ARE is always 1
         * ITLinesNumber == (((max SPI IntID + 1) / 32) - 1)
         */
        int itlinesnumber = (s->num_irq / 32) - 1;
        /*
         * SecurityExtn must be RAZ if GICD_CTLR.DS == 1, and
         * "security extensions not supported" always implies DS == 1,
         * so we only need to check the DS bit.
         */
        bool sec_extn = !(s->gicd_ctlr & GICD_CTLR_DS);
        bool dvis = s->revision >= 4;

        *data = (1 << 25) | (1 << 24) | (dvis << 18) | (sec_extn << 10) |
            (s->nmi_support << GICD_TYPER_NMI_SHIFT) |
            (s->lpi_enable << GICD_TYPER_LPIS_SHIFT) |
            (0xf << 19) | itlinesnumber;
        return true;
    }
    case GICD_IIDR:
        /* We claim to be an ARM r0p0 with a zero ProductID.
         * This is the same as an r0p0 GIC-500.
         */
        *data = gicv3_iidr();
        return true;
    case GICD_STATUSR:
        /* RAZ/WI for us (this is an optional register and our implementation
         * does not track RO/WO/reserved violations to report them to the guest)
         */
        *data = 0;
        return true;
    case GICD_IGROUPR ... GICD_IGROUPR + 0x7f:
    {
        int irq;

        if (!attrs.secure && !(s->gicd_ctlr & GICD_CTLR_DS)) {
            *data = 0;
            return true;
        }
        /* RAZ/WI for SGIs, PPIs, unimplemented irqs */
        irq = (offset - GICD_IGROUPR) * 8;
        if (irq < GIC_INTERNAL || irq >= s->num_irq) {
            *data = 0;
            return true;
        }
        *data = *gic_bmp_ptr32(s->group, irq);
        return true;
    }
    case GICD_ISENABLER ... GICD_ISENABLER + 0x7f:
        *data = gicd_read_bitmap_reg(s, attrs, s->enabled, NULL,
                                     offset - GICD_ISENABLER);
        return true;
    case GICD_ICENABLER ... GICD_ICENABLER + 0x7f:
        *data = gicd_read_bitmap_reg(s, attrs, s->enabled, NULL,
                                     offset - GICD_ICENABLER);
        return true;
    case GICD_ISPENDR ... GICD_ISPENDR + 0x7f:
        *data = gicd_read_bitmap_reg(s, attrs, s->pending, mask_nsacr_ge1,
                                     offset - GICD_ISPENDR);
        return true;
    case GICD_ICPENDR ... GICD_ICPENDR + 0x7f:
        *data = gicd_read_bitmap_reg(s, attrs, s->pending, mask_nsacr_ge2,
                                     offset - GICD_ICPENDR);
        return true;
    case GICD_ISACTIVER ... GICD_ISACTIVER + 0x7f:
        *data = gicd_read_bitmap_reg(s, attrs, s->active, mask_nsacr_ge2,
                                     offset - GICD_ISACTIVER);
        return true;
    case GICD_ICACTIVER ... GICD_ICACTIVER + 0x7f:
        *data = gicd_read_bitmap_reg(s, attrs, s->active, mask_nsacr_ge2,
                                     offset - GICD_ICACTIVER);
        return true;
    case GICD_IPRIORITYR ... GICD_IPRIORITYR + 0x3ff:
    {
        int i, irq = offset - GICD_IPRIORITYR;
        uint32_t value = 0;

        for (i = irq + 3; i >= irq; i--) {
            value <<= 8;
            value |= gicd_read_ipriorityr(s, attrs, i);
        }
        *data = value;
        return true;
    }
    case GICD_ITARGETSR ... GICD_ITARGETSR + 0x3ff:
        /* RAZ/WI since affinity routing is always enabled */
        *data = 0;
        return true;
    case GICD_ICFGR ... GICD_ICFGR + 0xff:
    {
        /* Here only the even bits are used; odd bits are RES0 */
        int irq = (offset - GICD_ICFGR) * 4;
        uint32_t value = 0;

        if (irq < GIC_INTERNAL || irq >= s->num_irq) {
            *data = 0;
            return true;
        }

        /* Since our edge_trigger bitmap is one bit per irq, we only need
         * half of the 32-bit word, which we can then spread out
         * into the odd bits.
         */
        value = *gic_bmp_ptr32(s->edge_trigger, irq & ~0x1f);
        value &= mask_group_and_nsacr(s, attrs, NULL, irq & ~0x1f);
        value = extract32(value, (irq & 0x1f) ? 16 : 0, 16);
        value = half_shuffle32(value) << 1;
        *data = value;
        return true;
    }
    case GICD_IGRPMODR ... GICD_IGRPMODR + 0xff:
    {
        int irq;

        if ((s->gicd_ctlr & GICD_CTLR_DS) || !attrs.secure) {
            /* RAZ/WI if security disabled, or if
             * security enabled and this is an NS access
             */
            *data = 0;
            return true;
        }
        /* RAZ/WI for SGIs, PPIs, unimplemented irqs */
        irq = (offset - GICD_IGRPMODR) * 8;
        if (irq < GIC_INTERNAL || irq >= s->num_irq) {
            *data = 0;
            return true;
        }
        *data = *gic_bmp_ptr32(s->grpmod, irq);
        return true;
    }
    case GICD_NSACR ... GICD_NSACR + 0xff:
    {
        /* Two bits per interrupt */
        int irq = (offset - GICD_NSACR) * 4;

        if (irq < GIC_INTERNAL || irq >= s->num_irq) {
            *data = 0;
            return true;
        }

        if ((s->gicd_ctlr & GICD_CTLR_DS) || !attrs.secure) {
            /* RAZ/WI if security disabled, or if
             * security enabled and this is an NS access
             */
            *data = 0;
            return true;
        }

        *data = s->gicd_nsacr[irq / 16];
        return true;
    }
    case GICD_CPENDSGIR ... GICD_CPENDSGIR + 0xf:
    case GICD_SPENDSGIR ... GICD_SPENDSGIR + 0xf:
        /* RAZ/WI since affinity routing is always enabled */
        *data = 0;
        return true;
    case GICD_INMIR ... GICD_INMIR + 0x7f:
        *data = (!s->nmi_support) ? 0 :
                gicd_read_bitmap_reg(s, attrs, s->nmi, NULL,
                                     offset - GICD_INMIR);
        return true;
    case GICD_IROUTER ... GICD_IROUTER + 0x1fdf:
    {
        uint64_t r;
        int irq = (offset - GICD_IROUTER) / 8;

        r = gicd_read_irouter(s, attrs, irq);
        if (offset & 7) {
            *data = r >> 32;
        } else {
            *data = (uint32_t)r;
        }
        return true;
    }
    case GICD_IDREGS ... GICD_IDREGS + 0x2f:
        /* ID registers */
        *data = gicv3_idreg(s, offset - GICD_IDREGS, GICV3_PIDR0_DIST);
        return true;
    case GICD_SGIR:
        /* WO registers, return unknown value */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest read from WO register at offset "
                      HWADDR_FMT_plx "\n", __func__, offset);
        *data = 0;
        return true;
    default:
        return false;
    }
}

static bool gicd_writel(GICv3State *s, hwaddr offset,
                        uint64_t value, MemTxAttrs attrs)
{
    /* Almost all GICv3 distributor registers are 32-bit. Note that
     * RO registers must ignore writes, not abort.
     */

    switch (offset) {
    case GICD_CTLR:
    {
        uint32_t mask;
        /* GICv3 5.3.20 */
        if (s->gicd_ctlr & GICD_CTLR_DS) {
            /* With only one security state, E1NWF is RAZ/WI, DS is RAO/WI,
             * ARE is RAO/WI (affinity routing always on), and only
             * bits 0 and 1 (group enables) are writable.
             */
            mask = GICD_CTLR_EN_GRP0 | GICD_CTLR_EN_GRP1NS;
        } else {
            if (attrs.secure) {
                /* for secure access:
                 * ARE_NS and ARE_S are RAO/WI (affinity routing always on)
                 * E1NWF is RAZ/WI (we don't support enable-1-of-n-wakeup)
                 *
                 * We can only modify bits[2:0] (the group enables).
                 */
                mask = GICD_CTLR_DS | GICD_CTLR_EN_GRP0 | GICD_CTLR_EN_GRP1_ALL;
            } else {
                /* For non secure access ARE_NS is RAO/WI and EnableGrp1
                 * is RES0. The only writable bit is [1] (EnableGrp1A), which
                 * is an alias of the Secure bit [1].
                 */
                mask = GICD_CTLR_EN_GRP1NS;
            }
        }
        s->gicd_ctlr = (s->gicd_ctlr & ~mask) | (value & mask);
        if (value & mask & GICD_CTLR_DS) {
            /* We just set DS, so the ARE_NS and EnG1S bits are now RES0.
             * Note that this is a one-way transition because if DS is set
             * then it's not writable, so it can only go back to 0 with a
             * hardware reset.
             */
            s->gicd_ctlr &= ~(GICD_CTLR_EN_GRP1S | GICD_CTLR_ARE_NS);
        }
        gicv3_full_update(s);
        return true;
    }
    case GICD_STATUSR:
        /* RAZ/WI for our implementation */
        return true;
    case GICD_IGROUPR ... GICD_IGROUPR + 0x7f:
    {
        int irq;

        if (!attrs.secure && !(s->gicd_ctlr & GICD_CTLR_DS)) {
            return true;
        }
        /* RAZ/WI for SGIs, PPIs, unimplemented irqs */
        irq = (offset - GICD_IGROUPR) * 8;
        if (irq < GIC_INTERNAL || irq >= s->num_irq) {
            return true;
        }
        *gic_bmp_ptr32(s->group, irq) = value;
        gicv3_update(s, irq, 32);
        return true;
    }
    case GICD_ISENABLER ... GICD_ISENABLER + 0x7f:
        gicd_write_set_bitmap_reg(s, attrs, s->enabled, NULL,
                                  offset - GICD_ISENABLER, value);
        return true;
    case GICD_ICENABLER ... GICD_ICENABLER + 0x7f:
        gicd_write_clear_bitmap_reg(s, attrs, s->enabled, NULL,
                                    offset - GICD_ICENABLER, value);
        return true;
    case GICD_ISPENDR ... GICD_ISPENDR + 0x7f:
        gicd_write_set_bitmap_reg(s, attrs, s->pending, mask_nsacr_ge1,
                                  offset - GICD_ISPENDR, value);
        return true;
    case GICD_ICPENDR ... GICD_ICPENDR + 0x7f:
        gicd_write_clear_bitmap_reg(s, attrs, s->pending, mask_nsacr_ge2,
                                    offset - GICD_ICPENDR, value);
        return true;
    case GICD_ISACTIVER ... GICD_ISACTIVER + 0x7f:
        gicd_write_set_bitmap_reg(s, attrs, s->active, NULL,
                                  offset - GICD_ISACTIVER, value);
        return true;
    case GICD_ICACTIVER ... GICD_ICACTIVER + 0x7f:
        gicd_write_clear_bitmap_reg(s, attrs, s->active, NULL,
                                    offset - GICD_ICACTIVER, value);
        return true;
    case GICD_IPRIORITYR ... GICD_IPRIORITYR + 0x3ff:
    {
        int i, irq = offset - GICD_IPRIORITYR;

        if (irq < GIC_INTERNAL || irq + 3 >= s->num_irq) {
            return true;
        }

        for (i = irq; i < irq + 4; i++, value >>= 8) {
            gicd_write_ipriorityr(s, attrs, i, value);
        }
        gicv3_update(s, irq, 4);
        return true;
    }
    case GICD_ITARGETSR ... GICD_ITARGETSR + 0x3ff:
        /* RAZ/WI since affinity routing is always enabled */
        return true;
    case GICD_ICFGR ... GICD_ICFGR + 0xff:
    {
        /* Here only the odd bits are used; even bits are RES0 */
        int irq = (offset - GICD_ICFGR) * 4;
        uint32_t mask, oldval;

        if (irq < GIC_INTERNAL || irq >= s->num_irq) {
            return true;
        }

        /* Since our edge_trigger bitmap is one bit per irq, our input
         * 32-bits will compress down into 16 bits which we need
         * to write into the bitmap.
         */
        value = half_unshuffle32(value >> 1);
        mask = mask_group_and_nsacr(s, attrs, NULL, irq & ~0x1f);
        if (irq & 0x1f) {
            value <<= 16;
            mask &= 0xffff0000U;
        } else {
            mask &= 0xffff;
        }
        oldval = *gic_bmp_ptr32(s->edge_trigger, (irq & ~0x1f));
        value = (oldval & ~mask) | (value & mask);
        *gic_bmp_ptr32(s->edge_trigger, irq & ~0x1f) = value;
        return true;
    }
    case GICD_IGRPMODR ... GICD_IGRPMODR + 0xff:
    {
        int irq;

        if ((s->gicd_ctlr & GICD_CTLR_DS) || !attrs.secure) {
            /* RAZ/WI if security disabled, or if
             * security enabled and this is an NS access
             */
            return true;
        }
        /* RAZ/WI for SGIs, PPIs, unimplemented irqs */
        irq = (offset - GICD_IGRPMODR) * 8;
        if (irq < GIC_INTERNAL || irq >= s->num_irq) {
            return true;
        }
        *gic_bmp_ptr32(s->grpmod, irq) = value;
        gicv3_update(s, irq, 32);
        return true;
    }
    case GICD_NSACR ... GICD_NSACR + 0xff:
    {
        /* Two bits per interrupt */
        int irq = (offset - GICD_NSACR) * 4;

        if (irq < GIC_INTERNAL || irq >= s->num_irq) {
            return true;
        }

        if ((s->gicd_ctlr & GICD_CTLR_DS) || !attrs.secure) {
            /* RAZ/WI if security disabled, or if
             * security enabled and this is an NS access
             */
            return true;
        }

        s->gicd_nsacr[irq / 16] = value;
        /* No update required as this only affects access permission checks */
        return true;
    }
    case GICD_SGIR:
        /* RES0 if affinity routing is enabled */
        return true;
    case GICD_CPENDSGIR ... GICD_CPENDSGIR + 0xf:
    case GICD_SPENDSGIR ... GICD_SPENDSGIR + 0xf:
        /* RAZ/WI since affinity routing is always enabled */
        return true;
    case GICD_INMIR ... GICD_INMIR + 0x7f:
        if (s->nmi_support) {
            gicd_write_bitmap_reg(s, attrs, s->nmi, NULL,
                                  offset - GICD_INMIR, value);
        }
        return true;
    case GICD_IROUTER ... GICD_IROUTER + 0x1fdf:
    {
        uint64_t r;
        int irq = (offset - GICD_IROUTER) / 8;

        if (irq < GIC_INTERNAL || irq >= s->num_irq) {
            return true;
        }

        /* Write half of the 64-bit register */
        r = gicd_read_irouter(s, attrs, irq);
        r = deposit64(r, (offset & 7) ? 32 : 0, 32, value);
        gicd_write_irouter(s, attrs, irq, r);
        return true;
    }
    case GICD_IDREGS ... GICD_IDREGS + 0x2f:
    case GICD_TYPER:
    case GICD_IIDR:
        /* RO registers, ignore the write */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest write to RO register at offset "
                      HWADDR_FMT_plx "\n", __func__, offset);
        return true;
    default:
        return false;
    }
}

static bool gicd_writeq(GICv3State *s, hwaddr offset,
                        uint64_t value, MemTxAttrs attrs)
{
    /* Our only 64-bit registers are GICD_IROUTER<n> */
    int irq;

    switch (offset) {
    case GICD_IROUTER ... GICD_IROUTER + 0x1fdf:
        irq = (offset - GICD_IROUTER) / 8;
        gicd_write_irouter(s, attrs, irq, value);
        return true;
    default:
        return false;
    }
}

static bool gicd_readq(GICv3State *s, hwaddr offset,
                       uint64_t *data, MemTxAttrs attrs)
{
    /* Our only 64-bit registers are GICD_IROUTER<n> */
    int irq;

    switch (offset) {
    case GICD_IROUTER ... GICD_IROUTER + 0x1fdf:
        irq = (offset - GICD_IROUTER) / 8;
        *data = gicd_read_irouter(s, attrs, irq);
        return true;
    default:
        return false;
    }
}

MemTxResult gicv3_dist_read(void *opaque, hwaddr offset, uint64_t *data,
                            unsigned size, MemTxAttrs attrs)
{
    GICv3State *s = (GICv3State *)opaque;
    bool r;

    switch (size) {
    case 1:
        r = gicd_readb(s, offset, data, attrs);
        break;
    case 2:
        r = gicd_readw(s, offset, data, attrs);
        break;
    case 4:
        r = gicd_readl(s, offset, data, attrs);
        break;
    case 8:
        r = gicd_readq(s, offset, data, attrs);
        break;
    default:
        r = false;
        break;
    }

    if (!r) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest read at offset " HWADDR_FMT_plx
                      " size %u\n", __func__, offset, size);
        trace_gicv3_dist_badread(offset, size, attrs.secure);
        /* The spec requires that reserved registers are RAZ/WI;
         * so use MEMTX_ERROR returns from leaf functions as a way to
         * trigger the guest-error logging but don't return it to
         * the caller, or we'll cause a spurious guest data abort.
         */
        *data = 0;
    } else {
        trace_gicv3_dist_read(offset, *data, size, attrs.secure);
    }
    return MEMTX_OK;
}

MemTxResult gicv3_dist_write(void *opaque, hwaddr offset, uint64_t data,
                             unsigned size, MemTxAttrs attrs)
{
    GICv3State *s = (GICv3State *)opaque;
    bool r;

    switch (size) {
    case 1:
        r = gicd_writeb(s, offset, data, attrs);
        break;
    case 2:
        r = gicd_writew(s, offset, data, attrs);
        break;
    case 4:
        r = gicd_writel(s, offset, data, attrs);
        break;
    case 8:
        r = gicd_writeq(s, offset, data, attrs);
        break;
    default:
        r = false;
        break;
    }

    if (!r) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest write at offset " HWADDR_FMT_plx
                      " size %u\n", __func__, offset, size);
        trace_gicv3_dist_badwrite(offset, data, size, attrs.secure);
        /* The spec requires that reserved registers are RAZ/WI;
         * so use MEMTX_ERROR returns from leaf functions as a way to
         * trigger the guest-error logging but don't return it to
         * the caller, or we'll cause a spurious guest data abort.
         */
    } else {
        trace_gicv3_dist_write(offset, data, size, attrs.secure);
    }
    return MEMTX_OK;
}

void gicv3_dist_set_irq(GICv3State *s, int irq, int level)
{
    /* Update distributor state for a change in an external SPI input line */
    if (level == gicv3_gicd_level_test(s, irq)) {
        return;
    }

    trace_gicv3_dist_set_irq(irq, level);

    gicv3_gicd_level_replace(s, irq, level);

    if (level) {
        /* 0->1 edges latch the pending bit for edge-triggered interrupts */
        if (gicv3_gicd_edge_trigger_test(s, irq)) {
            gicv3_gicd_pending_set(s, irq);
        }
    }

    gicv3_update(s, irq, 1);
}
