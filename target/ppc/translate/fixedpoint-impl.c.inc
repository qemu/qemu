/*
 * Power ISA decode for Fixed-Point Facility instructions
 *
 * Copyright (c) 2021 Instituto de Pesquisas Eldorado (eldorado.org.br)
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
 * Fixed-Point Load/Store Instructions
 */

static bool do_ldst(DisasContext *ctx, int rt, int ra, TCGv displ, bool update,
                    bool store, MemOp mop)
{
    TCGv ea;

    if (update && (ra == 0 || (!store && ra == rt))) {
        gen_invalid(ctx);
        return true;
    }
    gen_set_access_type(ctx, ACCESS_INT);

    ea = do_ea_calc(ctx, ra, displ);
    mop ^= ctx->default_tcg_memop_mask;
    if (store) {
        tcg_gen_qemu_st_tl(cpu_gpr[rt], ea, ctx->mem_idx, mop);
    } else {
        tcg_gen_qemu_ld_tl(cpu_gpr[rt], ea, ctx->mem_idx, mop);
    }
    if (update) {
        tcg_gen_mov_tl(cpu_gpr[ra], ea);
    }
    return true;
}

static bool do_ldst_D(DisasContext *ctx, arg_D *a, bool update, bool store,
                      MemOp mop)
{
    return do_ldst(ctx, a->rt, a->ra, tcg_constant_tl(a->si), update, store, mop);
}

static bool do_ldst_PLS_D(DisasContext *ctx, arg_PLS_D *a, bool update,
                          bool store, MemOp mop)
{
    arg_D d;
    if (!resolve_PLS_D(ctx, &d, a)) {
        return true;
    }
    return do_ldst_D(ctx, &d, update, store, mop);
}

static bool do_ldst_X(DisasContext *ctx, arg_X *a, bool update,
                      bool store, MemOp mop)
{
    return do_ldst(ctx, a->rt, a->ra, cpu_gpr[a->rb], update, store, mop);
}

static bool do_ldst_quad(DisasContext *ctx, arg_D *a, bool store, bool prefixed)
{
#if defined(TARGET_PPC64)
    TCGv ea;
    TCGv_i64 lo, hi;
    TCGv_i128 t16;

    REQUIRE_INSNS_FLAGS(ctx, 64BX);

    if (!prefixed && !(ctx->insns_flags2 & PPC2_LSQ_ISA207)) {
        /* lq and stq were privileged prior to V. 2.07 */
        REQUIRE_SV(ctx);

        if (ctx->le_mode) {
            gen_align_no_le(ctx);
            return true;
        }
    }

    if (!store && unlikely(a->ra == a->rt)) {
        gen_invalid(ctx);
        return true;
    }

    gen_set_access_type(ctx, ACCESS_INT);
    ea = do_ea_calc(ctx, a->ra, tcg_constant_tl(a->si));

    if (ctx->le_mode && prefixed) {
        lo = cpu_gpr[a->rt];
        hi = cpu_gpr[a->rt + 1];
    } else {
        lo = cpu_gpr[a->rt + 1];
        hi = cpu_gpr[a->rt];
    }
    t16 = tcg_temp_new_i128();

    if (store) {
        tcg_gen_concat_i64_i128(t16, lo, hi);
        tcg_gen_qemu_st_i128(t16, ea, ctx->mem_idx, DEF_MEMOP(MO_128));
    } else {
        tcg_gen_qemu_ld_i128(t16, ea, ctx->mem_idx, DEF_MEMOP(MO_128));
        tcg_gen_extr_i128_i64(lo, hi, t16);
    }
#else
    qemu_build_not_reached();
#endif

    return true;
}

static bool do_ldst_quad_PLS_D(DisasContext *ctx, arg_PLS_D *a, bool store)
{
    arg_D d;
    if (!resolve_PLS_D(ctx, &d, a)) {
        return true;
    }

    return do_ldst_quad(ctx, &d, store, true);
}

/* Load Byte and Zero */
TRANS(LBZ, do_ldst_D, false, false, MO_UB)
TRANS(LBZX, do_ldst_X, false, false, MO_UB)
TRANS(LBZU, do_ldst_D, true, false, MO_UB)
TRANS(LBZUX, do_ldst_X, true, false, MO_UB)
TRANS(PLBZ, do_ldst_PLS_D, false, false, MO_UB)

/* Load Halfword and Zero */
TRANS(LHZ, do_ldst_D, false, false, MO_UW)
TRANS(LHZX, do_ldst_X, false, false, MO_UW)
TRANS(LHZU, do_ldst_D, true, false, MO_UW)
TRANS(LHZUX, do_ldst_X, true, false, MO_UW)
TRANS(PLHZ, do_ldst_PLS_D, false, false, MO_UW)

/* Load Halfword Algebraic */
TRANS(LHA, do_ldst_D, false, false, MO_SW)
TRANS(LHAX, do_ldst_X, false, false, MO_SW)
TRANS(LHAU, do_ldst_D, true, false, MO_SW)
TRANS(LHAXU, do_ldst_X, true, false, MO_SW)
TRANS(PLHA, do_ldst_PLS_D, false, false, MO_SW)

/* Load Word and Zero */
TRANS(LWZ, do_ldst_D, false, false, MO_UL)
TRANS(LWZX, do_ldst_X, false, false, MO_UL)
TRANS(LWZU, do_ldst_D, true, false, MO_UL)
TRANS(LWZUX, do_ldst_X, true, false, MO_UL)
TRANS(PLWZ, do_ldst_PLS_D, false, false, MO_UL)

/* Load Word Algebraic */
TRANS64(LWA, do_ldst_D, false, false, MO_SL)
TRANS64(LWAX, do_ldst_X, false, false, MO_SL)
TRANS64(LWAUX, do_ldst_X, true, false, MO_SL)
TRANS64(PLWA, do_ldst_PLS_D, false, false, MO_SL)

