/*
 *  csky_v1 translation
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2011 C-SKY Microsystems, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "translate.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/log.h"
#include "exec/translator.h"
#include "tcg-op.h"
#include "trace-tcg.h"
#include "qemu/log.h"
#include "exec/gdbstub.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

/* is_jmp field values */
#define DISAS_JUMP    DISAS_TARGET_0 /* only pc was modified dynamically */
#define DISAS_UPDATE  DISAS_TARGET_1 /* cpu state was modified dynamically */
#define DISAS_TB_JUMP DISAS_TARGET_2 /* only pc was modified statically */

static const char *regnames[] = {
    "sp", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" };

static TCGv_i32 cpu_R[16];
static TCGv_i32 cpu_c;
static TCGv_i32 cpu_v;
static TCGv_i32 cpu_s;
static TCGv_i32 cpu_hi ;
static TCGv_i32 cpu_lo;
static TCGv_i32 cpu_hi_guard;
static TCGv_i32 cpu_lo_guard;

#include "exec/gen-icount.h"

#if defined(CONFIG_USER_ONLY)
#define IS_SUPER(dc)  0
#else
#define IS_SUPER(dc)  (dc->super)
#endif

/* initialize TCG globals. */
void csky_translate_init(void)
{
    int i;

    for (i = 0; i < 16; i++) {
        cpu_R[i] = tcg_global_mem_new_i32(cpu_env,
                            offsetof(CPUCSKYState, regs[i]),
                            regnames[i]);
    }
    cpu_c = tcg_global_mem_new_i32(cpu_env,
        offsetof(CPUCSKYState, psr_c), "cpu_c");
    cpu_v = tcg_global_mem_new_i32(cpu_env,
        offsetof(CPUCSKYState, dcsr_v), "cpu_v");
    cpu_s = tcg_global_mem_new_i32(cpu_env,
        offsetof(CPUCSKYState, psr_s), "cpu_s");
    cpu_hi = tcg_global_mem_new_i32(cpu_env,
        offsetof(CPUCSKYState, hi), "cpu_hi");
    cpu_lo = tcg_global_mem_new_i32(cpu_env,
        offsetof(CPUCSKYState, lo), "cpu_lo");
    cpu_hi_guard = tcg_global_mem_new_i32(cpu_env,
        offsetof(CPUCSKYState, hi_guard), "cpu_hi_guard");
    cpu_lo_guard = tcg_global_mem_new_i32(cpu_env,
        offsetof(CPUCSKYState, lo_guard), "cpu_lo_guard");

}


static inline TCGv load_cpu_offset(int offset)
{
    TCGv tmp = tcg_temp_new();
    tcg_gen_ld_i32(tmp, cpu_env, offset);
    return tmp;
}

#define load_cpu_field(name) load_cpu_offset(offsetof(CPUCSKYState, name))

static inline void store_cpu_offset(TCGv var, int offset)
{
    tcg_gen_st_i32(var, cpu_env, offset);
}

#define store_cpu_field(var, name) \
store_cpu_offset(var, offsetof(CPUCSKYState, name))

static inline void gen_save_pc(target_ulong pc)
{
    TCGv t0 = tcg_temp_new();

    t0 = tcg_const_i32(pc);
    store_cpu_field(t0, pc);

    tcg_temp_free(t0);
}

static inline void generate_exception(DisasContext *ctx, int excp)
{
    TCGv t0 = tcg_temp_new();

    print_exception(ctx, excp);

    t0 = tcg_const_i32(excp);
    gen_save_pc(ctx->pc);
    gen_helper_exception(cpu_env, t0);
    ctx->is_jmp = DISAS_UPDATE;

    tcg_temp_free(t0);
}

static inline bool use_goto_tb(DisasContext *s, uint32_t dest)
{
#ifndef CONFIG_USER_ONLY
    return (s->tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK) ||
           (s->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK);
#else
    return true;
#endif
}

static inline void gen_goto_tb(DisasContext *ctx, int n, uint32_t dest)
{
    TranslationBlock *tb;
    TCGv t0 = tcg_temp_new();

    tb = ctx->tb;

    if (unlikely(ctx->singlestep_enabled)) {
        gen_save_pc(dest);
        t0 = tcg_const_tl(EXCP_DEBUG);
        gen_helper_exception(cpu_env, t0);
    }
#if !defined(CONFIG_USER_ONLY)
    else if (unlikely((ctx->trace_mode == INST_TRACE_MODE)
                || (ctx->trace_mode == BRAN_TRACE_MODE))) {
        gen_save_pc(dest);
        t0 = tcg_const_tl(EXCP_CSKY_TRACE);
        gen_helper_exception(cpu_env, t0);
        ctx->maybe_change_flow = 1;
    }
#endif
    else if (use_goto_tb(ctx, dest)) {
        tcg_gen_goto_tb(n);
        gen_save_pc(dest);
        tcg_gen_exit_tb((uintptr_t)tb + n);
    } else {
        gen_save_pc(dest);
        tcg_gen_exit_tb(0);
    }

    tcg_temp_free(t0);
}

static inline void check_insn(DisasContext *ctx, uint32_t flags)
{
    if (unlikely(!has_insn(ctx, flags))) {
        generate_exception(ctx, EXCP_CSKY_UDEF);
    }
}

#ifndef CONFIG_USER_ONLY
/* generate mfcr instruction */
static inline void gen_mfcr(DisasContext *ctx, uint32_t rz, uint32_t cr_num)
{
    TCGv t0;

    switch (cr_num) {
    case 0x0:
        /* cr0 psr */
        gen_helper_mfcr_cr0(cpu_R[rz], cpu_env);
        break;
    case 0x1:
        /* vbr */
        t0 = load_cpu_field(cp0.vbr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x2:
        /* epsr */
        t0 = load_cpu_field(cp0.epsr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x3:
        /* fpsr */
        t0 = load_cpu_field(cp0.fpsr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x4:
        /* epc */
        t0 = load_cpu_field(cp0.epc);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x5:
        /* fpc */
        t0 = load_cpu_field(cp0.fpc);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x6:
        /* ss0 */
        t0 = load_cpu_field(cp0.ss0);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x7:
        /* ss1 */
        t0 = load_cpu_field(cp0.ss1);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x8:
        /* ss2 */
        t0 = load_cpu_field(cp0.ss2);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x9:
        /* ss3 */
        t0 = load_cpu_field(cp0.ss3);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0xa:
        /* ss4 */
        t0 = load_cpu_field(cp0.ss4);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0xb:
        /* gcr */
        t0 = load_cpu_field(cp0.gcr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0xc:
        /* gsr */
        t0 = load_cpu_field(cp0.gsr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0xd:
        /* cpidr */
        t0 = load_cpu_field(cp0.cpidr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0xe:
        /* cr14 dcsr */
        t0 = load_cpu_field(cp0.dcsr);
        tcg_gen_andi_tl(cpu_R[rz], t0, ~0x1);
        t0 = load_cpu_field(dcsr_v);
        tcg_gen_or_tl(cpu_R[rz], cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0xf:
        /* cpwr */
        t0 = load_cpu_field(cp0.cpwr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x10:
        /* no CR16 */
        break;
    case 0x11:
        /* cfr */
        t0 = load_cpu_field(cp0.cfr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x12:
        /* ccr */
        t0 = load_cpu_field(cp0.ccr);
        tcg_gen_mov_tl(cpu_R[rz], t0);


        tcg_temp_free(t0);
        break;
    case 0x13:
        /* capr */
        t0 = load_cpu_field(cp0.capr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x14:
        /* cr20 pacr */
        gen_helper_mfcr_cr20(cpu_R[rz], cpu_env);
        break;
    case 0x15:
        /* prsr */
        t0 = load_cpu_field(cp0.prsr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    default:
        break;
    }
}

/* generate mtcr instruction */
static inline void gen_mtcr(DisasContext *ctx, uint32_t cr_num, uint32_t rx)
{

    switch (cr_num) {
    case 0x0:
        /* psr */
        gen_helper_mtcr_cr0(cpu_env, cpu_R[rx]);
        gen_save_pc(ctx->pc + 2);
        ctx->is_jmp = DISAS_UPDATE;
        break;
    case 0x1:
        /* vbr */
        {
            TCGv t0 = tcg_temp_new();

            tcg_gen_andi_tl(t0, cpu_R[rx], ~0x3ff);
            store_cpu_field(t0, cp0.vbr);

            tcg_temp_free(t0);
        }
        break;
    case 0x2:
        /* epsr */
        store_cpu_field(cpu_R[rx], cp0.epsr);
        break;
    case 0x3:
        /* fpsr */
        store_cpu_field(cpu_R[rx], cp0.fpsr);
        break;
    case 0x4:
        /* epc */
        store_cpu_field(cpu_R[rx], cp0.epc);
        break;
    case 0x5:
        /* fpc */
        store_cpu_field(cpu_R[rx], cp0.fpc);
        break;
    case 0x6:
        /* ss0 */
        store_cpu_field(cpu_R[rx], cp0.ss0);
        break;
    case 0x7:
        /* ss1 */
        store_cpu_field(cpu_R[rx], cp0.ss1);
        break;
    case 0x8:
        /* ss2 */
        store_cpu_field(cpu_R[rx], cp0.ss2);
        break;
    case 0x9:
        /* ss3 */
        store_cpu_field(cpu_R[rx], cp0.ss3);
        break;
    case 0xa:
        /* ss4 */
        store_cpu_field(cpu_R[rx], cp0.ss4);
        break;
    case 0xb:
        /* gcr */
        store_cpu_field(cpu_R[rx], cp0.gcr);
        break;
    case 0xc:
        /* gsr */
        /* Read only */
        break;
    case 0xd:
        /* cpidr */
        /* Read only */
        break;
    case 0xe:
        /* dcsr */
        {
            TCGv t0, t1;

            t0 = load_cpu_field(cp0.dcsr);
            t1 = load_cpu_field(dcsr_v);
            tcg_gen_andi_tl(t0, t0, ~0x1);
            tcg_gen_or_tl(t1, t1, t0);
            store_cpu_field(t1, cp0.dcsr);

            tcg_temp_free(t0);
            tcg_temp_free(t1);
        }
        break;
    case 0xf:
        /* cpwr */
        /* FIXME */
        store_cpu_field(cpu_R[rx], cp0.cpwr);
        break;
    case 0x10:
        /* no CR16 */
        break;
    case 0x11:
        /* cfr */
        store_cpu_field(cpu_R[rx], cp0.cfr);
        break;
    case 0x12:
        /* ccr */
        gen_helper_mtcr_cr18(cpu_env, cpu_R[rx]);
        gen_save_pc(ctx->pc + 2);
        ctx->is_jmp = DISAS_UPDATE;
        break;
    case 0x13:
        /* capr */
        store_cpu_field(cpu_R[rx], cp0.capr);
        break;
    case 0x14:
        /* pacr */
        gen_helper_mtcr_cr20(cpu_env, cpu_R[rx]);
        gen_save_pc(ctx->pc + 2);
        ctx->is_jmp = DISAS_UPDATE;
        break;
    case 0x15:
        /* psrsr */
        store_cpu_field(cpu_R[rx], cp0.prsr);
        break;
    default:
        break;
    }
}

/* Read MMU Coprocessor Contronl Registers */
static inline void
gen_cprcr_cp15(DisasContext *ctx, uint32_t rz, uint32_t cr_num)
{
    TCGv t0;

    switch (cr_num) {
    case 0x0:
        /* MIR */
        t0 = load_cpu_field(mmu.mir);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x1:
        /* MRR */
        t0 = load_cpu_field(mmu.mrr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x2:
        /* MEL0 */
        t0 = load_cpu_field(mmu.mel0);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x3:
        /* MEL1 */
        t0 = load_cpu_field(mmu.mel1);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x4:
        /* MEH */
        t0 = load_cpu_field(mmu.meh);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x5:
        /* MCR */
        t0 = load_cpu_field(mmu.mcr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x6:
        /* MPR */
        t0 = load_cpu_field(mmu.mpr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x7:
        /* MWR */
        t0 = load_cpu_field(mmu.mwr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x8:
        /* MCIR */
        t0 = load_cpu_field(mmu.mcir);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0x9:
        /* CP15_CR9 */
        t0 = load_cpu_field(mmu.cr9);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0xa:
        /* CP15_CR10 */
        t0 = load_cpu_field(mmu.cr10);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0xb:
        /* CP15_CR11 */
        t0 = load_cpu_field(mmu.cr11);
        tcg_gen_mov_tl(cpu_R[rz], t0);

        tcg_temp_free(t0);
        break;
    case 0xc:
        /* CP15_CR12 */
        t0 = load_cpu_field(mmu.cr12);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0xd:
        /* CP15_CR13 */
        t0 = load_cpu_field(mmu.cr13);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0xe:
        /* CP15_CR14 */
        t0 = load_cpu_field(mmu.cr14);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0xf:
        /* CP15_CR15 */
        t0 = load_cpu_field(mmu.cr15);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x10:
        /* CP15_CR16 */
        t0 = load_cpu_field(mmu.cr16);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x1d:
        /* CP15_mpar */
        t0 = load_cpu_field(mmu.mpar);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    default:
        break;
    }
}

/* Write MMU Coprocessor Contronl Registers */
static inline void
gen_cpwcr_cp15(DisasContext *ctx, uint32_t cr_num, uint32_t rx)
{
    switch (cr_num) {
    case 0x0:
        /* MIR */
        store_cpu_field(cpu_R[rx], mmu.mir);
        break;
    case 0x1:
        /* MRR */
        store_cpu_field(cpu_R[rx], mmu.mrr);
        break;
    case 0x2:
        /* MEL0 */
        store_cpu_field(cpu_R[rx], mmu.mel0);
        break;
    case 0x3:
        /* MEL1 */
        store_cpu_field(cpu_R[rx], mmu.mel1);
        break;
    case 0x4:
        /* MEH */
        gen_helper_meh_write(cpu_env, cpu_R[rx]);
        gen_save_pc(ctx->pc + 2);
        ctx->is_jmp = DISAS_UPDATE;
        break;
    case 0x5:
        /* MCR */
        store_cpu_field(cpu_R[rx], mmu.mcr);
        break;
    case 0x6:
        /* MPR */
        store_cpu_field(cpu_R[rx], mmu.mpr);
        break;
    case 0x7:
        /* MWR */
        store_cpu_field(cpu_R[rx], mmu.mwr);
        break;
    case 0x8:
        /* MCIR */
        gen_helper_mcir_write(cpu_env, cpu_R[rx]);
        gen_save_pc(ctx->pc + 2);
        ctx->is_jmp = DISAS_UPDATE;
        break;
    case 0x9:
        /* FIXME SPM is not implement yet */
        /* CP15_CR9 */
        store_cpu_field(cpu_R[rx], mmu.cr9);
        break;
    case 0xa:
        /* CP15_CR10 */
        store_cpu_field(cpu_R[rx], mmu.cr10);
        break;
    case 0xb:
        /* CP15_CR11 */
        store_cpu_field(cpu_R[rx], mmu.cr11);
        break;
    case 0xc:
        /* CP15_CR12 */
        store_cpu_field(cpu_R[rx], mmu.cr12);
        break;
    case 0xd:
        /* CP15_CR13 */
        store_cpu_field(cpu_R[rx], mmu.cr13);
        break;
    case 0xe:
        /* CP15_CR14 */
        store_cpu_field(cpu_R[rx], mmu.cr14);
        break;
    case 0xf:
        /* CP15_CR15 */
        store_cpu_field(cpu_R[rx], mmu.cr15);
        break;
    case 0x10:
        /* CP15_CR16 */
        store_cpu_field(cpu_R[rx], mmu.cr16);
        break;
    case 0x1d:
        /* CP15_mpar */
        store_cpu_field(cpu_R[rx], mmu.mpar);
        gen_save_pc(ctx->pc + 2);
        ctx->is_jmp = DISAS_UPDATE;
        break;
    default:
        break;
    }
}
#endif /* !CONFIG_USER_ONLY */

static inline void tstnbz(int rx)
{
    TCGv t0 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();
    tcg_gen_movi_tl(cpu_c, 0);
    tcg_gen_andi_tl(t0, cpu_R[rx], 0xff000000);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
    tcg_gen_andi_tl(t0, cpu_R[rx], 0x00ff0000);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
    tcg_gen_andi_tl(t0, cpu_R[rx], 0x0000ff00);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
    tcg_gen_andi_tl(t0, cpu_R[rx], 0x000000ff);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
    tcg_gen_movi_tl(cpu_c, 1);
    gen_set_label(l1);

    tcg_temp_free(t0);
}

static inline void mac(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_extu_tl_i64(t0, cpu_R[rx]);
    tcg_gen_extu_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_concat_tl_i64(t1, cpu_lo, cpu_hi);
    tcg_gen_add_i64(t0, t0, t1);

    tcg_gen_trunc_i64_tl(cpu_lo, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_trunc_i64_tl(cpu_hi, t0);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void addc(int rx, int ry)
{
    TCGv t0 = tcg_temp_local_new();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_mov_tl(t0, cpu_R[rx]);
    tcg_gen_add_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
    tcg_gen_add_tl(cpu_R[rx], cpu_R[rx], cpu_c);
    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
    tcg_gen_setcond_tl(TCG_COND_LTU, cpu_c, cpu_R[rx], t0);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_setcond_tl(TCG_COND_LEU, cpu_c, cpu_R[rx], t0);
    gen_set_label(l2);

    tcg_temp_free(t0);
}

static inline void subc(int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_local_new();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_subfi_tl(t0, 1, cpu_c);
    tcg_gen_mov_tl(t1, cpu_R[rx]);
    tcg_gen_sub_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
    tcg_gen_sub_tl(cpu_R[rx], cpu_R[rx], t0);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
    tcg_gen_setcond_tl(TCG_COND_GTU, cpu_c, t1, cpu_R[ry]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_setcond_tl(TCG_COND_GEU, cpu_c, t1, cpu_R[ry]);
    gen_set_label(l2);

}

static inline void lsr(int rx, int ry)
{
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_movi_tl(t1, 0);
    tcg_gen_andi_tl(t0, cpu_R[ry], 0x3f);
    tcg_gen_brcondi_tl(TCG_COND_GTU, t0, 31, l1);
    tcg_gen_shr_tl(t1, cpu_R[rx], t0);
    gen_set_label(l1);
    tcg_gen_mov_tl(cpu_R[rx], t1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static inline void lsl(int rx, int ry)
{
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_movi_tl(t1, 0);
    tcg_gen_andi_tl(t0, cpu_R[ry], 0x3f);
    tcg_gen_brcondi_tl(TCG_COND_GTU, t0, 31, l1);
    tcg_gen_shl_tl(t1, cpu_R[rx], t0);
    gen_set_label(l1);
    tcg_gen_mov_tl(cpu_R[rx], t1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static inline void bgenr(int rx, int ry)
{
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();
    TCGLabel *l1 = gen_new_label();
    tcg_gen_mov_tl(t1, cpu_R[ry]);  /* maybe rx==ry, so ry's saved in temp */
    tcg_gen_movi_tl(cpu_R[rx], 0);
    tcg_gen_andi_tl(t0, t1, 0x20);
    tcg_gen_brcondi_tl(TCG_COND_NE, t0, 0, l1);
    tcg_gen_andi_tl(t1, t1, 0x1f);
    tcg_gen_movi_tl(t0, 1);
    tcg_gen_shl_tl(cpu_R[rx], t0, t1);
    gen_set_label(l1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static inline void asr(int rx, int ry)
{
    TCGv t0 = tcg_temp_local_new();
    TCGLabel *l1 = gen_new_label();
    tcg_gen_andi_tl(t0, cpu_R[ry], 0x3f);
    tcg_gen_brcondi_tl(TCG_COND_LEU, t0, 31, l1);
    tcg_gen_movi_tl(t0, 31);
    gen_set_label(l1);
    tcg_gen_sar_tl(cpu_R[rx], cpu_R[rx], t0);

    tcg_temp_free(t0);
}


static inline void divu(DisasContext *ctx, int rx)
{
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();

    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_R[1], 0, l1);
    tcg_gen_divu_tl(cpu_R[rx], cpu_R[rx], cpu_R[1]);
    tcg_gen_br(l2);
    gen_set_label(l1);

    TCGv t0 = tcg_temp_new();

    t0 = tcg_const_i32(EXCP_CSKY_DIV);
    gen_save_pc(ctx->pc);
    gen_helper_exception(cpu_env, t0);
    ctx->is_jmp = DISAS_NEXT;

    tcg_temp_free(t0);

    gen_set_label(l2);
}

static inline void divs(DisasContext *ctx, int rx)
{
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();

    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_R[1], 0, l1);
    tcg_gen_div_tl(cpu_R[rx], cpu_R[rx], cpu_R[1]);
    tcg_gen_br(l2);
    gen_set_label(l1);

    TCGv t0 = tcg_temp_new();

    t0 = tcg_const_i32(EXCP_CSKY_DIV);
    gen_save_pc(ctx->pc);
    gen_helper_exception(cpu_env, t0);
    ctx->is_jmp = DISAS_NEXT;

    tcg_temp_free(t0);

    gen_set_label(l2);
}

static inline void xsr(int rx)
{
    TCGv t0 = tcg_temp_new();

    tcg_gen_andi_tl(t0, cpu_R[rx], 0x1);
    tcg_gen_shri_tl(cpu_R[rx], cpu_R[rx], 1);
    tcg_gen_shli_tl(cpu_c, cpu_c, 31);
    tcg_gen_or_tl(cpu_R[rx], cpu_R[rx], cpu_c);
    tcg_gen_mov_tl(cpu_c, t0);

    tcg_temp_free(t0);
}

static inline void muls(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_movi_tl(cpu_v, 0);
    tcg_gen_movi_tl(cpu_lo_guard, 0x00000000);
    tcg_gen_movi_tl(cpu_hi_guard, 0x00000000);
    tcg_gen_ext_tl_i64(t0, cpu_R[rx]);
    tcg_gen_ext_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_trunc_i64_tl(cpu_lo, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_hi, t0);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void mulsa(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    TCGv_i64 t4 = tcg_temp_new_i64();
    TCGv_i32 t5 = tcg_temp_new_i32();

    tcg_gen_ext_tl_i64(t0, cpu_R[rx]);
    tcg_gen_ext_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_concat_i32_i64(t1, cpu_lo, cpu_hi);
    tcg_gen_add_i64(t2, t0, t1);

    tcg_gen_xor_i64(t3, t0, t1);
    tcg_gen_xor_i64(t4, t0, t2);
    tcg_gen_andc_i64(t4, t4, t3);

    TCGv_i64 tx = tcg_temp_new_i64();
    tcg_gen_shri_i64(tx, t4, 63);
    tcg_gen_extrl_i64_i32(t5, tx);
    tcg_gen_mov_i32(cpu_v, t5);
    tcg_temp_free_i64(tx);

    tcg_gen_trunc_i64_tl(cpu_lo, t2);
    tcg_gen_shri_i64(t2, t2, 32);
    tcg_gen_trunc_i64_tl(cpu_hi, t2);

    tcg_gen_movi_tl(cpu_lo_guard, 0x00000000);
    tcg_gen_movi_tl(cpu_hi_guard, 0x00000000);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free_i64(t3);
    tcg_temp_free_i64(t4);
}

static inline void mulss(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    TCGv_i64 t4 = tcg_temp_new_i64();
    TCGv_i32 t5 = tcg_temp_new_i32();

    tcg_gen_ext_tl_i64(t0, cpu_R[rx]);
    tcg_gen_ext_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_concat_i32_i64(t1, cpu_lo, cpu_hi);
    tcg_gen_sub_i64(t2, t1, t0);

    tcg_gen_xor_i64(t3, t0, t1);
    tcg_gen_xor_i64(t4, t0, t2);
    tcg_gen_andc_i64(t4, t3, t4);

    TCGv_i64 tx = tcg_temp_new_i64();
    tcg_gen_shri_i64(tx, t4, 63);
    tcg_gen_extrl_i64_i32(t5, tx);
    tcg_gen_mov_i32(cpu_v, t5);
    tcg_temp_free_i64(tx);

    tcg_gen_shri_i64(t3, t2, 63);
    tcg_gen_trunc_i64_tl(cpu_lo, t2);
    tcg_gen_shri_i64(t2, t2, 32);
    tcg_gen_trunc_i64_tl(cpu_hi, t2);

    tcg_gen_movi_tl(cpu_lo_guard, 0x00000000);
    tcg_gen_movi_tl(cpu_hi_guard, 0x00000000);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free_i64(t3);
    tcg_temp_free_i64(t4);

}

static inline void mulu(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_movi_tl(cpu_v, 0);
    tcg_gen_movi_tl(cpu_lo_guard, 0x00000000);
    tcg_gen_movi_tl(cpu_hi_guard, 0x00000000);

    tcg_gen_extu_tl_i64(t0, cpu_R[rx]);
    tcg_gen_extu_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_trunc_i64_tl(cpu_lo, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_hi, t0);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void mulua(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();

    tcg_gen_extu_tl_i64(t0, cpu_R[rx]);
    tcg_gen_extu_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t1, t0);
    tcg_gen_concat_i32_i64(t1, cpu_lo, cpu_hi);
    tcg_gen_add_i64(t2, t1, t0);

    tcg_gen_trunc_i64_tl(cpu_lo, t2);
    tcg_gen_shri_i64(t2, t2, 32);
    tcg_gen_trunc_i64_tl(cpu_hi, t2);
    TCGv_i64 tx = tcg_temp_new_i64();
    tcg_gen_setcond_i64(TCG_COND_LT, tx, t2, t1);
    tcg_gen_extrl_i64_i32(cpu_v, tx);
    tcg_temp_free_i64(tx);

    tcg_gen_movi_tl(cpu_lo_guard, 0x00000000);
    tcg_gen_movi_tl(cpu_hi_guard, 0x00000000);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
}

static inline void mulus(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();

    tcg_gen_extu_tl_i64(t0, cpu_R[rx]);
    tcg_gen_extu_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t1, t0);
    tcg_gen_concat_i32_i64(t1, cpu_lo, cpu_hi);

    TCGv_i64 tx = tcg_temp_new_i64();
    tcg_gen_setcond_i64(TCG_COND_LT, tx, t1, t0);
    tcg_gen_extrl_i64_i32(cpu_v, tx);
    tcg_temp_free_i64(tx);
    tcg_gen_sub_i64(t2, t1, t0);

    tcg_gen_trunc_i64_tl(cpu_lo, t2);
    tcg_gen_shri_i64(t2, t2, 32);
    tcg_gen_trunc_i64_tl(cpu_hi, t2);

    tcg_gen_movi_tl(cpu_lo_guard, 0x00000000);
    tcg_gen_movi_tl(cpu_hi_guard, 0x00000000);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);

}

static inline void mulsha(int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_local_new_i64();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_ext16s_tl(t0, cpu_R[rx]);
    tcg_gen_ext16s_tl(t1, cpu_R[ry]);
    tcg_gen_mul_tl(t0, t0, t1);
    tcg_gen_ext_tl_i64(t2, t0);
    tcg_gen_concat_i32_i64(t3, cpu_lo, cpu_lo_guard);
    tcg_gen_add_i64(t2, t3, t2);
    tcg_gen_extrl_i64_i32(cpu_lo, t2);
    tcg_gen_shri_i64(t3, t2, 31);
    tcg_gen_shri_i64(t2, t2, 32);
    tcg_gen_extrl_i64_i32(cpu_lo_guard, t2);
    tcg_gen_movi_i32(cpu_v, 0);
    tcg_gen_brcondi_i64(TCG_COND_EQ, t3, 0x0, l1);
    tcg_gen_brcondi_i64(TCG_COND_EQ, t3, 0x1ffffffffLL, l1);
    tcg_gen_movi_i32(cpu_v, 1);
    gen_set_label(l1);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free_i64(t3);
}

static inline void mulshs(int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_local_new_i64();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_ext16s_tl(t0, cpu_R[rx]);
    tcg_gen_ext16s_tl(t1, cpu_R[ry]);
    tcg_gen_mul_tl(t0, t0, t1);
    tcg_gen_ext_tl_i64(t2, t0);
    tcg_gen_concat_i32_i64(t3, cpu_lo, cpu_lo_guard);
    tcg_gen_sub_i64(t2, t3, t2);
    tcg_gen_extrl_i64_i32(cpu_lo, t2);
    tcg_gen_shri_i64(t3, t2, 31);
    tcg_gen_shri_i64(t2, t2, 32);
    tcg_gen_extrl_i64_i32(cpu_lo_guard, t2);
    tcg_gen_movi_i32(cpu_v, 0);
    tcg_gen_brcondi_i64(TCG_COND_EQ, t3, 0x0, l1);
    tcg_gen_brcondi_i64(TCG_COND_EQ, t3, 0x1ffffffffLL, l1);
    tcg_gen_movi_i32(cpu_v, 1);
    gen_set_label(l1);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free_i64(t3);

}

static inline void mulsw(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv t2 = tcg_temp_new();

    tcg_gen_ext16s_tl(t2, cpu_R[rx]);
    tcg_gen_ext_tl_i64(t0, t2);
    tcg_gen_ext_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 16);
    tcg_gen_extrl_i64_i32(cpu_R[rx], t0);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free(t2);
}

static inline void mulswa(int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv_i64 t1 = tcg_temp_local_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_ext16s_tl(t0, cpu_R[rx]) ;
    tcg_gen_ext_tl_i64(t1, t0);
    tcg_gen_ext_tl_i64(t2, cpu_R[ry]);
    tcg_gen_mul_i64(t1, t1, t2) ;
    tcg_gen_shri_i64(t1, t1, 16);
    tcg_gen_concat_i32_i64(t2, cpu_lo, cpu_lo_guard);
    tcg_gen_add_i64(t2, t2, t1);
    tcg_gen_extrl_i64_i32(cpu_lo, t2);
    tcg_gen_shri_i64(t1, t2, 31);
    tcg_gen_shri_i64(t2, t2, 32);
    tcg_gen_extrl_i64_i32(cpu_lo_guard, t2);
    tcg_gen_movi_i32(cpu_v, 0);
    tcg_gen_brcondi_i64(TCG_COND_EQ, t1, 0x0, l1);
    tcg_gen_brcondi_i64(TCG_COND_EQ, t1, 0x1ffffffffLL, l1);
    tcg_gen_movi_i32(cpu_v, 1);
    gen_set_label(l1);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);

}


static inline void mulsws(int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv_i64 t1 = tcg_temp_local_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_ext16s_tl(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, t0);
    tcg_gen_ext_i32_i64(t2, cpu_R[ry]);
    tcg_gen_mul_i64(t1, t1, t2);
    tcg_gen_shri_i64(t1, t1, 16);
    tcg_gen_concat_i32_i64(t2, cpu_lo, cpu_lo_guard);
    tcg_gen_sub_i64(t2, t2, t1);
    tcg_gen_extrl_i64_i32(cpu_lo, t2);
    tcg_gen_shri_i64(t1, t2, 31);
    tcg_gen_shri_i64(t2, t2, 32);
    tcg_gen_extrl_i64_i32(cpu_lo_guard, t2);
    tcg_gen_movi_i32(cpu_v, 0);
    tcg_gen_brcondi_i64(TCG_COND_EQ, t1, 0x0, l1);
    tcg_gen_brcondi_i64(TCG_COND_EQ, t1, 0x1ffffffffLL, l1);
    tcg_gen_movi_i32(cpu_v, 1);
    gen_set_label(l1);

    tcg_temp_free_i32(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);

}



static inline void vmulsh(int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    tcg_gen_movi_tl(cpu_v, 0);
    tcg_gen_ext16s_tl(t0, cpu_R[rx]);
    tcg_gen_ext16s_tl(t1, cpu_R[ry]);
    tcg_gen_mul_tl(cpu_lo, t0, t1);
    tcg_gen_sari_tl(cpu_lo_guard, cpu_lo, 31);
    tcg_gen_sari_tl(t0, cpu_R[rx], 16);
    tcg_gen_sari_tl(t1, cpu_R[ry], 16);
    tcg_gen_mul_tl(cpu_hi, t0, t1);
    tcg_gen_sari_tl(cpu_hi_guard, cpu_hi, 31);

    tcg_temp_free(t0);
    tcg_temp_free(t1);

}


static inline void vmulsha(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv t3 = tcg_temp_new();
    TCGv t4 = tcg_temp_local_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_ext16s_tl(t3, cpu_R[rx]);
    tcg_gen_ext16s_tl(t4,  cpu_R[ry]);
    tcg_gen_mul_tl(t3, t3, t4);
    tcg_gen_concat_i32_i64(t1, cpu_lo, cpu_lo_guard);
    tcg_gen_ext_tl_i64(t0, t3);
    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_extrl_i64_i32(cpu_lo, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_lo_guard, t0);

    tcg_gen_sari_tl(t3, cpu_R[rx], 16);
    tcg_gen_sari_tl(t4, cpu_R[ry], 16);
    tcg_gen_mul_tl(t3, t3, t4);
    tcg_gen_concat_i32_i64(t1, cpu_hi, cpu_hi_guard);
    tcg_gen_ext_tl_i64(t0, t3) ;
    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_extrl_i64_i32(cpu_hi, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_hi_guard, t0);

    tcg_gen_sari_tl(t3, cpu_lo, 31);
    tcg_gen_sari_tl(t4, cpu_hi, 31);
    tcg_gen_movi_tl(cpu_v, 1);
    tcg_gen_brcond_tl(TCG_COND_NE, t3, cpu_lo_guard, l1);
    tcg_gen_brcond_tl(TCG_COND_NE, t4, cpu_hi_guard, l1);
    tcg_gen_movi_tl(cpu_v, 0);
    gen_set_label(l1);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free(t3);
    tcg_temp_free(t4);

}


static inline void vmulshs(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv t3 = tcg_temp_new();
    TCGv t4 = tcg_temp_local_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_ext16s_tl(t3, cpu_R[rx]);
    tcg_gen_ext16s_tl(t4,  cpu_R[ry]);
    tcg_gen_mul_tl(t3, t3, t4);
    tcg_gen_concat_i32_i64(t1, cpu_lo, cpu_lo_guard);
    tcg_gen_ext_tl_i64(t0, t3);
    tcg_gen_sub_i64(t0, t1, t0);
    tcg_gen_extrl_i64_i32(cpu_lo, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_lo_guard, t0);

    tcg_gen_sari_tl(t3, cpu_R[rx], 16);
    tcg_gen_sari_tl(t4, cpu_R[ry], 16);
    tcg_gen_mul_tl(t3, t3, t4);
    tcg_gen_concat_i32_i64(t1, cpu_hi, cpu_hi_guard);
    tcg_gen_ext_tl_i64(t0, t3) ;
    tcg_gen_sub_i64(t0, t1, t0);
    tcg_gen_extrl_i64_i32(cpu_hi, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_hi_guard, t0);

    tcg_gen_sari_tl(t3, cpu_lo, 31);
    tcg_gen_sari_tl(t4, cpu_hi, 31);
    tcg_gen_movi_tl(cpu_v, 1);
    tcg_gen_brcond_tl(TCG_COND_NE, t3, cpu_lo_guard, l1);
    tcg_gen_brcond_tl(TCG_COND_NE, t4, cpu_hi_guard, l1);
    tcg_gen_movi_tl(cpu_v, 0);
    gen_set_label(l1);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free(t3);
    tcg_temp_free(t4);


}

static inline void vmulsw(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv t2 = tcg_temp_new();

    tcg_gen_movi_tl(cpu_v, 0);
    tcg_gen_ext16s_tl(t2, cpu_R[rx]);
    tcg_gen_ext_tl_i64(t0, t2);
    tcg_gen_ext_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 16);
    tcg_gen_extrl_i64_i32(cpu_lo, t0);
    tcg_gen_sari_tl(cpu_lo_guard, cpu_lo, 31);
    tcg_gen_sari_tl(t2, cpu_R[rx], 16);
    tcg_gen_ext_tl_i64(t0, t2);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 16);
    tcg_gen_extrl_i64_i32(cpu_hi, t0);
    tcg_gen_sari_tl(cpu_hi_guard, cpu_hi, 31);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free(t2);

}

static inline void vmulswa(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv t3 = tcg_temp_new();
    TCGv t4 = tcg_temp_local_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_ext16s_tl(t3, cpu_R[rx]);
    tcg_gen_ext_tl_i64(t0, t3);
    tcg_gen_ext_tl_i64(t1, cpu_R[ry]);
    tcg_gen_concat_i32_i64(t2, cpu_lo, cpu_lo_guard);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_sari_i64(t0, t0, 16);
    tcg_gen_add_i64(t0, t0, t2);
    tcg_gen_extrl_i64_i32(cpu_lo, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_lo_guard, t0);

    tcg_gen_sari_tl(t3, cpu_R[rx], 16);
    tcg_gen_ext_tl_i64(t0, t3);
    tcg_gen_concat_i32_i64(t2, cpu_hi, cpu_hi_guard);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_sari_i64(t0, t0, 16);
    tcg_gen_add_i64(t0, t0, t2);
    tcg_gen_extrl_i64_i32(cpu_hi, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_hi_guard, t0);

    tcg_gen_sari_tl(t3, cpu_lo, 31);
    tcg_gen_sari_tl(t4, cpu_hi, 31);
    tcg_gen_movi_tl(cpu_v, 1);
    tcg_gen_brcond_tl(TCG_COND_NE, t3, cpu_lo_guard, l1);
    tcg_gen_brcond_tl(TCG_COND_NE, t4, cpu_hi_guard, l1);
    tcg_gen_movi_tl(cpu_v, 0);
    gen_set_label(l1);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free(t3);
    tcg_temp_free(t4);

}

static inline void vmulsws(int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv t3 = tcg_temp_new();
    TCGv t4 = tcg_temp_local_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_ext16s_tl(t3, cpu_R[rx]);
    tcg_gen_ext_tl_i64(t0, t3);
    tcg_gen_ext_tl_i64(t1, cpu_R[ry]);
    tcg_gen_concat_i32_i64(t2, cpu_lo, cpu_lo_guard);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_sari_i64(t0, t0, 16);
    tcg_gen_sub_i64(t0, t2, t0);
    tcg_gen_extrl_i64_i32(cpu_lo, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_lo_guard, t0);

    tcg_gen_sari_tl(t3, cpu_R[rx], 16);
    tcg_gen_ext_tl_i64(t0, t3);
    tcg_gen_concat_i32_i64(t2, cpu_hi, cpu_hi_guard);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_sari_i64(t0, t0, 16);
    tcg_gen_sub_i64(t0, t2, t0);
    tcg_gen_extrl_i64_i32(cpu_hi, t0);
    tcg_gen_shri_i64(t0, t0, 32);
    tcg_gen_extrl_i64_i32(cpu_hi_guard, t0) ;

    tcg_gen_sari_tl(t3, cpu_lo, 31);
    tcg_gen_sari_tl(t4, cpu_hi, 31);
    tcg_gen_movi_tl(cpu_v, 1);
    tcg_gen_brcond_tl(TCG_COND_NE, t3, cpu_lo_guard, l1);
    tcg_gen_brcond_tl(TCG_COND_NE, t4, cpu_hi_guard, l1);
    tcg_gen_movi_tl(cpu_v, 0);
    gen_set_label(l1);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free(t3);
    tcg_temp_free(t4);

}

static inline void bt(DisasContext *ctx, int offset)
{
    int val;
    TCGLabel *l1 = gen_new_label();
    TCGv t0 = tcg_temp_new();

    val = offset << 1;
    if (val & 0x800) {
        val |= 0xfffff800;
    }
    val += ctx->pc + 2;

    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
    gen_goto_tb(ctx, 1, ctx->pc + 2);
    gen_set_label(l1);

    gen_goto_tb(ctx, 0, val);

    tcg_temp_free(t0);
}

static inline void bf(DisasContext *ctx, int offset)
{
    int val;
    TCGLabel *l1 = gen_new_label();
    TCGv t0 = tcg_temp_new();

    val = offset << 1;
    if (val & 0x800) {
        val |= 0xfffff800;
    }
    val += ctx->pc + 2;
    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 1, l1);
    gen_goto_tb(ctx, 1, ctx->pc + 2);
    gen_set_label(l1);

    gen_goto_tb(ctx, 0, val);

    tcg_temp_free(t0);
}

static inline void br(DisasContext *ctx, int offset)
{
    int val;
    val = offset << 1;
    if (val & 0x800) {
        val |= 0xfffff800;
    }
    val += ctx->pc + 2;

    gen_goto_tb(ctx, 0, val);

}

static inline void bsr(DisasContext *ctx, int offset)
{
    int val;
    val = offset << 1;
    if (val & 0x800) {
        val |= 0xfffff800;
    }
    val += ctx->pc + 2;
    tcg_gen_movi_tl(cpu_R[15], ctx->pc + 2);

    gen_goto_tb(ctx, 0, val);
}



#define  insn_1  ((ctx->insn & 0xf000) >> 12)
#define  insn_2  ((ctx->insn & 0x0f00) >> 8)
#define  insn_3  ((ctx->insn & 0x00f0) >> 4)
#define  insn_4   (ctx->insn & 0x000f)

static void disas_csky_v1_insn(CPUCSKYState *env, DisasContext *ctx)
{   unsigned int insn, rz, rx, ry;
    int imm, disp, i;
    TCGv t0 = 0;
    TCGLabel *l1;

    rx = 0;
    ry = 0;
    insn = ctx->insn;

    switch (insn_1) {
    case 0x0:
        switch (insn_2) {
        case 0x0:
            switch (insn_3) {
            case 0x0:
                switch (insn_4) {
                case 0x0:
                    if (is_gdbserver_start == TRUE) {
                        generate_exception(ctx, EXCP_DEBUG);
                        ctx->is_jmp = DISAS_JUMP;
                    } else {
                        generate_exception(ctx, EXCP_CSKY_BKPT);
                    }
#if !defined(CONFIG_USER_ONLY)
                    ctx->cannot_be_traced = 1;
#endif
                    break;/*bkpt*/
                case 0x1:
                    break;/*sync*/
                case 0x2:
#if defined(CONFIG_USER_ONLY)
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
                    if (IS_SUPER(ctx)) {
                        t0 = tcg_const_tl(0);
                        store_cpu_field(t0, idly4_counter);

                        gen_helper_rte(cpu_env);
                        ctx->is_jmp = DISAS_UPDATE;
#if !defined(CONFIG_USER_ONLY)
                        ctx->cannot_be_traced = 1;
#endif
                    } else {
                        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                    }
#endif
                    break;/*rte*/
                case 0x3:
#if defined(CONFIG_USER_ONLY)
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
                    if (IS_SUPER(ctx)) {
                        t0 = tcg_const_tl(0);
                        store_cpu_field(t0, idly4_counter);

                        gen_helper_rfi(cpu_env);
                        ctx->is_jmp = DISAS_UPDATE;
#if !defined(CONFIG_USER_ONLY)
                        ctx->cannot_be_traced = 1;
#endif
                    } else {
                        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                    }
#endif
                    break;/*rfi*/
                case 0x4:
#if defined(CONFIG_USER_ONLY)
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
                    if (IS_SUPER(ctx)) {
                        t0 = tcg_const_tl(0);
                        store_cpu_field(t0, idly4_counter);

                        gen_save_pc(ctx->pc + 2);
                        gen_helper_stop(cpu_env);
                        ctx->is_jmp = DISAS_UPDATE;
#if !defined(CONFIG_USER_ONLY)
                        ctx->cannot_be_traced = 1;
#endif
                    } else {
                        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                    }
#endif
                    break;/*stop*/
                case 0x5:
#if defined(CONFIG_USER_ONLY)
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
                    if (IS_SUPER(ctx)) {
                        t0 = tcg_const_tl(0);
                        store_cpu_field(t0, idly4_counter);

                        gen_save_pc(ctx->pc + 2);
                        gen_helper_wait(cpu_env);
                        ctx->is_jmp = DISAS_UPDATE;
#if !defined(CONFIG_USER_ONLY)
                        ctx->cannot_be_traced = 1;
#endif
                    } else {
                        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                    }
#endif
                    break;/*wait*/
                case 0x6:
#if defined(CONFIG_USER_ONLY)
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
                    if (IS_SUPER(ctx)) {
                        t0 = tcg_const_tl(0);
                        store_cpu_field(t0, idly4_counter);

                        gen_save_pc(ctx->pc + 2);
                        gen_helper_doze(cpu_env);
                        ctx->is_jmp = DISAS_UPDATE;
#if !defined(CONFIG_USER_ONLY)
                        ctx->cannot_be_traced = 1;
#endif
                    } else {
                        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                    }
#endif
                    break;/*doze*/
                case 0x7:
#if !defined(CONFIG_USER_ONLY)
                    if (ctx->trace_mode == NORMAL_MODE) {
                        l1 = gen_new_label();
                        t0 =  load_cpu_field(idly4_counter);
                        tcg_gen_brcondi_tl(TCG_COND_NE, t0, 0, l1);

                        t0 = tcg_const_tl(4);
                        store_cpu_field(t0, idly4_counter);
                        tcg_gen_movi_tl(cpu_c, 0);

                        gen_save_pc(ctx->pc + 2);
                        ctx->is_jmp = DISAS_UPDATE;
                        gen_set_label(l1);
                    }
#endif
                    break;/*idly4*/
                case 0x8:
                    generate_exception(ctx, EXCP_CSKY_TRAP0);
#if !defined(CONFIG_USER_ONLY)
                    ctx->cannot_be_traced = 1;
#endif
                    break;/*trap0*/
                case 0x9:
#if !defined(CONFIG_USER_ONLY)
                    generate_exception(ctx, EXCP_CSKY_TRAP1);
                    ctx->cannot_be_traced = 1;
#endif
                    break;/*trap1*/
                case 0xa:
                    generate_exception(ctx, EXCP_CSKY_TRAP2);
#if !defined(CONFIG_USER_ONLY)
                    ctx->cannot_be_traced = 1;
#endif
                    break;/*trap2*/
                case 0xb:
                    generate_exception(ctx, EXCP_CSKY_TRAP3);
#if !defined(CONFIG_USER_ONLY)
                    ctx->cannot_be_traced = 1;
#endif
                    break;/*trap3*/
                case 0xc:
                    check_insn(ctx, ABIV1_DSP);
                    tcg_gen_mov_tl(cpu_c, cpu_v);
                    break;/*mvtc*/
                case 0xd:
                    gen_helper_cprc(cpu_env);
                    gen_save_pc(ctx->pc + 2);
                    ctx->is_jmp = DISAS_UPDATE;
                    break;/*cprc-todo*/

                default:
                    goto illegal_op;
                }
                break;
            case 0x1:
#if defined(CONFIG_USER_ONLY)
                /*
                 * A temporary solution to support
                 * float instructions in cskyv1
                 */
                rx = (insn & 0xf) << 24;
                t0 = load_cpu_field(cp0.psr);
                tcg_gen_andi_tl(t0, t0, ~0x0f000000);
                tcg_gen_ori_tl(t0, t0, rx);
                store_cpu_field(t0, cp0.psr);
                if (rx == (0x1 << 24)) {
                    tcg_gen_movi_tl(t0, 0x0);
                    store_cpu_field(t0, cp1.fcr);
                    store_cpu_field(t0, cp1.fsr);
                }
                gen_save_pc(ctx->pc + 2);
                ctx->is_jmp = DISAS_UPDATE;

                tcg_temp_free(t0);
#else
                if (IS_SUPER(ctx)) {
                    rx = (insn & 0xf) << 24;
                    t0 = load_cpu_field(cp0.psr);
                    tcg_gen_andi_tl(t0, t0, ~0x0f000000);
                    tcg_gen_ori_tl(t0, t0, rx);
                    store_cpu_field(t0, cp0.psr);
                    if (rx == (0x1 << 24)) {
                        tcg_gen_movi_tl(t0, 0x0);
                        store_cpu_field(t0, cp1.fcr);
                        store_cpu_field(t0, cp1.fsr);
                    }
                    gen_save_pc(ctx->pc + 2);
                    ctx->is_jmp = DISAS_UPDATE;

                    tcg_temp_free(t0);
                } else {
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                }
#endif
                break;/*cpseti-todo*/
            case 0x2:
                rx = insn & 0x000f;
                tcg_gen_mov_tl(cpu_R[rx], cpu_c);
                break;/*mvc*/
            case 0x3:
                rx = insn & 0x000f;
                tcg_gen_subfi_tl(cpu_R[rx], 1, cpu_c);
                break;/*mvcv*/
            case 0x4:
                rx = insn & 0x000f;
                t0 = tcg_temp_new();
                tcg_gen_mov_tl(t0, cpu_R[rx]);
                for (i = 4; i <= 7; i++) {
                    tcg_gen_qemu_ld32u(cpu_R[i], t0, ctx->mem_idx);
                    tcg_gen_addi_tl(t0, t0, 4);
                }
                tcg_temp_free(t0);
                break;/*ldq*/
            case 0x5:
                rx = insn & 0x000f;
                t0 = tcg_temp_new();
                tcg_gen_mov_tl(t0, cpu_R[rx]);
                for (i = 4; i <= 7; i++) {
                    tcg_gen_qemu_st32(cpu_R[i], t0, ctx->mem_idx);
                    tcg_gen_addi_tl(t0, t0, 4);
                }
                tcg_temp_free(t0);
                break;/*stq*/
            case 0x6:
                rx = insn & 0x000f;
                t0 = tcg_temp_new();

                tcg_gen_mov_tl(t0, cpu_R[0]);
                for (i = rx; i <= 15; i++) {
                    tcg_gen_qemu_ld32u(cpu_R[i], t0, ctx->mem_idx);
                    tcg_gen_addi_tl(t0, t0, 4);
                }
                tcg_temp_free(t0);
                break;/*ldm*/
            case 0x7:
                rx = insn & 0x000f;
                t0 = tcg_temp_new();

                tcg_gen_mov_tl(t0, cpu_R[0]);
                for (i = rx; i <= 15; i++) {
                    tcg_gen_qemu_st32(cpu_R[i], t0, ctx->mem_idx);
                    tcg_gen_addi_tl(t0, t0, 4);
                }
                tcg_temp_free(t0);
                break;/*stm*/
            case 0x8:
                rx = insn & 0x000f;
                tcg_gen_sub_tl(cpu_R[rx], cpu_R[rx], cpu_c);
                break;/*dect*/
            case 0x9:
                rx = insn & 0x000f;
                tcg_gen_subi_tl(cpu_R[rx], cpu_R[rx], 1);
                tcg_gen_add_tl(cpu_R[rx], cpu_R[rx], cpu_c);
                break;/*decf*/
            case 0xa:
                rx = insn & 0x000f;
                tcg_gen_add_tl(cpu_R[rx], cpu_R[rx], cpu_c);
                break;/*inct*/
            case 0xb:
                rx = insn & 0x000f;
                tcg_gen_addi_tl(cpu_R[rx], cpu_R[rx], 1);
                tcg_gen_sub_tl(cpu_R[rx], cpu_R[rx], cpu_c);
                break;/*incf*/
            case 0xc:
                rx = insn & 0x000f;
                t0 = tcg_temp_new();
                tcg_gen_andi_tl(t0, cpu_R[rx], 0xfffffffe);
                store_cpu_field(t0, pc);

#if !defined(CONFIG_USER_ONLY)
                if ((ctx->trace_mode == BRAN_TRACE_MODE)
                        || (ctx->trace_mode == INST_TRACE_MODE)) {
                    t0 = tcg_const_i32(EXCP_CSKY_TRACE);
                    gen_helper_exception(cpu_env, t0);
                }
                ctx->maybe_change_flow = 1;
#endif
                tcg_temp_free(t0);
                ctx->is_jmp = DISAS_JUMP;
                break;/*jmp*/
            case 0xd:
                rx = insn & 0x000f;
                t0 = tcg_temp_new();
                tcg_gen_andi_tl(t0, cpu_R[rx], 0xfffffffe);
                tcg_gen_movi_tl(cpu_R[15], ctx->pc + 2);
                store_cpu_field(t0, pc);

#if !defined(CONFIG_USER_ONLY)
                if ((ctx->trace_mode == BRAN_TRACE_MODE)
                        || (ctx->trace_mode == INST_TRACE_MODE)) {
                    t0 = tcg_const_i32(EXCP_CSKY_TRACE);
                    gen_helper_exception(cpu_env, t0);
                }
                ctx->maybe_change_flow = 1;
#endif
                tcg_temp_free(t0);
                ctx->is_jmp = DISAS_JUMP;
                break;/*jsr*/
            case 0xe:
                rx = insn & 0x000f;
                gen_helper_ff1(cpu_R[rx], cpu_R[rx]);
                break;/*ff1*/
            case 0xf:
                rx = insn & 0x000f;
                gen_helper_brev(cpu_R[rx], cpu_R[rx]);
                break;/*brev*/
            default:
                goto illegal_op;
            }
            break;
        case 0x1:
            switch (insn_3) {
            case 0x0:
                rx = insn & 0x000f;
                tcg_gen_andi_tl(cpu_R[1], cpu_R[rx], 0x000000ff);
                tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[1], 0);
                break;/*xtrb3*/
            case 0x1:
                rx = insn & 0x000f;
                tcg_gen_andi_tl(cpu_R[1], cpu_R[rx], 0x0000ff00);
                tcg_gen_shri_tl(cpu_R[1], cpu_R[1], 8);
                tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[1], 0);
                break;/*xtrb2*/
            case 0x2:
                rx = insn & 0x000f;
                tcg_gen_andi_tl(cpu_R[1], cpu_R[rx], 0x00ff0000);
                tcg_gen_shri_tl(cpu_R[1], cpu_R[1], 16);
                tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[1], 0);
                break;/*xtrb1*/
            case 0x3:
                rx = insn & 0x000f;
                tcg_gen_shri_tl(cpu_R[1], cpu_R[rx], 24);
                tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[1], 0);
                break;/*xtrb0*/
            case 0x4:
                rx = insn & 0x000f;
                tcg_gen_ext8u_tl(cpu_R[rx], cpu_R[rx]);
                break;/*zextb*/
            case 0x5:
                rx = insn & 0x000f;
                tcg_gen_ext8s_tl(cpu_R[rx], cpu_R[rx]);
                break;/*sextb*/
            case 0x6:
                rx = insn & 0x000f;
                tcg_gen_ext16u_tl(cpu_R[rx], cpu_R[rx]);
                break;/*zexth*/
            case 0x7:
                rx = insn & 0x000f;
                tcg_gen_ext16s_tl(cpu_R[rx], cpu_R[rx]);
                break;/*sexth*/
            case 0x8:
                rx = insn & 0x000f;
                tcg_gen_subi_tl(cpu_R[rx], cpu_R[rx], 1);
                tcg_gen_setcondi_tl(TCG_COND_LT, cpu_c, cpu_R[rx], 0);
                break;/*declt*/
            case 0x9:
                rx = insn & 0x000f;
                tstnbz(rx);
                break;/*tstnbz*/
            case 0xa:
                rx = insn & 0x000f;
                tcg_gen_subi_tl(cpu_R[rx], cpu_R[rx], 1);
                tcg_gen_setcondi_tl(TCG_COND_GT, cpu_c, cpu_R[rx], 0);
                break;/*decgt*/
            case 0xb:
                rx = insn & 0x000f;
                tcg_gen_subi_tl(cpu_R[rx], cpu_R[rx], 1);
                tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rx], 0);
                break;/*decne*/
            case 0xc:
                rx = insn & 0x000f;
                l1 = gen_new_label();
                tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_c, 0, l1);
                tcg_gen_movi_tl(cpu_R[rx], 0);
                gen_set_label(l1);
                break;/*clrt*/
            case 0xd:
                rx = insn & 0x000f;
                l1 = gen_new_label();
                tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
                tcg_gen_movi_tl(cpu_R[rx], 0);
                gen_set_label(l1);
                break;/*clrf*/
            case 0xe:
                rx = insn & 0x000f;
                l1 = gen_new_label();
                tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_R[rx], 0x80000000, l1);
                tcg_gen_brcondi_tl(TCG_COND_GE, cpu_R[rx], 0, l1);
                tcg_gen_neg_tl(cpu_R[rx], cpu_R[rx]);
                gen_set_label(l1);
                break;/*abs*/
            case 0xf:
                rx = insn & 0x000f;
                tcg_gen_not_tl(cpu_R[rx], cpu_R[rx]);
                break;/*not*/
            default:
                goto illegal_op;
            }
            break;
        case 0x2:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_c, 0, l1);
            tcg_gen_mov_tl(cpu_R[rx], cpu_R[ry]);
            gen_set_label(l1);
            break;/*movt*/
        case 0x3:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_mul_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
            break;/*mult*/
        case 0x4:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            mac(rx, ry);
            break;/*mac*/
        case 0x5:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_sub_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
            break;/*subu*/
        case 0x6:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            addc(rx, ry);
            break;/*addc*/
        case 0x7:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            subc(rx, ry);
            break;/*subc*/
        case 0x8:
        case 0x9:
             rx = (insn & 0x1f0) >> 4;
             rz = insn & 0xf;
             t0 = load_cpu_field(cp1.fr[rx]);
             tcg_gen_mov_tl(cpu_R[rz], t0);
             tcg_temp_free(t0);
            break;/*cprgr-todo*/
        case 0xa:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
            tcg_gen_mov_tl(cpu_R[rx], cpu_R[ry]);
            gen_set_label(l1);
            break;/*movf*/
        case 0xb:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            lsr(rx, ry);
            break;/*lsr*/
        case 0xc:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_setcond_tl(TCG_COND_GEU, cpu_c, cpu_R[rx], cpu_R[ry]);
            break;/*cmphs*/
        case 0xd:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_setcond_tl(TCG_COND_LT, cpu_c, cpu_R[rx], cpu_R[ry]);
            break;/*cmplt*/
        case 0xe:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            t0 = tcg_temp_new();
            tcg_gen_and_tl(t0, cpu_R[rx], cpu_R[ry]);
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, t0, 0);
            tcg_temp_free(t0);
            break;/*tst*/
        case 0xf:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_setcond_tl(TCG_COND_NE, cpu_c, cpu_R[rx], cpu_R[ry]);
            break;/*cmpne*/
        default:
            goto illegal_op;
        }
        break;

    case 0x1:
        switch (insn_2) {
        case 0x0:
#if defined(CONFIG_USER_ONLY)
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
            if (IS_SUPER(ctx)) {
                rz = insn & 0xf;
                rx = (insn & 0x1f0) >> 4;
                gen_mfcr(ctx, rz, rx);
            } else {
                generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
            }
#endif
            break;/*mfcr*/
        case 0x1:
            if (insn_3 != 0xf) {
#if defined(CONFIG_USER_ONLY)
                generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
                if (IS_SUPER(ctx)) {
                    rz = insn & 0xf;
                    rx = (insn & 0x1f0) >> 4;
                    gen_mfcr(ctx, rz, rx);
                } else {
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                }
#endif
                break;/*mfcr*/
            } else if (insn_3 == 0xf) {
                if (insn_4 >> 3 == 0) {
#if defined(CONFIG_USER_ONLY)
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
                    if (IS_SUPER(ctx)) {
                        t0 = tcg_const_tl(insn & 0x7);
                        gen_helper_psrclr(cpu_env, t0);

                        gen_save_pc(ctx->pc + 2);
                        ctx->is_jmp = DISAS_UPDATE;
                    } else {
                        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                    }
#endif
                    break;/*psrclr*/
                } else if (insn_4 >> 3 == 1) {
#if defined(CONFIG_USER_ONLY)
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
                    if (IS_SUPER(ctx)) {
                        t0 = tcg_const_tl(insn & 0x7);
                        gen_helper_psrset(cpu_env, t0);

                        gen_save_pc(ctx->pc + 2);
                        ctx->is_jmp = DISAS_UPDATE;
                    } else {
                        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                    }
#endif
                    break;/*psrset*/
                } else {
                    goto illegal_op;
                }
            } else {
                goto illegal_op;
            }
        case 0x2:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_mov_tl(cpu_R[rx], cpu_R[ry]);
            break;/*mov*/
        case 0x3:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            bgenr(rx, ry);
            break;/*bgenr*/
        case 0x4:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_sub_tl(cpu_R[rx], cpu_R[ry], cpu_R[rx]);
            break;/*rsub*/
        case 0x5:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            t0 = tcg_temp_new();
            tcg_gen_shli_tl(t0, cpu_R[ry], 2);
            tcg_gen_add_tl(cpu_R[rx], cpu_R[rx], t0);
            tcg_temp_free(t0);
            break;/*ixw*/
        case 0x6:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_and_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
            break;/*and*/
        case 0x7:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_xor_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
            break;/*xor*/
        case 0x8:
        case 0x9:
#if defined(CONFIG_USER_ONLY)
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#else
            if (IS_SUPER(ctx)) {
                rx = insn & 0xf;
                rz = (insn & 0x1f0) >> 4;
                gen_mtcr(ctx, rz, rx);
            } else {
                generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
            }
#endif
            break;/*mtcr*/
        case 0xa:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            asr(rx, ry);
            break;/*asr*/
        case 0xb:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            lsl(rx, ry);
            break;/*lsl*/
        case 0xc:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_add_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
            break;/*addu*/
        case 0xd:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            {
            t0 = tcg_temp_new();
            tcg_gen_shli_tl(t0, cpu_R[ry], 1);
            tcg_gen_add_tl(cpu_R[rx], cpu_R[rx], t0);
            tcg_temp_free(t0);
            }
            break;/*ixh*/
        case 0xe:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_or_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
            break;/*or*/
        case 0xf:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            tcg_gen_andc_tl(cpu_R[rx], cpu_R[rx], cpu_R[ry]);
            break;/*andn*/
        default:
            goto illegal_op;
        }
        break;
    case 0x2:
        switch (insn_2) {
        case 0x0:
        case 0x1:
            rx = insn & 0x000f;
            imm = ((insn & 0x01f0) >> 4) + 1;
            tcg_gen_addi_tl(cpu_R[rx], cpu_R[rx], imm);
            break;/*addi*/
        case 0x2:
        case 0x3:
            rx = insn & 0x000f;
            imm = ((insn & 0x01f0) >> 4) + 1;
            tcg_gen_setcondi_tl(TCG_COND_LT, cpu_c, cpu_R[rx], imm);
            break;/*cmplti*/
        case 0x4:
        case 0x5:
            rx = insn & 0x000f;
            imm = ((insn & 0x01f0) >> 4) + 1;
            tcg_gen_subi_tl(cpu_R[rx], cpu_R[rx], imm);
            break;/*subi*/
        case 0x6:
        case 0x7:
            rx = (insn & 0x1f0) >> 4;
            rz = insn & 0xf;
            store_cpu_field(cpu_R[rz], cp1.fr[rx]);
            break;/*cpwgr-todo*/
        case 0x8:
        case 0x9:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_subfi_tl(cpu_R[rx], imm, cpu_R[rx]);
            break;/*rsubi*/
        case 0xa:
        case 0xb:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rx], imm);
            break;/*cmpnei*/
        case 0xc:
            switch (insn_3) {
            case 0x0:
                rx = insn & 0x000f;
                imm = (insn & 0x01f0) >> 4;

                if (imm == 0) {
                    tcg_gen_movi_tl(cpu_R[rx], 0xffffffff);
                } else {
                    tcg_gen_movi_tl(cpu_R[rx], (1 << imm) - 1);
                }

                break;/*bmaski#32*/
            case 0x1:
                rx = insn & 0x000f;
                divu(ctx, rx);
                break;/*divu*/
            case 0x2:
                check_insn(ctx, ABIV1_DSP);
                rx = insn & 0x000f;
                l1 = gen_new_label();
                tcg_gen_mov_tl(cpu_R[rx], cpu_lo_guard);
                tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_v, 1, l1);
                tcg_gen_mov_tl(cpu_R[rx], cpu_lo);
                gen_set_label(l1);
                break;/*mflos*/
            case 0x3:
                check_insn(ctx, ABIV1_DSP);
                rx = insn & 0x000f;
                l1 = gen_new_label();
                tcg_gen_mov_tl(cpu_R[rx], cpu_hi_guard);
                tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_v, 1, l1);
                tcg_gen_mov_tl(cpu_R[rx], cpu_hi);
                gen_set_label(l1);
                break;/*mfhis*/
            case 0x4:
                {
                    check_insn(ctx, ABIV1_DSP);
                    rx = insn & 0x000f;
                    TCGv_i64 t0 = tcg_temp_new_i64();
                    tcg_gen_movi_tl(cpu_v, 0);
                    tcg_gen_mov_tl(cpu_lo, cpu_R[rx]);
                    tcg_gen_ext_tl_i64(t0, cpu_lo);
                    tcg_gen_shri_i64(t0, t0, 32);
                    tcg_gen_extrl_i64_i32(cpu_lo_guard, t0);
                    tcg_temp_free_i64(t0);
                }
                break;/*mtlo*/
            case 0x5:
                {
                    check_insn(ctx, ABIV1_DSP);
                    rx = insn & 0x000f;
                    TCGv_i64 t0 = tcg_temp_new_i64();
                    tcg_gen_movi_tl(cpu_v, 0);
                    tcg_gen_mov_tl(cpu_hi, cpu_R[rx]);
                    tcg_gen_ext_tl_i64(t0, cpu_hi);
                    tcg_gen_shri_i64(t0, t0, 32);
                    tcg_gen_extrl_i64_i32(cpu_hi_guard, t0);
                    tcg_temp_free_i64(t0);
                }
                break;/*mthi*/
            case 0x6:
                check_insn(ctx, ABIV1_DSP);
                rx = insn & 0x000f;
                tcg_gen_mov_tl(cpu_R[rx], cpu_lo);
                break;/*mflo*/
            case 0x7:
                check_insn(ctx, ABIV1_DSP);
                rx = insn & 0x000f;
                tcg_gen_mov_tl(cpu_R[rx], cpu_hi);
                break;/*mfhi*/
            case 0x8:
            case 0x9:
            case 0xa:
            case 0xb:
            case 0xc:
            case 0xd:
            case 0xe:
            case 0xf:
                rx = insn & 0x000f;
                imm = (insn & 0x01f0) >> 4;

                if (imm == 0) {
                    tcg_gen_movi_tl(cpu_R[rx], 0xffffffff);
                } else {
                    tcg_gen_movi_tl(cpu_R[rx], (1 << imm) - 1);
                }

                break;/*bmaski*/
            default:
                goto illegal_op;
            }
            break;
        case 0xd:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;

            if (imm == 0) {
                tcg_gen_movi_tl(cpu_R[rx], 0xffffffff);
            } else {
                tcg_gen_movi_tl(cpu_R[rx], (1 << imm) - 1);
            }
            break;/*bmaski*/
        case 0xe:
        case 0xf:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_andi_tl(cpu_R[rx], cpu_R[rx], imm);
            break;/*andi*/
        default:
            goto illegal_op;
        }
        break;
    case 0x3:
        switch (insn_2) {
        case 0x0:
        case 0x1:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_andi_tl(cpu_R[rx], cpu_R[rx], ~(1 << imm));
            break;/*bclri*/
        case 0x2:
            switch (insn_3) {
            case 0x0:
                rx = insn & 0xf;
                store_cpu_field(cpu_R[rx], cp1.fir);
                gen_helper_cpwir(cpu_env);
                gen_save_pc(ctx->pc + 2);
                ctx->is_jmp = DISAS_UPDATE;
                break;/*cpwir-todo*/
            case 0x1:
                rx = insn & 0x000f;
                divs(ctx, rx);
                break;/*divs*/
            case 0x2:
#if defined(CONFIG_USER_ONLY)
                rz = insn & 0xf;
                t0 = load_cpu_field(cp1.fsr);
                tcg_gen_mov_tl(cpu_R[rz], t0);
                tcg_temp_free(t0);
#else
                if (IS_SUPER(ctx)) {
                    rz = insn & 0xf;
                    t0 = load_cpu_field(cp1.fsr);
                    tcg_gen_mov_tl(cpu_R[rz], t0);
                    tcg_temp_free(t0);
                } else {
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                }
#endif
                break;/*cprsr-todo*/
            case 0x3:
#if defined(CONFIG_USER_ONLY)
                rz = insn & 0xf;
                store_cpu_field(cpu_R[rz], cp1.fsr);
#else
                if (IS_SUPER(ctx)) {
                    rz = insn & 0xf;
                    store_cpu_field(cpu_R[rz], cp1.fsr);
                } else {
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                }
#endif
                break;/*cpwsr-todo*/

            case 0x7:
            case 0x8:
            case 0x9:
            case 0xa:
            case 0xb:
            case 0xc:
            case 0xd:
            case 0xe:
            case 0xf:
                rx = insn & 0x000f;
                imm = (insn & 0x01f0) >> 4;
                tcg_gen_movi_tl(cpu_R[rx], 1 << imm);
                break;/* bgeni */
            default:
                goto illegal_op;
            }
            break;

        case 0x3:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_movi_tl(cpu_R[rx], 1 << imm);
            break;/*bgeni*/
        case 0x4:
        case 0x5:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_ori_tl(cpu_R[rx], cpu_R[rx], 1 << imm);
            break;/*bseti*/
        case 0x6:
        case 0x7:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            t0 = tcg_temp_new();
            tcg_gen_andi_tl(t0, cpu_R[rx], 1 << imm);
            tcg_gen_shri_tl(cpu_c, t0, imm);
            tcg_temp_free(t0);
            break;/*btsti*/
        case 0x8:
            if (insn_3 == 0) {
                rx = insn & 0x000f;
                xsr(rx);
                break;/*xsr*/
            } else {
                rx = insn & 0x000f;
                imm = (insn & 0x01f0) >> 4;
                tcg_gen_rotli_tl(cpu_R[rx], cpu_R[rx], imm);
                break;/*rotli*/
            }
        case 0x9:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_rotli_tl(cpu_R[rx], cpu_R[rx], imm);
            break;/*rotli*/
        case 0xa:
            if (insn_3 == 0) {
                rx = insn & 0x000f;
                tcg_gen_andi_tl(cpu_c, cpu_R[rx], 0x1);
                tcg_gen_sari_tl(cpu_R[rx], cpu_R[rx], 1);
                break;/*asrc*/
            } else {
                rx = insn & 0x000f;
                imm = (insn & 0x01f0) >> 4;
                tcg_gen_sari_tl(cpu_R[rx], cpu_R[rx], imm);
                break;/*asri*/
            }
        case 0xb:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_sari_tl(cpu_R[rx], cpu_R[rx], imm);
            break;/*asri*/
        case 0xc:
            if (insn_3 == 0) {
                rx = insn & 0x000f;
                tcg_gen_shri_tl(cpu_c, cpu_R[rx], 31);
                tcg_gen_shli_tl(cpu_R[rx], cpu_R[rx], 1);
                break;/*lslc*/
            } else {
                rx = insn & 0x000f;
                imm = (insn & 0x01f0) >> 4;
                tcg_gen_shli_tl(cpu_R[rx], cpu_R[rx], imm);
                break;/*lsli*/
            }
        case 0xd:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_shli_tl(cpu_R[rx], cpu_R[rx], imm);
            break;/*lsli*/
        case 0xe:
            if (insn_3 == 0) {
                rx = insn & 0x000f;
                tcg_gen_andi_tl(cpu_c, cpu_R[rx], 0x1);
                tcg_gen_shri_tl(cpu_R[rx], cpu_R[rx], 1);
                break;/*lsrc*/
            } else {
                rx = insn & 0x000f;
                imm = (insn & 0x01f0) >> 4;
                tcg_gen_shri_tl(cpu_R[rx], cpu_R[rx], imm);
                break;/*lsri*/
            }
        case 0xf:
            rx = insn & 0x000f;
            imm = (insn & 0x01f0) >> 4;
            tcg_gen_shri_tl(cpu_R[rx], cpu_R[rx], imm);
            break;/*lsri*/
        default:
            goto illegal_op;
        }
        break;
    case 0x4:
        switch (insn_2) {
        case 0x0:
            goto illegal_op;
            break;/*omflip0*/
        case 0x1:
            goto illegal_op;
            break;/* omflip1*/
        case 0x2:
            goto illegal_op;
            break;/* omflip2*/
        case 0x3:
            goto illegal_op;
            break;/* omflip3*/

        default:
            goto illegal_op;
        }
        break;
    case 0x5:
        switch (insn_2) {
        case 0x0:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            muls(rx, ry);
            break;/*muls*/
        case 0x1:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            mulsa(rx, ry);
            break;/*mulsa*/
        case 0x2:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            mulss(rx, ry);
            break;/*mulss*/
        case 0x4:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            mulu(rx, ry);
            break;/*mulu*/
        case 0x5:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            mulua(rx, ry);
            break;/*mulua*/
        case 0x6:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;
            mulus(rx, ry);
            break;/*mulus*/
        case 0x8:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            vmulsh(rx, ry);

            break;/*vmulsh*/
        case 0x9:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            vmulsha(rx, ry);

            break;/* vmulsha */
        case 0xa:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            vmulshs(rx, ry);

            break;/* vmulshs */
        case 0xc:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            vmulsw(rx, ry);

            break;/* vmulsw*/
        case 0xd:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            vmulswa(rx, ry);

            break;/* vmulswa */
        case 0xe:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            vmulsws(rx, ry);

            break;/* vmulsws */
        default:
            goto illegal_op;
        }
        break;
    case 0x6:
        switch (insn_2) {
        case 0x0:
        case 0x1:
        case 0x2:
        case 0x3:
        case 0x4:
        case 0x5:
        case 0x6:
        case 0x7:
            rx = insn & 0x000f;
            imm = (insn & 0x07f0) >> 4;
            tcg_gen_movi_tl(cpu_R[rx], imm);
            break; /*movi*/
        case 0x8:
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            t0 = tcg_temp_new();
            tcg_gen_ext16s_tl(cpu_R[rx], cpu_R[rx]);
            tcg_gen_ext16s_tl(t0, cpu_R[ry]);
            tcg_gen_mul_tl(cpu_R[rx], cpu_R[rx], t0);
            tcg_temp_free(t0);

            break;/*mulsh*/
        case 0x9:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            mulsha(rx, ry);

            break;/*mulsha*/
        case 0xa:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            mulshs(rx, ry);

            break;/*mulshs*/
        case 0xb:
#if defined(CONFIG_USER_ONLY)
            /* ck610 has only one coprocessor: fpu, needn't be tested */
            rz = insn & 0x7;
            rx = (insn & 0xf8) >> 3;
            switch (rx) {
            case 0:
                t0 = load_cpu_field(cp1.fpcid);
                break;
            case 1:
                t0 = load_cpu_field(cp1.fcr);
                break;
            case 2:
                t0 = load_cpu_field(cp1.fsr);
                break;
            case 3:
                t0 = load_cpu_field(cp1.fir);
                break;
            case 4:
                t0 = load_cpu_field(cp1.fesr);
                break;
            case 5:
                t0 = load_cpu_field(cp1.feinst1);
                break;
            case 6:
                t0 = load_cpu_field(cp1.feinst2);
                break;
            default:
                tcg_gen_movi_tl(t0, 0);
                break;
            }
            tcg_gen_mov_tl(cpu_R[rz], t0);
            tcg_temp_free(t0);
#else
            rz = insn & 0x7;
            rx = (insn & 0xf8) >> 3;

            if (!IS_SUPER(ctx)) {
                generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
            }

            if (ctx->current_cp == 15) {
                check_insn(ctx, CSKY_MMU);
                gen_cprcr_cp15(ctx, rz, rx);
            } else if (ctx->current_cp == 1) {
                switch (rx) {
                case 0:
                    t0 = load_cpu_field(cp1.fpcid);
                    break;
                case 1:
                    t0 = load_cpu_field(cp1.fcr);
                    break;
                case 2:
                    t0 = load_cpu_field(cp1.fsr);
                    break;
                case 3:
                    t0 = load_cpu_field(cp1.fir);
                    break;
                case 4:
                    t0 = load_cpu_field(cp1.fesr);
                    break;
                case 5:
                    t0 = load_cpu_field(cp1.feinst1);
                    break;
                case 6:
                    t0 = load_cpu_field(cp1.feinst2);
                    break;
                default:
                    tcg_gen_movi_tl(t0, 0);
                    break;
                }
                tcg_gen_mov_tl(cpu_R[rz], t0);
                tcg_temp_free(t0);
            }
#endif
            break;/*cprcr-todo*/

        case 0xc:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            mulsw(rx, ry);

            break;/*mulsw*/
        case 0xd:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            mulswa(rx, ry);

            break;/*mulswa*/
        case 0xe:
            check_insn(ctx, ABIV1_DSP);
            rx = insn & 0x000f;
            ry = (insn & 0x00f0) >> 4;

            mulsws(rx, ry);

            break;/*mulsws*/
        case 0xf:
#if defined(CONFIG_USER_ONLY)
            /* ck610 has only one coprocessor: fpu, so needn't be test */
            rx = insn & 0x7;
            rz = (insn & 0xf8) >> 3;
            store_cpu_field(cpu_R[rz], cp1.fcr);
#else
            rx = insn & 0x7;
            rz = (insn & 0xf8) >> 3;

            if (!IS_SUPER(ctx)) {
                generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
            }

            if (ctx->current_cp == 15) {
                check_insn(ctx, CSKY_MMU);
                gen_cpwcr_cp15(ctx, rz, rx);
            } else if (ctx->current_cp == 1) {
                store_cpu_field(cpu_R[rz], cp1.fcr);
            }
#endif
            break;/*cpwcr-todo*/
        default:
            goto illegal_op;
        }
        break;

    case 0x7:
        if ((insn_2 > 0) && (insn_2 < 15)) {
            target_ulong addr;
            t0 = tcg_temp_new();

            disp = insn & 0x00ff;
            rz = (insn & 0x0f00) >> 8;
            addr = (ctx->pc + 2 + (disp << 2)) & 0xfffffffc ;
            tcg_gen_movi_tl(t0, addr);
            tcg_gen_qemu_ld32u(cpu_R[rz], t0, ctx->mem_idx);
            tcg_temp_free(t0);

            break;/*lrw*/
        } else if (insn_2 == 0) {
            target_ulong addr;

            disp = insn & 0x00ff;
            addr = (ctx->pc + 2 + (disp << 2)) & 0xfffffffc ;
#if defined(CONFIG_USER_ONLY)
            addr = cpu_ldl_code(env, addr);
            gen_goto_tb(ctx, 0, addr);
            ctx->is_jmp = DISAS_TB_JUMP;
#else
            t0 = tcg_temp_new();
            tcg_gen_movi_tl(t0, addr);
            tcg_gen_qemu_ld32u(t0, t0, ctx->mem_idx);
            tcg_gen_andi_tl(t0, t0, 0xfffffffe);
            store_cpu_field(t0, pc);

            if ((ctx->trace_mode == BRAN_TRACE_MODE)
                    || (ctx->trace_mode == INST_TRACE_MODE)) {
                t0 = tcg_const_i32(EXCP_CSKY_TRACE);
                gen_helper_exception(cpu_env, t0);
            }
            ctx->maybe_change_flow = 1;
            tcg_temp_free(t0);
            ctx->is_jmp = DISAS_JUMP;
#endif
            break;/*jmpi*/
        } else if (insn_2 == 15) {
            target_ulong addr;

            disp = insn & 0x00ff;
            addr = (ctx->pc + 2 + (disp << 2)) & 0xfffffffc;
            tcg_gen_movi_tl(cpu_R[15], ctx->pc + 2);
#if defined(CONFIG_USER_ONLY)
            addr = cpu_ldl_code(env, addr);
            gen_goto_tb(ctx, 0, addr);
            ctx->is_jmp = DISAS_TB_JUMP;
#else
            t0 = tcg_temp_new();
            tcg_gen_movi_tl(t0, addr);
            tcg_gen_qemu_ld32u(t0, t0, ctx->mem_idx);
            tcg_gen_andi_tl(t0, t0, 0xfffffffe);
            store_cpu_field(t0, pc);

            if ((ctx->trace_mode == BRAN_TRACE_MODE)
                    || (ctx->trace_mode == INST_TRACE_MODE)) {
                t0 = tcg_const_i32(EXCP_CSKY_TRACE);
                gen_helper_exception(cpu_env, t0);
            }
            ctx->maybe_change_flow = 1;
            tcg_temp_free(t0);
            ctx->is_jmp = DISAS_JUMP;
#endif
            break;/*jsri*/
        } else {
            goto illegal_op;
        }
    case 0x8:
        rx = insn & 0x000f;
        imm = ((insn & 0x00f0) >> 4) << 2;
        rz = (insn & 0x0f00) >> 8;

        t0 = tcg_temp_new();
        tcg_gen_addi_tl(t0, cpu_R[rx], imm);
        tcg_gen_qemu_ld32u(cpu_R[rz], t0, ctx->mem_idx);
        tcg_temp_free(t0);
        break;/*ld.w*/
    case 0x9:
        rx = insn & 0x000f;
        imm = ((insn & 0x00f0) >> 4) << 2;
        rz = (insn & 0x0f00) >> 8;

        t0 = tcg_temp_new();
        tcg_gen_addi_tl(t0, cpu_R[rx], imm);
        tcg_gen_qemu_st32(cpu_R[rz], t0, ctx->mem_idx);
        tcg_temp_free(t0);
        break;/*st.w*/
    case 0xa:
        rx = insn & 0x000f;
        imm = (insn & 0x00f0) >> 4;
        rz = (insn & 0x0f00) >> 8;

        t0 = tcg_temp_new();
        tcg_gen_addi_tl(t0, cpu_R[rx], imm);
        tcg_gen_qemu_ld8u(cpu_R[rz], t0, ctx->mem_idx);
        tcg_temp_free(t0);
        break;/*ld.b*/
    case 0xb:
        rx = insn & 0x000f;
        imm = (insn & 0x00f0) >> 4;
        rz = (insn & 0x0f00) >> 8;

        t0 = tcg_temp_new();
        tcg_gen_addi_tl(t0, cpu_R[rx], imm);
        tcg_gen_qemu_st8(cpu_R[rz], t0, ctx->mem_idx);
        tcg_temp_free(t0);
        break;/*st.b*/
    case 0xc:
        rx = insn & 0x000f;
        imm = ((insn & 0x00f0) >> 4) << 1;
        rz = (insn & 0x0f00) >> 8;

        t0 = tcg_temp_new();
        tcg_gen_addi_tl(t0, cpu_R[rx], imm);
        tcg_gen_qemu_ld16u(cpu_R[rz], t0, ctx->mem_idx);
        tcg_temp_free(t0);
        break;/*ld.h*/
    case 0xd:
        rx = insn & 0x000f;
        imm = ((insn & 0x00f0) >> 4) << 1;
        rz = (insn & 0x0f00) >> 8;

        t0 = tcg_temp_new();
        tcg_gen_addi_tl(t0, cpu_R[rx], imm);
        tcg_gen_qemu_st16(cpu_R[rz], t0, ctx->mem_idx);
        tcg_temp_free(t0);

        break;/*st.h*/
    case 0xe:
        if (insn_2 >> 3 == 0) {
            disp = insn & 0x07ff;

            bt(ctx, disp);
            ctx->is_jmp = DISAS_TB_JUMP;
            break;/*bt*/
        } else if (insn_2 >> 3 == 1) {
            disp = insn & 0x07ff;

            bf(ctx, disp);
            ctx->is_jmp = DISAS_TB_JUMP;
            break;/*bf*/
        } else {
            goto illegal_op;
        }
    case 0xf:
        if (insn_2 >> 3 == 0) {
            disp = insn & 0x07ff;

            br(ctx, disp);
            ctx->is_jmp = DISAS_TB_JUMP;
            break;/*br*/
        } else if (insn_2 >> 3 == 1) {
            disp = insn & 0x07ff;

            bsr(ctx, disp);
            ctx->is_jmp = DISAS_TB_JUMP;
            break;/*bsr*/
        } else {
            goto illegal_op;
        }
    default:

illegal_op:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void csky_tb_start(CPUCSKYState *env, TranslationBlock *tb)
{
    uint32_t tb_pc = (uint32_t)tb->pc;
    TCGv t0 = tcg_temp_new();

    t0 = tcg_const_tl(tb_pc);
    gen_helper_tb_trace(cpu_env, t0);
    tcg_temp_free(t0);
}

static void csky_dump_tb_map(CPUCSKYState *env, TranslationBlock *tb)
{
    uint32_t tb_pc = (uint32_t)tb->pc;
    uint32_t tb_end =  tb_pc + (uint32_t)tb->size;
    uint32_t icount = (uint32_t)tb->icount;

    qemu_log_mask(CPU_TB_TRACE, "tb_map: 0x%.8x 0x%.8x %d\n",
                  tb_pc, tb_end, icount);
}

static int jcount_start_insn_idx;
static void gen_csky_jcount_start(CPUCSKYState *env, TranslationBlock *tb)
{
    uint32_t tb_pc = (uint32_t)tb->pc;
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new_i32();
    /* We emit a movi with a dummy immediate argument. Keep the insn index
     * of the movi so that we later (when we know the actual insn count)
     * can update the immediate argument with the actual insn count.  */
    jcount_start_insn_idx = tcg_op_buf_count();
    tcg_gen_movi_i32(t1, 0xdeadbeef);

    t0 = tcg_const_tl(tb_pc);
    gen_helper_jcount(cpu_env, t0, t1);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static void gen_csky_jcount_end(int num_insns)
{
    tcg_set_insn_param(jcount_start_insn_idx, 1, num_insns);
}

/* generate intermediate code in tcg_ctx.gen_opc_buf and gen_opparam_buf for
   basic block 'tb'. If search_pc is TRUE, also generate PC
   information for each intermediate instruction. */
void gen_intermediate_code(CPUState *cs, TranslationBlock *tb)
{
    CPUCSKYState *env = cs->env_ptr;
    DisasContext dc1, *dc = &dc1;
    target_ulong pc_start;
    uint32_t next_page_start;
    int num_insns;
    int max_insns;

    /* part var init */
    pc_start = tb->pc;
    next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }
    if (max_insns > TCG_MAX_INSNS) {
        max_insns = TCG_MAX_INSNS;
    }

    /* DisasContext init */
    dc->tb = tb;
    dc->pc = tb->pc;
    dc->singlestep_enabled = cs->singlestep_enabled;
    dc->is_jmp = DISAS_NEXT;
    dc->features = env->features;

#ifndef CONFIG_USER_ONLY
    dc->super = CSKY_TBFLAG_PSR_S(tb->flags);
    dc->current_cp = CSKY_TBFLAG_CPID(tb->flags);
    dc->trace_mode = (TraceMode)CSKY_TBFLAG_PSR_TM(tb->flags);
#endif

#ifdef CONFIG_USER_ONLY
    dc->mem_idx = CSKY_USERMODE;
#else
    dc->mem_idx = dc->super;
#endif

    gen_tb_start(tb);

    if (env->jcount_start != 0) {
        gen_csky_jcount_start(env, tb);
    }

    if (env->tb_trace == 1) {
        csky_tb_start(env, tb);
    }

    /* for idly */
#if !defined(CONFIG_USER_ONLY)
    uint32_t idly4_counter;
    idly4_counter = env->idly4_counter;
    if (unlikely(idly4_counter != 0)) {
        TCGv t0 = tcg_temp_new();
        do {
            /* Intercept jump to the magic kernel page.  */
#ifdef CONFIG_USER_ONLY
            if (dc->pc >= 0x80000000) {
                /* We always get here via a jump, so know we are not in a
                   conditional execution block.  */
                generate_exception(dc, EXCP_CSKY_PRIVILEGE);
                dc->is_jmp = DISAS_UPDATE;
                break;
            }
#endif
            tcg_gen_insn_start(dc->pc);
            num_insns++;

            if (unlikely(cpu_breakpoint_test(cs, dc->pc, BP_ANY))) {
                generate_exception(dc, EXCP_DEBUG);
                dc->is_jmp = DISAS_JUMP;
                dc->pc += 2;
                goto done_generating;
                break;
            }

            if (num_insns == max_insns && (tb->cflags & CF_LAST_IO)) {
                gen_io_start();
            }

            dc->insn = cpu_lduw_code(env, dc->pc);

            disas_csky_v1_insn(env, dc);
            dc->pc += 2;

            idly4_counter--;

            if (!idly4_counter) {
                break;
            }
        } while (!dc->is_jmp && !tcg_op_buf_full() &&
                 !cs->singlestep_enabled &&
                 !singlestep &&
                 dc->pc < next_page_start &&
                 num_insns < max_insns);

        t0 = tcg_const_tl(idly4_counter);
        store_cpu_field(t0, idly4_counter);

        tcg_temp_free(t0);
        goto done_translation;
    }
#endif

    do {
#if !defined(CONFIG_USER_ONLY)
        dc->cannot_be_traced = 0;
        dc->maybe_change_flow = 0;
#endif
        /* Intercept jump to the magic kernel page.  */
#ifdef CONFIG_USER_ONLY
        if (dc->pc >= 0x80000000) {
            /* We always get here via a jump, so know we are not in a
                 conditional execution block.  */
            generate_exception(dc, EXCP_CSKY_PRIVILEGE);
            dc->is_jmp = DISAS_UPDATE;
            break;
        }
#endif

        tcg_gen_insn_start(dc->pc);
        num_insns++;

        if (unlikely(cpu_breakpoint_test(cs, dc->pc, BP_ANY))) {
            generate_exception(dc, EXCP_DEBUG);
            dc->is_jmp = DISAS_JUMP;
            dc->pc += 2;
            goto done_generating;
            break;
        }

        if (num_insns == max_insns && (tb->cflags & CF_LAST_IO)) {
            gen_io_start();
        }

        dc->insn = cpu_lduw_code(env, dc->pc);

        disas_csky_v1_insn(env, dc);
        dc->pc += 2;

#if !defined(CONFIG_USER_ONLY)
        if (dc->cannot_be_traced) {
            break;
        }

        if (dc->trace_mode == INST_TRACE_MODE) {
            if (!dc->maybe_change_flow) {
                generate_exception(dc, EXCP_CSKY_TRACE);
            }
            break;
        }
#endif

        /* Translation stops when a conditional branch is encountered.
         * Otherwise the subsequent code could get translated several times.
         * Also stop translation when a page boundary is reached.  This
         * ensures prefetch aborts occur at the right place.  */
    } while (!dc->is_jmp && !tcg_op_buf_full() &&
             !cs->singlestep_enabled &&
             !singlestep &&
             dc->pc < next_page_start &&
             num_insns < max_insns);

#ifndef CONFIG_USER_ONLY
done_translation:
#endif
    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }

    if (unlikely(cs->singlestep_enabled)) {
        if (!dc->is_jmp) {
            generate_exception(dc, EXCP_DEBUG);
        } else if (dc->is_jmp != DISAS_TB_JUMP) {
            TCGv t0 = tcg_temp_new();

            t0 = tcg_const_tl(EXCP_DEBUG);
            gen_helper_exception(cpu_env, t0);

            tcg_temp_free(t0);
        }
    } else {
        switch (dc->is_jmp) {
        case DISAS_NEXT:
            gen_goto_tb(dc, 1, dc->pc);
            break;
        case DISAS_JUMP:
        case DISAS_UPDATE:
        /* indicate that the hash table must be used to find the next TB */
            tcg_gen_exit_tb(0);
            break;
        case DISAS_TB_JUMP:
        /* nothing more to generate */
            break;
        }
    }

done_generating:
    if (env->jcount_start != 0) {
        gen_csky_jcount_end(num_insns);
    }
    gen_tb_end(tb, num_insns);

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("----------------\n");
        qemu_log("IN: %s\n", lookup_symbol(pc_start));
        log_target_disas(cs, pc_start, dc->pc - pc_start);
        qemu_log("\n");
    }
#endif

    tb->size = dc->pc - pc_start;
    tb->icount = num_insns;
    if (env->tb_trace == 1) {
        csky_dump_tb_map(env, tb);
    }
}

void csky_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                         int flags)
{
    int i;
    uint32_t psr;
    CSKYCPU *cpu = CSKY_CPU(cs);
    CPUCSKYState *env = &cpu->env;

    psr = (env->psr_s << 31) | (env->psr_tm << 14) | (env->cp0.psr) |
           (env->psr_c);

    for (i = 0; i < 16; i++) {
        cpu_fprintf(f, "R%02d=%08x", i, env->regs[i]);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }

    cpu_fprintf(f, "psr=%08x ", psr);
    cpu_fprintf(f, "pc=%08x\n", env->pc);
    cpu_fprintf(f, "epsr=%08x ", env->cp0.epsr);
    cpu_fprintf(f, "epc=%08x\n", env->cp0.epc);
    cpu_fprintf(f, "hi=%08x ", env->hi);
    cpu_fprintf(f, "lo=%08x ", env->lo);
    cpu_fprintf(f, "hi_guard=%08x ", env->hi_guard);
    cpu_fprintf(f, "lo_guard=%08x\n", env->lo_guard);
}

void restore_state_to_opc(CPUCSKYState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->pc = data[0];
}
