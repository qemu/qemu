/*
 * ARM GICv3 support - internal interfaces
 *
 * Copyright (c) 2012 Linaro Limited
 * Copyright (c) 2015 Huawei.
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Written by Peter Maydell
 * Reworked for GICv3 by Shlomo Pongratz and Pavel Fedin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_ARM_GICV3_INTERNAL_H
#define QEMU_ARM_GICV3_INTERNAL_H

#include "hw/intc/arm_gicv3_common.h"

/* Distributor registers, as offsets from the distributor base address */
#define GICD_CTLR            0x0000
#define GICD_TYPER           0x0004
#define GICD_IIDR            0x0008
#define GICD_STATUSR         0x0010
#define GICD_SETSPI_NSR      0x0040
#define GICD_CLRSPI_NSR      0x0048
#define GICD_SETSPI_SR       0x0050
#define GICD_CLRSPI_SR       0x0058
#define GICD_SEIR            0x0068
#define GICD_IGROUPR         0x0080
#define GICD_ISENABLER       0x0100
#define GICD_ICENABLER       0x0180
#define GICD_ISPENDR         0x0200
#define GICD_ICPENDR         0x0280
#define GICD_ISACTIVER       0x0300
#define GICD_ICACTIVER       0x0380
#define GICD_IPRIORITYR      0x0400
#define GICD_ITARGETSR       0x0800
#define GICD_ICFGR           0x0C00
#define GICD_IGRPMODR        0x0D00
#define GICD_NSACR           0x0E00
#define GICD_SGIR            0x0F00
#define GICD_CPENDSGIR       0x0F10
#define GICD_SPENDSGIR       0x0F20
#define GICD_IROUTER         0x6000
#define GICD_IDREGS          0xFFD0

/* GICD_CTLR fields  */
#define GICD_CTLR_EN_GRP0           (1U << 0)
#define GICD_CTLR_EN_GRP1NS         (1U << 1) /* GICv3 5.3.20 */
#define GICD_CTLR_EN_GRP1S          (1U << 2)
#define GICD_CTLR_EN_GRP1_ALL       (GICD_CTLR_EN_GRP1NS | GICD_CTLR_EN_GRP1S)
/* Bit 4 is ARE if the system doesn't support TrustZone, ARE_S otherwise */
#define GICD_CTLR_ARE               (1U << 4)
#define GICD_CTLR_ARE_S             (1U << 4)
#define GICD_CTLR_ARE_NS            (1U << 5)
#define GICD_CTLR_DS                (1U << 6)
#define GICD_CTLR_E1NWF             (1U << 7)
#define GICD_CTLR_RWP               (1U << 31)

/*
 * Redistributor frame offsets from RD_base
 */
#define GICR_SGI_OFFSET 0x10000

/*
 * Redistributor registers, offsets from RD_base
 */
#define GICR_CTLR             0x0000
#define GICR_IIDR             0x0004
#define GICR_TYPER            0x0008
#define GICR_STATUSR          0x0010
#define GICR_WAKER            0x0014
#define GICR_SETLPIR          0x0040
#define GICR_CLRLPIR          0x0048
#define GICR_PROPBASER        0x0070
#define GICR_PENDBASER        0x0078
#define GICR_INVLPIR          0x00A0
#define GICR_INVALLR          0x00B0
#define GICR_SYNCR            0x00C0
#define GICR_IDREGS           0xFFD0

/* SGI and PPI Redistributor registers, offsets from RD_base */
#define GICR_IGROUPR0         (GICR_SGI_OFFSET + 0x0080)
#define GICR_ISENABLER0       (GICR_SGI_OFFSET + 0x0100)
#define GICR_ICENABLER0       (GICR_SGI_OFFSET + 0x0180)
#define GICR_ISPENDR0         (GICR_SGI_OFFSET + 0x0200)
#define GICR_ICPENDR0         (GICR_SGI_OFFSET + 0x0280)
#define GICR_ISACTIVER0       (GICR_SGI_OFFSET + 0x0300)
#define GICR_ICACTIVER0       (GICR_SGI_OFFSET + 0x0380)
#define GICR_IPRIORITYR       (GICR_SGI_OFFSET + 0x0400)
#define GICR_ICFGR0           (GICR_SGI_OFFSET + 0x0C00)
#define GICR_ICFGR1           (GICR_SGI_OFFSET + 0x0C04)
#define GICR_IGRPMODR0        (GICR_SGI_OFFSET + 0x0D00)
#define GICR_NSACR            (GICR_SGI_OFFSET + 0x0E00)

