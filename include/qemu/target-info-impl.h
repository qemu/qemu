/*
 * QEMU TargetInfo structure definition
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_TARGET_INFO_IMPL_H
#define QEMU_TARGET_INFO_IMPL_H

#include "qemu/target-info.h"

typedef struct TargetInfo {
    /* runtime equivalent of TARGET_NAME definition */
    const char *target_name;
    /* QOM typename machines for this binary must implement */
    const char *machine_typename;
} TargetInfo;

/**
 * target_info:
 *
 * Returns: The TargetInfo structure definition for this target binary.
 */
const TargetInfo *target_info(void);

#endif
