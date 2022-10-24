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

#define ZMM_OFFSET(reg) offsetof(CPUX86State, xmm_regs[reg])

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
        if (!op->has_ea && (s->prefix & PREFIX_VEX) && op->ot <= MO_128) {
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

static void gen_3dnow(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
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

    gen_helper_enter_mmx(cpu_env);
    if (fn == FN_3DNOW_MOVE) {
       tcg_gen_ld_i64(s->tmp1_i64, cpu_env, decode->op[1].offset);
       tcg_gen_st_i64(s->tmp1_i64, cpu_env, decode->op[0].offset);
    } else {
       fn(cpu_env, OP_PTR0, OP_PTR1);
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

#define FMA_SSE_PACKED(uname, ptr0, ptr1, ptr2, even, odd)                         \
static void gen_##uname##Px(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    SSEFunc_0_eppppii xmm = s->vex_w ? gen_helper_fma4pd_xmm : gen_helper_fma4ps_xmm; \
    SSEFunc_0_eppppii ymm = s->vex_w ? gen_helper_fma4pd_ymm : gen_helper_fma4ps_ymm; \
    SSEFunc_0_eppppii fn = s->vex_l ? ymm : xmm;                                   \
                                                                                   \
    fn(cpu_env, OP_PTR0, ptr0, ptr1, ptr2,                                         \
       tcg_constant_i32(even),                                                     \
       tcg_constant_i32((even) ^ (odd)));                                          \
}

#define FMA_SSE(uname, ptr0, ptr1, ptr2, flags)                                    \
FMA_SSE_PACKED(uname, ptr0, ptr1, ptr2, flags, flags)                              \
static void gen_##uname##Sx(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    SSEFunc_0_eppppi fn = s->vex_w ? gen_helper_fma4sd : gen_helper_fma4ss;        \
                                                                                   \
    fn(cpu_env, OP_PTR0, ptr0, ptr1, ptr2,                                         \
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
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    /* PS maps to the DQ integer instruction, PD maps to QDQ.  */                  \
    gen_fp_sse(s, env, decode,                                                     \
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
#define TERNARY_SSE(uname, uvname, lname)                                          \
static void gen_##uvname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_ternary_sse(s, env, decode, (uint8_t)decode->immediate >> 4,               \
                    gen_helper_##lname##_xmm, gen_helper_##lname##_ymm);           \
}                                                                                  \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_ternary_sse(s, env, decode, 0,                                             \
                  gen_helper_##lname##_xmm, gen_helper_##lname##_ymm);             \
}
TERNARY_SSE(BLENDVPS, VBLENDVPS, blendvps)
TERNARY_SSE(BLENDVPD, VBLENDVPD, blendvpd)
TERNARY_SSE(PBLENDVB, VPBLENDVB, pblendvb)

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


#define UNARY_INT_GVEC(uname, func, ...)                                           \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
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
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_binary_int_sse(s, env, decode,                                             \
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
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    if (!s->vex_l) {                                                               \
        gen_helper_##lname##_xmm(cpu_env, OP_PTR1, OP_PTR2);                       \
    } else {                                                                       \
        gen_helper_##lname##_ymm(cpu_env, OP_PTR1, OP_PTR2);                       \
    }                                                                              \
    set_cc_op(s, CC_OP_EFLAGS);                                                    \
}
UNARY_CMP_SSE(VPTEST,     ptest)
UNARY_CMP_SSE(VTESTPS,    vtestps)
UNARY_CMP_SSE(VTESTPD,    vtestpd)

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

static inline void gen_vexw_avx(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                SSEFunc_0_eppp d_xmm, SSEFunc_0_eppp q_xmm,
                                SSEFunc_0_eppp d_ymm, SSEFunc_0_eppp q_ymm)
{
    SSEFunc_0_eppp d = s->vex_l ? d_ymm : d_xmm;
    SSEFunc_0_eppp q = s->vex_l ? q_ymm : q_xmm;
    SSEFunc_0_eppp fn = s->vex_w ? q : d;
    fn(cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
}

/* VEX.W affects whether to operate on 32- or 64-bit elements.  */
#define VEXW_AVX(uname, lname)                                                     \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_vexw_avx(s, env, decode,                                                   \
                 gen_helper_##lname##d_xmm, gen_helper_##lname##q_xmm,             \
                 gen_helper_##lname##d_ymm, gen_helper_##lname##q_ymm);            \
}
VEXW_AVX(VPSLLV,    vpsllv)
VEXW_AVX(VPSRLV,    vpsrlv)
VEXW_AVX(VPSRAV,    vpsrav)
VEXW_AVX(VPMASKMOV, vpmaskmov)

/* Same as above, but with extra arguments to the helper.  */
static inline void gen_vsib_avx(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                SSEFunc_0_epppti d_xmm, SSEFunc_0_epppti q_xmm,
                                SSEFunc_0_epppti d_ymm, SSEFunc_0_epppti q_ymm)
{
    SSEFunc_0_epppti d = s->vex_l ? d_ymm : d_xmm;
    SSEFunc_0_epppti q = s->vex_l ? q_ymm : q_xmm;
    SSEFunc_0_epppti fn = s->vex_w ? q : d;
    TCGv_i32 scale = tcg_constant_i32(decode->mem.scale);
    TCGv_ptr index = tcg_temp_new_ptr();

    /* Pass third input as (index, base, scale) */
    tcg_gen_addi_ptr(index, cpu_env, ZMM_OFFSET(decode->mem.index));
    fn(cpu_env, OP_PTR0, OP_PTR1, index, s->A0, scale);

    /*
     * There are two output operands, so zero OP1's high 128 bits
     * in the VEX.128 case.
     */
    if (!s->vex_l) {
        int ymmh_ofs = vector_elem_offset(&decode->op[1], MO_128, 1);
        tcg_gen_gvec_dup_imm(MO_64, ymmh_ofs, 16, 16, 0);
    }
    tcg_temp_free_ptr(index);
}
#define VSIB_AVX(uname, lname)                                                     \
static void gen_##uname(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode) \
{                                                                                  \
    gen_vsib_avx(s, env, decode,                                                   \
                 gen_helper_##lname##d_xmm, gen_helper_##lname##q_xmm,             \
                 gen_helper_##lname##d_ymm, gen_helper_##lname##q_ymm);            \
}
VSIB_AVX(VPGATHERD, vpgatherd)
VSIB_AVX(VPGATHERQ, vpgatherq)

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

static void gen_CVTPI2Px(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_helper_enter_mmx(cpu_env);
    if (s->prefix & PREFIX_DATA) {
        gen_helper_cvtpi2pd(cpu_env, OP_PTR0, OP_PTR2);
    } else {
        gen_helper_cvtpi2ps(cpu_env, OP_PTR0, OP_PTR2);
    }
}

static void gen_CVTPx2PI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_helper_enter_mmx(cpu_env);
    if (s->prefix & PREFIX_DATA) {
        gen_helper_cvtpd2pi(cpu_env, OP_PTR0, OP_PTR2);
    } else {
        gen_helper_cvtps2pi(cpu_env, OP_PTR0, OP_PTR2);
    }
}

static void gen_CVTTPx2PI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_helper_enter_mmx(cpu_env);
    if (s->prefix & PREFIX_DATA) {
        gen_helper_cvttpd2pi(cpu_env, OP_PTR0, OP_PTR2);
    } else {
        gen_helper_cvttps2pi(cpu_env, OP_PTR0, OP_PTR2);
    }
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

static void gen_LDMXCSR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    if (s->vex_l) {
        gen_illegal_opcode(s);
        return;
    }
    tcg_gen_trunc_tl_i32(s->tmp2_i32, s->T1);
    gen_helper_ldmxcsr(cpu_env, s->tmp2_i32);
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

static void gen_PINSRW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_pinsr(s, env, decode, MO_16);
}

static void gen_PINSR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_pinsr(s, env, decode, decode->op[2].ot);
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

#ifdef TARGET_X86_64
#define TCG_TARGET_HAS_extract2_tl TCG_TARGET_HAS_extract2_i64
#else
#define TCG_TARGET_HAS_extract2_tl TCG_TARGET_HAS_extract2_i32
#endif

static void gen_PMOVMSKB(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
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
    tcg_gen_ld8u_tl(s->T0, cpu_env, offsetof(CPUX86State, xmm_t0.ZMM_B(vec_len - 1)));
    while (vec_len > 8) {
        vec_len -= 8;
        if (TCG_TARGET_HAS_extract2_tl) {
            /*
             * Load the next byte of the result into the high byte of T.
             * TCG does a similar expansion of deposit to shl+extract2; by
             * loading the whole word, the shift left is avoided.
             */
#ifdef TARGET_X86_64
            tcg_gen_ld_tl(t, cpu_env, offsetof(CPUX86State, xmm_t0.ZMM_Q((vec_len - 1) / 8)));
#else
            tcg_gen_ld_tl(t, cpu_env, offsetof(CPUX86State, xmm_t0.ZMM_L((vec_len - 1) / 4)));
#endif

            tcg_gen_extract2_tl(s->T0, t, s->T0, TARGET_LONG_BITS - 8);
        } else {
            /*
             * The _previous_ value is deposited into bits 8 and higher of t.  Because
             * those bits are known to be zero after ld8u, this becomes a shift+or
             * if deposit is not available.
             */
            tcg_gen_ld8u_tl(t, cpu_env, offsetof(CPUX86State, xmm_t0.ZMM_B(vec_len - 1)));
            tcg_gen_deposit_tl(s->T0, t, s->T0, 8, TARGET_LONG_BITS - 8);
        }
    }
    tcg_temp_free(t);
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

static void gen_STMXCSR(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    if (s->vex_l) {
        gen_illegal_opcode(s);
        return;
    }
    gen_helper_update_mxcsr(cpu_env);
    tcg_gen_ld32u_tl(s->T0, cpu_env, offsetof(CPUX86State, mxcsr));
}

static void gen_VAESIMC(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    assert(!s->vex_l);
    gen_helper_aesimc_xmm(cpu_env, OP_PTR0, OP_PTR2);
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

static void gen_VCMP(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    int index = decode->immediate & (s->prefix & PREFIX_VEX ? 31 : 7);
    int b =
        s->prefix & PREFIX_REPZ  ? 2 /* ss */ :
        s->prefix & PREFIX_REPNZ ? 3 /* sd */ :
        !!(s->prefix & PREFIX_DATA) /* pd */ + (s->vex_l << 2);

    gen_helper_cmp_funcs[index][b](cpu_env, OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_VCOMI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    SSEFunc_0_epp fn;
    fn = s->prefix & PREFIX_DATA ? gen_helper_comisd : gen_helper_comiss;
    fn(cpu_env, OP_PTR1, OP_PTR2);
    set_cc_op(s, CC_OP_EFLAGS);
}

static void gen_VCVTfp2fp(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_unary_fp_sse(s, env, decode,
                     gen_helper_cvtpd2ps_xmm, gen_helper_cvtps2pd_xmm,
                     gen_helper_cvtpd2ps_ymm, gen_helper_cvtps2pd_ymm,
                     gen_helper_cvtsd2ss, gen_helper_cvtss2sd);
}

static void gen_VCVTPS2PH(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_unary_imm_fp_sse(s, env, decode,
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

static void gen_VCVTSI2Sx(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);
    TCGv_i32 in;

    tcg_gen_gvec_mov(MO_64, decode->op[0].offset, decode->op[1].offset, vec_len, vec_len);

#ifdef TARGET_X86_64
    MemOp ot = decode->op[2].ot;
    if (ot == MO_64) {
        if (s->prefix & PREFIX_REPNZ) {
            gen_helper_cvtsq2sd(cpu_env, OP_PTR0, s->T1);
        } else {
            gen_helper_cvtsq2ss(cpu_env, OP_PTR0, s->T1);
        }
        return;
    }
    in = s->tmp2_i32;
    tcg_gen_trunc_tl_i32(in, s->T1);
#else
    in = s->T1;
#endif

    if (s->prefix & PREFIX_REPNZ) {
        gen_helper_cvtsi2sd(cpu_env, OP_PTR0, in);
    } else {
        gen_helper_cvtsi2ss(cpu_env, OP_PTR0, in);
    }
}

static inline void gen_VCVTtSx2SI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                                  SSEFunc_i_ep ss2si, SSEFunc_l_ep ss2sq,
                                  SSEFunc_i_ep sd2si, SSEFunc_l_ep sd2sq)
{
    TCGv_i32 out;

#ifdef TARGET_X86_64
    MemOp ot = decode->op[0].ot;
    if (ot == MO_64) {
        if (s->prefix & PREFIX_REPNZ) {
            sd2sq(s->T0, cpu_env, OP_PTR2);
        } else {
            ss2sq(s->T0, cpu_env, OP_PTR2);
        }
        return;
    }

    out = s->tmp2_i32;
#else
    out = s->T0;
#endif
    if (s->prefix & PREFIX_REPNZ) {
        sd2si(out, cpu_env, OP_PTR2);
    } else {
        ss2si(out, cpu_env, OP_PTR2);
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

static void gen_VCVTSx2SI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_VCVTtSx2SI(s, env, decode,
                   gen_helper_cvtss2si, gen_helper_cvtss2sq,
                   gen_helper_cvtsd2si, gen_helper_cvtsd2sq);
}

static void gen_VCVTTSx2SI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_VCVTtSx2SI(s, env, decode,
                   gen_helper_cvttss2si, gen_helper_cvttss2sq,
                   gen_helper_cvttsd2si, gen_helper_cvttsd2sq);
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

static inline void gen_maskmov(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode,
                               SSEFunc_0_eppt xmm, SSEFunc_0_eppt ymm)
{
    if (!s->vex_l) {
        xmm(cpu_env, OP_PTR2, OP_PTR1, s->A0);
    } else {
        ymm(cpu_env, OP_PTR2, OP_PTR1, s->A0);
    }
}

static void gen_VMASKMOVPD_st(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_maskmov(s, env, decode, gen_helper_vpmaskmovq_st_xmm, gen_helper_vpmaskmovq_st_ymm);
}

static void gen_VMASKMOVPS_st(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_maskmov(s, env, decode, gen_helper_vpmaskmovd_st_xmm, gen_helper_vpmaskmovd_st_ymm);
}

static void gen_VMOVHPx_ld(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_ldq_env_A0(s, decode->op[0].offset + offsetof(XMMReg, XMM_Q(1)));
    if (decode->op[0].offset != decode->op[1].offset) {
        tcg_gen_ld_i64(s->tmp1_i64, cpu_env, decode->op[1].offset + offsetof(XMMReg, XMM_Q(0)));
        tcg_gen_st_i64(s->tmp1_i64, cpu_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(0)));
    }
}

static void gen_VMOVHPx_st(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    gen_stq_env_A0(s, decode->op[2].offset + offsetof(XMMReg, XMM_Q(1)));
}

static void gen_VMOVHPx(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    if (decode->op[0].offset != decode->op[2].offset) {
        tcg_gen_ld_i64(s->tmp1_i64, cpu_env, decode->op[2].offset + offsetof(XMMReg, XMM_Q(1)));
        tcg_gen_st_i64(s->tmp1_i64, cpu_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(1)));
    }
    if (decode->op[0].offset != decode->op[1].offset) {
        tcg_gen_ld_i64(s->tmp1_i64, cpu_env, decode->op[1].offset + offsetof(XMMReg, XMM_Q(0)));
        tcg_gen_st_i64(s->tmp1_i64, cpu_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(0)));
    }
}

static void gen_VMOVHLPS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    tcg_gen_ld_i64(s->tmp1_i64, cpu_env, decode->op[2].offset + offsetof(XMMReg, XMM_Q(1)));
    tcg_gen_st_i64(s->tmp1_i64, cpu_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(0)));
    if (decode->op[0].offset != decode->op[1].offset) {
        tcg_gen_ld_i64(s->tmp1_i64, cpu_env, decode->op[1].offset + offsetof(XMMReg, XMM_Q(1)));
        tcg_gen_st_i64(s->tmp1_i64, cpu_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(1)));
    }
}

