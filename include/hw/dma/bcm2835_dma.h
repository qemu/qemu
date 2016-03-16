/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * This code is licensed under the GNU GPLv2 and later.
 */

#ifndef BCM2835_DMA_H
#define BCM2835_DMA_H

#include "qemu-common.h"
#include "exec/address-spaces.h"
#include "hw/sysbus.h"

typedef struct {
    uint32_t cs;
    uint32_t conblk_ad;
    uint32_t ti;
    uint32_t source_ad;
    uint32_t dest_ad;
    uint32_t txfr_len;
    uint32_t stride;
    uint32_t nextconbk;
    uint32_t debug;

    qemu_irq irq;
} BCM2835DMAChan;

#define TYPE_BCM2835_DMA "bcm2835-dma"
#define BCM2835_DMA(obj) \
        OBJECT_CHECK(BCM2835DMAState, (obj), TYPE_BCM2835_DMA)

#define BCM2835_DMA_NCHANS 16

typedef struct {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    MemoryRegion iomem0, iomem15;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;

    BCM2835DMAChan chan[BCM2835_DMA_NCHANS];
    uint32_t int_status;
    uint32_t enable;
} BCM2835DMAState;

#endif
