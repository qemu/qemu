/*
 * A test device for IOMMU
 *
 * Copyright (c) 2026 Phytium Technology
 *
 * Author:
 *  Tao Tang <tangtao1634@phytium.com.cn>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_IOMMU_TESTDEV_H
#define HW_MISC_IOMMU_TESTDEV_H

#include "hw/pci/pci.h"

#define IOMMU_TESTDEV_VENDOR_ID     PCI_VENDOR_ID_REDHAT
#define IOMMU_TESTDEV_DEVICE_ID     PCI_DEVICE_ID_REDHAT_TEST

/* DMA_ATTRS register bit definitions which shadow some fields in MemTxAttrs */
#define ITD_ATTRS_SECURE_SHIFT          0
#define ITD_ATTRS_SECURE_MASK           0x1
#define ITD_ATTRS_SPACE_SHIFT           1
#define ITD_ATTRS_SPACE_MASK            0x3
#define ITD_ATTRS_SPACE_VALID_SHIFT     3
#define ITD_ATTRS_SPACE_VALID_MASK      0x1

#define ITD_ATTRS_SPACE_SECURE          0
#define ITD_ATTRS_SPACE_NONSECURE       1

/* Helper macros for setting fields */
#define ITD_ATTRS_SET_SECURE(attrs, val)                              \
    (((attrs) & ~(ITD_ATTRS_SECURE_MASK << ITD_ATTRS_SECURE_SHIFT)) | \
     (((val) & ITD_ATTRS_SECURE_MASK) << ITD_ATTRS_SECURE_SHIFT))

#define ITD_ATTRS_SET_SPACE(attrs, val)                               \
    (((attrs) & ~(ITD_ATTRS_SPACE_MASK << ITD_ATTRS_SPACE_SHIFT)) |   \
     (((val) & ITD_ATTRS_SPACE_MASK) << ITD_ATTRS_SPACE_SHIFT))

#define ITD_ATTRS_SET_SPACE_VALID(attrs, val)                         \
    (((attrs) & ~(ITD_ATTRS_SPACE_VALID_MASK <<                       \
                  ITD_ATTRS_SPACE_VALID_SHIFT)) |                     \
     (((val) & ITD_ATTRS_SPACE_VALID_MASK) <<                         \
      ITD_ATTRS_SPACE_VALID_SHIFT))

/* Helper macros for getting fields */
#define ITD_ATTRS_GET_SECURE(attrs)                                   \
    (((attrs) >> ITD_ATTRS_SECURE_SHIFT) & ITD_ATTRS_SECURE_MASK)

#define ITD_ATTRS_GET_SPACE(attrs)                                    \
    (((attrs) >> ITD_ATTRS_SPACE_SHIFT) & ITD_ATTRS_SPACE_MASK)

#define ITD_ATTRS_GET_SPACE_VALID(attrs)                              \
    (((attrs) >> ITD_ATTRS_SPACE_VALID_SHIFT) &                       \
     ITD_ATTRS_SPACE_VALID_MASK)

/* DMA result/status values shared with tests */
#define ITD_DMA_RESULT_IDLE    0xffffffffu
#define ITD_DMA_RESULT_BUSY    0xfffffffeu
#define ITD_DMA_ERR_BAD_LEN    0xdead0001u
#define ITD_DMA_ERR_TX_FAIL    0xdead0002u
#define ITD_DMA_ERR_RD_FAIL    0xdead0003u
#define ITD_DMA_ERR_MISMATCH   0xdead0004u
#define ITD_DMA_ERR_NOT_ARMED  0xdead0005u
#define ITD_DMA_ERR_BAD_ATTRS  0xdead0006u

#define ITD_DMA_WRITE_VAL     0x12345678u

/* DMA doorbell bits */
#define ITD_DMA_DBELL_ARM    0x1u

/* BAR0 layout of iommu-testdev */
enum {
    ITD_REG_DMA_TRIGGERING  = 0x00,
    ITD_REG_DMA_GVA_LO      = 0x04,
    ITD_REG_DMA_GVA_HI      = 0x08,
    ITD_REG_DMA_LEN         = 0x0c,
    ITD_REG_DMA_RESULT      = 0x10,
    ITD_REG_DMA_DBELL       = 0x14,
    /* [0] secure,[2:1] ArmSecuritySpace,[3] space_valid */
    ITD_REG_DMA_ATTRS       = 0x18,
    ITD_REG_DMA_GPA_LO      = 0x1c,
    ITD_REG_DMA_GPA_HI      = 0x20,
    BAR0_SIZE               = 0x1000,
};

#endif /* HW_MISC_IOMMU_TESTDEV_H */
