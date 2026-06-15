/*
 * ARM GICv5 emulation: Interrupt Routing Service (IRS)
 *
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The IRS is defined in IHI 111701
 * (ARM Generic Interrupt Controller Architecture Specification,
 * GIC architecture version 5):
 * https://developer.arm.com/documentation/111701/latest
 */

#include "qemu/osdep.h"
#include "hw/core/registerfields.h"
#include "hw/intc/arm_gicv5.h"
#include "hw/intc/arm_gicv5_stream.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "trace.h"
#include "migration/blocker.h"

OBJECT_DEFINE_TYPE(GICv5, gicv5, ARM_GICV5, ARM_GICV5_COMMON)

static const char *domain_name[] = {
    [GICV5_ID_S] = "Secure",
    [GICV5_ID_NS] = "NonSecure",
    [GICV5_ID_EL3] = "EL3",
    [GICV5_ID_REALM] = "Realm",
};

static const char *inttype_name(GICv5IntType t)
{
    /*
     * We have to be more cautious with getting human readable names
     * for a GICv5IntType for trace strings than we do with the domain
     * enum, because here the value can come from a guest register
     * field.
     */
    static const char *names[] = {
        [GICV5_PPI] = "PPI",
        [GICV5_LPI] = "LPI",
        [GICV5_SPI] = "SPI",
    };
    if (t >= ARRAY_SIZE(names) || !names[t]) {
        return "RESERVED";
    }
    return names[t];
}

REG32(IRS_IDR0, 0x0)
    FIELD(IRS_IDR0, INT_DOM, 0, 2)
    FIELD(IRS_IDR0, PA_RANGE, 2, 4)
    FIELD(IRS_IDR0, VIRT, 6, 1)
    FIELD(IRS_IDR0, ONE_N, 7, 1)
    FIELD(IRS_IDR0, VIRT_ONE_N, 8, 1)
    FIELD(IRS_IDR0, SETLPI, 9, 1)
    FIELD(IRS_IDR0, MEC, 10, 1)
    FIELD(IRS_IDR0, MPAM, 11, 1)
    FIELD(IRS_IDR0, SWE, 12, 1)
    FIELD(IRS_IDR0, IRSID, 16, 16)

REG32(IRS_IDR1, 0x4)
    FIELD(IRS_IDR1, PE_CNT, 0, 16)
    FIELD(IRS_IDR1, IAFFID_BITS, 16, 4)
    FIELD(IRS_IDR1, PRI_BITS, 20, 3)

REG32(IRS_IDR2, 0x8)
    FIELD(IRS_IDR2, ID_BITS, 0, 5)
    FIELD(IRS_IDR2, LPI, 5, 1)
    FIELD(IRS_IDR2, MIN_LPI_ID_BITS, 6, 4)
    FIELD(IRS_IDR2, IST_LEVELS, 10, 1)
    FIELD(IRS_IDR2, IST_L2SZ, 11, 3)
    FIELD(IRS_IDR2, IST_MD, 14, 1)
    FIELD(IRS_IDR2, ISTMD_SZ, 15, 5)

REG32(IRS_IDR3, 0xc)
    FIELD(IRS_IDR3, VMD, 0, 1)
    FIELD(IRS_IDR3, VMD_SZ, 1, 4)
    FIELD(IRS_IDR3, VM_ID_BITS, 5, 5)
    FIELD(IRS_IDR3, VMT_LEVELS, 10, 1)

REG32(IRS_IDR4, 0x10)
    FIELD(IRS_IDR4, VPED_SZ, 0, 6)
    FIELD(IRS_IDR4, VPE_ID_BITS, 6, 4)

REG32(IRS_IDR5, 0x14)
    FIELD(IRS_IDR5, SPI_RANGE, 0, 25)

REG32(IRS_IDR6, 0x18)
    FIELD(IRS_IDR6, SPI_IRS_RANGE, 0, 25)

REG32(IRS_IDR7, 0x1c)
    FIELD(IRS_IDR7, SPI_BASE, 0, 24)

REG32(IRS_IIDR, 0x40)
    FIELD(IRS_IIDR, IMPLEMENTER, 0, 12)
    FIELD(IRS_IIDR, REVISION, 12, 4)
    FIELD(IRS_IIDR, VARIANT, 16, 4)
    FIELD(IRS_IIDR, PRODUCTID, 20, 12)

REG32(IRS_AIDR, 0x44)
    FIELD(IRS_AIDR, ARCHMINORREV, 0, 4)
    FIELD(IRS_AIDR, ARCHMAJORREV, 4, 4)
    FIELD(IRS_AIDR, COMPONENT, 8, 4)

REG32(IRS_CR0, 0x80)
    FIELD(IRS_CR0, IRSEN, 0, 1)
    FIELD(IRS_CR0, IDLE, 1, 1)

REG32(IRS_CR1, 0x84)
    FIELD(IRS_CR1, SH, 0, 2)
    FIELD(IRS_CR1, OC, 2, 2)
    FIELD(IRS_CR1, IC, 4, 2)
    FIELD(IRS_CR1, IST_RA, 6, 1)
    FIELD(IRS_CR1, IST_WA, 7, 1)
    FIELD(IRS_CR1, VMT_RA, 8, 1)
    FIELD(IRS_CR1, VMT_WA, 9, 1)
    FIELD(IRS_CR1, VPET_RA, 10, 1)
    FIELD(IRS_CR1, VPET_WA, 11, 1)
    FIELD(IRS_CR1, VMD_RA, 12, 1)
    FIELD(IRS_CR1, VMD_WA, 13, 1)
    FIELD(IRS_CR1, VPED_RA, 14, 1)
    FIELD(IRS_CR1, VPED_WA, 15, 1)

REG32(IRS_SYNCR, 0xc0)
    FIELD(IRS_SYNCR, SYNC, 31, 1)

REG32(IRS_SYNC_STATUSR, 0xc4)
    FIELD(IRS_SYNC_STATUSR, IDLE, 0, 1)

REG64(IRS_SPI_VMR, 0x100)
    FIELD(IRS_SPI_VMR, VM_ID, 0, 16)
    FIELD(IRS_SPI_VMR, VIRT, 63, 1)

REG32(IRS_SPI_SELR, 0x108)
    FIELD(IRS_SPI_SELR, ID, 0, 24)

REG32(IRS_SPI_DOMAINR, 0x10c)
    FIELD(IRS_SPI_DOMAINR, DOMAIN, 0, 2)

REG32(IRS_SPI_RESAMPLER, 0x110)
    FIELD(IRS_SPI_RESAMPLER, SPI_ID, 0, 24)

REG32(IRS_SPI_CFGR, 0x114)
    FIELD(IRS_SPI_CFGR, TM, 0, 1)

REG32(IRS_SPI_STATUSR, 0x118)
    FIELD(IRS_SPI_STATUSR, IDLE, 0, 1)
    FIELD(IRS_SPI_STATUSR, V, 1, 1)

REG32(IRS_PE_SELR, 0x140)
    FIELD(IRS_PE_SELR, IAFFID, 0, 16)

REG32(IRS_PE_STATUSR, 0x144)
    FIELD(IRS_PE_STATUSR, IDLE, 0, 1)
    FIELD(IRS_PE_STATUSR, V, 1, 1)
    FIELD(IRS_PE_STATUSR, ONLINE, 2, 1)

REG32(IRS_PE_CR0, 0x148)
    FIELD(IRS_PE_CR0, DPS, 0, 1)

REG64(IRS_IST_BASER, 0x180)
    FIELD(IRS_IST_BASER, VALID, 0, 1)
    FIELD(IRS_IST_BASER, ADDR, 6, 50)

REG32(IRS_IST_CFGR, 0x190)
    FIELD(IRS_IST_CFGR, LPI_ID_BITS, 0, 5)
    FIELD(IRS_IST_CFGR, L2SZ, 5, 2)
    FIELD(IRS_IST_CFGR, ISTSZ, 7, 2)
    FIELD(IRS_IST_CFGR, STRUCTURE, 16, 1)

REG32(IRS_IST_STATUSR, 0x194)
    FIELD(IRS_IST_STATUSR, IDLE, 0, 1)

REG32(IRS_MAP_L2_ISTR, 0x1c0)
    FIELD(IRS_MAP_L2_ISTR, ID, 0, 24)

REG64(IRS_VMT_BASER, 0x200)
    FIELD(IRS_VMT_BASER, VALID, 0, 1)
    FIELD(IRS_VMT_BASER, ADDR, 3, 53)

REG32(IRS_VMT_CFGR, 0x210)
    FIELD(IRS_VMT_CFGR, VM_ID_BITS, 0, 5)
    FIELD(IRS_VMT_CFGR, STRUCTURE, 16, 1)

REG32(IRS_VMT_STATUSR, 0x124)
    FIELD(IRS_VMT_STATUSR, IDLE, 0, 1)

REG64(IRS_VPE_SELR, 0x240)
    FIELD(IRS_VPE_SELR, VM_ID, 0, 16)
    FIELD(IRS_VPE_SELR, VPE_ID, 32, 16)
    FIELD(IRS_VPE_SELR, S, 63, 1)

REG64(IRS_VPE_DBR, 0x248)
    FIELD(IRS_VPE_DBR, INTID, 0, 24)
    FIELD(IRS_VPE_DBR, DBPM, 32, 5)
    FIELD(IRS_VPE_DBR, REQ_DB, 62, 1)
    FIELD(IRS_VPE_DBR, DBV, 63, 1)

