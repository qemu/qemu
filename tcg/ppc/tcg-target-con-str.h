/* SPDX-License-Identifier: MIT */
/*
 * Define PowerPC target-specific operand constraints.
 * Copyright (c) 2021 Linaro
 */

/*
 * Define constraint letters for register sets:
 * REGS(letter, register_mask)
 */
REGS('r', ALL_GENERAL_REGS)
REGS('v', ALL_VECTOR_REGS)
REGS('A', 1u << TCG_REG_R3)
REGS('B', 1u << TCG_REG_R4)
REGS('C', 1u << TCG_REG_R5)
REGS('D', 1u << TCG_REG_R6)
REGS('L', ALL_QLOAD_REGS)
REGS('S', ALL_QSTORE_REGS)

/*
 * Define constraint letters for constants:
 * CONST(letter, TCG_CT_CONST_* bit set)
 */
CONST('I', TCG_CT_CONST_S16)
CONST('J', TCG_CT_CONST_U16)
CONST('M', TCG_CT_CONST_MONE)
CONST('T', TCG_CT_CONST_S32)
CONST('U', TCG_CT_CONST_U32)
CONST('W', TCG_CT_CONST_WSZ)
CONST('Z', TCG_CT_CONST_ZERO)
