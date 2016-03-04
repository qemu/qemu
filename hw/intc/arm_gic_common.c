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

#include "qemu/osdep.h"
#include "gic_internal.h"
#include "hw/arm/linux-boot-if.h"

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
        VMSTATE_UINT8(group, gic_irq_state),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_gic = {
    .name = "arm_gic",
    .version_id = 12,
    .minimum_version_id = 12,
    .pre_save = gic_pre_save,
    .post_load = gic_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctlr, GICState),
        VMSTATE_UINT32_ARRAY(cpu_ctlr, GICState, GIC_NCPU),
        VMSTATE_STRUCT_ARRAY(irq_state, GICState, GIC_MAXIRQ, 1,
                             vmstate_gic_irq_state, gic_irq_state),
        VMSTATE_UINT8_ARRAY(irq_target, GICState, GIC_MAXIRQ),
        VMSTATE_UINT8_2DARRAY(priority1, GICState, GIC_INTERNAL, GIC_NCPU),
        VMSTATE_UINT8_ARRAY(priority2, GICState, GIC_MAXIRQ - GIC_INTERNAL),
        VMSTATE_UINT8_2DARRAY(sgi_pending, GICState, GIC_NR_SGIS, GIC_NCPU),
        VMSTATE_UINT16_ARRAY(priority_mask, GICState, GIC_NCPU),
        VMSTATE_UINT16_ARRAY(running_priority, GICState, GIC_NCPU),
        VMSTATE_UINT16_ARRAY(current_pending, GICState, GIC_NCPU),
        VMSTATE_UINT8_ARRAY(bpr, GICState, GIC_NCPU),
        VMSTATE_UINT8_ARRAY(abpr, GICState, GIC_NCPU),
        VMSTATE_UINT32_2DARRAY(apr, GICState, GIC_NR_APRS, GIC_NCPU),
        VMSTATE_UINT32_2DARRAY(nsapr, GICState, GIC_NR_APRS, GIC_NCPU),
        VMSTATE_END_OF_LIST()
    }
};

void gic_init_irqs_and_mmio(GICState *s, qemu_irq_handler handler,
                            const MemoryRegionOps *ops)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(s);
    int i = s->num_irq - GIC_INTERNAL;

    /* For the GIC, also expose incoming GPIO lines for PPIs for each CPU.
     * GPIO array layout is thus:
     *  [0..N-1] SPIs
     *  [N..N+31] PPIs for CPU 0
     *  [N+32..N+63] PPIs for CPU 1
     *   ...
     */
    if (s->revision != REV_NVIC) {
        i += (GIC_INTERNAL * s->num_cpu);
    }
    qdev_init_gpio_in(DEVICE(s), handler, i);

    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->parent_irq[i]);
    }
    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->parent_fiq[i]);
    }

    /* Distributor */
    memory_region_init_io(&s->iomem, OBJECT(s), ops, s, "gic_dist", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    if (s->revision != REV_NVIC) {
        /* This is the main CPU interface "for this core". It is always
         * present because it is required by both software emulation and KVM.
         * NVIC is not handled here because its CPU interface is different,
         * neither it can use KVM.
         */
        memory_region_init_io(&s->cpuiomem[0], OBJECT(s), ops ? &ops[1] : NULL,
                              s, "gic_cpu", s->revision == 2 ? 0x2000 : 0x100);
        sysbus_init_mmio(sbd, &s->cpuiomem[0]);
    }
}

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

    if (s->security_extn &&
        (s->revision == REV_11MPCORE || s->revision == REV_NVIC)) {
        error_setg(errp, "this GIC revision does not implement "
                   "the security extensions");
        return;
    }
}

