/*
 * ARM Generic Interrupt Controller v3
 *
 * Copyright (c) 2015 Huawei.
 * Copyright (c) 2016 Linaro Limited
 * Written by Shlomo Pongratz, Peter Maydell
 *
 * This code is licensed under the GPL, version 2 or (at your option)
 * any later version.
 */

#ifndef HW_ARM_GICV3_H
#define HW_ARM_GICV3_H

#include "arm_gicv3_common.h"

#define TYPE_ARM_GICV3 "arm-gicv3"
#define ARM_GICV3(obj) OBJECT_CHECK(GICv3State, (obj), TYPE_ARM_GICV3)
#define ARM_GICV3_CLASS(klass) \
     OBJECT_CLASS_CHECK(ARMGICv3Class, (klass), TYPE_ARM_GICV3)
#define ARM_GICV3_GET_CLASS(obj) \
     OBJECT_GET_CLASS(ARMGICv3Class, (obj), TYPE_ARM_GICV3)

typedef struct ARMGICv3Class {
    /*< private >*/
    ARMGICv3CommonClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
} ARMGICv3Class;

#endif