#define GICR_CTLR_ENABLE_LPIS        (1U << 0)
#define GICR_CTLR_RWP                (1U << 3)
#define GICR_CTLR_DPG0               (1U << 24)
#define GICR_CTLR_DPG1NS             (1U << 25)
#define GICR_CTLR_DPG1S              (1U << 26)
#define GICR_CTLR_UWP                (1U << 31)

#define GICR_TYPER_PLPIS             (1U << 0)
#define GICR_TYPER_VLPIS             (1U << 1)
#define GICR_TYPER_DIRECTLPI         (1U << 3)
#define GICR_TYPER_LAST              (1U << 4)
#define GICR_TYPER_DPGS              (1U << 5)
#define GICR_TYPER_PROCNUM           (0xFFFFU << 8)
#define GICR_TYPER_COMMONLPIAFF      (0x3 << 24)
#define GICR_TYPER_AFFINITYVALUE     (0xFFFFFFFFULL << 32)

#define GICR_WAKER_ProcessorSleep    (1U << 1)
#define GICR_WAKER_ChildrenAsleep    (1U << 2)

#define GICR_PROPBASER_OUTER_CACHEABILITY_MASK (7ULL << 56)
#define GICR_PROPBASER_ADDR_MASK               (0xfffffffffULL << 12)
#define GICR_PROPBASER_SHAREABILITY_MASK       (3U << 10)
#define GICR_PROPBASER_CACHEABILITY_MASK       (7U << 7)
#define GICR_PROPBASER_IDBITS_MASK             (0x1f)

#define GICR_PENDBASER_PTZ                     (1ULL << 62)
#define GICR_PENDBASER_OUTER_CACHEABILITY_MASK (7ULL << 56)
#define GICR_PENDBASER_ADDR_MASK               (0xffffffffULL << 16)
#define GICR_PENDBASER_SHAREABILITY_MASK       (3U << 10)
#define GICR_PENDBASER_CACHEABILITY_MASK       (7U << 7)

#define ICC_CTLR_EL1_CBPR           (1U << 0)
#define ICC_CTLR_EL1_EOIMODE        (1U << 1)
#define ICC_CTLR_EL1_PMHE           (1U << 6)
#define ICC_CTLR_EL1_PRIBITS_SHIFT 8
#define ICC_CTLR_EL1_IDBITS_SHIFT 11
#define ICC_CTLR_EL1_SEIS           (1U << 14)
#define ICC_CTLR_EL1_A3V            (1U << 15)

#define ICC_PMR_PRIORITY_MASK    0xff
#define ICC_BPR_BINARYPOINT_MASK 0x07
#define ICC_IGRPEN_ENABLE        0x01

#define ICC_CTLR_EL3_CBPR_EL1S (1U << 0)
#define ICC_CTLR_EL3_CBPR_EL1NS (1U << 1)
#define ICC_CTLR_EL3_EOIMODE_EL3 (1U << 2)
#define ICC_CTLR_EL3_EOIMODE_EL1S (1U << 3)
#define ICC_CTLR_EL3_EOIMODE_EL1NS (1U << 4)
#define ICC_CTLR_EL3_RM (1U << 5)
#define ICC_CTLR_EL3_PMHE (1U << 6)
#define ICC_CTLR_EL3_PRIBITS_SHIFT 8
#define ICC_CTLR_EL3_IDBITS_SHIFT 11
#define ICC_CTLR_EL3_SEIS (1U << 14)
#define ICC_CTLR_EL3_A3V (1U << 15)
#define ICC_CTLR_EL3_NDS (1U << 17)

/* Special interrupt IDs */
#define INTID_SECURE 1020
#define INTID_NONSECURE 1021
#define INTID_SPURIOUS 1023

/* Functions internal to the emulated GICv3 */

/**
 * gicv3_redist_update:
 * @cs: GICv3CPUState for this redistributor
 *
 * Recalculate the highest priority pending interrupt after a
 * change to redistributor state, and inform the CPU accordingly.
 */
void gicv3_redist_update(GICv3CPUState *cs);

/**
 * gicv3_update:
 * @s: GICv3State
 * @start: first interrupt whose state changed
 * @len: length of the range of interrupts whose state changed
 *
 * Recalculate the highest priority pending interrupts after a
 * change to the distributor state affecting @len interrupts
 * starting at @start, and inform the CPUs accordingly.
 */
void gicv3_update(GICv3State *s, int start, int len);

/**
 * gicv3_full_update_noirqset:
 * @s: GICv3State
 *
 * Recalculate the cached information about highest priority
 * pending interrupts, but don't inform the CPUs. This should be
 * called after an incoming migration has loaded new state.
 */
void gicv3_full_update_noirqset(GICv3State *s);

/**
 * gicv3_full_update:
 * @s: GICv3State
 *
 * Recalculate the highest priority pending interrupts after
 * a change that could affect the status of all interrupts,
 * and inform the CPUs accordingly.
 */
