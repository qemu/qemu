/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2009, 2011 Stefan Weil
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* TODO list:
 * - See TODO comments in code.
 */

/* Marker for missing code. */
#define TODO() \
    do { \
        fprintf(stderr, "TODO %s:%u: %s()\n", \
                __FILE__, __LINE__, __func__); \
        tcg_abort(); \
    } while (0)

/* Bitfield n...m (in 32 bit value). */
#define BITS(n, m) (((0xffffffffU << (31 - n)) >> (31 - n + m)) << m)

static TCGConstraintSetIndex tcg_target_op_def(TCGOpcode op)
{
    switch (op) {
    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld_i32:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_ld_i64:
    case INDEX_op_not_i32:
    case INDEX_op_not_i64:
    case INDEX_op_neg_i32:
    case INDEX_op_neg_i64:
    case INDEX_op_ext8s_i32:
    case INDEX_op_ext8s_i64:
    case INDEX_op_ext16s_i32:
    case INDEX_op_ext16s_i64:
    case INDEX_op_ext8u_i32:
    case INDEX_op_ext8u_i64:
    case INDEX_op_ext16u_i32:
    case INDEX_op_ext16u_i64:
    case INDEX_op_ext32s_i64:
    case INDEX_op_ext32u_i64:
    case INDEX_op_ext_i32_i64:
    case INDEX_op_extu_i32_i64:
    case INDEX_op_bswap16_i32:
    case INDEX_op_bswap16_i64:
    case INDEX_op_bswap32_i32:
    case INDEX_op_bswap32_i64:
    case INDEX_op_bswap64_i64:
        return C_O1_I1(r, r);

    case INDEX_op_st8_i32:
    case INDEX_op_st16_i32:
    case INDEX_op_st_i32:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i64:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
        return C_O0_I2(r, r);

    case INDEX_op_div_i32:
    case INDEX_op_div_i64:
    case INDEX_op_divu_i32:
    case INDEX_op_divu_i64:
    case INDEX_op_rem_i32:
    case INDEX_op_rem_i64:
    case INDEX_op_remu_i32:
    case INDEX_op_remu_i64:
    case INDEX_op_add_i32:
    case INDEX_op_add_i64:
    case INDEX_op_sub_i32:
    case INDEX_op_sub_i64:
    case INDEX_op_mul_i32:
    case INDEX_op_mul_i64:
    case INDEX_op_and_i32:
    case INDEX_op_and_i64:
    case INDEX_op_andc_i32:
    case INDEX_op_andc_i64:
    case INDEX_op_eqv_i32:
    case INDEX_op_eqv_i64:
    case INDEX_op_nand_i32:
    case INDEX_op_nand_i64:
    case INDEX_op_nor_i32:
    case INDEX_op_nor_i64:
    case INDEX_op_or_i32:
    case INDEX_op_or_i64:
    case INDEX_op_orc_i32:
    case INDEX_op_orc_i64:
    case INDEX_op_xor_i32:
    case INDEX_op_xor_i64:
    case INDEX_op_shl_i32:
    case INDEX_op_shl_i64:
    case INDEX_op_shr_i32:
    case INDEX_op_shr_i64:
    case INDEX_op_sar_i32:
    case INDEX_op_sar_i64:
    case INDEX_op_rotl_i32:
    case INDEX_op_rotl_i64:
    case INDEX_op_rotr_i32:
    case INDEX_op_rotr_i64:
    case INDEX_op_setcond_i32:
    case INDEX_op_setcond_i64:
    case INDEX_op_deposit_i32:
    case INDEX_op_deposit_i64:
        return C_O1_I2(r, r, r);

    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        return C_O0_I2(r, r);

#if TCG_TARGET_REG_BITS == 32
    /* TODO: Support R, R, R, R, RI, RI? Will it be faster? */
    case INDEX_op_add2_i32:
    case INDEX_op_sub2_i32:
        return C_O2_I4(r, r, r, r, r, r);
    case INDEX_op_brcond2_i32:
        return C_O0_I4(r, r, r, r);
    case INDEX_op_mulu2_i32:
        return C_O2_I2(r, r, r, r);
    case INDEX_op_setcond2_i32:
        return C_O1_I4(r, r, r, r, r);
#endif

    case INDEX_op_qemu_ld_i32:
        return (TARGET_LONG_BITS <= TCG_TARGET_REG_BITS
                ? C_O1_I1(r, r)
                : C_O1_I2(r, r, r));
    case INDEX_op_qemu_ld_i64:
        return (TCG_TARGET_REG_BITS == 64 ? C_O1_I1(r, r)
                : TARGET_LONG_BITS <= TCG_TARGET_REG_BITS ? C_O2_I1(r, r, r)
                : C_O2_I2(r, r, r, r));
    case INDEX_op_qemu_st_i32:
        return (TARGET_LONG_BITS <= TCG_TARGET_REG_BITS
                ? C_O0_I2(r, r)
                : C_O0_I3(r, r, r));
    case INDEX_op_qemu_st_i64:
        return (TCG_TARGET_REG_BITS == 64 ? C_O0_I2(r, r)
                : TARGET_LONG_BITS <= TCG_TARGET_REG_BITS ? C_O0_I3(r, r, r)
                : C_O0_I4(r, r, r, r));

    default:
        g_assert_not_reached();
    }
}

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_R0,
    TCG_REG_R1,
    TCG_REG_R2,
    TCG_REG_R3,
    TCG_REG_R4,
    TCG_REG_R5,
    TCG_REG_R6,
    TCG_REG_R7,
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
    TCG_REG_R11,
    TCG_REG_R12,
    TCG_REG_R13,
    TCG_REG_R14,
    TCG_REG_R15,
};

