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

#include "hw/sysbus.h"
#include "sysemu/kvm.h"

/* A15MP private memory region.  */

typedef struct A15MPPrivState {
    SysBusDevice busdev;
    uint32_t num_cpu;
    uint32_t num_irq;
    MemoryRegion container;
    DeviceState *gic;
} A15MPPrivState;

static void a15mp_priv_set_irq(void *opaque, int irq, int level)
{
    A15MPPrivState *s = (A15MPPrivState *)opaque;
    qemu_set_irq(qdev_get_gpio_in(s->gic, irq), level);
}

static int a15mp_priv_init(SysBusDevice *dev)
{
    A15MPPrivState *s = FROM_SYSBUS(A15MPPrivState, dev);
    SysBusDevice *busdev;
    const char *gictype = "arm_gic";

    if (kvm_irqchip_in_kernel()) {
        gictype = "kvm-arm-gic";
    }

    s->gic = qdev_create(NULL, gictype);
    qdev_prop_set_uint32(s->gic, "num-cpu", s->num_cpu);
    qdev_prop_set_uint32(s->gic, "num-irq", s->num_irq);
    qdev_prop_set_uint32(s->gic, "revision", 2);
    qdev_init_nofail(s->gic);
    busdev = SYS_BUS_DEVICE(s->gic);

    /* Pass through outbound IRQ lines from the GIC */
    sysbus_pass_irq(dev, busdev);

    /* Pass through inbound GPIO lines to the GIC */
    qdev_init_gpio_in(&s->busdev.qdev, a15mp_priv_set_irq, s->num_irq - 32);

    /* Memory map (addresses are offsets from PERIPHBASE):
     *  0x0000-0x0fff -- reserved
     *  0x1000-0x1fff -- GIC Distributor
     *  0x2000-0x2fff -- GIC CPU interface
     *  0x4000-0x4fff -- GIC virtual interface control (not modelled)
     *  0x5000-0x5fff -- GIC virtual interface control (not modelled)
     *  0x6000-0x7fff -- GIC virtual CPU interface (not modelled)
     */
    memory_region_init(&s->container, "a15mp-priv-container", 0x8000);
    memory_region_add_subregion(&s->container, 0x1000,
                                sysbus_mmio_get_region(busdev, 0));
    memory_region_add_subregion(&s->container, 0x2000,
                                sysbus_mmio_get_region(busdev, 1));

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
    /* We currently have no savable state */
}

static const TypeInfo a15mp_priv_info = {
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
