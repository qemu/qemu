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
#include "system/iommufd.h"
#ifdef CONFIG_LINUX
#include <linux/iommufd.h>
#endif
#include CONFIG_DEVICES

/*
 * Represents an accelerated SMMU instance backed by an iommufd vIOMMU object.
 * Holds bypass and abort proxy HWPT IDs used for device attachment.
 */
typedef struct SMMUv3AccelState {
    IOMMUFDViommu *viommu;
    uint32_t bypass_hwpt_id;
    uint32_t abort_hwpt_id;
    QLIST_HEAD(, SMMUv3AccelDevice) device_list;
} SMMUv3AccelState;

typedef struct SMMUv3AccelDevice {
    SMMUDevice sdev;
    HostIOMMUDeviceIOMMUFD *idev;
    QLIST_ENTRY(SMMUv3AccelDevice) next;
    SMMUv3AccelState *s_accel;
} SMMUv3AccelDevice;

#ifdef CONFIG_ARM_SMMUV3_ACCEL
void smmuv3_accel_init(SMMUv3State *s);
#else
static inline void smmuv3_accel_init(SMMUv3State *s)
{
}
#endif

#endif /* HW_ARM_SMMUV3_ACCEL_H */