#if MAX_OPC_PARAM_IARGS != 6
# error Fix needed, number of supported input arguments changed!
#endif

static const int tcg_target_call_iarg_regs[] = {
    TCG_REG_R0,
    TCG_REG_R1,
    TCG_REG_R2,
    TCG_REG_R3,
    TCG_REG_R4,
    TCG_REG_R5,
#if TCG_TARGET_REG_BITS == 32
    /* 32 bit hosts need 2 * MAX_OPC_PARAM_IARGS registers. */
    TCG_REG_R6,
    TCG_REG_R7,
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
    TCG_REG_R11,
#endif
};

static const int tcg_target_call_oarg_regs[] = {
    TCG_REG_R0,
#if TCG_TARGET_REG_BITS == 32
    TCG_REG_R1
#endif
};

#ifdef CONFIG_DEBUG_TCG
static const char *const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "r00",
    "r01",
    "r02",
    "r03",
    "r04",
    "r05",
    "r06",
    "r07",
    "r08",
    "r09",
    "r10",
    "r11",
    "r12",
    "r13",
    "r14",
    "r15",
};
#endif

static bool patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    /* tcg_out_reloc always uses the same type, addend. */
    tcg_debug_assert(type == sizeof(tcg_target_long));
    tcg_debug_assert(addend == 0);
    tcg_debug_assert(value != 0);
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_patch32(code_ptr, value);
    } else {
        tcg_patch64(code_ptr, value);
    }
    return true;
}

/* Write value (native size). */
static void tcg_out_i(TCGContext *s, tcg_target_ulong v)
{
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_out32(s, v);
    } else {
        tcg_out64(s, v);
    }
}

/* Write opcode. */
static void tcg_out_op_t(TCGContext *s, TCGOpcode op)
{
    tcg_out8(s, op);
    tcg_out8(s, 0);
}

/* Write register. */
static void tcg_out_r(TCGContext *s, TCGArg t0)
{
    tcg_debug_assert(t0 < TCG_TARGET_NB_REGS);
    tcg_out8(s, t0);
}

/* Write label. */
static void tci_out_label(TCGContext *s, TCGLabel *label)
{
    if (label->has_value) {
        tcg_out_i(s, label->u.value);
        tcg_debug_assert(label->u.value);
    } else {
        tcg_out_reloc(s, s->code_ptr, sizeof(tcg_target_ulong), label, 0);
        s->code_ptr += sizeof(tcg_target_ulong);
    }
}

static void stack_bounds_check(TCGReg base, target_long offset)
{
    if (base == TCG_REG_CALL_STACK) {
        tcg_debug_assert(offset < 0);
        tcg_debug_assert(offset >= -(CPU_TEMP_BUF_NLONGS * sizeof(long)));
    }
}