REG32(IRS_VPE_HPPIR, 0x250)
    FIELD(IRS_VPE_HPPIR, ID, 0, 24)
    FIELD(IRS_VPE_HPPIR, TYPE, 29, 3)
    FIELD(IRS_VPE_HPPIR, HPPIV, 32, 1)

REG32(IRS_VPE_CR0, 0x258)
    FIELD(IRS_VPE_CR0, DPS, 0, 1)

REG32(IRS_VPE_STATUSR, 0x25c)
    FIELD(IRS_VPE_STATUSR, IDLE, 0, 1)
    FIELD(IRS_VPE_STATUSR, V, 1, 1)

REG64(IRS_VM_DBR, 0x280)
    FIELD(IRS_VM_DBR, VPE_ID, 0, 16)
    FIELD(IRS_VM_DBR, EN, 63, 1)

REG32(IRS_VM_SELR, 0x288)
    FIELD(IRS_VM_SELR, VM_ID, 0, 16)

REG32(IRS_VM_STATUSR, 0x28c)
    FIELD(IRS_VM_STATUSR, IDLE, 0, 1)
    FIELD(IRS_VM_STATUSR, V, 1, 1)

REG64(IRS_VMAP_L2_VMTR, 0x2c0)
    FIELD(IRS_VMAP_L2_VMTR, VM_ID, 0, 16)
    FIELD(IRS_VMAP_L2_VMTR, M, 63, 1)

REG64(IRS_VMAP_VMR, 0x2c8)
    FIELD(IRS_VMAP_VMR, VM_ID, 0, 16)
    FIELD(IRS_VMAP_VMR, U, 62, 1)
    FIELD(IRS_VMAP_VMR, M, 63, 1)

REG64(IRS_VMAP_VISTR, 0x2d0)
    FIELD(IRS_VMAP_VISTR, TYPE, 29, 3)
    FIELD(IRS_VMAP_VISTR, VM_ID, 32, 16)
    FIELD(IRS_VMAP_VISTR, U, 62, 1)
    FIELD(IRS_VMAP_VISTR, M, 63, 1)

REG64(IRS_VMAP_L2_VISTR, 0x2d8)
    FIELD(IRS_VMAP_L2_VISTR, ID, 0, 24)
    FIELD(IRS_VMAP_L2_VISTR, TYPE, 29, 3)
    FIELD(IRS_VMAP_L2_VISTR, VM_ID, 32, 16)
    FIELD(IRS_VMAP_L2_VISTR, M, 63, 1)

REG64(IRS_VMAP_VPER, 0x2e0)
    FIELD(IRS_VMAP_VPER, VPE_ID, 0, 16)
    FIELD(IRS_VMAP_VPER, VM_ID, 32, 16)
    FIELD(IRS_VMAP_VPER, M, 63, 1)

REG64(IRS_SAVE_VMR, 0x300)
    FIELD(IRS_SAVE_VMR, VM_ID, 0, 16)
    FIELD(IRS_SAVE_VMR, Q, 62, 1)
    FIELD(IRS_SAVE_VMR, S, 63, 1)

REG32(IRS_SAVE_VM_STATUSR, 0x308)
    FIELD(IRS_SAVE_VM_STATUSR, IDLE, 0, 1)
    FIELD(IRS_SAVE_VM_STATUSR, Q, 1, 1)

REG32(IRS_MEC_IDR, 0x340)
    FIELD(IRS_MEC_IDR, MECIDSIZE, 0, 4)

REG32(IRS_MEC_MECID_R, 0x344)
    FIELD(IRS_MEC_MICID_R, MECID, 0, 16)

REG32(IRS_MPAM_IDR, 0x380)
    FIELD(IRS_MPAM_IDR, PARTID_MAX, 0, 16)
    FIELD(IRS_MPAM_IDR, PMG_MAX, 16, 8)
    FIELD(IRS_MPAM_IDR, HAS_MPAM_SP, 24, 1)

REG32(IRS_MPAM_PARTID_R, 0x384)
    FIELD(IRS_MPAM_IDR, PARTID, 0, 16)
    FIELD(IRS_MPAM_IDR, PMG, 16, 8)
    FIELD(IRS_MPAM_IDR, MPAM_SP, 24, 2)
    FIELD(IRS_MPAM_IDR, IDLE, 31, 1)

REG64(IRS_SWERR_STATUSR, 0x3c0)
    FIELD(IRS_SWERR_STATUSR, V, 0, 1)
    FIELD(IRS_SWERR_STATUSR, S0V, 1, 1)
    FIELD(IRS_SWERR_STATUSR, S1V, 2, 1)
    FIELD(IRS_SWERR_STATUSR, OF, 3, 1)
    FIELD(IRS_SWERR_STATUSR, EC, 16, 8)
    FIELD(IRS_SWERR_STATUSR, IMP_EC, 24, 8)

REG64(IRS_SWERR_SYNDROMER0, 0x3c8)
    FIELD(IRS_SWERR_SYNDROMER0, VM_ID, 0, 16)
    FIELD(IRS_SWERR_SYNDROMER0, ID, 32, 24)
    FIELD(IRS_SWERR_SYNDROMER0, TYPE, 60, 3)
    FIELD(IRS_SWERR_SYNDROMER0, VIRTUAL, 63, 1)

REG64(IRS_SWERR_SYNDROMER1, 0x3d0)
    FIELD(IRS_SWERR_SYNDROMER2, ADDR, 3, 53)

REG32(IRS_IDREGS, 0xffd0)
REG32(IRS_DEVARCH, 0xffbc)

FIELD(L1_ISTE, VALID, 0, 1)
FIELD(L1_ISTE, L2_ADDR, 12, 44)

FIELD(L2_ISTE, PENDING, 0, 1)
FIELD(L2_ISTE, ACTIVE, 1, 1)
FIELD(L2_ISTE, HM, 2, 1)
FIELD(L2_ISTE, ENABLE, 3, 1)
FIELD(L2_ISTE, IRM, 4, 1)
FIELD(L2_ISTE, HWU, 9, 2)
FIELD(L2_ISTE, PRIORITY, 11, 5)
FIELD(L2_ISTE, IAFFID, 16, 16)

/*
 * Format used for gicv5_request_config() return value, which matches
 * the ICC_ICSR_EL1 bit layout.
 */
FIELD(ICSR, F, 0, 1)
FIELD(ICSR, ENABLED, 1, 1)
FIELD(ICSR, PENDING, 2, 1)
FIELD(ICSR, IRM, 3, 1)
FIELD(ICSR, ACTIVE, 4, 1)
FIELD(ICSR, HM, 5, 1)
FIELD(ICSR, PRIORITY, 11, 5)
FIELD(ICSR, IAFFID, 32, 16)

#define IRS_DEVARCH_VALUE ((0x23b << 31) | (0x1 << 20) | 0x5a19)

static uint32_t gicv5_idreg(int regoffset)
{
    /*
     * As with the main IRS_IIDR, we don't identify as a specific
     * hardware GICv5 implementation. Arm suggests that the
     * Implementer, Product, etc in IRS_IIDR should also be reported
     * here, so we do that.
     */
    static const uint8_t gic_ids[] = {
        QEMU_GICV5_IMPLEMENTER >> 8, 0x00, 0x00, 0x00, /* PIDR4..PIDR7 */
        QEMU_GICV5_PRODUCTID & 0xff, /* PIDR0 */
        ((QEMU_GICV5_PRODUCTID >> 8) |
         ((QEMU_GICV5_IMPLEMENTER & 0xf) << 4)), /* PIDR1 */
        ((QEMU_GICV5_REVISION << 4) | (1 << 3) |
         ((QEMU_GICV5_IMPLEMENTER & 0x70) >> 4)), /* PIDR2 */
        QEMU_GICV5_VARIANT << 4, /* PIDR3 */
        0x0D, 0xF0, 0x05, 0xB1, /* CIDR0..CIDR3 */
    };

    regoffset /= 4;
    return gic_ids[regoffset];
}

static GICv5SPIState *spi_for_selr(GICv5Common *cs, GICv5Domain domain)
{
    /*
     * If the IRS_SPI_SELR value specifies an SPI that can be managed in
     * this domain, return a pointer to its GICv5SPIState; otherwise
     * return NULL.
     */
    uint32_t id = FIELD_EX32(cs->irs_spi_selr[domain], IRS_SPI_SELR, ID);
    GICv5SPIState *spi = gicv5_raw_spi_state(cs, id);

    if (spi && (domain == GICV5_ID_EL3 || domain == spi->domain)) {
        return spi;
    }
    return NULL;
}

static MemTxAttrs irs_txattrs(GICv5Common *cs, GICv5Domain domain)
{
    /*
     * Return a MemTxAttrs to use for IRS memory accesses.  IRS_CR1
     * has the usual Arm cacheability/shareability attributes, but
     * QEMU doesn't care about those. All we need to specify here is
     * the correct security attributes, which depend on the interrupt
     * domain. Conveniently, our GICv5Domain encoding matches the
     * ARMSecuritySpace one (because both follow an architecturally
     * specified field). The exception is that the EL3 domain must be
     * Secure instead of Root if we don't implement Realm.
     */
    if (domain == GICV5_ID_EL3 &&
        !gicv5_domain_implemented(cs, GICV5_ID_REALM)) {
        domain = GICV5_ID_S;
    }
    return (MemTxAttrs) {
        .space = domain,
        .secure = domain == GICV5_ID_S || domain == GICV5_ID_EL3,
    };
}

/* Data we need to pass through to lpi_cache_get_hppi() */
typedef struct GetHPPIUserData {
    GICv5PendingIrq *best;
    uint32_t iaffid;
} GetHPPIUserData;

