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

/* Single bit n. */
#define BIT(n) (1 << (n))

/* Bitfield n...m (in 32 bit value). */
#define BITS(n, m) (((0xffffffffU << (31 - n)) >> (31 - n + m)) << m)

/* Used for function call generation. */
#define TCG_REG_CALL_STACK              TCG_REG_R4
#define TCG_TARGET_STACK_ALIGN          16
#define TCG_TARGET_CALL_STACK_OFFSET    0

/* TODO: documentation. */
static uint8_t *tb_ret_addr;

/* Macros used in tcg_target_op_defs. */
#define R       "r"
#define RI      "ri"
#if TCG_TARGET_REG_BITS == 32
# define R64    "r", "r"
#else
# define R64    "r"
#endif
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
# define L      "L", "L"
# define S      "S", "S"
#else
# define L      "L"
# define S      "S"
#endif

/* TODO: documentation. */
static const TCGTargetOpDef tcg_target_op_defs[] = {
    { INDEX_op_exit_tb, { NULL } },
    { INDEX_op_goto_tb, { NULL } },
    { INDEX_op_call, { RI } },
    { INDEX_op_jmp, { RI } },
    { INDEX_op_br, { NULL } },

    { INDEX_op_mov_i32, { R, R } },
    { INDEX_op_movi_i32, { R } },

    { INDEX_op_ld8u_i32, { R, R } },
    { INDEX_op_ld8s_i32, { R, R } },
    { INDEX_op_ld16u_i32, { R, R } },
    { INDEX_op_ld16s_i32, { R, R } },
    { INDEX_op_ld_i32, { R, R } },
    { INDEX_op_st8_i32, { R, R } },
    { INDEX_op_st16_i32, { R, R } },
    { INDEX_op_st_i32, { R, R } },

    { INDEX_op_add_i32, { R, RI, RI } },
    { INDEX_op_sub_i32, { R, RI, RI } },
    { INDEX_op_mul_i32, { R, RI, RI } },
#if TCG_TARGET_HAS_div_i32
    { INDEX_op_div_i32, { R, R, R } },
    { INDEX_op_divu_i32, { R, R, R } },
    { INDEX_op_rem_i32, { R, R, R } },
    { INDEX_op_remu_i32, { R, R, R } },
#elif TCG_TARGET_HAS_div2_i32
    { INDEX_op_div2_i32, { R, R, "0", "1", R } },
    { INDEX_op_divu2_i32, { R, R, "0", "1", R } },
#endif
    /* TODO: Does R, RI, RI result in faster code than R, R, RI?
       If both operands are constants, we can optimize. */
    { INDEX_op_and_i32, { R, RI, RI } },
#if TCG_TARGET_HAS_andc_i32
    { INDEX_op_andc_i32, { R, RI, RI } },
#endif
#if TCG_TARGET_HAS_eqv_i32
    { INDEX_op_eqv_i32, { R, RI, RI } },
#endif
#if TCG_TARGET_HAS_nand_i32
    { INDEX_op_nand_i32, { R, RI, RI } },
#endif
#if TCG_TARGET_HAS_nor_i32
    { INDEX_op_nor_i32, { R, RI, RI } },
#endif
    { INDEX_op_or_i32, { R, RI, RI } },
#if TCG_TARGET_HAS_orc_i32
    { INDEX_op_orc_i32, { R, RI, RI } },
#endif
    { INDEX_op_xor_i32, { R, RI, RI } },
    { INDEX_op_shl_i32, { R, RI, RI } },
    { INDEX_op_shr_i32, { R, RI, RI } },
    { INDEX_op_sar_i32, { R, RI, RI } },
#if TCG_TARGET_HAS_rot_i32
    { INDEX_op_rotl_i32, { R, RI, RI } },
    { INDEX_op_rotr_i32, { R, RI, RI } },
#endif

    { INDEX_op_brcond_i32, { R, RI } },

    { INDEX_op_setcond_i32, { R, R, RI } },
#if TCG_TARGET_REG_BITS == 64
    { INDEX_op_setcond_i64, { R, R, RI } },
#endif /* TCG_TARGET_REG_BITS == 64 */

#if TCG_TARGET_REG_BITS == 32
    /* TODO: Support R, R, R, R, RI, RI? Will it be faster? */
    { INDEX_op_add2_i32, { R, R, R, R, R, R } },
    { INDEX_op_sub2_i32, { R, R, R, R, R, R } },
    { INDEX_op_brcond2_i32, { R, R, RI, RI } },
    { INDEX_op_mulu2_i32, { R, R, R, R } },
    { INDEX_op_setcond2_i32, { R, R, R, RI, RI } },
#endif

#if TCG_TARGET_HAS_not_i32
    { INDEX_op_not_i32, { R, R } },
#endif
#if TCG_TARGET_HAS_neg_i32
    { INDEX_op_neg_i32, { R, R } },
#endif

#if TCG_TARGET_REG_BITS == 64
    { INDEX_op_mov_i64, { R, R } },
    { INDEX_op_movi_i64, { R } },

    { INDEX_op_ld8u_i64, { R, R } },
    { INDEX_op_ld8s_i64, { R, R } },
    { INDEX_op_ld16u_i64, { R, R } },
    { INDEX_op_ld16s_i64, { R, R } },
    { INDEX_op_ld32u_i64, { R, R } },
    { INDEX_op_ld32s_i64, { R, R } },
    { INDEX_op_ld_i64, { R, R } },

    { INDEX_op_st8_i64, { R, R } },
    { INDEX_op_st16_i64, { R, R } },
    { INDEX_op_st32_i64, { R, R } },
    { INDEX_op_st_i64, { R, R } },

    { INDEX_op_add_i64, { R, RI, RI } },
    { INDEX_op_sub_i64, { R, RI, RI } },
    { INDEX_op_mul_i64, { R, RI, RI } },
#if TCG_TARGET_HAS_div_i64
    { INDEX_op_div_i64, { R, R, R } },
    { INDEX_op_divu_i64, { R, R, R } },
    { INDEX_op_rem_i64, { R, R, R } },
    { INDEX_op_remu_i64, { R, R, R } },
#elif TCG_TARGET_HAS_div2_i64
    { INDEX_op_div2_i64, { R, R, "0", "1", R } },
    { INDEX_op_divu2_i64, { R, R, "0", "1", R } },
#endif
    { INDEX_op_and_i64, { R, RI, RI } },
#if TCG_TARGET_HAS_andc_i64
    { INDEX_op_andc_i64, { R, RI, RI } },
#endif
#if TCG_TARGET_HAS_eqv_i64
    { INDEX_op_eqv_i64, { R, RI, RI } },
#endif
#if TCG_TARGET_HAS_nand_i64
    { INDEX_op_nand_i64, { R, RI, RI } },
#endif
#if TCG_TARGET_HAS_nor_i64
    { INDEX_op_nor_i64, { R, RI, RI } },
#endif
    { INDEX_op_or_i64, { R, RI, RI } },
#if TCG_TARGET_HAS_orc_i64
    { INDEX_op_orc_i64, { R, RI, RI } },
#endif
    { INDEX_op_xor_i64, { R, RI, RI } },
    { INDEX_op_shl_i64, { R, RI, RI } },
    { INDEX_op_shr_i64, { R, RI, RI } },
    { INDEX_op_sar_i64, { R, RI, RI } },
#if TCG_TARGET_HAS_rot_i64
    { INDEX_op_rotl_i64, { R, RI, RI } },
    { INDEX_op_rotr_i64, { R, RI, RI } },
#endif
    { INDEX_op_brcond_i64, { R, RI } },

#if TCG_TARGET_HAS_ext8s_i64
    { INDEX_op_ext8s_i64, { R, R } },
#endif
#if TCG_TARGET_HAS_ext16s_i64
    { INDEX_op_ext16s_i64, { R, R } },
#endif
#if TCG_TARGET_HAS_ext32s_i64
    { INDEX_op_ext32s_i64, { R, R } },
#endif
#if TCG_TARGET_HAS_ext8u_i64
    { INDEX_op_ext8u_i64, { R, R } },
#endif
#if TCG_TARGET_HAS_ext16u_i64
    { INDEX_op_ext16u_i64, { R, R } },
#endif
#if TCG_TARGET_HAS_ext32u_i64
    { INDEX_op_ext32u_i64, { R, R } },
#endif
#if TCG_TARGET_HAS_bswap16_i64
    { INDEX_op_bswap16_i64, { R, R } },
#endif
#if TCG_TARGET_HAS_bswap32_i64
    { INDEX_op_bswap32_i64, { R, R } },
#endif
#if TCG_TARGET_HAS_bswap64_i64
    { INDEX_op_bswap64_i64, { R, R } },
#endif
#if TCG_TARGET_HAS_not_i64
    { INDEX_op_not_i64, { R, R } },
#endif
#if TCG_TARGET_HAS_neg_i64
    { INDEX_op_neg_i64, { R, R } },
#endif
#endif /* TCG_TARGET_REG_BITS == 64 */

    { INDEX_op_qemu_ld8u, { R, L } },
    { INDEX_op_qemu_ld8s, { R, L } },
    { INDEX_op_qemu_ld16u, { R, L } },
    { INDEX_op_qemu_ld16s, { R, L } },
    { INDEX_op_qemu_ld32, { R, L } },
#if TCG_TARGET_REG_BITS == 64
    { INDEX_op_qemu_ld32u, { R, L } },
    { INDEX_op_qemu_ld32s, { R, L } },
#endif
    { INDEX_op_qemu_ld64, { R64, L } },

    { INDEX_op_qemu_st8, { R, S } },
    { INDEX_op_qemu_st16, { R, S } },
    { INDEX_op_qemu_st32, { R, S } },
    { INDEX_op_qemu_st64, { R64, S } },

#if TCG_TARGET_HAS_ext8s_i32
    { INDEX_op_ext8s_i32, { R, R } },
#endif
#if TCG_TARGET_HAS_ext16s_i32
    { INDEX_op_ext16s_i32, { R, R } },
#endif
#if TCG_TARGET_HAS_ext8u_i32
    { INDEX_op_ext8u_i32, { R, R } },
#endif
#if TCG_TARGET_HAS_ext16u_i32
    { INDEX_op_ext16u_i32, { R, R } },
#endif

#if TCG_TARGET_HAS_bswap16_i32
    { INDEX_op_bswap16_i32, { R, R } },
#endif
#if TCG_TARGET_HAS_bswap32_i32
    { INDEX_op_bswap32_i32, { R, R } },
#endif

    { -1 },
};

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_R0,
    TCG_REG_R1,
    TCG_REG_R2,
    TCG_REG_R3,
