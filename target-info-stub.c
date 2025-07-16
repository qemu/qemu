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
#include "hw/boards.h"
#include "cpu.h"

static const TargetInfo target_info_stub = {
    .target_name = TARGET_NAME,
    .target_arch = SYS_EMU_TARGET__MAX,
    .long_bits = TARGET_LONG_BITS,
    .cpu_type = CPU_RESOLVING_TYPE,
    .machine_typename = TYPE_MACHINE,
    .endianness = TARGET_BIG_ENDIAN ? ENDIAN_MODE_BIG : ENDIAN_MODE_LITTLE,
};

const TargetInfo *target_info(void)
{
    return &target_info_stub;
}
