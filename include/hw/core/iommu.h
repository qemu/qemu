/*
 * General vIOMMU flags
 *
 * Copyright (C) 2026 Intel Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IOMMU_H
#define HW_IOMMU_H

#include "qemu/bitops.h"

/*
 * Theoretical vIOMMU flags. Only determined by the vIOMMU device properties and
 * independent on the actual host IOMMU capabilities they may depend on. Each
 * flag can be an expectation or request to other sub-system or just a pure
 * vIOMMU capability. vIOMMU can choose which flags to expose.
 */
enum viommu_flags {
    /* vIOMMU needs nesting parent HWPT to create nested HWPT */
    VIOMMU_FLAG_WANT_NESTING_PARENT = BIT_ULL(0),
};

/* Host IOMMU quirks. Extracted from host IOMMU capabilities */
enum host_iommu_quirks {
    HOST_IOMMU_QUIRK_NESTING_PARENT_BYPASS_RO = BIT_ULL(0),
};

#endif /* HW_IOMMU_H */
