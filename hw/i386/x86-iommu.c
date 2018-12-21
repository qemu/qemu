/*
 * QEMU emulation of common X86 IOMMU
 *
 * Copyright (C) 2016 Peter Xu, Red Hat <peterx@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/boards.h"
#include "hw/i386/x86-iommu.h"
#include "hw/i386/pc.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "sysemu/kvm.h"

void x86_iommu_iec_register_notifier(X86IOMMUState *iommu,
                                     iec_notify_fn fn, void *data)
{
    IEC_Notifier *notifier = g_new0(IEC_Notifier, 1);

    notifier->iec_notify = fn;
    notifier->private = data;

    QLIST_INSERT_HEAD(&iommu->iec_notifiers, notifier, list);
}

void x86_iommu_iec_notify_all(X86IOMMUState *iommu, bool global,
                              uint32_t index, uint32_t mask)
{
    IEC_Notifier *notifier;

    trace_x86_iommu_iec_notify(global, index, mask);

    QLIST_FOREACH(notifier, &iommu->iec_notifiers, list) {
        if (notifier->iec_notify) {
            notifier->iec_notify(notifier->private, global,
                                 index, mask);
        }
    }
}

/* Generate one MSI message from VTDIrq info */
void x86_iommu_irq_to_msi_message(X86IOMMUIrq *irq, MSIMessage *msg_out)
{
    X86IOMMU_MSIMessage msg = {};

    /* Generate address bits */
    msg.dest_mode = irq->dest_mode;
    msg.redir_hint = irq->redir_hint;
    msg.dest = irq->dest;
    msg.__addr_hi = irq->dest & 0xffffff00;
    msg.__addr_head = cpu_to_le32(0xfee);
    /* Keep this from original MSI address bits */
    msg.__not_used = irq->msi_addr_last_bits;

    /* Generate data bits */
    msg.vector = irq->vector;
    msg.delivery_mode = irq->delivery_mode;
    msg.level = 1;
    msg.trigger_mode = irq->trigger_mode;

    msg_out->address = msg.msi_addr;
    msg_out->data = msg.msi_data;
}

/* Default X86 IOMMU device */
static X86IOMMUState *x86_iommu_default = NULL;

static void x86_iommu_set_default(X86IOMMUState *x86_iommu)
{
    assert(x86_iommu);

    if (x86_iommu_default) {
        error_report("QEMU does not support multiple vIOMMUs "
                     "for x86 yet.");
        exit(1);
    }

    x86_iommu_default = x86_iommu;
}

X86IOMMUState *x86_iommu_get_default(void)
{
    return x86_iommu_default;
}

IommuType x86_iommu_get_type(void)
{
    return x86_iommu_default->type;
}

static void x86_iommu_realize(DeviceState *dev, Error **errp)
{
    X86IOMMUState *x86_iommu = X86_IOMMU_DEVICE(dev);
    X86IOMMUClass *x86_class = X86_IOMMU_GET_CLASS(dev);
    MachineState *ms = MACHINE(qdev_get_machine());
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    PCMachineState *pcms =
        PC_MACHINE(object_dynamic_cast(OBJECT(ms), TYPE_PC_MACHINE));
    QLIST_INIT(&x86_iommu->iec_notifiers);
    bool irq_all_kernel = kvm_irqchip_in_kernel() && !kvm_irqchip_is_split();

    if (!pcms || !pcms->bus) {
        error_setg(errp, "Machine-type '%s' not supported by IOMMU",
                   mc->name);
        return;
    }

    /* If the user didn't specify IR, choose a default value for it */
    if (x86_iommu->intr_supported == ON_OFF_AUTO_AUTO) {
        x86_iommu->intr_supported = irq_all_kernel ?
            ON_OFF_AUTO_OFF : ON_OFF_AUTO_ON;
    }

    /* Both Intel and AMD IOMMU IR only support "kernel-irqchip={off|split}" */
    if (x86_iommu_ir_supported(x86_iommu) && irq_all_kernel) {
        error_setg(errp, "Interrupt Remapping cannot work with "
                         "kernel-irqchip=on, please use 'split|off'.");
        return;
    }

    if (x86_class->realize) {
        x86_class->realize(dev, errp);
    }

    x86_iommu_set_default(X86_IOMMU_DEVICE(dev));
}

static Property x86_iommu_properties[] = {
    DEFINE_PROP_ON_OFF_AUTO("intremap", X86IOMMUState,
                            intr_supported, ON_OFF_AUTO_AUTO),
    DEFINE_PROP_BOOL("device-iotlb", X86IOMMUState, dt_supported, false),
    DEFINE_PROP_BOOL("pt", X86IOMMUState, pt_supported, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void x86_iommu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = x86_iommu_realize;
    dc->props = x86_iommu_properties;
}

bool x86_iommu_ir_supported(X86IOMMUState *s)
{
    return s->intr_supported == ON_OFF_AUTO_ON;
}

static const TypeInfo x86_iommu_info = {
    .name          = TYPE_X86_IOMMU_DEVICE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(X86IOMMUState),
    .class_init    = x86_iommu_class_init,
    .class_size    = sizeof(X86IOMMUClass),
    .abstract      = true,
};

static void x86_iommu_register_types(void)
{
    type_register_static(&x86_iommu_info);
}

type_init(x86_iommu_register_types)
