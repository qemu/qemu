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

/*
 * QEMU interface:
 *  + QOM property "num-cpu": number of CPUs to support
 *  + QOM property "num-irq": number of IRQs (including both SPIs and PPIs)
 *  + QOM property "revision": GIC version (1 or 2), or 0 for the 11MPCore GIC
 *  + QOM property "has-security-extensions": set true if the GIC should
 *    implement the security extensions
 *  + QOM property "has-virtualization-extensions": set true if the GIC should
 *    implement the virtualization extensions
 *  + QOM property "first-cpu-index": index of the first cpu attached to the
 *    GIC (default 0). The CPUs connected to the GIC are assumed to be
 *    first-cpu-index, first-cpu-index + 1, ... first-cpu-index + num-cpu - 1.
 *  + unnamed GPIO inputs: (where P is number of SPIs, i.e. num-irq - 32)
 *    [0..P-1]  SPIs
 *    [P..P+31] PPIs for CPU 0
 *    [P+32..P+63] PPIs for CPU 1
 *    ...
 *  + sysbus IRQs: (in order; number will vary depending on number of cores)
 *    - IRQ for CPU 0
 *    - IRQ for CPU 1
 *      ...
 *    - FIQ for CPU 0
 *    - FIQ for CPU 1
 *      ...
 *    - VIRQ for CPU 0 (exists even if virt extensions not present)
 *    - VIRQ for CPU 1 (exists even if virt extensions not present)
 *      ...
 *    - VFIQ for CPU 0 (exists even if virt extensions not present)
 *    - VFIQ for CPU 1 (exists even if virt extensions not present)
 *      ...
 *    - maintenance IRQ for CPU i/f 0 (only if virt extensions present)
 *    - maintenance IRQ for CPU i/f 1 (only if virt extensions present)
 *  + sysbus MMIO regions: (in order; numbers will vary depending on
 *    whether virtualization extensions are present and on number of cores)
 *    - distributor registers (GICD*)
 *    - CPU interface for the accessing core (GICC*)
 *    - virtual interface control registers (GICH*) (only if virt extns present)
 *    - virtual CPU interface for the accessing core (GICV*) (only if virt)
 *    - CPU 0 CPU interface registers
 *    - CPU 1 CPU interface registers
 *      ...
 *    - CPU 0 virtual interface control registers (only if virt extns present)
 *    - CPU 1 virtual interface control registers (only if virt extns present)
 *      ...
 */

#ifndef HW_ARM_GIC_H
#define HW_ARM_GIC_H

#include "arm_gic_common.h"
#include "qom/object.h"

/* Number of SGI target-list bits */
#define GIC_TARGETLIST_BITS 8
#define GIC_MAX_PRIORITY_BITS 8
#define GIC_MIN_PRIORITY_BITS 4

#define TYPE_ARM_GIC "arm_gic"
typedef struct ARMGICClass ARMGICClass;
/* This is reusing the GICState typedef from TYPE_ARM_GIC_COMMON */
DECLARE_OBJ_CHECKERS(GICState, ARMGICClass,
                     ARM_GIC, TYPE_ARM_GIC)

struct ARMGICClass {
    /*< private >*/
    ARMGICCommonClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
};

const char *gic_class_name(void);

#endif
