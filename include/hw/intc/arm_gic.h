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

#ifndef HW_ARM_GIC_H
#define HW_ARM_GIC_H

#include "arm_gic_common.h"

/* Number of SGI target-list bits */
#define GIC_TARGETLIST_BITS 8

#define TYPE_ARM_GIC "arm_gic"
#define ARM_GIC(obj) \
     OBJECT_CHECK(GICState, (obj), TYPE_ARM_GIC)
#define ARM_GIC_CLASS(klass) \
     OBJECT_CLASS_CHECK(ARMGICClass, (klass), TYPE_ARM_GIC)
#define ARM_GIC_GET_CLASS(obj) \
     OBJECT_GET_CLASS(ARMGICClass, (obj), TYPE_ARM_GIC)

typedef struct ARMGICClass {
    /*< private >*/
    ARMGICCommonClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
} ARMGICClass;

#endif
