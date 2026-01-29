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

typedef struct SMMUS1Hwpt {
    uint32_t hwpt_id;
} SMMUS1Hwpt;

typedef struct SMMUv3AccelDevice {
    SMMUDevice sdev;
    HostIOMMUDeviceIOMMUFD *idev;
    SMMUS1Hwpt *s1_hwpt;
    IOMMUFDVdev *vdev;
    QLIST_ENTRY(SMMUv3AccelDevice) next;
    SMMUv3AccelState *s_accel;
} SMMUv3AccelDevice;

#ifdef CONFIG_ARM_SMMUV3_ACCEL
void smmuv3_accel_init(SMMUv3State *s);
bool smmuv3_accel_install_ste(SMMUv3State *s, SMMUDevice *sdev, int sid,
                              Error **errp);
bool smmuv3_accel_install_ste_range(SMMUv3State *s, SMMUSIDRange *range,
                                    Error **errp);
bool smmuv3_accel_attach_gbpa_hwpt(SMMUv3State *s, Error **errp);
bool smmuv3_accel_issue_inv_cmd(SMMUv3State *s, void *cmd, SMMUDevice *sdev,
                                Error **errp);
void smmuv3_accel_idr_override(SMMUv3State *s);
void smmuv3_accel_reset(SMMUv3State *s);
#else
static inline void smmuv3_accel_init(SMMUv3State *s)
{
}
static inline bool
smmuv3_accel_install_ste(SMMUv3State *s, SMMUDevice *sdev, int sid,
                         Error **errp)
{
    return true;
}
static inline bool
smmuv3_accel_install_ste_range(SMMUv3State *s, SMMUSIDRange *range,
                               Error **errp)
{
    return true;
}
static inline bool smmuv3_accel_attach_gbpa_hwpt(SMMUv3State *s, Error **errp)
{
    return true;
}
static inline bool
smmuv3_accel_issue_inv_cmd(SMMUv3State *s, void *cmd, SMMUDevice *sdev,
                           Error **errp)
{
    return true;
}
static inline void smmuv3_accel_idr_override(SMMUv3State *s)
{
}
static inline void smmuv3_accel_reset(SMMUv3State *s)
{
}
#endif

#endif /* HW_ARM_SMMUV3_ACCEL_H */
