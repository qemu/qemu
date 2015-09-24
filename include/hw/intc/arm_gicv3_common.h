/*
 * ARM GIC support
 *
 * Copyright (c) 2012 Linaro Limited
 * Copyright (c) 2015 Huawei.
 * Written by Peter Maydell
 * Extended to 64 cores by Shlomo Pongratz
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

#ifndef HW_ARM_GICV3_COMMON_H
#define HW_ARM_GICV3_COMMON_H

#include "hw/sysbus.h"
#include "hw/intc/arm_gic_common.h"

typedef struct GICv3State {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    qemu_irq *parent_irq;
    qemu_irq *parent_fiq;

    MemoryRegion iomem_dist; /* Distributor */
    MemoryRegion iomem_redist; /* Redistributors */

    uint32_t num_cpu;
    uint32_t num_irq;
    uint32_t revision;
    bool security_extn;

    int dev_fd; /* kvm device fd if backed by kvm vgic support */
} GICv3State;

#define TYPE_ARM_GICV3_COMMON "arm-gicv3-common"
#define ARM_GICV3_COMMON(obj) \
     OBJECT_CHECK(GICv3State, (obj), TYPE_ARM_GICV3_COMMON)
#define ARM_GICV3_COMMON_CLASS(klass) \
     OBJECT_CLASS_CHECK(ARMGICv3CommonClass, (klass), TYPE_ARM_GICV3_COMMON)
#define ARM_GICV3_COMMON_GET_CLASS(obj) \
     OBJECT_GET_CLASS(ARMGICv3CommonClass, (obj), TYPE_ARM_GICV3_COMMON)

typedef struct ARMGICv3CommonClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/

    void (*pre_save)(GICv3State *s);
    void (*post_load)(GICv3State *s);
} ARMGICv3CommonClass;

void gicv3_init_irqs_and_mmio(GICv3State *s, qemu_irq_handler handler,
                              const MemoryRegionOps *ops);

#endif
