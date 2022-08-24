/*
 * New-style TCG opcode generator for i386 instructions
 *
 *  Copyright (c) 2022 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

static void gen_NM_exception(DisasContext *s)
{
    gen_exception(s, EXCP07_PREX);
}

static void gen_illegal(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_illegal_opcode(s);
}

static void gen_load_ea(DisasContext *s, AddressParts *mem, bool is_vsib)
{
    TCGv ea = gen_lea_modrm_1(s, *mem, is_vsib);
    gen_lea_v_seg(s, s->aflag, ea, mem->def_seg, s->override);
}

static inline int mmx_offset(MemOp ot)
{
    switch (ot) {
    case MO_8:
        return offsetof(MMXReg, MMX_B(0));
    case MO_16:
        return offsetof(MMXReg, MMX_W(0));
    case MO_32:
        return offsetof(MMXReg, MMX_L(0));
    case MO_64:
        return offsetof(MMXReg, MMX_Q(0));
    default:
        g_assert_not_reached();
    }
}

static inline int xmm_offset(MemOp ot)
{
    switch (ot) {
    case MO_8:
        return offsetof(ZMMReg, ZMM_B(0));
    case MO_16:
        return offsetof(ZMMReg, ZMM_W(0));
    case MO_32:
        return offsetof(ZMMReg, ZMM_L(0));
    case MO_64:
        return offsetof(ZMMReg, ZMM_Q(0));
    case MO_128:
        return offsetof(ZMMReg, ZMM_X(0));
    case MO_256:
        return offsetof(ZMMReg, ZMM_Y(0));
    default:
        g_assert_not_reached();
    }
}

static void compute_mmx_offset(X86DecodedOp *op)
{
    if (!op->has_ea) {
        op->offset = offsetof(CPUX86State, fpregs[op->n].mmx) + mmx_offset(op->ot);
    } else {
        op->offset = offsetof(CPUX86State, mmx_t0) + mmx_offset(op->ot);
    }
}

static void compute_xmm_offset(X86DecodedOp *op)
{
    if (!op->has_ea) {
        op->offset = ZMM_OFFSET(op->n) + xmm_offset(op->ot);
    } else {
        op->offset = offsetof(CPUX86State, xmm_t0) + xmm_offset(op->ot);
    }
}

static void gen_load_sse(DisasContext *s, TCGv temp, MemOp ot, int dest_ofs, bool aligned)
{
    switch(ot) {
    case MO_8:
        gen_op_ld_v(s, MO_8, temp, s->A0);
        tcg_gen_st8_tl(temp, cpu_env, dest_ofs);
        break;
    case MO_16:
        gen_op_ld_v(s, MO_16, temp, s->A0);
        tcg_gen_st16_tl(temp, cpu_env, dest_ofs);
        break;
    case MO_32:
        gen_op_ld_v(s, MO_32, temp, s->A0);
        tcg_gen_st32_tl(temp, cpu_env, dest_ofs);
        break;
    case MO_64:
        gen_ldq_env_A0(s, dest_ofs);
        break;
    case MO_128:
        gen_ldo_env_A0(s, dest_ofs, aligned);
        break;
    case MO_256:
        gen_ldy_env_A0(s, dest_ofs, aligned);
        break;
    default:
        g_assert_not_reached();
    }
}

static bool sse_needs_alignment(DisasContext *s, X86DecodedInsn *decode, MemOp ot)
{
    switch (decode->e.vex_class) {
    case 2:
    case 4:
        if ((s->prefix & PREFIX_VEX) ||
            decode->e.vex_special == X86_VEX_SSEUnaligned) {
            /* MOST legacy SSE instructions require aligned memory operands, but not all.  */
            return false;
        }
        /* fall through */
    case 1:
        return ot >= MO_128;

    default:
        return false;
    }
}

static void gen_load(DisasContext *s, X86DecodedInsn *decode, int opn, TCGv v)
{
    X86DecodedOp *op = &decode->op[opn];

    switch (op->unit) {
    case X86_OP_SKIP:
        return;
    case X86_OP_SEG:
        tcg_gen_ld32u_tl(v, cpu_env,
                         offsetof(CPUX86State,segs[op->n].selector));
        break;
    case X86_OP_CR:
        tcg_gen_ld_tl(v, cpu_env, offsetof(CPUX86State, cr[op->n]));
        break;
    case X86_OP_DR:
        tcg_gen_ld_tl(v, cpu_env, offsetof(CPUX86State, dr[op->n]));
        break;
    case X86_OP_INT:
        if (op->has_ea) {
            gen_op_ld_v(s, op->ot, v, s->A0);
        } else {
            gen_op_mov_v_reg(s, op->ot, v, op->n);
        }
        break;
    case X86_OP_IMM:
        tcg_gen_movi_tl(v, decode->immediate);
        break;

    case X86_OP_MMX:
        compute_mmx_offset(op);
        goto load_vector;

    case X86_OP_SSE:
        compute_xmm_offset(op);
    load_vector:
        if (op->has_ea) {
            bool aligned = sse_needs_alignment(s, decode, op->ot);
            gen_load_sse(s, v, op->ot, op->offset, aligned);
        }
        break;

    default:
        g_assert_not_reached();
    }
}