static void tcg_out_op_l(TCGContext *s, TCGOpcode op, TCGLabel *l0)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);
    tci_out_label(s, l0);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_op_p(TCGContext *s, TCGOpcode op, void *p0)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);
    tcg_out_i(s, (uintptr_t)p0);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_op_v(TCGContext *s, TCGOpcode op)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_op_ri(TCGContext *s, TCGOpcode op, TCGReg r0, int32_t i1)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);
    tcg_out_r(s, r0);
    tcg_out32(s, i1);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

#if TCG_TARGET_REG_BITS == 64
static void tcg_out_op_rI(TCGContext *s, TCGOpcode op,
                          TCGReg r0, uint64_t i1)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);
    tcg_out_r(s, r0);
    tcg_out64(s, i1);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}
#endif

static void tcg_out_op_rr(TCGContext *s, TCGOpcode op, TCGReg r0, TCGReg r1)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);
    tcg_out_r(s, r0);
    tcg_out_r(s, r1);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_op_rrm(TCGContext *s, TCGOpcode op,
                           TCGReg r0, TCGReg r1, TCGArg m2)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);
    tcg_out_r(s, r0);
    tcg_out_r(s, r1);
    tcg_out32(s, m2);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_op_rrr(TCGContext *s, TCGOpcode op,
                           TCGReg r0, TCGReg r1, TCGReg r2)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);
    tcg_out_r(s, r0);
    tcg_out_r(s, r1);
    tcg_out_r(s, r2);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_op_rrs(TCGContext *s, TCGOpcode op,
                           TCGReg r0, TCGReg r1, intptr_t i2)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);
    tcg_out_r(s, r0);
    tcg_out_r(s, r1);
    tcg_debug_assert(i2 == (int32_t)i2);
    tcg_out32(s, i2);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_op_rrcl(TCGContext *s, TCGOpcode op,
                            TCGReg r0, TCGReg r1, TCGCond c2, TCGLabel *l3)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);
    tcg_out_r(s, r0);
    tcg_out_r(s, r1);
    tcg_out8(s, c2);
    tci_out_label(s, l3);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_op_rrrc(TCGContext *s, TCGOpcode op,
                            TCGReg r0, TCGReg r1, TCGReg r2, TCGCond c3)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);
    tcg_out_r(s, r0);
    tcg_out_r(s, r1);
    tcg_out_r(s, r2);
    tcg_out8(s, c3);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_op_rrrm(TCGContext *s, TCGOpcode op,
                            TCGReg r0, TCGReg r1, TCGReg r2, TCGArg m3)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);
    tcg_out_r(s, r0);
    tcg_out_r(s, r1);
    tcg_out_r(s, r2);
    tcg_out32(s, m3);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_op_rrrbb(TCGContext *s, TCGOpcode op, TCGReg r0,
                             TCGReg r1, TCGReg r2, uint8_t b3, uint8_t b4)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);
    tcg_out_r(s, r0);
    tcg_out_r(s, r1);
    tcg_out_r(s, r2);
    tcg_out8(s, b3);
    tcg_out8(s, b4);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_op_rrrrm(TCGContext *s, TCGOpcode op, TCGReg r0,
                             TCGReg r1, TCGReg r2, TCGReg r3, TCGArg m4)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);
    tcg_out_r(s, r0);
    tcg_out_r(s, r1);
    tcg_out_r(s, r2);
    tcg_out_r(s, r3);
    tcg_out32(s, m4);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

