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

static void x86_iommu_realize(DeviceState *dev, Error **errp)
{
    X86IOMMUClass *x86_class = X86_IOMMU_GET_CLASS(dev);
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
