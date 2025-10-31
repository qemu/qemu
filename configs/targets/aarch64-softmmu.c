/*
 * QEMU binary/target API (qemu-system-aarch64)
 *
 *  Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/target-info-impl.h"
#include "hw/arm/machines-qom.h"
#include "target/arm/cpu-qom.h"

static const TargetInfo target_info_aarch64_system = {
    .target_name = "aarch64",
    .target_arch = SYS_EMU_TARGET_AARCH64,
    .long_bits = 64,
    .cpu_type = TYPE_ARM_CPU,
    .machine_typename = TYPE_TARGET_AARCH64_MACHINE,
    .endianness = ENDIAN_MODE_LITTLE,
};

const TargetInfo *target_info(void)
{
    return &target_info_aarch64_system;
}
