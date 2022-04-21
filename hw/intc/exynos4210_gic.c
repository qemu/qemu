/*
 * Samsung exynos4210 GIC implementation. Based on hw/arm_gic.c
 *
 * Copyright (c) 2000 - 2011 Samsung Electronics Co., Ltd.
 * All rights reserved.
 *
 * Evgeny Voevodin <e.voevodin@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/intc/exynos4210_gic.h"
#include "hw/arm/exynos4210.h"
#include "qom/object.h"

#define EXYNOS4210_GIC_NIRQ 160

#define EXYNOS4210_EXT_GIC_CPU_REGION_SIZE     0x10000
#define EXYNOS4210_EXT_GIC_DIST_REGION_SIZE    0x10000

#define EXYNOS4210_EXT_GIC_PER_CPU_OFFSET      0x8000
#define EXYNOS4210_EXT_GIC_CPU_GET_OFFSET(n) \
    ((n) * EXYNOS4210_EXT_GIC_PER_CPU_OFFSET)
#define EXYNOS4210_EXT_GIC_DIST_GET_OFFSET(n) \
    ((n) * EXYNOS4210_EXT_GIC_PER_CPU_OFFSET)

#define EXYNOS4210_GIC_CPU_REGION_SIZE  0x100
#define EXYNOS4210_GIC_DIST_REGION_SIZE 0x1000

static void exynos4210_gic_set_irq(void *opaque, int irq, int level)
{
    Exynos4210GicState *s = (Exynos4210GicState *)opaque;
    qemu_set_irq(qdev_get_gpio_in(s->gic, irq), level);
}

static void exynos4210_gic_realize(DeviceState *dev, Error **errp)
{
    Object *obj = OBJECT(dev);
    Exynos4210GicState *s = EXYNOS4210_GIC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    SysBusDevice *gicbusdev;
    uint32_t n = s->num_cpu;
    uint32_t i;

    s->gic = qdev_new("arm_gic");
    qdev_prop_set_uint32(s->gic, "num-cpu", s->num_cpu);
    qdev_prop_set_uint32(s->gic, "num-irq", EXYNOS4210_GIC_NIRQ);
    gicbusdev = SYS_BUS_DEVICE(s->gic);
    sysbus_realize_and_unref(gicbusdev, &error_fatal);

    /* Pass through outbound IRQ lines from the GIC */
    sysbus_pass_irq(sbd, gicbusdev);

    /* Pass through inbound GPIO lines to the GIC */
    qdev_init_gpio_in(dev, exynos4210_gic_set_irq,
                      EXYNOS4210_GIC_NIRQ - 32);

    memory_region_init(&s->cpu_container, obj, "exynos4210-cpu-container",
            EXYNOS4210_EXT_GIC_CPU_REGION_SIZE);
    memory_region_init(&s->dist_container, obj, "exynos4210-dist-container",
            EXYNOS4210_EXT_GIC_DIST_REGION_SIZE);

    /*
     * This clues in gcc that our on-stack buffers do, in fact have
     * enough room for the cpu numbers.  gcc 9.2.1 on 32-bit x86
     * doesn't figure this out, otherwise and gives spurious warnings.
     */
    assert(n <= EXYNOS4210_GIC_NCPUS);
    for (i = 0; i < n; i++) {
        g_autofree char *cpu_alias_name = g_strdup_printf("exynos4210-gic-alias_cpu%u", i);
        g_autofree char *dist_alias_name = g_strdup_printf("exynos4210-gic-alias_dist%u", i);

        /* Map CPU interface per SMP Core */
        memory_region_init_alias(&s->cpu_alias[i], obj,
                                 cpu_alias_name,
                                 sysbus_mmio_get_region(gicbusdev, 1),
                                 0,
                                 EXYNOS4210_GIC_CPU_REGION_SIZE);
        memory_region_add_subregion(&s->cpu_container,
                EXYNOS4210_EXT_GIC_CPU_GET_OFFSET(i), &s->cpu_alias[i]);

        /* Map Distributor per SMP Core */
        memory_region_init_alias(&s->dist_alias[i], obj,
                                 dist_alias_name,
                                 sysbus_mmio_get_region(gicbusdev, 0),
                                 0,
                                 EXYNOS4210_GIC_DIST_REGION_SIZE);
        memory_region_add_subregion(&s->dist_container,
                EXYNOS4210_EXT_GIC_DIST_GET_OFFSET(i), &s->dist_alias[i]);
    }

    sysbus_init_mmio(sbd, &s->cpu_container);
    sysbus_init_mmio(sbd, &s->dist_container);
}

static Property exynos4210_gic_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", Exynos4210GicState, num_cpu, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void exynos4210_gic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, exynos4210_gic_properties);
    dc->realize = exynos4210_gic_realize;
}

static const TypeInfo exynos4210_gic_info = {
    .name          = TYPE_EXYNOS4210_GIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Exynos4210GicState),
    .class_init    = exynos4210_gic_class_init,
};

static void exynos4210_gic_register_types(void)
{
    type_register_static(&exynos4210_gic_info);
}

type_init(exynos4210_gic_register_types)
