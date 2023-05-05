/*
 * RISC-V translation routines for the T-Head vendor extensions (xthead*).
 *
 * Copyright (c) 2022 VRULL GmbH.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define REQUIRE_XTHEADBA(ctx) do {               \
    if (!ctx->cfg_ptr->ext_xtheadba) {           \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_XTHEADBB(ctx) do {               \
    if (!ctx->cfg_ptr->ext_xtheadbb) {           \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_XTHEADBS(ctx) do {               \
    if (!ctx->cfg_ptr->ext_xtheadbs) {           \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_XTHEADCMO(ctx) do {              \
    if (!ctx->cfg_ptr->ext_xtheadcmo) {          \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_XTHEADCONDMOV(ctx) do {          \
    if (!ctx->cfg_ptr->ext_xtheadcondmov) {      \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_XTHEADFMEMIDX(ctx) do {          \
    if (!ctx->cfg_ptr->ext_xtheadfmemidx) {      \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_XTHEADFMV(ctx) do {              \
    if (!ctx->cfg_ptr->ext_xtheadfmv) {          \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_XTHEADMAC(ctx) do {              \
    if (!ctx->cfg_ptr->ext_xtheadmac) {          \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_XTHEADMEMIDX(ctx) do {           \
    if (!ctx->cfg_ptr->ext_xtheadmemidx) {       \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_XTHEADMEMPAIR(ctx) do {          \
    if (!ctx->cfg_ptr->ext_xtheadmempair) {      \
        return false;                            \
    }                                            \
} while (0)

#define REQUIRE_XTHEADSYNC(ctx) do {             \
    if (!ctx->cfg_ptr->ext_xtheadsync) {         \
        return false;                            \
    }                                            \
} while (0)

/*
 * Calculate and return the address for indexed mem operations:
 * If !zext_offs, then the address is rs1 + (rs2 << imm2).
 * If  zext_offs, then the address is rs1 + (zext(rs2[31:0]) << imm2).
 */
static TCGv get_th_address_indexed(DisasContext *ctx, int rs1, int rs2,
                                   int imm2, bool zext_offs)
{
    TCGv src2 = get_gpr(ctx, rs2, EXT_NONE);
    TCGv offs = tcg_temp_new();

    if (zext_offs) {
        tcg_gen_extract_tl(offs, src2, 0, 32);
        tcg_gen_shli_tl(offs, offs, imm2);
    } else {
        tcg_gen_shli_tl(offs, src2, imm2);
    }

    return get_address_indexed(ctx, rs1, offs);
}

/* XTheadBa */

/*
 * th.addsl is similar to sh[123]add (from Zba), but not an
 * alternative encoding: while sh[123] applies the shift to rs1,
 * th.addsl shifts rs2.
 */

#define GEN_TH_ADDSL(SHAMT)                                     \
static void gen_th_addsl##SHAMT(TCGv ret, TCGv arg1, TCGv arg2) \
{                                                               \
    TCGv t = tcg_temp_new();                                    \
    tcg_gen_shli_tl(t, arg2, SHAMT);                            \
    tcg_gen_add_tl(ret, t, arg1);                               \
}

GEN_TH_ADDSL(1)
GEN_TH_ADDSL(2)
GEN_TH_ADDSL(3)

#define GEN_TRANS_TH_ADDSL(SHAMT)                                       \
static bool trans_th_addsl##SHAMT(DisasContext *ctx,                    \
                                  arg_th_addsl##SHAMT * a)              \
{                                                                       \
    REQUIRE_XTHEADBA(ctx);                                              \
    return gen_arith(ctx, a, EXT_NONE, gen_th_addsl##SHAMT, NULL);      \
}

GEN_TRANS_TH_ADDSL(1)
GEN_TRANS_TH_ADDSL(2)
GEN_TRANS_TH_ADDSL(3)

/* XTheadBb */

/* th.srri is an alternate encoding for rori (from Zbb) */
static bool trans_th_srri(DisasContext *ctx, arg_th_srri * a)
{
    REQUIRE_XTHEADBB(ctx);
    return gen_shift_imm_fn_per_ol(ctx, a, EXT_NONE,
                                   tcg_gen_rotri_tl, gen_roriw, NULL);
}

/* th.srriw is an alternate encoding for roriw (from Zbb) */
static bool trans_th_srriw(DisasContext *ctx, arg_th_srriw *a)
{
    REQUIRE_XTHEADBB(ctx);
    REQUIRE_64BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_shift_imm_fn(ctx, a, EXT_NONE, gen_roriw, NULL);
}

/* th.ext and th.extu perform signed/unsigned bitfield extraction */
static bool gen_th_bfextract(DisasContext *ctx, arg_th_bfext *a,
                             void (*f)(TCGv, TCGv, unsigned int, unsigned int))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv source = get_gpr(ctx, a->rs1, EXT_ZERO);

    if (a->lsb <= a->msb) {
        f(dest, source, a->lsb, a->msb - a->lsb + 1);
        gen_set_gpr(ctx, a->rd, dest);
    }
    return true;
}