static void gen_VMOVLHPS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    tcg_gen_ld_i64(s->tmp1_i64, cpu_env, decode->op[2].offset);
    tcg_gen_st_i64(s->tmp1_i64, cpu_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(1)));
    if (decode->op[0].offset != decode->op[1].offset) {
        tcg_gen_ld_i64(s->tmp1_i64, cpu_env, decode->op[1].offset + offsetof(XMMReg, XMM_Q(0)));
        tcg_gen_st_i64(s->tmp1_i64, cpu_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(0)));
    }
}

/*
 * Note that MOVLPx supports 256-bit operation unlike MOVHLPx, MOVLHPx, MOXHPx.
 * Use a gvec move to move everything above the bottom 64 bits.
 */

static void gen_VMOVLPx(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    tcg_gen_ld_i64(s->tmp1_i64, cpu_env, decode->op[2].offset + offsetof(XMMReg, XMM_Q(0)));
    tcg_gen_gvec_mov(MO_64, decode->op[0].offset, decode->op[1].offset, vec_len, vec_len);
    tcg_gen_st_i64(s->tmp1_i64, cpu_env, decode->op[0].offset + offsetof(XMMReg, XMM_Q(0)));
}

static void gen_VMOVLPx_ld(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    tcg_gen_qemu_ld_i64(s->tmp1_i64, s->A0, s->mem_index, MO_LEUQ);
    tcg_gen_gvec_mov(MO_64, decode->op[0].offset, decode->op[1].offset, vec_len, vec_len);
    tcg_gen_st_i64(s->tmp1_i64, OP_PTR0, offsetof(ZMMReg, ZMM_Q(0)));
}