#if TCG_TARGET_REG_BITS == 32
static void tcg_out_op_rrrr(TCGContext *s, TCGOpcode op,
                            TCGReg r0, TCGReg r1, TCGReg r2, TCGReg r3)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);
    tcg_out_r(s, r0);
    tcg_out_r(s, r1);
    tcg_out_r(s, r2);
    tcg_out_r(s, r3);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_op_rrrrcl(TCGContext *s, TCGOpcode op,
                              TCGReg r0, TCGReg r1, TCGReg r2, TCGReg r3,
                              TCGCond c4, TCGLabel *l5)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);
    tcg_out_r(s, r0);
    tcg_out_r(s, r1);
    tcg_out_r(s, r2);
    tcg_out_r(s, r3);
    tcg_out8(s, c4);
    tci_out_label(s, l5);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_op_rrrrrc(TCGContext *s, TCGOpcode op,
                              TCGReg r0, TCGReg r1, TCGReg r2,
                              TCGReg r3, TCGReg r4, TCGCond c5)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);
    tcg_out_r(s, r0);
    tcg_out_r(s, r1);
    tcg_out_r(s, r2);
    tcg_out_r(s, r3);
    tcg_out_r(s, r4);
    tcg_out8(s, c5);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_op_rrrrrr(TCGContext *s, TCGOpcode op,
                              TCGReg r0, TCGReg r1, TCGReg r2,
                              TCGReg r3, TCGReg r4, TCGReg r5)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, op);
    tcg_out_r(s, r0);
    tcg_out_r(s, r1);
    tcg_out_r(s, r2);
    tcg_out_r(s, r3);
    tcg_out_r(s, r4);
    tcg_out_r(s, r5);

    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}
#endif

static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg val, TCGReg base,
                       intptr_t offset)
{
    stack_bounds_check(base, offset);
    switch (type) {
    case TCG_TYPE_I32:
        tcg_out_op_rrs(s, INDEX_op_ld_i32, val, base, offset);
        break;
#if TCG_TARGET_REG_BITS == 64
    case TCG_TYPE_I64:
        tcg_out_op_rrs(s, INDEX_op_ld_i64, val, base, offset);
        break;
#endif
    default:
        g_assert_not_reached();
    }
}

static bool tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_out_op_rr(s, INDEX_op_mov_i32, ret, arg);
        break;
#if TCG_TARGET_REG_BITS == 64
    case TCG_TYPE_I64:
        tcg_out_op_rr(s, INDEX_op_mov_i64, ret, arg);
        break;
#endif
    default:
        g_assert_not_reached();
    }
    return true;
}

static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long arg)
{
    switch (type) {
    case TCG_TYPE_I32:
        tcg_out_op_ri(s, INDEX_op_tci_movi_i32, ret, arg);
        break;
#if TCG_TARGET_REG_BITS == 64
    case TCG_TYPE_I64:
        tcg_out_op_rI(s, INDEX_op_tci_movi_i64, ret, arg);
        break;
#endif
    default:
        g_assert_not_reached();
    }
}

