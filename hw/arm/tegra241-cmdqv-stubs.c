/*
 * Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved
 *
 * Stubs for Tegra241 CMDQ-Virtualization extension for SMMUv3
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "smmuv3-accel.h"
#include "hw/arm/tegra241-cmdqv.h"

const SMMUv3AccelCmdqvOps *tegra241_cmdqv_get_ops(void)
{
    return NULL;
}
