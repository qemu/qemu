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
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "gic_internal.h"
#include "hw/arm/linux-boot-if.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "sysemu/kvm.h"

static int gic_pre_save(void *opaque)
{
    GICState *s = (GICState *)opaque;
    ARMGICCommonClass *c = ARM_GIC_COMMON_GET_CLASS(s);

    if (c->pre_save) {
        c->pre_save(s);
    }

    return 0;
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

static bool gic_virt_state_needed(void *opaque)
{
    GICState *s = (GICState *)opaque;

    return s->virt_extn;
}

static const VMStateDescription vmstate_gic_irq_state = {
    .name = "arm_gic_irq_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
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

static const VMStateDescription vmstate_gic_virt_state = {
    .name = "arm_gic_virt_state",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = gic_virt_state_needed,
    .fields = (const VMStateField[]) {
        /* Virtual interface */
        VMSTATE_UINT32_ARRAY(h_hcr, GICState, GIC_NCPU),
        VMSTATE_UINT32_ARRAY(h_misr, GICState, GIC_NCPU),
        VMSTATE_UINT32_2DARRAY(h_lr, GICState, GIC_MAX_LR, GIC_NCPU),
        VMSTATE_UINT32_ARRAY(h_apr, GICState, GIC_NCPU),

        /* Virtual CPU interfaces */
        VMSTATE_UINT32_SUB_ARRAY(cpu_ctlr, GICState, GIC_NCPU, GIC_NCPU),
        VMSTATE_UINT16_SUB_ARRAY(priority_mask, GICState, GIC_NCPU, GIC_NCPU),
        VMSTATE_UINT16_SUB_ARRAY(running_priority, GICState, GIC_NCPU, GIC_NCPU),
        VMSTATE_UINT16_SUB_ARRAY(current_pending, GICState, GIC_NCPU, GIC_NCPU),
        VMSTATE_UINT8_SUB_ARRAY(bpr, GICState, GIC_NCPU, GIC_NCPU),
        VMSTATE_UINT8_SUB_ARRAY(abpr, GICState, GIC_NCPU, GIC_NCPU),

        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_gic = {
    .name = "arm_gic",
    .version_id = 12,
    .minimum_version_id = 12,
    .pre_save = gic_pre_save,
    .post_load = gic_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctlr, GICState),
        VMSTATE_UINT32_SUB_ARRAY(cpu_ctlr, GICState, 0, GIC_NCPU),
        VMSTATE_STRUCT_ARRAY(irq_state, GICState, GIC_MAXIRQ, 1,
                             vmstate_gic_irq_state, gic_irq_state),
        VMSTATE_UINT8_ARRAY(irq_target, GICState, GIC_MAXIRQ),
        VMSTATE_UINT8_2DARRAY(priority1, GICState, GIC_INTERNAL, GIC_NCPU),
        VMSTATE_UINT8_ARRAY(priority2, GICState, GIC_MAXIRQ - GIC_INTERNAL),
        VMSTATE_UINT8_2DARRAY(sgi_pending, GICState, GIC_NR_SGIS, GIC_NCPU),
        VMSTATE_UINT16_SUB_ARRAY(priority_mask, GICState, 0, GIC_NCPU),
        VMSTATE_UINT16_SUB_ARRAY(running_priority, GICState, 0, GIC_NCPU),
        VMSTATE_UINT16_SUB_ARRAY(current_pending, GICState, 0, GIC_NCPU),
        VMSTATE_UINT8_SUB_ARRAY(bpr, GICState, 0, GIC_NCPU),
        VMSTATE_UINT8_SUB_ARRAY(abpr, GICState, 0, GIC_NCPU),
        VMSTATE_UINT32_2DARRAY(apr, GICState, GIC_NR_APRS, GIC_NCPU),
        VMSTATE_UINT32_2DARRAY(nsapr, GICState, GIC_NR_APRS, GIC_NCPU),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_gic_virt_state,
        NULL
    }
};

void gic_init_irqs_and_mmio(GICState *s, qemu_irq_handler handler,
                            const MemoryRegionOps *ops,
                            const MemoryRegionOps *virt_ops)
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
    i += (GIC_INTERNAL * s->num_cpu);
    qdev_init_gpio_in(DEVICE(s), handler, i);

    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->parent_irq[i]);
    }
    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->parent_fiq[i]);
    }
    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->parent_virq[i]);
    }
    for (i = 0; i < s->num_cpu; i++) {
        sysbus_init_irq(sbd, &s->parent_vfiq[i]);
    }
    if (s->virt_extn) {
        for (i = 0; i < s->num_cpu; i++) {
            sysbus_init_irq(sbd, &s->maintenance_irq[i]);
        }
    }

    /* Distributor */
    memory_region_init_io(&s->iomem, OBJECT(s), ops, s, "gic_dist", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    /* This is the main CPU interface "for this core". It is always
     * present because it is required by both software emulation and KVM.
     */
    memory_region_init_io(&s->cpuiomem[0], OBJECT(s), ops ? &ops[1] : NULL,
                          s, "gic_cpu", s->revision == 2 ? 0x2000 : 0x100);
    sysbus_init_mmio(sbd, &s->cpuiomem[0]);

    if (s->virt_extn) {
        memory_region_init_io(&s->vifaceiomem[0], OBJECT(s), virt_ops,
                              s, "gic_viface", 0x1000);
        sysbus_init_mmio(sbd, &s->vifaceiomem[0]);

        memory_region_init_io(&s->vcpuiomem, OBJECT(s),
                              virt_ops ? &virt_ops[1] : NULL,
                              s, "gic_vcpu", 0x2000);
        sysbus_init_mmio(sbd, &s->vcpuiomem);
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
        (s->revision == REV_11MPCORE)) {
        error_setg(errp, "this GIC revision does not implement "
                   "the security extensions");
        return;
    }

    if (s->virt_extn) {
        if (s->revision != 2) {
            error_setg(errp, "GIC virtualization extensions are only "
                       "supported by revision 2");
            return;
        }

        /* For now, set the number of implemented LRs to 4, as found in most
         * real GICv2. This could be promoted as a QOM property if we need to
         * emulate a variant with another num_lrs.
         */
        s->num_lrs = 4;
    }
}

