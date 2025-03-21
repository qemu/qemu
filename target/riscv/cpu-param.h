/*
 * RISC-V cpu parameters for qemu.
 *
 * Copyright (c) 2017-2018 SiFive, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RISCV_CPU_PARAM_H
#define RISCV_CPU_PARAM_H

#if defined(TARGET_RISCV64)
# define TARGET_PHYS_ADDR_SPACE_BITS 56 /* 44-bit PPN */
# define TARGET_VIRT_ADDR_SPACE_BITS 48 /* sv48 */
#elif defined(TARGET_RISCV32)
# define TARGET_PHYS_ADDR_SPACE_BITS 34 /* 22-bit PPN */
# define TARGET_VIRT_ADDR_SPACE_BITS 32 /* sv32 */
#endif
#define TARGET_PAGE_BITS 12 /* 4 KiB Pages */

/*
 * RISC-V-specific extra insn start words:
 * 1: Original instruction opcode
 * 2: more information about instruction
 */
#define TARGET_INSN_START_EXTRA_WORDS 2

/*
 * The current MMU Modes are:
 *  - U mode 0b000
 *  - S mode 0b001
 *  - M mode 0b011
 *  - U mode HLV/HLVX/HSV 0b100
 *  - S mode HLV/HLVX/HSV 0b101
 *  - M mode HLV/HLVX/HSV 0b111
 */

#endif