static void gen_writeback(DisasContext *s, X86DecodedInsn *decode, int opn, TCGv v)
{
    X86DecodedOp *op = &decode->op[opn];
    switch (op->unit) {
    case X86_OP_SKIP:
        break;
    case X86_OP_SEG:
        /* Note that gen_movl_seg_T0 takes care of interrupt shadow and TF.  */
        gen_movl_seg_T0(s, op->n);
        break;
    case X86_OP_INT:
        if (op->has_ea) {
            gen_op_st_v(s, op->ot, v, s->A0);
        } else {
            gen_op_mov_reg_v(s, op->ot, op->n, v);
        }
        break;
    case X86_OP_MMX:
        break;
    case X86_OP_SSE:
        if ((s->prefix & PREFIX_VEX) && op->ot == MO_128) {
            tcg_gen_gvec_dup_imm(MO_64,
                                 offsetof(CPUX86State, xmm_regs[op->n].ZMM_X(1)),
                                 16, 16, 0);
        }
        break;
    case X86_OP_CR:
    case X86_OP_DR:
    default:
        g_assert_not_reached();
    }
}

static void gen_ADCOX(DisasContext *s, CPUX86State *env, MemOp ot, int cc_op)
{
    TCGv carry_in = NULL;
    TCGv carry_out = (cc_op == CC_OP_ADCX ? cpu_cc_dst : cpu_cc_src2);
    TCGv zero;

    if (cc_op == s->cc_op || s->cc_op == CC_OP_ADCOX) {
        /* Re-use the carry-out from a previous round.  */
        carry_in = carry_out;
        cc_op = s->cc_op;
    } else if (s->cc_op == CC_OP_ADCX || s->cc_op == CC_OP_ADOX) {
        /* Merge with the carry-out from the opposite instruction.  */
        cc_op = CC_OP_ADCOX;
    }

    /* If we don't have a carry-in, get it out of EFLAGS.  */
    if (!carry_in) {
        if (s->cc_op != CC_OP_ADCX && s->cc_op != CC_OP_ADOX) {
            gen_compute_eflags(s);
        }
        carry_in = s->tmp0;
        tcg_gen_extract_tl(carry_in, cpu_cc_src,
            ctz32(cc_op == CC_OP_ADCX ? CC_C : CC_O), 1);
    }

    switch (ot) {
#ifdef TARGET_X86_64
    case MO_32:
        /* If TL is 64-bit just do everything in 64-bit arithmetic.  */
        tcg_gen_add_i64(s->T0, s->T0, s->T1);
        tcg_gen_add_i64(s->T0, s->T0, carry_in);
        tcg_gen_shri_i64(carry_out, s->T0, 32);
        break;
#endif
    default:
        zero = tcg_constant_tl(0);
        tcg_gen_add2_tl(s->T0, carry_out, s->T0, zero, carry_in, zero);
        tcg_gen_add2_tl(s->T0, carry_out, s->T0, carry_out, s->T1, zero);
        break;
    }
    set_cc_op(s, cc_op);
}

static void gen_ADCX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_ADCOX(s, env, decode->op[0].ot, CC_OP_ADCX);
}

static void gen_ADOX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_ADCOX(s, env, decode->op[0].ot, CC_OP_ADOX);
}

static void gen_ANDN(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;

    tcg_gen_andc_tl(s->T0, s->T1, s->T0);
    gen_op_update1_cc(s);
    set_cc_op(s, CC_OP_LOGICB + ot);
}

static void gen_BEXTR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    TCGv bound, zero;

    /*
     * Extract START, and shift the operand.
     * Shifts larger than operand size get zeros.
     */
    tcg_gen_ext8u_tl(s->A0, s->T1);
    tcg_gen_shr_tl(s->T0, s->T0, s->A0);

    bound = tcg_constant_tl(ot == MO_64 ? 63 : 31);
    zero = tcg_constant_tl(0);
    tcg_gen_movcond_tl(TCG_COND_LEU, s->T0, s->A0, bound, s->T0, zero);

    /*
     * Extract the LEN into a mask.  Lengths larger than
     * operand size get all ones.
     */
    tcg_gen_extract_tl(s->A0, s->T1, 8, 8);
    tcg_gen_movcond_tl(TCG_COND_LEU, s->A0, s->A0, bound, s->A0, bound);

    tcg_gen_movi_tl(s->T1, 1);
    tcg_gen_shl_tl(s->T1, s->T1, s->A0);
    tcg_gen_subi_tl(s->T1, s->T1, 1);
    tcg_gen_and_tl(s->T0, s->T0, s->T1);

    gen_op_update1_cc(s);
    set_cc_op(s, CC_OP_LOGICB + ot);
}

