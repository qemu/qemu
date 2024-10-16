/*
 * libqos driver riscv-iommu-pci framework
 *
 * Copyright (c) 2024 Ventana Micro Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at your
 * option) any later version.  See the COPYING file in the top-level directory.
 *
 */

#ifndef TESTS_LIBQOS_RISCV_IOMMU_H
#define TESTS_LIBQOS_RISCV_IOMMU_H

#include "qgraph.h"
#include "pci.h"
#include "qemu/bitops.h"

#ifndef GENMASK_ULL
#define GENMASK_ULL(h, l) (((~0ULL) >> (63 - (h) + (l))) << (l))
#endif

/*
 * RISC-V IOMMU uses PCI_VENDOR_ID_REDHAT 0x1b36 and
 * PCI_DEVICE_ID_REDHAT_RISCV_IOMMU 0x0014.
 */
#define RISCV_IOMMU_PCI_VENDOR_ID       0x1b36
#define RISCV_IOMMU_PCI_DEVICE_ID       0x0014
#define RISCV_IOMMU_PCI_DEVICE_CLASS    0x0806

/* Common field positions */
#define RISCV_IOMMU_QUEUE_ENABLE        BIT(0)
#define RISCV_IOMMU_QUEUE_INTR_ENABLE   BIT(1)
#define RISCV_IOMMU_QUEUE_MEM_FAULT     BIT(8)
#define RISCV_IOMMU_QUEUE_ACTIVE        BIT(16)
#define RISCV_IOMMU_QUEUE_BUSY          BIT(17)

#define RISCV_IOMMU_REG_CAP             0x0000
#define RISCV_IOMMU_CAP_VERSION         GENMASK_ULL(7, 0)

#define RISCV_IOMMU_REG_DDTP            0x0010
#define RISCV_IOMMU_DDTP_BUSY           BIT_ULL(4)
#define RISCV_IOMMU_DDTP_MODE           GENMASK_ULL(3, 0)
#define RISCV_IOMMU_DDTP_MODE_OFF       0

#define RISCV_IOMMU_REG_CQCSR           0x0048
#define RISCV_IOMMU_CQCSR_CQEN          RISCV_IOMMU_QUEUE_ENABLE
#define RISCV_IOMMU_CQCSR_CIE           RISCV_IOMMU_QUEUE_INTR_ENABLE
#define RISCV_IOMMU_CQCSR_CQON          RISCV_IOMMU_QUEUE_ACTIVE
#define RISCV_IOMMU_CQCSR_BUSY          RISCV_IOMMU_QUEUE_BUSY

#define RISCV_IOMMU_REG_FQCSR           0x004C
#define RISCV_IOMMU_FQCSR_FQEN          RISCV_IOMMU_QUEUE_ENABLE
#define RISCV_IOMMU_FQCSR_FIE           RISCV_IOMMU_QUEUE_INTR_ENABLE
#define RISCV_IOMMU_FQCSR_FQON          RISCV_IOMMU_QUEUE_ACTIVE
#define RISCV_IOMMU_FQCSR_BUSY          RISCV_IOMMU_QUEUE_BUSY

#define RISCV_IOMMU_REG_PQCSR           0x0050
#define RISCV_IOMMU_PQCSR_PQEN          RISCV_IOMMU_QUEUE_ENABLE
#define RISCV_IOMMU_PQCSR_PIE           RISCV_IOMMU_QUEUE_INTR_ENABLE
#define RISCV_IOMMU_PQCSR_PQON          RISCV_IOMMU_QUEUE_ACTIVE
#define RISCV_IOMMU_PQCSR_BUSY          RISCV_IOMMU_QUEUE_BUSY

#define RISCV_IOMMU_REG_IPSR            0x0054

typedef struct QRISCVIOMMU {
    QOSGraphObject obj;
    QPCIDevice dev;
    QPCIBar reg_bar;
} QRISCVIOMMU;

#endif
