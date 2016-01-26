/*
 * ARM GICv3 support - common bits of emulated and KVM kernel model
 *
 * Copyright (c) 2012 Linaro Limited
 * Copyright (c) 2015 Huawei.
 * Written by Peter Maydell
 * Extended to 64 cores by Shlomo Pongratz
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
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

#include "qemu/osdep.h"
#include "hw/intc/arm_gicv3_common.h"

static void gicv3_pre_save(void *opaque)
{
    GICv3State *s = (GICv3State *)opaque;
    ARMGICv3CommonClass *c = ARM_GICV3_COMMON_GET_CLASS(s);

    if (c->pre_save) {
        c->pre_save(s);
    }
}

static int gicv3_post_load(void *opaque, int version_id)
{
    GICv3State *s = (GICv3State *)opaque;
    ARMGICv3CommonClass *c = ARM_GICV3_COMMON_GET_CLASS(s);

    if (c->post_load) {
        c->post_load(s);
    }
    return 0;
}

static const VMStateDescription vmstate_gicv3 = {
    .name = "arm_gicv3",
    .unmigratable = 1,
    .pre_save = gicv3_pre_save,
    .post_load = gicv3_post_load,
};

void gicv3_init_irqs_and_mmio(GICv3State *s, qemu_irq_handler handler,
                              const MemoryRegionOps *ops)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);
    int i;

    /* For the GIC, also expose incoming GPIO lines for PPIs for each CPU.
     * GPIO array layout is thus:
     *  [0..N-1] spi
     *  [N..N+31] PPIs for CPU 0
     *  [N+32..N+63] PPIs for CPU 1
     *   ...
     */
    i = s->num_irq - GIC_INTERNAL + GIC_INTERNAL * s->num_cpu;
    qdev_init_gpio_in(DEVICE(s), handler, i);

    s->parent_irq = g_malloc(s->num_cpu * sizeof(qemu_irq));
    s->parent_fiq = g_malloc(s->num_cpu * sizeof(qemu_irq));

    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->parent_irq[i]);
    }
    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->parent_fiq[i]);
    }

    memory_region_init_io(&s->iomem_dist, OBJECT(s), ops, s,
                          "gicv3_dist", 0x10000);
    memory_region_init_io(&s->iomem_redist, OBJECT(s), ops ? &ops[1] : NULL, s,
                          "gicv3_redist", 0x20000 * s->num_cpu);

    sysbus_init_mmio(sbd, &s->iomem_dist);
    sysbus_init_mmio(sbd, &s->iomem_redist);
}

static void arm_gicv3_common_realize(DeviceState *dev, Error **errp)
{
    GICv3State *s = ARM_GICV3_COMMON(dev);

    /* revision property is actually reserved and currently used only in order
     * to keep the interface compatible with GICv2 code, avoiding extra
     * conditions. However, in future it could be used, for example, if we
     * implement GICv4.
     */
    if (s->revision != 3) {
        error_setg(errp, "unsupported GIC revision %d", s->revision);
        return;
    }
}

static void arm_gicv3_common_reset(DeviceState *dev)
{
    /* TODO */
}

static Property arm_gicv3_common_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", GICv3State, num_cpu, 1),
    DEFINE_PROP_UINT32("num-irq", GICv3State, num_irq, 32),
    DEFINE_PROP_UINT32("revision", GICv3State, revision, 3),
    DEFINE_PROP_BOOL("has-security-extensions", GICv3State, security_extn, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void arm_gicv3_common_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = arm_gicv3_common_reset;
    dc->realize = arm_gicv3_common_realize;
    dc->props = arm_gicv3_common_properties;
    dc->vmsd = &vmstate_gicv3;
}

static const TypeInfo arm_gicv3_common_type = {
    .name = TYPE_ARM_GICV3_COMMON,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GICv3State),
    .class_size = sizeof(ARMGICv3CommonClass),
    .class_init = arm_gicv3_common_class_init,
    .abstract = true,
};

static void register_types(void)
{
    type_register_static(&arm_gicv3_common_type);
}

type_init(register_types)
