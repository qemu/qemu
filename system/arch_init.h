#ifndef QEMU_ARCH_INIT_H
#define QEMU_ARCH_INIT_H

#include "qapi/qapi-types-machine.h"

enum {
    QEMU_ARCH_ALPHA =       (1UL << SYS_EMU_TARGET_ALPHA),
    QEMU_ARCH_ARM =         (1UL << SYS_EMU_TARGET_ARM) |
                            (1UL << SYS_EMU_TARGET_AARCH64),
    QEMU_ARCH_I386 =        (1UL << SYS_EMU_TARGET_I386) |
                            (1UL << SYS_EMU_TARGET_X86_64),
    QEMU_ARCH_M68K =        (1UL << SYS_EMU_TARGET_M68K),
    QEMU_ARCH_MICROBLAZE =  (1UL << SYS_EMU_TARGET_MICROBLAZE) |
                            (1UL << SYS_EMU_TARGET_MICROBLAZEEL),
    QEMU_ARCH_MIPS =        (1UL << SYS_EMU_TARGET_MIPS) |
                            (1UL << SYS_EMU_TARGET_MIPSEL) |
                            (1UL << SYS_EMU_TARGET_MIPS64) |
                            (1UL << SYS_EMU_TARGET_MIPS64EL),
    QEMU_ARCH_PPC =         (1UL << SYS_EMU_TARGET_PPC) |
                            (1UL << SYS_EMU_TARGET_PPC64),
    QEMU_ARCH_S390X =       (1UL << SYS_EMU_TARGET_S390X),
    QEMU_ARCH_SH4 =         (1UL << SYS_EMU_TARGET_SH4) |
                            (1UL << SYS_EMU_TARGET_SH4EB),
    QEMU_ARCH_SPARC =       (1UL << SYS_EMU_TARGET_SPARC) |
                            (1UL << SYS_EMU_TARGET_SPARC64),
    QEMU_ARCH_XTENSA =      (1UL << SYS_EMU_TARGET_XTENSA) |
                            (1UL << SYS_EMU_TARGET_XTENSAEB),
    QEMU_ARCH_OR1K =        (1UL << SYS_EMU_TARGET_OR1K),
    QEMU_ARCH_TRICORE =     (1UL << SYS_EMU_TARGET_TRICORE),
    QEMU_ARCH_HPPA =        (1UL << SYS_EMU_TARGET_HPPA),
    QEMU_ARCH_RISCV =       (1UL << SYS_EMU_TARGET_RISCV32) |
                            (1UL << SYS_EMU_TARGET_RISCV64),
    QEMU_ARCH_RX =          (1UL << SYS_EMU_TARGET_RX),
    QEMU_ARCH_AVR =         (1UL << SYS_EMU_TARGET_AVR),
    QEMU_ARCH_HEXAGON =     (1UL << SYS_EMU_TARGET_HEXAGON),
    QEMU_ARCH_LOONGARCH =   (1UL << SYS_EMU_TARGET_LOONGARCH64),
    QEMU_ARCH_ALL =         UINT32_MAX,
};

QEMU_BUILD_BUG_ON(SYS_EMU_TARGET__MAX > 32);

/**
 * qemu_arch_available:
 * @arch_bitmask: bitmask of QEMU_ARCH_* constants
 *
 * Return whether the current target architecture is contained in @arch_bitmask
 */
bool qemu_arch_available(uint32_t arch_bitmask);

#endif
