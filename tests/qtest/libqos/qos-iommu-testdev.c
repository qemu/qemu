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

#include "qemu/osdep.h"
#include "../libqtest.h"
#include "pci.h"
#include "qos-iommu-testdev.h"

uint32_t qos_iommu_testdev_trigger_dma(QPCIDevice *dev, QPCIBar bar,
                                       uint64_t iova, uint64_t gpa,
                                       uint32_t len, uint32_t attrs)
{
    uint32_t result = ITD_DMA_RESULT_BUSY;

    qpci_io_writel(dev, bar, ITD_REG_DMA_GVA_LO, (uint32_t)iova);
    qpci_io_writel(dev, bar, ITD_REG_DMA_GVA_HI, (uint32_t)(iova >> 32));
    qpci_io_writel(dev, bar, ITD_REG_DMA_GPA_LO, (uint32_t)gpa);
    qpci_io_writel(dev, bar, ITD_REG_DMA_GPA_HI, (uint32_t)(gpa >> 32));
    qpci_io_writel(dev, bar, ITD_REG_DMA_LEN, len);
    qpci_io_writel(dev, bar, ITD_REG_DMA_ATTRS, attrs);

    qpci_io_writel(dev, bar, ITD_REG_DMA_DBELL, ITD_DMA_DBELL_ARM);
    qpci_io_readl(dev, bar, ITD_REG_DMA_TRIGGERING);

    for (int i = 0; i < 1000; i++) {
        result = qpci_io_readl(dev, bar, ITD_REG_DMA_RESULT);
        if (result != ITD_DMA_RESULT_BUSY) {
            break;
        }
        g_usleep(1000);
    }

    if (result == ITD_DMA_RESULT_BUSY) {
        return ITD_DMA_ERR_TX_FAIL;
    }

    return result;
}

void qos_iommu_testdev_single_translation(const QOSIOMMUTestdevDmaCfg *dma,
                                          void *opaque,
                                          QOSIOMMUTestdevSetupFn setup_fn,
                                          QOSIOMMUTestdevAttrsFn attrs_fn,
                                          QOSIOMMUTestdevValidateFn validate_fn,
                                          QOSIOMMUTestdevReportFn report_fn,
                                          uint32_t *dma_result_out)
{
    uint32_t config_result;
    uint32_t dma_result;
    uint32_t attrs_val;

    g_assert(dma);
    g_assert(setup_fn);
    g_assert(attrs_fn);

    config_result = setup_fn(opaque);
    g_assert_cmpuint(config_result, ==, 0);

    attrs_val = attrs_fn(opaque);
    dma_result = qos_iommu_testdev_trigger_dma(dma->dev, dma->bar,
                                               dma->iova, dma->gpa,
                                               dma->len, attrs_val);
    if (dma_result_out) {
        *dma_result_out = dma_result;
    }

    if (report_fn) {
        report_fn(opaque, dma_result);
    }

    if (validate_fn) {
        g_assert_true(validate_fn(opaque));
    }
}
