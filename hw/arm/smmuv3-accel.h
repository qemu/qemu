/*
 * Copyright (c) 2025 Huawei Technologies R & D (UK) Ltd
 * Copyright (C) 2025 NVIDIA
 * Written by Nicolin Chen, Shameer Kolothum
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_SMMUV3_ACCEL_H
#define HW_ARM_SMMUV3_ACCEL_H

#include "hw/arm/smmu-common.h"
#include CONFIG_DEVICES

typedef struct SMMUv3AccelDevice {
    SMMUDevice sdev;
} SMMUv3AccelDevice;

#ifdef CONFIG_ARM_SMMUV3_ACCEL
void smmuv3_accel_init(SMMUv3State *s);
#else
static inline void smmuv3_accel_init(SMMUv3State *s)
{
}
#endif

#endif /* HW_ARM_SMMUV3_ACCEL_H */
