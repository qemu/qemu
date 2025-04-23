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

/*
 * Sometimes, knowing what the backend has can produce better code.
 * The exact opcode to check depends on 32- vs. 64-bit.
 */
#ifdef TARGET_X86_64
#define INDEX_op_extract2_tl            INDEX_op_extract2_i64
#else
#define INDEX_op_extract2_tl            INDEX_op_extract2_i32
#endif

#define MMX_OFFSET(reg)                        \
  ({ assert((reg) >= 0 && (reg) <= 7);         \
     offsetof(CPUX86State, fpregs[reg].mmx); })

#define ZMM_OFFSET(reg)                        \
  ({ assert((reg) >= 0 && (reg) <= 15);        \
     offsetof(CPUX86State, xmm_regs[reg]); })

typedef void (*SSEFunc_i_ep)(TCGv_i32 val, TCGv_ptr env, TCGv_ptr reg);
typedef void (*SSEFunc_l_ep)(TCGv_i64 val, TCGv_ptr env, TCGv_ptr reg);
typedef void (*SSEFunc_0_epp)(TCGv_ptr env, TCGv_ptr reg_a, TCGv_ptr reg_b);
typedef void (*SSEFunc_0_eppp)(TCGv_ptr env, TCGv_ptr reg_a, TCGv_ptr reg_b,
                               TCGv_ptr reg_c);
typedef void (*SSEFunc_0_epppp)(TCGv_ptr env, TCGv_ptr reg_a, TCGv_ptr reg_b,
                                TCGv_ptr reg_c, TCGv_ptr reg_d);
typedef void (*SSEFunc_0_eppi)(TCGv_ptr env, TCGv_ptr reg_a, TCGv_ptr reg_b,
                               TCGv_i32 val);
typedef void (*SSEFunc_0_epppi)(TCGv_ptr env, TCGv_ptr reg_a, TCGv_ptr reg_b,
                                TCGv_ptr reg_c, TCGv_i32 val);
typedef void (*SSEFunc_0_ppi)(TCGv_ptr reg_a, TCGv_ptr reg_b, TCGv_i32 val);
typedef void (*SSEFunc_0_pppi)(TCGv_ptr reg_a, TCGv_ptr reg_b, TCGv_ptr reg_c,
                               TCGv_i32 val);
typedef void (*SSEFunc_0_eppt)(TCGv_ptr env, TCGv_ptr reg_a, TCGv_ptr reg_b,
                               TCGv val);
typedef void (*SSEFunc_0_epppti)(TCGv_ptr env, TCGv_ptr reg_a, TCGv_ptr reg_b,
                                 TCGv_ptr reg_c, TCGv a0, TCGv_i32 scale);
typedef void (*SSEFunc_0_eppppi)(TCGv_ptr env, TCGv_ptr reg_a, TCGv_ptr reg_b,
                                  TCGv_ptr reg_c, TCGv_ptr reg_d, TCGv_i32 flags);
typedef void (*SSEFunc_0_eppppii)(TCGv_ptr env, TCGv_ptr reg_a, TCGv_ptr reg_b,
                                  TCGv_ptr reg_c, TCGv_ptr reg_d, TCGv_i32 even,
                                  TCGv_i32 odd);

static void gen_JMP_m(DisasContext *s, X86DecodedInsn *decode);
static void gen_JMP(DisasContext *s, X86DecodedInsn *decode);

static inline TCGv_i32 tcg_constant8u_i32(uint8_t val)
{
    return tcg_constant_i32(val);
}

static void gen_NM_exception(DisasContext *s)
{
    gen_exception(s, EXCP07_PREX);
}