static bool trans_th_ext(DisasContext *ctx, arg_th_ext *a)
{
    REQUIRE_XTHEADBB(ctx);
    return gen_th_bfextract(ctx, a, tcg_gen_sextract_tl);
}

static bool trans_th_extu(DisasContext *ctx, arg_th_extu *a)
{
    REQUIRE_XTHEADBB(ctx);
    return gen_th_bfextract(ctx, a, tcg_gen_extract_tl);
}

/* th.ff0: find first zero (clz on an inverted input) */
static bool gen_th_ff0(DisasContext *ctx, arg_th_ff0 *a, DisasExtend ext)
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);

    int olen = get_olen(ctx);
    TCGv t = tcg_temp_new();

    tcg_gen_not_tl(t, src1);
    if (olen != TARGET_LONG_BITS) {
        if (olen == 32) {
            gen_clzw(dest, t);
        } else {
            g_assert_not_reached();
        }
    } else {
        gen_clz(dest, t);
    }

    gen_set_gpr(ctx, a->rd, dest);

    return true;
}

static bool trans_th_ff0(DisasContext *ctx, arg_th_ff0 *a)
{
    REQUIRE_XTHEADBB(ctx);
    return gen_th_ff0(ctx, a, EXT_NONE);
}

/* th.ff1 is an alternate encoding for clz (from Zbb) */
static bool trans_th_ff1(DisasContext *ctx, arg_th_ff1 *a)
{
    REQUIRE_XTHEADBB(ctx);
    return gen_unary_per_ol(ctx, a, EXT_NONE, gen_clz, gen_clzw);
}

static void gen_th_revw(TCGv ret, TCGv arg1)
{
    tcg_gen_bswap32_tl(ret, arg1, TCG_BSWAP_OS);
}

/* th.rev is an alternate encoding for the RV64 rev8 (from Zbb) */
static bool trans_th_rev(DisasContext *ctx, arg_th_rev *a)
{
    REQUIRE_XTHEADBB(ctx);

    return gen_unary_per_ol(ctx, a, EXT_NONE, tcg_gen_bswap_tl, gen_th_revw);
}

/* th.revw is a sign-extended byte-swap of the lower word */
static bool trans_th_revw(DisasContext *ctx, arg_th_revw *a)
{
    REQUIRE_XTHEADBB(ctx);
    REQUIRE_64BIT(ctx);
    return gen_unary(ctx, a, EXT_NONE, gen_th_revw);
}

/* th.tstnbz is equivalent to an orc.b (from Zbb) with inverted result */
static void gen_th_tstnbz(TCGv ret, TCGv source1)
{
    gen_orc_b(ret, source1);
    tcg_gen_not_tl(ret, ret);
}

static bool trans_th_tstnbz(DisasContext *ctx, arg_th_tstnbz *a)
{
    REQUIRE_XTHEADBB(ctx);
    return gen_unary(ctx, a, EXT_ZERO, gen_th_tstnbz);
}

/* XTheadBs */

/* th.tst is an alternate encoding for bexti (from Zbs) */
static bool trans_th_tst(DisasContext *ctx, arg_th_tst *a)
{
    REQUIRE_XTHEADBS(ctx);
    return gen_shift_imm_tl(ctx, a, EXT_NONE, gen_bext);
}

/* XTheadCmo */

/* Test if priv level is M, S, or U (cannot fail). */
#define REQUIRE_PRIV_MSU(ctx)

/* Test if priv level is M or S. */
#define REQUIRE_PRIV_MS(ctx)                                    \
do {                                                            \
    if (ctx->priv == PRV_U) {                                   \
        return false;                                           \
    }                                                           \
} while (0)

