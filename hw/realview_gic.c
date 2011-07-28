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
    int iomemtype;
} RealViewGICState;

static uint32_t realview_gic_cpu_read(void *opaque, target_phys_addr_t offset)
{
    gic_state *s = (gic_state *)opaque;
    return gic_cpu_read(s, gic_get_current_cpu(), offset);
}

static void realview_gic_cpu_write(void *opaque, target_phys_addr_t offset,
                          uint32_t value)
{
    gic_state *s = (gic_state *)opaque;
    gic_cpu_write(s, gic_get_current_cpu(), offset, value);
}

static CPUReadMemoryFunc * const realview_gic_cpu_readfn[] = {
   realview_gic_cpu_read,
   realview_gic_cpu_read,
   realview_gic_cpu_read
};

static CPUWriteMemoryFunc * const realview_gic_cpu_writefn[] = {
   realview_gic_cpu_write,
   realview_gic_cpu_write,
   realview_gic_cpu_write
};

static void realview_gic_map(SysBusDevice *dev, target_phys_addr_t base)
{
    RealViewGICState *s = FROM_SYSBUSGIC(RealViewGICState, dev);
    cpu_register_physical_memory(base, 0x1000, s->iomemtype);
    cpu_register_physical_memory(base + 0x1000, 0x1000, s->gic.iomemtype);
}

static int realview_gic_init(SysBusDevice *dev)
{
    RealViewGICState *s = FROM_SYSBUSGIC(RealViewGICState, dev);

    gic_init(&s->gic);
    s->iomemtype = cpu_register_io_memory(realview_gic_cpu_readfn,
                                          realview_gic_cpu_writefn, s,
                                          DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio_cb(dev, 0x2000, realview_gic_map);
    return 0;
}

static void realview_gic_register_devices(void)
{
    sysbus_register_dev("realview_gic", sizeof(RealViewGICState),
                        realview_gic_init);
}

device_init(realview_gic_register_devices)