static void gen_VMOVLPx_st(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    tcg_gen_ld_i64(s->tmp1_i64, OP_PTR2, offsetof(ZMMReg, ZMM_Q(0)));
    tcg_gen_qemu_st_i64(s->tmp1_i64, s->A0, s->mem_index, MO_LEUQ);
}

static void gen_VMOVSD_ld(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGv_i64 zero = tcg_constant_i64(0);

    tcg_gen_qemu_ld_i64(s->tmp1_i64, s->A0, s->mem_index, MO_LEUQ);
    tcg_gen_st_i64(zero, OP_PTR0, offsetof(ZMMReg, ZMM_Q(1)));
    tcg_gen_st_i64(s->tmp1_i64, OP_PTR0, offsetof(ZMMReg, ZMM_Q(0)));
}

static void gen_VMOVSS(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    tcg_gen_ld_i32(s->tmp2_i32, OP_PTR2, offsetof(ZMMReg, ZMM_L(0)));
    tcg_gen_gvec_mov(MO_64, decode->op[0].offset, decode->op[1].offset, vec_len, vec_len);
    tcg_gen_st_i32(s->tmp2_i32, OP_PTR0, offsetof(ZMMReg, ZMM_L(0)));
}

static void gen_VMOVSS_ld(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    int vec_len = vector_len(s, decode);

    tcg_gen_qemu_ld_i32(s->tmp2_i32, s->A0, s->mem_index, MO_LEUL);
    tcg_gen_gvec_dup_imm(MO_64, decode->op[0].offset, vec_len, vec_len, 0);
    tcg_gen_st_i32(s->tmp2_i32, OP_PTR0, offsetof(ZMMReg, ZMM_L(0)));
}

