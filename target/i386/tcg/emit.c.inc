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

static inline TCGv_i32 tcg_constant8u_i32(uint8_t val)
{
    return tcg_constant_i32(val);
}

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

static TCGv_ptr op_ptr(X86DecodedInsn *decode, int opn)
{
    X86DecodedOp *op = &decode->op[opn];
    if (op->v_ptr) {
        return op->v_ptr;
    }
    op->v_ptr = tcg_temp_new_ptr();

    /* The temporary points to the MMXReg or ZMMReg.  */
    tcg_gen_addi_ptr(op->v_ptr, cpu_env, vector_reg_offset(op));
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

static inline int vector_len(DisasContext *s, X86DecodedInsn *decode)
{
    if (decode->e.special == X86_SPECIAL_MMX &&
        !(s->prefix & (PREFIX_DATA | PREFIX_REPZ | PREFIX_REPNZ))) {
        return 8;
    }
    return s->vex_l ? 32 : 16;
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

/*
 * 00 = v*ps Vps, Hps, Wpd
 * 66 = v*pd Vpd, Hpd, Wps
 * f3 = v*ss Vss, Hss, Wps
 * f2 = v*sd Vsd, Hsd, Wps
 */
static inline void gen_unary_fp_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
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
        fn(cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else {
        SSEFunc_0_epp ps, pd, fn;
        ps = s->vex_l ? ps_ymm : ps_xmm;
        pd = s->vex_l ? pd_ymm : pd_xmm;
        fn = s->prefix & PREFIX_DATA ? pd : ps;
        if (!fn) {
            gen_illegal_opcode(s);
            return;
        }
        fn(cpu_env, OP_PTR0, OP_PTR2);
    }
}
#define UNARY_FP_SSE(uname, lname)                                                 \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_unary_fp_sse(s, env, decode,                                               \
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
static inline void gen_fp_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
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
        fn(cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else {
        gen_illegal_opcode(s);
    }
}
#define FP_SSE(uname, lname)                                                       \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_fp_sse(s, env, decode,                                                     \
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

/*
 * 00 = v*ps Vps, Wpd
 * f3 = v*ss Vss, Wps
 */
static inline void gen_unary_fp32_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
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
        ss(cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else {
        SSEFunc_0_epp fn = s->vex_l ? ps_ymm : ps_xmm;
        if (!fn) {
            goto illegal_op;
        }
        fn(cpu_env, OP_PTR0, OP_PTR2);
    }
    return;

illegal_op:
    gen_illegal_opcode(s);
}
#define UNARY_FP32_SSE(uname, lname)                                               \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_unary_fp32_sse(s, env, decode,                                             \
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
static inline void gen_horizontal_fp_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                         SSEFunc_0_eppp pd_xmm, SSEFunc_0_eppp ps_xmm,
                                         SSEFunc_0_eppp pd_ymm, SSEFunc_0_eppp ps_ymm)
{
    SSEFunc_0_eppp ps, pd, fn;
    ps = s->vex_l ? ps_ymm : ps_xmm;
    pd = s->vex_l ? pd_ymm : pd_xmm;
    fn = s->prefix & PREFIX_DATA ? pd : ps;
    fn(cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
}
#define HORIZONTAL_FP_SSE(uname, lname)                                            \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_horizontal_fp_sse(s, env, decode,                                          \
                          gen_helper_##lname##pd_xmm, gen_helper_##lname##ps_xmm,  \
                          gen_helper_##lname##pd_ymm, gen_helper_##lname##ps_ymm); \
}
HORIZONTAL_FP_SSE(VHADD, hadd)
HORIZONTAL_FP_SSE(VHSUB, hsub)
HORIZONTAL_FP_SSE(VADDSUB, addsub)

static inline void gen_ternary_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                   int op3, SSEFunc_0_epppp xmm, SSEFunc_0_epppp ymm)
{
    SSEFunc_0_epppp fn = s->vex_l ? ymm : xmm;
    TCGv_ptr ptr3 = tcg_temp_new_ptr();

    /* The format of the fourth input is Lx */
    tcg_gen_addi_ptr(ptr3, cpu_env, ZMM_OFFSET(op3));
    fn(cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, ptr3);
    tcg_temp_free_ptr(ptr3);
}
#define TERNARY_SSE(uvname, lname)                                                 \
static void gen_##uvname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_ternary_sse(s, env, decode, (uint8_t)decode->immediate >> 4,               \
                    gen_helper_##lname##_xmm, gen_helper_##lname##_ymm);           \
}
TERNARY_SSE(VBLENDVPS, blendvps)
TERNARY_SSE(VBLENDVPD, blendvpd)
TERNARY_SSE(VPBLENDVB, pblendvb)

