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

#include "gic_internal.h"

static void gic_pre_save(void *opaque)
{
    GICState *s = (GICState *)opaque;
    ARMGICCommonClass *c = ARM_GIC_COMMON_GET_CLASS(s);

    if (c->pre_save) {
        c->pre_save(s);
    }
}

static int gic_post_load(void *opaque, int version_id)
{
    GICState *s = (GICState *)opaque;
    ARMGICCommonClass *c = ARM_GIC_COMMON_GET_CLASS(s);

    if (c->post_load) {
        c->post_load(s);
    }
    return 0;
}

static const VMStateDescription vmstate_gic_irq_state = {
    .name = "arm_gic_irq_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(enabled, gic_irq_state),
        VMSTATE_UINT8(pending, gic_irq_state),
        VMSTATE_UINT8(active, gic_irq_state),
        VMSTATE_UINT8(level, gic_irq_state),
        VMSTATE_BOOL(model, gic_irq_state),
        VMSTATE_BOOL(edge_trigger, gic_irq_state),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_gic = {
    .name = "arm_gic",
    .version_id = 7,
    .minimum_version_id = 7,
    .pre_save = gic_pre_save,
    .post_load = gic_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_BOOL(enabled, GICState),
        VMSTATE_BOOL_ARRAY(cpu_enabled, GICState, GIC_NCPU),
        VMSTATE_STRUCT_ARRAY(irq_state, GICState, GIC_MAXIRQ, 1,
                             vmstate_gic_irq_state, gic_irq_state),
        VMSTATE_UINT8_ARRAY(irq_target, GICState, GIC_MAXIRQ),
        VMSTATE_UINT8_2DARRAY(priority1, GICState, GIC_INTERNAL, GIC_NCPU),
        VMSTATE_UINT8_ARRAY(priority2, GICState, GIC_MAXIRQ - GIC_INTERNAL),
        VMSTATE_UINT16_2DARRAY(last_active, GICState, GIC_MAXIRQ, GIC_NCPU),
        VMSTATE_UINT8_2DARRAY(sgi_pending, GICState, GIC_NR_SGIS, GIC_NCPU),
        VMSTATE_UINT16_ARRAY(priority_mask, GICState, GIC_NCPU),
        VMSTATE_UINT16_ARRAY(running_irq, GICState, GIC_NCPU),
        VMSTATE_UINT16_ARRAY(running_priority, GICState, GIC_NCPU),
        VMSTATE_UINT16_ARRAY(current_pending, GICState, GIC_NCPU),
        VMSTATE_UINT8_ARRAY(bpr, GICState, GIC_NCPU),
        VMSTATE_UINT8_ARRAY(abpr, GICState, GIC_NCPU),
        VMSTATE_UINT32_2DARRAY(apr, GICState, GIC_NR_APRS, GIC_NCPU),
        VMSTATE_END_OF_LIST()
    }
};

static void arm_gic_common_realize(DeviceState *dev, Error **errp)
{
    GICState *s = ARM_GIC_COMMON(dev);
    int num_irq = s->num_irq;

    if (s->num_cpu > GIC_NCPU) {
        error_setg(errp, "requested %u CPUs exceeds GIC maximum %d",
                   s->num_cpu, GIC_NCPU);
        return;
    }
    s->num_irq += GIC_BASE_IRQ;
    if (s->num_irq > GIC_MAXIRQ) {
        error_setg(errp,
                   "requested %u interrupt lines exceeds GIC maximum %d",
                   num_irq, GIC_MAXIRQ);
        return;
    }
    /* ITLinesNumber is represented as (N / 32) - 1 (see
     * gic_dist_readb) so this is an implementation imposed
     * restriction, not an architectural one:
     */
    if (s->num_irq < 32 || (s->num_irq % 32)) {
        error_setg(errp,
                   "%d interrupt lines unsupported: not divisible by 32",
                   num_irq);
        return;
    }
}

static void arm_gic_common_reset(DeviceState *dev)
{
    GICState *s = ARM_GIC_COMMON(dev);
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
        s->cpu_enabled[i] = false;
    }
    for (i = 0; i < GIC_NR_SGIS; i++) {
        GIC_SET_ENABLED(i, ALL_CPU_MASK);
        GIC_SET_EDGE_TRIGGER(i);
    }
    if (s->num_cpu == 1) {
        /* For uniprocessor GICs all interrupts always target the sole CPU */
        for (i = 0; i < GIC_MAXIRQ; i++) {
            s->irq_target[i] = 1;
        }
    }
    s->enabled = false;
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
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = arm_gic_common_reset;
    dc->realize = arm_gic_common_realize;
    dc->props = arm_gic_common_properties;
    dc->vmsd = &vmstate_gic;
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
