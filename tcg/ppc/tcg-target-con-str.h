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
REGS('o', ALL_GENERAL_REGS & 0xAAAAAAAAu)  /* odd registers */
REGS('v', ALL_VECTOR_REGS)

/*
 * Define constraint letters for constants:
 * CONST(letter, TCG_CT_CONST_* bit set)
 */
CONST('C', TCG_CT_CONST_CMP)
CONST('I', TCG_CT_CONST_S16)
CONST('M', TCG_CT_CONST_MONE)
CONST('T', TCG_CT_CONST_S32)
CONST('U', TCG_CT_CONST_U32)
CONST('W', TCG_CT_CONST_WSZ)
CONST('Z', TCG_CT_CONST_ZERO)
