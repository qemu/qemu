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

#ifndef BCM2835_PERIPHERALS_H
#define BCM2835_PERIPHERALS_H

#include "hw/sysbus.h"
#include "hw/char/pl011.h"
#include "hw/char/bcm2835_aux.h"
#include "hw/display/bcm2835_fb.h"
#include "hw/dma/bcm2835_dma.h"
#include "hw/intc/bcm2835_ic.h"
#include "hw/misc/bcm2835_property.h"
#include "hw/misc/bcm2835_rng.h"
#include "hw/misc/bcm2835_mbox.h"
#include "hw/misc/bcm2835_mphi.h"
#include "hw/misc/bcm2835_thermal.h"
#include "hw/misc/bcm2835_cprman.h"
#include "hw/misc/bcm2835_powermgt.h"
#include "hw/sd/sdhci.h"
#include "hw/sd/bcm2835_sdhost.h"
#include "hw/gpio/bcm2835_gpio.h"
#include "hw/timer/bcm2835_systmr.h"
#include "hw/usb/hcd-dwc2.h"
#include "hw/misc/unimp.h"
#include "qom/object.h"

#define TYPE_BCM2835_PERIPHERALS "bcm2835-peripherals"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2835PeripheralState, BCM2835_PERIPHERALS)

struct BCM2835PeripheralState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion peri_mr, peri_mr_alias, gpu_bus_mr, mbox_mr;
    MemoryRegion ram_alias[4];
    qemu_irq irq, fiq;

    BCM2835SystemTimerState systmr;
    BCM2835MphiState mphi;
    UnimplementedDeviceState txp;
    UnimplementedDeviceState armtmr;
    BCM2835PowerMgtState powermgt;
    BCM2835CprmanState cprman;
    PL011State uart0;
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
    Bcm2835ThermalState thermal;
    UnimplementedDeviceState i2s;
    UnimplementedDeviceState spi[1];
    UnimplementedDeviceState i2c[3];
    UnimplementedDeviceState otp;
    UnimplementedDeviceState dbus;
    UnimplementedDeviceState ave0;
    UnimplementedDeviceState v3d;
    UnimplementedDeviceState bscsl;
    UnimplementedDeviceState smi;
    DWC2State dwc2;
    UnimplementedDeviceState sdramc;
};

#endif /* BCM2835_PERIPHERALS_H */
