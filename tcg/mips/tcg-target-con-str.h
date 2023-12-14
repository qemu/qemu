/* SPDX-License-Identifier: MIT */
/*
 * Define MIPS target-specific operand constraints.
 * Copyright (c) 2021 Linaro
 */

/*
 * Define constraint letters for register sets:
 * REGS(letter, register_mask)
 */
REGS('r', ALL_GENERAL_REGS)

/*
 * Define constraint letters for constants:
 * CONST(letter, TCG_CT_CONST_* bit set)
 */
CONST('I', TCG_CT_CONST_U16)
CONST('J', TCG_CT_CONST_S16)
CONST('K', TCG_CT_CONST_P2M1)
CONST('N', TCG_CT_CONST_N16)
CONST('W', TCG_CT_CONST_WSZ)
CONST('Z', TCG_CT_CONST_ZERO)
