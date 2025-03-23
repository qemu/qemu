/*
 * QEMU target info stubs (target specific)
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/target-info.h"
#include "qemu/target-info-impl.h"
#include "cpu.h"

static const TargetInfo target_info_stub = {
    .target_name = TARGET_NAME,
};

const TargetInfo *target_info(void)
{
    return &target_info_stub;
}

const char *target_cpu_type(void)
{
    return CPU_RESOLVING_TYPE;
}
