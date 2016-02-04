/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * Upstreaming code cleanup [including bcm2835_*] (c) 2013 Jan Petrous
 *
 * Rasperry Pi 2 emulation and refactoring Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * This code is licensed under the GNU GPLv2 and later.
 */

#ifndef BCM2835_PERIPHERALS_H
#define BCM2835_PERIPHERALS_H

#include "qemu-common.h"
#include "exec/address-spaces.h"
#include "hw/sysbus.h"
#include "hw/intc/bcm2835_ic.h"
#include "hw/misc/bcm2835_property.h"
#include "hw/misc/bcm2835_mbox.h"
#include "hw/sd/sdhci.h"

#define TYPE_BCM2835_PERIPHERALS "bcm2835-peripherals"
#define BCM2835_PERIPHERALS(obj) \
    OBJECT_CHECK(BCM2835PeripheralState, (obj), TYPE_BCM2835_PERIPHERALS)

typedef struct BCM2835PeripheralState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion peri_mr, peri_mr_alias, gpu_bus_mr, mbox_mr;
    MemoryRegion ram_alias[4];
    qemu_irq irq, fiq;

    SysBusDevice *uart0;
    BCM2835ICState ic;
    BCM2835PropertyState property;
    BCM2835MboxState mboxes;
    SDHCIState sdhci;
} BCM2835PeripheralState;

#endif /* BCM2835_PERIPHERALS_H */
