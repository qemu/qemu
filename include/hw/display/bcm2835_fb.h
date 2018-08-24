/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * Upstreaming code cleanup [including bcm2835_*] (c) 2013 Jan Petrous
 *
 * Rasperry Pi 2 emulation and refactoring Copyright (c) 2015, Microsoft
 * Written by Andrew Baumann
 *
 * This code is licensed under the GNU GPLv2 and later.
 */

#ifndef BCM2835_FB_H
#define BCM2835_FB_H

#include "hw/sysbus.h"
#include "ui/console.h"

#define TYPE_BCM2835_FB "bcm2835-fb"
#define BCM2835_FB(obj) OBJECT_CHECK(BCM2835FBState, (obj), TYPE_BCM2835_FB)

/*
 * Configuration information about the fb which the guest can program
 * via the mailbox property interface.
 */
typedef struct {
    uint32_t xres, yres;
    uint32_t xres_virtual, yres_virtual;
    uint32_t xoffset, yoffset;
    uint32_t bpp;
    uint32_t base;
    uint32_t pixo;
    uint32_t alpha;
} BCM2835FBConfig;

typedef struct {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    uint32_t vcram_base, vcram_size;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    MemoryRegion iomem;
    MemoryRegionSection fbsection;
    QemuConsole *con;
    qemu_irq mbox_irq;

    bool lock, invalidate, pending;

    BCM2835FBConfig config;

    /* These are just cached values calculated from the config settings */
    uint32_t size;
    uint32_t pitch;
} BCM2835FBState;

void bcm2835_fb_reconfigure(BCM2835FBState *s, uint32_t *xres, uint32_t *yres,
                            uint32_t *xoffset, uint32_t *yoffset, uint32_t *bpp,
                            uint32_t *pixo, uint32_t *alpha);

#endif
