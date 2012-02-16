/*
 * ARM RealView Emulation Baseboard Interrupt Controller
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "sysbus.h"

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

    /* The GICs on the RealView boards have a fixed nonconfigurable
     * number of interrupt lines, so we don't need to expose this as
     * a qdev property.
     */
    gic_init(&s->gic, 96);
    realview_gic_map_setup(s);
    sysbus_init_mmio(dev, &s->container);
    return 0;
}

static void realview_gic_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = realview_gic_init;
}

static TypeInfo realview_gic_info = {
    .name          = "realview_gic",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RealViewGICState),
    .class_init    = realview_gic_class_init,
};

static void realview_gic_register_types(void)
{
    type_register_static(&realview_gic_info);
}

type_init(realview_gic_register_types)
