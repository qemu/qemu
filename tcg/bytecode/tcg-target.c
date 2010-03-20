/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2009, 2010 Stefan Weil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* TODO list:
 * - Add support for constant parameters.
 */

/* Marker for missing code. */
#define TODO() \
    fprintf(stderr, "TODO %s:%u: %s()\n", __FILE__, __LINE__, __FUNCTION__); \
    tcg_abort()

/* Trace message to see program flow. */
#if defined(CONFIG_DEBUG_TCG_INTERPRETER)
#define TRACE() \
    loglevel \
    ? fprintf(stderr, "TCG %s:%u: %s()\n", __FILE__, __LINE__, __FUNCTION__) \
    : (void)0
#else
#define TRACE() ((void)0)
#endif

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

/* TODO: documentation. */
static const TCGTargetOpDef tcg_target_op_defs[] = {
    { INDEX_op_exit_tb, { } },
    { INDEX_op_goto_tb, { } },
    { INDEX_op_call, { "ri" } },
    { INDEX_op_jmp, { "ri" } },
    { INDEX_op_br, { } },

    { INDEX_op_mov_i32, { "r", "r" } },
    { INDEX_op_movi_i32, { "r" } },

    { INDEX_op_ld8u_i32, { "r", "r" } },
    { INDEX_op_ld8s_i32, { "r", "r" } },
    { INDEX_op_ld16u_i32, { "r", "r" } },
    { INDEX_op_ld16s_i32, { "r", "r" } },
    { INDEX_op_ld_i32, { "r", "r" } },
    { INDEX_op_st8_i32, { "r", "ri" } },
    { INDEX_op_st16_i32, { "r", "ri" } },
    { INDEX_op_st_i32, { "r", "ri" } },

    { INDEX_op_add_i32, { "r", "ri", "ri" } },
    { INDEX_op_sub_i32, { "r", "ri", "ri" } },
    { INDEX_op_mul_i32, { "r", "ri", "ri" } },
#if defined(TCG_TARGET_HAS_div_i32)
    { INDEX_op_div_i32, { "r", "r", "r" } },
    { INDEX_op_divu_i32, { "r", "r", "r" } },
    { INDEX_op_rem_i32, { "r", "r", "r" } },
    { INDEX_op_remu_i32, { "r", "r", "r" } },
#else
    { INDEX_op_div2_i32, { "r", "r", "0", "1", "r" } },
    { INDEX_op_divu2_i32, { "r", "r", "0", "1", "r" } },
#endif
    { INDEX_op_and_i32, { "r", "ri", "ri" } },
    { INDEX_op_or_i32, { "r", "ri", "ri" } },
    { INDEX_op_xor_i32, { "r", "ri", "ri" } },

    { INDEX_op_shl_i32, { "r", "ri", "ri" } },
    { INDEX_op_shr_i32, { "r", "ri", "ri" } },
    { INDEX_op_sar_i32, { "r", "r", "ri" } },
#ifdef TCG_TARGET_HAS_rot_i32
    { INDEX_op_rotl_i32, { "r", "r", "ri" } },
    { INDEX_op_rotr_i32, { "r", "r", "ri" } },
#endif

    { INDEX_op_brcond_i32, { "r", "ri" } },

    { INDEX_op_setcond_i32, { "r", "r", "ri" } },
#if TCG_TARGET_REG_BITS == 64
    { INDEX_op_setcond_i64, { "r", "r", "ri" } },
#endif /* TCG_TARGET_REG_BITS == 64 */

#if TCG_TARGET_REG_BITS == 32
    /* TODO: "r", "r", "r", "r", "ri", "ri" */
    { INDEX_op_add2_i32, { "r", "r", "r", "r", "r", "r" } },
    { INDEX_op_sub2_i32, { "r", "r", "r", "r", "r", "r" } },
    { INDEX_op_brcond2_i32, { "r", "r", "ri", "ri" } },
    { INDEX_op_mulu2_i32, { "r", "r", "r", "r" } },
    { INDEX_op_setcond2_i32, { "r", "r", "r", "ri", "ri" } },
#endif

#if defined(TCG_TARGET_HAS_not_i32)
    { INDEX_op_not_i32, { "r", "r" } },
#endif
#if defined(TCG_TARGET_HAS_neg_i32)
    { INDEX_op_neg_i32, { "r", "r" } },
#endif

#if TCG_TARGET_REG_BITS == 64
    { INDEX_op_mov_i64, { "r", "r" } },
    { INDEX_op_movi_i64, { "r" } },

    { INDEX_op_ld8u_i64, { "r", "r" } },
    { INDEX_op_ld8s_i64, { "r", "r" } },
    { INDEX_op_ld16u_i64, { "r", "r" } },
    { INDEX_op_ld16s_i64, { "r", "r" } },
    { INDEX_op_ld32u_i64, { "r", "r" } },
    { INDEX_op_ld32s_i64, { "r", "r" } },
    { INDEX_op_ld_i64, { "r", "r" } },

    { INDEX_op_st8_i64, { "r", "ri" } },
    { INDEX_op_st16_i64, { "r", "ri" } },
    { INDEX_op_st32_i64, { "r", "ri" } },
    { INDEX_op_st_i64, { "r", "ri" } },

    { INDEX_op_add_i64, { "r", "ri", "ri" } },
    { INDEX_op_sub_i64, { "r", "ri", "ri" } },
    { INDEX_op_mul_i64, { "r", "ri", "ri" } },
#if defined(TCG_TARGET_HAS_div_i64)
    { INDEX_op_div_i64, { "r", "r", "r" } },
    { INDEX_op_divu_i64, { "r", "r", "r" } },
    { INDEX_op_rem_i64, { "r", "r", "r" } },
    { INDEX_op_remu_i64, { "r", "r", "r" } },
#elif defined(TCG_TARGET_HAS_div2_i64)
    { INDEX_op_div2_i64, { "r", "r", "0", "1", "r" } },
    { INDEX_op_divu2_i64, { "r", "r", "0", "1", "r" } },
#endif
    { INDEX_op_and_i64, { "r", "ri", "ri" } },
    { INDEX_op_or_i64, { "r", "ri", "ri" } },
    { INDEX_op_xor_i64, { "r", "ri", "ri" } },
    { INDEX_op_shl_i64, { "r", "ri", "ri" } },
    { INDEX_op_shr_i64, { "r", "ri", "ri" } },
    { INDEX_op_sar_i64, { "r", "ri", "ri" } },
#ifdef TCG_TARGET_HAS_rot_i64
    { INDEX_op_rotl_i64, { "r", "ri", "ri" } },
    { INDEX_op_rotr_i64, { "r", "ri", "ri" } },
#endif
    { INDEX_op_brcond_i64, { "r", "ri" } },

#if defined(TCG_TARGET_HAS_ext8s_i64)
    { INDEX_op_ext8s_i64, { "r", "r" } },
#endif
#if defined(TCG_TARGET_HAS_ext16s_i64)
    { INDEX_op_ext16s_i64, { "r", "r" } },
#endif
#if defined(TCG_TARGET_HAS_ext32s_i64)
    { INDEX_op_ext32s_i64, { "r", "r" } },
#endif
#if defined(TCG_TARGET_HAS_ext8u_i64)
    { INDEX_op_ext8u_i64, { "r", "r" } },
#endif
#if defined(TCG_TARGET_HAS_ext16u_i64)
    { INDEX_op_ext16u_i64, { "r", "r" } },
#endif
#if defined(TCG_TARGET_HAS_ext32u_i64)
    { INDEX_op_ext32u_i64, { "r", "r" } },
#endif
#ifdef TCG_TARGET_HAS_bswap16_i64
    { INDEX_op_bswap16_i64, { "r", "r" } },
#endif
#ifdef TCG_TARGET_HAS_bswap32_i64
    { INDEX_op_bswap32_i64, { "r", "r" } },
#endif
#ifdef TCG_TARGET_HAS_bswap64_i64
    { INDEX_op_bswap64_i64, { "r", "r" } },
#endif
#ifdef TCG_TARGET_HAS_not_i64
    { INDEX_op_not_i64, { "r", "r" } },
#endif
#ifdef TCG_TARGET_HAS_neg_i64
    { INDEX_op_neg_i64, { "r", "r" } },
#endif
#endif /* TCG_TARGET_REG_BITS == 64 */

#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
    { INDEX_op_qemu_ld8u, { "r", "L", "L" } },
    { INDEX_op_qemu_ld8s, { "r", "L", "L" } },
    { INDEX_op_qemu_ld16u, { "r", "L", "L" } },
    { INDEX_op_qemu_ld16s, { "r", "L", "L" } },
    { INDEX_op_qemu_ld32, { "r", "L", "L" } },
    { INDEX_op_qemu_ld64, { "r", "r", "L", "L" } },
#else /* TARGET_LONG_BITS > TCG_TARGET_REG_BITS */
    { INDEX_op_qemu_ld8u, { "r", "L" } },
    { INDEX_op_qemu_ld8s, { "r", "L" } },
    { INDEX_op_qemu_ld16u, { "r", "L" } },
    { INDEX_op_qemu_ld16s, { "r", "L" } },
#if TCG_TARGET_REG_BITS == 64
    { INDEX_op_qemu_ld32u, { "r", "L" } },
    { INDEX_op_qemu_ld32s, { "r", "L" } },
#endif
    { INDEX_op_qemu_ld32, { "r", "L" } },
#if TCG_TARGET_REG_BITS == 32
    { INDEX_op_qemu_ld64, { "r", "r", "L" } },
#else
    { INDEX_op_qemu_ld64, { "r", "L" } },
#endif
#endif /* TARGET_LONG_BITS > TCG_TARGET_REG_BITS */

#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
    { INDEX_op_qemu_st8, { "S", "S", "S" } },
    { INDEX_op_qemu_st16, { "S", "S", "S" } },
    { INDEX_op_qemu_st32, { "S", "S", "S" } },
    { INDEX_op_qemu_st64, { "S", "S", "S", "S" } },
#else
    { INDEX_op_qemu_st8, { "S", "S" } },
    { INDEX_op_qemu_st16, { "S", "S" } },
    { INDEX_op_qemu_st32, { "S", "S" } },
#if TCG_TARGET_REG_BITS == 32
    { INDEX_op_qemu_st64, { "S", "S", "S" } },
#else
    { INDEX_op_qemu_st64, { "S", "S" } },
#endif
#endif /* TARGET_LONG_BITS > TCG_TARGET_REG_BITS */

#if defined(TCG_TARGET_HAS_ext8s_i32)
    { INDEX_op_ext8s_i32, { "r", "r" } },
#endif
#if defined(TCG_TARGET_HAS_ext16s_i32)
    { INDEX_op_ext16s_i32, { "r", "r" } },
#endif
#if defined(TCG_TARGET_HAS_ext8u_i32)
    { INDEX_op_ext8u_i32, { "r", "r" } },
#endif
#if defined(TCG_TARGET_HAS_ext16u_i32)
    { INDEX_op_ext16u_i32, { "r", "r" } },
#endif

#ifdef TCG_TARGET_HAS_bswap16_i32
    { INDEX_op_bswap16_i32, { "r", "r" } },
#endif
#ifdef TCG_TARGET_HAS_bswap32_i32
    { INDEX_op_bswap32_i32, { "r", "r" } },
#endif

    { -1 },
};

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_R0,
    TCG_REG_R1,
    TCG_REG_R2,
    TCG_REG_R3,
    //~ TCG_REG_R4,     // used for TCG_REG_CALL_STACK
    TCG_REG_R5,
    TCG_REG_R6,
    TCG_REG_R7,
};

