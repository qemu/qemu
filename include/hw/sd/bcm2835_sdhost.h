/*
 * Raspberry Pi (BCM2835) SD Host Controller
 *
 * Copyright (c) 2017 Antfield SAS
 *
 * Authors:
 *  Clement Deschamps <clement.deschamps@antfield.fr>
 *  Luc Michel <luc.michel@antfield.fr>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BCM2835_SDHOST_H
#define BCM2835_SDHOST_H

#include "hw/sysbus.h"
#include "hw/sd/sd.h"
#include "qom/object.h"

#define TYPE_BCM2835_SDHOST "bcm2835-sdhost"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2835SDHostState, BCM2835_SDHOST)

#define BCM2835_SDHOST_FIFO_LEN 16

struct BCM2835SDHostState {
    SysBusDevice busdev;
    SDBus sdbus;
    MemoryRegion iomem;

    uint32_t cmd;
    uint32_t cmdarg;
    uint32_t status;
    uint32_t rsp[4];
    uint32_t config;
    uint32_t edm;
    uint32_t vdd;
    uint32_t hbct;
    uint32_t hblc;
    int32_t fifo_pos;
    int32_t fifo_len;
    uint32_t fifo[BCM2835_SDHOST_FIFO_LEN];
    uint32_t datacnt;

    qemu_irq irq;
};

#endif