#define NOP_PRIVCHECK(insn, extcheck, privcheck)                \
static bool trans_ ## insn(DisasContext *ctx, arg_ ## insn * a) \
{                                                               \
    (void) a;                                                   \
    extcheck(ctx);                                              \
    privcheck(ctx);                                             \
    return true;                                                \
}

NOP_PRIVCHECK(th_dcache_call, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_ciall, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_iall, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_cpa, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_cipa, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_ipa, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_cva, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MSU)
NOP_PRIVCHECK(th_dcache_civa, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MSU)
NOP_PRIVCHECK(th_dcache_iva, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MSU)
NOP_PRIVCHECK(th_dcache_csw, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_cisw, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_isw, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_cpal1, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_dcache_cval1, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)

NOP_PRIVCHECK(th_icache_iall, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_icache_ialls, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_icache_ipa, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_icache_iva, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MSU)

NOP_PRIVCHECK(th_l2cache_call, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_l2cache_ciall, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)
NOP_PRIVCHECK(th_l2cache_iall, REQUIRE_XTHEADCMO, REQUIRE_PRIV_MS)

/* XTheadCondMov */

static bool gen_th_condmove(DisasContext *ctx, arg_r *a, TCGCond cond)
{
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv old = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv dest = dest_gpr(ctx, a->rd);

    tcg_gen_movcond_tl(cond, dest, src2, ctx->zero, src1, old);

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

/* th.mveqz: "if (rs2 == 0) rd = rs1;" */
static bool trans_th_mveqz(DisasContext *ctx, arg_th_mveqz *a)
{
    REQUIRE_XTHEADCONDMOV(ctx);
    return gen_th_condmove(ctx, a, TCG_COND_EQ);
}

/* th.mvnez: "if (rs2 != 0) rd = rs1;" */
static bool trans_th_mvnez(DisasContext *ctx, arg_th_mveqz *a)
{
    REQUIRE_XTHEADCONDMOV(ctx);
    return gen_th_condmove(ctx, a, TCG_COND_NE);
}

/* XTheadFMem */

/*
 * Load 64-bit float from indexed address.
 * If !zext_offs, then address is rs1 + (rs2 << imm2).
 * If  zext_offs, then address is rs1 + (zext(rs2[31:0]) << imm2).
 */
static bool gen_fload_idx(DisasContext *ctx, arg_th_memidx *a, MemOp memop,
                          bool zext_offs)
{
    TCGv_i64 rd = cpu_fpr[a->rd];
    TCGv addr = get_th_address_indexed(ctx, a->rs1, a->rs2, a->imm2, zext_offs);

    tcg_gen_qemu_ld_i64(rd, addr, ctx->mem_idx, memop);
    if ((memop & MO_SIZE) == MO_32) {
        gen_nanbox_s(rd, rd);
    }

    mark_fs_dirty(ctx);
    return true;
}

/*
 * Store 64-bit float to indexed address.
 * If !zext_offs, then address is rs1 + (rs2 << imm2).
 * If  zext_offs, then address is rs1 + (zext(rs2[31:0]) << imm2).
 */
static bool gen_fstore_idx(DisasContext *ctx, arg_th_memidx *a, MemOp memop,
                           bool zext_offs)
{
    TCGv_i64 rd = cpu_fpr[a->rd];
    TCGv addr = get_th_address_indexed(ctx, a->rs1, a->rs2, a->imm2, zext_offs);

    tcg_gen_qemu_st_i64(rd, addr, ctx->mem_idx, memop);

    return true;
}

static bool trans_th_flrd(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADFMEMIDX(ctx);
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);
    return gen_fload_idx(ctx, a, MO_TEUQ, false);
}

static bool trans_th_flrw(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADFMEMIDX(ctx);
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVF);
    return gen_fload_idx(ctx, a, MO_TEUL, false);
}

static bool trans_th_flurd(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADFMEMIDX(ctx);
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);
    return gen_fload_idx(ctx, a, MO_TEUQ, true);
}

static bool trans_th_flurw(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADFMEMIDX(ctx);
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVF);
    return gen_fload_idx(ctx, a, MO_TEUL, true);
}

static bool trans_th_fsrd(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADFMEMIDX(ctx);
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);
    return gen_fstore_idx(ctx, a, MO_TEUQ, false);
}