static const int tcg_target_call_iarg_regs[] = {
    TCG_REG_R0,
    TCG_REG_R1,
    TCG_REG_R2,
    TCG_REG_R3,
    //~ TCG_REG_R4,     // used for TCG_REG_CALL_STACK
    TCG_REG_R5,
    TCG_REG_R6,
    TCG_REG_R7,
};

static const int tcg_target_call_oarg_regs[2] = {
    // TODO: ppc64 only uses one register. Why do others use two?
    TCG_REG_R0,
    TCG_REG_R1,
};

#ifndef NDEBUG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "r00",
    "r01",
    "r02",
    "r03",
    "r04",
    "r05",
    "r06",
    "r07",
#if TCG_TARGET_NB_REGS == 16 || TCG_TARGET_NB_REGS == 32
    "r08",
    "r09",
    "r10",
    "r11",
    "r12",
    "r13",
    "r14",
    "r15",
#endif
#if TCG_TARGET_NB_REGS == 32
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
};
#endif

static void flush_icache_range(unsigned long start, unsigned long stop)
{
    TRACE();
}

static void patch_reloc(uint8_t *code_ptr, int type,
                        tcg_target_long value, tcg_target_long addend)
{
    //~ TRACE();
    assert(type == sizeof(tcg_target_long));
    *(tcg_target_long *)code_ptr = value;
}