void gicv3_full_update(GICv3State *s);
MemTxResult gicv3_dist_read(void *opaque, hwaddr offset, uint64_t *data,
                            unsigned size, MemTxAttrs attrs);
MemTxResult gicv3_dist_write(void *opaque, hwaddr addr, uint64_t data,
                             unsigned size, MemTxAttrs attrs);
MemTxResult gicv3_redist_read(void *opaque, hwaddr offset, uint64_t *data,
                              unsigned size, MemTxAttrs attrs);
MemTxResult gicv3_redist_write(void *opaque, hwaddr offset, uint64_t data,
                               unsigned size, MemTxAttrs attrs);
void gicv3_dist_set_irq(GICv3State *s, int irq, int level);
void gicv3_redist_set_irq(GICv3CPUState *cs, int irq, int level);
void gicv3_redist_send_sgi(GICv3CPUState *cs, int grp, int irq, bool ns);
void gicv3_init_cpuif(GICv3State *s);

/**
 * gicv3_cpuif_update:
 * @cs: GICv3CPUState for the CPU to update
 *
 * Recalculate whether to assert the IRQ or FIQ lines after a change
 * to the current highest priority pending interrupt, the CPU's
 * current running priority or the CPU's current exception level or
 * security state.
 */
void gicv3_cpuif_update(GICv3CPUState *cs);

static inline uint32_t gicv3_iidr(void)
{
    /* Return the Implementer Identification Register value
     * for the emulated GICv3, as reported in GICD_IIDR and GICR_IIDR.
     *
     * We claim to be an ARM r0p0 with a zero ProductID.
     * This is the same as an r0p0 GIC-500.
     */
    return 0x43b;
}

static inline uint32_t gicv3_idreg(int regoffset)
{
    /* Return the value of the CoreSight ID register at the specified
     * offset from the first ID register (as found in the distributor
     * and redistributor register banks).
     * These values indicate an ARM implementation of a GICv3.
     */
    static const uint8_t gicd_ids[] = {
        0x44, 0x00, 0x00, 0x00, 0x92, 0xB4, 0x3B, 0x00, 0x0D, 0xF0, 0x05, 0xB1
    };
    return gicd_ids[regoffset / 4];
}

/**
 * gicv3_irq_group:
 *
 * Return the group which this interrupt is configured as (GICV3_G0,
 * GICV3_G1 or GICV3_G1NS).
 */
static inline int gicv3_irq_group(GICv3State *s, GICv3CPUState *cs, int irq)
{
    bool grpbit, grpmodbit;

    if (irq < GIC_INTERNAL) {
        grpbit = extract32(cs->gicr_igroupr0, irq, 1);
        grpmodbit = extract32(cs->gicr_igrpmodr0, irq, 1);
    } else {
        grpbit = gicv3_gicd_group_test(s, irq);
        grpmodbit = gicv3_gicd_grpmod_test(s, irq);
    }
    if (grpbit) {
        return GICV3_G1NS;
    }
    if (s->gicd_ctlr & GICD_CTLR_DS) {
        return GICV3_G0;
    }
    return grpmodbit ? GICV3_G1 : GICV3_G0;
}

/**
 * gicv3_redist_affid:
 *
 * Return the 32-bit affinity ID of the CPU connected to this redistributor
 */
static inline uint32_t gicv3_redist_affid(GICv3CPUState *cs)
{
    return cs->gicr_typer >> 32;
}

/**
 * gicv3_cache_target_cpustate:
 *
 * Update the cached CPU state corresponding to the target for this interrupt
 * (which is kept in s->gicd_irouter_target[]).
 */
static inline void gicv3_cache_target_cpustate(GICv3State *s, int irq)
{
    GICv3CPUState *cs = NULL;
    int i;
    uint32_t tgtaff = extract64(s->gicd_irouter[irq], 0, 24) |
        extract64(s->gicd_irouter[irq], 32, 8) << 24;

    for (i = 0; i < s->num_cpu; i++) {
        if (s->cpu[i].gicr_typer >> 32 == tgtaff) {
            cs = &s->cpu[i];
            break;
        }
    }

    s->gicd_irouter_target[irq] = cs;
}

/**
 * gicv3_cache_all_target_cpustates:
 *
 * Populate the entire cache of CPU state pointers for interrupt targets
 * (eg after inbound migration or CPU reset)
 */
static inline void gicv3_cache_all_target_cpustates(GICv3State *s)
{
    int irq;

    for (irq = GIC_INTERNAL; irq < GICV3_MAXIRQ; irq++) {
        gicv3_cache_target_cpustate(s, irq);
    }
}

#endif /* !QEMU_ARM_GIC_INTERNAL_H */
