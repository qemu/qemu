/*
 * QEMU base architecture bit definitions
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_BASE_ARCH_DEFS_H
#define QEMU_BASE_ARCH_DEFS_H

#include "qapi/qapi-types-machine.h"

#define QEMU_ARCH_BIT(target) (1ULL << (target))

enum {
    QEMU_ARCH_ALPHA =       QEMU_ARCH_BIT(SYS_EMU_TARGET_ALPHA),
    QEMU_ARCH_ARM =         QEMU_ARCH_BIT(SYS_EMU_TARGET_ARM) |
                            QEMU_ARCH_BIT(SYS_EMU_TARGET_AARCH64),
    QEMU_ARCH_I386 =        QEMU_ARCH_BIT(SYS_EMU_TARGET_I386) |
                            QEMU_ARCH_BIT(SYS_EMU_TARGET_X86_64),
    QEMU_ARCH_M68K =        QEMU_ARCH_BIT(SYS_EMU_TARGET_M68K),
    QEMU_ARCH_MICROBLAZE =  QEMU_ARCH_BIT(SYS_EMU_TARGET_MICROBLAZE),
    QEMU_ARCH_MIPS =        QEMU_ARCH_BIT(SYS_EMU_TARGET_MIPS) |
                            QEMU_ARCH_BIT(SYS_EMU_TARGET_MIPSEL) |
                            QEMU_ARCH_BIT(SYS_EMU_TARGET_MIPS64) |
                            QEMU_ARCH_BIT(SYS_EMU_TARGET_MIPS64EL),
    QEMU_ARCH_PPC =         QEMU_ARCH_BIT(SYS_EMU_TARGET_PPC) |
                            QEMU_ARCH_BIT(SYS_EMU_TARGET_PPC64),
    QEMU_ARCH_S390X =       QEMU_ARCH_BIT(SYS_EMU_TARGET_S390X),
    QEMU_ARCH_SH4 =         QEMU_ARCH_BIT(SYS_EMU_TARGET_SH4) |
                            QEMU_ARCH_BIT(SYS_EMU_TARGET_SH4EB),
    QEMU_ARCH_SPARC =       QEMU_ARCH_BIT(SYS_EMU_TARGET_SPARC) |
                            QEMU_ARCH_BIT(SYS_EMU_TARGET_SPARC64),
    QEMU_ARCH_XTENSA =      QEMU_ARCH_BIT(SYS_EMU_TARGET_XTENSA) |
                            QEMU_ARCH_BIT(SYS_EMU_TARGET_XTENSAEB),
    QEMU_ARCH_OR1K =        QEMU_ARCH_BIT(SYS_EMU_TARGET_OR1K),
    QEMU_ARCH_TRICORE =     QEMU_ARCH_BIT(SYS_EMU_TARGET_TRICORE),
    QEMU_ARCH_HPPA =        QEMU_ARCH_BIT(SYS_EMU_TARGET_HPPA),
    QEMU_ARCH_RISCV =       QEMU_ARCH_BIT(SYS_EMU_TARGET_RISCV32) |
                            QEMU_ARCH_BIT(SYS_EMU_TARGET_RISCV64),
    QEMU_ARCH_RX =          QEMU_ARCH_BIT(SYS_EMU_TARGET_RX),
    QEMU_ARCH_AVR =         QEMU_ARCH_BIT(SYS_EMU_TARGET_AVR),
    QEMU_ARCH_HEXAGON =     QEMU_ARCH_BIT(SYS_EMU_TARGET_HEXAGON),
    QEMU_ARCH_LOONGARCH =   QEMU_ARCH_BIT(SYS_EMU_TARGET_LOONGARCH64),
    QEMU_ARCH_ALL =         QEMU_ARCH_BIT(SYS_EMU_TARGET__MAX) - 1,
};

/* Keep target-specific masks within qemu_opt_set_number()'s int64_t range. */
QEMU_BUILD_BUG_ON(SYS_EMU_TARGET__MAX > 63);

/**
 * qemu_arch_available:
 * @arch_bitmask: bitmask of QEMU_ARCH_* constants
 *
 * Return whether the current target architecture is contained in @arch_bitmask
 */
bool qemu_arch_available(uint64_t arch_bitmask);

#endif
