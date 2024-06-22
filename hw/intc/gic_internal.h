/*
 * ARM GIC support - internal interfaces
 *
 * Copyright (c) 2012 Linaro Limited
 * Written by Peter Maydell
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

#ifndef QEMU_ARM_GIC_INTERNAL_H
#define QEMU_ARM_GIC_INTERNAL_H

#include "hw/registerfields.h"
#include "hw/intc/arm_gic.h"

#define ALL_CPU_MASK ((unsigned)(((1 << GIC_NCPU) - 1)))

#define GIC_DIST_SET_ENABLED(irq, cm) (s->irq_state[irq].enabled |= (cm))
#define GIC_DIST_CLEAR_ENABLED(irq, cm) (s->irq_state[irq].enabled &= ~(cm))
#define GIC_DIST_TEST_ENABLED(irq, cm) ((s->irq_state[irq].enabled & (cm)) != 0)
#define GIC_DIST_SET_PENDING(irq, cm) (s->irq_state[irq].pending |= (cm))
#define GIC_DIST_CLEAR_PENDING(irq, cm) (s->irq_state[irq].pending &= ~(cm))
#define GIC_DIST_SET_ACTIVE(irq, cm) (s->irq_state[irq].active |= (cm))
#define GIC_DIST_CLEAR_ACTIVE(irq, cm) (s->irq_state[irq].active &= ~(cm))
#define GIC_DIST_TEST_ACTIVE(irq, cm) ((s->irq_state[irq].active & (cm)) != 0)
#define GIC_DIST_SET_MODEL(irq) (s->irq_state[irq].model = true)
#define GIC_DIST_CLEAR_MODEL(irq) (s->irq_state[irq].model = false)
#define GIC_DIST_TEST_MODEL(irq) (s->irq_state[irq].model)
#define GIC_DIST_SET_LEVEL(irq, cm) (s->irq_state[irq].level |= (cm))
#define GIC_DIST_CLEAR_LEVEL(irq, cm) (s->irq_state[irq].level &= ~(cm))
#define GIC_DIST_TEST_LEVEL(irq, cm) ((s->irq_state[irq].level & (cm)) != 0)
#define GIC_DIST_SET_EDGE_TRIGGER(irq) (s->irq_state[irq].edge_trigger = true)
#define GIC_DIST_CLEAR_EDGE_TRIGGER(irq) \
    (s->irq_state[irq].edge_trigger = false)
#define GIC_DIST_TEST_EDGE_TRIGGER(irq) (s->irq_state[irq].edge_trigger)
#define GIC_DIST_GET_PRIORITY(irq, cpu) (((irq) < GIC_INTERNAL) ?            \
                                    s->priority1[irq][cpu] :            \
                                    s->priority2[(irq) - GIC_INTERNAL])
#define GIC_DIST_TARGET(irq) (s->irq_target[irq])
#define GIC_DIST_CLEAR_GROUP(irq, cm) (s->irq_state[irq].group &= ~(cm))
#define GIC_DIST_SET_GROUP(irq, cm) (s->irq_state[irq].group |= (cm))
#define GIC_DIST_TEST_GROUP(irq, cm) ((s->irq_state[irq].group & (cm)) != 0)

#define GICD_CTLR_EN_GRP0 (1U << 0)
#define GICD_CTLR_EN_GRP1 (1U << 1)

#define GICC_CTLR_EN_GRP0    (1U << 0)
#define GICC_CTLR_EN_GRP1    (1U << 1)
#define GICC_CTLR_ACK_CTL    (1U << 2)
#define GICC_CTLR_FIQ_EN     (1U << 3)
#define GICC_CTLR_CBPR       (1U << 4) /* GICv1: SBPR */
#define GICC_CTLR_EOIMODE    (1U << 9)
#define GICC_CTLR_EOIMODE_NS (1U << 10)