/* Parse target specific constraints. */
static int target_parse_constraint(TCGArgConstraint *ct, const char **pct_str)
{
    const char *ct_str = *pct_str;
    switch (ct_str[0]) {
    case 'r':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, BIT(TCG_TARGET_NB_REGS) - 1);
        break;
    case 'L':                   /* qemu_ld constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, BIT(TCG_TARGET_NB_REGS) - 1);
#if 0 // TODO: do we need this? How should it look like?
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R3);
#ifdef CONFIG_SOFTMMU
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R4);
#endif
#endif
        break;
    case 'S':                   /* qemu_st constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, BIT(TCG_TARGET_NB_REGS) - 1);
#if 0 // TODO: do we need this? How should it look like?
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R3);
#ifdef CONFIG_SOFTMMU
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R4);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R5);
#endif
#endif
        break;
    default:
        return -1;
    }
    ct_str++;
    *pct_str = ct_str;
    return 0;
}

void tci_disas(uint8_t opc)
{
#if defined(CONFIG_DEBUG_TCG_INTERPRETER)
    if (loglevel) {
        const TCGOpDef *def = &tcg_op_defs[opc];
        fprintf(stderr, "TCG %s %u, %u, %u\n",
                def->name, def->nb_oargs, def->nb_iargs, def->nb_cargs);
    }
#endif
}

static void tcg_disas3(TCGContext *s, TCGOpcode c, const TCGArg *args)
{
#if defined(CONFIG_DEBUG_TCG_INTERPRETER)
    char buf[128];
    TCGArg arg;
    FILE *outfile = stderr;
    const TCGOpDef *def = &tcg_op_defs[c];
    int nb_oargs, nb_iargs, nb_cargs;
    int i, k;
    if (!loglevel) {
        return;
    }
    if (c == INDEX_op_debug_insn_start) {
        uint64_t pc;
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
        pc = ((uint64_t)args[1] << 32) | args[0];
#else
        pc = args[0];
#endif
        fprintf(outfile, " ---- 0x%" PRIx64, pc);
        nb_oargs = def->nb_oargs;
        nb_iargs = def->nb_iargs;
        nb_cargs = def->nb_cargs;
    } else if (c == INDEX_op_call) {
        TCGArg arg;

        /* variable number of arguments */
        arg = *args++;
        nb_oargs = arg >> 16;
        nb_iargs = arg & 0xffff;
        nb_cargs = def->nb_cargs;

        fprintf(outfile, " %s ", def->name);

#if 0 /* TODO: code does not work (crash), need better code for disassembly. */
        /* function name */
        fprintf(outfile, "%s",
                tcg_get_arg_str_idx(s, buf, sizeof(buf), args[nb_oargs + nb_iargs - 1]));
        /* flags */
        fprintf(outfile, ",$0x%" TCG_PRIlx,
                args[nb_oargs + nb_iargs]);
        /* nb out args */
        fprintf(outfile, ",$%d", nb_oargs);
        for(i = 0; i < nb_oargs; i++) {
            fprintf(outfile, ",");
            fprintf(outfile, "%s",
                    tcg_get_arg_str_idx(s, buf, sizeof(buf), args[i]));
        }
        for(i = 0; i < (nb_iargs - 1); i++) {
            fprintf(outfile, ",");
            if (args[nb_oargs + i] == TCG_CALL_DUMMY_ARG) {
                fprintf(outfile, "<dummy>");
            } else {
                fprintf(outfile, "%s",
                        tcg_get_arg_str_idx(s, buf, sizeof(buf), args[nb_oargs + i]));
            }
        }
#endif
    } else if (c == INDEX_op_movi_i32
#if TCG_TARGET_REG_BITS == 64
               || c == INDEX_op_movi_i64
#endif
               ) {
        tcg_target_ulong val;
        TCGHelperInfo *th;

        nb_oargs = def->nb_oargs;
        nb_iargs = def->nb_iargs;
        nb_cargs = def->nb_cargs;
        fprintf(outfile, " %s %s,$", def->name,
                tcg_get_arg_str_idx(s, buf, sizeof(buf), args[0]));
        val = args[1];
        th = tcg_find_helper(s, val);
        if (th) {
            fprintf(outfile, "%s", th->name);
        } else {
            if (c == INDEX_op_movi_i32) {
                fprintf(outfile, "0x%x", (uint32_t)val);
            } else {
                fprintf(outfile, "0x%" PRIx64 , (uint64_t)val);
            }
        }
    } else {
        fprintf(outfile, " %s ", def->name);
        if (c == INDEX_op_nopn) {
            /* variable number of arguments */
            nb_cargs = *args;
            nb_oargs = 0;
            nb_iargs = 0;
        } else {
            nb_oargs = def->nb_oargs;
            nb_iargs = def->nb_iargs;
            nb_cargs = def->nb_cargs;
        }

        k = 0;
        for(i = 0; i < nb_oargs; i++) {
            fprintf(outfile, "%s%s", (k != 0) ? "," : "",
                    tcg_get_arg_str_idx(s, buf, sizeof(buf), args[k]));
            k++;
        }
        for(i = 0; i < nb_iargs; i++) {
            fprintf(outfile, "%s%s", (k != 0) ? "," : "",
                    tcg_get_arg_str_idx(s, buf, sizeof(buf), args[k]));
            k++;
        }
        if (c == INDEX_op_brcond_i32
#if TCG_TARGET_REG_BITS == 32
            || c == INDEX_op_brcond2_i32
#elif TCG_TARGET_REG_BITS == 64
            || c == INDEX_op_brcond_i64
#endif
            ) {
            if (args[k] < ARRAY_SIZE(cond_name) && cond_name[args[k]]) {
                fprintf(outfile, ",%s", cond_name[args[k++]]);
            } else {
                fprintf(outfile, ",$0x%" TCG_PRIlx, args[k++]);
            }
            i = 1;
        } else {
            i = 0;
        }
        for(; i < nb_cargs; i++) {
            arg = args[k];
            fprintf(outfile, "%s$0x%" TCG_PRIlx,  (k != 0) ? "," : "", arg);
            k++;
        }
    }
    fprintf(stderr, " %u, %u, %u\n",
            def->nb_oargs, def->nb_iargs, def->nb_cargs);
