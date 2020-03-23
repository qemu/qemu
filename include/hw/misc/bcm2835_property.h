/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BCM2835_PROPERTY_H
#define BCM2835_PROPERTY_H

#include "hw/sysbus.h"
#include "net/net.h"
#include "hw/display/bcm2835_fb.h"

#define TYPE_BCM2835_PROPERTY "bcm2835-property"
#define BCM2835_PROPERTY(obj) \
        OBJECT_CHECK(BCM2835PropertyState, (obj), TYPE_BCM2835_PROPERTY)

typedef struct {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    MemoryRegion iomem;
    qemu_irq mbox_irq;
    BCM2835FBState *fbdev;

    MACAddr macaddr;
    uint32_t board_rev;
    uint32_t addr;
    bool pending;
} BCM2835PropertyState;

#endif