static bool trans_th_fsrw(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADFMEMIDX(ctx);
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVF);
    return gen_fstore_idx(ctx, a, MO_TEUL, false);
}

static bool trans_th_fsurd(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADFMEMIDX(ctx);
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);
    return gen_fstore_idx(ctx, a, MO_TEUQ, true);
}

static bool trans_th_fsurw(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADFMEMIDX(ctx);
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVF);
    return gen_fstore_idx(ctx, a, MO_TEUL, true);
}

/* XTheadFmv */

static bool trans_th_fmv_hw_x(DisasContext *ctx, arg_th_fmv_hw_x *a)
{
    REQUIRE_XTHEADFMV(ctx);
    REQUIRE_32BIT(ctx);
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);

    TCGv src1 = get_gpr(ctx, a->rs1, EXT_ZERO);
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_extu_tl_i64(t1, src1);
    tcg_gen_deposit_i64(cpu_fpr[a->rd], cpu_fpr[a->rd], t1, 32, 32);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_th_fmv_x_hw(DisasContext *ctx, arg_th_fmv_x_hw *a)
{
    REQUIRE_XTHEADFMV(ctx);
    REQUIRE_32BIT(ctx);
    REQUIRE_FPU;
    REQUIRE_EXT(ctx, RVD);
    TCGv dst;
    TCGv_i64 t1;

    dst = dest_gpr(ctx, a->rd);
    t1 = tcg_temp_new_i64();

    tcg_gen_extract_i64(t1, cpu_fpr[a->rs1], 32, 32);
    tcg_gen_trunc_i64_tl(dst, t1);
    gen_set_gpr(ctx, a->rd, dst);
    mark_fs_dirty(ctx);
    return true;
}

/* XTheadMac */

static bool gen_th_mac(DisasContext *ctx, arg_r *a,
                       void (*accumulate_func)(TCGv, TCGv, TCGv),
                       void (*extend_operand_func)(TCGv, TCGv))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src0 = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv tmp = tcg_temp_new();

    if (extend_operand_func) {
        TCGv tmp2 = tcg_temp_new();
        extend_operand_func(tmp, src1);
        extend_operand_func(tmp2, src2);
        tcg_gen_mul_tl(tmp, tmp, tmp2);
    } else {
        tcg_gen_mul_tl(tmp, src1, src2);
    }

    accumulate_func(dest, src0, tmp);
    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

/* th.mula: "rd = rd + rs1 * rs2" */
static bool trans_th_mula(DisasContext *ctx, arg_th_mula *a)
{
    REQUIRE_XTHEADMAC(ctx);
    return gen_th_mac(ctx, a, tcg_gen_add_tl, NULL);
}

/* th.mulah: "rd = sext.w(rd + sext.w(rs1[15:0]) * sext.w(rs2[15:0]))" */
static bool trans_th_mulah(DisasContext *ctx, arg_th_mulah *a)
{
    REQUIRE_XTHEADMAC(ctx);
    ctx->ol = MXL_RV32;
    return gen_th_mac(ctx, a, tcg_gen_add_tl, tcg_gen_ext16s_tl);
}

/* th.mulaw: "rd = sext.w(rd + rs1 * rs2)" */
static bool trans_th_mulaw(DisasContext *ctx, arg_th_mulaw *a)
{
    REQUIRE_XTHEADMAC(ctx);
    REQUIRE_64BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_th_mac(ctx, a, tcg_gen_add_tl, NULL);
}

/* th.muls: "rd = rd - rs1 * rs2" */
static bool trans_th_muls(DisasContext *ctx, arg_th_muls *a)
{
    REQUIRE_XTHEADMAC(ctx);
    return gen_th_mac(ctx, a, tcg_gen_sub_tl, NULL);
}

/* th.mulsh: "rd = sext.w(rd - sext.w(rs1[15:0]) * sext.w(rs2[15:0]))" */
static bool trans_th_mulsh(DisasContext *ctx, arg_th_mulsh *a)
{
    REQUIRE_XTHEADMAC(ctx);
    ctx->ol = MXL_RV32;
    return gen_th_mac(ctx, a, tcg_gen_sub_tl, tcg_gen_ext16s_tl);
}

/* th.mulsw: "rd = sext.w(rd - rs1 * rs2)" */
static bool trans_th_mulsw(DisasContext *ctx, arg_th_mulsw *a)
{
    REQUIRE_XTHEADMAC(ctx);
    REQUIRE_64BIT(ctx);
    ctx->ol = MXL_RV32;
    return gen_th_mac(ctx, a, tcg_gen_sub_tl, NULL);
}

