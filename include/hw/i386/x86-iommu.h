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

#ifndef HW_I386_X86_IOMMU_H
#define HW_I386_X86_IOMMU_H

#include "hw/sysbus.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "qom/object.h"

#define  TYPE_X86_IOMMU_DEVICE  ("x86-iommu")
OBJECT_DECLARE_TYPE(X86IOMMUState, X86IOMMUClass, X86_IOMMU_DEVICE)

#define X86_IOMMU_SID_INVALID             (0xffff)

typedef struct X86IOMMUIrq X86IOMMUIrq;
typedef struct X86IOMMU_MSIMessage X86IOMMU_MSIMessage;

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
    OnOffAuto intr_supported;   /* Whether vIOMMU supports IR */
    bool dt_supported;          /* Whether vIOMMU supports DT */
    bool pt_supported;          /* Whether vIOMMU supports pass-through */
    QLIST_HEAD(, IEC_Notifier) iec_notifiers; /* IEC notify list */
};

bool x86_iommu_ir_supported(X86IOMMUState *s);

/* Generic IRQ entry information when interrupt remapping is enabled */
struct X86IOMMUIrq {
    /* Used by both IOAPIC/MSI interrupt remapping */
    uint8_t trigger_mode;
    uint8_t vector;
    uint8_t delivery_mode;
    uint32_t dest;
    uint8_t dest_mode;

    /* only used by MSI interrupt remapping */
    uint8_t redir_hint;
    uint8_t msi_addr_last_bits;
};

struct X86IOMMU_MSIMessage {
    union {
        struct {
#ifdef HOST_WORDS_BIGENDIAN
            uint32_t __addr_head:12; /* 0xfee */
            uint32_t dest:8;
            uint32_t __reserved:8;
            uint32_t redir_hint:1;
            uint32_t dest_mode:1;
            uint32_t __not_used:2;
#else
            uint32_t __not_used:2;
            uint32_t dest_mode:1;
            uint32_t redir_hint:1;
            uint32_t __reserved:8;
            uint32_t dest:8;
            uint32_t __addr_head:12; /* 0xfee */
#endif
            uint32_t __addr_hi;
        } QEMU_PACKED;
        uint64_t msi_addr;
    };
    union {
        struct {
#ifdef HOST_WORDS_BIGENDIAN
            uint16_t trigger_mode:1;
            uint16_t level:1;
            uint16_t __resved:3;
            uint16_t delivery_mode:3;
            uint16_t vector:8;
#else
            uint16_t vector:8;
            uint16_t delivery_mode:3;
            uint16_t __resved:3;
            uint16_t level:1;
            uint16_t trigger_mode:1;
#endif
            uint16_t __resved1;
        } QEMU_PACKED;
        uint32_t msi_data;
    };
};

/**
 * x86_iommu_get_default - get default IOMMU device
 * @return: pointer to default IOMMU device
 */
X86IOMMUState *x86_iommu_get_default(void);

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

/**
 * x86_iommu_irq_to_msi_message - Populate one MSIMessage from X86IOMMUIrq
 * @X86IOMMUIrq: The IRQ information
 * @out: Output MSI message
 */
void x86_iommu_irq_to_msi_message(X86IOMMUIrq *irq, MSIMessage *out);
#endif
