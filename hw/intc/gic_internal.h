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

#include "hw/intc/arm_gic.h"

#define ALL_CPU_MASK ((unsigned)(((1 << GIC_NCPU) - 1)))

/* The NVIC has 16 internal vectors.  However these are not exposed
   through the normal GIC interface.  */
#define GIC_BASE_IRQ ((s->revision == REV_NVIC) ? 32 : 0)

#define GIC_SET_ENABLED(irq, cm) s->irq_state[irq].enabled |= (cm)
#define GIC_CLEAR_ENABLED(irq, cm) s->irq_state[irq].enabled &= ~(cm)
#define GIC_TEST_ENABLED(irq, cm) ((s->irq_state[irq].enabled & (cm)) != 0)
#define GIC_SET_PENDING(irq, cm) s->irq_state[irq].pending |= (cm)
#define GIC_CLEAR_PENDING(irq, cm) s->irq_state[irq].pending &= ~(cm)
#define GIC_SET_ACTIVE(irq, cm) s->irq_state[irq].active |= (cm)
#define GIC_CLEAR_ACTIVE(irq, cm) s->irq_state[irq].active &= ~(cm)
#define GIC_TEST_ACTIVE(irq, cm) ((s->irq_state[irq].active & (cm)) != 0)
#define GIC_SET_MODEL(irq) s->irq_state[irq].model = true
#define GIC_CLEAR_MODEL(irq) s->irq_state[irq].model = false
#define GIC_TEST_MODEL(irq) s->irq_state[irq].model
#define GIC_SET_LEVEL(irq, cm) s->irq_state[irq].level |= (cm)
#define GIC_CLEAR_LEVEL(irq, cm) s->irq_state[irq].level &= ~(cm)
#define GIC_TEST_LEVEL(irq, cm) ((s->irq_state[irq].level & (cm)) != 0)
#define GIC_SET_EDGE_TRIGGER(irq) s->irq_state[irq].edge_trigger = true
#define GIC_CLEAR_EDGE_TRIGGER(irq) s->irq_state[irq].edge_trigger = false
#define GIC_TEST_EDGE_TRIGGER(irq) (s->irq_state[irq].edge_trigger)
#define GIC_GET_PRIORITY(irq, cpu) (((irq) < GIC_INTERNAL) ?            \
                                    s->priority1[irq][cpu] :            \
                                    s->priority2[(irq) - GIC_INTERNAL])
#define GIC_TARGET(irq) s->irq_target[irq]
#define GIC_CLEAR_GROUP(irq, cm) (s->irq_state[irq].group &= ~(cm))
#define GIC_SET_GROUP(irq, cm) (s->irq_state[irq].group |= (cm))
#define GIC_TEST_GROUP(irq, cm) ((s->irq_state[irq].group & (cm)) != 0)

#define GICD_CTLR_EN_GRP0 (1U << 0)
#define GICD_CTLR_EN_GRP1 (1U << 1)

#define GICC_CTLR_EN_GRP0    (1U << 0)
#define GICC_CTLR_EN_GRP1    (1U << 1)
#define GICC_CTLR_ACK_CTL    (1U << 2)
#define GICC_CTLR_FIQ_EN     (1U << 3)
#define GICC_CTLR_CBPR       (1U << 4) /* GICv1: SBPR */
#define GICC_CTLR_EOIMODE    (1U << 9)
#define GICC_CTLR_EOIMODE_NS (1U << 10)

/* Valid bits for GICC_CTLR for GICv1, v1 with security extensions,
 * GICv2 and GICv2 with security extensions:
 */
#define GICC_CTLR_V1_MASK    0x1
#define GICC_CTLR_V1_S_MASK  0x1f
#define GICC_CTLR_V2_MASK    0x21f
#define GICC_CTLR_V2_S_MASK  0x61f

/* The special cases for the revision property: */
#define REV_11MPCORE 0
#define REV_NVIC 0xffffffff

void gic_set_pending_private(GICState *s, int cpu, int irq);
uint32_t gic_acknowledge_irq(GICState *s, int cpu, MemTxAttrs attrs);
void gic_complete_irq(GICState *s, int cpu, int irq, MemTxAttrs attrs);
void gic_update(GICState *s);
void gic_init_irqs_and_distributor(GICState *s);
void gic_set_priority(GICState *s, int cpu, int irq, uint8_t val,
                      MemTxAttrs attrs);

static inline bool gic_test_pending(GICState *s, int irq, int cm)
{
    if (s->revision == REV_NVIC || s->revision == REV_11MPCORE) {
        return s->irq_state[irq].pending & cm;
    } else {
        /* Edge-triggered interrupts are marked pending on a rising edge, but
         * level-triggered interrupts are either considered pending when the
         * level is active or if software has explicitly written to
         * GICD_ISPENDR to set the state pending.
         */
        return (s->irq_state[irq].pending & cm) ||
            (!GIC_TEST_EDGE_TRIGGER(irq) && GIC_TEST_LEVEL(irq, cm));
    }
}

#endif /* !QEMU_ARM_GIC_INTERNAL_H */
