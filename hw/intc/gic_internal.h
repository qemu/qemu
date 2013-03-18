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

#include "hw/sysbus.h"

/* Maximum number of possible interrupts, determined by the GIC architecture */
#define GIC_MAXIRQ 1020
/* First 32 are private to each CPU (SGIs and PPIs). */
#define GIC_INTERNAL 32
/* Maximum number of possible CPU interfaces, determined by GIC architecture */
#define NCPU 8

#define ALL_CPU_MASK ((unsigned)(((1 << NCPU) - 1)))

/* The NVIC has 16 internal vectors.  However these are not exposed
   through the normal GIC interface.  */
#define GIC_BASE_IRQ ((s->revision == REV_NVIC) ? 32 : 0)

#define GIC_SET_ENABLED(irq, cm) s->irq_state[irq].enabled |= (cm)
#define GIC_CLEAR_ENABLED(irq, cm) s->irq_state[irq].enabled &= ~(cm)
#define GIC_TEST_ENABLED(irq, cm) ((s->irq_state[irq].enabled & (cm)) != 0)
#define GIC_SET_PENDING(irq, cm) s->irq_state[irq].pending |= (cm)
#define GIC_CLEAR_PENDING(irq, cm) s->irq_state[irq].pending &= ~(cm)
#define GIC_TEST_PENDING(irq, cm) ((s->irq_state[irq].pending & (cm)) != 0)
#define GIC_SET_ACTIVE(irq, cm) s->irq_state[irq].active |= (cm)
#define GIC_CLEAR_ACTIVE(irq, cm) s->irq_state[irq].active &= ~(cm)
#define GIC_TEST_ACTIVE(irq, cm) ((s->irq_state[irq].active & (cm)) != 0)
#define GIC_SET_MODEL(irq) s->irq_state[irq].model = true
#define GIC_CLEAR_MODEL(irq) s->irq_state[irq].model = false
#define GIC_TEST_MODEL(irq) s->irq_state[irq].model
#define GIC_SET_LEVEL(irq, cm) s->irq_state[irq].level = (cm)
#define GIC_CLEAR_LEVEL(irq, cm) s->irq_state[irq].level &= ~(cm)
#define GIC_TEST_LEVEL(irq, cm) ((s->irq_state[irq].level & (cm)) != 0)
#define GIC_SET_TRIGGER(irq) s->irq_state[irq].trigger = true
#define GIC_CLEAR_TRIGGER(irq) s->irq_state[irq].trigger = false
#define GIC_TEST_TRIGGER(irq) s->irq_state[irq].trigger
#define GIC_GET_PRIORITY(irq, cpu) (((irq) < GIC_INTERNAL) ?            \
                                    s->priority1[irq][cpu] :            \
                                    s->priority2[(irq) - GIC_INTERNAL])
#define GIC_TARGET(irq) s->irq_target[irq]

typedef struct gic_irq_state {
    /* The enable bits are only banked for per-cpu interrupts.  */
    uint8_t enabled;
    uint8_t pending;
    uint8_t active;
    uint8_t level;
    bool model; /* 0 = N:N, 1 = 1:N */
    bool trigger; /* nonzero = edge triggered.  */
} gic_irq_state;

typedef struct GICState {
    SysBusDevice busdev;
    qemu_irq parent_irq[NCPU];
    bool enabled;
    bool cpu_enabled[NCPU];

    gic_irq_state irq_state[GIC_MAXIRQ];
    uint8_t irq_target[GIC_MAXIRQ];
    uint8_t priority1[GIC_INTERNAL][NCPU];
    uint8_t priority2[GIC_MAXIRQ - GIC_INTERNAL];
    uint16_t last_active[GIC_MAXIRQ][NCPU];

    uint16_t priority_mask[NCPU];
    uint16_t running_irq[NCPU];
    uint16_t running_priority[NCPU];
    uint16_t current_pending[NCPU];

    uint32_t num_cpu;

    MemoryRegion iomem; /* Distributor */
    /* This is just so we can have an opaque pointer which identifies
     * both this GIC and which CPU interface we should be accessing.
     */
    struct GICState *backref[NCPU];
    MemoryRegion cpuiomem[NCPU+1]; /* CPU interfaces */
    uint32_t num_irq;
    uint32_t revision;
} GICState;

/* The special cases for the revision property: */
#define REV_11MPCORE 0
#define REV_NVIC 0xffffffff

void gic_set_pending_private(GICState *s, int cpu, int irq);
uint32_t gic_acknowledge_irq(GICState *s, int cpu);
void gic_complete_irq(GICState *s, int cpu, int irq);
void gic_update(GICState *s);
void gic_init_irqs_and_distributor(GICState *s, int num_irq);

#define TYPE_ARM_GIC_COMMON "arm_gic_common"
#define ARM_GIC_COMMON(obj) \
     OBJECT_CHECK(GICState, (obj), TYPE_ARM_GIC_COMMON)
#define ARM_GIC_COMMON_CLASS(klass) \
     OBJECT_CLASS_CHECK(ARMGICCommonClass, (klass), TYPE_ARM_GIC_COMMON)
#define ARM_GIC_COMMON_GET_CLASS(obj) \
     OBJECT_GET_CLASS(ARMGICCommonClass, (obj), TYPE_ARM_GIC_COMMON)

typedef struct ARMGICCommonClass {
    SysBusDeviceClass parent_class;
    void (*pre_save)(GICState *s);
    void (*post_load)(GICState *s);
} ARMGICCommonClass;

#define TYPE_ARM_GIC "arm_gic"
#define ARM_GIC(obj) \
     OBJECT_CHECK(GICState, (obj), TYPE_ARM_GIC)
#define ARM_GIC_CLASS(klass) \
     OBJECT_CLASS_CHECK(ARMGICClass, (klass), TYPE_ARM_GIC)
#define ARM_GIC_GET_CLASS(obj) \
     OBJECT_GET_CLASS(ARMGICClass, (obj), TYPE_ARM_GIC)

typedef struct ARMGICClass {
    ARMGICCommonClass parent_class;
    DeviceRealize parent_realize;
} ARMGICClass;

#endif /* !QEMU_ARM_GIC_INTERNAL_H */
