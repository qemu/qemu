/* SPDX-License-Identifier: MIT */
/*
 * Define LoongArch target-specific operand constraints.
 *
 * Copyright (c) 2021 WANG Xuerui <git@xen0n.name>
 *
 * Based on tcg/riscv/tcg-target-con-str.h
 *
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
CONST('I', TCG_CT_CONST_S12)
CONST('J', TCG_CT_CONST_S32)
CONST('U', TCG_CT_CONST_U12)
CONST('C', TCG_CT_CONST_C12)
CONST('W', TCG_CT_CONST_WSZ)
CONST('M', TCG_CT_CONST_VCMP)
CONST('A', TCG_CT_CONST_VADD)