static void gen_BLSI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;

    tcg_gen_neg_tl(s->T1, s->T0);
    tcg_gen_and_tl(s->T0, s->T0, s->T1);
    tcg_gen_mov_tl(cpu_cc_dst, s->T0);
    set_cc_op(s, CC_OP_BMILGB + ot);
}

static void gen_BLSMSK(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;

    tcg_gen_subi_tl(s->T1, s->T0, 1);
    tcg_gen_xor_tl(s->T0, s->T0, s->T1);
    tcg_gen_mov_tl(cpu_cc_dst, s->T0);
    set_cc_op(s, CC_OP_BMILGB + ot);
}

static void gen_BLSR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;

    tcg_gen_subi_tl(s->T1, s->T0, 1);
    tcg_gen_and_tl(s->T0, s->T0, s->T1);
    tcg_gen_mov_tl(cpu_cc_dst, s->T0);
    set_cc_op(s, CC_OP_BMILGB + ot);
}

static void gen_BZHI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    TCGv bound;

    tcg_gen_ext8u_tl(s->T1, cpu_regs[s->vex_v]);
    bound = tcg_constant_tl(ot == MO_64 ? 63 : 31);

    /*
     * Note that since we're using BMILG (in order to get O
     * cleared) we need to store the inverse into C.
     */
    tcg_gen_setcond_tl(TCG_COND_LT, cpu_cc_src, s->T1, bound);
    tcg_gen_movcond_tl(TCG_COND_GT, s->T1, s->T1, bound, bound, s->T1);

    tcg_gen_movi_tl(s->A0, -1);
    tcg_gen_shl_tl(s->A0, s->A0, s->T1);
    tcg_gen_andc_tl(s->T0, s->T0, s->A0);

    gen_op_update1_cc(s);
    set_cc_op(s, CC_OP_BMILGB + ot);
}

static void gen_CRC32(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[2].ot;

    tcg_gen_trunc_tl_i32(s->tmp2_i32, s->T0);
    gen_helper_crc32(s->T0, s->tmp2_i32, s->T1, tcg_constant_i32(8 << ot));
}

static void gen_MOVBE(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;

    /* M operand type does not load/store */
    if (decode->e.op0 == X86_TYPE_M) {
        tcg_gen_qemu_st_tl(s->T0, s->A0, s->mem_index, ot | MO_BE);
    } else {
        tcg_gen_qemu_ld_tl(s->T0, s->A0, s->mem_index, ot | MO_BE);
    }
}

static void gen_MULX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;

    /* low part of result in VEX.vvvv, high in MODRM */
    switch (ot) {
    default:
        tcg_gen_trunc_tl_i32(s->tmp2_i32, s->T0);
        tcg_gen_trunc_tl_i32(s->tmp3_i32, s->T1);
        tcg_gen_mulu2_i32(s->tmp2_i32, s->tmp3_i32,
                          s->tmp2_i32, s->tmp3_i32);
        tcg_gen_extu_i32_tl(cpu_regs[s->vex_v], s->tmp2_i32);
        tcg_gen_extu_i32_tl(s->T0, s->tmp3_i32);
        break;
#ifdef TARGET_X86_64
    case MO_64:
        tcg_gen_mulu2_i64(cpu_regs[s->vex_v], s->T0, s->T0, s->T1);
        break;
#endif
    }

}

static void gen_PDEP(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;
    if (ot < MO_64) {
        tcg_gen_ext32u_tl(s->T0, s->T0);
    }
    gen_helper_pdep(s->T0, s->T0, s->T1);
}

static void gen_PEXT(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;
    if (ot < MO_64) {
        tcg_gen_ext32u_tl(s->T0, s->T0);
    }
    gen_helper_pext(s->T0, s->T0, s->T1);
}

static void gen_RORX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    int b = decode->immediate;

    if (ot == MO_64) {
        tcg_gen_rotri_tl(s->T0, s->T0, b & 63);
    } else {
        tcg_gen_trunc_tl_i32(s->tmp2_i32, s->T0);
        tcg_gen_rotri_i32(s->tmp2_i32, s->tmp2_i32, b & 31);
        tcg_gen_extu_i32_tl(s->T0, s->tmp2_i32);
    }
}

static void gen_SARX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    int mask;

    mask = ot == MO_64 ? 63 : 31;
    tcg_gen_andi_tl(s->T1, s->T1, mask);
    if (ot != MO_64) {
        tcg_gen_ext32s_tl(s->T0, s->T0);
    }
    tcg_gen_sar_tl(s->T0, s->T0, s->T1);
}

static void gen_SHLX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    int mask;

    mask = ot == MO_64 ? 63 : 31;
    tcg_gen_andi_tl(s->T1, s->T1, mask);
    tcg_gen_shl_tl(s->T0, s->T0, s->T1);
}

static void gen_SHRX(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    int mask;

    mask = ot == MO_64 ? 63 : 31;
    tcg_gen_andi_tl(s->T1, s->T1, mask);
    if (ot != MO_64) {
        tcg_gen_ext32u_tl(s->T0, s->T0);
    }
    tcg_gen_shr_tl(s->T0, s->T0, s->T1);
}