/* Load Doubleword */
TRANS64(LD, do_ldst_D, false, false, MO_UQ)
TRANS64(LDX, do_ldst_X, false, false, MO_UQ)
TRANS64(LDU, do_ldst_D, true, false, MO_UQ)
TRANS64(LDUX, do_ldst_X, true, false, MO_UQ)
TRANS64(PLD, do_ldst_PLS_D, false, false, MO_UQ)

/* Load Quadword */
TRANS64(LQ, do_ldst_quad, false, false);
TRANS64(PLQ, do_ldst_quad_PLS_D, false);

/* Store Byte */
TRANS(STB, do_ldst_D, false, true, MO_UB)
TRANS(STBX, do_ldst_X, false, true, MO_UB)
TRANS(STBU, do_ldst_D, true, true, MO_UB)
TRANS(STBUX, do_ldst_X, true, true, MO_UB)
TRANS(PSTB, do_ldst_PLS_D, false, true, MO_UB)

/* Store Halfword */
TRANS(STH, do_ldst_D, false, true, MO_UW)
TRANS(STHX, do_ldst_X, false, true, MO_UW)
TRANS(STHU, do_ldst_D, true, true, MO_UW)
TRANS(STHUX, do_ldst_X, true, true, MO_UW)
TRANS(PSTH, do_ldst_PLS_D, false, true, MO_UW)

/* Store Word */
TRANS(STW, do_ldst_D, false, true, MO_UL)
TRANS(STWX, do_ldst_X, false, true, MO_UL)
TRANS(STWU, do_ldst_D, true, true, MO_UL)
TRANS(STWUX, do_ldst_X, true, true, MO_UL)
TRANS(PSTW, do_ldst_PLS_D, false, true, MO_UL)

/* Store Doubleword */
TRANS64(STD, do_ldst_D, false, true, MO_UQ)
TRANS64(STDX, do_ldst_X, false, true, MO_UQ)
TRANS64(STDU, do_ldst_D, true, true, MO_UQ)
TRANS64(STDUX, do_ldst_X, true, true, MO_UQ)
TRANS64(PSTD, do_ldst_PLS_D, false, true, MO_UQ)

/* Store Quadword */
TRANS64(STQ, do_ldst_quad, true, false);
TRANS64(PSTQ, do_ldst_quad_PLS_D, true);

/*
 * Fixed-Point Compare Instructions
 */

static bool do_cmp_X(DisasContext *ctx, arg_X_bfl *a, bool s)
{
    if ((ctx->insns_flags & PPC_64B) == 0) {
        /*
         * For 32-bit implementations, The Programming Environments Manual says
         * that "the L field must be cleared, otherwise the instruction form is
         * invalid." It seems, however, that most 32-bit CPUs ignore invalid
         * forms (e.g., section "Instruction Formats" of the 405 and 440
         * manuals, "Integer Compare Instructions" of the 601 manual), with the
         * notable exception of the e500 and e500mc, where L=1 was reported to
         * cause an exception.
         */
        if (a->l) {
            if ((ctx->insns_flags2 & PPC2_BOOKE206)) {
                /*
                 * For 32-bit Book E v2.06 implementations (i.e. e500/e500mc),
                 * generate an illegal instruction exception.
                 */
                return false;
            } else {
                qemu_log_mask(LOG_GUEST_ERROR,
                        "Invalid form of CMP%s at 0x" TARGET_FMT_lx ", L = 1\n",
                        s ? "" : "L", ctx->cia);
            }
        }
        gen_op_cmp32(cpu_gpr[a->ra], cpu_gpr[a->rb], s, a->bf);
        return true;
    }

    /* For 64-bit implementations, deal with bit L accordingly. */
    if (a->l) {
        gen_op_cmp(cpu_gpr[a->ra], cpu_gpr[a->rb], s, a->bf);
    } else {
        gen_op_cmp32(cpu_gpr[a->ra], cpu_gpr[a->rb], s, a->bf);
    }
    return true;
}

static bool do_cmp_D(DisasContext *ctx, arg_D_bf *a, bool s)
{
    if ((ctx->insns_flags & PPC_64B) == 0) {
        /*
         * For 32-bit implementations, The Programming Environments Manual says
         * that "the L field must be cleared, otherwise the instruction form is
         * invalid." It seems, however, that most 32-bit CPUs ignore invalid
         * forms (e.g., section "Instruction Formats" of the 405 and 440
         * manuals, "Integer Compare Instructions" of the 601 manual), with the
         * notable exception of the e500 and e500mc, where L=1 was reported to
         * cause an exception.
         */
        if (a->l) {
            if ((ctx->insns_flags2 & PPC2_BOOKE206)) {
                /*
                 * For 32-bit Book E v2.06 implementations (i.e. e500/e500mc),
                 * generate an illegal instruction exception.
                 */
                return false;
            } else {
                qemu_log_mask(LOG_GUEST_ERROR,
                        "Invalid form of CMP%s at 0x" TARGET_FMT_lx ", L = 1\n",
                        s ? "I" : "LI", ctx->cia);
            }
        }
        gen_op_cmp32(cpu_gpr[a->ra], tcg_constant_tl(a->imm), s, a->bf);
        return true;
    }

    /* For 64-bit implementations, deal with bit L accordingly. */
    if (a->l) {
        gen_op_cmp(cpu_gpr[a->ra], tcg_constant_tl(a->imm), s, a->bf);
    } else {
        gen_op_cmp32(cpu_gpr[a->ra], tcg_constant_tl(a->imm), s, a->bf);
    }
    return true;
}

TRANS(CMP, do_cmp_X, true);
TRANS(CMPL, do_cmp_X, false);
TRANS(CMPI, do_cmp_D, true);
TRANS(CMPLI, do_cmp_D, false);