static void gen_lea_modrm(DisasContext *s, X86DecodedInsn *decode)
{
    AddressParts *mem = &decode->mem;
    TCGv ea;

    ea = gen_lea_modrm_1(s, *mem, decode->e.vex_class == 12);
    if (decode->e.special == X86_SPECIAL_BitTest) {
        MemOp ot = decode->op[1].ot;
        int poslen = 8 << ot;
        int opn = decode->op[2].n;
        TCGv ofs = tcg_temp_new();

        /* Extract memory displacement from the second operand.  */
        assert(decode->op[2].unit == X86_OP_INT && decode->op[2].ot != MO_8);
        tcg_gen_sextract_tl(ofs, cpu_regs[opn], 3, poslen - 3);
        tcg_gen_andi_tl(ofs, ofs, -1 << ot);
        tcg_gen_add_tl(s->A0, ea, ofs);
        ea = s->A0;
    }

    gen_lea_v_seg(s, ea, mem->def_seg, s->override);
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

static int vector_reg_offset(X86DecodedOp *op)
{
    assert(op->unit == X86_OP_MMX || op->unit == X86_OP_SSE);

    if (op->unit == X86_OP_MMX) {
        return op->offset - mmx_offset(op->ot);
    } else {
        return op->offset - xmm_offset(op->ot);
    }
}

static int vector_elem_offset(X86DecodedOp *op, MemOp ot, int n)
{
    int base_ofs = vector_reg_offset(op);
    switch(ot) {
    case MO_8:
        if (op->unit == X86_OP_MMX) {
            return base_ofs + offsetof(MMXReg, MMX_B(n));
        } else {
            return base_ofs + offsetof(ZMMReg, ZMM_B(n));
        }
    case MO_16:
        if (op->unit == X86_OP_MMX) {
            return base_ofs + offsetof(MMXReg, MMX_W(n));
        } else {
            return base_ofs + offsetof(ZMMReg, ZMM_W(n));
        }
    case MO_32:
        if (op->unit == X86_OP_MMX) {
            return base_ofs + offsetof(MMXReg, MMX_L(n));
        } else {
            return base_ofs + offsetof(ZMMReg, ZMM_L(n));
        }
    case MO_64:
        if (op->unit == X86_OP_MMX) {
            return base_ofs;
        } else {
            return base_ofs + offsetof(ZMMReg, ZMM_Q(n));
        }
    case MO_128:
        assert(op->unit == X86_OP_SSE);
        return base_ofs + offsetof(ZMMReg, ZMM_X(n));
    case MO_256:
        assert(op->unit == X86_OP_SSE);
        return base_ofs + offsetof(ZMMReg, ZMM_Y(n));
    default:
        g_assert_not_reached();
    }
}

static void compute_mmx_offset(X86DecodedOp *op)
{
    if (!op->has_ea) {
        op->offset = MMX_OFFSET(op->n) + mmx_offset(op->ot);
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
        tcg_gen_st8_tl(temp, tcg_env, dest_ofs);
        break;
    case MO_16:
        gen_op_ld_v(s, MO_16, temp, s->A0);
        tcg_gen_st16_tl(temp, tcg_env, dest_ofs);
        break;
    case MO_32:
        gen_op_ld_v(s, MO_32, temp, s->A0);
        tcg_gen_st32_tl(temp, tcg_env, dest_ofs);
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
        tcg_gen_ld32u_tl(v, tcg_env,
                         offsetof(CPUX86State,segs[op->n].selector));
        break;
#ifndef CONFIG_USER_ONLY
    case X86_OP_CR:
        if (op->n == 8) {
            translator_io_start(&s->base);
            gen_helper_read_cr8(v, tcg_env);
        } else {
            tcg_gen_ld_tl(v, tcg_env, offsetof(CPUX86State, cr[op->n]));
        }
        break;
    case X86_OP_DR:
        /* CR4.DE tested in the helper.  */
        gen_helper_get_dr(v, tcg_env, tcg_constant_i32(op->n));
        break;
#endif
    case X86_OP_INT:
        if (op->has_ea) {
            if (v == s->T0 && decode->e.special == X86_SPECIAL_SExtT0) {
                gen_op_ld_v(s, op->ot | MO_SIGN, v, s->A0);
            } else {
                gen_op_ld_v(s, op->ot, v, s->A0);
            }

        } else if (op->ot < MO_TL && v == s->T0 &&
                   (decode->e.special == X86_SPECIAL_SExtT0 ||
                    decode->e.special == X86_SPECIAL_ZExtT0)) {
            if (op->ot == MO_8 && byte_reg_is_xH(s, op->n)) {
                if (decode->e.special == X86_SPECIAL_SExtT0) {
                    tcg_gen_sextract_tl(v, cpu_regs[op->n - 4], 8, 8);
                } else {
                    tcg_gen_extract_tl(v, cpu_regs[op->n - 4], 8, 8);
                }
            } else {
                if (decode->e.special == X86_SPECIAL_SExtT0) {
                    tcg_gen_ext_tl(v, cpu_regs[op->n], op->ot | MO_SIGN);
                } else {
                    tcg_gen_ext_tl(v, cpu_regs[op->n], op->ot);
                }
            }

        } else {
            gen_op_mov_v_reg(s, op->ot, v, op->n);
        }
        break;
    case X86_OP_IMM:
        tcg_gen_movi_tl(v, op->imm);
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

static TCGv_ptr op_ptr(X86DecodedInsn *decode, int opn)
{
    X86DecodedOp *op = &decode->op[opn];

    assert(op->unit == X86_OP_MMX || op->unit == X86_OP_SSE);
    if (op->v_ptr) {
        return op->v_ptr;
    }
    op->v_ptr = tcg_temp_new_ptr();

    /* The temporary points to the MMXReg or ZMMReg.  */
    tcg_gen_addi_ptr(op->v_ptr, tcg_env, vector_reg_offset(op));
    return op->v_ptr;
}

#define OP_PTR0 op_ptr(decode, 0)
#define OP_PTR1 op_ptr(decode, 1)
#define OP_PTR2 op_ptr(decode, 2)

static void gen_writeback(DisasContext *s, X86DecodedInsn *decode, int opn, TCGv v)
{
    X86DecodedOp *op = &decode->op[opn];
    switch (op->unit) {
    case X86_OP_SKIP:
        break;
    case X86_OP_SEG:
        /* Note that gen_movl_seg takes care of interrupt shadow and TF.  */
        gen_movl_seg(s, op->n, s->T0);
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
        if (!op->has_ea && (s->prefix & PREFIX_VEX) && op->ot <= MO_128) {
            tcg_gen_gvec_dup_imm(MO_64,
                                 offsetof(CPUX86State, xmm_regs[op->n].ZMM_X(1)),
                                 16, 16, 0);
        }
        break;
#ifndef CONFIG_USER_ONLY
    case X86_OP_CR:
        if (op->n == 8) {
            translator_io_start(&s->base);
        }
        gen_helper_write_crN(tcg_env, tcg_constant_i32(op->n), v);
        s->base.is_jmp = DISAS_EOB_NEXT;
        break;
    case X86_OP_DR:
        /* CR4.DE tested in the helper.  */
        gen_helper_set_dr(tcg_env, tcg_constant_i32(op->n), v);
        s->base.is_jmp = DISAS_EOB_NEXT;
        break;
#endif
    default:
        g_assert_not_reached();
    }
    op->unit = X86_OP_SKIP;
}

static inline int vector_len(DisasContext *s, X86DecodedInsn *decode)
{
    if (decode->e.special == X86_SPECIAL_MMX &&
        !(s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ))) {
        return 8;
    }
    return s->vex_l ? 32 : 16;
}

static void prepare_update1_cc(X86DecodedInsn *decode, DisasContext *s, CCOp op)
{
    decode->cc_dst = s->T0;
    decode->cc_op = op;
}

static void prepare_update2_cc(X86DecodedInsn *decode, DisasContext *s, CCOp op)
{
    decode->cc_src = s->T1;
    decode->cc_dst = s->T0;
    decode->cc_op = op;
}

static void prepare_update_cc_incdec(X86DecodedInsn *decode, DisasContext *s, CCOp op)
{
    gen_compute_eflags_c(s, s->T1);
    prepare_update2_cc(decode, s, op);
}

static void prepare_update3_cc(X86DecodedInsn *decode, DisasContext *s, CCOp op, TCGv reg)
{
    decode->cc_src2 = reg;
    decode->cc_src = s->T1;
    decode->cc_dst = s->T0;
    decode->cc_op = op;
}

/* Set up decode->cc_* to modify CF while keeping other flags unchanged.  */
static void prepare_update_cf(X86DecodedInsn *decode, DisasContext *s, TCGv cf)
{
    switch (s->cc_op) {
    case CC_OP_ADOX:
    case CC_OP_ADCOX:
        decode->cc_src2 = cpu_cc_src2;
        decode->cc_src = cpu_cc_src;
        decode->cc_op = CC_OP_ADCOX;
        break;

    case CC_OP_EFLAGS:
    case CC_OP_ADCX:
        decode->cc_src = cpu_cc_src;
        decode->cc_op = CC_OP_ADCX;
        break;

    default:
        decode->cc_src = tcg_temp_new();
        gen_mov_eflags(s, decode->cc_src);
        decode->cc_op = CC_OP_ADCX;
        break;
    }
    decode->cc_dst = cf;
}

static void gen_store_sse(DisasContext *s, X86DecodedInsn *decode, int src_ofs)
{
    MemOp ot = decode->op[0].ot;
    int vec_len = vector_len(s, decode);
    bool aligned = sse_needs_alignment(s, decode, ot);

    if (!decode->op[0].has_ea) {
        tcg_gen_gvec_mov(MO_64, decode->op[0].offset, src_ofs, vec_len, vec_len);
        return;
    }

    switch (ot) {
    case MO_64:
        gen_stq_env_A0(s, src_ofs);
        break;
    case MO_128:
        gen_sto_env_A0(s, src_ofs, aligned);
        break;
    case MO_256:
        gen_sty_env_A0(s, src_ofs, aligned);
        break;
    default:
        g_assert_not_reached();
    }
}

static void gen_helper_pavgusb(TCGv_ptr env, TCGv_ptr reg_a, TCGv_ptr reg_b)
{
    gen_helper_pavgb_mmx(env, reg_a, reg_a, reg_b);
}

#define FN_3DNOW_MOVE ((SSEFunc_0_epp) (uintptr_t) 1)
static const SSEFunc_0_epp fns_3dnow[] = {
    [0x0c] = gen_helper_pi2fw,
    [0x0d] = gen_helper_pi2fd,
    [0x1c] = gen_helper_pf2iw,
    [0x1d] = gen_helper_pf2id,
    [0x8a] = gen_helper_pfnacc,
    [0x8e] = gen_helper_pfpnacc,
    [0x90] = gen_helper_pfcmpge,
    [0x94] = gen_helper_pfmin,
    [0x96] = gen_helper_pfrcp,
    [0x97] = gen_helper_pfrsqrt,
    [0x9a] = gen_helper_pfsub,
    [0x9e] = gen_helper_pfadd,
    [0xa0] = gen_helper_pfcmpgt,
    [0xa4] = gen_helper_pfmax,
    [0xa6] = FN_3DNOW_MOVE, /* PFRCPIT1; no need to actually increase precision */
    [0xa7] = FN_3DNOW_MOVE, /* PFRSQIT1 */
    [0xb6] = FN_3DNOW_MOVE, /* PFRCPIT2 */
    [0xaa] = gen_helper_pfsubr,
    [0xae] = gen_helper_pfacc,
    [0xb0] = gen_helper_pfcmpeq,
    [0xb4] = gen_helper_pfmul,
    [0xb7] = gen_helper_pmulhrw_mmx,
    [0xbb] = gen_helper_pswapd,
    [0xbf] = gen_helper_pavgusb,
};

static void gen_3dnow(DisasContext *s, X86DecodedInsn *decode)
{
    uint8_t b = decode->immediate;
    SSEFunc_0_epp fn = b < ARRAY_SIZE(fns_3dnow) ? fns_3dnow[b] : NULL;

    if (!fn) {
        gen_illegal_opcode(s);
        return;
    }
    if (s->flags & HF_TS_MASK) {
        gen_NM_exception(s);
        return;
    }
    if (s->flags & HF_EM_MASK) {
        gen_illegal_opcode(s);
        return;
    }

    gen_helper_enter_mmx(tcg_env);
    if (fn == FN_3DNOW_MOVE) {
       tcg_gen_ld_i64(s->tmp1_i64, tcg_env, decode->op[1].offset);
       tcg_gen_st_i64(s->tmp1_i64, tcg_env, decode->op[0].offset);
    } else {
       fn(tcg_env, OP_PTR0, OP_PTR1);
    }
}

/*
 * 00 = v*ps Vps, Hps, Wpd
 * 66 = v*pd Vpd, Hpd, Wps
 * f3 = v*ss Vss, Hss, Wps
 * f2 = v*sd Vsd, Hsd, Wps
 */
static inline void gen_unary_fp_sse(DisasContext *s, X86DecodedInsn *decode,
                              SSEFunc_0_epp pd_xmm, SSEFunc_0_epp ps_xmm,
                              SSEFunc_0_epp pd_ymm, SSEFunc_0_epp ps_ymm,
                              SSEFunc_0_eppp sd, SSEFunc_0_eppp ss)
{
    if ((s->prefix & (PREFIX_REPZ | PREFIX_REPNZ)) != 0) {
        SSEFunc_0_eppp fn = s->prefix & PREFIX_REPZ ? ss : sd;
        if (!fn) {
            gen_illegal_opcode(s);
            return;
        }
        fn(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else {
        SSEFunc_0_epp ps, pd, fn;
        ps = s->vex_l ? ps_ymm : ps_xmm;
        pd = s->vex_l ? pd_ymm : pd_xmm;
        fn = s->prefix & PREFIX_DATA ? pd : ps;
        if (!fn) {
            gen_illegal_opcode(s);
            return;
        }
        fn(tcg_env, OP_PTR0, OP_PTR2);
    }
}
#define UNARY_FP_SSE(uname, lname)                                                 \
static void gen_##uname(DisasContext *s, X86DecodedInsn *decode)                   \
{                                                                                  \
    gen_unary_fp_sse(s, decode,                                                    \
                     gen_helper_##lname##pd_xmm,                                   \
                     gen_helper_##lname##ps_xmm,                                   \
                     gen_helper_##lname##pd_ymm,                                   \
                     gen_helper_##lname##ps_ymm,                                   \
                     gen_helper_##lname##sd,                                       \
                     gen_helper_##lname##ss);                                      \
}
UNARY_FP_SSE(VSQRT, sqrt)

/*
 * 00 = v*ps Vps, Hps, Wpd
 * 66 = v*pd Vpd, Hpd, Wps
 * f3 = v*ss Vss, Hss, Wps
 * f2 = v*sd Vsd, Hsd, Wps
 */
static inline void gen_fp_sse(DisasContext *s, X86DecodedInsn *decode,
                              SSEFunc_0_eppp pd_xmm, SSEFunc_0_eppp ps_xmm,
                              SSEFunc_0_eppp pd_ymm, SSEFunc_0_eppp ps_ymm,
                              SSEFunc_0_eppp sd, SSEFunc_0_eppp ss)
{
    SSEFunc_0_eppp ps, pd, fn;
    if ((s->prefix & (PREFIX_REPZ | PREFIX_REPNZ)) != 0) {
        fn = s->prefix & PREFIX_REPZ ? ss : sd;
    } else {
        ps = s->vex_l ? ps_ymm : ps_xmm;
        pd = s->vex_l ? pd_ymm : pd_xmm;
        fn = s->prefix & PREFIX_DATA ? pd : ps;
    }
    if (fn) {
        fn(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else {
        gen_illegal_opcode(s);
    }
}

#define FP_SSE(uname, lname)                                                       \
static void gen_##uname(DisasContext *s, X86DecodedInsn *decode)                   \
{                                                                                  \
    gen_fp_sse(s, decode,                                                          \
               gen_helper_##lname##pd_xmm,                                         \
               gen_helper_##lname##ps_xmm,                                         \
               gen_helper_##lname##pd_ymm,                                         \
               gen_helper_##lname##ps_ymm,                                         \
               gen_helper_##lname##sd,                                             \
               gen_helper_##lname##ss);                                            \
}
FP_SSE(VADD, add)
FP_SSE(VMUL, mul)
FP_SSE(VSUB, sub)
FP_SSE(VMIN, min)
FP_SSE(VDIV, div)
FP_SSE(VMAX, max)

#define FMA_SSE_PACKED(uname, ptr0, ptr1, ptr2, even, odd)                         \
static void gen_##uname##Px(DisasContext *s, X86DecodedInsn *decode)               \
{                                                                                  \
    SSEFunc_0_eppppii xmm = s->vex_w ? gen_helper_fma4pd_xmm : gen_helper_fma4ps_xmm; \
    SSEFunc_0_eppppii ymm = s->vex_w ? gen_helper_fma4pd_ymm : gen_helper_fma4ps_ymm; \
    SSEFunc_0_eppppii fn = s->vex_l ? ymm : xmm;                                   \
                                                                                   \
    fn(tcg_env, OP_PTR0, ptr0, ptr1, ptr2,                                         \
       tcg_constant_i32(even),                                                     \
       tcg_constant_i32((even) ^ (odd)));                                          \
}

#define FMA_SSE(uname, ptr0, ptr1, ptr2, flags)                                    \
FMA_SSE_PACKED(uname, ptr0, ptr1, ptr2, flags, flags)                              \
static void gen_##uname##Sx(DisasContext *s, X86DecodedInsn *decode)               \
{                                                                                  \
    SSEFunc_0_eppppi fn = s->vex_w ? gen_helper_fma4sd : gen_helper_fma4ss;        \
                                                                                   \
    fn(tcg_env, OP_PTR0, ptr0, ptr1, ptr2,                                         \
       tcg_constant_i32(flags));                                                   \
}                                                                                  \

FMA_SSE(VFMADD231,  OP_PTR1, OP_PTR2, OP_PTR0, 0)
FMA_SSE(VFMADD213,  OP_PTR1, OP_PTR0, OP_PTR2, 0)
FMA_SSE(VFMADD132,  OP_PTR0, OP_PTR2, OP_PTR1, 0)

FMA_SSE(VFNMADD231, OP_PTR1, OP_PTR2, OP_PTR0, float_muladd_negate_product)
FMA_SSE(VFNMADD213, OP_PTR1, OP_PTR0, OP_PTR2, float_muladd_negate_product)
FMA_SSE(VFNMADD132, OP_PTR0, OP_PTR2, OP_PTR1, float_muladd_negate_product)

FMA_SSE(VFMSUB231,  OP_PTR1, OP_PTR2, OP_PTR0, float_muladd_negate_c)
FMA_SSE(VFMSUB213,  OP_PTR1, OP_PTR0, OP_PTR2, float_muladd_negate_c)
FMA_SSE(VFMSUB132,  OP_PTR0, OP_PTR2, OP_PTR1, float_muladd_negate_c)

FMA_SSE(VFNMSUB231, OP_PTR1, OP_PTR2, OP_PTR0, float_muladd_negate_c|float_muladd_negate_product)
FMA_SSE(VFNMSUB213, OP_PTR1, OP_PTR0, OP_PTR2, float_muladd_negate_c|float_muladd_negate_product)
FMA_SSE(VFNMSUB132, OP_PTR0, OP_PTR2, OP_PTR1, float_muladd_negate_c|float_muladd_negate_product)

FMA_SSE_PACKED(VFMADDSUB231, OP_PTR1, OP_PTR2, OP_PTR0, float_muladd_negate_c, 0)
FMA_SSE_PACKED(VFMADDSUB213, OP_PTR1, OP_PTR0, OP_PTR2, float_muladd_negate_c, 0)
FMA_SSE_PACKED(VFMADDSUB132, OP_PTR0, OP_PTR2, OP_PTR1, float_muladd_negate_c, 0)

FMA_SSE_PACKED(VFMSUBADD231, OP_PTR1, OP_PTR2, OP_PTR0, 0, float_muladd_negate_c)
FMA_SSE_PACKED(VFMSUBADD213, OP_PTR1, OP_PTR0, OP_PTR2, 0, float_muladd_negate_c)
FMA_SSE_PACKED(VFMSUBADD132, OP_PTR0, OP_PTR2, OP_PTR1, 0, float_muladd_negate_c)

#define FP_UNPACK_SSE(uname, lname)                                                \
static void gen_##uname(DisasContext *s, X86DecodedInsn *decode)                   \
{                                                                                  \
    /* PS maps to the DQ integer instruction, PD maps to QDQ.  */                  \
    gen_fp_sse(s, decode,                                                          \
               gen_helper_##lname##qdq_xmm,                                        \
               gen_helper_##lname##dq_xmm,                                         \
               gen_helper_##lname##qdq_ymm,                                        \
               gen_helper_##lname##dq_ymm,                                         \
               NULL, NULL);                                                        \
}
FP_UNPACK_SSE(VUNPCKLPx, punpckl)
FP_UNPACK_SSE(VUNPCKHPx, punpckh)

/*
 * 00 = v*ps Vps, Wpd
 * f3 = v*ss Vss, Wps
 */
static inline void gen_unary_fp32_sse(DisasContext *s, X86DecodedInsn *decode,
                                      SSEFunc_0_epp ps_xmm,
                                      SSEFunc_0_epp ps_ymm,
                                      SSEFunc_0_eppp ss)
{
    if ((s->prefix & (PREFIX_DATA | PREFIX_REPNZ)) != 0) {
        goto illegal_op;
    } else if (s->prefix & PREFIX_REPZ) {
        if (!ss) {
            goto illegal_op;
        }
        ss(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else {
        SSEFunc_0_epp fn = s->vex_l ? ps_ymm : ps_xmm;
        if (!fn) {
            goto illegal_op;
        }
        fn(tcg_env, OP_PTR0, OP_PTR2);
    }
    return;

illegal_op:
    gen_illegal_opcode(s);
}
#define UNARY_FP32_SSE(uname, lname)                                               \
static void gen_##uname(DisasContext *s, X86DecodedInsn *decode)                   \
{                                                                                  \
    gen_unary_fp32_sse(s, decode,                                                  \
                       gen_helper_##lname##ps_xmm,                                 \
                       gen_helper_##lname##ps_ymm,                                 \
                       gen_helper_##lname##ss);                                    \
}
UNARY_FP32_SSE(VRSQRT, rsqrt)
UNARY_FP32_SSE(VRCP, rcp)

/*
 * 66 = v*pd Vpd, Hpd, Wpd
 * f2 = v*ps Vps, Hps, Wps
 */
static inline void gen_horizontal_fp_sse(DisasContext *s, X86DecodedInsn *decode,
                                         SSEFunc_0_eppp pd_xmm, SSEFunc_0_eppp ps_xmm,
                                         SSEFunc_0_eppp pd_ymm, SSEFunc_0_eppp ps_ymm)
{
    SSEFunc_0_eppp ps, pd, fn;
    ps = s->vex_l ? ps_ymm : ps_xmm;
    pd = s->vex_l ? pd_ymm : pd_xmm;
    fn = s->prefix & PREFIX_DATA ? pd : ps;
    fn(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2);
}
#define HORIZONTAL_FP_SSE(uname, lname)                                            \
static void gen_##uname(DisasContext *s, X86DecodedInsn *decode)                   \
{                                                                                  \
    gen_horizontal_fp_sse(s, decode,                                               \
                          gen_helper_##lname##pd_xmm, gen_helper_##lname##ps_xmm,  \
                          gen_helper_##lname##pd_ymm, gen_helper_##lname##ps_ymm); \
}
HORIZONTAL_FP_SSE(VHADD, hadd)
HORIZONTAL_FP_SSE(VHSUB, hsub)
HORIZONTAL_FP_SSE(VADDSUB, addsub)

static inline void gen_ternary_sse(DisasContext *s, X86DecodedInsn *decode,
                                   int op3, SSEFunc_0_epppp xmm, SSEFunc_0_epppp ymm)
{
    SSEFunc_0_epppp fn = s->vex_l ? ymm : xmm;
    TCGv_ptr ptr3 = tcg_temp_new_ptr();

    /* The format of the fourth input is Lx */
    tcg_gen_addi_ptr(ptr3, tcg_env, ZMM_OFFSET(op3));
    fn(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2, ptr3);
}
#define TERNARY_SSE(uname, uvname, lname)                                          \
static void gen_##uvname(DisasContext *s, X86DecodedInsn *decode)                  \
{                                                                                  \
    gen_ternary_sse(s, decode, (uint8_t)decode->immediate >> 4,                    \
                    gen_helper_##lname##_xmm, gen_helper_##lname##_ymm);           \
}                                                                                  \
static void gen_##uname(DisasContext *s, X86DecodedInsn *decode)                   \
{                                                                                  \
    gen_ternary_sse(s, decode, 0,                                                  \
                  gen_helper_##lname##_xmm, gen_helper_##lname##_ymm);             \
}
TERNARY_SSE(BLENDVPS, VBLENDVPS, blendvps)
TERNARY_SSE(BLENDVPD, VBLENDVPD, blendvpd)
TERNARY_SSE(PBLENDVB, VPBLENDVB, pblendvb)

static inline void gen_binary_imm_sse(DisasContext *s, X86DecodedInsn *decode,
                                      SSEFunc_0_epppi xmm, SSEFunc_0_epppi ymm)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    if (!s->vex_l) {
        xmm(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
    } else {
        ymm(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
    }
}

#define BINARY_IMM_SSE(uname, lname)                                               \
static void gen_##uname(DisasContext *s, X86DecodedInsn *decode)                   \
{                                                                                  \
    gen_binary_imm_sse(s, decode,                                                  \
                       gen_helper_##lname##_xmm,                                   \
                       gen_helper_##lname##_ymm);                                  \
}

BINARY_IMM_SSE(VBLENDPD,   blendpd)
BINARY_IMM_SSE(VBLENDPS,   blendps)
BINARY_IMM_SSE(VPBLENDW,   pblendw)
BINARY_IMM_SSE(VDDPS,      dpps)
#define gen_helper_dppd_ymm NULL
BINARY_IMM_SSE(VDDPD,      dppd)
BINARY_IMM_SSE(VMPSADBW,   mpsadbw)
BINARY_IMM_SSE(PCLMULQDQ,  pclmulqdq)


#define UNARY_INT_GVEC(uname, func, ...)                                           \
static void gen_##uname(DisasContext *s, X86DecodedInsn *decode)                   \
{                                                                                  \
    int vec_len = vector_len(s, decode);                                          \
                                                                                   \
    func(__VA_ARGS__, decode->op[0].offset,                                        \
         decode->op[2].offset, vec_len, vec_len);                                  \
}
UNARY_INT_GVEC(PABSB,          tcg_gen_gvec_abs, MO_8)
UNARY_INT_GVEC(PABSW,          tcg_gen_gvec_abs, MO_16)
UNARY_INT_GVEC(PABSD,          tcg_gen_gvec_abs, MO_32)
UNARY_INT_GVEC(VBROADCASTx128, tcg_gen_gvec_dup_mem, MO_128)
UNARY_INT_GVEC(VPBROADCASTB,   tcg_gen_gvec_dup_mem, MO_8)
UNARY_INT_GVEC(VPBROADCASTW,   tcg_gen_gvec_dup_mem, MO_16)
UNARY_INT_GVEC(VPBROADCASTD,   tcg_gen_gvec_dup_mem, MO_32)
UNARY_INT_GVEC(VPBROADCASTQ,   tcg_gen_gvec_dup_mem, MO_64)


#define BINARY_INT_GVEC(uname, func, ...)                                          \
static void gen_##uname(DisasContext *s, X86DecodedInsn *decode)                   \
{                                                                                  \
    int vec_len = vector_len(s, decode);                                          \
                                                                                   \
    func(__VA_ARGS__,                                                              \
         decode->op[0].offset, decode->op[1].offset,                               \
         decode->op[2].offset, vec_len, vec_len);                                  \
}

BINARY_INT_GVEC(PADDB,   tcg_gen_gvec_add, MO_8)
BINARY_INT_GVEC(PADDW,   tcg_gen_gvec_add, MO_16)
BINARY_INT_GVEC(PADDD,   tcg_gen_gvec_add, MO_32)
BINARY_INT_GVEC(PADDQ,   tcg_gen_gvec_add, MO_64)
BINARY_INT_GVEC(PADDSB,  tcg_gen_gvec_ssadd, MO_8)
BINARY_INT_GVEC(PADDSW,  tcg_gen_gvec_ssadd, MO_16)
BINARY_INT_GVEC(PADDUSB, tcg_gen_gvec_usadd, MO_8)
BINARY_INT_GVEC(PADDUSW, tcg_gen_gvec_usadd, MO_16)
BINARY_INT_GVEC(PAND,    tcg_gen_gvec_and, MO_64)
BINARY_INT_GVEC(PCMPEQB, tcg_gen_gvec_cmp, TCG_COND_EQ, MO_8)
BINARY_INT_GVEC(PCMPEQD, tcg_gen_gvec_cmp, TCG_COND_EQ, MO_32)
BINARY_INT_GVEC(PCMPEQW, tcg_gen_gvec_cmp, TCG_COND_EQ, MO_16)
BINARY_INT_GVEC(PCMPEQQ, tcg_gen_gvec_cmp, TCG_COND_EQ, MO_64)
BINARY_INT_GVEC(PCMPGTB, tcg_gen_gvec_cmp, TCG_COND_GT, MO_8)
BINARY_INT_GVEC(PCMPGTW, tcg_gen_gvec_cmp, TCG_COND_GT, MO_16)
BINARY_INT_GVEC(PCMPGTD, tcg_gen_gvec_cmp, TCG_COND_GT, MO_32)
BINARY_INT_GVEC(PCMPGTQ, tcg_gen_gvec_cmp, TCG_COND_GT, MO_64)
BINARY_INT_GVEC(PMAXSB,  tcg_gen_gvec_smax, MO_8)
BINARY_INT_GVEC(PMAXSW,  tcg_gen_gvec_smax, MO_16)
BINARY_INT_GVEC(PMAXSD,  tcg_gen_gvec_smax, MO_32)
BINARY_INT_GVEC(PMAXUB,  tcg_gen_gvec_umax, MO_8)
BINARY_INT_GVEC(PMAXUW,  tcg_gen_gvec_umax, MO_16)
BINARY_INT_GVEC(PMAXUD,  tcg_gen_gvec_umax, MO_32)
BINARY_INT_GVEC(PMINSB,  tcg_gen_gvec_smin, MO_8)
BINARY_INT_GVEC(PMINSW,  tcg_gen_gvec_smin, MO_16)
BINARY_INT_GVEC(PMINSD,  tcg_gen_gvec_smin, MO_32)
BINARY_INT_GVEC(PMINUB,  tcg_gen_gvec_umin, MO_8)
BINARY_INT_GVEC(PMINUW,  tcg_gen_gvec_umin, MO_16)
BINARY_INT_GVEC(PMINUD,  tcg_gen_gvec_umin, MO_32)
BINARY_INT_GVEC(PMULLW,  tcg_gen_gvec_mul, MO_16)
BINARY_INT_GVEC(PMULLD,  tcg_gen_gvec_mul, MO_32)
BINARY_INT_GVEC(POR,     tcg_gen_gvec_or, MO_64)
BINARY_INT_GVEC(PSUBB,   tcg_gen_gvec_sub, MO_8)
BINARY_INT_GVEC(PSUBW,   tcg_gen_gvec_sub, MO_16)
BINARY_INT_GVEC(PSUBD,   tcg_gen_gvec_sub, MO_32)
BINARY_INT_GVEC(PSUBQ,   tcg_gen_gvec_sub, MO_64)
BINARY_INT_GVEC(PSUBSB,  tcg_gen_gvec_sssub, MO_8)
BINARY_INT_GVEC(PSUBSW,  tcg_gen_gvec_sssub, MO_16)
BINARY_INT_GVEC(PSUBUSB, tcg_gen_gvec_ussub, MO_8)
BINARY_INT_GVEC(PSUBUSW, tcg_gen_gvec_ussub, MO_16)
BINARY_INT_GVEC(PXOR,    tcg_gen_gvec_xor, MO_64)


/*
 * 00 = p*  Pq, Qq (if mmx not NULL; no VEX)
 * 66 = vp* Vx, Hx, Wx
 *
 * These are really the same encoding, because 1) V is the same as P when VEX.V
 * is not present 2) P and Q are the same as H and W apart from MM/XMM
 */
static inline void gen_binary_int_sse(DisasContext *s, X86DecodedInsn *decode,
                                      SSEFunc_0_eppp mmx, SSEFunc_0_eppp xmm, SSEFunc_0_eppp ymm)
{
    assert(!!mmx == !!(decode->e.special == X86_SPECIAL_MMX));

    if (mmx && (s->prefix & PREFIX_VEX) && !(s->prefix & PREFIX_DATA)) {
        /* VEX encoding is not applicable to MMX instructions.  */
        gen_illegal_opcode(s);
        return;
    }
    if (!(s->prefix & PREFIX_DATA)) {
        mmx(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else if (!s->vex_l) {
        xmm(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else {
        ymm(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2);
    }
}


#define BINARY_INT_MMX(uname, lname)                                               \
static void gen_##uname(DisasContext *s, X86DecodedInsn *decode)                   \
{                                                                                  \
    gen_binary_int_sse(s, decode,                                                  \
                          gen_helper_##lname##_mmx,                                \
                          gen_helper_##lname##_xmm,                                \
                          gen_helper_##lname##_ymm);                               \
}
BINARY_INT_MMX(PUNPCKLBW,  punpcklbw)
BINARY_INT_MMX(PUNPCKLWD,  punpcklwd)
BINARY_INT_MMX(PUNPCKLDQ,  punpckldq)
BINARY_INT_MMX(PACKSSWB,   packsswb)
BINARY_INT_MMX(PACKUSWB,   packuswb)
BINARY_INT_MMX(PUNPCKHBW,  punpckhbw)
BINARY_INT_MMX(PUNPCKHWD,  punpckhwd)
BINARY_INT_MMX(PUNPCKHDQ,  punpckhdq)
BINARY_INT_MMX(PACKSSDW,   packssdw)

BINARY_INT_MMX(PAVGB,   pavgb)
BINARY_INT_MMX(PAVGW,   pavgw)
BINARY_INT_MMX(PMADDWD, pmaddwd)
BINARY_INT_MMX(PMULHUW, pmulhuw)
BINARY_INT_MMX(PMULHW,  pmulhw)
BINARY_INT_MMX(PMULUDQ, pmuludq)
BINARY_INT_MMX(PSADBW,  psadbw)

BINARY_INT_MMX(PSLLW_r, psllw)
BINARY_INT_MMX(PSLLD_r, pslld)
BINARY_INT_MMX(PSLLQ_r, psllq)
BINARY_INT_MMX(PSRLW_r, psrlw)
BINARY_INT_MMX(PSRLD_r, psrld)
BINARY_INT_MMX(PSRLQ_r, psrlq)
BINARY_INT_MMX(PSRAW_r, psraw)
BINARY_INT_MMX(PSRAD_r, psrad)

BINARY_INT_MMX(PHADDW,    phaddw)
BINARY_INT_MMX(PHADDSW,   phaddsw)
BINARY_INT_MMX(PHADDD,    phaddd)
BINARY_INT_MMX(PHSUBW,    phsubw)
BINARY_INT_MMX(PHSUBSW,   phsubsw)
BINARY_INT_MMX(PHSUBD,    phsubd)
BINARY_INT_MMX(PMADDUBSW, pmaddubsw)
BINARY_INT_MMX(PSHUFB,    pshufb)
BINARY_INT_MMX(PSIGNB,    psignb)
BINARY_INT_MMX(PSIGNW,    psignw)
BINARY_INT_MMX(PSIGND,    psignd)
BINARY_INT_MMX(PMULHRSW,  pmulhrsw)

/* Instructions with no MMX equivalent.  */
#define BINARY_INT_SSE(uname, lname)                                               \
static void gen_##uname(DisasContext *s, X86DecodedInsn *decode)                   \
{                                                                                  \
    gen_binary_int_sse(s, decode,                                                  \
                          NULL,                                                    \
                          gen_helper_##lname##_xmm,                                \
                          gen_helper_##lname##_ymm);                               \
}

/* Instructions with no MMX equivalent.  */
BINARY_INT_SSE(PUNPCKLQDQ, punpcklqdq)
BINARY_INT_SSE(PUNPCKHQDQ, punpckhqdq)
BINARY_INT_SSE(VPACKUSDW,  packusdw)
BINARY_INT_SSE(VPERMILPS,  vpermilps)
BINARY_INT_SSE(VPERMILPD,  vpermilpd)
BINARY_INT_SSE(VMASKMOVPS, vpmaskmovd)
BINARY_INT_SSE(VMASKMOVPD, vpmaskmovq)

BINARY_INT_SSE(PMULDQ,    pmuldq)

BINARY_INT_SSE(VAESDEC, aesdec)
BINARY_INT_SSE(VAESDECLAST, aesdeclast)
BINARY_INT_SSE(VAESENC, aesenc)
BINARY_INT_SSE(VAESENCLAST, aesenclast)

#define UNARY_CMP_SSE(uname, lname)                                                \
static void gen_##uname(DisasContext *s, X86DecodedInsn *decode)                   \
{                                                                                  \
    if (!s->vex_l) {                                                               \
        gen_helper_##lname##_xmm(tcg_env, OP_PTR1, OP_PTR2);                       \
    } else {                                                                       \
        gen_helper_##lname##_ymm(tcg_env, OP_PTR1, OP_PTR2);                       \
    }                                                                              \
    assume_cc_op(s, CC_OP_EFLAGS);                                                  \
}
UNARY_CMP_SSE(VPTEST,     ptest)
UNARY_CMP_SSE(VTESTPS,    vtestps)
UNARY_CMP_SSE(VTESTPD,    vtestpd)

static inline void gen_unary_int_sse(DisasContext *s, X86DecodedInsn *decode,
                                     SSEFunc_0_epp xmm, SSEFunc_0_epp ymm)
{
    if (!s->vex_l) {
        xmm(tcg_env, OP_PTR0, OP_PTR2);
    } else {
        ymm(tcg_env, OP_PTR0, OP_PTR2);
    }
}

#define UNARY_INT_SSE(uname, lname)                                                \
static void gen_##uname(DisasContext *s, X86DecodedInsn *decode)                   \
{                                                                                  \
    gen_unary_int_sse(s, decode,                                                   \
                      gen_helper_##lname##_xmm,                                    \
                      gen_helper_##lname##_ymm);                                   \
}

UNARY_INT_SSE(VPMOVSXBW,    pmovsxbw)
UNARY_INT_SSE(VPMOVSXBD,    pmovsxbd)
UNARY_INT_SSE(VPMOVSXBQ,    pmovsxbq)
UNARY_INT_SSE(VPMOVSXWD,    pmovsxwd)
UNARY_INT_SSE(VPMOVSXWQ,    pmovsxwq)
UNARY_INT_SSE(VPMOVSXDQ,    pmovsxdq)

UNARY_INT_SSE(VPMOVZXBW,    pmovzxbw)
UNARY_INT_SSE(VPMOVZXBD,    pmovzxbd)
UNARY_INT_SSE(VPMOVZXBQ,    pmovzxbq)
UNARY_INT_SSE(VPMOVZXWD,    pmovzxwd)
UNARY_INT_SSE(VPMOVZXWQ,    pmovzxwq)
UNARY_INT_SSE(VPMOVZXDQ,    pmovzxdq)

UNARY_INT_SSE(VMOVSLDUP,    pmovsldup)
UNARY_INT_SSE(VMOVSHDUP,    pmovshdup)
UNARY_INT_SSE(VMOVDDUP,     pmovdldup)

UNARY_INT_SSE(VCVTDQ2PD, cvtdq2pd)
UNARY_INT_SSE(VCVTPD2DQ, cvtpd2dq)
UNARY_INT_SSE(VCVTTPD2DQ, cvttpd2dq)
UNARY_INT_SSE(VCVTDQ2PS, cvtdq2ps)
UNARY_INT_SSE(VCVTPS2DQ, cvtps2dq)
UNARY_INT_SSE(VCVTTPS2DQ, cvttps2dq)
UNARY_INT_SSE(VCVTPH2PS, cvtph2ps)


static inline void gen_unary_imm_sse(DisasContext *s, X86DecodedInsn *decode,
                                     SSEFunc_0_ppi xmm, SSEFunc_0_ppi ymm)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    if (!s->vex_l) {
        xmm(OP_PTR0, OP_PTR1, imm);
    } else {
        ymm(OP_PTR0, OP_PTR1, imm);
    }
}

#define UNARY_IMM_SSE(uname, lname)                                                \
static void gen_##uname(DisasContext *s, X86DecodedInsn *decode)                   \
{                                                                                  \
    gen_unary_imm_sse(s, decode,                                                   \
                      gen_helper_##lname##_xmm,                                    \
                      gen_helper_##lname##_ymm);                                   \
}

UNARY_IMM_SSE(PSHUFD,     pshufd)
UNARY_IMM_SSE(PSHUFHW,    pshufhw)
UNARY_IMM_SSE(PSHUFLW,    pshuflw)
#define gen_helper_vpermq_xmm NULL
UNARY_IMM_SSE(VPERMQ,      vpermq)
UNARY_IMM_SSE(VPERMILPS_i, vpermilps_imm)
UNARY_IMM_SSE(VPERMILPD_i, vpermilpd_imm)

static inline void gen_unary_imm_fp_sse(DisasContext *s, X86DecodedInsn *decode,
                                        SSEFunc_0_eppi xmm, SSEFunc_0_eppi ymm)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    if (!s->vex_l) {
        xmm(tcg_env, OP_PTR0, OP_PTR1, imm);
    } else {
        ymm(tcg_env, OP_PTR0, OP_PTR1, imm);
    }
}

#define UNARY_IMM_FP_SSE(uname, lname)                                             \
static void gen_##uname(DisasContext *s, X86DecodedInsn *decode)                   \
{                                                                                  \
    gen_unary_imm_fp_sse(s, decode,                                                \
                      gen_helper_##lname##_xmm,                                    \
                      gen_helper_##lname##_ymm);                                   \
}

UNARY_IMM_FP_SSE(VROUNDPS,    roundps)
UNARY_IMM_FP_SSE(VROUNDPD,    roundpd)

static inline void gen_vexw_avx(DisasContext *s, X86DecodedInsn *decode,
                                SSEFunc_0_eppp d_xmm, SSEFunc_0_eppp q_xmm,
                                SSEFunc_0_eppp d_ymm, SSEFunc_0_eppp q_ymm)
{
    SSEFunc_0_eppp d = s->vex_l ? d_ymm : d_xmm;
    SSEFunc_0_eppp q = s->vex_l ? q_ymm : q_xmm;
    SSEFunc_0_eppp fn = s->vex_w ? q : d;
    fn(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2);
}

/* VEX.W affects whether to operate on 32- or 64-bit elements.  */
#define VEXW_AVX(uname, lname)                                                     \
static void gen_##uname(DisasContext *s, X86DecodedInsn *decode)                   \
{                                                                                  \
    gen_vexw_avx(s, decode,                                                        \
                 gen_helper_##lname##d_xmm, gen_helper_##lname##q_xmm,             \
                 gen_helper_##lname##d_ymm, gen_helper_##lname##q_ymm);            \
}
VEXW_AVX(VPSLLV,    vpsllv)
VEXW_AVX(VPSRLV,    vpsrlv)
VEXW_AVX(VPSRAV,    vpsrav)
VEXW_AVX(VPMASKMOV, vpmaskmov)

/* Same as above, but with extra arguments to the helper.  */
static inline void gen_vsib_avx(DisasContext *s, X86DecodedInsn *decode,
                                SSEFunc_0_epppti d_xmm, SSEFunc_0_epppti q_xmm,
                                SSEFunc_0_epppti d_ymm, SSEFunc_0_epppti q_ymm)
{
    SSEFunc_0_epppti d = s->vex_l ? d_ymm : d_xmm;
    SSEFunc_0_epppti q = s->vex_l ? q_ymm : q_xmm;
    SSEFunc_0_epppti fn = s->vex_w ? q : d;
    TCGv_i32 scale = tcg_constant_i32(decode->mem.scale);
    TCGv_ptr index = tcg_temp_new_ptr();

    /* Pass third input as (index, base, scale) */
    tcg_gen_addi_ptr(index, tcg_env, ZMM_OFFSET(decode->mem.index));
    fn(tcg_env, OP_PTR0, OP_PTR1, index, s->A0, scale);

    /*
     * There are two output operands, so zero OP1's high 128 bits
     * in the VEX.128 case.
     */
    if (!s->vex_l) {
        int ymmh_ofs = vector_elem_offset(&decode->op[1], MO_128, 1);
        tcg_gen_gvec_dup_imm(MO_64, ymmh_ofs, 16, 16, 0);
    }
}
#define VSIB_AVX(uname, lname)                                                     \
static void gen_##uname(DisasContext *s, X86DecodedInsn *decode)                   \
{                                                                                  \
    gen_vsib_avx(s, decode,                                                        \
                 gen_helper_##lname##d_xmm, gen_helper_##lname##q_xmm,             \
                 gen_helper_##lname##d_ymm, gen_helper_##lname##q_ymm);            \
}
VSIB_AVX(VPGATHERD, vpgatherd)
VSIB_AVX(VPGATHERQ, vpgatherq)

static void gen_AAA(DisasContext *s, X86DecodedInsn *decode)
{
    gen_update_cc_op(s);
    gen_helper_aaa(tcg_env);
    assume_cc_op(s, CC_OP_EFLAGS);
}

static void gen_AAD(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_aad(s->T0, s->T0, s->T1);
    prepare_update1_cc(decode, s, CC_OP_LOGICB);
}

static void gen_AAM(DisasContext *s, X86DecodedInsn *decode)
{
    if (decode->immediate == 0) {
        gen_exception(s, EXCP00_DIVZ);
    } else {
        gen_helper_aam(s->T0, s->T0, s->T1);
        prepare_update1_cc(decode, s, CC_OP_LOGICB);
    }
}

static void gen_AAS(DisasContext *s, X86DecodedInsn *decode)
{
    gen_update_cc_op(s);
    gen_helper_aas(tcg_env);
    assume_cc_op(s, CC_OP_EFLAGS);
}

static void gen_ADD(DisasContext *s, X86DecodedInsn *decode);
static void gen_ADC(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;
    TCGv c_in;

    /*
     * Try to avoid CC_OP_ADC by transforming as follows:
     * CC_ADC: src1 = dst + c_in, src2 = 0, src3 = c_in
     * CC_ADD: src1 = dst + c_in, src2 = c_in (no src3)
     *
     * In general src2 vs. src3 matters when computing AF and OF, but not here:
     * - AF is bit 4 of dst^src1^src2, which is bit 4 of dst^src1 in both cases
     * - OF is a function of the two MSBs, and in both cases they are zero for src2
     */
    if (decode->e.op2 == X86_TYPE_I && decode->immediate == 0) {
        gen_compute_eflags_c(s, s->T1);
        gen_ADD(s, decode);
        return;
    }

    c_in = tcg_temp_new();
    gen_compute_eflags_c(s, c_in);
    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_add_tl(s->T0, c_in, s->T1);
        tcg_gen_atomic_add_fetch_tl(s->T0, s->A0, s->T0,
                                    s->mem_index, ot | MO_LE);
    } else {
        tcg_gen_add_tl(s->T0, s->T0, s->T1);
        tcg_gen_add_tl(s->T0, s->T0, c_in);
    }
    prepare_update3_cc(decode, s, CC_OP_ADCB + ot, c_in);
}

static void gen_ADCOX(DisasContext *s, X86DecodedInsn *decode, int cc_op)
{
    MemOp ot = decode->op[0].ot;
    TCGv carry_in = NULL;
    TCGv *carry_out = (cc_op == CC_OP_ADCX ? &decode->cc_dst : &decode->cc_src2);
    TCGv zero;

    decode->cc_op = cc_op;
    *carry_out = tcg_temp_new();
    if (CC_OP_HAS_EFLAGS(s->cc_op)) {
        decode->cc_src = cpu_cc_src;

        /* Re-use the carry-out from a previous round?  */
        if (s->cc_op == cc_op || s->cc_op == CC_OP_ADCOX) {
            carry_in = (cc_op == CC_OP_ADCX ? cpu_cc_dst : cpu_cc_src2);
        }

        /* Preserve the opposite carry from previous rounds?  */
        if (s->cc_op != cc_op && s->cc_op != CC_OP_EFLAGS) {
            decode->cc_op = CC_OP_ADCOX;
            if (carry_out == &decode->cc_dst) {
                decode->cc_src2 = cpu_cc_src2;
            } else {
                decode->cc_dst = cpu_cc_dst;
            }
        }
    } else {
        decode->cc_src = tcg_temp_new();
        gen_mov_eflags(s, decode->cc_src);
    }

    if (!carry_in) {
        /* Get carry_in out of EFLAGS.  */
        carry_in = tcg_temp_new();
        tcg_gen_extract_tl(carry_in, decode->cc_src,
            ctz32(cc_op == CC_OP_ADCX ? CC_C : CC_O), 1);
    }

    switch (ot) {
#ifdef TARGET_X86_64
    case MO_32:
        /* If TL is 64-bit just do everything in 64-bit arithmetic.  */
        tcg_gen_ext32u_tl(s->T0, s->T0);
        tcg_gen_ext32u_tl(s->T1, s->T1);
        tcg_gen_add_i64(s->T0, s->T0, s->T1);
        tcg_gen_add_i64(s->T0, s->T0, carry_in);
        tcg_gen_shri_i64(*carry_out, s->T0, 32);
        break;
#endif
    default:
        zero = tcg_constant_tl(0);
        tcg_gen_add2_tl(s->T0, *carry_out, s->T0, zero, carry_in, zero);
        tcg_gen_add2_tl(s->T0, *carry_out, s->T0, *carry_out, s->T1, zero);
        break;
    }
}

static void gen_ADCX(DisasContext *s, X86DecodedInsn *decode)
{
    gen_ADCOX(s, decode, CC_OP_ADCX);
}

static void gen_ADD(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;

    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_atomic_add_fetch_tl(s->T0, s->A0, s->T1,
                                    s->mem_index, ot | MO_LE);
    } else {
        tcg_gen_add_tl(s->T0, s->T0, s->T1);
    }
    prepare_update2_cc(decode, s, CC_OP_ADDB + ot);
}

static void gen_ADOX(DisasContext *s, X86DecodedInsn *decode)
{
    gen_ADCOX(s, decode, CC_OP_ADOX);
}

static void gen_AND(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;

    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_atomic_and_fetch_tl(s->T0, s->A0, s->T1,
                                    s->mem_index, ot | MO_LE);
    } else {
        tcg_gen_and_tl(s->T0, s->T0, s->T1);
    }
    prepare_update1_cc(decode, s, CC_OP_LOGICB + ot);
}

static void gen_ANDN(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;

    tcg_gen_andc_tl(s->T0, s->T1, s->T0);
    prepare_update1_cc(decode, s, CC_OP_LOGICB + ot);
}

static void gen_ARPL(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv zf = tcg_temp_new();
    TCGv flags = tcg_temp_new();

    gen_mov_eflags(s, flags);

    /* Compute adjusted DST in T1, merging in SRC[RPL].  */
    tcg_gen_deposit_tl(s->T1, s->T0, s->T1, 0, 2);

    /* Z flag set if DST[RPL] < SRC[RPL] */
    tcg_gen_setcond_tl(TCG_COND_LTU, zf, s->T0, s->T1);
    tcg_gen_deposit_tl(flags, flags, zf, ctz32(CC_Z), 1);

    /* Place maximum RPL in DST */
    tcg_gen_umax_tl(s->T0, s->T0, s->T1);

    decode->cc_src = flags;
    decode->cc_op = CC_OP_EFLAGS;
}

static void gen_BEXTR(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    TCGv bound = tcg_constant_tl(ot == MO_64 ? 63 : 31);
    TCGv zero = tcg_constant_tl(0);
    TCGv mone = tcg_constant_tl(-1);

    /*
     * Extract START, and shift the operand.
     * Shifts larger than operand size get zeros.
     */
    tcg_gen_ext8u_tl(s->A0, s->T1);
    tcg_gen_shr_tl(s->T0, s->T0, s->A0);

    tcg_gen_movcond_tl(TCG_COND_LEU, s->T0, s->A0, bound, s->T0, zero);

    /*
     * Extract the LEN into an inverse mask.  Lengths larger than
     * operand size get all zeros, length 0 gets all ones.
     */
    tcg_gen_extract_tl(s->A0, s->T1, 8, 8);
    tcg_gen_shl_tl(s->T1, mone, s->A0);
    tcg_gen_movcond_tl(TCG_COND_LEU, s->T1, s->A0, bound, s->T1, zero);
    tcg_gen_andc_tl(s->T0, s->T0, s->T1);

    prepare_update1_cc(decode, s, CC_OP_LOGICB + ot);
}

static void gen_BLSI(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;

    /* input in T1, which is ready for prepare_update2_cc  */
    tcg_gen_neg_tl(s->T0, s->T1);
    tcg_gen_and_tl(s->T0, s->T0, s->T1);
    prepare_update2_cc(decode, s, CC_OP_BLSIB + ot);
}

static void gen_BLSMSK(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;

    /* input in T1, which is ready for prepare_update2_cc  */
    tcg_gen_subi_tl(s->T0, s->T1, 1);
    tcg_gen_xor_tl(s->T0, s->T0, s->T1);
    prepare_update2_cc(decode, s, CC_OP_BMILGB + ot);
}

static void gen_BLSR(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;

    /* input in T1, which is ready for prepare_update2_cc  */
    tcg_gen_subi_tl(s->T0, s->T1, 1);
    tcg_gen_and_tl(s->T0, s->T0, s->T1);
    prepare_update2_cc(decode, s, CC_OP_BMILGB + ot);
}

static void gen_BOUND(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 op = tcg_temp_new_i32();
    tcg_gen_trunc_tl_i32(op, s->T0);
    if (decode->op[1].ot == MO_16) {
        gen_helper_boundw(tcg_env, s->A0, op);
    } else {
        gen_helper_boundl(tcg_env, s->A0, op);
    }
}

/* Non-standard convention - on entry T0 is zero-extended input, T1 is the output.  */
static void gen_BSF(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;

    /* Only the Z bit is defined and it is related to the input.  */
    decode->cc_dst = tcg_temp_new();
    decode->cc_op = CC_OP_LOGICB + ot;
    tcg_gen_mov_tl(decode->cc_dst, s->T0);

    /*
     * The manual says that the output is undefined when the
     * input is zero, but real hardware leaves it unchanged, and
     * real programs appear to depend on that.  Accomplish this
     * by passing the output as the value to return upon zero.
     */
    tcg_gen_ctz_tl(s->T0, s->T0, s->T1);
}

/* Non-standard convention - on entry T0 is zero-extended input, T1 is the output.  */
static void gen_BSR(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;

    /* Only the Z bit is defined and it is related to the input.  */
    decode->cc_dst = tcg_temp_new();
    decode->cc_op = CC_OP_LOGICB + ot;
    tcg_gen_mov_tl(decode->cc_dst, s->T0);

    /*
     * The manual says that the output is undefined when the
     * input is zero, but real hardware leaves it unchanged, and
     * real programs appear to depend on that.  Accomplish this
     * by passing the output as the value to return upon zero.
     * Plus, return the bit index of the first 1 bit.
     */
    tcg_gen_xori_tl(s->T1, s->T1, TARGET_LONG_BITS - 1);
    tcg_gen_clz_tl(s->T0, s->T0, s->T1);
    tcg_gen_xori_tl(s->T0, s->T0, TARGET_LONG_BITS - 1);
}

static void gen_BSWAP(DisasContext *s, X86DecodedInsn *decode)
{
#ifdef TARGET_X86_64
    if (s->dflag == MO_64) {
        tcg_gen_bswap64_i64(s->T0, s->T0);
        return;
    }
#endif
    tcg_gen_bswap32_tl(s->T0, s->T0, TCG_BSWAP_OZ);
}

static TCGv gen_bt_mask(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;
    TCGv mask = tcg_temp_new();

    tcg_gen_andi_tl(s->T1, s->T1, (8 << ot) - 1);
    tcg_gen_shl_tl(mask, tcg_constant_tl(1), s->T1);
    return mask;
}

/* Expects truncated bit index in COUNT, 1 << COUNT in MASK.  */
static void gen_bt_flags(DisasContext *s, X86DecodedInsn *decode, TCGv src,
                         TCGv count, TCGv mask)
{
    TCGv cf;

    /*
     * C is the result of the test, Z is unchanged, and the others
     * are all undefined.
     */
    if (s->cc_op == CC_OP_DYNAMIC || CC_OP_HAS_EFLAGS(s->cc_op)) {
        /* Generate EFLAGS and replace the C bit.  */
        cf = tcg_temp_new();
        tcg_gen_setcond_tl(TCG_COND_TSTNE, cf, src, mask);
        prepare_update_cf(decode, s, cf);
    } else {
        /*
         * Z was going to be computed from the non-zero status of CC_DST.
         * We can get that same Z value (and the new C value) by leaving
         * CC_DST alone, setting CC_SRC, and using a CC_OP_SAR of the
         * same width.
         */
        decode->cc_src = tcg_temp_new();
        decode->cc_dst = cpu_cc_dst;
        decode->cc_op = CC_OP_SARB + cc_op_size(s->cc_op);
        tcg_gen_shr_tl(decode->cc_src, src, count);
    }
}

static void gen_BT(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv count = s->T1;
    TCGv mask;

    /*
     * Try to ensure that the rhs of the TSTNE condition is a constant (and a
     * power of two), as that is more readily available on most TCG backends.
     *
     * For immediate bit number gen_bt_mask()'s output is already a constant;
     * for register bit number, shift the source right and check bit 0.
     */
    if (decode->e.op2 == X86_TYPE_I) {
        mask = gen_bt_mask(s, decode);
    } else {
        MemOp ot = decode->op[1].ot;

        tcg_gen_andi_tl(s->T1, s->T1, (8 << ot) - 1);
        tcg_gen_shr_tl(s->T0, s->T0, s->T1);

        count = tcg_constant_tl(0);
        mask = tcg_constant_tl(1);
    }
    gen_bt_flags(s, decode, s->T0, count, mask);
}

static void gen_BTC(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    TCGv old = tcg_temp_new();
    TCGv mask = gen_bt_mask(s, decode);

    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_atomic_fetch_xor_tl(old, s->A0, mask, s->mem_index, ot | MO_LE);
    } else {
        tcg_gen_mov_tl(old, s->T0);
        tcg_gen_xor_tl(s->T0, s->T0, mask);
    }

    gen_bt_flags(s, decode, old, s->T1, mask);
}

static void gen_BTR(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    TCGv old = tcg_temp_new();
    TCGv mask = gen_bt_mask(s, decode);

    if (s->prefix & PREFIX_LOCK) {
        TCGv maskc = tcg_temp_new();
        tcg_gen_not_tl(maskc, mask);
        tcg_gen_atomic_fetch_and_tl(old, s->A0, maskc, s->mem_index, ot | MO_LE);
    } else {
        tcg_gen_mov_tl(old, s->T0);
        tcg_gen_andc_tl(s->T0, s->T0, mask);
    }

    gen_bt_flags(s, decode, old, s->T1, mask);
}

static void gen_BTS(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    TCGv old = tcg_temp_new();
    TCGv mask = gen_bt_mask(s, decode);

    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_atomic_fetch_or_tl(old, s->A0, mask, s->mem_index, ot | MO_LE);
    } else {
        tcg_gen_mov_tl(old, s->T0);
        tcg_gen_or_tl(s->T0, s->T0, mask);
    }

    gen_bt_flags(s, decode, old, s->T1, mask);
}

static void gen_BZHI(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    TCGv bound = tcg_constant_tl(ot == MO_64 ? 63 : 31);
    TCGv zero = tcg_constant_tl(0);
    TCGv mone = tcg_constant_tl(-1);

    tcg_gen_ext8u_tl(s->T1, s->T1);

    tcg_gen_shl_tl(s->A0, mone, s->T1);
    tcg_gen_movcond_tl(TCG_COND_LEU, s->A0, s->T1, bound, s->A0, zero);
    tcg_gen_andc_tl(s->T0, s->T0, s->A0);
    /*
     * Note that since we're using BMILG (in order to get O
     * cleared) we need to store the inverse into C.
     */
    tcg_gen_setcond_tl(TCG_COND_LEU, s->T1, s->T1, bound);
    prepare_update2_cc(decode, s, CC_OP_BMILGB + ot);
}

static void gen_CALL(DisasContext *s, X86DecodedInsn *decode)
{
    gen_push_v(s, eip_next_tl(s));
    gen_JMP(s, decode);
}

static void gen_CALL_m(DisasContext *s, X86DecodedInsn *decode)
{
    gen_push_v(s, eip_next_tl(s));
    gen_JMP_m(s, decode);
}

static void gen_CALLF(DisasContext *s, X86DecodedInsn *decode)
{
    gen_far_call(s);
}

static void gen_CALLF_m(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;

    gen_op_ld_v(s, ot, s->T0, s->A0);
    gen_add_A0_im(s, 1 << ot);
    gen_op_ld_v(s, MO_16, s->T1, s->A0);
    gen_far_call(s);
}

static void gen_CBW(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp src_ot = decode->op[0].ot - 1;

    tcg_gen_ext_tl(s->T0, s->T0, src_ot | MO_SIGN);
}

static void gen_CLC(DisasContext *s, X86DecodedInsn *decode)
{
    gen_compute_eflags(s);
    tcg_gen_andi_tl(cpu_cc_src, cpu_cc_src, ~CC_C);
}

static void gen_CLD(DisasContext *s, X86DecodedInsn *decode)
{
    tcg_gen_st_i32(tcg_constant_i32(1), tcg_env, offsetof(CPUX86State, df));
}

static void gen_CLI(DisasContext *s, X86DecodedInsn *decode)
{
    gen_reset_eflags(s, IF_MASK);
}

static void gen_CLTS(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_clts(tcg_env);
    /* abort block because static cpu state changed */
    s->base.is_jmp = DISAS_EOB_NEXT;
}

static void gen_CMC(DisasContext *s, X86DecodedInsn *decode)
{
    gen_compute_eflags(s);
    tcg_gen_xori_tl(cpu_cc_src, cpu_cc_src, CC_C);
}

static void gen_CMOVcc(DisasContext *s, X86DecodedInsn *decode)
{
    gen_cmovcc(s, decode->b & 0xf, s->T0, s->T1);
}

static void gen_CMPccXADD(DisasContext *s, X86DecodedInsn *decode)
{
    TCGLabel *label_top = gen_new_label();
    TCGLabel *label_bottom = gen_new_label();
    TCGv oldv = tcg_temp_new();
    TCGv newv = tcg_temp_new();
    TCGv cmpv = tcg_temp_new();
    TCGCond cond;

    TCGv cmp_lhs, cmp_rhs;
    MemOp ot, ot_full;

    int jcc_op = (decode->b >> 1) & 7;
    static const TCGCond cond_table[8] = {
        [JCC_O] = TCG_COND_LT,  /* test sign bit by comparing against 0 */
        [JCC_B] = TCG_COND_LTU,
        [JCC_Z] = TCG_COND_EQ,
        [JCC_BE] = TCG_COND_LEU,
        [JCC_S] = TCG_COND_LT,  /* test sign bit by comparing against 0 */
        [JCC_P] = TCG_COND_TSTEQ,  /* even parity - tests low bit of popcount */
        [JCC_L] = TCG_COND_LT,
        [JCC_LE] = TCG_COND_LE,
    };

    cond = cond_table[jcc_op];
    if (decode->b & 1) {
        cond = tcg_invert_cond(cond);
    }

    ot = decode->op[0].ot;
    ot_full = ot | MO_LE;
    if (jcc_op >= JCC_S) {
        /*
         * Sign-extend values before subtracting for S, P (zero/sign extension
         * does not matter there) L, LE and their inverses.
         */
        ot_full |= MO_SIGN;
    }

    /*
     * cmpv will be moved to cc_src *after* cpu_regs[] is written back, so use
     * tcg_gen_ext_tl instead of gen_ext_tl.
     */
    tcg_gen_ext_tl(cmpv, cpu_regs[decode->op[1].n], ot_full);

    /*
     * Cmpxchg loop starts here.
     * - s->T1: addition operand (from decoder)
     * - s->A0: dest address (from decoder)
     * - s->cc_srcT: memory operand (lhs for comparison)
     * - cmpv: rhs for comparison
     */
    gen_set_label(label_top);
    gen_op_ld_v(s, ot_full, s->cc_srcT, s->A0);
    tcg_gen_sub_tl(s->T0, s->cc_srcT, cmpv);

    /* Compute the comparison result by hand, to avoid clobbering cc_*.  */
    switch (jcc_op) {
    case JCC_O:
        /* (src1 ^ src2) & (src1 ^ dst). newv is only used here for a moment */
        cmp_lhs = tcg_temp_new(), cmp_rhs = tcg_constant_tl(0);
        tcg_gen_xor_tl(newv, s->cc_srcT, s->T0);
        tcg_gen_xor_tl(cmp_lhs, s->cc_srcT, cmpv);
        tcg_gen_and_tl(cmp_lhs, cmp_lhs, newv);
        tcg_gen_sextract_tl(cmp_lhs, cmp_lhs, 0, 8 << ot);
        break;

    case JCC_P:
        cmp_lhs = tcg_temp_new(), cmp_rhs = tcg_constant_tl(1);
        tcg_gen_ext8u_tl(cmp_lhs, s->T0);
        tcg_gen_ctpop_tl(cmp_lhs, cmp_lhs);
        break;

    case JCC_S:
        cmp_lhs = tcg_temp_new(), cmp_rhs = tcg_constant_tl(0);
        tcg_gen_sextract_tl(cmp_lhs, s->T0, 0, 8 << ot);
        break;

    default:
        cmp_lhs = s->cc_srcT, cmp_rhs = cmpv;
        break;
    }

    /* Compute new value: if condition does not hold, just store back s->cc_srcT */
    tcg_gen_add_tl(newv, s->cc_srcT, s->T1);
    tcg_gen_movcond_tl(cond, newv, cmp_lhs, cmp_rhs, newv, s->cc_srcT);
    tcg_gen_atomic_cmpxchg_tl(oldv, s->A0, s->cc_srcT, newv, s->mem_index, ot_full);

    /* Exit unconditionally if cmpxchg succeeded.  */
    tcg_gen_brcond_tl(TCG_COND_EQ, oldv, s->cc_srcT, label_bottom);

    /* Try again if there was actually a store to make.  */
    tcg_gen_brcond_tl(cond, cmp_lhs, cmp_rhs, label_top);
    gen_set_label(label_bottom);

    /* Store old value to registers only after a successful store.  */
    gen_writeback(s, decode, 1, s->cc_srcT);

    decode->cc_dst = s->T0;
    decode->cc_src = cmpv;
    decode->cc_op = CC_OP_SUBB + ot;
}

static void gen_CMPS(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[2].ot;
    gen_repz_nz(s, ot, gen_cmps);
}

static void gen_CMPXCHG(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[2].ot;
    TCGv cmpv = tcg_temp_new();
    TCGv oldv = tcg_temp_new();
    TCGv newv = tcg_temp_new();
    TCGv dest;

    tcg_gen_ext_tl(cmpv, cpu_regs[R_EAX], ot);
    tcg_gen_ext_tl(newv, s->T1, ot);
    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_atomic_cmpxchg_tl(oldv, s->A0, cmpv, newv,
                                  s->mem_index, ot | MO_LE);
    } else {
        tcg_gen_ext_tl(oldv, s->T0, ot);
        if (decode->op[0].has_ea) {
            /*
             * Perform an unconditional store cycle like physical cpu;
             * must be before changing accumulator to ensure
             * idempotency if the store faults and the instruction
             * is restarted
             */
            tcg_gen_movcond_tl(TCG_COND_EQ, newv, oldv, cmpv, newv, oldv);
            gen_op_st_v(s, ot, newv, s->A0);
        } else {
            /*
             * Unlike the memory case, where "the destination operand receives
             * a write cycle without regard to the result of the comparison",
             * rm must not be touched altogether if the write fails, including
             * not zero-extending it on 64-bit processors.  So, precompute
             * the result of a successful writeback and perform the movcond
             * directly on cpu_regs.  In case rm is part of RAX, note that this
             * movcond and the one below are mutually exclusive is executed.
             */
            dest = gen_op_deposit_reg_v(s, ot, decode->op[0].n, newv, newv);
            tcg_gen_movcond_tl(TCG_COND_EQ, dest, oldv, cmpv, newv, dest);
        }
        decode->op[0].unit = X86_OP_SKIP;
    }

    /* Write RAX only if the cmpxchg fails.  */
    dest = gen_op_deposit_reg_v(s, ot, R_EAX, s->T0, oldv);
    tcg_gen_movcond_tl(TCG_COND_NE, dest, oldv, cmpv, s->T0, dest);

    tcg_gen_mov_tl(s->cc_srcT, cmpv);
    tcg_gen_sub_tl(cmpv, cmpv, oldv);
    decode->cc_dst = cmpv;
    decode->cc_src = oldv;
    decode->cc_op = CC_OP_SUBB + ot;
}

static void gen_CMPXCHG16B(DisasContext *s, X86DecodedInsn *decode)
{
#ifdef TARGET_X86_64
    MemOp mop = MO_TE | MO_128 | MO_ALIGN;
    TCGv_i64 t0, t1;
    TCGv_i128 cmp, val;

    cmp = tcg_temp_new_i128();
    val = tcg_temp_new_i128();
    tcg_gen_concat_i64_i128(cmp, cpu_regs[R_EAX], cpu_regs[R_EDX]);
    tcg_gen_concat_i64_i128(val, cpu_regs[R_EBX], cpu_regs[R_ECX]);

    /* Only require atomic with LOCK; non-parallel handled in generator. */
    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_atomic_cmpxchg_i128(val, s->A0, cmp, val, s->mem_index, mop);
    } else {
        tcg_gen_nonatomic_cmpxchg_i128(val, s->A0, cmp, val, s->mem_index, mop);
    }

    tcg_gen_extr_i128_i64(s->T0, s->T1, val);

    /* Determine success after the fact. */
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    tcg_gen_xor_i64(t0, s->T0, cpu_regs[R_EAX]);
    tcg_gen_xor_i64(t1, s->T1, cpu_regs[R_EDX]);
    tcg_gen_or_i64(t0, t0, t1);

    /* Update Z. */
    gen_compute_eflags(s);
    tcg_gen_setcondi_i64(TCG_COND_EQ, t0, t0, 0);
    tcg_gen_deposit_tl(cpu_cc_src, cpu_cc_src, t0, ctz32(CC_Z), 1);

    /*
     * Extract the result values for the register pair.  We may do this
     * unconditionally, because on success (Z=1), the old value matches
     * the previous value in RDX:RAX.
     */
    tcg_gen_mov_i64(cpu_regs[R_EAX], s->T0);
    tcg_gen_mov_i64(cpu_regs[R_EDX], s->T1);
#else
    abort();
#endif
}

static void gen_CMPXCHG8B(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i64 cmp, val, old;
    TCGv Z;

    cmp = tcg_temp_new_i64();
    val = tcg_temp_new_i64();
    old = tcg_temp_new_i64();

    /* Construct the comparison values from the register pair. */
    tcg_gen_concat_tl_i64(cmp, cpu_regs[R_EAX], cpu_regs[R_EDX]);
    tcg_gen_concat_tl_i64(val, cpu_regs[R_EBX], cpu_regs[R_ECX]);

    /* Only require atomic with LOCK; non-parallel handled in generator. */
    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_atomic_cmpxchg_i64(old, s->A0, cmp, val, s->mem_index, MO_TEUQ);
    } else {
        tcg_gen_nonatomic_cmpxchg_i64(old, s->A0, cmp, val,
                                      s->mem_index, MO_TEUQ);
    }

    /* Compute the required value of Z. */
    tcg_gen_setcond_i64(TCG_COND_EQ, cmp, old, cmp);
    Z = tcg_temp_new();
    tcg_gen_trunc_i64_tl(Z, cmp);

    /*
     * Extract the result values for the register pair.
     * For 32-bit, we may do this unconditionally, because on success (Z=1),
     * the old value matches the previous value in EDX:EAX.  For x86_64,
     * the store must be conditional, because we must leave the source
     * registers unchanged on success, and zero-extend the writeback
     * on failure (Z=0).
     */
    if (TARGET_LONG_BITS == 32) {
        tcg_gen_extr_i64_tl(cpu_regs[R_EAX], cpu_regs[R_EDX], old);
    } else {
        TCGv zero = tcg_constant_tl(0);

        tcg_gen_extr_i64_tl(s->T0, s->T1, old);
        tcg_gen_movcond_tl(TCG_COND_EQ, cpu_regs[R_EAX], Z, zero,
                           s->T0, cpu_regs[R_EAX]);
        tcg_gen_movcond_tl(TCG_COND_EQ, cpu_regs[R_EDX], Z, zero,
                           s->T1, cpu_regs[R_EDX]);
    }

    /* Update Z. */
    gen_compute_eflags(s);
    tcg_gen_deposit_tl(cpu_cc_src, cpu_cc_src, Z, ctz32(CC_Z), 1);
}

static void gen_CPUID(DisasContext *s, X86DecodedInsn *decode)
{
    gen_update_cc_op(s);
    gen_update_eip_cur(s);
    gen_helper_cpuid(tcg_env);
}

static void gen_CRC32(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[2].ot;
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(tmp, s->T0);
    gen_helper_crc32(s->T0, tmp, s->T1, tcg_constant_i32(8 << ot));
}

static void gen_CVTPI2Px(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_enter_mmx(tcg_env);
    if (s->prefix & PREFIX_DATA) {
        gen_helper_cvtpi2pd(tcg_env, OP_PTR0, OP_PTR2);
    } else {
        gen_helper_cvtpi2ps(tcg_env, OP_PTR0, OP_PTR2);
    }
}

static void gen_CVTPx2PI(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_enter_mmx(tcg_env);
    if (s->prefix & PREFIX_DATA) {
        gen_helper_cvtpd2pi(tcg_env, OP_PTR0, OP_PTR2);
    } else {
        gen_helper_cvtps2pi(tcg_env, OP_PTR0, OP_PTR2);
    }
}

static void gen_CVTTPx2PI(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_enter_mmx(tcg_env);
    if (s->prefix & PREFIX_DATA) {
        gen_helper_cvttpd2pi(tcg_env, OP_PTR0, OP_PTR2);
    } else {
        gen_helper_cvttps2pi(tcg_env, OP_PTR0, OP_PTR2);
    }
}

static void gen_CWD(DisasContext *s, X86DecodedInsn *decode)
{
    int shift = 8 << decode->op[0].ot;

    tcg_gen_sextract_tl(s->T0, s->T0, shift - 1, 1);
}

static void gen_DAA(DisasContext *s, X86DecodedInsn *decode)
{
    gen_update_cc_op(s);
    gen_helper_daa(tcg_env);
    assume_cc_op(s, CC_OP_EFLAGS);
}

static void gen_DAS(DisasContext *s, X86DecodedInsn *decode)
{
    gen_update_cc_op(s);
    gen_helper_das(tcg_env);
    assume_cc_op(s, CC_OP_EFLAGS);
}

static void gen_DEC(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;

    tcg_gen_movi_tl(s->T1, -1);
    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_atomic_add_fetch_tl(s->T0, s->A0, s->T1,
                                    s->mem_index, ot | MO_LE);
    } else {
        tcg_gen_add_tl(s->T0, s->T0, s->T1);
    }
    prepare_update_cc_incdec(decode, s, CC_OP_DECB + ot);
}

static void gen_DIV(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;

    switch(ot) {
    case MO_8:
        gen_helper_divb_AL(tcg_env, s->T0);
        break;
    case MO_16:
        gen_helper_divw_AX(tcg_env, s->T0);
        break;
    default:
    case MO_32:
        gen_helper_divl_EAX(tcg_env, s->T0);
        break;
#ifdef TARGET_X86_64
    case MO_64:
        gen_helper_divq_EAX(tcg_env, s->T0);
        break;
#endif
    }
}

static void gen_EMMS(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_emms(tcg_env);
}

static void gen_ENTER(DisasContext *s, X86DecodedInsn *decode)
{
   gen_enter(s, decode->op[1].imm, decode->op[2].imm);
}

static void gen_EXTRQ_i(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 length = tcg_constant_i32(decode->immediate & 63);
    TCGv_i32 index = tcg_constant_i32((decode->immediate >> 8) & 63);

    gen_helper_extrq_i(tcg_env, OP_PTR0, index, length);
}

static void gen_EXTRQ_r(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_extrq_r(tcg_env, OP_PTR0, OP_PTR2);
}

static void gen_FXRSTOR(DisasContext *s, X86DecodedInsn *decode)
{
    if ((s->flags & HF_EM_MASK) || (s->flags & HF_TS_MASK)) {
        gen_NM_exception(s);
    } else {
        gen_helper_fxrstor(tcg_env, s->A0);
    }
}

static void gen_FXSAVE(DisasContext *s, X86DecodedInsn *decode)
{
    if ((s->flags & HF_EM_MASK) || (s->flags & HF_TS_MASK)) {
        gen_NM_exception(s);
    } else {
        gen_helper_fxsave(tcg_env, s->A0);
    }
}

static void gen_HLT(DisasContext *s, X86DecodedInsn *decode)
{
#ifdef CONFIG_SYSTEM_ONLY
    gen_update_cc_op(s);
    gen_update_eip_next(s);
    gen_helper_hlt(tcg_env);
    s->base.is_jmp = DISAS_NORETURN;
#endif
}

static void gen_IDIV(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;

    switch(ot) {
    case MO_8:
        gen_helper_idivb_AL(tcg_env, s->T0);
        break;
    case MO_16:
        gen_helper_idivw_AX(tcg_env, s->T0);
        break;
    default:
    case MO_32:
        gen_helper_idivl_EAX(tcg_env, s->T0);
        break;
#ifdef TARGET_X86_64
    case MO_64:
        gen_helper_idivq_EAX(tcg_env, s->T0);
        break;
#endif
    }
}

static void gen_IMUL3(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    TCGv cc_src_rhs;

    switch (ot) {
    case MO_16:
        /* s->T0 already sign-extended */
        tcg_gen_ext16s_tl(s->T1, s->T1);
        tcg_gen_mul_tl(s->T0, s->T0, s->T1);
        /* Compare the full result to the extension of the truncated result.  */
        tcg_gen_ext16s_tl(s->T1, s->T0);
        cc_src_rhs = s->T0;
        break;

    case MO_32:
#ifdef TARGET_X86_64
        if (TCG_TARGET_REG_BITS == 64) {
            /*
             * This produces fewer TCG ops, and better code if flags are needed,
             * but it requires a 64-bit multiply even if they are not.  Use it
             * only if the target has 64-bits registers.
             *
             * s->T0 is already sign-extended.
             */
            tcg_gen_ext32s_tl(s->T1, s->T1);
            tcg_gen_mul_tl(s->T0, s->T0, s->T1);
            /* Compare the full result to the extension of the truncated result.  */
            tcg_gen_ext32s_tl(s->T1, s->T0);
            cc_src_rhs = s->T0;
        } else {
            /* Variant that only needs a 32-bit widening multiply.  */
            TCGv_i32 hi = tcg_temp_new_i32();
            TCGv_i32 lo = tcg_temp_new_i32();
            tcg_gen_trunc_tl_i32(lo, s->T0);
            tcg_gen_trunc_tl_i32(hi, s->T1);
            tcg_gen_muls2_i32(lo, hi, lo, hi);
            tcg_gen_extu_i32_tl(s->T0, lo);

            cc_src_rhs = tcg_temp_new();
            tcg_gen_extu_i32_tl(cc_src_rhs, hi);
            /* Compare the high part to the sign bit of the truncated result */
            tcg_gen_sari_i32(lo, lo, 31);
            tcg_gen_extu_i32_tl(s->T1, lo);
        }
        break;

    case MO_64:
#endif
        cc_src_rhs = tcg_temp_new();
        tcg_gen_muls2_tl(s->T0, cc_src_rhs, s->T0, s->T1);
        /* Compare the high part to the sign bit of the truncated result */
        tcg_gen_sari_tl(s->T1, s->T0, TARGET_LONG_BITS - 1);
        break;

    default:
        g_assert_not_reached();
    }

    tcg_gen_sub_tl(s->T1, s->T1, cc_src_rhs);
    prepare_update2_cc(decode, s, CC_OP_MULB + ot);
}

static void gen_IMUL(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;
    TCGv cc_src_rhs;

    switch (ot) {
    case MO_8:
        /* s->T0 already sign-extended */
        tcg_gen_ext8s_tl(s->T1, s->T1);
        tcg_gen_mul_tl(s->T0, s->T0, s->T1);
        gen_op_mov_reg_v(s, MO_16, R_EAX, s->T0);
        /* Compare the full result to the extension of the truncated result.  */
        tcg_gen_ext8s_tl(s->T1, s->T0);
        cc_src_rhs = s->T0;
        break;

    case MO_16:
        /* s->T0 already sign-extended */
        tcg_gen_ext16s_tl(s->T1, s->T1);
        tcg_gen_mul_tl(s->T0, s->T0, s->T1);
        gen_op_mov_reg_v(s, MO_16, R_EAX, s->T0);
        tcg_gen_shri_tl(s->T1, s->T0, 16);
        gen_op_mov_reg_v(s, MO_16, R_EDX, s->T1);
        /* Compare the full result to the extension of the truncated result.  */
        tcg_gen_ext16s_tl(s->T1, s->T0);
        cc_src_rhs = s->T0;
        break;

    case MO_32:
#ifdef TARGET_X86_64
        /* s->T0 already sign-extended */
        tcg_gen_ext32s_tl(s->T1, s->T1);
        tcg_gen_mul_tl(s->T0, s->T0, s->T1);
        tcg_gen_ext32u_tl(cpu_regs[R_EAX], s->T0);
        tcg_gen_shri_tl(cpu_regs[R_EDX], s->T0, 32);
        /* Compare the full result to the extension of the truncated result.  */
        tcg_gen_ext32s_tl(s->T1, s->T0);
        cc_src_rhs = s->T0;
        break;

    case MO_64:
#endif
        tcg_gen_muls2_tl(s->T0, cpu_regs[R_EDX], s->T0, s->T1);
        tcg_gen_mov_tl(cpu_regs[R_EAX], s->T0);

        /* Compare the high part to the sign bit of the truncated result */
        tcg_gen_negsetcondi_tl(TCG_COND_LT, s->T1, s->T0, 0);
        cc_src_rhs = cpu_regs[R_EDX];
        break;

    default:
        g_assert_not_reached();
    }

    tcg_gen_sub_tl(s->T1, s->T1, cc_src_rhs);
    prepare_update2_cc(decode, s, CC_OP_MULB + ot);
}

static void gen_IN(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    TCGv_i32 port = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(port, s->T0);
    tcg_gen_ext16u_i32(port, port);
    if (!gen_check_io(s, ot, port, SVM_IOIO_TYPE_MASK)) {
        return;
    }
    translator_io_start(&s->base);
    gen_helper_in_func(ot, s->T0, port);
    gen_writeback(s, decode, 0, s->T0);
    gen_bpt_io(s, port, ot);
}

static void gen_INC(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;

    tcg_gen_movi_tl(s->T1, 1);
    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_atomic_add_fetch_tl(s->T0, s->A0, s->T1,
                                    s->mem_index, ot | MO_LE);
    } else {
        tcg_gen_add_tl(s->T0, s->T0, s->T1);
    }
    prepare_update_cc_incdec(decode, s, CC_OP_INCB + ot);
}

static void gen_INS(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;
    TCGv_i32 port = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(port, s->T1);
    tcg_gen_ext16u_i32(port, port);
    if (!gen_check_io(s, ot, port,
                      SVM_IOIO_TYPE_MASK | SVM_IOIO_STR_MASK)) {
        return;
    }

    translator_io_start(&s->base);
    gen_repz(s, ot, gen_ins);
}

static void gen_INSERTQ_i(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 length = tcg_constant_i32(decode->immediate & 63);
    TCGv_i32 index = tcg_constant_i32((decode->immediate >> 8) & 63);

    gen_helper_insertq_i(tcg_env, OP_PTR0, OP_PTR1, index, length);
}

static void gen_INSERTQ_r(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_insertq_r(tcg_env, OP_PTR0, OP_PTR2);
}

static void gen_INT(DisasContext *s, X86DecodedInsn *decode)
{
    gen_interrupt(s, decode->immediate);
}

static void gen_INT1(DisasContext *s, X86DecodedInsn *decode)
{
    gen_update_cc_op(s);
    gen_update_eip_next(s);
    gen_helper_icebp(tcg_env);
    s->base.is_jmp = DISAS_NORETURN;
}

static void gen_INT3(DisasContext *s, X86DecodedInsn *decode)
{
    gen_interrupt(s, EXCP03_INT3);
}

static void gen_INTO(DisasContext *s, X86DecodedInsn *decode)
{
    gen_update_cc_op(s);
    gen_update_eip_cur(s);
    gen_helper_into(tcg_env, cur_insn_len_i32(s));
}

static void gen_IRET(DisasContext *s, X86DecodedInsn *decode)
{
    if (!PE(s) || VM86(s)) {
        gen_helper_iret_real(tcg_env, tcg_constant_i32(s->dflag - 1));
    } else {
        gen_helper_iret_protected(tcg_env, tcg_constant_i32(s->dflag - 1),
                                  eip_next_i32(s));
    }
    assume_cc_op(s, CC_OP_EFLAGS);
    s->base.is_jmp = DISAS_EOB_ONLY;
}

static void gen_Jcc(DisasContext *s, X86DecodedInsn *decode)
{
    TCGLabel *taken = gen_new_label();

    gen_bnd_jmp(s);
    gen_jcc(s, decode->b & 0xf, taken);
    gen_conditional_jump_labels(s, decode->immediate, NULL, taken);
}

static void gen_JCXZ(DisasContext *s, X86DecodedInsn *decode)
{
    TCGLabel *taken = gen_new_label();

    gen_update_cc_op(s);
    gen_op_jz_ecx(s, taken);
    gen_conditional_jump_labels(s, decode->immediate, NULL, taken);
}

static void gen_JMP(DisasContext *s, X86DecodedInsn *decode)
{
    gen_update_cc_op(s);
    gen_jmp_rel(s, s->dflag, decode->immediate, 0);
}

static void gen_JMP_m(DisasContext *s, X86DecodedInsn *decode)
{
    gen_op_jmp_v(s, s->T0);
    gen_bnd_jmp(s);
    s->base.is_jmp = DISAS_JUMP;
}

static void gen_JMPF(DisasContext *s, X86DecodedInsn *decode)
{
    gen_far_jmp(s);
}

static void gen_JMPF_m(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;

    gen_op_ld_v(s, ot, s->T0, s->A0);
    gen_add_A0_im(s, 1 << ot);
    gen_op_ld_v(s, MO_16, s->T1, s->A0);
    gen_far_jmp(s);
}

static void gen_LAHF(DisasContext *s, X86DecodedInsn *decode)
{
    if (CODE64(s) && !(s->cpuid_ext3_features & CPUID_EXT3_LAHF_LM)) {
        return gen_illegal_opcode(s);
    }
    gen_compute_eflags(s);
    /* Note: gen_compute_eflags() only gives the condition codes */
    tcg_gen_ori_tl(s->T0, cpu_cc_src, 0x02);
    tcg_gen_deposit_tl(cpu_regs[R_EAX], cpu_regs[R_EAX], s->T0, 8, 8);
}

static void gen_LAR(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    TCGv result = tcg_temp_new();
    TCGv dest;

    gen_compute_eflags(s);
    gen_update_cc_op(s);
    gen_helper_lar(result, tcg_env, s->T0);

    /* Perform writeback here to skip it if ZF=0.  */
    decode->op[0].unit = X86_OP_SKIP;
    dest = gen_op_deposit_reg_v(s, ot, decode->op[0].n, result, result);
    tcg_gen_movcond_tl(TCG_COND_TSTNE, dest, cpu_cc_src, tcg_constant_tl(CC_Z),
                       result, dest);
}

static void gen_LDMXCSR(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(tmp, s->T0);
    gen_helper_ldmxcsr(tcg_env, tmp);
}

static void gen_lxx_seg(DisasContext *s, X86DecodedInsn *decode, int seg)
{
    MemOp ot = decode->op[0].ot;

    /* Offset already in s->T0.  */
    gen_add_A0_im(s, 1 << ot);
    gen_op_ld_v(s, MO_16, s->T1, s->A0);

    /* load the segment here to handle exceptions properly */
    gen_movl_seg(s, seg, s->T1);
}

static void gen_LDS(DisasContext *s, X86DecodedInsn *decode)
{
    gen_lxx_seg(s, decode, R_DS);
}

static void gen_LEA(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv ea = gen_lea_modrm_1(s, decode->mem, false);
    gen_lea_v_seg_dest(s, s->aflag, s->T0, ea, -1, -1);
}

static void gen_LEAVE(DisasContext *s, X86DecodedInsn *decode)
{
    gen_leave(s);
}

static void gen_LES(DisasContext *s, X86DecodedInsn *decode)
{
    gen_lxx_seg(s, decode, R_ES);
}

static void gen_LFENCE(DisasContext *s, X86DecodedInsn *decode)
{
    tcg_gen_mb(TCG_MO_LD_LD | TCG_BAR_SC);
}

static void gen_LFS(DisasContext *s, X86DecodedInsn *decode)
{
    gen_lxx_seg(s, decode, R_FS);
}

static void gen_LGS(DisasContext *s, X86DecodedInsn *decode)
{
    gen_lxx_seg(s, decode, R_GS);
}

static void gen_LODS(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;
    gen_repz(s, ot, gen_lods);
}

static void gen_LOOP(DisasContext *s, X86DecodedInsn *decode)
{
    TCGLabel *taken = gen_new_label();

    gen_update_cc_op(s);
    gen_op_add_reg_im(s, s->aflag, R_ECX, -1);
    gen_op_jnz_ecx(s, taken);
    gen_conditional_jump_labels(s, decode->immediate, NULL, taken);
}

static void gen_LOOPE(DisasContext *s, X86DecodedInsn *decode)
{
    TCGLabel *taken = gen_new_label();
    TCGLabel *not_taken = gen_new_label();

    gen_update_cc_op(s);
    gen_op_add_reg_im(s, s->aflag, R_ECX, -1);
    gen_op_jz_ecx(s, not_taken);
    gen_jcc(s, (JCC_Z << 1), taken); /* jz taken */
    gen_conditional_jump_labels(s, decode->immediate, not_taken, taken);
}

static void gen_LOOPNE(DisasContext *s, X86DecodedInsn *decode)
{
    TCGLabel *taken = gen_new_label();
    TCGLabel *not_taken = gen_new_label();

    gen_update_cc_op(s);
    gen_op_add_reg_im(s, s->aflag, R_ECX, -1);
    gen_op_jz_ecx(s, not_taken);
    gen_jcc(s, (JCC_Z << 1) | 1, taken); /* jnz taken */
    gen_conditional_jump_labels(s, decode->immediate, not_taken, taken);
}

static void gen_LSL(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    TCGv result = tcg_temp_new();
    TCGv dest;

    gen_compute_eflags(s);
    gen_update_cc_op(s);
    gen_helper_lsl(result, tcg_env, s->T0);

    /* Perform writeback here to skip it if ZF=0.  */
    decode->op[0].unit = X86_OP_SKIP;
    dest = gen_op_deposit_reg_v(s, ot, decode->op[0].n, result, result);
    tcg_gen_movcond_tl(TCG_COND_TSTNE, dest, cpu_cc_src, tcg_constant_tl(CC_Z),
                       result, dest);
}

static void gen_LSS(DisasContext *s, X86DecodedInsn *decode)
{
    gen_lxx_seg(s, decode, R_SS);
}

static void gen_LZCNT(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;

    /* C bit (cc_src) is defined related to the input.  */
    decode->cc_src = tcg_temp_new();
    decode->cc_dst = s->T0;
    decode->cc_op = CC_OP_BMILGB + ot;
    tcg_gen_mov_tl(decode->cc_src, s->T0);

    /*
     * Reduce the target_ulong result by the number of zeros that
     * we expect to find at the top.
     */
    tcg_gen_clzi_tl(s->T0, s->T0, TARGET_LONG_BITS);
    tcg_gen_subi_tl(s->T0, s->T0, TARGET_LONG_BITS - (8 << ot));
}

static void gen_MFENCE(DisasContext *s, X86DecodedInsn *decode)
{
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
}

static void gen_MOV(DisasContext *s, X86DecodedInsn *decode)
{
    /* nothing to do! */
}
#define gen_NOP gen_MOV

static void gen_MASKMOV(DisasContext *s, X86DecodedInsn *decode)
{
    gen_lea_v_seg(s, cpu_regs[R_EDI], R_DS, s->override);

    if (s->prefix & PREFIX_DATA) {
        gen_helper_maskmov_xmm(tcg_env, OP_PTR1, OP_PTR2, s->A0);
    } else {
        gen_helper_maskmov_mmx(tcg_env, OP_PTR1, OP_PTR2, s->A0);
    }
}

static void gen_MOVBE(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;

    /* M operand type does not load/store */
    if (decode->e.op0 == X86_TYPE_M) {
        tcg_gen_qemu_st_tl(s->T0, s->A0, s->mem_index, ot | MO_BE);
    } else {
        tcg_gen_qemu_ld_tl(s->T0, s->A0, s->mem_index, ot | MO_BE);
    }
}

static void gen_MOVD_from(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[2].ot;

    switch (ot) {
    case MO_32:
#ifdef TARGET_X86_64
        tcg_gen_ld32u_tl(s->T0, tcg_env, decode->op[2].offset);
        break;
    case MO_64:
#endif
        tcg_gen_ld_tl(s->T0, tcg_env, decode->op[2].offset);
        break;
    default:
        abort();
    }
}

static void gen_MOVD_to(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[2].ot;
    int vec_len = vector_len(s, decode);
    int lo_ofs = vector_elem_offset(&decode->op[0], ot, 0);

    tcg_gen_gvec_dup_imm(MO_64, decode->op[0].offset, vec_len, vec_len, 0);

    switch (ot) {
    case MO_32:
#ifdef TARGET_X86_64
        tcg_gen_st32_tl(s->T1, tcg_env, lo_ofs);
        break;
    case MO_64:
#endif
        tcg_gen_st_tl(s->T1, tcg_env, lo_ofs);
        break;
    default:
        g_assert_not_reached();
    }
}

static void gen_MOVDQ(DisasContext *s, X86DecodedInsn *decode)
{
    gen_store_sse(s, decode, decode->op[2].offset);
}

static void gen_MOVMSK(DisasContext *s, X86DecodedInsn *decode)
{
    typeof(gen_helper_movmskps_ymm) *ps, *pd, *fn;
    TCGv_i32 tmp = tcg_temp_new_i32();

    ps = s->vex_l ? gen_helper_movmskps_ymm : gen_helper_movmskps_xmm;
    pd = s->vex_l ? gen_helper_movmskpd_ymm : gen_helper_movmskpd_xmm;
    fn = s->prefix & PREFIX_DATA ? pd : ps;
    fn(tmp, tcg_env, OP_PTR2);
    tcg_gen_extu_i32_tl(s->T0, tmp);
}

static void gen_MOVQ(DisasContext *s, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);
    int lo_ofs = vector_elem_offset(&decode->op[0], MO_64, 0);

    tcg_gen_ld_i64(s->tmp1_i64, tcg_env, decode->op[2].offset);
    if (decode->op[0].has_ea) {
        tcg_gen_qemu_st_i64(s->tmp1_i64, s->A0, s->mem_index, MO_LEUQ);
    } else {
        /*
         * tcg_gen_gvec_dup_i64(MO_64, op0.offset, 8, vec_len, s->tmp1_64) would
         * seem to work, but it does not on big-endian platforms; the cleared parts
         * are always at higher addresses, but cross-endian emulation inverts the
         * byte order so that the cleared parts need to be at *lower* addresses.
         * Because oprsz is 8, we see this here even for SSE; but more in general,
         * it disqualifies using oprsz < maxsz to emulate VEX128.
         */
        tcg_gen_gvec_dup_imm(MO_64, decode->op[0].offset, vec_len, vec_len, 0);
        tcg_gen_st_i64(s->tmp1_i64, tcg_env, lo_ofs);
    }
}

static void gen_MOVq_dq(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_enter_mmx(tcg_env);
    /* Otherwise the same as any other movq.  */
    return gen_MOVQ(s, decode);
}

static void gen_MOVS(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[2].ot;
    gen_repz(s, ot, gen_movs);
}

static void gen_MUL(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;

    switch (ot) {
    case MO_8:
        /* s->T0 already zero-extended */
        tcg_gen_ext8u_tl(s->T1, s->T1);
        tcg_gen_mul_tl(s->T0, s->T0, s->T1);
        gen_op_mov_reg_v(s, MO_16, R_EAX, s->T0);
        tcg_gen_andi_tl(s->T1, s->T0, 0xff00);
        decode->cc_dst = s->T0;
        decode->cc_src = s->T1;
        break;

    case MO_16:
        /* s->T0 already zero-extended */
        tcg_gen_ext16u_tl(s->T1, s->T1);
        tcg_gen_mul_tl(s->T0, s->T0, s->T1);
        gen_op_mov_reg_v(s, MO_16, R_EAX, s->T0);
        tcg_gen_shri_tl(s->T1, s->T0, 16);
        gen_op_mov_reg_v(s, MO_16, R_EDX, s->T1);
        decode->cc_dst = s->T0;
        decode->cc_src = s->T1;
        break;

    case MO_32:
#ifdef TARGET_X86_64
        /* s->T0 already zero-extended */
        tcg_gen_ext32u_tl(s->T1, s->T1);
        tcg_gen_mul_tl(s->T0, s->T0, s->T1);
        tcg_gen_ext32u_tl(cpu_regs[R_EAX], s->T0);
        tcg_gen_shri_tl(cpu_regs[R_EDX], s->T0, 32);
        decode->cc_dst = cpu_regs[R_EAX];
        decode->cc_src = cpu_regs[R_EDX];
        break;

    case MO_64:
#endif
        tcg_gen_mulu2_tl(cpu_regs[R_EAX], cpu_regs[R_EDX], s->T0, s->T1);
        decode->cc_dst = cpu_regs[R_EAX];
        decode->cc_src = cpu_regs[R_EDX];
        break;

    default:
        g_assert_not_reached();
    }

    decode->cc_op = CC_OP_MULB + ot;
}

static void gen_MULX(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;

    /* low part of result in VEX.vvvv, high in MODRM */
    switch (ot) {
    case MO_32:
#ifdef TARGET_X86_64
        {
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i32 t1 = tcg_temp_new_i32();

            tcg_gen_trunc_tl_i32(t0, s->T0);
            tcg_gen_trunc_tl_i32(t1, s->T1);
            tcg_gen_mulu2_i32(t0, t1, t0, t1);
            tcg_gen_extu_i32_tl(cpu_regs[s->vex_v], t0);
            tcg_gen_extu_i32_tl(s->T0, t1);
            break;
        }

    case MO_64:
#endif
        tcg_gen_mulu2_tl(cpu_regs[s->vex_v], s->T0, s->T0, s->T1);
        break;

    default:
        g_assert_not_reached();
    }
}

static void gen_NEG(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    TCGv oldv = tcg_temp_new();

    if (s->prefix & PREFIX_LOCK) {
        TCGv newv = tcg_temp_new();
        TCGv cmpv = tcg_temp_new();
        TCGLabel *label1 = gen_new_label();

        gen_set_label(label1);
        gen_op_ld_v(s, ot, oldv, s->A0);
        tcg_gen_neg_tl(newv, oldv);
        tcg_gen_atomic_cmpxchg_tl(cmpv, s->A0, oldv, newv,
                                  s->mem_index, ot | MO_LE);
        tcg_gen_brcond_tl(TCG_COND_NE, oldv, cmpv, label1);
    } else {
        tcg_gen_mov_tl(oldv, s->T0);
    }
    tcg_gen_neg_tl(s->T0, oldv);

    decode->cc_dst = s->T0;
    decode->cc_src = oldv;
    tcg_gen_movi_tl(s->cc_srcT, 0);
    decode->cc_op = CC_OP_SUBB + ot;
}

static void gen_NOT(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;

    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_movi_tl(s->T0, ~0);
        tcg_gen_atomic_xor_fetch_tl(s->T0, s->A0, s->T0,
                                    s->mem_index, ot | MO_LE);
    } else {
        tcg_gen_not_tl(s->T0, s->T0);
    }
}

static void gen_OR(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;

    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_atomic_or_fetch_tl(s->T0, s->A0, s->T1,
                                   s->mem_index, ot | MO_LE);
    } else {
        tcg_gen_or_tl(s->T0, s->T0, s->T1);
    }
    prepare_update1_cc(decode, s, CC_OP_LOGICB + ot);
}

static void gen_OUT(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;
    TCGv_i32 port = tcg_temp_new_i32();
    TCGv_i32 value = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(port, s->T1);
    tcg_gen_ext16u_i32(port, port);
    if (!gen_check_io(s, ot, port, 0)) {
        return;
    }
    tcg_gen_trunc_tl_i32(value, s->T0);
    translator_io_start(&s->base);
    gen_helper_out_func(ot, port, value);
    gen_bpt_io(s, port, ot);
}

static void gen_OUTS(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;
    TCGv_i32 port = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(port, s->T1);
    tcg_gen_ext16u_i32(port, port);
    if (!gen_check_io(s, ot, port, SVM_IOIO_STR_MASK)) {
        return;
    }

    translator_io_start(&s->base);
    gen_repz(s, ot, gen_outs);
}

static void gen_PALIGNR(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    if (!(s->prefix & PREFIX_DATA)) {
        gen_helper_palignr_mmx(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
    } else if (!s->vex_l) {
        gen_helper_palignr_xmm(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
    } else {
        gen_helper_palignr_ymm(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
    }
}

static void gen_PANDN(DisasContext *s, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    /* Careful, operand order is reversed!  */
    tcg_gen_gvec_andc(MO_64,
                      decode->op[0].offset, decode->op[2].offset,
                      decode->op[1].offset, vec_len, vec_len);
}

static void gen_PAUSE(DisasContext *s, X86DecodedInsn *decode)
{
    gen_update_cc_op(s);
    gen_update_eip_next(s);
    gen_helper_pause(tcg_env);
    s->base.is_jmp = DISAS_NORETURN;
}

static void gen_PCMPESTRI(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    gen_helper_pcmpestri_xmm(tcg_env, OP_PTR1, OP_PTR2, imm);
    assume_cc_op(s, CC_OP_EFLAGS);
}

static void gen_PCMPESTRM(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    gen_helper_pcmpestrm_xmm(tcg_env, OP_PTR1, OP_PTR2, imm);
    assume_cc_op(s, CC_OP_EFLAGS);
    if ((s->prefix & PREFIX_VEX) && !s->vex_l) {
        tcg_gen_gvec_dup_imm(MO_64, offsetof(CPUX86State, xmm_regs[0].ZMM_X(1)),
                             16, 16, 0);
    }
}

static void gen_PCMPISTRI(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    gen_helper_pcmpistri_xmm(tcg_env, OP_PTR1, OP_PTR2, imm);
    assume_cc_op(s, CC_OP_EFLAGS);
}

static void gen_PCMPISTRM(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    gen_helper_pcmpistrm_xmm(tcg_env, OP_PTR1, OP_PTR2, imm);
    assume_cc_op(s, CC_OP_EFLAGS);
    if ((s->prefix & PREFIX_VEX) && !s->vex_l) {
        tcg_gen_gvec_dup_imm(MO_64, offsetof(CPUX86State, xmm_regs[0].ZMM_X(1)),
                             16, 16, 0);
    }
}

static void gen_PDEP(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_pdep(s->T0, s->T0, s->T1);
}

static void gen_PEXT(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_pext(s->T0, s->T0, s->T1);
}

static inline void gen_pextr(DisasContext *s, X86DecodedInsn *decode, MemOp ot)
{
    int vec_len = vector_len(s, decode);
    int mask = (vec_len >> ot) - 1;
    int val = decode->immediate & mask;

    switch (ot) {
    case MO_8:
        tcg_gen_ld8u_tl(s->T0, tcg_env, vector_elem_offset(&decode->op[1], ot, val));
        break;
    case MO_16:
        tcg_gen_ld16u_tl(s->T0, tcg_env, vector_elem_offset(&decode->op[1], ot, val));
        break;
    case MO_32:
#ifdef TARGET_X86_64
        tcg_gen_ld32u_tl(s->T0, tcg_env, vector_elem_offset(&decode->op[1], ot, val));
        break;
    case MO_64:
#endif
        tcg_gen_ld_tl(s->T0, tcg_env, vector_elem_offset(&decode->op[1], ot, val));
        break;
    default:
        abort();
    }
}

static void gen_PEXTRB(DisasContext *s, X86DecodedInsn *decode)
{
    gen_pextr(s, decode, MO_8);
}

static void gen_PEXTRW(DisasContext *s, X86DecodedInsn *decode)
{
    gen_pextr(s, decode, MO_16);
}

static void gen_PEXTR(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    gen_pextr(s, decode, ot);
}

static inline void gen_pinsr(DisasContext *s, X86DecodedInsn *decode, MemOp ot)
{
    int vec_len = vector_len(s, decode);
    int mask = (vec_len >> ot) - 1;
    int val = decode->immediate & mask;

    if (decode->op[1].offset != decode->op[0].offset) {
        assert(vec_len == 16);
        gen_store_sse(s, decode, decode->op[1].offset);
    }

    switch (ot) {
    case MO_8:
        tcg_gen_st8_tl(s->T1, tcg_env, vector_elem_offset(&decode->op[0], ot, val));
        break;
    case MO_16:
        tcg_gen_st16_tl(s->T1, tcg_env, vector_elem_offset(&decode->op[0], ot, val));
        break;
    case MO_32:
#ifdef TARGET_X86_64
        tcg_gen_st32_tl(s->T1, tcg_env, vector_elem_offset(&decode->op[0], ot, val));
        break;
    case MO_64:
#endif
        tcg_gen_st_tl(s->T1, tcg_env, vector_elem_offset(&decode->op[0], ot, val));
        break;
    default:
        abort();
    }
}

static void gen_PINSRB(DisasContext *s, X86DecodedInsn *decode)
{
    gen_pinsr(s, decode, MO_8);
}

static void gen_PINSRW(DisasContext *s, X86DecodedInsn *decode)
{
    gen_pinsr(s, decode, MO_16);
}

static void gen_PINSR(DisasContext *s, X86DecodedInsn *decode)
{
    gen_pinsr(s, decode, decode->op[2].ot);
}

static void gen_pmovmskb_i64(TCGv_i64 d, TCGv_i64 s)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_andi_i64(d, s, 0x8080808080808080ull);

    /*
     * After each shift+or pair:
     * 0:  a.......b.......c.......d.......e.......f.......g.......h.......
     * 7:  ab......bc......cd......de......ef......fg......gh......h.......
     * 14: abcd....bcde....cdef....defg....efgh....fgh.....gh......h.......
     * 28: abcdefghbcdefgh.cdefgh..defgh...efgh....fgh.....gh......h.......
     * The result is left in the high bits of the word.
     */
    tcg_gen_shli_i64(t, d, 7);
    tcg_gen_or_i64(d, d, t);
    tcg_gen_shli_i64(t, d, 14);
    tcg_gen_or_i64(d, d, t);
    tcg_gen_shli_i64(t, d, 28);
    tcg_gen_or_i64(d, d, t);
}

static void gen_pmovmskb_vec(unsigned vece, TCGv_vec d, TCGv_vec s)
{
    TCGv_vec t = tcg_temp_new_vec_matching(d);
    TCGv_vec m = tcg_constant_vec_matching(d, MO_8, 0x80);

    /* See above */
    tcg_gen_and_vec(vece, d, s, m);
    tcg_gen_shli_vec(vece, t, d, 7);
    tcg_gen_or_vec(vece, d, d, t);
    tcg_gen_shli_vec(vece, t, d, 14);
    tcg_gen_or_vec(vece, d, d, t);
    tcg_gen_shli_vec(vece, t, d, 28);
    tcg_gen_or_vec(vece, d, d, t);
}

static void gen_PMOVMSKB(DisasContext *s, X86DecodedInsn *decode)
{
    static const TCGOpcode vecop_list[] = { INDEX_op_shli_vec, 0 };
    static const GVecGen2 g = {
        .fni8 = gen_pmovmskb_i64,
        .fniv = gen_pmovmskb_vec,
        .opt_opc = vecop_list,
        .vece = MO_64,
        .prefer_i64 = TCG_TARGET_REG_BITS == 64
    };
    MemOp ot = decode->op[2].ot;
    int vec_len = vector_len(s, decode);
    TCGv t = tcg_temp_new();

    tcg_gen_gvec_2(offsetof(CPUX86State, xmm_t0) + xmm_offset(ot), decode->op[2].offset,
                   vec_len, vec_len, &g);
    tcg_gen_ld8u_tl(s->T0, tcg_env, offsetof(CPUX86State, xmm_t0.ZMM_B(vec_len - 1)));
    while (vec_len > 8) {
        vec_len -= 8;
        if (tcg_op_supported(INDEX_op_extract2_tl, TCG_TYPE_TL, 0)) {
            /*
             * Load the next byte of the result into the high byte of T.
             * TCG does a similar expansion of deposit to shl+extract2; by
             * loading the whole word, the shift left is avoided.
             */
#ifdef TARGET_X86_64
            tcg_gen_ld_tl(t, tcg_env, offsetof(CPUX86State, xmm_t0.ZMM_Q((vec_len - 1) / 8)));
#else
            tcg_gen_ld_tl(t, tcg_env, offsetof(CPUX86State, xmm_t0.ZMM_L((vec_len - 1) / 4)));
#endif

            tcg_gen_extract2_tl(s->T0, t, s->T0, TARGET_LONG_BITS - 8);
        } else {
            /*
             * The _previous_ value is deposited into bits 8 and higher of t.  Because
             * those bits are known to be zero after ld8u, this becomes a shift+or
             * if deposit is not available.
             */
            tcg_gen_ld8u_tl(t, tcg_env, offsetof(CPUX86State, xmm_t0.ZMM_B(vec_len - 1)));
            tcg_gen_deposit_tl(s->T0, t, s->T0, 8, TARGET_LONG_BITS - 8);
        }
    }
}

static void gen_POP(DisasContext *s, X86DecodedInsn *decode)
{
    X86DecodedOp *op = &decode->op[0];
    MemOp ot = gen_pop_T0(s);

    assert(ot >= op->ot);
    if (op->has_ea || op->unit == X86_OP_SEG) {
        /* NOTE: order is important for MMU exceptions */
        gen_writeback(s, decode, 0, s->T0);
    }

    /* NOTE: writing back registers after update is important for pop %sp */
    gen_pop_update(s, ot);
}

static void gen_POPA(DisasContext *s, X86DecodedInsn *decode)
{
    gen_popa(s);
}

static void gen_POPCNT(DisasContext *s, X86DecodedInsn *decode)
{
    decode->cc_dst = tcg_temp_new();
    decode->cc_op = CC_OP_POPCNT;

    tcg_gen_mov_tl(decode->cc_dst, s->T0);
    tcg_gen_ctpop_tl(s->T0, s->T0);
}

static void gen_POPF(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot;
    int mask = TF_MASK | AC_MASK | ID_MASK | NT_MASK;

    if (CPL(s) == 0) {
        mask |= IF_MASK | IOPL_MASK;
    } else if (CPL(s) <= IOPL(s)) {
        mask |= IF_MASK;
    }
    if (s->dflag == MO_16) {
        mask &= 0xffff;
    }

    ot = gen_pop_T0(s);
    gen_helper_write_eflags(tcg_env, s->T0, tcg_constant_i32(mask));
    gen_pop_update(s, ot);
    set_cc_op(s, CC_OP_EFLAGS);
    /* abort translation because TF/AC flag may change */
    s->base.is_jmp = DISAS_EOB_NEXT;
}

static void gen_PSHUFW(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    gen_helper_pshufw_mmx(OP_PTR0, OP_PTR1, imm);
}

static void gen_PSRLW_i(DisasContext *s, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 16) {
        tcg_gen_gvec_dup_imm(MO_64, decode->op[0].offset, vec_len, vec_len, 0);
    } else {
        tcg_gen_gvec_shri(MO_16,
                          decode->op[0].offset, decode->op[1].offset,
                          decode->immediate, vec_len, vec_len);
    }
}

static void gen_PSLLW_i(DisasContext *s, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 16) {
        tcg_gen_gvec_dup_imm(MO_64, decode->op[0].offset, vec_len, vec_len, 0);
    } else {
        tcg_gen_gvec_shli(MO_16,
                          decode->op[0].offset, decode->op[1].offset,
                          decode->immediate, vec_len, vec_len);
    }
}

static void gen_PSRAW_i(DisasContext *s, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 16) {
        decode->immediate = 15;
    }
    tcg_gen_gvec_sari(MO_16,
                      decode->op[0].offset, decode->op[1].offset,
                      decode->immediate, vec_len, vec_len);
}

static void gen_PSRLD_i(DisasContext *s, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 32) {
        tcg_gen_gvec_dup_imm(MO_64, decode->op[0].offset, vec_len, vec_len, 0);
    } else {
        tcg_gen_gvec_shri(MO_32,
                          decode->op[0].offset, decode->op[1].offset,
                          decode->immediate, vec_len, vec_len);
    }
}

static void gen_PSLLD_i(DisasContext *s, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 32) {
        tcg_gen_gvec_dup_imm(MO_64, decode->op[0].offset, vec_len, vec_len, 0);
    } else {
        tcg_gen_gvec_shli(MO_32,
                          decode->op[0].offset, decode->op[1].offset,
                          decode->immediate, vec_len, vec_len);
    }
}

static void gen_PSRAD_i(DisasContext *s, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 32) {
        decode->immediate = 31;
    }
    tcg_gen_gvec_sari(MO_32,
                      decode->op[0].offset, decode->op[1].offset,
                      decode->immediate, vec_len, vec_len);
}

static void gen_PSRLQ_i(DisasContext *s, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 64) {
        tcg_gen_gvec_dup_imm(MO_64, decode->op[0].offset, vec_len, vec_len, 0);
    } else {
        tcg_gen_gvec_shri(MO_64,
                          decode->op[0].offset, decode->op[1].offset,
                          decode->immediate, vec_len, vec_len);
    }
}

static void gen_PSLLQ_i(DisasContext *s, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 64) {
        tcg_gen_gvec_dup_imm(MO_64, decode->op[0].offset, vec_len, vec_len, 0);
    } else {
        tcg_gen_gvec_shli(MO_64,
                          decode->op[0].offset, decode->op[1].offset,
                          decode->immediate, vec_len, vec_len);
    }
}

static TCGv_ptr make_imm8u_xmm_vec(uint8_t imm, int vec_len)
{
    MemOp ot = vec_len == 16 ? MO_128 : MO_256;
    TCGv_i32 imm_v = tcg_constant8u_i32(imm);
    TCGv_ptr ptr = tcg_temp_new_ptr();

    tcg_gen_gvec_dup_imm(MO_64, offsetof(CPUX86State, xmm_t0) + xmm_offset(ot),
                         vec_len, vec_len, 0);

    tcg_gen_addi_ptr(ptr, tcg_env, offsetof(CPUX86State, xmm_t0));
    tcg_gen_st_i32(imm_v, tcg_env, offsetof(CPUX86State, xmm_t0.ZMM_L(0)));
    return ptr;
}

static void gen_PSRLDQ_i(DisasContext *s, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);
    TCGv_ptr imm_vec = make_imm8u_xmm_vec(decode->immediate, vec_len);

    if (s->vex_l) {
        gen_helper_psrldq_ymm(tcg_env, OP_PTR0, OP_PTR1, imm_vec);
    } else {
        gen_helper_psrldq_xmm(tcg_env, OP_PTR0, OP_PTR1, imm_vec);
    }
}

static void gen_PSLLDQ_i(DisasContext *s, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);
    TCGv_ptr imm_vec = make_imm8u_xmm_vec(decode->immediate, vec_len);

    if (s->vex_l) {
        gen_helper_pslldq_ymm(tcg_env, OP_PTR0, OP_PTR1, imm_vec);
    } else {
        gen_helper_pslldq_xmm(tcg_env, OP_PTR0, OP_PTR1, imm_vec);
    }
}

static void gen_PUSH(DisasContext *s, X86DecodedInsn *decode)
{
    gen_push_v(s, s->T0);
}

static void gen_PUSHA(DisasContext *s, X86DecodedInsn *decode)
{
    gen_pusha(s);
}

static void gen_PUSHF(DisasContext *s, X86DecodedInsn *decode)
{
    gen_update_cc_op(s);
    gen_helper_read_eflags(s->T0, tcg_env);
    gen_push_v(s, s->T0);
}

static MemOp gen_shift_count(DisasContext *s, X86DecodedInsn *decode,
                             bool *can_be_zero, TCGv *count, int unit)
{
    MemOp ot = decode->op[0].ot;
    int mask = (ot <= MO_32 ? 0x1f : 0x3f);

    *can_be_zero = false;
    switch (unit) {
    case X86_OP_INT:
        *count = tcg_temp_new();
        tcg_gen_andi_tl(*count, cpu_regs[R_ECX], mask);
        *can_be_zero = true;
        break;

    case X86_OP_IMM:
        if ((decode->immediate & mask) == 0) {
            *count = NULL;
            break;
        }
        *count = tcg_temp_new();
        tcg_gen_movi_tl(*count, decode->immediate & mask);
        break;

    case X86_OP_SKIP:
        *count = tcg_temp_new();
        tcg_gen_movi_tl(*count, 1);
        break;

    default:
        g_assert_not_reached();
    }

    return ot;
}

/*
 * Compute existing flags in decode->cc_src, for gen_* functions that wants
 * to set the cc_op set to CC_OP_ADCOX.  In particular, this allows rotate
 * operations to compute the carry in decode->cc_dst and the overflow in
 * decode->cc_src2.
 *
 * If need_flags is true, decode->cc_dst and decode->cc_src2 are preloaded
 * with the value of CF and OF before the instruction, so that it is possible
 * to keep the flags unmodified.
 *
 * Return true if carry could be made available cheaply as a 1-bit value in
 * decode->cc_dst (trying a bit harder if want_carry is true).  If false is
 * returned, decode->cc_dst is uninitialized and the carry is only available
 * as bit 0 of decode->cc_src.
 */
static bool gen_eflags_adcox(DisasContext *s, X86DecodedInsn *decode, bool want_carry, bool need_flags)
{
    bool got_cf = false;
    bool got_of = false;

    decode->cc_dst = tcg_temp_new();
    decode->cc_src = tcg_temp_new();
    decode->cc_src2 = tcg_temp_new();
    decode->cc_op = CC_OP_ADCOX;

    /* A lot more cc_ops could be "optimized" to avoid the extracts at
     * the end (INC/DEC, BMILG, MUL), but they are all really unlikely
     * to be followed by rotations within the same basic block.
     */
    switch (s->cc_op) {
    case CC_OP_ADCOX:
        /* No need to compute the full EFLAGS, CF/OF are already isolated.  */
        tcg_gen_mov_tl(decode->cc_src, cpu_cc_src);
        if (need_flags) {
            tcg_gen_mov_tl(decode->cc_src2, cpu_cc_src2);
            got_of = true;
        }
        if (want_carry || need_flags) {
            tcg_gen_mov_tl(decode->cc_dst, cpu_cc_dst);
            got_cf = true;
        }
        break;

    case CC_OP_LOGICB ... CC_OP_LOGICQ:
        /* CF and OF are zero, do it just because it's easy.  */
        gen_mov_eflags(s, decode->cc_src);
        if (need_flags) {
            tcg_gen_movi_tl(decode->cc_src2, 0);
            got_of = true;
        }
        if (want_carry || need_flags) {
            tcg_gen_movi_tl(decode->cc_dst, 0);
            got_cf = true;
        }
        break;

    case CC_OP_SARB ... CC_OP_SARQ:
        /*
         * SHR/RCR/SHR/RCR/... is a relatively common occurrence of RCR.
         * By computing CF without using eflags, the calls to cc_compute_all
         * can be eliminated as dead code (except for the last RCR).
         */
        if (want_carry || need_flags) {
            tcg_gen_andi_tl(decode->cc_dst, cpu_cc_src, 1);
            got_cf = true;
        }
        gen_mov_eflags(s, decode->cc_src);
        break;

    case CC_OP_SHLB ... CC_OP_SHLQ:
        /*
         * Likewise for SHL/RCL/SHL/RCL/... but, if CF is not in the sign
         * bit, we might as well fish CF out of EFLAGS and save a shift.
         */
        if (want_carry && (!need_flags || s->cc_op == CC_OP_SHLB + MO_TL)) {
            MemOp size = cc_op_size(s->cc_op);
            tcg_gen_shri_tl(decode->cc_dst, cpu_cc_src, (8 << size) - 1);
            got_cf = true;
        }
        gen_mov_eflags(s, decode->cc_src);
        break;

    default:
        gen_mov_eflags(s, decode->cc_src);
        break;
    }

    if (need_flags) {
        /* If the flags could be left unmodified, always load them.  */
        if (!got_of) {
            tcg_gen_extract_tl(decode->cc_src2, decode->cc_src, ctz32(CC_O), 1);
            got_of = true;
        }
        if (!got_cf) {
            tcg_gen_extract_tl(decode->cc_dst, decode->cc_src, ctz32(CC_C), 1);
            got_cf = true;
        }
    }
    return got_cf;
}

static void gen_rot_overflow(X86DecodedInsn *decode, TCGv result, TCGv old,
                             bool can_be_zero, TCGv count)
{
    MemOp ot = decode->op[0].ot;
    TCGv temp = can_be_zero ? tcg_temp_new() : decode->cc_src2;

    tcg_gen_xor_tl(temp, old, result);
    tcg_gen_extract_tl(temp, temp, (8 << ot) - 1, 1);
    if (can_be_zero) {
        tcg_gen_movcond_tl(TCG_COND_EQ, decode->cc_src2, count, tcg_constant_tl(0),
                           decode->cc_src2, temp);
    }
}

/*
 * RCx operations are invariant modulo 8*operand_size+1.  For 8 and 16-bit operands,
 * this is less than 0x1f (the mask applied by gen_shift_count) so reduce further.
 */
static void gen_rotc_mod(MemOp ot, TCGv count)
{
    TCGv temp;

    switch (ot) {
    case MO_8:
        temp = tcg_temp_new();
        tcg_gen_subi_tl(temp, count, 18);
        tcg_gen_movcond_tl(TCG_COND_GE, count, temp, tcg_constant_tl(0), temp, count);
        tcg_gen_subi_tl(temp, count, 9);
        tcg_gen_movcond_tl(TCG_COND_GE, count, temp, tcg_constant_tl(0), temp, count);
        break;

    case MO_16:
        temp = tcg_temp_new();
        tcg_gen_subi_tl(temp, count, 17);
        tcg_gen_movcond_tl(TCG_COND_GE, count, temp, tcg_constant_tl(0), temp, count);
        break;

    default:
        break;
    }
}

/*
 * The idea here is that the bit to the right of the new bit 0 is the
 * new carry, and the bit to the right of the old bit 0 is the old carry.
 * Just like a regular rotation, the result of the rotation is composed
 * from a right shifted part and a left shifted part of s->T0.  The new carry
 * is extracted from the right-shifted portion, and the old carry is
 * inserted at the end of the left-shifted portion.
 *
 * Because of the separate shifts involving the carry, gen_RCL and gen_RCR
 * mostly operate on count-1.  This also comes in handy when computing
 * length - count, because (length-1) - (count-1) can be computed with
 * a XOR, and that is commutative unlike subtraction.
 */
static void gen_RCL(DisasContext *s, X86DecodedInsn *decode)
{
    bool have_1bit_cin, can_be_zero;
    TCGv count;
    TCGLabel *zero_label = NULL;
    MemOp ot = gen_shift_count(s, decode, &can_be_zero, &count, decode->op[2].unit);
    TCGv low, high, low_count;

    if (!count) {
        return;
    }

    low = tcg_temp_new();
    high = tcg_temp_new();
    low_count = tcg_temp_new();

    gen_rotc_mod(ot, count);
    have_1bit_cin = gen_eflags_adcox(s, decode, true, can_be_zero);
    if (can_be_zero) {
        zero_label = gen_new_label();
        tcg_gen_brcondi_tl(TCG_COND_EQ, count, 0, zero_label);
    }

    /* Compute high part, including incoming carry.  */
    if (!have_1bit_cin || tcg_op_deposit_valid(TCG_TYPE_TL, 1, TARGET_LONG_BITS - 1)) {
        /* high = (T0 << 1) | cin */
        TCGv cin = have_1bit_cin ? decode->cc_dst : decode->cc_src;
        tcg_gen_deposit_tl(high, cin, s->T0, 1, TARGET_LONG_BITS - 1);
    } else {
        /* Same as above but without deposit; cin in cc_dst.  */
        tcg_gen_add_tl(high, s->T0, decode->cc_dst);
        tcg_gen_add_tl(high, high, s->T0);
    }
    tcg_gen_subi_tl(count, count, 1);
    tcg_gen_shl_tl(high, high, count);

    /* Compute low part and outgoing carry, incoming s->T0 is zero extended */
    tcg_gen_xori_tl(low_count, count, (8 << ot) - 1); /* LENGTH - 1 - (count - 1) */
    tcg_gen_shr_tl(low, s->T0, low_count);
    tcg_gen_andi_tl(decode->cc_dst, low, 1);
    tcg_gen_shri_tl(low, low, 1);

    /* Compute result and outgoing overflow */
    tcg_gen_mov_tl(decode->cc_src2, s->T0);
    tcg_gen_or_tl(s->T0, low, high);
    gen_rot_overflow(decode, s->T0, decode->cc_src2, false, NULL);

    if (zero_label) {
        gen_set_label(zero_label);
    }
}

static void gen_RCR(DisasContext *s, X86DecodedInsn *decode)
{
    bool have_1bit_cin, can_be_zero;
    TCGv count;
    TCGLabel *zero_label = NULL;
    MemOp ot = gen_shift_count(s, decode, &can_be_zero, &count, decode->op[2].unit);
    TCGv low, high, high_count;

    if (!count) {
        return;
    }

    low = tcg_temp_new();
    high = tcg_temp_new();
    high_count = tcg_temp_new();

    gen_rotc_mod(ot, count);
    have_1bit_cin = gen_eflags_adcox(s, decode, true, can_be_zero);
    if (can_be_zero) {
        zero_label = gen_new_label();
        tcg_gen_brcondi_tl(TCG_COND_EQ, count, 0, zero_label);
    }

    /* Save incoming carry into high, it will be shifted later.  */
    if (!have_1bit_cin || tcg_op_deposit_valid(TCG_TYPE_TL, 1, TARGET_LONG_BITS - 1)) {
        TCGv cin = have_1bit_cin ? decode->cc_dst : decode->cc_src;
        tcg_gen_deposit_tl(high, cin, s->T0, 1, TARGET_LONG_BITS - 1);
    } else {
        /* Same as above but without deposit; cin in cc_dst.  */
        tcg_gen_add_tl(high, s->T0, decode->cc_dst);
        tcg_gen_add_tl(high, high, s->T0);
    }

    /* Compute low part and outgoing carry, incoming s->T0 is zero extended */
    tcg_gen_subi_tl(count, count, 1);
    tcg_gen_shr_tl(low, s->T0, count);
    tcg_gen_andi_tl(decode->cc_dst, low, 1);
    tcg_gen_shri_tl(low, low, 1);

    /* Move high part to the right position */
    tcg_gen_xori_tl(high_count, count, (8 << ot) - 1); /* LENGTH - 1 - (count - 1) */
    tcg_gen_shl_tl(high, high, high_count);

    /* Compute result and outgoing overflow */
    tcg_gen_mov_tl(decode->cc_src2, s->T0);
    tcg_gen_or_tl(s->T0, low, high);
    gen_rot_overflow(decode, s->T0, decode->cc_src2, false, NULL);

    if (zero_label) {
        gen_set_label(zero_label);
    }
}

#ifdef CONFIG_USER_ONLY
static void gen_unreachable(DisasContext *s, X86DecodedInsn *decode)
{
    g_assert_not_reached();
}
#endif

#ifndef CONFIG_USER_ONLY
static void gen_RDMSR(DisasContext *s, X86DecodedInsn *decode)
{
    gen_update_cc_op(s);
    gen_update_eip_cur(s);
    gen_helper_rdmsr(tcg_env);
}
#else
#define gen_RDMSR gen_unreachable
#endif

static void gen_RDPMC(DisasContext *s, X86DecodedInsn *decode)
{
    gen_update_cc_op(s);
    gen_update_eip_cur(s);
    translator_io_start(&s->base);
    gen_helper_rdpmc(tcg_env);
    s->base.is_jmp = DISAS_NORETURN;
}

static void gen_RDTSC(DisasContext *s, X86DecodedInsn *decode)
{
    gen_update_cc_op(s);
    gen_update_eip_cur(s);
    translator_io_start(&s->base);
    gen_helper_rdtsc(tcg_env);
}

static void gen_RDxxBASE(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv base = cpu_seg_base[s->modrm & 8 ? R_GS : R_FS];

    /* Preserve hflags bits by testing CR4 at runtime.  */
    gen_helper_cr4_testbit(tcg_env, tcg_constant_i32(CR4_FSGSBASE_MASK));
    tcg_gen_mov_tl(s->T0, base);
}

static void gen_RET(DisasContext *s, X86DecodedInsn *decode)
{
    int16_t adjust = decode->e.op1 == X86_TYPE_I ? decode->immediate : 0;

    MemOp ot = gen_pop_T0(s);
    gen_stack_update(s, adjust + (1 << ot));
    gen_op_jmp_v(s, s->T0);
    gen_bnd_jmp(s);
    s->base.is_jmp = DISAS_JUMP;
}

static void gen_RETF(DisasContext *s, X86DecodedInsn *decode)
{
    int16_t adjust = decode->e.op1 == X86_TYPE_I ? decode->immediate : 0;

    if (!PE(s) || VM86(s)) {
        gen_lea_ss_ofs(s, s->A0, cpu_regs[R_ESP], 0);
        /* pop offset */
        gen_op_ld_v(s, s->dflag, s->T0, s->A0);
        /* NOTE: keeping EIP updated is not a problem in case of
           exception */
        gen_op_jmp_v(s, s->T0);
        /* pop selector */
        gen_add_A0_im(s, 1 << s->dflag);
        gen_op_ld_v(s, s->dflag, s->T0, s->A0);
        gen_op_movl_seg_real(s, R_CS, s->T0);
        /* add stack offset */
        gen_stack_update(s, adjust + (2 << s->dflag));
    } else {
        gen_update_cc_op(s);
        gen_update_eip_cur(s);
        gen_helper_lret_protected(tcg_env, tcg_constant_i32(s->dflag - 1),
                                  tcg_constant_i32(adjust));
    }
    s->base.is_jmp = DISAS_EOB_ONLY;
}

/*
 * Return non-NULL if a 32-bit rotate works, after possibly replicating the input.
 * The input has already been zero-extended upon operand decode.
 */
static TCGv_i32 gen_rot_replicate(MemOp ot, TCGv in)
{
    TCGv_i32 temp;
    switch (ot) {
    case MO_8:
        temp = tcg_temp_new_i32();
        tcg_gen_trunc_tl_i32(temp, in);
        tcg_gen_muli_i32(temp, temp, 0x01010101);
        return temp;

    case MO_16:
        temp = tcg_temp_new_i32();
        tcg_gen_trunc_tl_i32(temp, in);
        tcg_gen_deposit_i32(temp, temp, temp, 16, 16);
        return temp;

#ifdef TARGET_X86_64
    case MO_32:
        temp = tcg_temp_new_i32();
        tcg_gen_trunc_tl_i32(temp, in);
        return temp;
#endif

    default:
        return NULL;
    }
}

static void gen_rot_carry(X86DecodedInsn *decode, TCGv result,
                          bool can_be_zero, TCGv count, int bit)
{
    if (!can_be_zero) {
        tcg_gen_extract_tl(decode->cc_dst, result, bit, 1);
    } else {
        TCGv temp = tcg_temp_new();
        tcg_gen_extract_tl(temp, result, bit, 1);
        tcg_gen_movcond_tl(TCG_COND_EQ, decode->cc_dst, count, tcg_constant_tl(0),
                           decode->cc_dst, temp);
    }
}

static void gen_ROL(DisasContext *s, X86DecodedInsn *decode)
{
    bool can_be_zero;
    TCGv count;
    MemOp ot = gen_shift_count(s, decode, &can_be_zero, &count, decode->op[2].unit);
    TCGv_i32 temp32, count32;
    TCGv old = tcg_temp_new();

    if (!count) {
        return;
    }

    gen_eflags_adcox(s, decode, false, can_be_zero);
    tcg_gen_mov_tl(old, s->T0);
    temp32 = gen_rot_replicate(ot, s->T0);
    if (temp32) {
        count32 = tcg_temp_new_i32();
        tcg_gen_trunc_tl_i32(count32, count);
        tcg_gen_rotl_i32(temp32, temp32, count32);
        /* Zero extend to facilitate later optimization.  */
        tcg_gen_extu_i32_tl(s->T0, temp32);
    } else {
        tcg_gen_rotl_tl(s->T0, s->T0, count);
    }
    gen_rot_carry(decode, s->T0, can_be_zero, count, 0);
    gen_rot_overflow(decode, s->T0, old, can_be_zero, count);
}

static void gen_ROR(DisasContext *s, X86DecodedInsn *decode)
{
    bool can_be_zero;
    TCGv count;
    MemOp ot = gen_shift_count(s, decode, &can_be_zero, &count, decode->op[2].unit);
    TCGv_i32 temp32, count32;
    TCGv old = tcg_temp_new();

    if (!count) {
        return;
    }

    gen_eflags_adcox(s, decode, false, can_be_zero);
    tcg_gen_mov_tl(old, s->T0);
    temp32 = gen_rot_replicate(ot, s->T0);
    if (temp32) {
        count32 = tcg_temp_new_i32();
        tcg_gen_trunc_tl_i32(count32, count);
        tcg_gen_rotr_i32(temp32, temp32, count32);
        /* Zero extend to facilitate later optimization.  */
        tcg_gen_extu_i32_tl(s->T0, temp32);
        gen_rot_carry(decode, s->T0, can_be_zero, count, 31);
    } else {
        tcg_gen_rotr_tl(s->T0, s->T0, count);
        gen_rot_carry(decode, s->T0, can_be_zero, count, TARGET_LONG_BITS - 1);
    }
    gen_rot_overflow(decode, s->T0, old, can_be_zero, count);
}

static void gen_RORX(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    int mask = ot == MO_64 ? 63 : 31;
    int b = decode->immediate & mask;

    switch (ot) {
    case MO_32:
#ifdef TARGET_X86_64
        {
            TCGv_i32 tmp = tcg_temp_new_i32();

            tcg_gen_trunc_tl_i32(tmp, s->T0);
            tcg_gen_rotri_i32(tmp, tmp, b);
            tcg_gen_extu_i32_tl(s->T0, tmp);
            break;
        }

    case MO_64:
#endif
        tcg_gen_rotri_tl(s->T0, s->T0, b);
        break;

    default:
        g_assert_not_reached();
    }
}

#ifndef CONFIG_USER_ONLY
static void gen_RSM(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_rsm(tcg_env);
    assume_cc_op(s, CC_OP_EFLAGS);
    s->base.is_jmp = DISAS_EOB_ONLY;
}
#else
#define gen_RSM gen_UD
#endif

static void gen_SAHF(DisasContext *s, X86DecodedInsn *decode)
{
    if (CODE64(s) && !(s->cpuid_ext3_features & CPUID_EXT3_LAHF_LM)) {
        return gen_illegal_opcode(s);
    }
    tcg_gen_shri_tl(s->T0, cpu_regs[R_EAX], 8);
    gen_compute_eflags(s);
    tcg_gen_andi_tl(cpu_cc_src, cpu_cc_src, CC_O);
    tcg_gen_andi_tl(s->T0, s->T0, CC_S | CC_Z | CC_A | CC_P | CC_C);
    tcg_gen_or_tl(cpu_cc_src, cpu_cc_src, s->T0);
}

static void gen_SALC(DisasContext *s, X86DecodedInsn *decode)
{
    gen_compute_eflags_c(s, s->T0);
    tcg_gen_neg_tl(s->T0, s->T0);
}

static void gen_shift_dynamic_flags(DisasContext *s, X86DecodedInsn *decode, TCGv count, CCOp cc_op)
{
    TCGv_i32 count32 = tcg_temp_new_i32();
    TCGv_i32 old_cc_op;

    decode->cc_op = CC_OP_DYNAMIC;
    decode->cc_op_dynamic = tcg_temp_new_i32();

    assert(decode->cc_dst == s->T0);
    if (cc_op_live(s->cc_op) & USES_CC_DST) {
        decode->cc_dst = tcg_temp_new();
        tcg_gen_movcond_tl(TCG_COND_EQ, decode->cc_dst, count, tcg_constant_tl(0),
                           cpu_cc_dst, s->T0);
    }

    if (cc_op_live(s->cc_op) & USES_CC_SRC) {
        tcg_gen_movcond_tl(TCG_COND_EQ, decode->cc_src, count, tcg_constant_tl(0),
                           cpu_cc_src, decode->cc_src);
    }

    tcg_gen_trunc_tl_i32(count32, count);
    if (s->cc_op == CC_OP_DYNAMIC) {
        old_cc_op = cpu_cc_op;
    } else {
        old_cc_op = tcg_constant_i32(s->cc_op);
    }
    tcg_gen_movcond_i32(TCG_COND_EQ, decode->cc_op_dynamic, count32, tcg_constant_i32(0),
                        old_cc_op, tcg_constant_i32(cc_op));
}

static void gen_SAR(DisasContext *s, X86DecodedInsn *decode)
{
    bool can_be_zero;
    TCGv count;
    MemOp ot = gen_shift_count(s, decode, &can_be_zero, &count, decode->op[2].unit);

    if (!count) {
        return;
    }

    decode->cc_dst = s->T0;
    decode->cc_src = tcg_temp_new();
    tcg_gen_subi_tl(decode->cc_src, count, 1);
    tcg_gen_sar_tl(decode->cc_src, s->T0, decode->cc_src);
    tcg_gen_sar_tl(s->T0, s->T0, count);
    if (can_be_zero) {
        gen_shift_dynamic_flags(s, decode, count, CC_OP_SARB + ot);
    } else {
        decode->cc_op = CC_OP_SARB + ot;
    }
}

static void gen_SARX(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    int mask;

    mask = ot == MO_64 ? 63 : 31;
    tcg_gen_andi_tl(s->T1, s->T1, mask);
    tcg_gen_sar_tl(s->T0, s->T0, s->T1);
}

static void gen_SUB(DisasContext *s, X86DecodedInsn *decode);
static void gen_SBB(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    TCGv c_in;

    /*
     * Try to avoid CC_OP_SBB by transforming as follows:
     * CC_SBB: src1 = dst + c_in, src2 = 0, src3 = c_in
     * CC_SUB: src1 = dst + c_in, src2 = c_in (no src3)
     *
     * In general src2 vs. src3 matters when computing AF and OF, but not here:
     * - AF is bit 4 of dst^src1^src2, which is bit 4 of dst^src1 in both cases
     * - OF is a function of the two MSBs, and in both cases they are zero for src2
     */
    if (decode->e.op2 == X86_TYPE_I && decode->immediate == 0) {
        gen_compute_eflags_c(s, s->T1);
        gen_SUB(s, decode);
        return;
    }

    c_in = tcg_temp_new();
    gen_compute_eflags_c(s, c_in);

    /*
     * Here the change is as follows:
     * CC_SBB: src1 = T0, src2 = T0, src3 = c_in
     * CC_SUB: src1 = 0, src2 = c_in (no src3)
     *
     * The difference also does not matter:
     * - AF is bit 4 of dst^src1^src2, but bit 4 of src1^src2 is zero in both cases
     *   therefore AF comes straight from dst (in fact it is c_in)
     * - for OF, src1 and src2 have the same sign in both cases, meaning there
     *   can be no overflow
     */
    if (decode->e.op2 != X86_TYPE_I && !decode->op[0].has_ea && decode->op[0].n == decode->op[2].n) {
        if (s->cc_op == CC_OP_DYNAMIC) {
            tcg_gen_neg_tl(s->T0, c_in);
        } else {
            /*
             * Do not negate c_in because it will often be dead and only the
             * instruction generated by negsetcond will survive.
             */
            gen_neg_setcc(s, JCC_B << 1, s->T0);
        }
        tcg_gen_movi_tl(s->cc_srcT, 0);
        decode->cc_src = c_in;
        decode->cc_dst = s->T0;
        decode->cc_op = CC_OP_SUBB + ot;
        return;
    }

    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_add_tl(s->T0, s->T1, c_in);
        tcg_gen_neg_tl(s->T0, s->T0);
        tcg_gen_atomic_add_fetch_tl(s->T0, s->A0, s->T0,
                                    s->mem_index, ot | MO_LE);
    } else {
        tcg_gen_sub_tl(s->T0, s->T0, s->T1);
        tcg_gen_sub_tl(s->T0, s->T0, c_in);
    }
    prepare_update3_cc(decode, s, CC_OP_SBBB + ot, c_in);
}

static void gen_SCAS(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[2].ot;
    gen_repz_nz(s, ot, gen_scas);
}

static void gen_SETcc(DisasContext *s, X86DecodedInsn *decode)
{
    gen_setcc(s, decode->b & 0xf, s->T0);
}

static void gen_SFENCE(DisasContext *s, X86DecodedInsn *decode)
{
    tcg_gen_mb(TCG_MO_ST_ST | TCG_BAR_SC);
}

static void gen_SHA1NEXTE(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_sha1nexte(OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_SHA1MSG1(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_sha1msg1(OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_SHA1MSG2(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_sha1msg2(OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_SHA1RNDS4(DisasContext *s, X86DecodedInsn *decode)
{
    switch(decode->immediate & 3) {
    case 0:
        gen_helper_sha1rnds4_f0(OP_PTR0, OP_PTR0, OP_PTR1);
        break;
    case 1:
        gen_helper_sha1rnds4_f1(OP_PTR0, OP_PTR0, OP_PTR1);
        break;
    case 2:
        gen_helper_sha1rnds4_f2(OP_PTR0, OP_PTR0, OP_PTR1);
        break;
    case 3:
        gen_helper_sha1rnds4_f3(OP_PTR0, OP_PTR0, OP_PTR1);
        break;
    }
}

static void gen_SHA256MSG1(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_sha256msg1(OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_SHA256MSG2(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_sha256msg2(OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_SHA256RNDS2(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 wk0 = tcg_temp_new_i32();
    TCGv_i32 wk1 = tcg_temp_new_i32();

    tcg_gen_ld_i32(wk0, tcg_env, ZMM_OFFSET(0) + offsetof(ZMMReg, ZMM_L(0)));
    tcg_gen_ld_i32(wk1, tcg_env, ZMM_OFFSET(0) + offsetof(ZMMReg, ZMM_L(1)));

    gen_helper_sha256rnds2(OP_PTR0, OP_PTR1, OP_PTR2, wk0, wk1);
}

static void gen_SHL(DisasContext *s, X86DecodedInsn *decode)
{
    bool can_be_zero;
    TCGv count;
    MemOp ot = gen_shift_count(s, decode, &can_be_zero, &count, decode->op[2].unit);

    if (!count) {
        return;
    }

    decode->cc_dst = s->T0;
    decode->cc_src = tcg_temp_new();
    tcg_gen_subi_tl(decode->cc_src, count, 1);
    tcg_gen_shl_tl(decode->cc_src, s->T0, decode->cc_src);
    tcg_gen_shl_tl(s->T0, s->T0, count);
    if (can_be_zero) {
        gen_shift_dynamic_flags(s, decode, count, CC_OP_SHLB + ot);
    } else {
        decode->cc_op = CC_OP_SHLB + ot;
    }
}

static void gen_SHLD(DisasContext *s, X86DecodedInsn *decode)
{
    bool can_be_zero;
    TCGv count;
    int unit = decode->e.op3 == X86_TYPE_I ? X86_OP_IMM : X86_OP_INT;
    MemOp ot = gen_shift_count(s, decode, &can_be_zero, &count, unit);

    if (!count) {
        return;
    }

    decode->cc_dst = s->T0;
    decode->cc_src = gen_shiftd_rm_T1(s, ot, false, count);
    if (can_be_zero) {
        gen_shift_dynamic_flags(s, decode, count, CC_OP_SHLB + ot);
    } else {
        decode->cc_op = CC_OP_SHLB + ot;
    }
}

static void gen_SHLX(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    int mask;

    mask = ot == MO_64 ? 63 : 31;
    tcg_gen_andi_tl(s->T1, s->T1, mask);
    tcg_gen_shl_tl(s->T0, s->T0, s->T1);
}

static void gen_SHR(DisasContext *s, X86DecodedInsn *decode)
{
    bool can_be_zero;
    TCGv count;
    MemOp ot = gen_shift_count(s, decode, &can_be_zero, &count, decode->op[2].unit);

    if (!count) {
        return;
    }

    decode->cc_dst = s->T0;
    decode->cc_src = tcg_temp_new();
    tcg_gen_subi_tl(decode->cc_src, count, 1);
    tcg_gen_shr_tl(decode->cc_src, s->T0, decode->cc_src);
    tcg_gen_shr_tl(s->T0, s->T0, count);
    if (can_be_zero) {
        gen_shift_dynamic_flags(s, decode, count, CC_OP_SARB + ot);
    } else {
        decode->cc_op = CC_OP_SARB + ot;
    }
}

static void gen_SHRD(DisasContext *s, X86DecodedInsn *decode)
{
    bool can_be_zero;
    TCGv count;
    int unit = decode->e.op3 == X86_TYPE_I ? X86_OP_IMM : X86_OP_INT;
    MemOp ot = gen_shift_count(s, decode, &can_be_zero, &count, unit);

    if (!count) {
        return;
    }

    decode->cc_dst = s->T0;
    decode->cc_src = gen_shiftd_rm_T1(s, ot, true, count);
    if (can_be_zero) {
        gen_shift_dynamic_flags(s, decode, count, CC_OP_SARB + ot);
    } else {
        decode->cc_op = CC_OP_SARB + ot;
    }
}

static void gen_SHRX(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    int mask;

    mask = ot == MO_64 ? 63 : 31;
    tcg_gen_andi_tl(s->T1, s->T1, mask);
    tcg_gen_shr_tl(s->T0, s->T0, s->T1);
}

static void gen_STC(DisasContext *s, X86DecodedInsn *decode)
{
    gen_compute_eflags(s);
    tcg_gen_ori_tl(cpu_cc_src, cpu_cc_src, CC_C);
}

static void gen_STD(DisasContext *s, X86DecodedInsn *decode)
{
    tcg_gen_st_i32(tcg_constant_i32(-1), tcg_env, offsetof(CPUX86State, df));
}

static void gen_STI(DisasContext *s, X86DecodedInsn *decode)
{
    gen_set_eflags(s, IF_MASK);
    s->base.is_jmp = DISAS_EOB_INHIBIT_IRQ;
}

static void gen_VAESKEYGEN(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    assert(!s->vex_l);
    gen_helper_aeskeygenassist_xmm(tcg_env, OP_PTR0, OP_PTR1, imm);
}

static void gen_STMXCSR(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_update_mxcsr(tcg_env);
    tcg_gen_ld32u_tl(s->T0, tcg_env, offsetof(CPUX86State, mxcsr));
}

static void gen_STOS(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;
    gen_repz(s, ot, gen_stos);
}

static void gen_SUB(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;

    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_neg_tl(s->T0, s->T1);
        tcg_gen_atomic_fetch_add_tl(s->cc_srcT, s->A0, s->T0,
                                    s->mem_index, ot | MO_LE);
        tcg_gen_sub_tl(s->T0, s->cc_srcT, s->T1);
    } else {
        tcg_gen_mov_tl(s->cc_srcT, s->T0);
        tcg_gen_sub_tl(s->T0, s->T0, s->T1);
    }
    prepare_update2_cc(decode, s, CC_OP_SUBB + ot);
}

static void gen_SYSCALL(DisasContext *s, X86DecodedInsn *decode)
{
    gen_update_cc_op(s);
    gen_update_eip_cur(s);
    gen_helper_syscall(tcg_env, cur_insn_len_i32(s));
    if (LMA(s)) {
        assume_cc_op(s, CC_OP_EFLAGS);
    }

    /*
     * TF handling for the syscall insn is different. The TF bit is checked
     * after the syscall insn completes. This allows #DB to not be
     * generated after one has entered CPL0 if TF is set in FMASK.
     */
    s->base.is_jmp = DISAS_EOB_RECHECK_TF;
}

static void gen_SYSENTER(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_sysenter(tcg_env);
    s->base.is_jmp = DISAS_EOB_ONLY;
}

static void gen_SYSEXIT(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_sysexit(tcg_env, tcg_constant_i32(s->dflag - 1));
    s->base.is_jmp = DISAS_EOB_ONLY;
}

static void gen_SYSRET(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_sysret(tcg_env, tcg_constant_i32(s->dflag - 1));
    if (LMA(s)) {
        assume_cc_op(s, CC_OP_EFLAGS);
    }

    /*
     * TF handling for the sysret insn is different. The TF bit is checked
     * after the sysret insn completes. This allows #DB to be
     * generated "as if" the syscall insn in userspace has just
     * completed.
     */
    s->base.is_jmp = DISAS_EOB_RECHECK_TF;
}

static void gen_TZCNT(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;

    /* C bit (cc_src) is defined related to the input.  */
    decode->cc_src = tcg_temp_new();
    decode->cc_dst = s->T0;
    decode->cc_op = CC_OP_BMILGB + ot;
    tcg_gen_mov_tl(decode->cc_src, s->T0);

    /* A zero input returns the operand size.  */
    tcg_gen_ctzi_tl(s->T0, s->T0, 8 << ot);
}

static void gen_UD(DisasContext *s, X86DecodedInsn *decode)
{
    gen_illegal_opcode(s);
}

static void gen_VAESIMC(DisasContext *s, X86DecodedInsn *decode)
{
    assert(!s->vex_l);
    gen_helper_aesimc_xmm(tcg_env, OP_PTR0, OP_PTR2);
}

/*
 * 00 = v*ps Vps, Hps, Wpd
 * 66 = v*pd Vpd, Hpd, Wps
 * f3 = v*ss Vss, Hss, Wps
 * f2 = v*sd Vsd, Hsd, Wps
 */
#define SSE_CMP(x) { \
    gen_helper_ ## x ## ps ## _xmm, gen_helper_ ## x ## pd ## _xmm, \
    gen_helper_ ## x ## ss, gen_helper_ ## x ## sd, \
    gen_helper_ ## x ## ps ## _ymm, gen_helper_ ## x ## pd ## _ymm}
static const SSEFunc_0_eppp gen_helper_cmp_funcs[32][6] = {
    SSE_CMP(cmpeq),
    SSE_CMP(cmplt),
    SSE_CMP(cmple),
    SSE_CMP(cmpunord),
    SSE_CMP(cmpneq),
    SSE_CMP(cmpnlt),
    SSE_CMP(cmpnle),
    SSE_CMP(cmpord),

    SSE_CMP(cmpequ),
    SSE_CMP(cmpnge),
    SSE_CMP(cmpngt),
    SSE_CMP(cmpfalse),
    SSE_CMP(cmpnequ),
    SSE_CMP(cmpge),
    SSE_CMP(cmpgt),
    SSE_CMP(cmptrue),

    SSE_CMP(cmpeqs),
    SSE_CMP(cmpltq),
    SSE_CMP(cmpleq),
    SSE_CMP(cmpunords),
    SSE_CMP(cmpneqq),
    SSE_CMP(cmpnltq),
    SSE_CMP(cmpnleq),
    SSE_CMP(cmpords),

    SSE_CMP(cmpequs),
    SSE_CMP(cmpngeq),
    SSE_CMP(cmpngtq),
    SSE_CMP(cmpfalses),
    SSE_CMP(cmpnequs),
    SSE_CMP(cmpgeq),
    SSE_CMP(cmpgtq),
    SSE_CMP(cmptrues),
};
#undef SSE_CMP

static void gen_VCMP(DisasContext *s, X86DecodedInsn *decode)
{
    int index = decode->immediate & (s->prefix & PREFIX_VEX ? 31 : 7);
    int b =
        s->prefix & PREFIX_REPZ  ? 2 /* ss */ :
        s->prefix & PREFIX_REPNZ ? 3 /* sd */ :
        !!(s->prefix & PREFIX_DATA) /* pd */ + (s->vex_l << 2);

    gen_helper_cmp_funcs[index][b](tcg_env, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_VCOMI(DisasContext *s, X86DecodedInsn *decode)
{
    SSEFunc_0_epp fn;
    fn = s->prefix & PREFIX_DATA ? gen_helper_comisd : gen_helper_comiss;
    fn(tcg_env, OP_PTR1, OP_PTR2);
    assume_cc_op(s, CC_OP_EFLAGS);
}

static void gen_VCVTPD2PS(DisasContext *s, X86DecodedInsn *decode)
{
    if (s->vex_l) {
        gen_helper_cvtpd2ps_ymm(tcg_env, OP_PTR0, OP_PTR2);
    } else {
        gen_helper_cvtpd2ps_xmm(tcg_env, OP_PTR0, OP_PTR2);
    }
}

static void gen_VCVTPS2PD(DisasContext *s, X86DecodedInsn *decode)
{
    if (s->vex_l) {
        gen_helper_cvtps2pd_ymm(tcg_env, OP_PTR0, OP_PTR2);
    } else {
        gen_helper_cvtps2pd_xmm(tcg_env, OP_PTR0, OP_PTR2);
    }
}

static void gen_VCVTPS2PH(DisasContext *s, X86DecodedInsn *decode)
{
    gen_unary_imm_fp_sse(s, decode,
                      gen_helper_cvtps2ph_xmm,
                      gen_helper_cvtps2ph_ymm);
    /*
     * VCVTPS2PH is the only instruction that performs an operation on a
     * register source and then *stores* into memory.
     */
    if (decode->op[0].has_ea) {
        gen_store_sse(s, decode, decode->op[0].offset);
    }
}

static void gen_VCVTSD2SS(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_cvtsd2ss(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_VCVTSS2SD(DisasContext *s, X86DecodedInsn *decode)
{
    gen_helper_cvtss2sd(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_VCVTSI2Sx(DisasContext *s, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);
    TCGv_i32 in;

    tcg_gen_gvec_mov(MO_64, decode->op[0].offset, decode->op[1].offset, vec_len, vec_len);

#ifdef TARGET_X86_64
    MemOp ot = decode->op[2].ot;
    if (ot == MO_64) {
        if (s->prefix & PREFIX_REPNZ) {
            gen_helper_cvtsq2sd(tcg_env, OP_PTR0, s->T1);
        } else {
            gen_helper_cvtsq2ss(tcg_env, OP_PTR0, s->T1);
        }
        return;
    }
    in = tcg_temp_new_i32();
    tcg_gen_trunc_tl_i32(in, s->T1);
#else
    in = s->T1;
#endif

    if (s->prefix & PREFIX_REPNZ) {
        gen_helper_cvtsi2sd(tcg_env, OP_PTR0, in);
    } else {
        gen_helper_cvtsi2ss(tcg_env, OP_PTR0, in);
    }
}

static inline void gen_VCVTtSx2SI(DisasContext *s, X86DecodedInsn *decode,
                                  SSEFunc_i_ep ss2si, SSEFunc_l_ep ss2sq,
                                  SSEFunc_i_ep sd2si, SSEFunc_l_ep sd2sq)
{
    TCGv_i32 out;

#ifdef TARGET_X86_64
    MemOp ot = decode->op[0].ot;
    if (ot == MO_64) {
        if (s->prefix & PREFIX_REPNZ) {
            sd2sq(s->T0, tcg_env, OP_PTR2);
        } else {
            ss2sq(s->T0, tcg_env, OP_PTR2);
        }
        return;
    }

    out = tcg_temp_new_i32();
#else
    out = s->T0;
#endif
    if (s->prefix & PREFIX_REPNZ) {
        sd2si(out, tcg_env, OP_PTR2);
    } else {
        ss2si(out, tcg_env, OP_PTR2);
    }
#ifdef TARGET_X86_64
    tcg_gen_extu_i32_tl(s->T0, out);
#endif
}

#ifndef TARGET_X86_64
#define gen_helper_cvtss2sq NULL
#define gen_helper_cvtsd2sq NULL
#define gen_helper_cvttss2sq NULL
#define gen_helper_cvttsd2sq NULL
#endif

static void gen_VCVTSx2SI(DisasContext *s, X86DecodedInsn *decode)
{
    gen_VCVTtSx2SI(s, decode,
                   gen_helper_cvtss2si, gen_helper_cvtss2sq,
                   gen_helper_cvtsd2si, gen_helper_cvtsd2sq);
}

static void gen_VCVTTSx2SI(DisasContext *s, X86DecodedInsn *decode)
{
    gen_VCVTtSx2SI(s, decode,
                   gen_helper_cvttss2si, gen_helper_cvttss2sq,
                   gen_helper_cvttsd2si, gen_helper_cvttsd2sq);
}

static void gen_VEXTRACTx128(DisasContext *s, X86DecodedInsn *decode)
{
    int mask = decode->immediate & 1;
    int src_ofs = vector_elem_offset(&decode->op[1], MO_128, mask);
    if (decode->op[0].has_ea) {
        /* VEX-only instruction, no alignment requirements.  */
        gen_sto_env_A0(s, src_ofs, false);
    } else {
        tcg_gen_gvec_mov(MO_64, decode->op[0].offset, src_ofs, 16, 16);
    }
}

static void gen_VEXTRACTPS(DisasContext *s, X86DecodedInsn *decode)
{
    gen_pextr(s, decode, MO_32);
}

static void gen_vinsertps(DisasContext *s, X86DecodedInsn *decode, TCGv_i32 tmp)
{
    int val = decode->immediate;
    int dest_word = (val >> 4) & 3;
    int new_mask = (val & 15) | (1 << dest_word);
    int vec_len = 16;

    assert(!s->vex_l);

    if (new_mask == 15) {
        /* All zeroes except possibly for the inserted element */
        tcg_gen_gvec_dup_imm(MO_64, decode->op[0].offset, vec_len, vec_len, 0);
    } else if (decode->op[1].offset != decode->op[0].offset) {
        gen_store_sse(s, decode, decode->op[1].offset);
    }

    if (new_mask != (val & 15)) {
        tcg_gen_st_i32(tmp, tcg_env,
                       vector_elem_offset(&decode->op[0], MO_32, dest_word));
    }

    if (new_mask != 15) {
        TCGv_i32 zero = tcg_constant_i32(0); /* float32_zero */
        int i;
        for (i = 0; i < 4; i++) {
            if ((val >> i) & 1) {
                tcg_gen_st_i32(zero, tcg_env,
                               vector_elem_offset(&decode->op[0], MO_32, i));
            }
        }
    }
}

static void gen_VINSERTPS_r(DisasContext *s, X86DecodedInsn *decode)
{
    int val = decode->immediate;
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_ld_i32(tmp, tcg_env,
                   vector_elem_offset(&decode->op[2], MO_32, (val >> 6) & 3));
    gen_vinsertps(s, decode, tmp);
}

static void gen_VINSERTPS_m(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_qemu_ld_i32(tmp, s->A0, s->mem_index, MO_LEUL);
    gen_vinsertps(s, decode, tmp);
}

static void gen_VINSERTx128(DisasContext *s, X86DecodedInsn *decode)
{
    int mask = decode->immediate & 1;
    tcg_gen_gvec_mov(MO_64,
                     decode->op[0].offset + offsetof(YMMReg, YMM_X(mask)),
                     decode->op[2].offset + offsetof(YMMReg, YMM_X(0)), 16, 16);
    tcg_gen_gvec_mov(MO_64,
                     decode->op[0].offset + offsetof(YMMReg, YMM_X(!mask)),
                     decode->op[1].offset + offsetof(YMMReg, YMM_X(!mask)), 16, 16);
}

static inline void gen_maskmov(DisasContext *s, X86DecodedInsn *decode,
                               SSEFunc_0_eppt xmm, SSEFunc_0_eppt ymm)
{
    if (!s->vex_l) {
        xmm(tcg_env, OP_PTR2, OP_PTR1, s->A0);
    } else {
        ymm(tcg_env, OP_PTR2, OP_PTR1, s->A0);
    }
}

static void gen_VMASKMOVPD_st(DisasContext *s, X86DecodedInsn *decode)
{
    gen_maskmov(s, decode, gen_helper_vpmaskmovq_st_xmm, gen_helper_vpmaskmovq_st_ymm);
}

static void gen_VMASKMOVPS_st(DisasContext *s, X86DecodedInsn *decode)
{
    gen_maskmov(s, decode, gen_helper_vpmaskmovd_st_xmm, gen_helper_vpmaskmovd_st_ymm);
}

static void gen_VMOVHPx_ld(DisasContext *s, X86DecodedInsn *decode)
{
    gen_ldq_env_A0(s, decode->op[0].offset + offsetof(XMMReg, XMM_Q(1)));
    if (decode->op[0].offset != decode->op[1].offset) {
        tcg_gen_ld_i64(s->tmp1_i64, tcg_env, decode->op[1].offset + offsetof(XMMReg, XMM_Q(0)));
        tcg_gen_st_i64(s->tmp1_i64, tcg_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(0)));
    }
}

static void gen_VMOVHPx_st(DisasContext *s, X86DecodedInsn *decode)
{
    gen_stq_env_A0(s, decode->op[2].offset + offsetof(XMMReg, XMM_Q(1)));
}

static void gen_VMOVHPx(DisasContext *s, X86DecodedInsn *decode)
{
    if (decode->op[0].offset != decode->op[2].offset) {
        tcg_gen_ld_i64(s->tmp1_i64, tcg_env, decode->op[2].offset + offsetof(XMMReg, XMM_Q(1)));
        tcg_gen_st_i64(s->tmp1_i64, tcg_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(1)));
    }
    if (decode->op[0].offset != decode->op[1].offset) {
        tcg_gen_ld_i64(s->tmp1_i64, tcg_env, decode->op[1].offset + offsetof(XMMReg, XMM_Q(0)));
        tcg_gen_st_i64(s->tmp1_i64, tcg_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(0)));
    }
}

static void gen_VMOVHLPS(DisasContext *s, X86DecodedInsn *decode)
{
    tcg_gen_ld_i64(s->tmp1_i64, tcg_env, decode->op[2].offset + offsetof(XMMReg, XMM_Q(1)));
    tcg_gen_st_i64(s->tmp1_i64, tcg_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(0)));
    if (decode->op[0].offset != decode->op[1].offset) {
        tcg_gen_ld_i64(s->tmp1_i64, tcg_env, decode->op[1].offset + offsetof(XMMReg, XMM_Q(1)));
        tcg_gen_st_i64(s->tmp1_i64, tcg_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(1)));
    }
}

static void gen_VMOVLHPS(DisasContext *s, X86DecodedInsn *decode)
{
    tcg_gen_ld_i64(s->tmp1_i64, tcg_env, decode->op[2].offset);
    tcg_gen_st_i64(s->tmp1_i64, tcg_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(1)));
    if (decode->op[0].offset != decode->op[1].offset) {
        tcg_gen_ld_i64(s->tmp1_i64, tcg_env, decode->op[1].offset + offsetof(XMMReg, XMM_Q(0)));
        tcg_gen_st_i64(s->tmp1_i64, tcg_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(0)));
    }
}

/*
 * Note that MOVLPx supports 256-bit operation unlike MOVHLPx, MOVLHPx, MOXHPx.
 * Use a gvec move to move everything above the bottom 64 bits.
 */

static void gen_VMOVLPx(DisasContext *s, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    tcg_gen_ld_i64(s->tmp1_i64, tcg_env, decode->op[2].offset + offsetof(XMMReg, XMM_Q(0)));
    tcg_gen_gvec_mov(MO_64, decode->op[0].offset, decode->op[1].offset, vec_len, vec_len);
    tcg_gen_st_i64(s->tmp1_i64, tcg_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(0)));
}

static void gen_VMOVLPx_ld(DisasContext *s, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    tcg_gen_qemu_ld_i64(s->tmp1_i64, s->A0, s->mem_index, MO_LEUQ);
    tcg_gen_gvec_mov(MO_64, decode->op[0].offset, decode->op[1].offset, vec_len, vec_len);
    tcg_gen_st_i64(s->tmp1_i64, OP_PTR0, offsetof(ZMMReg, ZMM_Q(0)));
}

static void gen_VMOVLPx_st(DisasContext *s, X86DecodedInsn *decode)
{
    tcg_gen_ld_i64(s->tmp1_i64, OP_PTR2, offsetof(ZMMReg, ZMM_Q(0)));
    tcg_gen_qemu_st_i64(s->tmp1_i64, s->A0, s->mem_index, MO_LEUQ);
}

static void gen_VMOVSD_ld(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i64 zero = tcg_constant_i64(0);

    tcg_gen_qemu_ld_i64(s->tmp1_i64, s->A0, s->mem_index, MO_LEUQ);
    tcg_gen_st_i64(zero, OP_PTR0, offsetof(ZMMReg, ZMM_Q(1)));
    tcg_gen_st_i64(s->tmp1_i64, OP_PTR0, offsetof(ZMMReg, ZMM_Q(0)));
}

static void gen_VMOVSS(DisasContext *s, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_ld_i32(tmp, OP_PTR2, offsetof(ZMMReg, ZMM_L(0)));
    tcg_gen_gvec_mov(MO_64, decode->op[0].offset, decode->op[1].offset, vec_len, vec_len);
    tcg_gen_st_i32(tmp, OP_PTR0, offsetof(ZMMReg, ZMM_L(0)));
}

static void gen_VMOVSS_ld(DisasContext *s, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_qemu_ld_i32(tmp, s->A0, s->mem_index, MO_LEUL);
    tcg_gen_gvec_dup_imm(MO_64, decode->op[0].offset, vec_len, vec_len, 0);
    tcg_gen_st_i32(tmp, OP_PTR0, offsetof(ZMMReg, ZMM_L(0)));
}

static void gen_VMOVSS_st(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_ld_i32(tmp, OP_PTR2, offsetof(ZMMReg, ZMM_L(0)));
    tcg_gen_qemu_st_i32(tmp, s->A0, s->mem_index, MO_LEUL);
}

static void gen_VPMASKMOV_st(DisasContext *s, X86DecodedInsn *decode)
{
    if (s->vex_w) {
        gen_VMASKMOVPD_st(s, decode);
    } else {
        gen_VMASKMOVPS_st(s, decode);
    }
}

static void gen_VPERMD(DisasContext *s, X86DecodedInsn *decode)
{
    assert(s->vex_l);
    gen_helper_vpermd_ymm(OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_VPERM2x128(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    assert(s->vex_l);
    gen_helper_vpermdq_ymm(OP_PTR0, OP_PTR1, OP_PTR2, imm);
}

static void gen_VPHMINPOSUW(DisasContext *s, X86DecodedInsn *decode)
{
    assert(!s->vex_l);
    gen_helper_phminposuw_xmm(tcg_env, OP_PTR0, OP_PTR2);
}

static void gen_VROUNDSD(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    assert(!s->vex_l);
    gen_helper_roundsd_xmm(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
}

static void gen_VROUNDSS(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    assert(!s->vex_l);
    gen_helper_roundss_xmm(tcg_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
}

static void gen_VSHUF(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant_i32(decode->immediate);
    SSEFunc_0_pppi ps, pd, fn;
    ps = s->vex_l ? gen_helper_shufps_ymm : gen_helper_shufps_xmm;
    pd = s->vex_l ? gen_helper_shufpd_ymm : gen_helper_shufpd_xmm;
    fn = s->prefix & PREFIX_DATA ? pd : ps;
    fn(OP_PTR0, OP_PTR1, OP_PTR2, imm);
}

static void gen_VUCOMI(DisasContext *s, X86DecodedInsn *decode)
{
    SSEFunc_0_epp fn;
    fn = s->prefix & PREFIX_DATA ? gen_helper_ucomisd : gen_helper_ucomiss;
    fn(tcg_env, OP_PTR1, OP_PTR2);
    assume_cc_op(s, CC_OP_EFLAGS);
}

static void gen_VZEROALL(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_ptr ptr = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(ptr, tcg_env, offsetof(CPUX86State, xmm_regs));
    gen_helper_memset(ptr, ptr, tcg_constant_i32(0),
                      tcg_constant_ptr(CPU_NB_REGS * sizeof(ZMMReg)));
}

static void gen_VZEROUPPER(DisasContext *s, X86DecodedInsn *decode)
{
    int i;

    for (i = 0; i < CPU_NB_REGS; i++) {
        int offset = offsetof(CPUX86State, xmm_regs[i].ZMM_X(1));
        tcg_gen_gvec_dup_imm(MO_64, offset, 16, 16, 0);
    }
}

static void gen_WAIT(DisasContext *s, X86DecodedInsn *decode)
{
    if ((s->flags & (HF_MP_MASK | HF_TS_MASK)) == (HF_MP_MASK | HF_TS_MASK)) {
        gen_NM_exception(s);
    } else {
        /* needs to be treated as I/O because of ferr_irq */
        translator_io_start(&s->base);
        gen_helper_fwait(tcg_env);
    }
}

#ifndef CONFIG_USER_ONLY
static void gen_WRMSR(DisasContext *s, X86DecodedInsn *decode)
{
    gen_update_cc_op(s);
    gen_update_eip_cur(s);
    gen_helper_wrmsr(tcg_env);
    s->base.is_jmp = DISAS_EOB_NEXT;
}
#else
#define gen_WRMSR gen_unreachable
#endif

static void gen_WRxxBASE(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv base = cpu_seg_base[s->modrm & 8 ? R_GS : R_FS];

    /* Preserve hflags bits by testing CR4 at runtime.  */
    gen_helper_cr4_testbit(tcg_env, tcg_constant_i32(CR4_FSGSBASE_MASK));
    tcg_gen_mov_tl(base, s->T0);
}

static void gen_XADD(DisasContext *s, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[1].ot;

    decode->cc_dst = tcg_temp_new();
    decode->cc_src = s->T1;
    decode->cc_op = CC_OP_ADDB + ot;

    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_atomic_fetch_add_tl(s->T0, s->A0, s->T1, s->mem_index, ot | MO_LE);
        tcg_gen_add_tl(decode->cc_dst, s->T0, s->T1);
    } else {
        tcg_gen_add_tl(decode->cc_dst, s->T0, s->T1);
        /*
         * NOTE: writing memory first is important for MMU exceptions,
         * but "new result" wins for XADD AX, AX.
         */
        gen_writeback(s, decode, 0, decode->cc_dst);
    }
    if (decode->op[0].has_ea || decode->op[2].n != decode->op[0].n) {
        gen_writeback(s, decode, 2, s->T0);
    }
}

static void gen_XCHG(DisasContext *s, X86DecodedInsn *decode)
{
    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_atomic_xchg_tl(s->T0, s->A0, s->T1,
                               s->mem_index, decode->op[0].ot | MO_LE);
        /* now store old value into register operand */
        gen_op_mov_reg_v(s, decode->op[2].ot, decode->op[2].n, s->T0);
    } else {
        /* move destination value into source operand, source preserved in T1 */
        gen_op_mov_reg_v(s, decode->op[2].ot, decode->op[2].n, s->T0);
        tcg_gen_mov_tl(s->T0, s->T1);
    }
}

static void gen_XLAT(DisasContext *s, X86DecodedInsn *decode)
{
    /* AL is already zero-extended into s->T0.  */
    tcg_gen_add_tl(s->A0, cpu_regs[R_EBX], s->T0);
    gen_lea_v_seg(s, s->A0, R_DS, s->override);
    gen_op_ld_v(s, MO_8, s->T0, s->A0);
}

static void gen_XOR(DisasContext *s, X86DecodedInsn *decode)
{
    /* special case XOR reg, reg */
    if (decode->op[1].unit == X86_OP_INT &&
        decode->op[2].unit == X86_OP_INT &&
        decode->op[1].n == decode->op[2].n) {
        tcg_gen_movi_tl(s->T0, 0);
        decode->cc_op = CC_OP_EFLAGS;
        decode->cc_src = tcg_constant_tl(CC_Z | CC_P);
    } else {
        MemOp ot = decode->op[1].ot;

        if (s->prefix & PREFIX_LOCK) {
            tcg_gen_atomic_xor_fetch_tl(s->T0, s->A0, s->T1,
                                        s->mem_index, ot | MO_LE);
        } else {
            tcg_gen_xor_tl(s->T0, s->T0, s->T1);
        }
        prepare_update1_cc(decode, s, CC_OP_LOGICB + ot);
    }
}

static void gen_XRSTOR(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i64 features = tcg_temp_new_i64();

    tcg_gen_concat_tl_i64(features, cpu_regs[R_EAX], cpu_regs[R_EDX]);
    gen_helper_xrstor(tcg_env, s->A0, features);
    if (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_MPX) {
        /*
         * XRSTOR is how MPX is enabled, which changes how
         * we translate.  Thus we need to end the TB.
         */
        s->base.is_jmp = DISAS_EOB_NEXT;
    }
}

static void gen_XSAVE(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i64 features = tcg_temp_new_i64();

    tcg_gen_concat_tl_i64(features, cpu_regs[R_EAX], cpu_regs[R_EDX]);
    gen_helper_xsave(tcg_env, s->A0, features);
}

static void gen_XSAVEOPT(DisasContext *s, X86DecodedInsn *decode)
{
    TCGv_i64 features = tcg_temp_new_i64();

    tcg_gen_concat_tl_i64(features, cpu_regs[R_EAX], cpu_regs[R_EDX]);
    gen_helper_xsave(tcg_env, s->A0, features);
}
