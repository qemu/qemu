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
#include "hw/pci/pci.h"

#define  TYPE_X86_IOMMU_DEVICE  ("x86-iommu")
#define  X86_IOMMU_DEVICE(obj) \
    OBJECT_CHECK(X86IOMMUState, (obj), TYPE_X86_IOMMU_DEVICE)
#define  X86_IOMMU_CLASS(klass) \
    OBJECT_CLASS_CHECK(X86IOMMUClass, (klass), TYPE_X86_IOMMU_DEVICE)
#define  X86_IOMMU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(X86IOMMUClass, obj, TYPE_X86_IOMMU_DEVICE)

#define X86_IOMMU_PCI_DEVFN_MAX           256
#define X86_IOMMU_SID_INVALID             (0xffff)

typedef struct X86IOMMUState X86IOMMUState;
typedef struct X86IOMMUClass X86IOMMUClass;

typedef enum IommuType {
    TYPE_INTEL,
    TYPE_AMD,
    TYPE_NONE
} IommuType;

struct X86IOMMUClass {
    SysBusDeviceClass parent;
    /* Intel/AMD specific realize() hook */
    DeviceRealize realize;
    /* MSI-based interrupt remapping */
    int (*int_remap)(X86IOMMUState *iommu, MSIMessage *src,
                     MSIMessage *dst, uint16_t sid);
};

/**
 * iec_notify_fn - IEC (Interrupt Entry Cache) notifier hook,
 *                 triggered when IR invalidation happens.
 * @private: private data
 * @global: whether this is a global IEC invalidation
 * @index: IRTE index to invalidate (start from)
 * @mask: invalidation mask
 */
typedef void (*iec_notify_fn)(void *private, bool global,
                              uint32_t index, uint32_t mask);

struct IEC_Notifier {
    iec_notify_fn iec_notify;
    void *private;
    QLIST_ENTRY(IEC_Notifier) list;
};
typedef struct IEC_Notifier IEC_Notifier;

struct X86IOMMUState {
    SysBusDevice busdev;
    bool intr_supported;        /* Whether vIOMMU supports IR */
    bool dt_supported;          /* Whether vIOMMU supports DT */
    bool pt_supported;          /* Whether vIOMMU supports pass-through */
    IommuType type;             /* IOMMU type - AMD/Intel     */
    QLIST_HEAD(, IEC_Notifier) iec_notifiers; /* IEC notify list */
};

/**
 * x86_iommu_get_default - get default IOMMU device
 * @return: pointer to default IOMMU device
 */
X86IOMMUState *x86_iommu_get_default(void);

/*
 * x86_iommu_get_type - get IOMMU type
 */
IommuType x86_iommu_get_type(void);

/**
 * x86_iommu_iec_register_notifier - register IEC (Interrupt Entry
 *                                   Cache) notifiers
 * @iommu: IOMMU device to register
 * @fn: IEC notifier hook function
 * @data: notifier private data
 */
void x86_iommu_iec_register_notifier(X86IOMMUState *iommu,
                                     iec_notify_fn fn, void *data);

/**
 * x86_iommu_iec_notify_all - Notify IEC invalidations
 * @iommu: IOMMU device that sends the notification
 * @global: whether this is a global invalidation. If true, @index
 *          and @mask are undefined.
 * @index: starting index of interrupt entry to invalidate
 * @mask: index mask for the invalidation
 */
void x86_iommu_iec_notify_all(X86IOMMUState *iommu, bool global,
                              uint32_t index, uint32_t mask);

#endif
