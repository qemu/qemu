/*
 * Initial TCG Implementation for aarch64
 *
 * Copyright (c) 2013 Huawei Technologies Duesseldorf GmbH
 * Written by Claudio Fontana
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.
 *
 * See the COPYING file in the top-level directory for details.
 */

#ifndef TCG_TARGET_AARCH64
#define TCG_TARGET_AARCH64 1

#define TCG_TARGET_INSN_UNIT_SIZE  4
#undef TCG_TARGET_STACK_GROWSUP

typedef enum {
    TCG_REG_X0, TCG_REG_X1, TCG_REG_X2, TCG_REG_X3,
    TCG_REG_X4, TCG_REG_X5, TCG_REG_X6, TCG_REG_X7,
    TCG_REG_X8, TCG_REG_X9, TCG_REG_X10, TCG_REG_X11,
    TCG_REG_X12, TCG_REG_X13, TCG_REG_X14, TCG_REG_X15,
    TCG_REG_X16, TCG_REG_X17, TCG_REG_X18, TCG_REG_X19,
    TCG_REG_X20, TCG_REG_X21, TCG_REG_X22, TCG_REG_X23,
    TCG_REG_X24, TCG_REG_X25, TCG_REG_X26, TCG_REG_X27,
    TCG_REG_X28, TCG_REG_X29, TCG_REG_X30,

    /* X31 is either the stack pointer or zero, depending on context.  */
    TCG_REG_SP = 31,
    TCG_REG_XZR = 31,

    /* Aliases.  */
    TCG_REG_FP = TCG_REG_X29,
    TCG_REG_LR = TCG_REG_X30,
    TCG_AREG0  = TCG_REG_X19,
} TCGReg;

#define TCG_TARGET_NB_REGS 32

/* used for function call generation */
#define TCG_REG_CALL_STACK              TCG_REG_SP
#define TCG_TARGET_STACK_ALIGN          16
#define TCG_TARGET_CALL_ALIGN_ARGS      1
#define TCG_TARGET_CALL_STACK_OFFSET    0

/* optional instructions */
#define TCG_TARGET_HAS_div_i32          1
#define TCG_TARGET_HAS_rem_i32          1
#define TCG_TARGET_HAS_ext8s_i32        1
#define TCG_TARGET_HAS_ext16s_i32       1
#define TCG_TARGET_HAS_ext8u_i32        1
#define TCG_TARGET_HAS_ext16u_i32       1
#define TCG_TARGET_HAS_bswap16_i32      1
#define TCG_TARGET_HAS_bswap32_i32      1
#define TCG_TARGET_HAS_not_i32          1
#define TCG_TARGET_HAS_neg_i32          1
#define TCG_TARGET_HAS_rot_i32          1
#define TCG_TARGET_HAS_andc_i32         1
#define TCG_TARGET_HAS_orc_i32          1
#define TCG_TARGET_HAS_eqv_i32          1
#define TCG_TARGET_HAS_nand_i32         0
#define TCG_TARGET_HAS_nor_i32          0
#define TCG_TARGET_HAS_deposit_i32      1
#define TCG_TARGET_HAS_movcond_i32      1
#define TCG_TARGET_HAS_add2_i32         1
#define TCG_TARGET_HAS_sub2_i32         1
#define TCG_TARGET_HAS_mulu2_i32        0
#define TCG_TARGET_HAS_muls2_i32        0
#define TCG_TARGET_HAS_muluh_i32        0
#define TCG_TARGET_HAS_mulsh_i32        0
#define TCG_TARGET_HAS_trunc_shr_i32    0

#define TCG_TARGET_HAS_div_i64          1
#define TCG_TARGET_HAS_rem_i64          1
#define TCG_TARGET_HAS_ext8s_i64        1
#define TCG_TARGET_HAS_ext16s_i64       1
#define TCG_TARGET_HAS_ext32s_i64       1
#define TCG_TARGET_HAS_ext8u_i64        1
#define TCG_TARGET_HAS_ext16u_i64       1
#define TCG_TARGET_HAS_ext32u_i64       1
#define TCG_TARGET_HAS_bswap16_i64      1
#define TCG_TARGET_HAS_bswap32_i64      1
#define TCG_TARGET_HAS_bswap64_i64      1
#define TCG_TARGET_HAS_not_i64          1
#define TCG_TARGET_HAS_neg_i64          1
#define TCG_TARGET_HAS_rot_i64          1
#define TCG_TARGET_HAS_andc_i64         1
#define TCG_TARGET_HAS_orc_i64          1
#define TCG_TARGET_HAS_eqv_i64          1
#define TCG_TARGET_HAS_nand_i64         0
#define TCG_TARGET_HAS_nor_i64          0
#define TCG_TARGET_HAS_deposit_i64      1
#define TCG_TARGET_HAS_movcond_i64      1
#define TCG_TARGET_HAS_add2_i64         1
#define TCG_TARGET_HAS_sub2_i64         1
#define TCG_TARGET_HAS_mulu2_i64        0
#define TCG_TARGET_HAS_muls2_i64        0
#define TCG_TARGET_HAS_muluh_i64        1
#define TCG_TARGET_HAS_mulsh_i64        1

static inline void flush_icache_range(uintptr_t start, uintptr_t stop)
{
    __builtin___clear_cache((char *)start, (char *)stop);
}

#endif /* TCG_TARGET_AARCH64 */
