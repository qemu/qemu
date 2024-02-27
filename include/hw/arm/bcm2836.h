/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * Upstreaming code cleanup [including bcm2835_*] (c) 2013 Jan Petrous
 *
 * Rasperry Pi 2 emulation and refactoring Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BCM2836_H
#define BCM2836_H

#include "hw/arm/bcm2835_peripherals.h"
#include "hw/intc/bcm2836_control.h"
#include "target/arm/cpu.h"
#include "qom/object.h"

#define TYPE_BCM283X_BASE "bcm283x-base"
OBJECT_DECLARE_TYPE(BCM283XBaseState, BCM283XBaseClass, BCM283X_BASE)
#define TYPE_BCM283X "bcm283x"
OBJECT_DECLARE_SIMPLE_TYPE(BCM283XState, BCM283X)

#define BCM283X_NCPUS 4

/* These type names are for specific SoCs; other than instantiating
 * them, code using these devices should always handle them via the
 * BCM283x base class, so they have no BCM2836(obj) etc macros.
 */
#define TYPE_BCM2835 "bcm2835"
#define TYPE_BCM2836 "bcm2836"
#define TYPE_BCM2837 "bcm2837"

struct BCM283XBaseState {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    uint32_t enabled_cpus;

    struct {
        ARMCPU core;
    } cpu[BCM283X_NCPUS];
    BCM2836ControlState control;
};

struct BCM283XBaseClass {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/
    const char *name;
    const char *cpu_type;
    unsigned core_count;
    hwaddr peri_base; /* Peripheral base address seen by the CPU */
    hwaddr ctrl_base; /* Interrupt controller and mailboxes etc. */
    int clusterid;
};

struct BCM283XState {
    /*< private >*/
    BCM283XBaseState parent_obj;
    /*< public >*/
    BCM2835PeripheralState peripherals;
};

bool bcm283x_common_realize(DeviceState *dev, BCMSocPeripheralBaseState *ps,
                            Error **errp);

#endif /* BCM2836_H */