static bool trans_CMPRB(DisasContext *ctx, arg_CMPRB *a)
{
    TCGv_i32 src1 = tcg_temp_new_i32();
    TCGv_i32 src2 = tcg_temp_new_i32();
    TCGv_i32 src2lo = tcg_temp_new_i32();
    TCGv_i32 src2hi = tcg_temp_new_i32();
    TCGv_i32 crf = cpu_crf[a->bf];

    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    tcg_gen_trunc_tl_i32(src1, cpu_gpr[a->ra]);
    tcg_gen_trunc_tl_i32(src2, cpu_gpr[a->rb]);

    tcg_gen_andi_i32(src1, src1, 0xFF);
    tcg_gen_ext8u_i32(src2lo, src2);
    tcg_gen_extract_i32(src2hi, src2, 8, 8);

    tcg_gen_setcond_i32(TCG_COND_LEU, src2lo, src2lo, src1);
    tcg_gen_setcond_i32(TCG_COND_LEU, src2hi, src1, src2hi);
    tcg_gen_and_i32(crf, src2lo, src2hi);

    if (a->l) {
        tcg_gen_extract_i32(src2lo, src2, 16, 8);
        tcg_gen_extract_i32(src2hi, src2, 24, 8);
        tcg_gen_setcond_i32(TCG_COND_LEU, src2lo, src2lo, src1);
        tcg_gen_setcond_i32(TCG_COND_LEU, src2hi, src1, src2hi);
        tcg_gen_and_i32(src2lo, src2lo, src2hi);
        tcg_gen_or_i32(crf, crf, src2lo);
    }
    tcg_gen_shli_i32(crf, crf, CRF_GT_BIT);
    return true;
}

static bool trans_CMPEQB(DisasContext *ctx, arg_CMPEQB *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
#if defined(TARGET_PPC64)
    gen_helper_CMPEQB(cpu_crf[a->bf], cpu_gpr[a->ra], cpu_gpr[a->rb]);
#else
    qemu_build_not_reached();
#endif
    return true;
}

/*
 * Fixed-Point Arithmetic Instructions
 */

static bool trans_ADDI(DisasContext *ctx, arg_D *a)
{
    if (a->ra) {
        tcg_gen_addi_tl(cpu_gpr[a->rt], cpu_gpr[a->ra], a->si);
    } else {
        tcg_gen_movi_tl(cpu_gpr[a->rt], a->si);
    }
    return true;
}

static bool trans_PADDI(DisasContext *ctx, arg_PLS_D *a)
{
    arg_D d;
    if (!resolve_PLS_D(ctx, &d, a)) {
        return true;
    }
    return trans_ADDI(ctx, &d);
}

static bool trans_ADDIS(DisasContext *ctx, arg_D *a)
{
    a->si <<= 16;
    return trans_ADDI(ctx, a);
}

static bool trans_ADDPCIS(DisasContext *ctx, arg_DX *a)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    tcg_gen_movi_tl(cpu_gpr[a->rt], ctx->base.pc_next + (a->d << 16));
    return true;
}

static bool trans_ADDEX(DisasContext *ctx, arg_X *a)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    gen_op_arith_add(ctx, cpu_gpr[a->rt], cpu_gpr[a->ra], cpu_gpr[a->rb],
                     cpu_ov, cpu_ov32, true, true, false, false);
    return true;
}

static bool do_add_D(DisasContext *ctx, arg_D *a, bool add_ca, bool compute_ca,
                     bool compute_ov, bool compute_rc0)
{
    gen_op_arith_add(ctx, cpu_gpr[a->rt], cpu_gpr[a->ra],
                     tcg_constant_tl(a->si), cpu_ca, cpu_ca32,
                     add_ca, compute_ca, compute_ov, compute_rc0);
    return true;
}

static bool do_add_XO(DisasContext *ctx, arg_XO *a, bool add_ca,
                      bool compute_ca)
{
    gen_op_arith_add(ctx, cpu_gpr[a->rt], cpu_gpr[a->ra], cpu_gpr[a->rb],
                     cpu_ca, cpu_ca32, add_ca, compute_ca, a->oe, a->rc);
    return true;
}

static bool do_add_const_XO(DisasContext *ctx, arg_XO_ta *a, TCGv const_val,
                            bool add_ca, bool compute_ca)
{
    gen_op_arith_add(ctx, cpu_gpr[a->rt], cpu_gpr[a->ra], const_val,
                     cpu_ca, cpu_ca32, add_ca, compute_ca, a->oe, a->rc);
    return true;
}

TRANS(ADD, do_add_XO, false, false);
TRANS(ADDC, do_add_XO, false, true);
TRANS(ADDE, do_add_XO, true, true);
TRANS(ADDME, do_add_const_XO, tcg_constant_tl(-1LL), true, true);
TRANS(ADDZE, do_add_const_XO, tcg_constant_tl(0), true, true);
TRANS(ADDIC, do_add_D, false, true, false, false);
TRANS(ADDIC_, do_add_D, false, true, false, true);

static bool trans_SUBFIC(DisasContext *ctx, arg_D *a)
{
    gen_op_arith_subf(ctx, cpu_gpr[a->rt], cpu_gpr[a->ra],
                      tcg_constant_tl(a->si), false, true, false, false);
    return true;
}

static bool do_subf_XO(DisasContext *ctx, arg_XO *a, bool add_ca,
                       bool compute_ca)
{
    gen_op_arith_subf(ctx, cpu_gpr[a->rt], cpu_gpr[a->ra], cpu_gpr[a->rb],
                      add_ca, compute_ca, a->oe, a->rc);
    return true;
}

static bool do_subf_const_XO(DisasContext *ctx, arg_XO_ta *a, TCGv const_val,
                             bool add_ca, bool compute_ca)
{
    gen_op_arith_subf(ctx, cpu_gpr[a->rt], cpu_gpr[a->ra], const_val,
                      add_ca, compute_ca, a->oe, a->rc);
    return true;
}

TRANS(SUBF, do_subf_XO, false, false)
TRANS(SUBFC, do_subf_XO, false, true)
TRANS(SUBFE, do_subf_XO, true, true)
TRANS(SUBFME, do_subf_const_XO, tcg_constant_tl(-1LL), true, true)
TRANS(SUBFZE, do_subf_const_XO, tcg_constant_tl(0), true, true)

