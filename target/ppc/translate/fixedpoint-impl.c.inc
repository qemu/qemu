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
    TCGv_i64 low_addr_gpr, high_addr_gpr;
    MemOp mop;

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

    if (prefixed || !ctx->le_mode) {
        low_addr_gpr = cpu_gpr[a->rt];
        high_addr_gpr = cpu_gpr[a->rt + 1];
    } else {
        low_addr_gpr = cpu_gpr[a->rt + 1];
        high_addr_gpr = cpu_gpr[a->rt];
    }

    if (tb_cflags(ctx->base.tb) & CF_PARALLEL) {
        if (HAVE_ATOMIC128) {
            mop = DEF_MEMOP(MO_128);
            TCGv_i32 oi = tcg_constant_i32(make_memop_idx(mop, ctx->mem_idx));
            if (store) {
                if (ctx->le_mode) {
                    gen_helper_stq_le_parallel(cpu_env, ea, low_addr_gpr,
                                               high_addr_gpr, oi);
                } else {
                    gen_helper_stq_be_parallel(cpu_env, ea, high_addr_gpr,
                                               low_addr_gpr, oi);

                }
            } else {
                if (ctx->le_mode) {
                    gen_helper_lq_le_parallel(low_addr_gpr, cpu_env, ea, oi);
                    tcg_gen_ld_i64(high_addr_gpr, cpu_env,
                                   offsetof(CPUPPCState, retxh));
                } else {
                    gen_helper_lq_be_parallel(high_addr_gpr, cpu_env, ea, oi);
                    tcg_gen_ld_i64(low_addr_gpr, cpu_env,
                                   offsetof(CPUPPCState, retxh));
                }
            }
        } else {
            /* Restart with exclusive lock.  */
            gen_helper_exit_atomic(cpu_env);
            ctx->base.is_jmp = DISAS_NORETURN;
        }
    } else {
        mop = DEF_MEMOP(MO_UQ);
        if (store) {
            tcg_gen_qemu_st_i64(low_addr_gpr, ea, ctx->mem_idx, mop);
        } else {
            tcg_gen_qemu_ld_i64(low_addr_gpr, ea, ctx->mem_idx, mop);
        }

        gen_addr_add(ctx, ea, ea, 8);

        if (store) {
            tcg_gen_qemu_st_i64(high_addr_gpr, ea, ctx->mem_idx, mop);
        } else {
            tcg_gen_qemu_ld_i64(high_addr_gpr, ea, ctx->mem_idx, mop);
        }
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

    tcg_gen_extu_i32_tl(temp, cpu_crf[a->bi >> 2]);
    tcg_gen_andi_tl(temp, temp, mask);
    tcg_gen_setcondi_tl(cond, cpu_gpr[a->rt], temp, 0);
    if (neg) {
        tcg_gen_neg_tl(cpu_gpr[a->rt], cpu_gpr[a->rt]);
    }
    return true;
}

TRANS(SETBC, do_set_bool_cond, false, false)
TRANS(SETBCR, do_set_bool_cond, false, true)
TRANS(SETNBC, do_set_bool_cond, true, false)
TRANS(SETNBCR, do_set_bool_cond, true, true)

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
    helper(cpu_env, ea, cpu_gpr[a->ra], cpu_gpr[a->rb]);
    return true;
}

TRANS(HASHST, do_hash, false, gen_helper_HASHST)
TRANS(HASHCHK, do_hash, false, gen_helper_HASHCHK)
TRANS(HASHSTP, do_hash, true, gen_helper_HASHSTP)
TRANS(HASHCHKP, do_hash, true, gen_helper_HASHCHKP)