REG32(GICH_HCR, 0x0)
    FIELD(GICH_HCR, EN, 0, 1)
    FIELD(GICH_HCR, UIE, 1, 1)
    FIELD(GICH_HCR, LRENPIE, 2, 1)
    FIELD(GICH_HCR, NPIE, 3, 1)
    FIELD(GICH_HCR, VGRP0EIE, 4, 1)
    FIELD(GICH_HCR, VGRP0DIE, 5, 1)
    FIELD(GICH_HCR, VGRP1EIE, 6, 1)
    FIELD(GICH_HCR, VGRP1DIE, 7, 1)
    FIELD(GICH_HCR, EOICount, 27, 5)

#define GICH_HCR_MASK \
    (R_GICH_HCR_EN_MASK | R_GICH_HCR_UIE_MASK | \
     R_GICH_HCR_LRENPIE_MASK | R_GICH_HCR_NPIE_MASK | \
     R_GICH_HCR_VGRP0EIE_MASK | R_GICH_HCR_VGRP0DIE_MASK | \
     R_GICH_HCR_VGRP1EIE_MASK | R_GICH_HCR_VGRP1DIE_MASK | \
     R_GICH_HCR_EOICount_MASK)

REG32(GICH_VTR, 0x4)
    FIELD(GICH_VTR, ListRegs, 0, 6)
    FIELD(GICH_VTR, PREbits, 26, 3)
    FIELD(GICH_VTR, PRIbits, 29, 3)

REG32(GICH_VMCR, 0x8)
    FIELD(GICH_VMCR, VMCCtlr, 0, 10)
    FIELD(GICH_VMCR, VMABP, 18, 3)
    FIELD(GICH_VMCR, VMBP, 21, 3)
    FIELD(GICH_VMCR, VMPriMask, 27, 5)

REG32(GICH_MISR, 0x10)
    FIELD(GICH_MISR, EOI, 0, 1)
    FIELD(GICH_MISR, U, 1, 1)
    FIELD(GICH_MISR, LRENP, 2, 1)
    FIELD(GICH_MISR, NP, 3, 1)
    FIELD(GICH_MISR, VGrp0E, 4, 1)
    FIELD(GICH_MISR, VGrp0D, 5, 1)
    FIELD(GICH_MISR, VGrp1E, 6, 1)
    FIELD(GICH_MISR, VGrp1D, 7, 1)

REG32(GICH_EISR0, 0x20)
REG32(GICH_EISR1, 0x24)
REG32(GICH_ELRSR0, 0x30)
REG32(GICH_ELRSR1, 0x34)
REG32(GICH_APR, 0xf0)

REG32(GICH_LR0, 0x100)
    FIELD(GICH_LR0, VirtualID, 0, 10)
    FIELD(GICH_LR0, PhysicalID, 10, 10)
    FIELD(GICH_LR0, CPUID, 10, 3)
    FIELD(GICH_LR0, EOI, 19, 1)
    FIELD(GICH_LR0, Priority, 23, 5)
    FIELD(GICH_LR0, State, 28, 2)
    FIELD(GICH_LR0, Grp1, 30, 1)
    FIELD(GICH_LR0, HW, 31, 1)

/* Last LR register */
REG32(GICH_LR63, 0x1fc)

#define GICH_LR_MASK \
    (R_GICH_LR0_VirtualID_MASK | R_GICH_LR0_PhysicalID_MASK | \
     R_GICH_LR0_CPUID_MASK | R_GICH_LR0_EOI_MASK | \
     R_GICH_LR0_Priority_MASK | R_GICH_LR0_State_MASK | \
     R_GICH_LR0_Grp1_MASK | R_GICH_LR0_HW_MASK)

#define GICH_LR_STATE_INVALID         0
#define GICH_LR_STATE_PENDING         1
#define GICH_LR_STATE_ACTIVE          2
#define GICH_LR_STATE_ACTIVE_PENDING  3