static void lpi_cache_get_hppi(gpointer key, gpointer value, gpointer user_data)
{
    uint64_t id = GPOINTER_TO_INT(key);
    uint64_t l2_iste = *(uint64_t *)value;
    uint32_t prio, iaffid;
    GetHPPIUserData *ud = user_data;

    if ((l2_iste & (R_L2_ISTE_PENDING_MASK | R_L2_ISTE_ACTIVE_MASK | R_L2_ISTE_ENABLE_MASK))
        != (R_L2_ISTE_PENDING_MASK | R_L2_ISTE_ENABLE_MASK)) {
        return;
    }
    prio = FIELD_EX32(l2_iste, L2_ISTE, PRIORITY);
    iaffid = FIELD_EX32(l2_iste, L2_ISTE, IAFFID);
    if (iaffid == ud->iaffid && prio < ud->best->prio) {
        id = FIELD_DP32(id, INTID, TYPE, GICV5_LPI);
        ud->best->intid = id;
        ud->best->prio = prio;
    }
}

static int irs_cpuidx_from_iaffid(GICv5Common *cs, uint32_t iaffid)
{
    for (int i = 0; i < cs->num_cpus; i++) {
        if (cs->cpu_iaffids[i] == iaffid) {
            return i;
        }
    }
    return -1;
}

static void irs_recalc_hppi(GICv5 *s, GICv5Domain domain, uint32_t iaffid)
{
    /*
     * Recalculate the highest priority pending interrupt for the
     * specified domain and cpuif.  HPPI candidates must be pending,
     * inactive and enabled.
     */
    GICv5Common *cs = ARM_GICV5_COMMON(s);
    int cpuidx = irs_cpuidx_from_iaffid(cs, iaffid);
    ARMCPU *cpu = cpuidx >= 0 ? cs->cpus[cpuidx] : NULL;
    GICv5PendingIrq best;

    best.intid = 0;
    best.prio = PRIO_IDLE;

    if (!cpu) {
        /* Nothing happens for iaffids targeting nonexistent CPUs */
        trace_gicv5_irs_recalc_hppi_fail(domain_name[domain], iaffid,
                                         "IAFFID doesn't match any CPU");
        return;
    }

    if (!FIELD_EX32(cs->irs_cr0[domain], IRS_CR0, IRSEN)) {
        /* When the IRS is disabled we don't forward HPPIs */
        trace_gicv5_irs_recalc_hppi_fail(domain_name[domain], iaffid,
                                         "IRS_CR0.IRSEN is zero");
        return;
    }

    if (s->phys_lpi_config[domain].valid) {
        GetHPPIUserData ud;

        ud.best = &best;
        ud.iaffid = iaffid;
        g_hash_table_foreach(s->phys_lpi_config[domain].lpi_cache,
                             lpi_cache_get_hppi, &ud);
    }

    /*
     * OPT: consider also caching the SPI interrupt information,
     * similarly to how we handle LPIs, if iterating through the whole
     * SPI array every time is too expensive.
     */
    for (int i = 0; i < cs->spi_irs_range; i++) {
        GICv5SPIState *spi = &cs->spi[i];

        if (spi->active || !spi->pending || !spi->enabled) {
            continue;
        }
        if (spi->domain != domain || spi->iaffid != iaffid) {
            continue;
        }
        if (spi->priority < best.prio) {
            uint32_t intid = 0;
            intid = FIELD_DP32(intid, INTID, ID, i);
            intid = FIELD_DP32(intid, INTID, TYPE, GICV5_SPI);
            best.intid = intid;
            best.prio = spi->priority;
        }
    }

    trace_gicv5_irs_recalc_hppi(domain_name[domain], iaffid,
                                best.intid, best.prio);

    s->hppi[domain][cpuidx] = best;
    /*
     * Now present the HPPI to the cpuif. In the real hardware stream
     * protocol, the connection between IRS and cpuif is asynchronous,
     * and so both ends track their idea of the current HPPI, with a
     * back-and-forth sequence so they stay in sync and more
     * interaction when the cpuif resets.  For QEMU, we are strictly
     * synchronous and the cpuif asking the IRS for data is a cheap
     * function call, so we simplify this:
     *  - the IRS knows what the current HPPI is
     *  - s->hppi[][] is a cache we can recalculate
     *  - the IRS merely tells the cpuif "something changed", and
     *    the cpuif asks for the current HPPI when it needs it
     *  - the cpuif does not cache the HPPI on its end
     */
    gicv5_forward_interrupt(cpu, domain);
}

static void irs_recalc_hppi_all_cpus(GICv5 *s, GICv5Domain domain)
{
    /*
     * Recalculate the HPPI for every CPU for this domain.  This is
     * not as efficient as it could be because we will scan through
     * the LPI cached hash table and the SPI array for each CPU rather
     * than doing a single combined scan, but we only need to do this
     * very rarely, when the guest enables or disables the IST, so we
     * implement this the simple way.
     */
    GICv5Common *cs = ARM_GICV5_COMMON(s);
    for (int i = 0; i < cs->num_cpus; i++) {
        irs_recalc_hppi(s, domain, cs->cpu_iaffids[i]);
    }
}

static void irs_recall_hppis(GICv5 *s, GICv5Domain domain)
{
    /*
     * The IRS was just disabled -- we must recall any pending HPPIs
     * we have sent to the CPU interfaces. For us this means that we
     * clear our cached HPPI data and tell the cpuif that it has
     * changed.
     */
    GICv5Common *cs = ARM_GICV5_COMMON(s);

    for (int i = 0; i < cs->num_cpus; i++) {
        s->hppi[domain][i].intid = 0;
        s->hppi[domain][i].prio = PRIO_IDLE;
        gicv5_forward_interrupt(cs->cpus[i], domain);
    }
}

GICv5PendingIrq gicv5_get_hppi(GICv5Common *cs, GICv5Domain domain,
                               uint32_t iaffid)
{
    GICv5 *s = ARM_GICV5(cs);
    int cpuidx = irs_cpuidx_from_iaffid(cs, iaffid);

    assert(cpuidx >= 0);
    return s->hppi[domain][cpuidx];
}

static hwaddr l1_iste_addr(GICv5Common *cs, const GICv5ISTConfig *cfg,
                           uint32_t id)
{
    /*
     * In a 2-level IST configuration, return the address of the L1
     * IST entry for this interrupt ID.  The bottom l2_idx_bits of the
     * ID value are the index into the L2 table, and the higher bits
     * of the ID index the L1 table.
     */
    uint32_t l1_index = id >> cfg->l2_idx_bits;
    return cfg->base + (l1_index * 8);
}

static bool get_l2_iste_addr(GICv5Common *cs, const GICv5ISTConfig *cfg,
                             uint32_t id, hwaddr *l2_iste_addr)
{
    /*
     * Get the address of the L2 interrupt state table entry for this
     * interrupt. On success, fill in l2_iste_addr and return true.
     * On failure, return false.
     */
    hwaddr l2_base;

    if (!cfg->valid) {
        return false;
    }

    if (id >= (1 << cfg->id_bits)) {
        return false;
    }

    if (cfg->structure) {
        /*
         * 2-level table: read the L1 IST. The bottom l2_idx_bits of
         * the ID value are the index into the L2 table, and the
         * higher bits of the ID index the L1 table. There is always
         * at least one L1 table entry.
         */
        hwaddr l1_addr = l1_iste_addr(cs, cfg, id);
        uint64_t l1_iste;
        MemTxResult res;

        l1_iste = address_space_ldq_le(&cs->dma_as, l1_addr,
                                       cfg->txattrs, &res);
        if (res != MEMTX_OK) {
            /* Reportable with EC=0x01 if sw error reporting implemented */
            qemu_log_mask(LOG_GUEST_ERROR, "L1 ISTE lookup failed for ID 0x%x"
                          " at physical address 0x" HWADDR_FMT_plx "\n",
                          id, l1_addr);
            return false;
        }
        if (!FIELD_EX64(l1_iste, L1_ISTE, VALID)) {
            return false;
        }
        l2_base = l1_iste & R_L1_ISTE_L2_ADDR_MASK;
        id = extract32(id, 0, cfg->l2_idx_bits);
    } else {
        /* 1-level table */
        l2_base = cfg->base;
    }

    *l2_iste_addr = l2_base + (id * cfg->istsz);
    return true;
}

static bool read_l2_iste_mem(GICv5Common *cs, const GICv5ISTConfig *cfg,
                             hwaddr addr, uint32_t *l2_iste)
{
    MemTxResult res;

    *l2_iste = address_space_ldl_le(&cs->dma_as, addr, cfg->txattrs, &res);
    if (res != MEMTX_OK) {
        /* Reportable with EC=0x02 if sw error reporting implemented */
        qemu_log_mask(LOG_GUEST_ERROR, "L2 ISTE read failed at physical "
                      "address 0x" HWADDR_FMT_plx "\n", addr);
    }
    return res == MEMTX_OK;
}

static bool write_l2_iste_mem(GICv5Common *cs, const GICv5ISTConfig *cfg,
                              hwaddr addr, uint32_t l2_iste)
{
    MemTxResult res;

    address_space_stl_le(&cs->dma_as, addr, l2_iste, cfg->txattrs, &res);
    if (res != MEMTX_OK) {
        /* Reportable with EC=0x02 if sw error reporting implemented */
        qemu_log_mask(LOG_GUEST_ERROR, "L2 ISTE write failed at physical "
                      "address 0x" HWADDR_FMT_plx "\n", addr);
    }
    return res == MEMTX_OK;
}