/* XTheadMemIdx */

/*
 * Load with memop from indexed address and add (imm5 << imm2) to rs1.
 * If !preinc, then the load address is rs1.
 * If  preinc, then the load address is rs1 + (imm5) << imm2).
 */
static bool gen_load_inc(DisasContext *ctx, arg_th_meminc *a, MemOp memop,
                         bool preinc)
{
    if (a->rs1 == a->rd) {
        return false;
    }

    int imm = a->imm5 << a->imm2;
    TCGv addr = get_address(ctx, a->rs1, preinc ? imm : 0);
    TCGv rd = dest_gpr(ctx, a->rd);
    TCGv rs1 = get_gpr(ctx, a->rs1, EXT_NONE);

    tcg_gen_qemu_ld_tl(rd, addr, ctx->mem_idx, memop);
    tcg_gen_addi_tl(rs1, rs1, imm);
    gen_set_gpr(ctx, a->rd, rd);
    gen_set_gpr(ctx, a->rs1, rs1);
    return true;
}

/*
 * Store with memop to indexed address and add (imm5 << imm2) to rs1.
 * If !preinc, then the store address is rs1.
 * If  preinc, then the store address is rs1 + (imm5) << imm2).
 */
static bool gen_store_inc(DisasContext *ctx, arg_th_meminc *a, MemOp memop,
                          bool preinc)
{
    int imm = a->imm5 << a->imm2;
    TCGv addr = get_address(ctx, a->rs1, preinc ? imm : 0);
    TCGv data = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv rs1 = get_gpr(ctx, a->rs1, EXT_NONE);

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, memop);
    tcg_gen_addi_tl(rs1, rs1, imm);
    gen_set_gpr(ctx, a->rs1, rs1);
    return true;
}

static bool trans_th_ldia(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    REQUIRE_64BIT(ctx);
    return gen_load_inc(ctx, a, MO_TESQ, false);
}

static bool trans_th_ldib(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    REQUIRE_64BIT(ctx);
    return gen_load_inc(ctx, a, MO_TESQ, true);
}

static bool trans_th_lwia(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_inc(ctx, a, MO_TESL, false);
}

static bool trans_th_lwib(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_inc(ctx, a, MO_TESL, true);
}

static bool trans_th_lwuia(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    REQUIRE_64BIT(ctx);
    return gen_load_inc(ctx, a, MO_TEUL, false);
}

static bool trans_th_lwuib(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    REQUIRE_64BIT(ctx);
    return gen_load_inc(ctx, a, MO_TEUL, true);
}

static bool trans_th_lhia(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_inc(ctx, a, MO_TESW, false);
}

static bool trans_th_lhib(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_inc(ctx, a, MO_TESW, true);
}

static bool trans_th_lhuia(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_inc(ctx, a, MO_TEUW, false);
}

static bool trans_th_lhuib(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_inc(ctx, a, MO_TEUW, true);
}

static bool trans_th_lbia(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_inc(ctx, a, MO_SB, false);
}

static bool trans_th_lbib(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_inc(ctx, a, MO_SB, true);
}

static bool trans_th_lbuia(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_inc(ctx, a, MO_UB, false);
}

static bool trans_th_lbuib(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_inc(ctx, a, MO_UB, true);
}

static bool trans_th_sdia(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    REQUIRE_64BIT(ctx);
    return gen_store_inc(ctx, a, MO_TESQ, false);
}

static bool trans_th_sdib(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    REQUIRE_64BIT(ctx);
    return gen_store_inc(ctx, a, MO_TESQ, true);
}

static bool trans_th_swia(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_store_inc(ctx, a, MO_TESL, false);
}

static bool trans_th_swib(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_store_inc(ctx, a, MO_TESL, true);
}

static bool trans_th_shia(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_store_inc(ctx, a, MO_TESW, false);
}

static bool trans_th_shib(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_store_inc(ctx, a, MO_TESW, true);
}

static bool trans_th_sbia(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_store_inc(ctx, a, MO_SB, false);
}

static bool trans_th_sbib(DisasContext *ctx, arg_th_meminc *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_store_inc(ctx, a, MO_SB, true);
}