static bool trans_MULLI(DisasContext *ctx, arg_MULLI *a)
{
    tcg_gen_muli_tl(cpu_gpr[a->rt], cpu_gpr[a->ra], a->si);
    return true;
}

static bool trans_MULLW(DisasContext *ctx, arg_MULLW *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    tcg_gen_ext32s_tl(t0, cpu_gpr[a->ra]);
    tcg_gen_ext32s_tl(t1, cpu_gpr[a->rb]);
    tcg_gen_mul_tl(cpu_gpr[a->rt], t0, t1);
    if (unlikely(a->rc)) {
        gen_set_Rc0(ctx, cpu_gpr[a->rt]);
    }
    return true;
}

static bool trans_MULLWO(DisasContext *ctx, arg_MULLWO *a)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

#if defined(TARGET_PPC64)
    tcg_gen_ext32s_i64(t0, cpu_gpr[a->ra]);
    tcg_gen_ext32s_i64(t1, cpu_gpr[a->rb]);
    tcg_gen_mul_i64(cpu_gpr[a->rt], t0, t1);
    tcg_gen_sextract_i64(t0, cpu_gpr[a->rt], 31, 1);
    tcg_gen_sari_i64(t1, cpu_gpr[a->rt], 32);
#else
    tcg_gen_muls2_i32(cpu_gpr[a->rt], t1, cpu_gpr[a->ra], cpu_gpr[a->rb]);
    tcg_gen_sari_i32(t0, cpu_gpr[a->rt], 31);
#endif
    tcg_gen_setcond_tl(TCG_COND_NE, cpu_ov, t0, t1);
    if (is_isa300(ctx)) {
        tcg_gen_mov_tl(cpu_ov32, cpu_ov);
    }
    tcg_gen_or_tl(cpu_so, cpu_so, cpu_ov);

    if (unlikely(a->rc)) {
        gen_set_Rc0(ctx, cpu_gpr[a->rt]);
    }
    return true;
}

static bool do_mulhw(DisasContext *ctx, arg_XO_tab_rc *a,
                     void (*helper)(TCGv_i32 rl, TCGv_i32 rh, TCGv_i32 arg1,
                                    TCGv_i32 arg2))
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_trunc_tl_i32(t0, cpu_gpr[a->ra]);
    tcg_gen_trunc_tl_i32(t1, cpu_gpr[a->rb]);
    helper(t0, t1, t0, t1);
    tcg_gen_extu_i32_tl(cpu_gpr[a->rt], t1);
    if (unlikely(a->rc)) {
        gen_set_Rc0(ctx, cpu_gpr[a->rt]);
    }
    return true;
}

TRANS(MULHW, do_mulhw, tcg_gen_muls2_i32)
TRANS(MULHWU, do_mulhw, tcg_gen_mulu2_i32)

static bool do_divw(DisasContext *ctx, arg_XO *a, int sign)
{
    gen_op_arith_divw(ctx, cpu_gpr[a->rt], cpu_gpr[a->ra], cpu_gpr[a->rb],
                      sign, a->oe, a->rc);
    return true;
}

static bool do_dive(DisasContext *ctx, arg_XO *a,
                     void (*helper)(TCGv, TCGv_ptr, TCGv, TCGv, TCGv_i32))
{
    REQUIRE_INSNS_FLAGS2(ctx, DIVE_ISA206);
    helper(cpu_gpr[a->rt], tcg_env, cpu_gpr[a->ra], cpu_gpr[a->rb],
           tcg_constant_i32(a->oe));
    if (unlikely(a->rc)) {
        gen_set_Rc0(ctx, cpu_gpr[a->rt]);
    }
    return true;
}

TRANS(DIVW, do_divw, 1);
TRANS(DIVWU, do_divw, 0);
TRANS(DIVWE, do_dive, gen_helper_DIVWE);
TRANS(DIVWEU, do_dive, gen_helper_DIVWEU);

static bool do_modw(DisasContext *ctx, arg_X *a, bool sign)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    gen_op_arith_modw(ctx, cpu_gpr[a->rt], cpu_gpr[a->ra], cpu_gpr[a->rb],
                      sign);
    return true;
}

TRANS(MODUW, do_modw, false);
TRANS(MODSW, do_modw, true);

static bool trans_NEG(DisasContext *ctx, arg_NEG *a)
{
    if (a->oe) {
        TCGv zero = tcg_constant_tl(0);
        gen_op_arith_subf(ctx, cpu_gpr[a->rt], cpu_gpr[a->ra], zero,
                          false, false, true, a->rc);
    } else {
        tcg_gen_neg_tl(cpu_gpr[a->rt], cpu_gpr[a->ra]);
        if (unlikely(a->rc)) {
            gen_set_Rc0(ctx, cpu_gpr[a->rt]);
        }
    }
    return true;
}