static inline void gen_binary_imm_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                      SSEFunc_0_epppi xmm, SSEFunc_0_epppi ymm)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    if (!s->vex_l) {
        xmm(cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
    } else {
        ymm(cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
    }
}

#define BINARY_IMM_SSE(uname, lname)                                               \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_binary_imm_sse(s, env, decode,                                             \
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

#define BINARY_INT_GVEC(uname, func, ...)                                          \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
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
BINARY_INT_GVEC(PCMPGTB, tcg_gen_gvec_cmp, TCG_COND_GT, MO_8)
BINARY_INT_GVEC(PCMPGTW, tcg_gen_gvec_cmp, TCG_COND_GT, MO_16)
BINARY_INT_GVEC(PCMPGTD, tcg_gen_gvec_cmp, TCG_COND_GT, MO_32)
BINARY_INT_GVEC(PMAXSW,  tcg_gen_gvec_smax, MO_16)
BINARY_INT_GVEC(PMAXUB,  tcg_gen_gvec_umax, MO_8)
BINARY_INT_GVEC(PMINSW,  tcg_gen_gvec_smin, MO_16)
BINARY_INT_GVEC(PMINUB,  tcg_gen_gvec_umin, MO_8)
BINARY_INT_GVEC(PMULLW,  tcg_gen_gvec_mul, MO_16)
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
static inline void gen_binary_int_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                      SSEFunc_0_eppp mmx, SSEFunc_0_eppp xmm, SSEFunc_0_eppp ymm)
{
    assert(!!mmx == !!(decode->e.special == X86_SPECIAL_MMX));

    if (mmx && (s->prefix & PREFIX_VEX) && !(s->prefix & PREFIX_DATA)) {
        /* VEX encoding is not applicable to MMX instructions.  */
        gen_illegal_opcode(s);
        return;
    }
    if (!(s->prefix & PREFIX_DATA)) {
        mmx(cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else if (!s->vex_l) {
        xmm(cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    } else {
        ymm(cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
    }
}


#define BINARY_INT_MMX(uname, lname)                                               \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_binary_int_sse(s, env, decode,                                             \
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

/* Instructions with no MMX equivalent.  */
#define BINARY_INT_SSE(uname, lname)                                               \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_binary_int_sse(s, env, decode,                                             \
                          NULL,                                                    \
                          gen_helper_##lname##_xmm,                                \
                          gen_helper_##lname##_ymm);                               \
}

BINARY_INT_SSE(PUNPCKLQDQ, punpcklqdq)
BINARY_INT_SSE(PUNPCKHQDQ, punpckhqdq)

static inline void gen_unary_int_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                     SSEFunc_0_epp xmm, SSEFunc_0_epp ymm)
{
    if (!s->vex_l) {
        xmm(cpu_env, OP_PTR0, OP_PTR2);
    } else {
        ymm(cpu_env, OP_PTR0, OP_PTR2);
    }
}

#define UNARY_INT_SSE(uname, lname)                                                \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_unary_int_sse(s, env, decode,                                              \
                      gen_helper_##lname##_xmm,                                    \
                      gen_helper_##lname##_ymm);                                   \
}

UNARY_INT_SSE(VCVTDQ2PD, cvtdq2pd)
UNARY_INT_SSE(VCVTPD2DQ, cvtpd2dq)
UNARY_INT_SSE(VCVTTPD2DQ, cvttpd2dq)
UNARY_INT_SSE(VCVTDQ2PS, cvtdq2ps)
UNARY_INT_SSE(VCVTPS2DQ, cvtps2dq)
UNARY_INT_SSE(VCVTTPS2DQ, cvttps2dq)


static inline void gen_unary_imm_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
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
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_unary_imm_sse(s, env, decode,                                              \
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

static inline void gen_unary_imm_fp_sse(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                        SSEFunc_0_eppi xmm, SSEFunc_0_eppi ymm)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    if (!s->vex_l) {
        xmm(cpu_env, OP_PTR0, OP_PTR1, imm);
    } else {
        ymm(cpu_env, OP_PTR0, OP_PTR1, imm);
    }
}

#define UNARY_IMM_FP_SSE(uname, lname)                                             \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_unary_imm_fp_sse(s, env, decode,                                           \
                      gen_helper_##lname##_xmm,                                    \
                      gen_helper_##lname##_ymm);                                   \
}

UNARY_IMM_FP_SSE(VROUNDPS,    roundps)
UNARY_IMM_FP_SSE(VROUNDPD,    roundpd)

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

