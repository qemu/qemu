/*
 * Rasperry Pi 2 emulation and refactoring Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BCM2835_AUX_H
#define BCM2835_AUX_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"

#define TYPE_BCM2835_AUX "bcm2835-aux"
#define BCM2835_AUX(obj) OBJECT_CHECK(BCM2835AuxState, (obj), TYPE_BCM2835_AUX)

#define BCM2835_AUX_RX_FIFO_LEN 8

typedef struct {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion iomem;
    CharBackend chr;
    qemu_irq irq;

    uint8_t read_fifo[BCM2835_AUX_RX_FIFO_LEN];
    uint8_t read_pos, read_count;
    uint8_t ier, iir;
} BCM2835AuxState;

#endif