/*
 * Load with memop from indexed address.
 * If !zext_offs, then address is rs1 + (rs2 << imm2).
 * If  zext_offs, then address is rs1 + (zext(rs2[31:0]) << imm2).
 */
static bool gen_load_idx(DisasContext *ctx, arg_th_memidx *a, MemOp memop,
                         bool zext_offs)
{
    TCGv rd = dest_gpr(ctx, a->rd);
    TCGv addr = get_th_address_indexed(ctx, a->rs1, a->rs2, a->imm2, zext_offs);

    tcg_gen_qemu_ld_tl(rd, addr, ctx->mem_idx, memop);
    gen_set_gpr(ctx, a->rd, rd);

    return true;
}

/*
 * Store with memop to indexed address.
 * If !zext_offs, then address is rs1 + (rs2 << imm2).
 * If  zext_offs, then address is rs1 + (zext(rs2[31:0]) << imm2).
 */
static bool gen_store_idx(DisasContext *ctx, arg_th_memidx *a, MemOp memop,
                          bool zext_offs)
{
    TCGv data = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv addr = get_th_address_indexed(ctx, a->rs1, a->rs2, a->imm2, zext_offs);

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, memop);

    return true;
}

static bool trans_th_lrd(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    REQUIRE_64BIT(ctx);
    return gen_load_idx(ctx, a, MO_TESQ, false);
}

static bool trans_th_lrw(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_idx(ctx, a, MO_TESL, false);
}

static bool trans_th_lrwu(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    REQUIRE_64BIT(ctx);
    return gen_load_idx(ctx, a, MO_TEUL, false);
}

static bool trans_th_lrh(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_idx(ctx, a, MO_TESW, false);
}

static bool trans_th_lrhu(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_idx(ctx, a, MO_TEUW, false);
}

static bool trans_th_lrb(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_idx(ctx, a, MO_SB, false);
}

static bool trans_th_lrbu(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_idx(ctx, a, MO_UB, false);
}

static bool trans_th_srd(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    REQUIRE_64BIT(ctx);
    return gen_store_idx(ctx, a, MO_TESQ, false);
}

static bool trans_th_srw(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_store_idx(ctx, a, MO_TESL, false);
}

static bool trans_th_srh(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_store_idx(ctx, a, MO_TESW, false);
}

static bool trans_th_srb(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_store_idx(ctx, a, MO_SB, false);
}
static bool trans_th_lurd(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    REQUIRE_64BIT(ctx);
    return gen_load_idx(ctx, a, MO_TESQ, true);
}

static bool trans_th_lurw(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_idx(ctx, a, MO_TESL, true);
}

static bool trans_th_lurwu(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    REQUIRE_64BIT(ctx);
    return gen_load_idx(ctx, a, MO_TEUL, true);
}

static bool trans_th_lurh(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_idx(ctx, a, MO_TESW, true);
}

static bool trans_th_lurhu(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_idx(ctx, a, MO_TEUW, true);
}

static bool trans_th_lurb(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_idx(ctx, a, MO_SB, true);
}

static bool trans_th_lurbu(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_load_idx(ctx, a, MO_UB, true);
}

static bool trans_th_surd(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    REQUIRE_64BIT(ctx);
    return gen_store_idx(ctx, a, MO_TESQ, true);
}

static bool trans_th_surw(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_store_idx(ctx, a, MO_TESL, true);
}

static bool trans_th_surh(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_store_idx(ctx, a, MO_TESW, true);
}

static bool trans_th_surb(DisasContext *ctx, arg_th_memidx *a)
{
    REQUIRE_XTHEADMEMIDX(ctx);
    return gen_store_idx(ctx, a, MO_SB, true);
}

/* XTheadMemPair */

static bool gen_loadpair_tl(DisasContext *ctx, arg_th_pair *a, MemOp memop,
                            int shamt)
{
    if (a->rs == a->rd1 || a->rs == a->rd2 || a->rd1 == a->rd2) {
        return false;
    }

    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    TCGv addr1 = tcg_temp_new();
    TCGv addr2 = tcg_temp_new();
    int imm = a->sh2 << shamt;

    addr1 = get_address(ctx, a->rs, imm);
    addr2 = get_address(ctx, a->rs, memop_size(memop) + imm);

    tcg_gen_qemu_ld_tl(t1, addr1, ctx->mem_idx, memop);
    tcg_gen_qemu_ld_tl(t2, addr2, ctx->mem_idx, memop);
    gen_set_gpr(ctx, a->rd1, t1);
    gen_set_gpr(ctx, a->rd2, t2);
    return true;
}

