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
    BCM2835FBConfig initial_config;
} BCM2835FBState;

void bcm2835_fb_reconfigure(BCM2835FBState *s, BCM2835FBConfig *newconfig);

/**
 * bcm2835_fb_get_pitch: return number of bytes per line of the framebuffer
 * @config: configuration info for the framebuffer
 *
 * Return the number of bytes per line of the framebuffer, ie the number
 * that must be added to a pixel address to get the address of the pixel
 * directly below it on screen.
 */
static inline uint32_t bcm2835_fb_get_pitch(BCM2835FBConfig *config)
{
    uint32_t xres = MAX(config->xres, config->xres_virtual);
    return xres * (config->bpp >> 3);
}

/**
 * bcm2835_fb_get_size: return total size of framebuffer in bytes
 * @config: configuration info for the framebuffer
 */
static inline uint32_t bcm2835_fb_get_size(BCM2835FBConfig *config)
{
    uint32_t yres = MAX(config->yres, config->yres_virtual);
    return yres * bcm2835_fb_get_pitch(config);
}

/**
 * bcm2835_fb_validate_config: check provided config
 *
 * Validates the configuration information provided by the guest and
 * adjusts it if necessary.
 */
void bcm2835_fb_validate_config(BCM2835FBConfig *config);

#endif
