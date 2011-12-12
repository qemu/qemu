/*
 * ARM RealView Emulation Baseboard Interrupt Controller
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "sysbus.h"

#define GIC_NIRQ 96
#define NCPU 1

/* Only a single "CPU" interface is present.  */
static inline int
gic_get_current_cpu(void)
{
  return 0;
}

#include "arm_gic.c"

typedef struct {
    gic_state gic;
    MemoryRegion container;
} RealViewGICState;

static void realview_gic_map_setup(RealViewGICState *s)
{
    memory_region_init(&s->container, "realview-gic-container", 0x2000);
    memory_region_add_subregion(&s->container, 0, &s->gic.cpuiomem[0]);
    memory_region_add_subregion(&s->container, 0x1000, &s->gic.iomem);
}

static int realview_gic_init(SysBusDevice *dev)
{
    RealViewGICState *s = FROM_SYSBUSGIC(RealViewGICState, dev);

    gic_init(&s->gic);
    realview_gic_map_setup(s);
    sysbus_init_mmio(dev, &s->container);
    return 0;
}

static void realview_gic_register_devices(void)
{
    sysbus_register_dev("realview_gic", sizeof(RealViewGICState),
                        realview_gic_init);
}

device_init(realview_gic_register_devices)
