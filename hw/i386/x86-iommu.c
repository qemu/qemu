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
#include "qemu/error-report.h"
#include "trace.h"

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
    QLIST_INIT(&x86_iommu->iec_notifiers);
    if (x86_class->realize) {
        x86_class->realize(dev, errp);
    }

    x86_iommu_set_default(X86_IOMMU_DEVICE(dev));
}

static void x86_iommu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = x86_iommu_realize;
}

static bool x86_iommu_intremap_prop_get(Object *o, Error **errp)
{
    X86IOMMUState *s = X86_IOMMU_DEVICE(o);
    return s->intr_supported;
}

static void x86_iommu_intremap_prop_set(Object *o, bool value, Error **errp)
{
    X86IOMMUState *s = X86_IOMMU_DEVICE(o);
    s->intr_supported = value;
}

static bool x86_iommu_device_iotlb_prop_get(Object *o, Error **errp)
{
    X86IOMMUState *s = X86_IOMMU_DEVICE(o);
    return s->dt_supported;
}

static void x86_iommu_device_iotlb_prop_set(Object *o, bool value, Error **errp)
{
    X86IOMMUState *s = X86_IOMMU_DEVICE(o);
    s->dt_supported = value;
}

static void x86_iommu_instance_init(Object *o)
{
    X86IOMMUState *s = X86_IOMMU_DEVICE(o);

    /* By default, do not support IR */
    s->intr_supported = false;
    object_property_add_bool(o, "intremap", x86_iommu_intremap_prop_get,
                             x86_iommu_intremap_prop_set, NULL);
    s->dt_supported = false;
    object_property_add_bool(o, "device-iotlb",
                             x86_iommu_device_iotlb_prop_get,
                             x86_iommu_device_iotlb_prop_set,
                             NULL);
}

static const TypeInfo x86_iommu_info = {
    .name          = TYPE_X86_IOMMU_DEVICE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = x86_iommu_instance_init,
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
