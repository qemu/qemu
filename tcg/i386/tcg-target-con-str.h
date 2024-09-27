/* SPDX-License-Identifier: MIT */
/*
 * Define i386 target-specific operand constraints.
 * Copyright (c) 2021 Linaro
 *
 */

/*
 * Define constraint letters for register sets:
 * REGS(letter, register_mask)
 */
REGS('a', 1u << TCG_REG_EAX)
REGS('b', 1u << TCG_REG_EBX)
REGS('c', 1u << TCG_REG_ECX)
REGS('d', 1u << TCG_REG_EDX)
REGS('S', 1u << TCG_REG_ESI)
REGS('D', 1u << TCG_REG_EDI)

REGS('r', ALL_GENERAL_REGS)
REGS('x', ALL_VECTOR_REGS)
REGS('q', ALL_BYTEL_REGS)     /* regs that can be used as a byte operand */
REGS('L', ALL_GENERAL_REGS & ~SOFTMMU_RESERVE_REGS)  /* qemu_ld/st */
REGS('s', ALL_BYTEL_REGS & ~SOFTMMU_RESERVE_REGS)    /* qemu_st8_i32 data */

/*
 * Define constraint letters for constants:
 * CONST(letter, TCG_CT_CONST_* bit set)
 */
CONST('e', TCG_CT_CONST_S32)
CONST('I', TCG_CT_CONST_I32)
CONST('O', TCG_CT_CONST_ZERO)
CONST('T', TCG_CT_CONST_TST)
CONST('W', TCG_CT_CONST_WSZ)
CONST('Z', TCG_CT_CONST_U32)