#endif
}

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
    tcg_out8(s, const_arg);
    if (const_arg) {
        tcg_out_i(s, arg);
    } else {
        tcg_out_r(s, arg);
    }
}

/* Write register or constant (32 bit). */
static void tcg_out_ri32(TCGContext *s, int const_arg, TCGArg arg)
{
    tcg_out8(s, const_arg);
    if (const_arg) {
        //~ assert(arg == (uint32_t)arg);
        tcg_out32(s, arg);
    } else {
        tcg_out_r(s, arg);
    }
}

#if TCG_TARGET_REG_BITS == 64
/* Write register or constant (64 bit). */
static void tcg_out_ri64(TCGContext *s, int const_arg, TCGArg arg)
{
    tcg_out8(s, const_arg);
    if (const_arg) {
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
    } else {
        tcg_out_reloc(s, s->code_ptr, sizeof(tcg_target_ulong), arg, 0);
        tcg_out_i(s, 0);
    }
}

static void tcg_out_addi(TCGContext *s, int reg, tcg_target_long val)
{
    TCGArg args[2] = { reg, val };
    tcg_disas3(s, INDEX_op_add_i32, args);
    TODO();
}

static void tcg_out_ld(TCGContext *s, TCGType type, int ret, int arg1,
                       tcg_target_long arg2)
{
    TCGArg args[3] = { ret, arg1, arg2 };
    if (type == TCG_TYPE_I32) {
        tcg_disas3(s, INDEX_op_ld_i32, args);
        tcg_out_op_t(s, INDEX_op_ld_i32);
        tcg_out_r(s, ret);
        tcg_out_r(s, arg1);
        tcg_out32(s, arg2);
    } else {
        assert(type == TCG_TYPE_I64);
#if TCG_TARGET_REG_BITS == 64
        tcg_disas3(s, INDEX_op_ld_i64, args);
        tcg_out_op_t(s, INDEX_op_ld_i64);
        tcg_out_r(s, ret);
        tcg_out_r(s, arg1);
        assert(arg2 == (uint32_t)arg2);
        tcg_out32(s, arg2);
#else
        TODO();
#endif
    }
}

