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
#include "hw/arm/smmuv3.h"
#include "system/iommufd.h"
#ifdef CONFIG_LINUX
#include <linux/iommufd.h>
#endif

typedef enum SMMUv3AccelCmdqvType {
    SMMUV3_CMDQV_NONE = 0,
    SMMUV3_CMDQV_TEGRA241,
} SMMUv3AccelCmdqvType;

/*
 * CMDQ-Virtualization (CMDQV) hardware support, extends the SMMUv3 to
 * support multiple VCMDQs with virtualization capabilities.
 * CMDQV specific behavior is factored behind this ops interface.
 */
typedef struct SMMUv3AccelCmdqvOps {
    /**
     * @probe: Mandatory. Vendor-specific device probing.
     */
    bool (*probe)(SMMUv3State *s, HostIOMMUDeviceIOMMUFD *idev, Error **errp);
    /**
     * @init: Optional callback. Initialize CMDQV hardware.
     */
    bool (*init)(SMMUv3State *s, Error **errp);
    /**
     * @alloc_viommu: Mandatory. Allocate CMDQV viommu resources.
     */
    bool (*alloc_viommu)(SMMUv3State *s,
                         HostIOMMUDeviceIOMMUFD *idev,
                         uint32_t *out_viommu_id,
                         Error **errp);
    /**
     * @free_viommu: Optional callback. Free CMDQV viommu resources.
     * If NULL, the viommu_id is freed directly via iommufd_backend_free_id().
     */
    void (*free_viommu)(SMMUv3State *s);
    /**
     * @get_type: Optional callback. Return the CMDQV implementation type.
     */
    SMMUv3AccelCmdqvType (*get_type)(void);
    /**
     * @reset: Optional callback. Reset CMDQV state.
     */
    void (*reset)(SMMUv3State *s);
} SMMUv3AccelCmdqvOps;

/*
 * Represents an accelerated SMMU instance backed by an iommufd vIOMMU object.
 * Holds bypass and abort proxy HWPT IDs used for device attachment.
 */
typedef struct SMMUv3AccelState {
    IOMMUFDViommu *viommu;
    IOMMUFDVeventq *veventq;
    uint32_t bypass_hwpt_id;
    uint32_t abort_hwpt_id;
    QLIST_HEAD(, SMMUv3AccelDevice) device_list;
    bool auto_mode;
    bool auto_finalised;
    const SMMUv3AccelCmdqvOps *cmdqv_ops;
    void *cmdqv;  /* vendor specific CMDQV state */
} SMMUv3AccelState;

typedef struct SMMUS1Hwpt {
    uint32_t hwpt_id;
} SMMUS1Hwpt;

typedef struct SMMUv3AccelDevice {
    SMMUDevice sdev;
    HostIOMMUDeviceIOMMUFD *hiodi;
    SMMUS1Hwpt *s1_hwpt;
    IOMMUFDVdev *vdev;
    QLIST_ENTRY(SMMUv3AccelDevice) next;
    SMMUv3AccelState *s_accel;
    Error *unplug_blocker; /* set when CMDQV is active to block hot-unplug */
} SMMUv3AccelDevice;

bool smmuv3_accel_init(SMMUv3State *s, Error **errp);
bool smmuv3_accel_install_ste(SMMUv3State *s, SMMUDevice *sdev, int sid,
                              Error **errp);
bool smmuv3_accel_install_ste_range(SMMUv3State *s, SMMUSIDRange *range,
                                    Error **errp);
bool smmuv3_accel_attach_gbpa_hwpt(SMMUv3State *s, Error **errp);
bool smmuv3_accel_issue_inv_cmd(SMMUv3State *s, void *cmd, SMMUDevice *sdev,
                                Error **errp);
void smmuv3_accel_idr_override(SMMUv3State *s);
bool smmuv3_accel_alloc_veventq(SMMUv3State *s, Error **errp);
bool smmuv3_accel_event_read_validate(IOMMUFDVeventq *veventq, uint32_t type,
                                      void *buf, size_t size, Error **errp);
void smmuv3_accel_reset(SMMUv3State *s);
SMMUv3AccelCmdqvType smmuv3_accel_cmdqv_type(Object *obj);

#endif /* HW_ARM_SMMUV3_ACCEL_H */
