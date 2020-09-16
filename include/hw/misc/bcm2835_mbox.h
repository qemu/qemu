/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BCM2835_MBOX_H
#define BCM2835_MBOX_H

#include "bcm2835_mbox_defs.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_BCM2835_MBOX "bcm2835-mbox"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2835MboxState, BCM2835_MBOX)

typedef struct {
    uint32_t reg[MBOX_SIZE];
    uint32_t count;
    uint32_t status;
    uint32_t config;
} BCM2835Mbox;

struct BCM2835MboxState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/
    MemoryRegion *mbox_mr;
    AddressSpace mbox_as;
    MemoryRegion iomem;
    qemu_irq arm_irq;

    bool mbox_irq_disabled;
    bool available[MBOX_CHAN_COUNT];
    BCM2835Mbox mbox[2];
};

#endif