static void tcg_out_mov(TCGContext *s, int ret, int arg)
{
    assert(ret != arg);
    TCGArg args[2] = { ret, arg };
#if TCG_TARGET_REG_BITS == 32
    tcg_disas3(s, INDEX_op_mov_i32, args);
    tcg_out_op_t(s, INDEX_op_mov_i32);
#else
    tcg_disas3(s, INDEX_op_mov_i64, args);
    tcg_out_op_t(s, INDEX_op_mov_i64);
#endif
    tcg_out_r(s, ret);
    tcg_out_r(s, arg);
}

static void tcg_out_movi(TCGContext *s, TCGType type,
                         int t0, tcg_target_long arg)
{
    TCGArg args[2] = { t0, arg };
    uint32_t arg32 = arg;
    if (type == TCG_TYPE_I32 || arg == arg32) {
        tcg_disas3(s, INDEX_op_movi_i32, args);
        tcg_out_op_t(s, INDEX_op_movi_i32);
        tcg_out_r(s, t0);
        tcg_out32(s, arg32);
    } else {
        assert(type == TCG_TYPE_I64);
#if TCG_TARGET_REG_BITS == 64
        tcg_disas3(s, INDEX_op_movi_i64, args);
        tcg_out_op_t(s, INDEX_op_movi_i64);
        tcg_out_r(s, t0);
        tcg_out64(s, arg);
#else
        TODO();
#endif
    }
}