#if 0 /* used for TCG_REG_CALL_STACK */
    TCG_REG_R4,
#endif
    TCG_REG_R5,
    TCG_REG_R6,
    TCG_REG_R7,
#if TCG_TARGET_NB_REGS >= 16
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
    TCG_REG_R11,
    TCG_REG_R12,
    TCG_REG_R13,
    TCG_REG_R14,
    TCG_REG_R15,
#endif
};

#if MAX_OPC_PARAM_IARGS != 4
# error Fix needed, number of supported input arguments changed!
#endif

static const int tcg_target_call_iarg_regs[] = {
    TCG_REG_R0,
    TCG_REG_R1,
    TCG_REG_R2,
    TCG_REG_R3,
#if TCG_TARGET_REG_BITS == 32
    /* 32 bit hosts need 2 * MAX_OPC_PARAM_IARGS registers. */
#if 0 /* used for TCG_REG_CALL_STACK */
    TCG_REG_R4,
#endif
    TCG_REG_R5,
    TCG_REG_R6,
    TCG_REG_R7,
#if TCG_TARGET_NB_REGS >= 16
    TCG_REG_R8,
#else
# error Too few input registers available
#endif
#endif
};

static const int tcg_target_call_oarg_regs[] = {
    TCG_REG_R0,
#if TCG_TARGET_REG_BITS == 32
    TCG_REG_R1
#endif
};