/*
 * This is returned by get_l2_iste() and has everything we need to do
 * the writeback of the L2 ISTE word in put_l2_iste().  Not all these
 * fields are always valid; they are private to the implementation of
 * get_l2_iste() and put_l2_iste().
 */
typedef struct L2_ISTE_Handle {
    /* Guest memory address of the L2 ISTE; valid only if !hashed */
    hwaddr l2_iste_addr;
    union {
        /* Actual L2_ISTE word; valid only if !hashed */
        uint32_t l2_iste;
        /* Pointer to L2 ISTE word; valid only if hashed */
        uint32_t *l2_iste_p;
    };
    uint32_t id;
    /* True if this ISTE is currently in the cache */
    bool hashed;
} L2_ISTE_Handle;

static uint32_t *get_l2_iste(GICv5Common *cs, const GICv5ISTConfig *cfg,
                             uint32_t id, L2_ISTE_Handle *h)
{
    /*
     * Find the L2 ISTE for the interrupt @id.
     *
     * We return a pointer to the ISTE: the caller can freely read and
     * modify the uint64_t pointed to to update the ISTE.  If the
     * caller modifies the L2 ISTE word, it must call put_l2_iste(),
     * passing it @h, to write back the ISTE.  If the caller is only
     * reading the L2 ISTE, it does not need to call put_l2_iste().
     *
     * We fill in @h with information needed for put_l2_iste().
     *
     * If the ISTE could not be read (typically because of a memory
     * error), return NULL.
     */
    uint32_t *hashvalue;

    if (!cfg->valid) {
        /* Catch invalid config early, it has no lpi_cache */
        return NULL;
    }

    hashvalue = g_hash_table_lookup(cfg->lpi_cache,
                                    GINT_TO_POINTER(id));

    h->id = id;

    if (hashvalue) {
        h->hashed = true;
        h->l2_iste_p = hashvalue;
        return hashvalue;
    }

    h->hashed = false;
    if (!get_l2_iste_addr(cs, cfg, id, &h->l2_iste_addr) ||
        !read_l2_iste_mem(cs, cfg, h->l2_iste_addr, &h->l2_iste)) {
        return NULL;
    }
    return &h->l2_iste;
}

static void put_l2_iste(GICv5Common *cs, const GICv5ISTConfig *cfg,
                        L2_ISTE_Handle *h)
{
    /*
     * Write back the modified L2_ISTE word found with get_l2_iste().
     * Once this has been called the L2_ISTE_Handle @h and the pointer
     * to the L2 ISTE word are no longer valid.
     */
    if (h->hashed) {
        uint32_t l2_iste = *h->l2_iste_p;
        if (!FIELD_EX32(l2_iste, L2_ISTE, PENDING)) {
            /*
             * We just made this not pending: remove from hash table
             * and write back to memory.
             */
            hwaddr l2_iste_addr;

            g_hash_table_remove(cfg->lpi_cache, GINT_TO_POINTER(h->id));
            if (get_l2_iste_addr(cs, cfg, h->id, &l2_iste_addr)) {
                write_l2_iste_mem(cs, cfg, l2_iste_addr, l2_iste);
                /* Writeback errors are ignored. */
            }
        }
        return;
    }

    if (FIELD_EX32(h->l2_iste, L2_ISTE, PENDING)) {
        /*
         * We just made this pending: add it to the hash table, and
         * don't bother writing it back to memory.
         */
        uint32_t *hashvalue = g_new(uint32_t, 1);
        *hashvalue = h->l2_iste;
        g_hash_table_insert(cfg->lpi_cache, GINT_TO_POINTER(h->id), hashvalue);
        return;
    }
    write_l2_iste_mem(cs, cfg, h->l2_iste_addr, h->l2_iste);
}

void gicv5_set_priority(GICv5Common *cs, uint32_t id, uint8_t priority,
                        GICv5Domain domain, GICv5IntType type, bool virtual)
{
    GICv5 *s = ARM_GICV5(cs);
    uint32_t iaffid;

    trace_gicv5_set_priority(domain_name[domain], inttype_name(type), virtual,
                             id, priority);
    /* We must ignore unimplemented low-order priority bits */
    priority &= MAKE_64BIT_MASK(5 - QEMU_GICV5_PRI_BITS, QEMU_GICV5_PRI_BITS);

    if (virtual) {
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_priority: tried to set "
                      "priority of a virtual interrupt\n");
        return;
    }

    switch (type) {
    case GICV5_LPI:
    {
        const GICv5ISTConfig *cfg = &s->phys_lpi_config[domain];
        L2_ISTE_Handle h;
        uint32_t *l2_iste_p = get_l2_iste(cs, cfg, id, &h);

        if (!l2_iste_p) {
            return;
        }
        *l2_iste_p = FIELD_DP32(*l2_iste_p, L2_ISTE, PRIORITY, priority);
        iaffid = FIELD_EX32(*l2_iste_p, L2_ISTE, IAFFID);
        put_l2_iste(cs, cfg, &h);
        break;
    }
    case GICV5_SPI:
    {
        GICv5SPIState *spi = gicv5_spi_state(cs, id, domain);

        if (!spi) {
            qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_priority: tried to set "
                          "priority of unreachable SPI %d\n", id);
            return;
        }

        spi->priority = priority;
        iaffid = spi->iaffid;
        break;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_priority: tried to set "
                      "priority of bad interrupt type %d\n", type);
        return;
    }

    irs_recalc_hppi(s, domain, iaffid);
}

void gicv5_set_enabled(GICv5Common *cs, uint32_t id, bool enabled,
                       GICv5Domain domain, GICv5IntType type, bool virtual)
{
    GICv5 *s = ARM_GICV5(cs);
    uint32_t iaffid;

    trace_gicv5_set_enabled(domain_name[domain], inttype_name(type), virtual,
                            id, enabled);
    if (virtual) {
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_enabled: tried to set "
                      "enable state of a virtual interrupt\n");
        return;
    }

    switch (type) {
    case GICV5_LPI:
    {
        const GICv5ISTConfig *cfg = &s->phys_lpi_config[domain];
        L2_ISTE_Handle h;
        uint32_t *l2_iste_p = get_l2_iste(cs, cfg, id, &h);

        if (!l2_iste_p) {
            return;
        }
        *l2_iste_p = FIELD_DP32(*l2_iste_p, L2_ISTE, ENABLE, enabled);
        iaffid = FIELD_EX32(*l2_iste_p, L2_ISTE, IAFFID);
        put_l2_iste(cs, cfg, &h);
        break;
    }
    case GICV5_SPI:
    {
        GICv5SPIState *spi = gicv5_spi_state(cs, id, domain);

        if (!spi) {
            qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_enabled: tried to set "
                          "enable state of unreachable SPI %d\n", id);
            return;
        }

        spi->enabled = true;
        iaffid = spi->iaffid;
        break;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_enabled: tried to set "
                      "enable state of bad interrupt type %d\n", type);
        return;
    }

    irs_recalc_hppi(s, domain, iaffid);
}

void gicv5_set_pending(GICv5Common *cs, uint32_t id, bool pending,
                       GICv5Domain domain, GICv5IntType type, bool virtual)
{
    GICv5 *s = ARM_GICV5(cs);
    uint32_t iaffid;

    trace_gicv5_set_pending(domain_name[domain], inttype_name(type), virtual,
                            id, pending);
    if (virtual) {
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_pending: tried to set "
                      "pending state of a virtual interrupt\n");
        return;
    }

    switch (type) {
    case GICV5_LPI:
    {
        const GICv5ISTConfig *cfg = &s->phys_lpi_config[domain];
        L2_ISTE_Handle h;
        uint32_t *l2_iste_p = get_l2_iste(cs, cfg, id, &h);

        if (!l2_iste_p) {
            return;
        }
        *l2_iste_p = FIELD_DP32(*l2_iste_p, L2_ISTE, PENDING, pending);
        iaffid = FIELD_EX32(*l2_iste_p, L2_ISTE, IAFFID);
        put_l2_iste(cs, cfg, &h);
        break;
    }
    case GICV5_SPI:
    {
        GICv5SPIState *spi = gicv5_spi_state(cs, id, domain);

        if (!spi) {
            qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_pending: tried to set "
                          "pending state of unreachable SPI %d\n", id);
            return;
        }

        spi->pending = true;
        iaffid = spi->iaffid;
        break;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_pending: tried to set "
                      "pending state of bad interrupt type %d\n", type);
        return;
    }

    irs_recalc_hppi(s, domain, iaffid);
}

void gicv5_set_handling(GICv5Common *cs, uint32_t id,
                        GICv5HandlingMode handling, GICv5Domain domain,
                        GICv5IntType type, bool virtual)
{
    GICv5 *s = ARM_GICV5(cs);

    trace_gicv5_set_handling(domain_name[domain], inttype_name(type), virtual,
                            id, handling);
    if (virtual) {
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_handling: tried to set "
                      "handling mode of a virtual interrupt\n");
        return;
    }

    switch (type) {
    case GICV5_LPI:
    {
        const GICv5ISTConfig *cfg = &s->phys_lpi_config[domain];
        L2_ISTE_Handle h;
        uint32_t *l2_iste_p = get_l2_iste(cs, cfg, id, &h);

        if (!l2_iste_p) {
            return;
        }
        *l2_iste_p = FIELD_DP32(*l2_iste_p, L2_ISTE, HM, handling);
        put_l2_iste(cs, cfg, &h);
        break;
    }
    case GICV5_SPI:
    {
        GICv5SPIState *spi = gicv5_spi_state(cs, id, domain);

        if (!spi) {
            qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_handling: tried to set "
                          "priority of unreachable SPI %d\n", id);
            return;
        }

        spi->hm = handling;
        break;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_handling: tried to set "
                      "handling mode of bad interrupt type %d\n", type);
        return;
    }
}