static void tcg_out_op(TCGContext *s, TCGOpcode opc, const TCGArg *args,
                       const int *const_args)
{
    tcg_disas3(s, opc, args);
    switch (opc) {
    case INDEX_op_exit_tb:
        tcg_out_op_t(s, opc);
        tcg_out64(s, args[0]);
        break;
    case INDEX_op_goto_tb:
        tcg_out_op_t(s, opc);
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
        tcg_out_op_t(s, opc);
        tci_out_label(s, args[0]);
        break;
    case INDEX_op_call:
        tcg_out_op_t(s, opc);
        tcg_out_ri(s, const_args[0], args[0]);
        break;
    case INDEX_op_jmp:
        TODO();
        break;
    case INDEX_op_setcond_i32:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        tcg_out_ri32(s, const_args[2], args[2]);
        tcg_out8(s, args[3]);   /* condition */
        break;
#if TCG_TARGET_REG_BITS == 32
    case INDEX_op_setcond2_i32:
        tcg_out_op_t(s, opc);
        TODO();
        break;
#elif TCG_TARGET_REG_BITS == 64
    case INDEX_op_setcond_i64:
        TODO();
        break;
#endif
    case INDEX_op_movi_i32:
        TODO();
        break;
    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld_i32:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        assert(args[2] == (uint32_t)args[2]);
        tcg_out32(s, args[2]);
        break;
    case INDEX_op_st8_i32:
    case INDEX_op_st16_i32:
    case INDEX_op_st_i32:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        //~ assert(const_args[2]);
        assert(args[2] == (uint32_t)args[2]);
        tcg_out32(s, args[2]);
        break;
    case INDEX_op_add_i32:
    case INDEX_op_sub_i32:
    case INDEX_op_and_i32:
    case INDEX_op_or_i32:
    case INDEX_op_xor_i32:
    case INDEX_op_mul_i32:
    case INDEX_op_shl_i32:
    case INDEX_op_shr_i32:
    case INDEX_op_sar_i32:
#ifdef TCG_TARGET_HAS_rot_i32
    case INDEX_op_rotl_i32:
    case INDEX_op_rotr_i32:
#endif
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_ri32(s, const_args[1], args[1]);
        tcg_out_ri32(s, const_args[2], args[2]);
        break;

#if TCG_TARGET_REG_BITS == 64
    case INDEX_op_mov_i64:
    case INDEX_op_movi_i64:
        TODO();
        break;
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_ld_i64:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        assert(args[2] == (uint32_t)args[2]);
        tcg_out32(s, args[2]);
        break;
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i64:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        assert(args[2] == (uint32_t)args[2]);
        tcg_out32(s, args[2]);
        break;
    case INDEX_op_add_i64:
    case INDEX_op_sub_i64:
    case INDEX_op_mul_i64:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_ri64(s, const_args[1], args[1]);
        tcg_out_ri64(s, const_args[2], args[2]);
        break;
#ifdef TCG_TARGET_HAS_div_i64
    case INDEX_op_div_i64:
    case INDEX_op_divu_i64:
    case INDEX_op_rem_i64:
    case INDEX_op_remu_i64:
        TODO();
        break;
#elif defined(TCG_TARGET_HAS_div2_i64)
    case INDEX_op_div2_i64:
    case INDEX_op_divu2_i64:
        TODO();
        break;
#endif
    case INDEX_op_and_i64:
    case INDEX_op_or_i64:
    case INDEX_op_xor_i64:
    case INDEX_op_shl_i64:
    case INDEX_op_shr_i64:
    case INDEX_op_sar_i64:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_ri64(s, const_args[1], args[1]);
        tcg_out_ri64(s, const_args[2], args[2]);
        break;
    case INDEX_op_brcond_i64:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_ri64(s, const_args[1], args[1]);
        tcg_out8(s, args[2]);           /* condition */
        tci_out_label(s, args[3]);
        break;
#ifdef TCG_TARGET_HAS_bswap32_i64
    case INDEX_op_bswap32_i64:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
#endif
#ifdef TCG_TARGET_HAS_not_i64
    case INDEX_op_not_i64:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
#endif
#ifdef TCG_TARGET_HAS_neg_i64
    case INDEX_op_neg_i64:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
#endif
#endif /* TCG_TARGET_REG_BITS == 64 */

#if defined(TCG_TARGET_HAS_div_i32)
    case INDEX_op_div_i32:
    case INDEX_op_divu_i32:
    case INDEX_op_rem_i32:
    case INDEX_op_remu_i32:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_ri32(s, const_args[1], args[1]);
        tcg_out_ri32(s, const_args[2], args[2]);
        break;
#else
    case INDEX_op_div2_i32:
    case INDEX_op_divu2_i32:
        TODO();
        break;
#endif
#if TCG_TARGET_REG_BITS == 32
    case INDEX_op_add2_i32:
    case INDEX_op_sub2_i32:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        tcg_out_r(s, args[2]);
        tcg_out_r(s, args[3]);
        tcg_out_r(s, args[4]);
        tcg_out_r(s, args[5]);
        break;
    case INDEX_op_brcond2_i32:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        tcg_out_ri32(s, const_args[2], args[2]);
        tcg_out_ri32(s, const_args[3], args[3]);
        tcg_out8(s, args[4]);           /* condition */
        tci_out_label(s, args[5]);
        break;
    case INDEX_op_mulu2_i32:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        tcg_out_r(s, args[2]);
        tcg_out_r(s, args[3]);
        break;
#endif
    case INDEX_op_brcond_i32:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_ri32(s, const_args[1], args[1]);
        tcg_out8(s, args[2]);           /* condition */
        tci_out_label(s, args[3]);
        break;
#if defined(TCG_TARGET_HAS_neg_i32)
    case INDEX_op_neg_i32:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
#endif
#if defined(TCG_TARGET_HAS_not_i32)
    case INDEX_op_not_i32:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
#endif
    case INDEX_op_qemu_ld8u:
    case INDEX_op_qemu_ld8s:
    case INDEX_op_qemu_ld16u:
    case INDEX_op_qemu_ld16s:
    case INDEX_op_qemu_ld32:
#if TCG_TARGET_REG_BITS == 64
    case INDEX_op_qemu_ld32s:
    case INDEX_op_qemu_ld32u:
#endif
        tcg_out_op_t(s, opc);
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
        tcg_out_op_t(s, opc);
        tcg_out_r(s, *args++);
        tcg_out_r(s, *args++);
#if TCG_TARGET_REG_BITS == 32
        tcg_out_r(s, *args++);
#endif
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
        tcg_out_op_t(s, opc);
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
        tcg_out_op_t(s, opc);
        tcg_out_r(s, *args++);
        tcg_out_r(s, *args++);
#if TARGET_LONG_BITS > TCG_TARGET_REG_BITS
        tcg_out_r(s, *args++);
        tcg_out_r(s, *args++);
#elif TCG_TARGET_REG_BITS == 32
        tcg_out_r(s, *args++);
#endif
#ifdef CONFIG_SOFTMMU
        tcg_out_i(s, *args);
#endif
        break;
#if defined(TCG_TARGET_HAS_ext8s_i32)
    case INDEX_op_ext8s_i32:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
#endif
#if defined(TCG_TARGET_HAS_ext16s_i32)
    case INDEX_op_ext16s_i32:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
#endif
#if defined(TCG_TARGET_HAS_ext8u_i32)
    case INDEX_op_ext8u_i32:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
#endif
#if defined(TCG_TARGET_HAS_ext16u_i32)
    case INDEX_op_ext16u_i32:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
#endif
#if TCG_TARGET_REG_BITS == 64
#if defined(TCG_TARGET_HAS_ext8s_i64)
    case INDEX_op_ext8s_i64:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
#endif
#if defined(TCG_TARGET_HAS_ext8u_i64)
    case INDEX_op_ext8u_i64:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
#endif
#if defined(TCG_TARGET_HAS_ext16s_i64)
    case INDEX_op_ext16s_i64:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
#endif
#if defined(TCG_TARGET_HAS_ext16u_i64)
    case INDEX_op_ext16u_i64:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
#endif
#if defined(TCG_TARGET_HAS_ext32s_i64)
    case INDEX_op_ext32s_i64:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
#endif
#if defined(TCG_TARGET_HAS_ext32u_i64)
    case INDEX_op_ext32u_i64:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
#endif
#endif /* TCG_TARGET_REG_BITS == 64 */
#if defined(TCG_TARGET_HAS_bswap32_i32)
    case INDEX_op_bswap32_i32:
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
#endif
#if defined(TCG_TARGET_HAS_bswap16_i32)
    case INDEX_op_bswap16_i32:
        tcg_dump_ops(s, stderr);
        tcg_out_op_t(s, opc);
        tcg_out_r(s, args[0]);
        tcg_out_r(s, args[1]);
        break;
#endif
    case INDEX_op_end:
        TODO();
        break;
    default:
        //~ tcg_dump_ops(s, stderr);
        fprintf(stderr, "Missing: %s\n", tcg_op_defs[opc].name);
        tcg_abort();
    }
}

