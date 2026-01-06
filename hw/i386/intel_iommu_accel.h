/*
 * Intel IOMMU acceleration with nested translation
 *
 * Copyright (C) 2026 Intel Corporation.
 *
 * Authors: Zhenzhong Duan <zhenzhong.duan@intel.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_I386_INTEL_IOMMU_ACCEL_H
#define HW_I386_INTEL_IOMMU_ACCEL_H
#include CONFIG_DEVICES

#ifdef CONFIG_VTD_ACCEL
bool vtd_check_hiod_accel(IntelIOMMUState *s, HostIOMMUDevice *hiod,
                          Error **errp);
#else
static inline bool vtd_check_hiod_accel(IntelIOMMUState *s,
                                        HostIOMMUDevice *hiod,
                                        Error **errp)
{
    error_setg(errp, "host IOMMU cannot be checked!");
    error_append_hint(errp, "CONFIG_VTD_ACCEL is not enabled");
    return false;
}
#endif
#endif
