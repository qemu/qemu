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
#include "hw/char/bcm2835_aux.h"
#include "hw/display/bcm2835_fb.h"
#include "hw/dma/bcm2835_dma.h"
#include "hw/intc/bcm2835_ic.h"
#include "hw/misc/bcm2835_property.h"
#include "hw/misc/bcm2835_rng.h"
#include "hw/misc/bcm2835_mbox.h"
#include "hw/sd/sdhci.h"
#include "hw/sd/bcm2835_sdhost.h"
#include "hw/gpio/bcm2835_gpio.h"

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
    BCM2835AuxState aux;
    BCM2835FBState fb;
    BCM2835DMAState dma;
    BCM2835ICState ic;
    BCM2835PropertyState property;
    BCM2835RngState rng;
    BCM2835MboxState mboxes;
    SDHCIState sdhci;
    BCM2835SDHostState sdhost;
    BCM2835GpioState gpio;
} BCM2835PeripheralState;

#endif /* BCM2835_PERIPHERALS_H */
