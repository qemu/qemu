/* SPDX-License-Identifier: MIT */
/*
 * Define S390 target-specific operand constraints.
 * Copyright (c) 2021 Linaro
 */

/*
 * Define constraint letters for register sets:
 * REGS(letter, register_mask)
 */
REGS('r', ALL_GENERAL_REGS)
REGS('v', ALL_VECTOR_REGS)
REGS('o', 0xaaaa) /* odd numbered general regs */

/*
 * Define constraint letters for constants:
 * CONST(letter, TCG_CT_CONST_* bit set)
 */
CONST('C', TCG_CT_CONST_CMP)
CONST('I', TCG_CT_CONST_S16)
CONST('J', TCG_CT_CONST_S32)
CONST('K', TCG_CT_CONST_P32)
CONST('N', TCG_CT_CONST_INV)
CONST('R', TCG_CT_CONST_INVRISBG)
CONST('U', TCG_CT_CONST_U32)
CONST('Z', TCG_CT_CONST_ZERO)