static void gen_EMMS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_helper_emms(cpu_env);
}

static void gen_EXTRQ_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGv_i32 length = tcg_constant_i32(decode->immediate & 63);
    TCGv_i32 index = tcg_constant_i32((decode->immediate >> 8) & 63);

    gen_helper_extrq_i(cpu_env, OP_PTR0, index, length);
}

static void gen_EXTRQ_r(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_helper_extrq_r(cpu_env, OP_PTR0, OP_PTR2);
}

static void gen_INSERTQ_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGv_i32 length = tcg_constant_i32(decode->immediate & 63);
    TCGv_i32 index = tcg_constant_i32((decode->immediate >> 8) & 63);

    gen_helper_insertq_i(cpu_env, OP_PTR0, OP_PTR1, index, length);
}

static void gen_INSERTQ_r(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_helper_insertq_r(cpu_env, OP_PTR0, OP_PTR2);
}

static void gen_MASKMOV(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    tcg_gen_mov_tl(s->A0, cpu_regs[R_EDI]);
    gen_extu(s->aflag, s->A0);
    gen_add_A0_ds_seg(s);

    if (s->prefix & PREFIX_DATA) {
        gen_helper_maskmov_xmm(cpu_env, OP_PTR1, OP_PTR2, s->A0);
    } else {
        gen_helper_maskmov_mmx(cpu_env, OP_PTR1, OP_PTR2, s->A0);
    }
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

static void gen_MOVD_from(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[2].ot;

    switch (ot) {
    case MO_32:
#ifdef TARGET_X86_64
        tcg_gen_ld32u_tl(s->T0, cpu_env, decode->op[2].offset);
        break;
    case MO_64:
#endif
        tcg_gen_ld_tl(s->T0, cpu_env, decode->op[2].offset);
        break;
    default:
        abort();
    }
}

static void gen_MOVD_to(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[2].ot;
    int vec_len = vector_len(s, decode);
    int lo_ofs = vector_elem_offset(&decode->op[0], ot, 0);

    tcg_gen_gvec_dup_imm(MO_64, decode->op[0].offset, vec_len, vec_len, 0);

    switch (ot) {
    case MO_32:
#ifdef TARGET_X86_64
        tcg_gen_st32_tl(s->T1, cpu_env, lo_ofs);
        break;
    case MO_64:
#endif
        tcg_gen_st_tl(s->T1, cpu_env, lo_ofs);
        break;
    default:
        g_assert_not_reached();
    }
}

static void gen_MOVDQ(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_store_sse(s, decode, decode->op[2].offset);
}

static void gen_MOVMSK(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    typeof(gen_helper_movmskps_ymm) *ps, *pd, *fn;
    ps = s->vex_l ? gen_helper_movmskps_ymm : gen_helper_movmskps_xmm;
    pd = s->vex_l ? gen_helper_movmskpd_ymm : gen_helper_movmskpd_xmm;
    fn = s->prefix & PREFIX_DATA ? pd : ps;
    fn(s->tmp2_i32, cpu_env, OP_PTR2);
    tcg_gen_extu_i32_tl(s->T0, s->tmp2_i32);
}

static void gen_MOVQ(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);
    int lo_ofs = vector_elem_offset(&decode->op[0], MO_64, 0);

    tcg_gen_ld_i64(s->tmp1_i64, cpu_env, decode->op[2].offset);
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
        tcg_gen_st_i64(s->tmp1_i64, cpu_env, lo_ofs);
    }
}

