/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 * LoongArch translation routines for the privileged instructions.
 */

#include "csr.h"

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

static bool check_plv(DisasContext *ctx)
{
    if (ctx->plv == MMU_PLV_USER) {
        generate_exception(ctx, EXCCODE_IPE);
        return true;
    }
    return false;
}

static bool set_csr_trans_func(unsigned int csr_num, GenCSRRead readfn,
                               GenCSRWrite writefn)
{
    CSRInfo *csr;

    csr = get_csr(csr_num);
    if (!csr) {
        return false;
    }

    csr->readfn = (GenCSRFunc)readfn;
    csr->writefn = (GenCSRFunc)writefn;
    return true;
}

#define SET_CSR_FUNC(NAME, read, write)                 \
        set_csr_trans_func(LOONGARCH_CSR_##NAME, read, write)

void loongarch_csr_translate_init(void)
{
    SET_CSR_FUNC(STLBPS, NULL, gen_helper_csrwr_stlbps);
    SET_CSR_FUNC(ESTAT, NULL, gen_helper_csrwr_estat);
    SET_CSR_FUNC(ASID,  NULL, gen_helper_csrwr_asid);
    SET_CSR_FUNC(PGD,   gen_helper_csrrd_pgd, NULL);
    SET_CSR_FUNC(PWCL,  NULL, gen_helper_csrwr_pwcl);
    SET_CSR_FUNC(PWCH,  NULL, gen_helper_csrwr_pwch);
    SET_CSR_FUNC(CPUID, gen_helper_csrrd_cpuid, NULL);
    SET_CSR_FUNC(TCFG,  NULL, gen_helper_csrwr_tcfg);
    SET_CSR_FUNC(TVAL,  gen_helper_csrrd_tval, NULL);
    SET_CSR_FUNC(TICLR, NULL, gen_helper_csrwr_ticlr);
    SET_CSR_FUNC(MSGIR, gen_helper_csrrd_msgir, NULL);
}
#undef SET_CSR_FUNC

static bool check_csr_flags(DisasContext *ctx, const CSRInfo *csr, bool write)
{
    if ((csr->flags & CSRFL_READONLY) && write) {
        return false;
    }
    if ((csr->flags & CSRFL_IO) && translator_io_start(&ctx->base)) {
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
    GenCSRRead readfn;

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
        readfn = (GenCSRRead)csr->readfn;
        if (readfn) {
            readfn(dest, tcg_env);
        } else {
            tcg_gen_ld_tl(dest, tcg_env, csr->offset);
        }
    }
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool trans_csrwr(DisasContext *ctx, arg_csrwr *a)
{
    TCGv dest, src1;
    const CSRInfo *csr;
    GenCSRWrite writefn;

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
    writefn = (GenCSRWrite)csr->writefn;
    if (writefn) {
        dest = gpr_dst(ctx, a->rd, EXT_NONE);
        writefn(dest, tcg_env, src1);
    } else {
        dest = tcg_temp_new();
        tcg_gen_ld_tl(dest, tcg_env, csr->offset);
        tcg_gen_st_tl(src1, tcg_env, csr->offset);
    }
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool trans_csrxchg(DisasContext *ctx, arg_csrxchg *a)
{
    TCGv src1, mask, oldv, newv, temp;
    const CSRInfo *csr;
    GenCSRWrite writefn;

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

    tcg_gen_ld_tl(oldv, tcg_env, csr->offset);
    tcg_gen_and_tl(newv, src1, mask);
    tcg_gen_andc_tl(temp, oldv, mask);
    tcg_gen_or_tl(newv, newv, temp);

    writefn = (GenCSRWrite)csr->writefn;
    if (writefn) {
        writefn(oldv, tcg_env, newv);
    } else {
        tcg_gen_st_tl(newv, tcg_env, csr->offset);
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
    func(dest, tcg_env, src1);
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
    func(tcg_env, addr, val);
    return true;
}

TRANS(iocsrrd_b, IOCSR, gen_iocsrrd, gen_helper_iocsrrd_b)
TRANS(iocsrrd_h, IOCSR, gen_iocsrrd, gen_helper_iocsrrd_h)
TRANS(iocsrrd_w, IOCSR, gen_iocsrrd, gen_helper_iocsrrd_w)
TRANS64(iocsrrd_d, IOCSR, gen_iocsrrd, gen_helper_iocsrrd_d)
TRANS(iocsrwr_b, IOCSR, gen_iocsrwr, gen_helper_iocsrwr_b)
TRANS(iocsrwr_h, IOCSR, gen_iocsrwr, gen_helper_iocsrwr_h)
TRANS(iocsrwr_w, IOCSR, gen_iocsrwr, gen_helper_iocsrwr_w)
TRANS64(iocsrwr_d, IOCSR, gen_iocsrwr, gen_helper_iocsrwr_d)

static void check_mmu_idx(DisasContext *ctx)
{
    if (ctx->mem_idx != MMU_DA_IDX) {
        tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next + 4);
        ctx->base.is_jmp = DISAS_EXIT;
    }
}

static bool trans_tlbsrch(DisasContext *ctx, arg_tlbsrch *a)
{
    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_tlbsrch(tcg_env);
    return true;
}

static bool trans_tlbrd(DisasContext *ctx, arg_tlbrd *a)
{
    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_tlbrd(tcg_env);
    return true;
}

static bool trans_tlbwr(DisasContext *ctx, arg_tlbwr *a)
{
    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_tlbwr(tcg_env);
    check_mmu_idx(ctx);
    return true;
}

static bool trans_tlbfill(DisasContext *ctx, arg_tlbfill *a)
{
    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_tlbfill(tcg_env);
    check_mmu_idx(ctx);
    return true;
}

static bool trans_tlbclr(DisasContext *ctx, arg_tlbclr *a)
{
    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_tlbclr(tcg_env);
    check_mmu_idx(ctx);
    return true;
}

static bool trans_tlbflush(DisasContext *ctx, arg_tlbflush *a)
{
    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_tlbflush(tcg_env);
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
        gen_helper_invtlb_all(tcg_env);
        break;
    case 2:
        gen_helper_invtlb_all_g(tcg_env, tcg_constant_i32(1));
        break;
    case 3:
        gen_helper_invtlb_all_g(tcg_env, tcg_constant_i32(0));
        break;
    case 4:
        gen_helper_invtlb_all_asid(tcg_env, rj);
        break;
    case 5:
        gen_helper_invtlb_page_asid(tcg_env, rj, rk);
        break;
    case 6:
        gen_helper_invtlb_page_asid_or_g(tcg_env, rj, rk);
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

    if (!avail_LSPW(ctx)) {
        return true;
    }

    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_ldpte(tcg_env, src1, tcg_constant_tl(a->imm), mem_idx);
    return true;
}

static bool trans_lddir(DisasContext *ctx, arg_lddir *a)
{
    TCGv_i32 mem_idx = tcg_constant_i32(ctx->mem_idx);
    TCGv src = gpr_src(ctx, a->rj, EXT_NONE);
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);

    if (!avail_LSPW(ctx)) {
        return true;
    }

    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_lddir(dest, tcg_env, src, tcg_constant_i32(a->imm), mem_idx);
    return true;
}

static bool trans_ertn(DisasContext *ctx, arg_ertn *a)
{
    if (check_plv(ctx)) {
        return false;
    }
    gen_helper_ertn(tcg_env);
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
    gen_helper_idle(tcg_env);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}
#endif