static void gen_VMOVSS_st(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    tcg_gen_ld_i32(s->tmp2_i32, OP_PTR2, offsetof(ZMMReg, ZMM_L(0)));
    tcg_gen_qemu_st_i32(s->tmp2_i32, s->A0, s->mem_index, MO_LEUL);
}

static void gen_VPMASKMOV_st(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    if (s->vex_w) {
        gen_VMASKMOVPD_st(s, env, decode);
    } else {
        gen_VMASKMOVPS_st(s, env, decode);
    }
}

static void gen_VPERMD(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    assert(s->vex_l);
    gen_helper_vpermd_ymm(OP_PTR0, OP_PTR1, OP_PTR2);
}

static void gen_VPERM2x128(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant8u_i32(decode->immediate);
    assert(s->vex_l);
    gen_helper_vpermdq_ymm(OP_PTR0, OP_PTR1, OP_PTR2, imm);
}

static void gen_VPHMINPOSUW(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    assert(!s->vex_l);
    gen_helper_phminposuw_xmm(cpu_env, OP_PTR0, OP_PTR2);
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

static void gen_VSHUF(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    TCGv_i32 imm = tcg_constant_i32(decode->immediate);
    SSEFunc_0_pppi ps, pd, fn;
    ps = s->vex_l ? gen_helper_shufps_ymm : gen_helper_shufps_xmm;
    pd = s->vex_l ? gen_helper_shufpd_ymm : gen_helper_shufpd_xmm;
    fn = s->prefix & PREFIX_DATA ? pd : ps;
    fn(OP_PTR0, OP_PTR1, OP_PTR2, imm);
}

static void gen_VUCOMI(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode)
{
    SSEFunc_0_epp fn;
    fn = s->prefix & PREFIX_DATA ? gen_helper_ucomisd : gen_helper_ucomiss;
    fn(cpu_env, OP_PTR1, OP_PTR2);
    set_cc_op(s, CC_OP_EFLAGS);
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
