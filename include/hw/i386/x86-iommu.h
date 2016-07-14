/*
 * Common IOMMU interface for X86 platform
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

#ifndef IOMMU_COMMON_H
#define IOMMU_COMMON_H

#include "hw/sysbus.h"

#define  TYPE_X86_IOMMU_DEVICE  ("x86-iommu")
#define  X86_IOMMU_DEVICE(obj) \
    OBJECT_CHECK(X86IOMMUState, (obj), TYPE_X86_IOMMU_DEVICE)
#define  X86_IOMMU_CLASS(klass) \
    OBJECT_CLASS_CHECK(X86IOMMUClass, (klass), TYPE_X86_IOMMU_DEVICE)
#define  X86_IOMMU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(X86IOMMUClass, obj, TYPE_X86_IOMMU_DEVICE)

#define X86_IOMMU_PCI_DEVFN_MAX           256

typedef struct X86IOMMUState X86IOMMUState;
typedef struct X86IOMMUClass X86IOMMUClass;

struct X86IOMMUClass {
    SysBusDeviceClass parent;
    /* Intel/AMD specific realize() hook */
    DeviceRealize realize;
};

struct X86IOMMUState {
    SysBusDevice busdev;
    bool intr_supported;        /* Whether vIOMMU supports IR */
};

/**
 * x86_iommu_get_default - get default IOMMU device
 * @return: pointer to default IOMMU device
 */
X86IOMMUState *x86_iommu_get_default(void);

#endif
