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
#include "qom/object.h"

#define TYPE_ARM_GICV3 "arm-gicv3"
typedef struct ARMGICv3Class ARMGICv3Class;
/* This is reusing the GICState typedef from TYPE_ARM_GICV3_COMMON */
DECLARE_OBJ_CHECKERS(GICv3State, ARMGICv3Class,
                     ARM_GICV3, TYPE_ARM_GICV3)

struct ARMGICv3Class {
    /*< private >*/
    ARMGICv3CommonClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
};

#endif
