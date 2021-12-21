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
REGS('L', ALL_GENERAL_REGS & ~SOFTMMU_RESERVE_REGS)

/*
 * Define constraint letters for constants:
 * CONST(letter, TCG_CT_CONST_* bit set)
 */
CONST('I', TCG_CT_CONST_S12)
CONST('N', TCG_CT_CONST_N12)
CONST('U', TCG_CT_CONST_U12)
CONST('Z', TCG_CT_CONST_ZERO)
CONST('C', TCG_CT_CONST_C12)
CONST('W', TCG_CT_CONST_WSZ)
