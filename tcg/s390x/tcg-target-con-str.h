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
REGS('L', ALL_GENERAL_REGS & ~SOFTMMU_RESERVE_REGS)
REGS('v', ALL_VECTOR_REGS)
/*
 * A (single) even/odd pair for division.
 * TODO: Add something to the register allocator to allow
 * this kind of regno+1 pairing to be done more generally.
 */
REGS('a', 1u << TCG_REG_R2)
REGS('b', 1u << TCG_REG_R3)

/*
 * Define constraint letters for constants:
 * CONST(letter, TCG_CT_CONST_* bit set)
 */
CONST('A', TCG_CT_CONST_S33)
CONST('I', TCG_CT_CONST_S16)
CONST('J', TCG_CT_CONST_S32)
CONST('Z', TCG_CT_CONST_ZERO)
