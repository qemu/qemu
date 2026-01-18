/*
 * IOMMU test device helpers for libqos qtests
 *
 * Copyright (c) 2026 Phytium Technology
 *
 * Author:
 *  Tao Tang <tangtao1634@phytium.com.cn>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QTEST_LIBQOS_IOMMU_TESTDEV_H
#define QTEST_LIBQOS_IOMMU_TESTDEV_H

#include "pci.h"
#include "hw/misc/iommu-testdev.h"

typedef uint32_t (*QOSIOMMUTestdevSetupFn)(void *opaque);
typedef uint32_t (*QOSIOMMUTestdevAttrsFn)(void *opaque);
typedef bool (*QOSIOMMUTestdevValidateFn)(void *opaque);
typedef void (*QOSIOMMUTestdevReportFn)(void *opaque, uint32_t dma_result);

typedef struct QOSIOMMUTestdevDmaCfg {
    QPCIDevice *dev;
    QPCIBar bar;
    uint64_t iova;
    uint64_t gpa;
    uint32_t len;
} QOSIOMMUTestdevDmaCfg;

uint32_t qos_iommu_testdev_trigger_dma(QPCIDevice *dev, QPCIBar bar,
                                       uint64_t iova, uint64_t gpa,
                                       uint32_t len, uint32_t attrs);

void qos_iommu_testdev_single_translation(const QOSIOMMUTestdevDmaCfg *dma,
                                          void *opaque,
                                          QOSIOMMUTestdevSetupFn setup_fn,
                                          QOSIOMMUTestdevAttrsFn attrs_fn,
                                          QOSIOMMUTestdevValidateFn validate_fn,
                                          QOSIOMMUTestdevReportFn report_fn,
                                          uint32_t *dma_result_out);

#endif /* QTEST_LIBQOS_IOMMU_TESTDEV_H */
