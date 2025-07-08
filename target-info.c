/*
 * QEMU target info helpers
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/target-info.h"
#include "qemu/target-info-qapi.h"
#include "qemu/target-info-impl.h"
#include "qapi/error.h"

const char *target_name(void)
{
    return target_info()->target_name;
}

unsigned target_long_bits(void)
{
    return target_info()->long_bits;
}

SysEmuTarget target_arch(void)
{
    return qapi_enum_parse(&SysEmuTarget_lookup, target_name(), -1,
                           &error_abort);
}

const char *target_cpu_type(void)
{
    return target_info()->cpu_type;
}

const char *target_machine_typename(void)
{
    return target_info()->machine_typename;
}
