/*
 * Type definitions for the mshv guest interface.
 *
 * Copyright Microsoft, Corp. 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_HYPERV_HVGDK_H
#define HW_HYPERV_HVGDK_H

#define HVGDK_H_VERSION         (25125)

enum hv_unimplemented_msr_action {
    HV_UNIMPLEMENTED_MSR_ACTION_FAULT = 0,
    HV_UNIMPLEMENTED_MSR_ACTION_IGNORE_WRITE_READ_ZERO = 1,
    HV_UNIMPLEMENTED_MSR_ACTION_COUNT = 2,
};

#endif /* HW_HYPERV_HVGDK_H */
