/* SPDX-License-Identifier: MIT */
/*
 * Define Sparc target-specific operand constraints.
 * Copyright (c) 2021 Linaro
 */

/*
 * Define constraint letters for register sets:
 * REGS(letter, register_mask)
 */
REGS('r', ALL_GENERAL_REGS)
REGS('R', ALL_GENERAL_REGS64)
REGS('s', ALL_QLDST_REGS)
REGS('S', ALL_QLDST_REGS64)
REGS('A', TARGET_LONG_BITS == 64 ? ALL_QLDST_REGS64 : ALL_QLDST_REGS)

/*
 * Define constraint letters for constants:
 * CONST(letter, TCG_CT_CONST_* bit set)
 */
CONST('I', TCG_CT_CONST_S11)
CONST('J', TCG_CT_CONST_S13)
CONST('Z', TCG_CT_CONST_ZERO)
