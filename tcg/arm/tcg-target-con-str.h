/* SPDX-License-Identifier: MIT */
/*
 * Define Arm target-specific operand constraints.
 * Copyright (c) 2021 Linaro
 */

/*
 * Define constraint letters for register sets:
 * REGS(letter, register_mask)
 */
REGS('r', ALL_GENERAL_REGS)
REGS('l', ALL_QLOAD_REGS)
REGS('s', ALL_QSTORE_REGS)

/*
 * Define constraint letters for constants:
 * CONST(letter, TCG_CT_CONST_* bit set)
 */
CONST('I', TCG_CT_CONST_ARM)
CONST('K', TCG_CT_CONST_INV)
CONST('N', TCG_CT_CONST_NEG)
CONST('Z', TCG_CT_CONST_ZERO)