void gicv5_set_target(GICv5Common *cs, uint32_t id, uint32_t iaffid,
                      GICv5RoutingMode irm, GICv5Domain domain,
                      GICv5IntType type, bool virtual)
{
    GICv5 *s = ARM_GICV5(cs);
    uint32_t old_iaffid;

    trace_gicv5_set_target(domain_name[domain], inttype_name(type), virtual,
                           id, iaffid, irm);
    if (virtual) {
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_target: tried to set "
                      "target of a virtual interrupt\n");
        return;
    }
    if (irm != GICV5_TARGETED) {
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_target: tried to set "
                      "1-of-N routing\n");
        /*
         * In the cpuif insn "GIC CDAFF", IRM is RES0 for a GIC which
         * does not support 1-of-N routing. So warn, and fall through
         * to treat IRM=1 the same as IRM=0.
         */
    }

    switch (type) {
    case GICV5_LPI:
    {
        const GICv5ISTConfig *cfg = &s->phys_lpi_config[domain];
        L2_ISTE_Handle h;
        uint32_t *l2_iste_p = get_l2_iste(cs, cfg, id, &h);

        if (!l2_iste_p) {
            return;
        }
        /*
         * For QEMU we do not implement 1-of-N routing, and so
         * L2_ISTE.IRM is RES0.  We never read it, and we can skip
         * explicitly writing it to zero here.
         */
        old_iaffid = FIELD_EX32(*l2_iste_p, L2_ISTE, IAFFID);
        *l2_iste_p = FIELD_DP32(*l2_iste_p, L2_ISTE, IAFFID, iaffid);
        put_l2_iste(cs, cfg, &h);
        break;
    }
    case GICV5_SPI:
    {
        GICv5SPIState *spi = gicv5_spi_state(cs, id, domain);

        if (!spi) {
            qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_target: tried to set "
                          "target of unreachable SPI %d\n", id);
            return;
        }

        old_iaffid = spi->iaffid;
        spi->iaffid = iaffid;
        break;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_set_target: tried to set "
                      "target of bad interrupt type %d\n", type);
        return;
    }

    irs_recalc_hppi(s, domain, old_iaffid);
    irs_recalc_hppi(s, domain, iaffid);
}

static uint64_t l2_iste_to_icsr(GICv5Common *cs, const GICv5ISTConfig *cfg,
                                uint32_t id)
{
    uint64_t icsr = 0;
    const uint32_t *l2_iste_p;
    L2_ISTE_Handle h;

    l2_iste_p = get_l2_iste(cs, cfg, id, &h);
    if (!l2_iste_p) {
        return R_ICSR_F_MASK;
    }

    /*
     * The field locations in the L2 ISTE do not line up with the
     * corresponding fields in the ICC_ICSR_EL1 register, so we need
     * to extract and deposit them individually.
     */
    icsr = FIELD_DP64(icsr, ICSR, F, 0);
    icsr = FIELD_DP64(icsr, ICSR, ENABLED, FIELD_EX32(*l2_iste_p, L2_ISTE, ENABLE));
    icsr = FIELD_DP64(icsr, ICSR, PENDING, FIELD_EX32(*l2_iste_p, L2_ISTE, PENDING));
    icsr = FIELD_DP64(icsr, ICSR, IRM, FIELD_EX32(*l2_iste_p, L2_ISTE, IRM));
    icsr = FIELD_DP64(icsr, ICSR, ACTIVE, FIELD_EX32(*l2_iste_p, L2_ISTE, ACTIVE));
    icsr = FIELD_DP64(icsr, ICSR, HM, FIELD_EX32(*l2_iste_p, L2_ISTE, HM));
    icsr = FIELD_DP64(icsr, ICSR, PRIORITY, FIELD_EX32(*l2_iste_p, L2_ISTE, PRIORITY));
    icsr = FIELD_DP64(icsr, ICSR, IAFFID, FIELD_EX32(*l2_iste_p, L2_ISTE, IAFFID));

    return icsr;
}

static uint64_t spi_state_to_icsr(GICv5SPIState *spi)
{
    uint64_t icsr = 0;

    icsr = FIELD_DP64(icsr, ICSR, F, 0);
    icsr = FIELD_DP64(icsr, ICSR, ENABLED, spi->enabled);
    icsr = FIELD_DP64(icsr, ICSR, PENDING, spi->pending);
    icsr = FIELD_DP64(icsr, ICSR, IRM, spi->irm);
    icsr = FIELD_DP64(icsr, ICSR, ACTIVE, spi->active);
    icsr = FIELD_DP64(icsr, ICSR, HM, spi->hm);
    icsr = FIELD_DP64(icsr, ICSR, PRIORITY, spi->priority);
    icsr = FIELD_DP64(icsr, ICSR, IAFFID, spi->iaffid);

    return icsr;
}

uint64_t gicv5_request_config(GICv5Common *cs, uint32_t id, GICv5Domain domain,
                              GICv5IntType type, bool virtual)
{
    GICv5 *s = ARM_GICV5(cs);
    uint64_t icsr;

    if (virtual) {
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_request_config: tried to "
                      "read config of a virtual interrupt\n");
        return R_ICSR_F_MASK;
    }

    switch (type) {
    case GICV5_LPI:
    {
        const GICv5ISTConfig *cfg = &s->phys_lpi_config[domain];

        icsr = l2_iste_to_icsr(cs, cfg, id);
        trace_gicv5_request_config(domain_name[domain], inttype_name(type),
                                   virtual, id, icsr);
        return icsr;
    }
    case GICV5_SPI:
    {
        GICv5SPIState *spi = gicv5_spi_state(cs, id, domain);

        if (!spi) {
            qemu_log_mask(LOG_GUEST_ERROR, "gicv5_request_config: tried to "
                          "read config of unreachable SPI %d\n", id);
            return R_ICSR_F_MASK;
        }

        icsr = spi_state_to_icsr(spi);
        trace_gicv5_request_config(domain_name[domain], inttype_name(type),
                                   virtual, id, icsr);
        return icsr;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_request_config: tried to "
                      "read config of bad interrupt type %d\n", type);
        return R_ICSR_F_MASK;
    }
}

void gicv5_activate(GICv5Common *cs, uint32_t id, GICv5Domain domain,
                    GICv5IntType type, bool virtual)
{
    GICv5 *s = ARM_GICV5(cs);
    uint32_t iaffid;

    trace_gicv5_activate(domain_name[domain], inttype_name(type), virtual, id);

    if (virtual) {
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_activate: tried to "
                      "activate a virtual interrupt\n");
        return;
    }

    switch (type) {
    case GICV5_LPI:
    {
        const GICv5ISTConfig *cfg = &s->phys_lpi_config[domain];
        L2_ISTE_Handle h;
        uint32_t *l2_iste_p = get_l2_iste(cs, cfg, id, &h);

        if (!l2_iste_p) {
            return;
        }
        *l2_iste_p = FIELD_DP32(*l2_iste_p, L2_ISTE, ACTIVE, true);
        if (FIELD_EX32(*l2_iste_p, L2_ISTE, HM) == GICV5_EDGE) {
            *l2_iste_p = FIELD_DP32(*l2_iste_p, L2_ISTE, PENDING, false);
        }
        iaffid = FIELD_EX32(*l2_iste_p, L2_ISTE, IAFFID);
        put_l2_iste(cs, cfg, &h);
        break;
    }
    case GICV5_SPI:
    {
        GICv5SPIState *spi = gicv5_spi_state(cs, id, domain);

        if (!spi) {
            qemu_log_mask(LOG_GUEST_ERROR, "gicv5_activate: tried to "
                          "activate unreachable SPI %d\n", id);
            return;
        }

        spi->active = true;
        if (spi->hm == GICV5_EDGE) {
            spi->pending = false;
        }
        iaffid = spi->iaffid;
        break;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_activate: tried to "
                      "activate bad interrupt type %d\n", type);
        return;
    }

    irs_recalc_hppi(s, domain, iaffid);
}

void gicv5_deactivate(GICv5Common *cs, uint32_t id, GICv5Domain domain,
                      GICv5IntType type, bool virtual)
{
    GICv5 *s = ARM_GICV5(cs);
    uint32_t iaffid;

    trace_gicv5_deactivate(domain_name[domain], inttype_name(type), virtual, id);

    if (virtual) {
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_deactivate: tried to "
                      "deactivate a virtual interrupt\n");
        return;
    }

    switch (type) {
    case GICV5_LPI:
    {
        const GICv5ISTConfig *cfg = &s->phys_lpi_config[domain];
        L2_ISTE_Handle h;
        uint32_t *l2_iste_p = get_l2_iste(cs, cfg, id, &h);

        if (!l2_iste_p) {
            return;
        }
        *l2_iste_p = FIELD_DP32(*l2_iste_p, L2_ISTE, ACTIVE, false);
        iaffid = FIELD_EX32(*l2_iste_p, L2_ISTE, IAFFID);
        put_l2_iste(cs, cfg, &h);
        break;
    }
    case GICV5_SPI:
    {
        GICv5SPIState *spi = gicv5_spi_state(cs, id, domain);

        if (!spi) {
            qemu_log_mask(LOG_GUEST_ERROR, "gicv5_deactivate: tried to "
                          "deactivate unreachable SPI %d\n", id);
            return;
        }

        spi->active = false;
        iaffid = spi->iaffid;
        break;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "gicv5_deactivate: tried to "
                      "deactivate bad interrupt type %d\n", type);
        return;
    }

    irs_recalc_hppi(s, domain, iaffid);
}

