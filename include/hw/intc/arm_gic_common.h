/*
 * ARM GIC support
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

#ifndef HW_ARM_GIC_COMMON_H
#define HW_ARM_GIC_COMMON_H

#include "hw/sysbus.h"

/* Maximum number of possible interrupts, determined by the GIC architecture */
#define GIC_MAXIRQ 1020
/* First 32 are private to each CPU (SGIs and PPIs). */
#define GIC_INTERNAL 32
/* Maximum number of possible CPU interfaces, determined by GIC architecture */
#define GIC_NCPU 8

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
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    qemu_irq parent_irq[GIC_NCPU];
    bool enabled;
    bool cpu_enabled[GIC_NCPU];

    gic_irq_state irq_state[GIC_MAXIRQ];
    uint8_t irq_target[GIC_MAXIRQ];
    uint8_t priority1[GIC_INTERNAL][GIC_NCPU];
    uint8_t priority2[GIC_MAXIRQ - GIC_INTERNAL];
    uint16_t last_active[GIC_MAXIRQ][GIC_NCPU];

    uint16_t priority_mask[GIC_NCPU];
    uint16_t running_irq[GIC_NCPU];
    uint16_t running_priority[GIC_NCPU];
    uint16_t current_pending[GIC_NCPU];

    uint32_t num_cpu;

    MemoryRegion iomem; /* Distributor */
    /* This is just so we can have an opaque pointer which identifies
     * both this GIC and which CPU interface we should be accessing.
     */
    struct GICState *backref[GIC_NCPU];
    MemoryRegion cpuiomem[GIC_NCPU + 1]; /* CPU interfaces */
    uint32_t num_irq;
    uint32_t revision;
} GICState;

#define TYPE_ARM_GIC_COMMON "arm_gic_common"
#define ARM_GIC_COMMON(obj) \
     OBJECT_CHECK(GICState, (obj), TYPE_ARM_GIC_COMMON)
#define ARM_GIC_COMMON_CLASS(klass) \
     OBJECT_CLASS_CHECK(ARMGICCommonClass, (klass), TYPE_ARM_GIC_COMMON)
#define ARM_GIC_COMMON_GET_CLASS(obj) \
     OBJECT_GET_CLASS(ARMGICCommonClass, (obj), TYPE_ARM_GIC_COMMON)

typedef struct ARMGICCommonClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/

    void (*pre_save)(GICState *s);
    void (*post_load)(GICState *s);
} ARMGICCommonClass;

#endif
