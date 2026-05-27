/*
 * QEMU binary/target API (qemu-system-riscv32)
 *
 *  Copyright (c) rev.ng Labs Srl.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/target-info-impl.h"
#include "qemu/target-info-init.h"
#include "hw/riscv/machines-qom.h"
#include "target/riscv/cpu-qom.h"
#include "target/riscv/cpu-param.h"

static const TargetInfo target_info_riscv32_system = {
    .target_name = "riscv32",
    .target_arch = SYS_EMU_TARGET_RISCV32,
    .long_bits = 32,
    .cpu_type = TYPE_RISCV_CPU,
    .machine_typename = TYPE_TARGET_RISCV32_MACHINE,
    .endianness = ENDIAN_MODE_LITTLE,
    .page_bits_init = TARGET_PAGE_BITS,
};

target_info_init(target_info_riscv32_system)
