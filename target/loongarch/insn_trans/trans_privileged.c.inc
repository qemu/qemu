/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 * LoongArch translation routines for the privileged instructions.
 */

#include "cpu-csr.h"

#ifdef CONFIG_USER_ONLY

#define GEN_FALSE_TRANS(name)   \
static bool trans_##name(DisasContext *ctx, arg_##name * a)  \
{   \
    return false;   \
}

GEN_FALSE_TRANS(csrrd)
GEN_FALSE_TRANS(csrwr)
GEN_FALSE_TRANS(csrxchg)
GEN_FALSE_TRANS(iocsrrd_b)
GEN_FALSE_TRANS(iocsrrd_h)
GEN_FALSE_TRANS(iocsrrd_w)
GEN_FALSE_TRANS(iocsrrd_d)
GEN_FALSE_TRANS(iocsrwr_b)
GEN_FALSE_TRANS(iocsrwr_h)
GEN_FALSE_TRANS(iocsrwr_w)
GEN_FALSE_TRANS(iocsrwr_d)
GEN_FALSE_TRANS(tlbsrch)
GEN_FALSE_TRANS(tlbrd)
GEN_FALSE_TRANS(tlbwr)
GEN_FALSE_TRANS(tlbfill)
GEN_FALSE_TRANS(tlbclr)
GEN_FALSE_TRANS(tlbflush)
GEN_FALSE_TRANS(invtlb)
GEN_FALSE_TRANS(cacop)
GEN_FALSE_TRANS(ldpte)
GEN_FALSE_TRANS(lddir)
GEN_FALSE_TRANS(ertn)
GEN_FALSE_TRANS(dbcl)
GEN_FALSE_TRANS(idle)

#else

typedef void (*GenCSRRead)(TCGv dest, TCGv_ptr env);
typedef void (*GenCSRWrite)(TCGv dest, TCGv_ptr env, TCGv src);

typedef struct {
    int offset;
    int flags;
    GenCSRRead readfn;
    GenCSRWrite writefn;
} CSRInfo;

enum {
    CSRFL_READONLY = (1 << 0),
    CSRFL_EXITTB   = (1 << 1),
    CSRFL_IO       = (1 << 2),
};

