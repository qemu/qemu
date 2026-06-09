/*
 * Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
 * NVIDIA Tegra241 CMDQ-Virtualization extension for SMMUv3
 *
 * Written by Nicolin Chen, Shameer Kolothum
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_TEGRA241_CMDQV_H
#define HW_ARM_TEGRA241_CMDQV_H

#define CMDQV_VER                 1
#define CMDQV_NUM_CMDQ_LOG2       1
#define CMDQV_NUM_SID_PER_VI_LOG2 4

/*
 * Tegra241 CMDQV MMIO layout (64KB pages)
 *
 * 0x00000  (CMDQ-V Config page)
 * 0x10000  (CMDQ-V CMDQ Page0)
 * 0x20000  (CMDQ-V CMDQ Page1)
 * 0x30000  (Virtual Interface Page0)
 * 0x40000  (Virtual Interface Page1)
 */
#define TEGRA241_CMDQV_IO_LEN 0x50000

#define VINTF_PAGE_SIZE 0x10000

struct iommu_viommu_tegra241_cmdqv;

typedef struct Tegra241CMDQV {
    struct iommu_viommu_tegra241_cmdqv *cmdqv_data;
    SMMUv3AccelState *s_accel;
    MemoryRegion mmio_cmdqv;
    qemu_irq irq;
    IOMMUFDVeventq *veventq;
    void *vintf_page0;
} Tegra241CMDQV;

const SMMUv3AccelCmdqvOps *tegra241_cmdqv_get_ops(void);

#endif /* HW_ARM_TEGRA241_CMDQV_H */
