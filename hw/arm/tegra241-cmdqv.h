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

const SMMUv3AccelCmdqvOps *tegra241_cmdqv_get_ops(void);

#endif /* HW_ARM_TEGRA241_CMDQV_H */
