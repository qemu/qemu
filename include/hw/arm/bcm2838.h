/*
 * BCM2838 SoC emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BCM2838_H
#define BCM2838_H

#include "hw/arm/bcm2836.h"
#include "hw/intc/arm_gic.h"
#include "hw/arm/bcm2838_peripherals.h"

#define BCM2838_PERI_LOW_BASE 0xfc000000
#define BCM2838_GIC_BASE 0x40000

#define TYPE_BCM2838 "bcm2838"

OBJECT_DECLARE_TYPE(BCM2838State, BCM2838Class, BCM2838)

struct BCM2838State {
    /*< private >*/
    BCM283XBaseState parent_obj;
    /*< public >*/
    BCM2838PeripheralState peripherals;
    GICState gic;
};

#endif /* BCM2838_H */