#define GICH_LR_VIRT_ID(entry) (FIELD_EX32(entry, GICH_LR0, VirtualID))
#define GICH_LR_PHYS_ID(entry) (FIELD_EX32(entry, GICH_LR0, PhysicalID))
#define GICH_LR_CPUID(entry) (FIELD_EX32(entry, GICH_LR0, CPUID))
#define GICH_LR_EOI(entry) (FIELD_EX32(entry, GICH_LR0, EOI))
#define GICH_LR_PRIORITY(entry) (FIELD_EX32(entry, GICH_LR0, Priority) << 3)
#define GICH_LR_STATE(entry) (FIELD_EX32(entry, GICH_LR0, State))
#define GICH_LR_GROUP(entry) (FIELD_EX32(entry, GICH_LR0, Grp1))
#define GICH_LR_HW(entry) (FIELD_EX32(entry, GICH_LR0, HW))

#define GICH_LR_CLEAR_PENDING(entry) \
        ((entry) &= ~(GICH_LR_STATE_PENDING << R_GICH_LR0_State_SHIFT))
#define GICH_LR_SET_ACTIVE(entry) \
        ((entry) |= (GICH_LR_STATE_ACTIVE << R_GICH_LR0_State_SHIFT))
#define GICH_LR_CLEAR_ACTIVE(entry) \
        ((entry) &= ~(GICH_LR_STATE_ACTIVE << R_GICH_LR0_State_SHIFT))

/* Valid bits for GICC_CTLR for GICv1, v1 with security extensions,
 * GICv2 and GICv2 with security extensions:
 */
#define GICC_CTLR_V1_MASK    0x1
#define GICC_CTLR_V1_S_MASK  0x1f
#define GICC_CTLR_V2_MASK    0x21f
#define GICC_CTLR_V2_S_MASK  0x61f

/* The special cases for the revision property: */
#define REV_11MPCORE 0

uint32_t gic_acknowledge_irq(GICState *s, int cpu, MemTxAttrs attrs);
void gic_dist_set_priority(GICState *s, int cpu, int irq, uint8_t val,
                           MemTxAttrs attrs);

static inline bool gic_test_pending(GICState *s, int irq, int cm)
{
    if (s->revision == REV_11MPCORE) {
        return s->irq_state[irq].pending & cm;
    } else {
        /* Edge-triggered interrupts are marked pending on a rising edge, but
         * level-triggered interrupts are either considered pending when the
         * level is active or if software has explicitly written to
         * GICD_ISPENDR to set the state pending.
         */
        return (s->irq_state[irq].pending & cm) ||
            (!GIC_DIST_TEST_EDGE_TRIGGER(irq) && GIC_DIST_TEST_LEVEL(irq, cm));
    }
}

static inline bool gic_is_vcpu(int cpu)
{
    return cpu >= GIC_NCPU;
}

static inline int gic_get_vcpu_real_id(int cpu)
{
    return (cpu >= GIC_NCPU) ? (cpu - GIC_NCPU) : cpu;
}

/* Return true if the given vIRQ state exists in a LR and is either active or
 * pending and active.
 *
 * This function is used to check that a guest's `end of interrupt' or
 * `interrupts deactivation' request is valid, and matches with a LR of an
 * already acknowledged vIRQ (i.e. has the active bit set in its state).
 */
static inline bool gic_virq_is_valid(GICState *s, int irq, int vcpu)
{
    int cpu = gic_get_vcpu_real_id(vcpu);
    int lr_idx;

    for (lr_idx = 0; lr_idx < s->num_lrs; lr_idx++) {
        uint32_t *entry = &s->h_lr[lr_idx][cpu];

        if ((GICH_LR_VIRT_ID(*entry) == irq) &&
            (GICH_LR_STATE(*entry) & GICH_LR_STATE_ACTIVE)) {
            return true;
        }
    }

    return false;
}

/* Return a pointer on the LR entry matching the given vIRQ.
 *
 * This function is used to retrieve an LR for which we know for sure that the
 * corresponding vIRQ exists in the current context (i.e. its current state is
 * not `invalid'):
 *   - Either the corresponding vIRQ has been validated with gic_virq_is_valid()
 *     so it is `active' or `active and pending',
 *   - Or it was pending and has been selected by gic_get_best_virq(). It is now
 *     `pending', `active' or `active and pending', depending on what the guest
 *     already did with this vIRQ.
 *
 * Having multiple LRs with the same VirtualID leads to UNPREDICTABLE
 * behaviour in the GIC. We choose to return the first one that matches.
 */
