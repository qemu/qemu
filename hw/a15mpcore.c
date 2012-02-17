/*
 * Cortex-A15MPCore internal peripheral emulation.
 *
 * Copyright (c) 2012 Linaro Limited.
 * Written by Peter Maydell.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "sysbus.h"

/* Configuration for arm_gic.c:
 * max number of CPUs, how to ID current CPU
 */
#define NCPU 4

static inline int gic_get_current_cpu(void)
{
  return cpu_single_env->cpu_index;
}

#include "arm_gic.c"

/* A15MP private memory region.  */

typedef struct A15MPPrivState {
    gic_state gic;
    uint32_t num_cpu;
    uint32_t num_irq;
    MemoryRegion container;
} A15MPPrivState;

static int a15mp_priv_init(SysBusDevice *dev)
{
    A15MPPrivState *s = FROM_SYSBUSGIC(A15MPPrivState, dev);

    if (s->num_cpu > NCPU) {
        hw_error("a15mp_priv_init: num-cpu may not be more than %d\n", NCPU);
    }

    gic_init(&s->gic, s->num_cpu, s->num_irq);

    /* Memory map (addresses are offsets from PERIPHBASE):
     *  0x0000-0x0fff -- reserved
     *  0x1000-0x1fff -- GIC Distributor
     *  0x2000-0x2fff -- GIC CPU interface
     *  0x4000-0x4fff -- GIC virtual interface control (not modelled)
     *  0x5000-0x5fff -- GIC virtual interface control (not modelled)
     *  0x6000-0x7fff -- GIC virtual CPU interface (not modelled)
     */
    memory_region_init(&s->container, "a15mp-priv-container", 0x8000);
    memory_region_add_subregion(&s->container, 0x1000, &s->gic.iomem);
    memory_region_add_subregion(&s->container, 0x2000, &s->gic.cpuiomem[0]);

    sysbus_init_mmio(dev, &s->container);
    return 0;
}

static Property a15mp_priv_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", A15MPPrivState, num_cpu, 1),
    /* The Cortex-A15MP may have anything from 0 to 224 external interrupt
     * IRQ lines (with another 32 internal). We default to 64+32, which
     * is the number provided by the Cortex-A15MP test chip in the
     * Versatile Express A15 development board.
     * Other boards may differ and should set this property appropriately.
     */
    DEFINE_PROP_UINT32("num-irq", A15MPPrivState, num_irq, 96),
    DEFINE_PROP_END_OF_LIST(),
};

static void a15mp_priv_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    k->init = a15mp_priv_init;
    dc->props = a15mp_priv_properties;
    /* We currently have no savable state outside the common GIC state */
}

static TypeInfo a15mp_priv_info = {
    .name  = "a15mpcore_priv",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(A15MPPrivState),
    .class_init = a15mp_priv_class_init,
};

static void a15mp_register_types(void)
{
    type_register_static(&a15mp_priv_info);
}

type_init(a15mp_register_types)