static inline void tcg_out_call(TCGContext *s, const tcg_insn_unit *arg)
{
    uint8_t *old_code_ptr = s->code_ptr;
    tcg_out_op_t(s, INDEX_op_call);
    tcg_out_i(s, (uintptr_t)arg);
    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

#if TCG_TARGET_REG_BITS == 64
# define CASE_32_64(x) \
        case glue(glue(INDEX_op_, x), _i64): \
        case glue(glue(INDEX_op_, x), _i32):
# define CASE_64(x) \
        case glue(glue(INDEX_op_, x), _i64):
#else
# define CASE_32_64(x) \
        case glue(glue(INDEX_op_, x), _i32):
# define CASE_64(x)
#endif

static void tcg_out_op(TCGContext *s, TCGOpcode opc,
                       const TCGArg args[TCG_MAX_OP_ARGS],
                       const int const_args[TCG_MAX_OP_ARGS])
{
    switch (opc) {
    case INDEX_op_exit_tb:
        tcg_out_op_p(s, opc, (void *)args[0]);
        break;

    case INDEX_op_goto_tb:
        tcg_debug_assert(s->tb_jmp_insn_offset == 0);
        /* indirect jump method. */
        tcg_out_op_p(s, opc, s->tb_jmp_target_addr + args[0]);
        set_jmp_reset_offset(s, args[0]);
        break;

    case INDEX_op_br:
        tcg_out_op_l(s, opc, arg_label(args[0]));
        break;

    CASE_32_64(setcond)
        tcg_out_op_rrrc(s, opc, args[0], args[1], args[2], args[3]);
        break;

#if TCG_TARGET_REG_BITS == 32
    case INDEX_op_setcond2_i32:
        tcg_out_op_rrrrrc(s, opc, args[0], args[1], args[2],
                          args[3], args[4], args[5]);
        break;
#endif

    CASE_32_64(ld8u)
    CASE_32_64(ld8s)
    CASE_32_64(ld16u)
    CASE_32_64(ld16s)
    case INDEX_op_ld_i32:
    CASE_64(ld32u)
    CASE_64(ld32s)
    CASE_64(ld)
    CASE_32_64(st8)
    CASE_32_64(st16)
    case INDEX_op_st_i32:
    CASE_64(st32)
    CASE_64(st)
        stack_bounds_check(args[1], args[2]);
        tcg_out_op_rrs(s, opc, args[0], args[1], args[2]);
        break;

    CASE_32_64(add)
    CASE_32_64(sub)
    CASE_32_64(mul)
    CASE_32_64(and)
    CASE_32_64(or)
    CASE_32_64(xor)
    CASE_32_64(andc)     /* Optional (TCG_TARGET_HAS_andc_*). */
    CASE_32_64(orc)      /* Optional (TCG_TARGET_HAS_orc_*). */
    CASE_32_64(eqv)      /* Optional (TCG_TARGET_HAS_eqv_*). */
    CASE_32_64(nand)     /* Optional (TCG_TARGET_HAS_nand_*). */
    CASE_32_64(nor)      /* Optional (TCG_TARGET_HAS_nor_*). */
    CASE_32_64(shl)
    CASE_32_64(shr)
    CASE_32_64(sar)
    CASE_32_64(rotl)     /* Optional (TCG_TARGET_HAS_rot_*). */
    CASE_32_64(rotr)     /* Optional (TCG_TARGET_HAS_rot_*). */
    CASE_32_64(div)      /* Optional (TCG_TARGET_HAS_div_*). */
    CASE_32_64(divu)     /* Optional (TCG_TARGET_HAS_div_*). */
    CASE_32_64(rem)      /* Optional (TCG_TARGET_HAS_div_*). */
    CASE_32_64(remu)     /* Optional (TCG_TARGET_HAS_div_*). */
        tcg_out_op_rrr(s, opc, args[0], args[1], args[2]);
        break;

    CASE_32_64(deposit)  /* Optional (TCG_TARGET_HAS_deposit_*). */
        {
            TCGArg pos = args[3], len = args[4];
            TCGArg max = opc == INDEX_op_deposit_i32 ? 32 : 64;

            tcg_debug_assert(pos < max);
            tcg_debug_assert(pos + len <= max);

            tcg_out_op_rrrbb(s, opc, args[0], args[1], args[2], pos, len);
        }
        break;

    CASE_32_64(brcond)
        tcg_out_op_rrcl(s, opc, args[0], args[1], args[2], arg_label(args[3]));
        break;

    CASE_32_64(neg)      /* Optional (TCG_TARGET_HAS_neg_*). */
    CASE_32_64(not)      /* Optional (TCG_TARGET_HAS_not_*). */
    CASE_32_64(ext8s)    /* Optional (TCG_TARGET_HAS_ext8s_*). */
    CASE_32_64(ext8u)    /* Optional (TCG_TARGET_HAS_ext8u_*). */
    CASE_32_64(ext16s)   /* Optional (TCG_TARGET_HAS_ext16s_*). */
    CASE_32_64(ext16u)   /* Optional (TCG_TARGET_HAS_ext16u_*). */
    CASE_64(ext32s)      /* Optional (TCG_TARGET_HAS_ext32s_i64). */
    CASE_64(ext32u)      /* Optional (TCG_TARGET_HAS_ext32u_i64). */
    CASE_64(ext_i32)
    CASE_64(extu_i32)
    CASE_32_64(bswap16)  /* Optional (TCG_TARGET_HAS_bswap16_*). */
    CASE_32_64(bswap32)  /* Optional (TCG_TARGET_HAS_bswap32_*). */
    CASE_64(bswap64)     /* Optional (TCG_TARGET_HAS_bswap64_i64). */
        tcg_out_op_rr(s, opc, args[0], args[1]);
        break;

#if TCG_TARGET_REG_BITS == 32
    case INDEX_op_add2_i32:
    case INDEX_op_sub2_i32:
        tcg_out_op_rrrrrr(s, opc, args[0], args[1], args[2],
                          args[3], args[4], args[5]);
        break;
    case INDEX_op_brcond2_i32:
        tcg_out_op_rrrrcl(s, opc, args[0], args[1], args[2],
                          args[3], args[4], arg_label(args[5]));
        break;
    case INDEX_op_mulu2_i32:
        tcg_out_op_rrrr(s, opc, args[0], args[1], args[2], args[3]);
        break;
#endif

    case INDEX_op_qemu_ld_i32:
    case INDEX_op_qemu_st_i32:
        if (TARGET_LONG_BITS <= TCG_TARGET_REG_BITS) {
            tcg_out_op_rrm(s, opc, args[0], args[1], args[2]);
        } else {
            tcg_out_op_rrrm(s, opc, args[0], args[1], args[2], args[3]);
        }
        break;

    case INDEX_op_qemu_ld_i64:
    case INDEX_op_qemu_st_i64:
        if (TCG_TARGET_REG_BITS == 64) {
            tcg_out_op_rrm(s, opc, args[0], args[1], args[2]);
        } else if (TARGET_LONG_BITS <= TCG_TARGET_REG_BITS) {
            tcg_out_op_rrrm(s, opc, args[0], args[1], args[2], args[3]);
        } else {
            tcg_out_op_rrrrm(s, opc, args[0], args[1],
                             args[2], args[3], args[4]);
        }
        break;

    case INDEX_op_mb:
        tcg_out_op_v(s, opc);
        break;

    case INDEX_op_mov_i32:  /* Always emitted via tcg_out_mov.  */
    case INDEX_op_mov_i64:
    case INDEX_op_call:     /* Always emitted via tcg_out_call.  */
    default:
        tcg_abort();
    }
}

static void tcg_out_st(TCGContext *s, TCGType type, TCGReg val, TCGReg base,
                       intptr_t offset)
{
    stack_bounds_check(base, offset);
    switch (type) {
    case TCG_TYPE_I32:
        tcg_out_op_rrs(s, INDEX_op_st_i32, val, base, offset);
        break;
#if TCG_TARGET_REG_BITS == 64
    case TCG_TYPE_I64:
        tcg_out_op_rrs(s, INDEX_op_st_i64, val, base, offset);
        break;
#endif
    default:
        g_assert_not_reached();
    }
}

static inline bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                               TCGReg base, intptr_t ofs)
{
    return false;
}

