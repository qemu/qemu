/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Define AArch64 target-specific operand constraints.
 * Copyright (c) 2021 Linaro
 */

/*
 * Define constraint letters for register sets:
 * REGS(letter, register_mask)
 */
REGS('r', ALL_GENERAL_REGS)
REGS('w', ALL_VECTOR_REGS)

/*
 * Define constraint letters for constants:
 * CONST(letter, TCG_CT_CONST_* bit set)
 */
CONST('A', TCG_CT_CONST_AIMM)
CONST('L', TCG_CT_CONST_LIMM)
CONST('M', TCG_CT_CONST_MONE)
CONST('O', TCG_CT_CONST_ORRI)
CONST('N', TCG_CT_CONST_ANDI)
CONST('Z', TCG_CT_CONST_ZERO)