#define CSR_OFF_FUNCS(NAME, FL, RD, WR)                    \
    [LOONGARCH_CSR_##NAME] = {                             \
        .offset = offsetof(CPULoongArchState, CSR_##NAME), \
        .flags = FL, .readfn = RD, .writefn = WR           \
    }

#define CSR_OFF_ARRAY(NAME, N)                                \
    [LOONGARCH_CSR_##NAME(N)] = {                             \
        .offset = offsetof(CPULoongArchState, CSR_##NAME[N]), \
        .flags = 0, .readfn = NULL, .writefn = NULL           \
    }

#define CSR_OFF_FLAGS(NAME, FL) \
    CSR_OFF_FUNCS(NAME, FL, NULL, NULL)

#define CSR_OFF(NAME) \
    CSR_OFF_FLAGS(NAME, 0)

static const CSRInfo csr_info[] = {
    CSR_OFF_FLAGS(CRMD, CSRFL_EXITTB),
    CSR_OFF(PRMD),
    CSR_OFF_FLAGS(EUEN, CSRFL_EXITTB),
    CSR_OFF_FLAGS(MISC, CSRFL_READONLY),
    CSR_OFF(ECFG),
    CSR_OFF_FUNCS(ESTAT, CSRFL_EXITTB, NULL, gen_helper_csrwr_estat),
    CSR_OFF(ERA),
    CSR_OFF(BADV),
    CSR_OFF_FLAGS(BADI, CSRFL_READONLY),
    CSR_OFF(EENTRY),
    CSR_OFF(TLBIDX),
    CSR_OFF(TLBEHI),
    CSR_OFF(TLBELO0),
    CSR_OFF(TLBELO1),
    CSR_OFF_FUNCS(ASID, CSRFL_EXITTB, NULL, gen_helper_csrwr_asid),
    CSR_OFF(PGDL),
    CSR_OFF(PGDH),
    CSR_OFF_FUNCS(PGD, CSRFL_READONLY, gen_helper_csrrd_pgd, NULL),
    CSR_OFF(PWCL),
    CSR_OFF(PWCH),
    CSR_OFF(STLBPS),
    CSR_OFF(RVACFG),
    [LOONGARCH_CSR_CPUID] = {
        .offset = (int)offsetof(CPUState, cpu_index)
                  - (int)offsetof(LoongArchCPU, env),
        .flags = CSRFL_READONLY,
        .readfn = NULL,
        .writefn = NULL
    },
    CSR_OFF_FLAGS(PRCFG1, CSRFL_READONLY),
    CSR_OFF_FLAGS(PRCFG2, CSRFL_READONLY),
    CSR_OFF_FLAGS(PRCFG3, CSRFL_READONLY),
    CSR_OFF_ARRAY(SAVE, 0),
    CSR_OFF_ARRAY(SAVE, 1),
    CSR_OFF_ARRAY(SAVE, 2),
    CSR_OFF_ARRAY(SAVE, 3),
    CSR_OFF_ARRAY(SAVE, 4),
    CSR_OFF_ARRAY(SAVE, 5),
    CSR_OFF_ARRAY(SAVE, 6),
    CSR_OFF_ARRAY(SAVE, 7),
    CSR_OFF_ARRAY(SAVE, 8),
    CSR_OFF_ARRAY(SAVE, 9),
    CSR_OFF_ARRAY(SAVE, 10),
    CSR_OFF_ARRAY(SAVE, 11),
    CSR_OFF_ARRAY(SAVE, 12),
    CSR_OFF_ARRAY(SAVE, 13),
    CSR_OFF_ARRAY(SAVE, 14),
    CSR_OFF_ARRAY(SAVE, 15),
    CSR_OFF(TID),
    CSR_OFF_FUNCS(TCFG, CSRFL_IO, NULL, gen_helper_csrwr_tcfg),
    CSR_OFF_FUNCS(TVAL, CSRFL_READONLY | CSRFL_IO, gen_helper_csrrd_tval, NULL),
    CSR_OFF(CNTC),
    CSR_OFF_FUNCS(TICLR, CSRFL_IO, NULL, gen_helper_csrwr_ticlr),
    CSR_OFF(LLBCTL),
    CSR_OFF(IMPCTL1),
    CSR_OFF(IMPCTL2),
    CSR_OFF(TLBRENTRY),
    CSR_OFF(TLBRBADV),
    CSR_OFF(TLBRERA),
    CSR_OFF(TLBRSAVE),
    CSR_OFF(TLBRELO0),
    CSR_OFF(TLBRELO1),
    CSR_OFF(TLBREHI),
    CSR_OFF(TLBRPRMD),
    CSR_OFF(MERRCTL),
    CSR_OFF(MERRINFO1),
    CSR_OFF(MERRINFO2),
    CSR_OFF(MERRENTRY),
    CSR_OFF(MERRERA),
    CSR_OFF(MERRSAVE),
    CSR_OFF(CTAG),
    CSR_OFF_ARRAY(DMW, 0),
    CSR_OFF_ARRAY(DMW, 1),
    CSR_OFF_ARRAY(DMW, 2),
    CSR_OFF_ARRAY(DMW, 3),
    CSR_OFF(DBG),
    CSR_OFF(DERA),
    CSR_OFF(DSAVE),
};

static bool check_plv(DisasContext *ctx)
{
    if (ctx->plv == MMU_PLV_USER) {
        generate_exception(ctx, EXCCODE_IPE);
        return true;
    }
    return false;
}

static const CSRInfo *get_csr(unsigned csr_num)
{
    const CSRInfo *csr;

    if (csr_num >= ARRAY_SIZE(csr_info)) {
        return NULL;
    }
    csr = &csr_info[csr_num];
    if (csr->offset == 0) {
        return NULL;
    }
    return csr;
}

static bool check_csr_flags(DisasContext *ctx, const CSRInfo *csr, bool write)
{
    if ((csr->flags & CSRFL_READONLY) && write) {
        return false;
    }
    if ((csr->flags & CSRFL_IO) &&
        (tb_cflags(ctx->base.tb) & CF_USE_ICOUNT)) {
        gen_io_start();
        ctx->base.is_jmp = DISAS_EXIT_UPDATE;
    } else if ((csr->flags & CSRFL_EXITTB) && write) {
        ctx->base.is_jmp = DISAS_EXIT_UPDATE;
    }
    return true;
}

static bool trans_csrrd(DisasContext *ctx, arg_csrrd *a)
{
    TCGv dest;
    const CSRInfo *csr;

    if (check_plv(ctx)) {
        return false;
    }
    csr = get_csr(a->csr);
    if (csr == NULL) {
        /* CSR is undefined: read as 0. */
        dest = tcg_constant_tl(0);
    } else {
        check_csr_flags(ctx, csr, false);
        dest = gpr_dst(ctx, a->rd, EXT_NONE);
        if (csr->readfn) {
            csr->readfn(dest, cpu_env);
        } else {
            tcg_gen_ld_tl(dest, cpu_env, csr->offset);
        }
    }
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool trans_csrwr(DisasContext *ctx, arg_csrwr *a)
{
    TCGv dest, src1;
    const CSRInfo *csr;

    if (check_plv(ctx)) {
        return false;
    }
    csr = get_csr(a->csr);
    if (csr == NULL) {
        /* CSR is undefined: write ignored, read old_value as 0. */
        gen_set_gpr(a->rd, tcg_constant_tl(0), EXT_NONE);
        return true;
    }
    if (!check_csr_flags(ctx, csr, true)) {
        /* CSR is readonly: trap. */
        return false;
    }
    src1 = gpr_src(ctx, a->rd, EXT_NONE);
    if (csr->writefn) {
        dest = gpr_dst(ctx, a->rd, EXT_NONE);
        csr->writefn(dest, cpu_env, src1);
    } else {
        dest = tcg_temp_new();
        tcg_gen_ld_tl(dest, cpu_env, csr->offset);
        tcg_gen_st_tl(src1, cpu_env, csr->offset);
    }
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool trans_csrxchg(DisasContext *ctx, arg_csrxchg *a)
{
    TCGv src1, mask, oldv, newv, temp;
    const CSRInfo *csr;

    if (check_plv(ctx)) {
        return false;
    }
    csr = get_csr(a->csr);
    if (csr == NULL) {
        /* CSR is undefined: write ignored, read old_value as 0. */
        gen_set_gpr(a->rd, tcg_constant_tl(0), EXT_NONE);
        return true;
    }

    if (!check_csr_flags(ctx, csr, true)) {
        /* CSR is readonly: trap. */
        return false;
    }

    /* So far only readonly csrs have readfn. */
    assert(csr->readfn == NULL);

    src1 = gpr_src(ctx, a->rd, EXT_NONE);
    mask = gpr_src(ctx, a->rj, EXT_NONE);
    oldv = tcg_temp_new();
    newv = tcg_temp_new();
    temp = tcg_temp_new();

    tcg_gen_ld_tl(oldv, cpu_env, csr->offset);
    tcg_gen_and_tl(newv, src1, mask);
    tcg_gen_andc_tl(temp, oldv, mask);
    tcg_gen_or_tl(newv, newv, temp);

    if (csr->writefn) {
        csr->writefn(oldv, cpu_env, newv);
    } else {
        tcg_gen_st_tl(newv, cpu_env, csr->offset);
    }
    gen_set_gpr(a->rd, oldv, EXT_NONE);
    return true;
}

static bool gen_iocsrrd(DisasContext *ctx, arg_rr *a,
                        void (*func)(TCGv, TCGv_ptr, TCGv))
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);

    if (check_plv(ctx)) {
        return false;
    }
    func(dest, cpu_env, src1);
    return true;
}

static bool gen_iocsrwr(DisasContext *ctx, arg_rr *a,
                        void (*func)(TCGv_ptr, TCGv, TCGv))
{
    TCGv val = gpr_src(ctx, a->rd, EXT_NONE);
    TCGv addr = gpr_src(ctx, a->rj, EXT_NONE);

    if (check_plv(ctx)) {
        return false;
    }
    func(cpu_env, addr, val);
    return true;
}

TRANS(iocsrrd_b, gen_iocsrrd, gen_helper_iocsrrd_b)
TRANS(iocsrrd_h, gen_iocsrrd, gen_helper_iocsrrd_h)
TRANS(iocsrrd_w, gen_iocsrrd, gen_helper_iocsrrd_w)
TRANS(iocsrrd_d, gen_iocsrrd, gen_helper_iocsrrd_d)
TRANS(iocsrwr_b, gen_iocsrwr, gen_helper_iocsrwr_b)
TRANS(iocsrwr_h, gen_iocsrwr, gen_helper_iocsrwr_h)
TRANS(iocsrwr_w, gen_iocsrwr, gen_helper_iocsrwr_w)
TRANS(iocsrwr_d, gen_iocsrwr, gen_helper_iocsrwr_d)

static void check_mmu_idx(DisasContext *ctx)
{
    if (ctx->mem_idx != MMU_IDX_DA) {
        tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next + 4);
        ctx->base.is_jmp = DISAS_EXIT;
    }
}

static bool trans_tlbsrch(DisasContext *ctx, arg_tlbsrch *a)
{
    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_tlbsrch(cpu_env);
    return true;
}

static bool trans_tlbrd(DisasContext *ctx, arg_tlbrd *a)
{
    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_tlbrd(cpu_env);
    return true;
}

static bool trans_tlbwr(DisasContext *ctx, arg_tlbwr *a)
{
    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_tlbwr(cpu_env);
    check_mmu_idx(ctx);
    return true;
}

static bool trans_tlbfill(DisasContext *ctx, arg_tlbfill *a)
{
    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_tlbfill(cpu_env);
    check_mmu_idx(ctx);
    return true;
}

static bool trans_tlbclr(DisasContext *ctx, arg_tlbclr *a)
{
    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_tlbclr(cpu_env);
    check_mmu_idx(ctx);
    return true;
}

static bool trans_tlbflush(DisasContext *ctx, arg_tlbflush *a)
{
    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_tlbflush(cpu_env);
    check_mmu_idx(ctx);
    return true;
}

static bool trans_invtlb(DisasContext *ctx, arg_invtlb *a)
{
    TCGv rj = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv rk = gpr_src(ctx, a->rk, EXT_NONE);

    if (check_plv(ctx)) {
        return false;
    }

    switch (a->imm) {
    case 0:
    case 1:
        gen_helper_invtlb_all(cpu_env);
        break;
    case 2:
        gen_helper_invtlb_all_g(cpu_env, tcg_constant_i32(1));
        break;
    case 3:
        gen_helper_invtlb_all_g(cpu_env, tcg_constant_i32(0));
        break;
    case 4:
        gen_helper_invtlb_all_asid(cpu_env, rj);
        break;
    case 5:
        gen_helper_invtlb_page_asid(cpu_env, rj, rk);
        break;
    case 6:
        gen_helper_invtlb_page_asid_or_g(cpu_env, rj, rk);
        break;
    default:
        return false;
    }
    ctx->base.is_jmp = DISAS_STOP;
    return true;
}

static bool trans_cacop(DisasContext *ctx, arg_cacop *a)
{
    /* Treat the cacop as a nop */
    if (check_plv(ctx)) {
        return false;
    }
    return true;
}

static bool trans_ldpte(DisasContext *ctx, arg_ldpte *a)
{
    TCGv_i32 mem_idx = tcg_constant_i32(ctx->mem_idx);
    TCGv src1 = gpr_src(ctx, a->rj, EXT_NONE);

    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_ldpte(cpu_env, src1, tcg_constant_tl(a->imm), mem_idx);
    return true;
}

static bool trans_lddir(DisasContext *ctx, arg_lddir *a)
{
    TCGv_i32 mem_idx = tcg_constant_i32(ctx->mem_idx);
    TCGv src = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);

    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_lddir(dest, cpu_env, src, tcg_constant_tl(a->imm), mem_idx);
    return true;
}

static bool trans_ertn(DisasContext *ctx, arg_ertn *a)
{
    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_ertn(cpu_env);
    ctx->base.is_jmp = DISAS_EXIT;
    return true;
}

static bool trans_dbcl(DisasContext *ctx, arg_dbcl *a)
{
    if (check_plv(ctx)) {
        return false;
    }
    generate_exception(ctx, EXCCODE_DBP);
    return true;
}

static bool trans_idle(DisasContext *ctx, arg_idle *a)
{
    if (check_plv(ctx)) {
        return false;
    }

    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next + 4);
    gen_helper_idle(cpu_env);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}
#endif