static inline void arm_gic_common_reset_irq_state(GICState *s, int cidx,
                                                  int resetprio)
{
    int i, j;

    for (i = cidx; i < cidx + s->num_cpu; i++) {
        if (s->revision == REV_11MPCORE) {
            s->priority_mask[i] = 0xf0;
        } else {
            s->priority_mask[i] = resetprio;
        }
        s->current_pending[i] = 1023;
        s->running_priority[i] = 0x100;
        s->cpu_ctlr[i] = 0;
        s->bpr[i] = gic_is_vcpu(i) ? GIC_VIRT_MIN_BPR : GIC_MIN_BPR;
        s->abpr[i] = gic_is_vcpu(i) ? GIC_VIRT_MIN_ABPR : GIC_MIN_ABPR;

        if (!gic_is_vcpu(i)) {
            for (j = 0; j < GIC_INTERNAL; j++) {
                s->priority1[j][i] = resetprio;
            }
            for (j = 0; j < GIC_NR_SGIS; j++) {
                s->sgi_pending[j][i] = 0;
            }
        }
    }
}

static void arm_gic_common_reset_hold(Object *obj)
{
    GICState *s = ARM_GIC_COMMON(obj);
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
    arm_gic_common_reset_irq_state(s, 0, resetprio);

    if (s->virt_extn) {
        /* vCPU states are stored at indexes GIC_NCPU .. GIC_NCPU+num_cpu.
         * The exposed vCPU interface does not have security extensions.
         */
        arm_gic_common_reset_irq_state(s, GIC_NCPU, 0);
    }

    for (i = 0; i < GIC_NR_SGIS; i++) {
        GIC_DIST_SET_ENABLED(i, ALL_CPU_MASK);
        GIC_DIST_SET_EDGE_TRIGGER(i);
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
            GIC_DIST_SET_GROUP(i, ALL_CPU_MASK);
        }
    }

    if (s->virt_extn) {
        for (i = 0; i < s->num_lrs; i++) {
            for (j = 0; j < s->num_cpu; j++) {
                s->h_lr[i][j] = 0;
            }
        }

        for (i = 0; i < s->num_cpu; i++) {
            s->h_hcr[i] = 0;
            s->h_misr[i] = 0;
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
     */
    DEFINE_PROP_UINT32("revision", GICState, revision, 1),
    /* True if the GIC should implement the security extensions */
    DEFINE_PROP_BOOL("has-security-extensions", GICState, security_extn, 0),
    /* True if the GIC should implement the virtualization extensions */
    DEFINE_PROP_BOOL("has-virtualization-extensions", GICState, virt_extn, 0),
    DEFINE_PROP_UINT32("num-priority-bits", GICState, n_prio_bits, 8),
    DEFINE_PROP_END_OF_LIST(),
};

static void arm_gic_common_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    ARMLinuxBootIfClass *albifc = ARM_LINUX_BOOT_IF_CLASS(klass);

    rc->phases.hold = arm_gic_common_reset_hold;
    dc->realize = arm_gic_common_realize;
    device_class_set_props(dc, arm_gic_common_properties);
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

const char *gic_class_name(void)
{
    return kvm_irqchip_in_kernel() ? "kvm-arm-gic" : "arm_gic";
}