static bool trans_DARN(DisasContext *ctx, arg_DARN *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
#if defined(TARGET_PPC64)
    if (a->l > 2) {
        tcg_gen_movi_i64(cpu_gpr[a->rt], -1);
    } else {
        translator_io_start(&ctx->base);
        if (a->l == 0) {
            gen_helper_DARN32(cpu_gpr[a->rt]);
        } else {
            /* Return 64-bit random for both CRN and RRN */
            gen_helper_DARN64(cpu_gpr[a->rt]);
        }
    }
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_MULLD(DisasContext *ctx, arg_MULLD *a)
{
    REQUIRE_64BIT(ctx);
#if defined(TARGET_PPC64)
    tcg_gen_mul_tl(cpu_gpr[a->rt], cpu_gpr[a->ra], cpu_gpr[a->rb]);
    if (unlikely(a->rc)) {
        gen_set_Rc0(ctx, cpu_gpr[a->rt]);
    }
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_MULLDO(DisasContext *ctx, arg_MULLD *a)
{
    REQUIRE_64BIT(ctx);
#if defined(TARGET_PPC64)
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_muls2_i64(t0, t1, cpu_gpr[a->ra], cpu_gpr[a->rb]);
    tcg_gen_mov_i64(cpu_gpr[a->rt], t0);

    tcg_gen_sari_i64(t0, t0, 63);
    tcg_gen_setcond_i64(TCG_COND_NE, cpu_ov, t0, t1);
    if (is_isa300(ctx)) {
        tcg_gen_mov_tl(cpu_ov32, cpu_ov);
    }
    tcg_gen_or_tl(cpu_so, cpu_so, cpu_ov);

    if (unlikely(a->rc)) {
        gen_set_Rc0(ctx, cpu_gpr[a->rt]);
    }
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool do_mulhd(DisasContext *ctx, arg_XO_tab_rc *a,
                     void (*helper)(TCGv, TCGv, TCGv, TCGv))
{
    TCGv lo = tcg_temp_new();
    helper(lo, cpu_gpr[a->rt], cpu_gpr[a->ra], cpu_gpr[a->rb]);
    if (unlikely(a->rc)) {
        gen_set_Rc0(ctx, cpu_gpr[a->rt]);
    }
    return true;
}

TRANS64(MULHD, do_mulhd, tcg_gen_muls2_tl);
TRANS64(MULHDU, do_mulhd, tcg_gen_mulu2_tl);

static bool trans_MADDLD(DisasContext *ctx, arg_MADDLD *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
#if defined(TARGET_PPC64)
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_mul_i64(t1, cpu_gpr[a->vra], cpu_gpr[a->vrb]);
    tcg_gen_add_i64(cpu_gpr[a->vrt], t1, cpu_gpr[a->rc]);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_MADDHD(DisasContext *ctx, arg_MADDHD *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
#if defined(TARGET_PPC64)
    TCGv_i64 lo = tcg_temp_new_i64();
    TCGv_i64 hi = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_muls2_i64(lo, hi, cpu_gpr[a->vra], cpu_gpr[a->vrb]);
    tcg_gen_sari_i64(t1, cpu_gpr[a->rc], 63);
    tcg_gen_add2_i64(t1, cpu_gpr[a->vrt], lo, hi, cpu_gpr[a->rc], t1);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_MADDHDU(DisasContext *ctx, arg_MADDHDU *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
#if defined(TARGET_PPC64)
    TCGv_i64 lo = tcg_temp_new_i64();
    TCGv_i64 hi = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_mulu2_i64(lo, hi, cpu_gpr[a->vra], cpu_gpr[a->vrb]);
    tcg_gen_add2_i64(t1, cpu_gpr[a->vrt], lo, hi, cpu_gpr[a->rc],
                     tcg_constant_i64(0));
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool do_divd(DisasContext *ctx, arg_XO *a, bool sign)
{
    REQUIRE_64BIT(ctx);
#if defined(TARGET_PPC64)
    gen_op_arith_divd(ctx, cpu_gpr[a->rt], cpu_gpr[a->ra], cpu_gpr[a->rb],
                      sign, a->oe, a->rc);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool do_modd(DisasContext *ctx, arg_X *a, bool sign)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
#if defined(TARGET_PPC64)
    gen_op_arith_modd(ctx, cpu_gpr[a->rt], cpu_gpr[a->ra], cpu_gpr[a->rb],
                      sign);
#else
    qemu_build_not_reached();
#endif
    return true;
}

TRANS64(DIVD, do_divd, true);
TRANS64(DIVDU, do_divd, false);

static bool trans_DIVDE(DisasContext *ctx, arg_DIVDE *a)
{
    REQUIRE_64BIT(ctx);
#if defined(TARGET_PPC64)
    return do_dive(ctx, a, gen_helper_DIVDE);
#else
    qemu_build_not_reached();
#endif
}

static bool trans_DIVDEU(DisasContext *ctx, arg_DIVDEU *a)
{
    REQUIRE_64BIT(ctx);
#if defined(TARGET_PPC64)
    return do_dive(ctx, a, gen_helper_DIVDEU);
#else
    qemu_build_not_reached();
#endif
    return true;
}

TRANS64(MODSD, do_modd, true);
TRANS64(MODUD, do_modd, false);

/*
 * Fixed-Point Select Instructions
 */

static bool trans_ISEL(DisasContext *ctx, arg_ISEL *a)
{
    REQUIRE_INSNS_FLAGS(ctx, ISEL);
    uint32_t bi = a->bc;
    uint32_t mask = 0x08 >> (bi & 0x03);
    TCGv t0 = tcg_temp_new();
    TCGv zr;

    tcg_gen_extu_i32_tl(t0, cpu_crf[bi >> 2]);
    tcg_gen_andi_tl(t0, t0, mask);

    zr = tcg_constant_tl(0);
    tcg_gen_movcond_tl(TCG_COND_NE, cpu_gpr[a->rt], t0, zr,
                       a->ra ? cpu_gpr[a->ra] : zr,
                       cpu_gpr[a->rb]);
    return true;
}

/*
 * Fixed-Point Trap Instructions
 */

static bool trans_TW(DisasContext *ctx, arg_TW *a)
{
    TCGv_i32 t0;

    if (check_unconditional_trap(ctx, a->rt)) {
        return true;
    }
    t0 = tcg_constant_i32(a->rt);
    gen_helper_TW(tcg_env, cpu_gpr[a->ra], cpu_gpr[a->rb], t0);
    return true;
}

static bool trans_TWI(DisasContext *ctx, arg_TWI *a)
{
    TCGv t0;
    TCGv_i32 t1;

    if (check_unconditional_trap(ctx, a->rt)) {
        return true;
    }
    t0 = tcg_constant_tl(a->si);
    t1 = tcg_constant_i32(a->rt);
    gen_helper_TW(tcg_env, cpu_gpr[a->ra], t0, t1);
    return true;
}

static bool trans_TD(DisasContext *ctx, arg_TD *a)
{
    REQUIRE_64BIT(ctx);
#if defined(TARGET_PPC64)
    TCGv_i32 t0;

    if (check_unconditional_trap(ctx, a->rt)) {
        return true;
    }
    t0 = tcg_constant_i32(a->rt);
    gen_helper_TD(tcg_env, cpu_gpr[a->ra], cpu_gpr[a->rb], t0);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_TDI(DisasContext *ctx, arg_TDI *a)
{
    REQUIRE_64BIT(ctx);
#if defined(TARGET_PPC64)
    TCGv t0;
    TCGv_i32 t1;

    if (check_unconditional_trap(ctx, a->rt)) {
        return true;
    }
    t0 = tcg_constant_tl(a->si);
    t1 = tcg_constant_i32(a->rt);
    gen_helper_TD(tcg_env, cpu_gpr[a->ra], t0, t1);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_INVALID(DisasContext *ctx, arg_INVALID *a)
{
    gen_invalid(ctx);
    return true;
}

static bool trans_PNOP(DisasContext *ctx, arg_PNOP *a)
{
    return true;
}

static bool do_set_bool_cond(DisasContext *ctx, arg_X_bi *a, bool neg, bool rev)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
    uint32_t mask = 0x08 >> (a->bi & 0x03);
    TCGCond cond = rev ? TCG_COND_EQ : TCG_COND_NE;
    TCGv temp = tcg_temp_new();
    TCGv zero = tcg_constant_tl(0);

    tcg_gen_extu_i32_tl(temp, cpu_crf[a->bi >> 2]);
    tcg_gen_andi_tl(temp, temp, mask);
    if (neg) {
        tcg_gen_negsetcond_tl(cond, cpu_gpr[a->rt], temp, zero);
    } else {
        tcg_gen_setcond_tl(cond, cpu_gpr[a->rt], temp, zero);
    }
    return true;
}

TRANS(SETBC, do_set_bool_cond, false, false)
TRANS(SETBCR, do_set_bool_cond, false, true)
TRANS(SETNBC, do_set_bool_cond, true, false)
TRANS(SETNBCR, do_set_bool_cond, true, true)

/*
 * Fixed-Point Logical Instructions
 */

static bool do_addi_(DisasContext *ctx, arg_D_ui *a, bool shift)
{
    tcg_gen_andi_tl(cpu_gpr[a->ra], cpu_gpr[a->rt], shift ? a->ui << 16 : a->ui);
    gen_set_Rc0(ctx, cpu_gpr[a->ra]);
    return true;
}

static bool do_ori(DisasContext *ctx, arg_D_ui *a, bool shift)
{
    if (a->rt == a->ra && a->ui == 0) {
        /* NOP */
        return true;
    }
    tcg_gen_ori_tl(cpu_gpr[a->ra], cpu_gpr[a->rt], shift ? a->ui << 16 : a->ui);
    return true;
}

static bool do_xori(DisasContext *ctx, arg_D_ui *a, bool shift)
{
    if (a->rt == a->ra && a->ui == 0) {
        /* NOP */
        return true;
    }
    tcg_gen_xori_tl(cpu_gpr[a->ra], cpu_gpr[a->rt], shift ? a->ui << 16 : a->ui);
    return true;
}

static bool do_logical1(DisasContext *ctx, arg_X_sa_rc *a,
                        void (*helper)(TCGv, TCGv))
{
    helper(cpu_gpr[a->ra], cpu_gpr[a->rs]);
    if (unlikely(a->rc)) {
        gen_set_Rc0(ctx, cpu_gpr[a->ra]);
    }
    return true;
}

static bool do_logical2(DisasContext *ctx, arg_X_rc *a,
                        void (*helper)(TCGv, TCGv, TCGv))
{
    helper(cpu_gpr[a->ra], cpu_gpr[a->rt], cpu_gpr[a->rb]);
    if (unlikely(a->rc)) {
        gen_set_Rc0(ctx, cpu_gpr[a->ra]);
    }
    return true;
}

static bool trans_OR(DisasContext *ctx, arg_OR *a)
{
    /* Optimisation for mr. ri case */
    if (a->rt != a->ra || a->rt != a->rb) {
        if (a->rt != a->rb) {
            tcg_gen_or_tl(cpu_gpr[a->ra], cpu_gpr[a->rt], cpu_gpr[a->rb]);
        } else {
            tcg_gen_mov_tl(cpu_gpr[a->ra], cpu_gpr[a->rt]);
        }
        if (unlikely(a->rc)) {
            gen_set_Rc0(ctx, cpu_gpr[a->ra]);
        }
    } else if (unlikely(a->rc)) {
        gen_set_Rc0(ctx, cpu_gpr[a->rt]);
#if defined(TARGET_PPC64)
    } else if (a->rt != 0) { /* 0 is nop */
        int prio = 0;

        switch (a->rt) {
        case 1:
            /* Set process priority to low */
            prio = 2;
            break;
        case 6:
            /* Set process priority to medium-low */
            prio = 3;
            break;
        case 2:
            /* Set process priority to normal */
            prio = 4;
            break;
#if !defined(CONFIG_USER_ONLY)
        case 31:
            if (!ctx->pr) {
                /* Set process priority to very low */
                prio = 1;
            }
            break;
        case 5:
            if (!ctx->pr) {
                /* Set process priority to medium-hight */
                prio = 5;
            }
            break;
        case 3:
            if (!ctx->pr) {
                /* Set process priority to high */
                prio = 6;
            }
            break;
        case 7:
            if (ctx->hv && !ctx->pr) {
                /* Set process priority to very high */
                prio = 7;
            }
            break;
#endif
        default:
            break;
        }
        if (prio) {
            TCGv t0 = tcg_temp_new();
            gen_load_spr(t0, SPR_PPR);
            tcg_gen_andi_tl(t0, t0, ~0x001C000000000000ULL);
            tcg_gen_ori_tl(t0, t0, ((uint64_t)prio) << 50);
            gen_store_spr(SPR_PPR, t0);
        }
#if !defined(CONFIG_USER_ONLY)
        /*
         * Pause out of TCG otherwise spin loops with smt_low eat too
         * much CPU and the kernel hangs.  This applies to all
         * encodings other than no-op, e.g., miso(rs=26), yield(27),
         * mdoio(29), mdoom(30), and all currently undefined.
         */
        gen_pause(ctx);
#endif
#endif
    }

    return true;
}

static bool trans_XOR(DisasContext *ctx, arg_XOR *a)
{
    /* Optimisation for "set to zero" case */
    if (a->rt != a->rb) {
        tcg_gen_xor_tl(cpu_gpr[a->ra], cpu_gpr[a->rt], cpu_gpr[a->rb]);
    } else {
        tcg_gen_movi_tl(cpu_gpr[a->ra], 0);
    }
    if (unlikely(a->rc)) {
        gen_set_Rc0(ctx, cpu_gpr[a->ra]);
    }
    return true;
}

static bool trans_CMPB(DisasContext *ctx, arg_CMPB *a)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA205);
    gen_helper_CMPB(cpu_gpr[a->ra], cpu_gpr[a->rt], cpu_gpr[a->rb]);
    return true;
}

static bool do_cntzw(DisasContext *ctx, arg_X_sa_rc *a,
                    void (*helper)(TCGv_i32, TCGv_i32, uint32_t))
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(t, cpu_gpr[a->rs]);
    helper(t, t, 32);
    tcg_gen_extu_i32_tl(cpu_gpr[a->ra], t);

    if (unlikely(a->rc)) {
        gen_set_Rc0(ctx, cpu_gpr[a->ra]);
    }
    return true;
}

#if defined(TARGET_PPC64)
static bool do_cntzd(DisasContext *ctx, arg_X_sa_rc *a,
                    void (*helper)(TCGv_i64, TCGv_i64, uint64_t))
{
    helper(cpu_gpr[a->ra], cpu_gpr[a->rs], 64);
    if (unlikely(a->rc)) {
        gen_set_Rc0(ctx, cpu_gpr[a->ra]);
    }
    return true;
}
#endif

static bool trans_CNTLZD(DisasContext *ctx, arg_CNTLZD *a)
{
    REQUIRE_64BIT(ctx);
#if defined(TARGET_PPC64)
    do_cntzd(ctx, a, tcg_gen_clzi_i64);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_CNTTZD(DisasContext *ctx, arg_CNTTZD *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
#if defined(TARGET_PPC64)
    do_cntzd(ctx, a, tcg_gen_ctzi_i64);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_POPCNTB(DisasContext *ctx, arg_POPCNTB *a)
{
    REQUIRE_INSNS_FLAGS(ctx, POPCNTB);
    gen_helper_POPCNTB(cpu_gpr[a->ra], cpu_gpr[a->rs]);
    return true;
}

static bool trans_POPCNTW(DisasContext *ctx, arg_POPCNTW *a)
{
    REQUIRE_INSNS_FLAGS(ctx, POPCNTWD);
#if defined(TARGET_PPC64)
    gen_helper_POPCNTW(cpu_gpr[a->ra], cpu_gpr[a->rs]);
#else
    tcg_gen_ctpop_i32(cpu_gpr[a->ra], cpu_gpr[a->rs]);
#endif
    return true;
}

static bool trans_POPCNTD(DisasContext *ctx, arg_POPCNTD *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS(ctx, POPCNTWD);
#if defined(TARGET_PPC64)
    tcg_gen_ctpop_i64(cpu_gpr[a->ra], cpu_gpr[a->rs]);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_PRTYW(DisasContext *ctx, arg_PRTYW *a)
{
    TCGv ra = cpu_gpr[a->ra];
    TCGv rs = cpu_gpr[a->rs];
    TCGv t0 = tcg_temp_new();

    REQUIRE_INSNS_FLAGS2(ctx, ISA205);
    tcg_gen_shri_tl(t0, rs, 16);
    tcg_gen_xor_tl(ra, rs, t0);
    tcg_gen_shri_tl(t0, ra, 8);
    tcg_gen_xor_tl(ra, ra, t0);
    tcg_gen_andi_tl(ra, ra, (target_ulong)0x100000001ULL);
    return true;
}

static bool trans_PRTYD(DisasContext *ctx, arg_PRTYD *a)
{
    TCGv ra = cpu_gpr[a->ra];
    TCGv rs = cpu_gpr[a->rs];
    TCGv t0 = tcg_temp_new();

    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA205);
    tcg_gen_shri_tl(t0, rs, 32);
    tcg_gen_xor_tl(ra, rs, t0);
    tcg_gen_shri_tl(t0, ra, 16);
    tcg_gen_xor_tl(ra, ra, t0);
    tcg_gen_shri_tl(t0, ra, 8);
    tcg_gen_xor_tl(ra, ra, t0);
    tcg_gen_andi_tl(ra, ra, 1);
    return true;
}

static bool trans_BPERMD(DisasContext *ctx, arg_BPERMD *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, PERM_ISA206);
#if defined(TARGET_PPC64)
    gen_helper_BPERMD(cpu_gpr[a->ra], cpu_gpr[a->rt], cpu_gpr[a->rb]);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_CFUGED(DisasContext *ctx, arg_X *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
#if defined(TARGET_PPC64)
    gen_helper_CFUGED(cpu_gpr[a->ra], cpu_gpr[a->rt], cpu_gpr[a->rb]);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static void do_cntzdm(TCGv_i64 dst, TCGv_i64 src, TCGv_i64 mask, int64_t trail)
{
    TCGv_i64 t0, t1;

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    tcg_gen_and_i64(t0, src, mask);
    if (trail) {
        tcg_gen_ctzi_i64(t0, t0, -1);
    } else {
        tcg_gen_clzi_i64(t0, t0, -1);
    }

    tcg_gen_setcondi_i64(TCG_COND_NE, t1, t0, -1);
    tcg_gen_andi_i64(t0, t0, 63);
    tcg_gen_xori_i64(t0, t0, 63);
    if (trail) {
        tcg_gen_shl_i64(t0, mask, t0);
        tcg_gen_shl_i64(t0, t0, t1);
    } else {
        tcg_gen_shr_i64(t0, mask, t0);
        tcg_gen_shr_i64(t0, t0, t1);
    }

    tcg_gen_ctpop_i64(dst, t0);
}

static bool trans_CNTLZDM(DisasContext *ctx, arg_X *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
#if defined(TARGET_PPC64)
    do_cntzdm(cpu_gpr[a->ra], cpu_gpr[a->rt], cpu_gpr[a->rb], false);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_CNTTZDM(DisasContext *ctx, arg_X *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
#if defined(TARGET_PPC64)
    do_cntzdm(cpu_gpr[a->ra], cpu_gpr[a->rt], cpu_gpr[a->rb], true);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_PDEPD(DisasContext *ctx, arg_X *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
#if defined(TARGET_PPC64)
    gen_helper_PDEPD(cpu_gpr[a->ra], cpu_gpr[a->rt], cpu_gpr[a->rb]);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_PEXTD(DisasContext *ctx, arg_X *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA310);
#if defined(TARGET_PPC64)
    gen_helper_PEXTD(cpu_gpr[a->ra], cpu_gpr[a->rt], cpu_gpr[a->rb]);
#else
    qemu_build_not_reached();
#endif
    return true;
}

TRANS(ANDI_, do_addi_, false);
TRANS(ANDIS_, do_addi_, true);
TRANS(ORI, do_ori, false);
TRANS(ORIS, do_ori, true);
TRANS(XORI, do_xori, false);
TRANS(XORIS, do_xori, true);

TRANS(AND, do_logical2, tcg_gen_and_tl);
TRANS(ANDC, do_logical2, tcg_gen_andc_tl);
TRANS(NAND, do_logical2, tcg_gen_nand_tl);
TRANS(ORC, do_logical2, tcg_gen_orc_tl);
TRANS(NOR, do_logical2, tcg_gen_nor_tl);
TRANS(EQV, do_logical2, tcg_gen_eqv_tl);
TRANS(EXTSB, do_logical1, tcg_gen_ext8s_tl);
TRANS(EXTSH, do_logical1, tcg_gen_ext16s_tl);

TRANS(CNTLZW, do_cntzw, tcg_gen_clzi_i32);
TRANS_FLAGS2(ISA300, CNTTZW, do_cntzw, tcg_gen_ctzi_i32);

TRANS64(EXTSW, do_logical1, tcg_gen_ext32s_tl);

static bool trans_ADDG6S(DisasContext *ctx, arg_X *a)
{
    const target_ulong carry_bits = (target_ulong)-1 / 0xf;
    TCGv in1, in2, carryl, carryh, tmp;
    TCGv zero = tcg_constant_tl(0);

    REQUIRE_INSNS_FLAGS2(ctx, BCDA_ISA206);

    in1 = cpu_gpr[a->ra];
    in2 = cpu_gpr[a->rb];
    tmp = tcg_temp_new();
    carryl = tcg_temp_new();
    carryh = tcg_temp_new();

    /* Addition with carry. */
    tcg_gen_add2_tl(carryl, carryh, in1, zero, in2, zero);
    /* Addition without carry. */
    tcg_gen_xor_tl(tmp, in1, in2);
    /* Difference between the two is carry in to each bit. */
    tcg_gen_xor_tl(carryl, carryl, tmp);

    /*
     * The carry-out that we're looking for is the carry-in to
     * the next nibble.  Shift the double-word down one nibble,
     * which puts all of the bits back into one word.
     */
    tcg_gen_extract2_tl(carryl, carryl, carryh, 4);

    /* Invert, isolate the carry bits, and produce 6's. */
    tcg_gen_andc_tl(carryl, tcg_constant_tl(carry_bits), carryl);
    tcg_gen_muli_tl(cpu_gpr[a->rt], carryl, 6);
    return true;
}

static bool trans_CDTBCD(DisasContext *ctx, arg_X_sa *a)
{
    REQUIRE_INSNS_FLAGS2(ctx, BCDA_ISA206);
    gen_helper_CDTBCD(cpu_gpr[a->ra], cpu_gpr[a->rs]);
    return true;
}

static bool trans_CBCDTD(DisasContext *ctx, arg_X_sa *a)
{
    REQUIRE_INSNS_FLAGS2(ctx, BCDA_ISA206);
    gen_helper_CBCDTD(cpu_gpr[a->ra], cpu_gpr[a->rs]);
    return true;
}

static bool do_hash(DisasContext *ctx, arg_X *a, bool priv,
    void (*helper)(TCGv_ptr, TCGv, TCGv, TCGv))
{
    TCGv ea;

    if (!(ctx->insns_flags2 & PPC2_ISA310)) {
        /* if version is before v3.1, this operation is a nop */
        return true;
    }

    if (priv) {
        /* if instruction is privileged but the context is in user space */
        REQUIRE_SV(ctx);
    }

    if (unlikely(a->ra == 0)) {
        /* if RA=0, the instruction form is invalid */
        gen_invalid(ctx);
        return true;
    }

    ea = do_ea_calc(ctx, a->ra, tcg_constant_tl(a->rt));
    helper(tcg_env, ea, cpu_gpr[a->ra], cpu_gpr[a->rb]);
    return true;
}

TRANS(HASHST, do_hash, false, gen_helper_HASHST)
TRANS(HASHCHK, do_hash, false, gen_helper_HASHCHK)
TRANS(HASHSTP, do_hash, true, gen_helper_HASHSTP)
TRANS(HASHCHKP, do_hash, true, gen_helper_HASHCHKP)
