/*
 * QEMU binary/target API (qemu-system-arm)
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/target-info-impl.h"
#include "hw/arm/machines-qom.h"
#include "target/arm/cpu-qom.h"

static const TargetInfo target_info_arm_system = {
    .target_name = "arm",
    .target_arch = SYS_EMU_TARGET_ARM,
    .long_bits = 32,
    .cpu_type = TYPE_ARM_CPU,
    .machine_typename = TYPE_TARGET_ARM_MACHINE,
    .endianness = ENDIAN_MODE_LITTLE,
};

const TargetInfo *target_info(void)
{
    return &target_info_arm_system;
}