static void tcg_out_st(TCGContext *s, TCGType type, int arg, int arg1,
                       tcg_target_long arg2)
{
    TCGArg args[3] = { arg, arg1, arg2 };
    if (type == TCG_TYPE_I32) {
        tcg_disas3(s, INDEX_op_st_i32, args);
        tcg_out_op_t(s, INDEX_op_st_i32);
        tcg_out_r(s, arg);
        tcg_out_r(s, arg1);
        tcg_out32(s, arg2);
    } else {
        assert(type == TCG_TYPE_I64);
#if TCG_TARGET_REG_BITS == 64
        tcg_disas3(s, INDEX_op_st_i64, args);
        tcg_out_op_t(s, INDEX_op_st_i64);
        tcg_out_r(s, arg);
        tcg_out_r(s, arg1);
        tcg_out32(s, arg2);
#else
        TODO();
#endif
    }
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

void tcg_target_init(TCGContext *s)
{
#if defined(CONFIG_DEBUG_TCG_INTERPRETER)
    const char *env = getenv("DEBUG_TCG");
    if (env) {
        loglevel = strtol(env, NULL, 0);
    }
#endif
    TRACE();

    /* The current code uses uint8_t for tcg operations. */
    assert(ARRAY_SIZE(tcg_op_defs) <= UINT8_MAX);

    /* Registers available for 32 bit operations. */
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, BIT(TCG_TARGET_NB_REGS) - 1);
    /* Registers available for 64 bit operations. */
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I64], 0, BIT(TCG_TARGET_NB_REGS) - 1);
    /* TODO: Which registers should be set here? */
    tcg_regset_set32(tcg_target_call_clobber_regs, 0,
                     BIT(TCG_REG_R0) |
                     BIT(TCG_REG_R1) |
                     BIT(TCG_REG_R2) |
                     BIT(TCG_REG_R3) |
                     BIT(TCG_REG_R4) |
                     BIT(TCG_REG_R5) |
                     BIT(TCG_REG_R6) |
                     BIT(TCG_REG_R7));
    /* TODO: Reserved registers. */
    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R4);
    //~ tcg_regset_set_reg(s->reserved_regs, TCG_REG_R1);
    tcg_add_target_add_op_defs(tcg_target_op_defs);
}

/* Generate global QEMU prologue and epilogue code. */
void tcg_target_qemu_prologue(TCGContext *s)
{
    TRACE();
    tb_ret_addr = s->code_ptr;
}