static void gen_MOVq_dq(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_helper_enter_mmx(cpu_env);
    /* Otherwise the same as any other movq.  */
    return gen_MOVQ(s, env, decode);
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

static void gen_PALIGNR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    if (!(s->prefix & PREFIX_DATA)) {
        gen_helper_palignr_mmx(cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
    } else if (!s->vex_l) {
        gen_helper_palignr_xmm(cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
    } else {
        gen_helper_palignr_ymm(cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
    }
}

static void gen_PANDN(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    /* Careful, operand order is reversed!  */
    tcg_gen_gvec_andc(MO_64,
                      decode->op[0].offset, decode->op[2].offset,
                      decode->op[1].offset, vec_len, vec_len);
}

static void gen_PCMPESTRI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    gen_helper_pcmpestri_xmm(cpu_env, OP_PTR1, OP_PTR2, imm);
    set_cc_op(s, CC_OP_EFLAGS);
}

static void gen_PCMPESTRM(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    gen_helper_pcmpestrm_xmm(cpu_env, OP_PTR1, OP_PTR2, imm);
    set_cc_op(s, CC_OP_EFLAGS);
    if ((s->prefix & PREFIX_VEX) && !s->vex_l) {
        tcg_gen_gvec_dup_imm(MO_64, offsetof(CPUX86State, xmm_regs[0].ZMM_X(1)),
                             16, 16, 0);
    }
}

static void gen_PCMPISTRI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    gen_helper_pcmpistri_xmm(cpu_env, OP_PTR1, OP_PTR2, imm);
    set_cc_op(s, CC_OP_EFLAGS);
}

static void gen_PCMPISTRM(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    gen_helper_pcmpistrm_xmm(cpu_env, OP_PTR1, OP_PTR2, imm);
    set_cc_op(s, CC_OP_EFLAGS);
    if ((s->prefix & PREFIX_VEX) && !s->vex_l) {
        tcg_gen_gvec_dup_imm(MO_64, offsetof(CPUX86State, xmm_regs[0].ZMM_X(1)),
                             16, 16, 0);
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

static inline void gen_pextr(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode, MemOp ot)
{
    int vec_len = vector_len(s, decode);
    int mask = (vec_len >> ot) - 1;
    int val = decode->immediate & mask;

    switch (ot) {
    case MO_8:
        tcg_gen_ld8u_tl(s->T0, cpu_env, vector_elem_offset(&decode->op[1], ot, val));
        break;
    case MO_16:
        tcg_gen_ld16u_tl(s->T0, cpu_env, vector_elem_offset(&decode->op[1], ot, val));
        break;
    case MO_32:
#ifdef TARGET_X86_64
        tcg_gen_ld32u_tl(s->T0, cpu_env, vector_elem_offset(&decode->op[1], ot, val));
        break;
    case MO_64:
#endif
        tcg_gen_ld_tl(s->T0, cpu_env, vector_elem_offset(&decode->op[1], ot, val));
        break;
    default:
        abort();
    }
}

static void gen_PEXTRB(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_pextr(s, env, decode, MO_8);
}

static void gen_PEXTRW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_pextr(s, env, decode, MO_16);
}

static void gen_PEXTR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    MemOp ot = decode->op[0].ot;
    gen_pextr(s, env, decode, ot);
}

static inline void gen_pinsr(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode, MemOp ot)
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
        tcg_gen_st8_tl(s->T1, cpu_env, vector_elem_offset(&decode->op[0], ot, val));
        break;
    case MO_16:
        tcg_gen_st16_tl(s->T1, cpu_env, vector_elem_offset(&decode->op[0], ot, val));
        break;
    case MO_32:
#ifdef TARGET_X86_64
        tcg_gen_st32_tl(s->T1, cpu_env, vector_elem_offset(&decode->op[0], ot, val));
        break;
    case MO_64:
#endif
        tcg_gen_st_tl(s->T1, cpu_env, vector_elem_offset(&decode->op[0], ot, val));
        break;
    default:
        abort();
    }
}

static void gen_PINSRB(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_pinsr(s, env, decode, MO_8);
}

static void gen_PINSR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_pinsr(s, env, decode, decode->op[2].ot);
}

static void gen_PMOVMSKB(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    if (s->prefix & PREFIX_DATA) {
        gen_helper_pmovmskb_xmm(s->tmp2_i32, cpu_env, OP_PTR2);
    } else {
        gen_helper_pmovmskb_mmx(s->tmp2_i32, cpu_env, OP_PTR2);
    }
    tcg_gen_extu_i32_tl(s->T0, s->tmp2_i32);
}

static void gen_PSHUFW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    gen_helper_pshufw_mmx(OP_PTR0, OP_PTR1, imm);
}

static void gen_PSRLW_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
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

static void gen_PSLLW_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
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

static void gen_PSRAW_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 16) {
        decode->immediate = 15;
    }
    tcg_gen_gvec_sari(MO_16,
                      decode->op[0].offset, decode->op[1].offset,
                      decode->immediate, vec_len, vec_len);
}

static void gen_PSRLD_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
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

static void gen_PSLLD_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
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

static void gen_PSRAD_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    if (decode->immediate >= 32) {
        decode->immediate = 31;
    }
    tcg_gen_gvec_sari(MO_32,
                      decode->op[0].offset, decode->op[1].offset,
                      decode->immediate, vec_len, vec_len);
}

static void gen_PSRLQ_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
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

static void gen_PSLLQ_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
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

    tcg_gen_addi_ptr(ptr, cpu_env, offsetof(CPUX86State, xmm_t0));
    tcg_gen_st_i32(imm_v, cpu_env, offsetof(CPUX86State, xmm_t0.ZMM_L(0)));
    return ptr;
}