static void irs_map_l2_istr_write(GICv5 *s, GICv5Domain domain, uint64_t value)
{
    GICv5Common *cs = ARM_GICV5_COMMON(s);
    GICv5ISTConfig *cfg = &s->phys_lpi_config[domain];
    uint32_t intid = FIELD_EX32(value, IRS_MAP_L2_ISTR, ID);
    hwaddr l1_addr;
    uint64_t l1_iste;
    MemTxResult res;

    if (!FIELD_EX64(cs->irs_ist_baser[domain], IRS_IST_BASER, VALID) ||
        !cfg->structure) {
        /* WI if no IST set up or it is not 2-level */
        return;
    }

    /* Find the relevant L1 ISTE and set its VALID bit */
    l1_addr = l1_iste_addr(cs, cfg, intid);

    l1_iste = address_space_ldq_le(&cs->dma_as, l1_addr, cfg->txattrs, &res);
    if (res != MEMTX_OK) {
        goto txfail;
    }

    l1_iste = FIELD_DP64(l1_iste, L1_ISTE, VALID, 1);

    address_space_stq_le(&cs->dma_as, l1_addr, l1_iste, cfg->txattrs, &res);
    if (res != MEMTX_OK) {
        goto txfail;
    }
    /*
     * It's CONSTRAINED UNPREDICTABLE to make an L2 IST valid when
     * some of its entries have Pending already set, so we don't need
     * to go through looking for Pending bits and pulling them into
     * the cache, and we don't need to recalc our HPPI.
     */
    return;

txfail:
    /* Reportable with EC=0x0 if sw error reporting implemented */
    qemu_log_mask(LOG_GUEST_ERROR, "L1 ISTE update failed for ID 0x%x at "
                  "physical address 0x" HWADDR_FMT_plx "\n", intid, l1_addr);
}

/* Data we need to pass through to irs_clean_lpi_cache_entry() */
typedef struct CleanLPICacheUserData {
    GICv5Common *cs;
    GICv5ISTConfig *cfg;
} CleanLPICacheUserData;

static gboolean irs_clean_lpi_cache_entry(gpointer key, gpointer value,
                                          gpointer user_data)
{
    /* Drop this entry from the LPI cache, writing it back to guest memory. */
    CleanLPICacheUserData *ud = user_data;
    hwaddr l2_iste_addr;
    uint64_t id = GPOINTER_TO_INT(key);
    uint32_t l2_iste = *(uint32_t *)value;

    if (!get_l2_iste_addr(ud->cs, ud->cfg, id, &l2_iste_addr) ||
        !write_l2_iste_mem(ud->cs, ud->cfg, l2_iste_addr, l2_iste)) {
        /* We drop the cached entry regardless of writeback errors */
        return true;
    }
    return true;
}

static void irs_clean_lpi_cache(GICv5Common *cs, GICv5ISTConfig *cfg)
{
    /* Write everything in the LPI cache out to guest memory */
    CleanLPICacheUserData ud;
    ud.cs = cs;
    ud.cfg = cfg;

    g_hash_table_foreach_remove(cfg->lpi_cache, irs_clean_lpi_cache_entry, &ud);
}

static void irs_ist_baser_write(GICv5 *s, GICv5Domain domain, uint64_t value)
{
    GICv5Common *cs = ARM_GICV5_COMMON(s);

    if (FIELD_EX64(cs->irs_ist_baser[domain], IRS_IST_BASER, VALID)) {
        /* If VALID is set, ADDR is RO and we can only update VALID */
        bool valid = FIELD_EX64(value, IRS_IST_BASER, VALID);
        if (valid) {
            /* Ignore 1->1 transition */
            return;
        }
        irs_clean_lpi_cache(cs, &s->phys_lpi_config[domain]);
        cs->irs_ist_baser[domain] = FIELD_DP64(cs->irs_ist_baser[domain],
                                               IRS_IST_BASER, VALID, valid);
        s->phys_lpi_config[domain].valid = false;
        trace_gicv5_ist_invalid(domain_name[domain]);
        irs_recalc_hppi_all_cpus(s, domain);
        return;
    }
    cs->irs_ist_baser[domain] = value;

    if (FIELD_EX64(cs->irs_ist_baser[domain], IRS_IST_BASER, VALID)) {
        /*
         * If the guest just set VALID then capture data into config struct,
         * sanitize the reserved values, and expand fields out into byte counts.
         */
        GICv5ISTConfig *cfg = &s->phys_lpi_config[domain];
        uint8_t istbits, l2bits, l2_idx_bits;
        uint8_t id_bits = FIELD_EX64(cs->irs_ist_cfgr[domain],
                                     IRS_IST_CFGR, LPI_ID_BITS);
        id_bits = MIN(MAX(id_bits, QEMU_GICV5_MIN_LPI_ID_BITS), QEMU_GICV5_ID_BITS);

        switch (FIELD_EX64(cs->irs_ist_cfgr[domain], IRS_IST_CFGR, ISTSZ)) {
        case 0:
        case 3: /* reserved: acts like the minimum required size */
            istbits = 2;
            break;
        case 1:
            istbits = 3;
            break;
        case 2:
            istbits = 4;
            break;
        default:
            g_assert_not_reached();
        }
        switch (FIELD_EX64(cs->irs_ist_cfgr[domain], IRS_IST_CFGR, L2SZ)) {
        case 0:
        case 3: /* reserved; CONSTRAINED UNPREDICTABLE */
            l2bits = 12; /* 4K: 12 bits */
            break;
        case 1:
            l2bits = 14; /* 16K: 14 bits */
            break;
        case 2:
            l2bits = 16; /* 64K: 16 bits */
            break;
        default:
            g_assert_not_reached();
        }
        /*
         * Calculate how many bits of an ID index the L2 table
         * (e.g. if we need 14 bits to index each byte in a 16K L2 table,
         * but each entry is 4 bytes wide then we need 14 - 2 = 12 bits
         * to index an entry in the table).
         */
        l2_idx_bits = l2bits - istbits;
        cfg->base = cs->irs_ist_baser[domain] & R_IRS_IST_BASER_ADDR_MASK;
        cfg->txattrs = irs_txattrs(cs, domain),
        cfg->id_bits = id_bits;
        cfg->istsz = 1 << istbits;
        cfg->l2_idx_bits = l2_idx_bits;
        cfg->structure = FIELD_EX64(cs->irs_ist_cfgr[domain],
                                    IRS_IST_CFGR, STRUCTURE);
        if (!cfg->lpi_cache) {
            /*
             * Keys are GINT_TO_POINTER(intid), so we want the g_direct_hash
             * and g_direct_equal hash and equality functions. We don't
             * want to free the keys, but we do want to free the values
             * (which are pointer-to-uint32_t).
             */
            cfg->lpi_cache = g_hash_table_new_full(NULL, NULL, NULL, g_free);
        }
        cfg->valid = true;
        trace_gicv5_ist_valid(domain_name[domain], cfg->base, cfg->id_bits,
                              cfg->l2_idx_bits, cfg->istsz, cfg->structure);
        irs_recalc_hppi_all_cpus(s, domain);
    }
}

static void spi_sample(GICv5SPIState *spi)
{
    /*
     * Sample the state of the SPI input line; this generates
     * SET_EDGE, SET_LEVEL or CLEAR events which update the SPI's
     * pending state and handling mode per R_HHKMN.  The logic is the
     * same for "the input line changed" (R_QBXXV) and "software asked
     * us to resample" (R_DMTFM).
     */
    if (spi->level) {
        /*
         * SET_LEVEL or SET_EDGE: interrupt becomes pending, and the
         * handling mode is updated to match the trigger mode.
         */
        spi->pending = true;
        spi->hm = spi->tm == GICV5_TRIGGER_EDGE ? GICV5_EDGE : GICV5_LEVEL;
    } else if (spi->tm == GICV5_TRIGGER_LEVEL) {
        /* falling edges only trigger a CLEAR event for level-triggered */
        spi->pending = false;
    }
}

static bool irs_pe_selr_valid(GICv5Common *cs, GICv5Domain domain)
{
    /*
     * Return true if IRS_PE_SELR has a valid AFFID in it. We don't
     * expect the guest to do this except perhaps once at startup, so
     * do a simple linear scan through the cpu_iaffids array.
     */
    for (int i = 0; i < cs->num_cpu_iaffids; i++) {
        if (cs->irs_pe_selr[domain] == cs->cpu_iaffids[i]) {
            return true;
        }
    }
    return false;
}