/* Test if a constant matches the constraint. */
static int tcg_target_const_match(tcg_target_long val, TCGType type,
                                  const TCGArgConstraint *arg_ct)
{
    /* No need to return 0 or 1, 0 or != 0 is good enough. */
    return arg_ct->ct & TCG_CT_CONST;
}

static void tcg_target_init(TCGContext *s)
{
#if defined(CONFIG_DEBUG_TCG_INTERPRETER)
    const char *envval = getenv("DEBUG_TCG");
    if (envval) {
        qemu_set_log(strtol(envval, NULL, 0));
    }
#endif

    /* The current code uses uint8_t for tcg operations. */
    tcg_debug_assert(tcg_op_defs_max <= UINT8_MAX);

    /* Registers available for 32 bit operations. */
    tcg_target_available_regs[TCG_TYPE_I32] = BIT(TCG_TARGET_NB_REGS) - 1;
    /* Registers available for 64 bit operations. */
    tcg_target_available_regs[TCG_TYPE_I64] = BIT(TCG_TARGET_NB_REGS) - 1;
    /* TODO: Which registers should be set here? */
    tcg_target_call_clobber_regs = BIT(TCG_TARGET_NB_REGS) - 1;

    s->reserved_regs = 0;
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_CALL_STACK);

    /* We use negative offsets from "sp" so that we can distinguish
       stores that might pretend to be call arguments.  */
    tcg_set_frame(s, TCG_REG_CALL_STACK,
                  -CPU_TEMP_BUF_NLONGS * sizeof(long),
                  CPU_TEMP_BUF_NLONGS * sizeof(long));
}

/* Generate global QEMU prologue and epilogue code. */
static inline void tcg_target_qemu_prologue(TCGContext *s)
{
}