#ifndef NDEBUG
static const char *const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "r00",
    "r01",
    "r02",
    "r03",
    "r04",
    "r05",
    "r06",
    "r07",
#if TCG_TARGET_NB_REGS >= 16
    "r08",
    "r09",
    "r10",
    "r11",
    "r12",
    "r13",
    "r14",
    "r15",
#if TCG_TARGET_NB_REGS >= 32
    "r16",
    "r17",
    "r18",
    "r19",
    "r20",
    "r21",
    "r22",
    "r23",
    "r24",
    "r25",
    "r26",
    "r27",
    "r28",
    "r29",
    "r30",
    "r31"
#endif
#endif
};
#endif

static void patch_reloc(uint8_t *code_ptr, int type,
                        tcg_target_long value, tcg_target_long addend)
{
    /* tcg_out_reloc always uses the same type, addend. */
    assert(type == sizeof(tcg_target_long));
    assert(addend == 0);
    assert(value != 0);
    *(tcg_target_long *)code_ptr = value;
}

/* Parse target specific constraints. */
static int target_parse_constraint(TCGArgConstraint *ct, const char **pct_str)
{
    const char *ct_str = *pct_str;
    switch (ct_str[0]) {
    case 'r':
    case 'L':                   /* qemu_ld constraint */
    case 'S':                   /* qemu_st constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, BIT(TCG_TARGET_NB_REGS) - 1);
        break;
    default:
        return -1;
    }
    ct_str++;
    *pct_str = ct_str;
    return 0;
}

#if defined(CONFIG_DEBUG_TCG_INTERPRETER)
/* Show current bytecode. Used by tcg interpreter. */
void tci_disas(uint8_t opc)
{
    const TCGOpDef *def = &tcg_op_defs[opc];
    fprintf(stderr, "TCG %s %u, %u, %u\n",
            def->name, def->nb_oargs, def->nb_iargs, def->nb_cargs);
}
#endif

/* Write value (native size). */
static void tcg_out_i(TCGContext *s, tcg_target_ulong v)
{
    *(tcg_target_ulong *)s->code_ptr = v;
    s->code_ptr += sizeof(tcg_target_ulong);
}

/* Write 64 bit value. */
static void tcg_out64(TCGContext *s, uint64_t v)
{
    *(uint64_t *)s->code_ptr = v;
    s->code_ptr += sizeof(v);
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
    assert(t0 < TCG_TARGET_NB_REGS);
    tcg_out8(s, t0);
}

/* Write register or constant (native size). */
static void tcg_out_ri(TCGContext *s, int const_arg, TCGArg arg)
{
    if (const_arg) {
        assert(const_arg == 1);
        tcg_out8(s, TCG_CONST);
        tcg_out_i(s, arg);
    } else {
        tcg_out_r(s, arg);
    }
}

/* Write register or constant (32 bit). */
static void tcg_out_ri32(TCGContext *s, int const_arg, TCGArg arg)
{
    if (const_arg) {
        assert(const_arg == 1);
        tcg_out8(s, TCG_CONST);
        tcg_out32(s, arg);
    } else {
        tcg_out_r(s, arg);
    }
}

#if TCG_TARGET_REG_BITS == 64
/* Write register or constant (64 bit). */
static void tcg_out_ri64(TCGContext *s, int const_arg, TCGArg arg)
{
    if (const_arg) {
        assert(const_arg == 1);
        tcg_out8(s, TCG_CONST);
        tcg_out64(s, arg);
    } else {
        tcg_out_r(s, arg);
    }
}
#endif

/* Write label. */
static void tci_out_label(TCGContext *s, TCGArg arg)
{
    TCGLabel *label = &s->labels[arg];
    if (label->has_value) {
        tcg_out_i(s, label->u.value);
        assert(label->u.value);
    } else {
        tcg_out_reloc(s, s->code_ptr, sizeof(tcg_target_ulong), arg, 0);
        tcg_out_i(s, 0);
    }
}

static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg1,
                       tcg_target_long arg2)
{
    uint8_t *old_code_ptr = s->code_ptr;
    if (type == TCG_TYPE_I32) {
        tcg_out_op_t(s, INDEX_op_ld_i32);
        tcg_out_r(s, ret);
        tcg_out_r(s, arg1);
        tcg_out32(s, arg2);
    } else {
        assert(type == TCG_TYPE_I64);
#if TCG_TARGET_REG_BITS == 64
        tcg_out_op_t(s, INDEX_op_ld_i64);
        tcg_out_r(s, ret);
        tcg_out_r(s, arg1);
        assert(arg2 == (uint32_t)arg2);
        tcg_out32(s, arg2);
#else
        TODO();
#endif
    }
    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    uint8_t *old_code_ptr = s->code_ptr;
    assert(ret != arg);
#if TCG_TARGET_REG_BITS == 32
    tcg_out_op_t(s, INDEX_op_mov_i32);
#else
    tcg_out_op_t(s, INDEX_op_mov_i64);
#endif
    tcg_out_r(s, ret);
    tcg_out_r(s, arg);
    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg t0, tcg_target_long arg)
{
    uint8_t *old_code_ptr = s->code_ptr;
    uint32_t arg32 = arg;
    if (type == TCG_TYPE_I32 || arg == arg32) {
        tcg_out_op_t(s, INDEX_op_movi_i32);
        tcg_out_r(s, t0);
        tcg_out32(s, arg32);
    } else {
        assert(type == TCG_TYPE_I64);
#if TCG_TARGET_REG_BITS == 64
        tcg_out_op_t(s, INDEX_op_movi_i64);
        tcg_out_r(s, t0);
        tcg_out64(s, arg);
#else
        TODO();
#endif
    }
    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_op(TCGContext *s, TCGOpcode opc, const TCGArg *args,
                       const int *const_args)
{
    uint8_t *old_code_ptr = s->code_ptr;

    tcg_out_op_t(s, opc);

    switch (opc) {
    case INDEX_op_exit_tb:
        tcg_out64(s, args[0]);
        break;
    case INDEX_op_goto_tb:
        if (s->tb_jmp_offset) {
            /* Direct jump method. */
            assert(args[0] < ARRAY_SIZE(s->tb_jmp_offset));
            s->tb_jmp_offset[args[0]] = s->code_ptr - s->code_buf;
            tcg_out32(s, 0);
        } else {
            /* Indirect jump method. */
            TODO();
        }
        assert(args[0] < ARRAY_SIZE(s->tb_next_offset));
        s->tb_next_offset[args[0]] = s->code_ptr - s->code_buf;
        break;
    case INDEX_op_br:
        tci_out_label(s, args[0]);
        break;
    case INDEX_op_call:
        tcg_out_ri(s, const_args[0], args[0]);
        break;
    case INDEX_op_jmp:
        TODO();
        break;
    case INDEX_op_setcond_i32:
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        tcg_out_ri32(s, const_args[2], args[2]);
        tcg_out8(s, args[3]);   /* condition */
        break;
#if TCG_TARGET_REG_BITS == 32
    case INDEX_op_setcond2_i32:
        /* setcond2_i32 cond, t0, t1_low, t1_high, t2_low, t2_high */
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        tcg_out_r(s, args[2]);
        tcg_out_ri32(s, const_args[3], args[3]);
        tcg_out_ri32(s, const_args[4], args[4]);
        tcg_out8(s, args[5]);   /* condition */
        break;
#elif TCG_TARGET_REG_BITS == 64
    case INDEX_op_setcond_i64:
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        tcg_out_ri64(s, const_args[2], args[2]);
        tcg_out8(s, args[3]);   /* condition */
        break;
#endif
    case INDEX_op_movi_i32:
        TODO(); /* Handled by tcg_out_movi? */
        break;
    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld_i32:
    case INDEX_op_st8_i32:
    case INDEX_op_st16_i32:
    case INDEX_op_st_i32:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_ld_i64:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i64:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        assert(args[2] == (uint32_t)args[2]);
        tcg_out32(s, args[2]);
        break;
    case INDEX_op_add_i32:
    case INDEX_op_sub_i32:
    case INDEX_op_mul_i32:
    case INDEX_op_and_i32:
    case INDEX_op_andc_i32:     /* Optional (TCG_TARGET_HAS_andc_i32). */
    case INDEX_op_eqv_i32:      /* Optional (TCG_TARGET_HAS_eqv_i32). */
    case INDEX_op_nand_i32:     /* Optional (TCG_TARGET_HAS_nand_i32). */
    case INDEX_op_nor_i32:      /* Optional (TCG_TARGET_HAS_nor_i32). */
    case INDEX_op_or_i32:
    case INDEX_op_orc_i32:      /* Optional (TCG_TARGET_HAS_orc_i32). */
    case INDEX_op_xor_i32:
    case INDEX_op_shl_i32:
    case INDEX_op_shr_i32:
    case INDEX_op_sar_i32:
    case INDEX_op_rotl_i32:     /* Optional (TCG_TARGET_HAS_rot_i32). */
    case INDEX_op_rotr_i32:     /* Optional (TCG_TARGET_HAS_rot_i32). */
        tcg_out_r(s, args[0]);
        tcg_out_ri32(s, const_args[1], args[1]);
        tcg_out_ri32(s, const_args[2], args[2]);
        break;

#if TCG_TARGET_REG_BITS == 64
    case INDEX_op_mov_i64:
    case INDEX_op_movi_i64:
        TODO();
        break;
    case INDEX_op_add_i64:
    case INDEX_op_sub_i64:
    case INDEX_op_mul_i64:
    case INDEX_op_and_i64:
    case INDEX_op_andc_i64:     /* Optional (TCG_TARGET_HAS_andc_i64). */
    case INDEX_op_eqv_i64:      /* Optional (TCG_TARGET_HAS_eqv_i64). */
    case INDEX_op_nand_i64:     /* Optional (TCG_TARGET_HAS_nand_i64). */
    case INDEX_op_nor_i64:      /* Optional (TCG_TARGET_HAS_nor_i64). */
    case INDEX_op_or_i64:
    case INDEX_op_orc_i64:      /* Optional (TCG_TARGET_HAS_orc_i64). */
    case INDEX_op_xor_i64:
    case INDEX_op_shl_i64:
    case INDEX_op_shr_i64:
    case INDEX_op_sar_i64:
        /* TODO: Implementation of rotl_i64, rotr_i64 missing in tci.c. */
    case INDEX_op_rotl_i64:     /* Optional (TCG_TARGET_HAS_rot_i64). */
    case INDEX_op_rotr_i64:     /* Optional (TCG_TARGET_HAS_rot_i64). */
        tcg_out_r(s, args[0]);
        tcg_out_ri64(s, const_args[1], args[1]);
        tcg_out_ri64(s, const_args[2], args[2]);
        break;
    case INDEX_op_div_i64:      /* Optional (TCG_TARGET_HAS_div_i64). */
    case INDEX_op_divu_i64:     /* Optional (TCG_TARGET_HAS_div_i64). */
    case INDEX_op_rem_i64:      /* Optional (TCG_TARGET_HAS_div_i64). */
    case INDEX_op_remu_i64:     /* Optional (TCG_TARGET_HAS_div_i64). */
        TODO();
        break;
    case INDEX_op_div2_i64:     /* Optional (TCG_TARGET_HAS_div2_i64). */
    case INDEX_op_divu2_i64:    /* Optional (TCG_TARGET_HAS_div2_i64). */
        TODO();
        break;
    case INDEX_op_brcond_i64:
        tcg_out_r(s, args[0]);
        tcg_out_ri64(s, const_args[1], args[1]);
        tcg_out8(s, args[2]);           /* condition */
        tci_out_label(s, args[3]);
        break;
    case INDEX_op_bswap16_i64:  /* Optional (TCG_TARGET_HAS_bswap16_i64). */
    case INDEX_op_bswap32_i64:  /* Optional (TCG_TARGET_HAS_bswap32_i64). */
    case INDEX_op_bswap64_i64:  /* Optional (TCG_TARGET_HAS_bswap64_i64). */
    case INDEX_op_not_i64:      /* Optional (TCG_TARGET_HAS_not_i64). */
    case INDEX_op_neg_i64:      /* Optional (TCG_TARGET_HAS_neg_i64). */
    case INDEX_op_ext8s_i64:    /* Optional (TCG_TARGET_HAS_ext8s_i64). */
    case INDEX_op_ext8u_i64:    /* Optional (TCG_TARGET_HAS_ext8u_i64). */
    case INDEX_op_ext16s_i64:   /* Optional (TCG_TARGET_HAS_ext16s_i64). */
    case INDEX_op_ext16u_i64:   /* Optional (TCG_TARGET_HAS_ext16u_i64). */
    case INDEX_op_ext32s_i64:   /* Optional (TCG_TARGET_HAS_ext32s_i64). */
    case INDEX_op_ext32u_i64:   /* Optional (TCG_TARGET_HAS_ext32u_i64). */
#endif /* TCG_TARGET_REG_BITS == 64 */
    case INDEX_op_neg_i32:      /* Optional (TCG_TARGET_HAS_neg_i32). */
    case INDEX_op_not_i32:      /* Optional (TCG_TARGET_HAS_not_i32). */
    case INDEX_op_ext8s_i32:    /* Optional (TCG_TARGET_HAS_ext8s_i32). */
    case INDEX_op_ext16s_i32:   /* Optional (TCG_TARGET_HAS_ext16s_i32). */
    case INDEX_op_ext8u_i32:    /* Optional (TCG_TARGET_HAS_ext8u_i32). */
    case INDEX_op_ext16u_i32:   /* Optional (TCG_TARGET_HAS_ext16u_i32). */
    case INDEX_op_bswap16_i32:  /* Optional (TCG_TARGET_HAS_bswap16_i32). */
    case INDEX_op_bswap32_i32:  /* Optional (TCG_TARGET_HAS_bswap32_i32). */
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
    case INDEX_op_div_i32:      /* Optional (TCG_TARGET_HAS_div_i32). */
    case INDEX_op_divu_i32:     /* Optional (TCG_TARGET_HAS_div_i32). */
    case INDEX_op_rem_i32:      /* Optional (TCG_TARGET_HAS_div_i32). */
    case INDEX_op_remu_i32:     /* Optional (TCG_TARGET_HAS_div_i32). */
        tcg_out_r(s, args[0]);
        tcg_out_ri32(s, const_args[1], args[1]);
        tcg_out_ri32(s, const_args[2], args[2]);
        break;
    case INDEX_op_div2_i32:     /* Optional (TCG_TARGET_HAS_div2_i32). */
    case INDEX_op_divu2_i32:    /* Optional (TCG_TARGET_HAS_div2_i32). */
        TODO();
        break;
#if TCG_TARGET_REG_BITS == 32
    case INDEX_op_add2_i32:
    case INDEX_op_sub2_i32:
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        tcg_out_r(s, args[2]);
        tcg_out_r(s, args[3]);
        tcg_out_r(s, args[4]);
        tcg_out_r(s, args[5]);
        break;
    case INDEX_op_brcond2_i32:
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        tcg_out_ri32(s, const_args[2], args[2]);
        tcg_out_ri32(s, const_args[3], args[3]);
        tcg_out8(s, args[4]);           /* condition */
        tci_out_label(s, args[5]);
        break;
    case INDEX_op_mulu2_i32:
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        tcg_out_r(s, args[2]);
        tcg_out_r(s, args[3]);
        break;
#endif
    case INDEX_op_brcond_i32:
        tcg_out_r(s, args[0]);
        tcg_out_ri32(s, const_args[1], args[1]);
        tcg_out8(s, args[2]);           /* condition */
        tci_out_label(s, args[3]);
        break;
    case INDEX_op_qemu_ld8u:
    case INDEX_op_qemu_ld8s:
    case INDEX_op_qemu_ld16u:
    case INDEX_op_qemu_ld16s:
    case INDEX_op_qemu_ld32:
#if TCG_TARGET_REG_BITS == 64
    case INDEX_op_qemu_ld32s:
    case INDEX_op_qemu_ld32u:
#endif
        tcg_out_r(s, *args++);
        tcg_out_r(s, *args++);
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
        tcg_out_r(s, *args++);
#endif
#ifdef CONFIG_SOFTMMU
        tcg_out_i(s, *args);
#endif
        break;
    case INDEX_op_qemu_ld64:
        tcg_out_r(s, *args++);
#if TCG_TARGET_REG_BITS == 32
        tcg_out_r(s, *args++);
#endif
        tcg_out_r(s, *args++);
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
        tcg_out_r(s, *args++);
#endif
#ifdef CONFIG_SOFTMMU
        tcg_out_i(s, *args);
#endif
        break;
    case INDEX_op_qemu_st8:
    case INDEX_op_qemu_st16:
    case INDEX_op_qemu_st32:
#ifdef CONFIG_TCG_PASS_AREG0
        tcg_out_r(s, TCG_AREG0);
#endif
        tcg_out_r(s, *args++);
        tcg_out_r(s, *args++);
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
        tcg_out_r(s, *args++);
#endif
#ifdef CONFIG_SOFTMMU
        tcg_out_i(s, *args);
#endif
        break;
    case INDEX_op_qemu_st64:
#ifdef CONFIG_TCG_PASS_AREG0
        tcg_out_r(s, TCG_AREG0);
#endif
        tcg_out_r(s, *args++);
#if TCG_TARGET_REG_BITS == 32
        tcg_out_r(s, *args++);
#endif
        tcg_out_r(s, *args++);
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
        tcg_out_r(s, *args++);
#endif
#ifdef CONFIG_SOFTMMU
        tcg_out_i(s, *args);
#endif
        break;
    case INDEX_op_end:
        TODO();
        break;
    default:
        fprintf(stderr, "Missing: %s\n", tcg_op_defs[opc].name);
        tcg_abort();
    }
    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

static void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg, TCGReg arg1,
                       tcg_target_long arg2)
{
    uint8_t *old_code_ptr = s->code_ptr;
    if (type == TCG_TYPE_I32) {
        tcg_out_op_t(s, INDEX_op_st_i32);
        tcg_out_r(s, arg);
        tcg_out_r(s, arg1);
        tcg_out32(s, arg2);
    } else {
        assert(type == TCG_TYPE_I64);
#if TCG_TARGET_REG_BITS == 64
        tcg_out_op_t(s, INDEX_op_st_i64);
        tcg_out_r(s, arg);
        tcg_out_r(s, arg1);
        tcg_out32(s, arg2);
#else
        TODO();
#endif
    }
    old_code_ptr[1] = s->code_ptr - old_code_ptr;
}

/* Test if a constant matches the constraint. */
static int tcg_target_const_match(tcg_target_long val,
                                  const TCGArgConstraint *arg_ct)
{
    /* No need to return 0 or 1, 0 or != 0 is good enough. */
    return arg_ct->ct & TCG_CT_CONST;
}

/* Maximum number of register used for input function arguments. */
static int tcg_target_get_call_iarg_regs_count(int flags)
{
    return ARRAY_SIZE(tcg_target_call_iarg_regs);
}

static void tcg_target_init(TCGContext *s)
{
#if defined(CONFIG_DEBUG_TCG_INTERPRETER)
    const char *envval = getenv("DEBUG_TCG");
    if (envval) {
        loglevel = strtol(envval, NULL, 0);
    }
#endif

    /* The current code uses uint8_t for tcg operations. */
    assert(ARRAY_SIZE(tcg_op_defs) <= UINT8_MAX);

    /* Registers available for 32 bit operations. */
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0,
                     BIT(TCG_TARGET_NB_REGS) - 1);
    /* Registers available for 64 bit operations. */
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I64], 0,
                     BIT(TCG_TARGET_NB_REGS) - 1);
    /* TODO: Which registers should be set here? */
    tcg_regset_set32(tcg_target_call_clobber_regs, 0,
                     BIT(TCG_TARGET_NB_REGS) - 1);
    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_CALL_STACK);
    tcg_add_target_add_op_defs(tcg_target_op_defs);
    tcg_set_frame(s, TCG_AREG0, offsetof(CPUArchState, temp_buf),
                  CPU_TEMP_BUF_NLONGS * sizeof(long));
}

/* Generate global QEMU prologue and epilogue code. */
static void tcg_target_qemu_prologue(TCGContext *s)
{
    tb_ret_addr = s->code_ptr;
}
