/*
 * BCM2838 peripherals emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BCM2838_PERIPHERALS_H
#define BCM2838_PERIPHERALS_H

#include "hw/arm/bcm2835_peripherals.h"


#define TYPE_BCM2838_PERIPHERALS "bcm2838-peripherals"
OBJECT_DECLARE_TYPE(BCM2838PeripheralState, BCM2838PeripheralClass,
                    BCM2838_PERIPHERALS)

struct BCM2838PeripheralState {
    /*< private >*/
    BCMSocPeripheralBaseState parent_obj;

    /*< public >*/
    MemoryRegion peri_low_mr;
    MemoryRegion peri_low_mr_alias;
    MemoryRegion mphi_mr_alias;
};

struct BCM2838PeripheralClass {
    /*< private >*/
    BCMSocPeripheralBaseClass parent_class;
    /*< public >*/
    uint64_t peri_low_size; /* Peripheral lower range size */
};

#endif /* BCM2838_PERIPHERALS_H */