static inline uint32_t *gic_get_lr_entry(GICState *s, int irq, int vcpu)
{
    int cpu = gic_get_vcpu_real_id(vcpu);
    int lr_idx;

    for (lr_idx = 0; lr_idx < s->num_lrs; lr_idx++) {
        uint32_t *entry = &s->h_lr[lr_idx][cpu];

        if ((GICH_LR_VIRT_ID(*entry) == irq) &&
            (GICH_LR_STATE(*entry) != GICH_LR_STATE_INVALID)) {
            return entry;
        }
    }

    g_assert_not_reached();
}

static inline bool gic_test_group(GICState *s, int irq, int cpu)
{
    if (gic_is_vcpu(cpu)) {
        uint32_t *entry = gic_get_lr_entry(s, irq, cpu);
        return GICH_LR_GROUP(*entry);
    } else {
        return GIC_DIST_TEST_GROUP(irq, 1 << cpu);
    }
}

static inline void gic_clear_pending(GICState *s, int irq, int cpu)
{
    if (gic_is_vcpu(cpu)) {
        uint32_t *entry = gic_get_lr_entry(s, irq, cpu);
        GICH_LR_CLEAR_PENDING(*entry);
    } else {
        /* Clear pending state for both level and edge triggered
         * interrupts. (level triggered interrupts with an active line
         * remain pending, see gic_test_pending)
         */
        GIC_DIST_CLEAR_PENDING(irq, GIC_DIST_TEST_MODEL(irq) ? ALL_CPU_MASK
                                                             : (1 << cpu));
    }
}

static inline void gic_set_active(GICState *s, int irq, int cpu)
{
    if (gic_is_vcpu(cpu)) {
        uint32_t *entry = gic_get_lr_entry(s, irq, cpu);
        GICH_LR_SET_ACTIVE(*entry);
    } else {
        GIC_DIST_SET_ACTIVE(irq, 1 << cpu);
    }
}

static inline void gic_clear_active(GICState *s, int irq, int cpu)
{
    unsigned int cm;

    if (gic_is_vcpu(cpu)) {
        uint32_t *entry = gic_get_lr_entry(s, irq, cpu);
        GICH_LR_CLEAR_ACTIVE(*entry);

        if (GICH_LR_HW(*entry)) {
            /* Hardware interrupt. We must forward the deactivation request to
             * the distributor.
             */
            int phys_irq = GICH_LR_PHYS_ID(*entry);
            int rcpu = gic_get_vcpu_real_id(cpu);

            if (phys_irq < GIC_NR_SGIS || phys_irq >= GIC_MAXIRQ) {
                /* UNPREDICTABLE behaviour, we choose to ignore the request */
                return;
            }

            /* This is equivalent to a NS write to DIR on the physical CPU
             * interface. Hence group0 interrupt deactivation is ignored if
             * the GIC is secure.
             */
            if (!s->security_extn || GIC_DIST_TEST_GROUP(phys_irq, 1 << rcpu)) {
                cm = phys_irq < GIC_INTERNAL ? 1 << rcpu : ALL_CPU_MASK;
                GIC_DIST_CLEAR_ACTIVE(phys_irq, cm);
            }
        }
    } else {
        cm = irq < GIC_INTERNAL ? 1 << cpu : ALL_CPU_MASK;
        GIC_DIST_CLEAR_ACTIVE(irq, cm);
    }
}

static inline int gic_get_priority(GICState *s, int irq, int cpu)
{
    if (gic_is_vcpu(cpu)) {
        uint32_t *entry = gic_get_lr_entry(s, irq, cpu);
        return GICH_LR_PRIORITY(*entry);
    } else {
        return GIC_DIST_GET_PRIORITY(irq, cpu);
    }
}

#endif /* QEMU_ARM_GIC_INTERNAL_H */
