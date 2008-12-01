/*
 * ARM RealView Emulation Baseboard Interrupt Controller
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

#include "hw.h"
#include "primecell.h"

#define GIC_NIRQ 96
#define NCPU 1

/* Only a single "CPU" interface is present.  */
static inline int
gic_get_current_cpu(void)
{
  return 0;
}

#include "arm_gic.c"

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

static CPUReadMemoryFunc *realview_gic_cpu_readfn[] = {
   realview_gic_cpu_read,
   realview_gic_cpu_read,
   realview_gic_cpu_read
};

static CPUWriteMemoryFunc *realview_gic_cpu_writefn[] = {
   realview_gic_cpu_write,
   realview_gic_cpu_write,
   realview_gic_cpu_write
};

qemu_irq *realview_gic_init(uint32_t base, qemu_irq parent_irq)
{
    gic_state *s;
    int iomemtype;

    s = gic_init(base + 0x1000, &parent_irq);
    if (!s)
        return NULL;
    iomemtype = cpu_register_io_memory(0, realview_gic_cpu_readfn,
                                       realview_gic_cpu_writefn, s);
    cpu_register_physical_memory(base, 0x00001000, iomemtype);
    return s->in;
}