static void arm_gic_common_reset(DeviceState *dev)
{
    GICState *s = ARM_GIC_COMMON(dev);
    int i, j;
    int resetprio;

    /* If we're resetting a TZ-aware GIC as if secure firmware
     * had set it up ready to start a kernel in non-secure,
     * we need to set interrupt priorities to a "zero for the
     * NS view" value. This is particularly critical for the
     * priority_mask[] values, because if they are zero then NS
     * code cannot ever rewrite the priority to anything else.
     */
    if (s->security_extn && s->irq_reset_nonsecure) {
        resetprio = 0x80;
    } else {
        resetprio = 0;
    }

    memset(s->irq_state, 0, GIC_MAXIRQ * sizeof(gic_irq_state));
    for (i = 0 ; i < s->num_cpu; i++) {
        if (s->revision == REV_11MPCORE) {
            s->priority_mask[i] = 0xf0;
        } else {
            s->priority_mask[i] = resetprio;
        }
        s->current_pending[i] = 1023;
        s->running_priority[i] = 0x100;
        s->cpu_ctlr[i] = 0;
        s->bpr[i] = GIC_MIN_BPR;
        s->abpr[i] = GIC_MIN_ABPR;
        for (j = 0; j < GIC_INTERNAL; j++) {
            s->priority1[j][i] = resetprio;
        }
        for (j = 0; j < GIC_NR_SGIS; j++) {
            s->sgi_pending[j][i] = 0;
        }
    }
    for (i = 0; i < GIC_NR_SGIS; i++) {
        GIC_SET_ENABLED(i, ALL_CPU_MASK);
        GIC_SET_EDGE_TRIGGER(i);
    }

    for (i = 0; i < ARRAY_SIZE(s->priority2); i++) {
        s->priority2[i] = resetprio;
    }

    for (i = 0; i < GIC_MAXIRQ; i++) {
        /* For uniprocessor GICs all interrupts always target the sole CPU */
        if (s->num_cpu == 1) {
            s->irq_target[i] = 1;
        } else {
            s->irq_target[i] = 0;
        }
    }
    if (s->security_extn && s->irq_reset_nonsecure) {
        for (i = 0; i < GIC_MAXIRQ; i++) {
            GIC_SET_GROUP(i, ALL_CPU_MASK);
        }
    }

    s->ctlr = 0;
}

static void arm_gic_common_linux_init(ARMLinuxBootIf *obj,
                                      bool secure_boot)
{
    GICState *s = ARM_GIC_COMMON(obj);

    if (s->security_extn && !secure_boot) {
        /* We're directly booting a kernel into NonSecure. If this GIC
         * implements the security extensions then we must configure it
         * to have all the interrupts be NonSecure (this is a job that
         * is done by the Secure boot firmware in real hardware, and in
         * this mode QEMU is acting as a minimalist firmware-and-bootloader
         * equivalent).
         */
        s->irq_reset_nonsecure = true;
    }
}

static Property arm_gic_common_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", GICState, num_cpu, 1),
    DEFINE_PROP_UINT32("num-irq", GICState, num_irq, 32),
    /* Revision can be 1 or 2 for GIC architecture specification
     * versions 1 or 2, or 0 to indicate the legacy 11MPCore GIC.
     * (Internally, 0xffffffff also indicates "not a GIC but an NVIC".)
     */
    DEFINE_PROP_UINT32("revision", GICState, revision, 1),
    /* True if the GIC should implement the security extensions */
    DEFINE_PROP_BOOL("has-security-extensions", GICState, security_extn, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void arm_gic_common_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ARMLinuxBootIfClass *albifc = ARM_LINUX_BOOT_IF_CLASS(klass);

    dc->reset = arm_gic_common_reset;
    dc->realize = arm_gic_common_realize;
    dc->props = arm_gic_common_properties;
    dc->vmsd = &vmstate_gic;
    albifc->arm_linux_init = arm_gic_common_linux_init;
}

static const TypeInfo arm_gic_common_type = {
    .name = TYPE_ARM_GIC_COMMON,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GICState),
    .class_size = sizeof(ARMGICCommonClass),
    .class_init = arm_gic_common_class_init,
    .abstract = true,
    .interfaces = (InterfaceInfo []) {
        { TYPE_ARM_LINUX_BOOT_IF },
        { },
    },
};

static void register_types(void)
{
    type_register_static(&arm_gic_common_type);
}

type_init(register_types)