static bool config_readl(GICv5 *s, GICv5Domain domain, hwaddr offset,
                         uint64_t *data, MemTxAttrs attrs)
{
    GICv5Common *cs = ARM_GICV5_COMMON(s);
    uint32_t v = 0;

    switch (offset) {
    case A_IRS_IDR0:
        v = cs->irs_idr0;
        /* INT_DOM reports the domain this register is for */
        v = FIELD_DP32(v, IRS_IDR0, INT_DOM, domain);
        if (domain != GICV5_ID_REALM) {
            /* MEC field RES0 except for the Realm domain */
            v &= ~R_IRS_IDR0_MEC_MASK;
        }
        if (domain == GICV5_ID_EL3) {
            /* VIRT is RES0 for EL3 domain */
            v &= ~R_IRS_IDR0_VIRT_MASK;
            /* ...which means VIRT_ONE_N is also RES0 */
            v &= ~R_IRS_IDR0_VIRT_ONE_N_MASK;
        }
        return true;

    case A_IRS_IDR1:
        *data = cs->irs_idr1;
        return true;

    case A_IRS_IDR2:
        *data = cs->irs_idr2;
        return true;

    case A_IRS_IDR3:
        /* In EL3 IDR0.VIRT is 0 so this is RES0 */
        *data = domain == GICV5_ID_EL3 ? 0 : cs->irs_idr3;
        return true;

    case A_IRS_IDR4:
        /* In EL3 IDR0.VIRT is 0 so this is RES0 */
        *data = domain == GICV5_ID_EL3 ? 0 : cs->irs_idr4;
        return true;

    case A_IRS_IDR5:
        *data = cs->irs_idr5;
        return true;

    case A_IRS_IDR6:
        *data = cs->irs_idr6;
        return true;

    case A_IRS_IDR7:
        *data = cs->irs_idr7;
        return true;

    case A_IRS_IIDR:
        *data = cs->irs_iidr;
        return true;

    case A_IRS_AIDR:
        *data = cs->irs_aidr;
        return true;

    case A_IRS_IST_BASER:
        *data = extract64(cs->irs_ist_baser[domain], 0, 32);
        return true;

    case A_IRS_IST_BASER + 4:
        *data = extract64(cs->irs_ist_baser[domain], 32, 32);
        return true;

    case A_IRS_IST_STATUSR:
        /*
         * For QEMU writes to IRS_IST_BASER and IRS_MAP_L2_ISTR take effect
         * instantaneously, and the guest can never see the IDLE bit as 0.
         */
        *data = R_IRS_IST_STATUSR_IDLE_MASK;
        return true;

    case A_IRS_IST_CFGR:
        *data = cs->irs_ist_cfgr[domain];
        return true;

    case A_IRS_SPI_STATUSR:
        /*
         * QEMU writes to IRS_SPI_{CFGR,DOMAINR,SELR,VMR} take effect
         * instantaneously, so the guest can never see the IDLE bit as 0.
         */
        v = FIELD_DP32(v, IRS_SPI_STATUSR, V,
                       spi_for_selr(cs, domain) != NULL);
        v = FIELD_DP32(v, IRS_SPI_STATUSR, IDLE, 1);
        *data = v;
        return true;

    case A_IRS_SPI_CFGR:
    {
        GICv5SPIState *spi = spi_for_selr(cs, domain);

        if (spi) {
            v = FIELD_DP32(v, IRS_SPI_CFGR, TM, spi->tm);
        }
        *data = v;
        return true;
    }
    case A_IRS_SPI_DOMAINR:
        if (domain == GICV5_ID_EL3) {
            /* This is RAZ/WI except for the EL3 domain */
            GICv5SPIState *spi = spi_for_selr(cs, domain);
            if (spi) {
                v = FIELD_DP32(v, IRS_SPI_DOMAINR, DOMAIN, spi->domain);
            }
        }
        *data = v;
        return true;
    case A_IRS_CR0:
        /* Enabling is instantaneous for us so IDLE is always 1 */
        *data = cs->irs_cr0[domain] | R_IRS_CR0_IDLE_MASK;
        if (FIELD_EX32(cs->irs_cr0[domain], IRS_CR0, IRSEN)) {
            irs_recalc_hppi_all_cpus(s, domain);
        } else {
            irs_recall_hppis(s, domain);
        }
        return true;
    case A_IRS_CR1:
        *data = cs->irs_cr1[domain];
        return true;
    case A_IRS_SYNC_STATUSR:
        /* Sync is a no-op for QEMU: we are always IDLE */
        *data = R_IRS_SYNC_STATUSR_IDLE_MASK;
        return true;
    case A_IRS_PE_SELR:
        *data = cs->irs_pe_selr[domain];
        return true;
    case A_IRS_PE_CR0:
        /* We don't implement 1ofN, so this is RAZ/WI for us */
        *data = 0;
        return true;
    case A_IRS_PE_STATUSR:
        /*
         * Our CPUs are always online, so we're really just reporting
         * whether the guest wrote a valid AFFID to IRS_PE_SELR
         */
        v = R_IRS_PE_STATUSR_IDLE_MASK;
        if (irs_pe_selr_valid(cs, domain)) {
            v |= R_IRS_PE_STATUSR_V_MASK | R_IRS_PE_STATUSR_ONLINE_MASK;
        }
        *data = v;
        return true;
    case A_IRS_DEVARCH:
        *data = IRS_DEVARCH_VALUE;
        return true;
    case A_IRS_IDREGS ... A_IRS_IDREGS + 0x2f:
        /* CoreSight ID registers */
        *data = gicv5_idreg(offset - A_IRS_IDREGS);
        return true;
    }

    return false;
}

static bool config_writel(GICv5 *s, GICv5Domain domain, hwaddr offset,
                          uint64_t data, MemTxAttrs attrs)
{
    GICv5Common *cs = ARM_GICV5_COMMON(s);

    switch (offset) {
    case A_IRS_IST_BASER:
        irs_ist_baser_write(s, domain,
                            deposit64(cs->irs_ist_baser[domain], 0, 32, data));
        return true;
    case A_IRS_IST_BASER + 4:
        irs_ist_baser_write(s, domain,
                            deposit64(cs->irs_ist_baser[domain], 32, 32, data));
        return true;
    case A_IRS_IST_CFGR:
        if (FIELD_EX64(cs->irs_ist_baser[domain], IRS_IST_BASER, VALID)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "guest tried to write IRS_IST_CFGR for %s config frame "
                          "while IST_BASER.VALID set\n", domain_name[domain]);
        } else {
            cs->irs_ist_cfgr[domain] = data;
        }
        return true;
    case A_IRS_MAP_L2_ISTR:
        irs_map_l2_istr_write(s, domain, data);
        return true;
    case A_IRS_SPI_SELR:
        cs->irs_spi_selr[domain] = data;
        return true;
    case A_IRS_SPI_CFGR:
    {
        GICv5SPIState *spi = spi_for_selr(cs, domain);
        if (spi) {
            GICv5TriggerMode old_tm = spi->tm;
            spi->tm = FIELD_EX32(data, IRS_SPI_CFGR, TM);
            if (spi->tm != old_tm) {
                /*
                 * R_KBPXL: updates to SPI trigger mode can generate CLEAR or
                 * SET_LEVEL events. This is not the same logic as spi_sample().
                 */
                if (spi->tm == GICV5_TRIGGER_LEVEL) {
                    if (spi->level) {
                        spi->pending = true;
                        spi->hm = GICV5_LEVEL;
                    } else {
                        spi->pending = false;
                    }
                } else if (spi->level) {
                    spi->pending = false;
                }
                irs_recalc_hppi(s, spi->domain, spi->iaffid);
            }
        }
        return true;
    }
    case A_IRS_SPI_DOMAINR:
        if (domain == GICV5_ID_EL3) {
            /* this is RAZ/WI except for the EL3 domain */
            GICv5SPIState *spi = spi_for_selr(cs, domain);
            if (spi) {
                GICv5Domain old_domain = spi->domain;
                spi->domain = FIELD_EX32(data, IRS_SPI_DOMAINR, DOMAIN);
                if (spi->domain != old_domain) {
                    irs_recalc_hppi(s, old_domain, spi->iaffid);
                    irs_recalc_hppi(s, spi->domain, spi->iaffid);
                }
            }
        }
        return true;
    case A_IRS_SPI_RESAMPLER:
    {
        uint32_t id = FIELD_EX32(data, IRS_SPI_RESAMPLER, SPI_ID);
        GICv5SPIState *spi = gicv5_spi_state(cs, id, domain);

        if (spi) {
            spi_sample(spi);
            irs_recalc_hppi(s, spi->domain, spi->iaffid);
            trace_gicv5_spi_state(id, spi->level, spi->pending, spi->active);
        }
        return true;
    }
    case A_IRS_CR0:
        cs->irs_cr0[domain] = data & R_IRS_CR0_IRSEN_MASK;
        return true;
    case A_IRS_CR1:
        cs->irs_cr1[domain] = data;
        return true;
    case A_IRS_SYNCR:
        /* Sync is a no-op for QEMU: ignore write */
        return true;
    case A_IRS_PE_SELR:
        cs->irs_pe_selr[domain] = data;
        return true;
    case A_IRS_PE_CR0:
        /* We don't implement 1ofN, so this is RAZ/WI for us */
        return true;
    }

    return false;
}

static bool config_readll(GICv5 *s, GICv5Domain domain, hwaddr offset,
                          uint64_t *data, MemTxAttrs attrs)
{
    GICv5Common *cs = ARM_GICV5_COMMON(s);

    switch (offset) {
    case A_IRS_IST_BASER:
        *data = cs->irs_ist_baser[domain];
        return true;
    }

    return false;
}

static bool config_writell(GICv5 *s, GICv5Domain domain, hwaddr offset,
                           uint64_t data, MemTxAttrs attrs)
{
    switch (offset) {
    case A_IRS_IST_BASER:
        irs_ist_baser_write(s, domain, data);
        return true;
    }

    return false;
}

