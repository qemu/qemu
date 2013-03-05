/*
 * ARM GIC support - common bits of emulated and KVM kernel model
 *
 * Copyright (c) 2012 Linaro Limited
 * Written by Peter Maydell
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

#include "arm_gic_internal.h"

static void gic_save(QEMUFile *f, void *opaque)
{
    GICState *s = (GICState *)opaque;
    ARMGICCommonClass *c = ARM_GIC_COMMON_GET_CLASS(s);
    int i;
    int j;

    if (c->pre_save) {
        c->pre_save(s);
    }

    qemu_put_be32(f, s->enabled);
    for (i = 0; i < s->num_cpu; i++) {
        qemu_put_be32(f, s->cpu_enabled[i]);
        for (j = 0; j < GIC_INTERNAL; j++) {
            qemu_put_be32(f, s->priority1[j][i]);
        }
        for (j = 0; j < s->num_irq; j++) {
            qemu_put_be32(f, s->last_active[j][i]);
        }
        qemu_put_be32(f, s->priority_mask[i]);
        qemu_put_be32(f, s->running_irq[i]);
        qemu_put_be32(f, s->running_priority[i]);
        qemu_put_be32(f, s->current_pending[i]);
    }
    for (i = 0; i < s->num_irq - GIC_INTERNAL; i++) {
        qemu_put_be32(f, s->priority2[i]);
    }
    for (i = 0; i < s->num_irq; i++) {
        qemu_put_be32(f, s->irq_target[i]);
        qemu_put_byte(f, s->irq_state[i].enabled);
        qemu_put_byte(f, s->irq_state[i].pending);
        qemu_put_byte(f, s->irq_state[i].active);
        qemu_put_byte(f, s->irq_state[i].level);
        qemu_put_byte(f, s->irq_state[i].model);
        qemu_put_byte(f, s->irq_state[i].trigger);
    }
}

static int gic_load(QEMUFile *f, void *opaque, int version_id)
{
    GICState *s = (GICState *)opaque;
    ARMGICCommonClass *c = ARM_GIC_COMMON_GET_CLASS(s);
    int i;
    int j;

    if (version_id != 3) {
        return -EINVAL;
    }

    s->enabled = qemu_get_be32(f);
    for (i = 0; i < s->num_cpu; i++) {
        s->cpu_enabled[i] = qemu_get_be32(f);
        for (j = 0; j < GIC_INTERNAL; j++) {
            s->priority1[j][i] = qemu_get_be32(f);
        }
        for (j = 0; j < s->num_irq; j++) {
            s->last_active[j][i] = qemu_get_be32(f);
        }
        s->priority_mask[i] = qemu_get_be32(f);
        s->running_irq[i] = qemu_get_be32(f);
        s->running_priority[i] = qemu_get_be32(f);
        s->current_pending[i] = qemu_get_be32(f);
    }
    for (i = 0; i < s->num_irq - GIC_INTERNAL; i++) {
        s->priority2[i] = qemu_get_be32(f);
    }
    for (i = 0; i < s->num_irq; i++) {
        s->irq_target[i] = qemu_get_be32(f);
        s->irq_state[i].enabled = qemu_get_byte(f);
        s->irq_state[i].pending = qemu_get_byte(f);
        s->irq_state[i].active = qemu_get_byte(f);
        s->irq_state[i].level = qemu_get_byte(f);
        s->irq_state[i].model = qemu_get_byte(f);
        s->irq_state[i].trigger = qemu_get_byte(f);
    }

    if (c->post_load) {
        c->post_load(s);
    }

    return 0;
}

static int arm_gic_common_init(SysBusDevice *dev)
{
    GICState *s = FROM_SYSBUS(GICState, dev);
    int num_irq = s->num_irq;

    if (s->num_cpu > NCPU) {
        hw_error("requested %u CPUs exceeds GIC maximum %d\n",
                 s->num_cpu, NCPU);
    }
    s->num_irq += GIC_BASE_IRQ;
    if (s->num_irq > GIC_MAXIRQ) {
        hw_error("requested %u interrupt lines exceeds GIC maximum %d\n",
                 num_irq, GIC_MAXIRQ);
    }
    /* ITLinesNumber is represented as (N / 32) - 1 (see
     * gic_dist_readb) so this is an implementation imposed
     * restriction, not an architectural one:
     */
    if (s->num_irq < 32 || (s->num_irq % 32)) {
        hw_error("%d interrupt lines unsupported: not divisible by 32\n",
                 num_irq);
    }

    register_savevm(NULL, "arm_gic", -1, 3, gic_save, gic_load, s);
    return 0;
}

static void arm_gic_common_reset(DeviceState *dev)
{
    GICState *s = FROM_SYSBUS(GICState, SYS_BUS_DEVICE(dev));
    int i;
    memset(s->irq_state, 0, GIC_MAXIRQ * sizeof(gic_irq_state));
    for (i = 0 ; i < s->num_cpu; i++) {
        if (s->revision == REV_11MPCORE) {
            s->priority_mask[i] = 0xf0;
        } else {
            s->priority_mask[i] = 0;
        }
        s->current_pending[i] = 1023;
        s->running_irq[i] = 1023;
        s->running_priority[i] = 0x100;
        s->cpu_enabled[i] = 0;
    }
    for (i = 0; i < 16; i++) {
        GIC_SET_ENABLED(i, ALL_CPU_MASK);
        GIC_SET_TRIGGER(i);
    }
    if (s->num_cpu == 1) {
        /* For uniprocessor GICs all interrupts always target the sole CPU */
        for (i = 0; i < GIC_MAXIRQ; i++) {
            s->irq_target[i] = 1;
        }
    }
    s->enabled = 0;
}

static Property arm_gic_common_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", GICState, num_cpu, 1),
    DEFINE_PROP_UINT32("num-irq", GICState, num_irq, 32),
    /* Revision can be 1 or 2 for GIC architecture specification
     * versions 1 or 2, or 0 to indicate the legacy 11MPCore GIC.
     * (Internally, 0xffffffff also indicates "not a GIC but an NVIC".)
     */
    DEFINE_PROP_UINT32("revision", GICState, revision, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void arm_gic_common_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sc = SYS_BUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = arm_gic_common_reset;
    dc->props = arm_gic_common_properties;
    dc->no_user = 1;
    sc->init = arm_gic_common_init;
}

static const TypeInfo arm_gic_common_type = {
    .name = TYPE_ARM_GIC_COMMON,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GICState),
    .class_size = sizeof(ARMGICCommonClass),
    .class_init = arm_gic_common_class_init,
    .abstract = true,
};

static void register_types(void)
{
    type_register_static(&arm_gic_common_type);
}

type_init(register_types)
