/*
 * QEMU target info stubs (target specific)
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/target-info.h"
#include "cpu.h"

const char *target_cpu_type(void)
{
    return CPU_RESOLVING_TYPE;
}