static void gen_PSRLDQ_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);
    TCGv_ptr imm_vec = make_imm8u_xmm_vec(decode->immediate, vec_len);

    if (s->vex_l) {
        gen_helper_psrldq_ymm(cpu_env, OP_PTR0, OP_PTR1, imm_vec);
    } else {
        gen_helper_psrldq_xmm(cpu_env, OP_PTR0, OP_PTR1, imm_vec);
    }
    tcg_temp_free_ptr(imm_vec);
}

static void gen_PSLLDQ_i(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);
    TCGv_ptr imm_vec = make_imm8u_xmm_vec(decode->immediate, vec_len);

    if (s->vex_l) {
        gen_helper_pslldq_ymm(cpu_env, OP_PTR0, OP_PTR1, imm_vec);
    } else {
        gen_helper_pslldq_xmm(cpu_env, OP_PTR0, OP_PTR1, imm_vec);
    }
    tcg_temp_free_ptr(imm_vec);
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

static void gen_VAESKEYGEN(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    assert(!s->vex_l);
    gen_helper_aeskeygenassist_xmm(cpu_env, OP_PTR0, OP_PTR1, imm);
}

static void gen_VCVTfp2fp(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_unary_fp_sse(s, env, decode,
                     gen_helper_cvtpd2ps_xmm, gen_helper_cvtps2pd_xmm,
                     gen_helper_cvtpd2ps_ymm, gen_helper_cvtps2pd_ymm,
                     gen_helper_cvtsd2ss, gen_helper_cvtss2sd);
}

static void gen_VEXTRACTx128(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
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

static void gen_VEXTRACTPS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_pextr(s, env, decode, MO_32);
}

static void gen_vinsertps(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
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
        tcg_gen_st_i32(s->tmp2_i32, cpu_env,
                       vector_elem_offset(&decode->op[0], MO_32, dest_word));
    }

    if (new_mask != 15) {
        TCGv_i32 zero = tcg_constant_i32(0); /* float32_zero */
        int i;
        for (i = 0; i < 4; i++) {
            if ((val >> i) & 1) {
                tcg_gen_st_i32(zero, cpu_env,
                               vector_elem_offset(&decode->op[0], MO_32, i));
            }
        }
    }
}

static void gen_VINSERTPS_r(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    int val = decode->immediate;
    tcg_gen_ld_i32(s->tmp2_i32, cpu_env,
                   vector_elem_offset(&decode->op[2], MO_32, (val >> 6) & 3));
    gen_vinsertps(s, env, decode);
}

static void gen_VINSERTPS_m(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    tcg_gen_qemu_ld_i32(s->tmp2_i32, s->A0, s->mem_index, MO_LEUL);
    gen_vinsertps(s, env, decode);
}

static void gen_VINSERTx128(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    int mask = decode->immediate & 1;
    tcg_gen_gvec_mov(MO_64,
                     decode->op[0].offset + offsetof(YMMReg, YMM_X(mask)),
                     decode->op[2].offset + offsetof(YMMReg, YMM_X(0)), 16, 16);
    tcg_gen_gvec_mov(MO_64,
                     decode->op[0].offset + offsetof(YMMReg, YMM_X(!mask)),
                     decode->op[1].offset + offsetof(YMMReg, YMM_X(!mask)), 16, 16);
}

static void gen_VPERM2x128(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    assert(s->vex_l);
    gen_helper_vpermdq_ymm(OP_PTR0, OP_PTR1, OP_PTR2, imm);
}

static void gen_VROUNDSD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    assert(!s->vex_l);
    gen_helper_roundsd_xmm(cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
}

static void gen_VROUNDSS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    assert(!s->vex_l);
    gen_helper_roundss_xmm(cpu_env, OP_PTR0, OP_PTR1, OP_PTR2, imm);
}

static void gen_VZEROALL(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGv_ptr ptr = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(ptr, cpu_env, offsetof(CPUX86State, xmm_t0));
    gen_helper_memset(ptr, ptr, tcg_constant_i32(0),
                      tcg_constant_ptr(CPU_NB_REGS * sizeof(ZMMReg)));
    tcg_temp_free_ptr(ptr);
}

static void gen_VZEROUPPER(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    int i;

    for (i = 0; i < CPU_NB_REGS; i++) {
        int offset = offsetof(CPUX86State, xmm_regs[i].ZMM_X(1));
        tcg_gen_gvec_dup_imm(MO_64, offset, 16, 16, 0);
    }
}