static MemTxResult config_read(void *opaque, GICv5Domain domain, hwaddr offset,
                               uint64_t *data, unsigned size,
                               MemTxAttrs attrs)
{
    GICv5 *s = ARM_GICV5(opaque);
    bool result;

    switch (size) {
    case 4:
        result = config_readl(s, domain, offset, data, attrs);
        break;
    case 8:
        result = config_readll(s, domain, offset, data, attrs);
        break;
    default:
        result = false;
        break;
    }

    if (!result) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest read for IRS %s config frame "
                      "at offset " HWADDR_FMT_plx
                      " size %u\n", __func__, domain_name[domain],
                      offset, size);
        trace_gicv5_badread(domain_name[domain], offset, size);
        /*
         * The spec requires that reserved registers are RAZ/WI; so we
         * log the error but return MEMTX_OK so we don't cause a
         * spurious data abort.
         */
        *data = 0;
    } else {
        trace_gicv5_read(domain_name[domain], offset, *data, size);
    }

    return MEMTX_OK;
}

static MemTxResult config_write(void *opaque, GICv5Domain domain,
                                hwaddr offset, uint64_t data, unsigned size,
                                MemTxAttrs attrs)
{
    GICv5 *s = ARM_GICV5(opaque);
    bool result;

    switch (size) {
    case 4:
        result = config_writel(s, domain, offset, data, attrs);
        break;
    case 8:
        result = config_writell(s, domain, offset, data, attrs);
        break;
    default:
        result = false;
        break;
    }

    if (!result) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid guest write for IRS %s config frame "
                      "at offset " HWADDR_FMT_plx
                      " size %u\n", __func__, domain_name[domain],
                      offset, size);
        trace_gicv5_badwrite(domain_name[domain], offset, data, size);
        /*
         * The spec requires that reserved registers are RAZ/WI; so we
         * log the error but return MEMTX_OK so we don't cause a
         * spurious data abort.
         */
    } else {
        trace_gicv5_write(domain_name[domain], offset, data, size);
    }

    return MEMTX_OK;
}

#define DEFINE_READ_WRITE_WRAPPERS(NAME, DOMAIN)                           \
    static MemTxResult config_##NAME##_read(void *opaque, hwaddr offset,   \
                                            uint64_t *data, unsigned size, \
                                            MemTxAttrs attrs)              \
    {                                                                      \
        return config_read(opaque, DOMAIN, offset, data, size, attrs);     \
    }                                                                      \
    static MemTxResult config_##NAME##_write(void *opaque, hwaddr offset,  \
                                             uint64_t data, unsigned size, \
                                             MemTxAttrs attrs)             \
    {                                                                      \
        return config_write(opaque, DOMAIN, offset, data, size, attrs);    \
    }

DEFINE_READ_WRITE_WRAPPERS(ns, GICV5_ID_NS)
DEFINE_READ_WRITE_WRAPPERS(realm, GICV5_ID_REALM)
DEFINE_READ_WRITE_WRAPPERS(secure, GICV5_ID_S)
DEFINE_READ_WRITE_WRAPPERS(el3, GICV5_ID_EL3)

#define FRAME_OP_ENTRY(NAME, DOMAIN)                    \
    [DOMAIN] = {                                        \
        .read_with_attrs = config_##NAME##_read,        \
        .write_with_attrs = config_##NAME##_write,      \
        .endianness = DEVICE_LITTLE_ENDIAN,             \
        .valid.min_access_size = 4,                     \
        .valid.max_access_size = 8,                     \
        .impl.min_access_size = 4,                      \
        .impl.max_access_size = 8,                      \
    }

static const MemoryRegionOps config_frame_ops[NUM_GICV5_DOMAINS] = {
    FRAME_OP_ENTRY(ns, GICV5_ID_NS),
    FRAME_OP_ENTRY(realm, GICV5_ID_REALM),
    FRAME_OP_ENTRY(secure, GICV5_ID_S),
    FRAME_OP_ENTRY(el3, GICV5_ID_EL3),
};

static void gicv5_set_spi(void *opaque, int irq, int level)
{
    /* These irqs are all SPIs; the INTID is irq + s->spi_base */
    GICv5Common *cs = ARM_GICV5_COMMON(opaque);
    GICv5 *s = ARM_GICV5(cs);
    uint32_t spi_id = irq + cs->spi_base;
    GICv5SPIState *spi = gicv5_raw_spi_state(cs, spi_id);

    if (!spi || spi->level == level) {
        return;
    }

    trace_gicv5_spi(spi_id, level);

    spi->level = level;
    spi_sample(spi);
    trace_gicv5_spi_state(spi_id, spi->level, spi->pending, spi->active);

    irs_recalc_hppi(s, spi->domain, spi->iaffid);
}

static void gicv5_reset_hold(Object *obj, ResetType type)
{
    GICv5 *s = ARM_GICV5(obj);
    GICv5Class *c = ARM_GICV5_GET_CLASS(s);

    if (c->parent_phases.hold) {
        c->parent_phases.hold(obj, type);
    }

    /* IRS_IST_BASER and IRS_IST_CFGR reset to 0, clear cached info */
    for (int i = 0; i < NUM_GICV5_DOMAINS; i++) {
        s->phys_lpi_config[i].valid = false;
        /*
         * If we got reset (power-cycled) with data in the cache, don't
         * write it out to guest memory; just return to "empty cache".
         */
        if (s->phys_lpi_config[i].lpi_cache) {
            g_hash_table_remove_all(s->phys_lpi_config[i].lpi_cache);
        }
    }
}

static void gicv5_set_idregs(GICv5Common *cs)
{
    /* Set the ID register value fields */
    uint32_t v;

    /*
     * Fields in IDR0 for optional parts of the spec that we don't
     * implement are 0.
     */
    v = 0;
    /*
     * We can handle physical addresses of any size, so report support
     * for 56 bits of physical address space.
     */
    v = FIELD_DP32(v, IRS_IDR0, PA_RANGE, 7);
    v = FIELD_DP32(v, IRS_IDR0, IRSID, cs->irsid);
    cs->irs_idr0 = v;

    v = 0;
    v = FIELD_DP32(v, IRS_IDR1, PE_CNT, cs->num_cpus);
    v = FIELD_DP32(v, IRS_IDR1, IAFFID_BITS, QEMU_GICV5_IAFFID_BITS - 1);
    v = FIELD_DP32(v, IRS_IDR1, PRI_BITS, QEMU_GICV5_PRI_BITS - 1);
    cs->irs_idr1 = v;

    v = 0;
    /* We always support physical LPIs with 2-level ISTs of all sizes */
    v = FIELD_DP32(v, IRS_IDR2, ID_BITS, QEMU_GICV5_ID_BITS);
    v = FIELD_DP32(v, IRS_IDR2, LPI, 1);
    v = FIELD_DP32(v, IRS_IDR2, MIN_LPI_ID_BITS, QEMU_GICV5_MIN_LPI_ID_BITS);
    v = FIELD_DP32(v, IRS_IDR2, IST_LEVELS, 1);
    v = FIELD_DP32(v, IRS_IDR2, IST_L2SZ, 7);
    /* Our impl does not need IST metadata, so ISTMD and ISTMD_SZ are 0 */
    cs->irs_idr2 = v;

    /* We don't implement virtualization yet, so these are zero */
    cs->irs_idr3 = 0;
    cs->irs_idr4 = 0;

    /* These three have just one field each */
    cs->irs_idr5 = FIELD_DP32(0, IRS_IDR5, SPI_RANGE, cs->spi_range);
    cs->irs_idr6 = FIELD_DP32(0, IRS_IDR6, SPI_IRS_RANGE, cs->spi_irs_range);
    cs->irs_idr7 = FIELD_DP32(0, IRS_IDR7, SPI_BASE, cs->spi_base);

    v = 0;
    v = FIELD_DP32(v, IRS_IIDR, IMPLEMENTER, QEMU_GICV5_IMPLEMENTER);
    v = FIELD_DP32(v, IRS_IIDR, REVISION, QEMU_GICV5_REVISION);
    v = FIELD_DP32(v, IRS_IIDR, VARIANT, QEMU_GICV5_VARIANT);
    v = FIELD_DP32(v, IRS_IIDR, PRODUCTID, QEMU_GICV5_PRODUCTID);
    cs->irs_iidr = v;

    /* This is a GICv5.0 IRS, so all fields are zero */
    cs->irs_aidr = 0;
}

static void gicv5_realize(DeviceState *dev, Error **errp)
{
    GICv5 *s = ARM_GICV5(dev);
    GICv5Common *cs = ARM_GICV5_COMMON(dev);
    GICv5Class *gc = ARM_GICV5_GET_CLASS(dev);
    Error *migration_blocker = NULL;

    ERRP_GUARD();

    gc->parent_realize(dev, errp);
    if (*errp) {
        return;
    }

    error_setg(&migration_blocker,
               "Live migration disabled: not yet supported by GICv5");
    if (migrate_add_blocker(&migration_blocker, errp)) {
        return;
    }

    /*
     * When we implement support for more than one interrupt domain,
     * we will provide some QOM properties so the board can configure
     * which domains are implemented. For now, we only implement the
     * NS domain.
     */
    cs->implemented_domains = (1 << GICV5_ID_NS);

    gicv5_set_idregs(cs);
    gicv5_common_init_irqs_and_mmio(cs, gicv5_set_spi, config_frame_ops);

    for (int i = 0; i < NUM_GICV5_DOMAINS; i++) {
        if (gicv5_domain_implemented(cs, i)) {
            s->hppi[i] = g_new0(GICv5PendingIrq, cs->num_cpus);
        }
    }
}

static void gicv5_init(Object *obj)
{
}

static void gicv5_finalize(Object *obj)
{
}

static void gicv5_class_init(ObjectClass *oc, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);
    GICv5Class *gc = ARM_GICV5_CLASS(oc);

    device_class_set_parent_realize(dc, gicv5_realize, &gc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, gicv5_reset_hold, NULL,
                                       &gc->parent_phases);
}