static bool trans_th_ldd(DisasContext *ctx, arg_th_pair *a)
{
    REQUIRE_XTHEADMEMPAIR(ctx);
    REQUIRE_64BIT(ctx);
    return gen_loadpair_tl(ctx, a, MO_TESQ, 4);
}

static bool trans_th_lwd(DisasContext *ctx, arg_th_pair *a)
{
    REQUIRE_XTHEADMEMPAIR(ctx);
    return gen_loadpair_tl(ctx, a, MO_TESL, 3);
}

static bool trans_th_lwud(DisasContext *ctx, arg_th_pair *a)
{
    REQUIRE_XTHEADMEMPAIR(ctx);
    return gen_loadpair_tl(ctx, a, MO_TEUL, 3);
}

static bool gen_storepair_tl(DisasContext *ctx, arg_th_pair *a, MemOp memop,
                             int shamt)
{
    TCGv data1 = get_gpr(ctx, a->rd1, EXT_NONE);
    TCGv data2 = get_gpr(ctx, a->rd2, EXT_NONE);
    TCGv addr1 = tcg_temp_new();
    TCGv addr2 = tcg_temp_new();
    int imm = a->sh2 << shamt;

    addr1 = get_address(ctx, a->rs, imm);
    addr2 = get_address(ctx, a->rs, memop_size(memop) + imm);

    tcg_gen_qemu_st_tl(data1, addr1, ctx->mem_idx, memop);
    tcg_gen_qemu_st_tl(data2, addr2, ctx->mem_idx, memop);
    return true;
}

static bool trans_th_sdd(DisasContext *ctx, arg_th_pair *a)
{
    REQUIRE_XTHEADMEMPAIR(ctx);
    REQUIRE_64BIT(ctx);
    return gen_storepair_tl(ctx, a, MO_TESQ, 4);
}

static bool trans_th_swd(DisasContext *ctx, arg_th_pair *a)
{
    REQUIRE_XTHEADMEMPAIR(ctx);
    return gen_storepair_tl(ctx, a, MO_TESL, 3);
}

/* XTheadSync */

static bool trans_th_sfence_vmas(DisasContext *ctx, arg_th_sfence_vmas *a)
{
    (void) a;
    REQUIRE_XTHEADSYNC(ctx);

#ifndef CONFIG_USER_ONLY
    REQUIRE_PRIV_MS(ctx);
    gen_helper_tlb_flush_all(cpu_env);
    return true;
#else
    return false;
#endif
}

#ifndef CONFIG_USER_ONLY
static void gen_th_sync_local(DisasContext *ctx)
{
    /*
     * Emulate out-of-order barriers with pipeline flush
     * by exiting the translation block.
     */
    gen_set_pc_imm(ctx, ctx->pc_succ_insn);
    tcg_gen_exit_tb(NULL, 0);
    ctx->base.is_jmp = DISAS_NORETURN;
}
#endif

static bool trans_th_sync(DisasContext *ctx, arg_th_sync *a)
{
    (void) a;
    REQUIRE_XTHEADSYNC(ctx);

#ifndef CONFIG_USER_ONLY
    REQUIRE_PRIV_MSU(ctx);

    /*
     * th.sync is an out-of-order barrier.
     */
    gen_th_sync_local(ctx);

    return true;
#else
    return false;
#endif
}

static bool trans_th_sync_i(DisasContext *ctx, arg_th_sync_i *a)
{
    (void) a;
    REQUIRE_XTHEADSYNC(ctx);

#ifndef CONFIG_USER_ONLY
    REQUIRE_PRIV_MSU(ctx);

    /*
     * th.sync.i is th.sync plus pipeline flush.
     */
    gen_th_sync_local(ctx);

    return true;
#else
    return false;
#endif
}

static bool trans_th_sync_is(DisasContext *ctx, arg_th_sync_is *a)
{
    /* This instruction has the same behaviour like th.sync.i. */
    return trans_th_sync_i(ctx, a);
}

static bool trans_th_sync_s(DisasContext *ctx, arg_th_sync_s *a)
{
    /* This instruction has the same behaviour like th.sync. */
    return trans_th_sync(ctx, a);
}
