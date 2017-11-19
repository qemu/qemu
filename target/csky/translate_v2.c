/*
 * CSKY translation
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
#include <math.h>
#include "exec/gdbstub.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

/*******************************/
#define sp 14
#define SVBR 30
#define FP 23
#define TOP 24
#define BSP 25
#define WORD_MASK 0x3
#define REG_MASK   0x1c
/*******************************************/

/* is_jmp field values */
#define DISAS_JUMP    DISAS_TARGET_0 /* only pc was modified dynamically */
#define DISAS_UPDATE  DISAS_TARGET_1 /* cpu state was modified dynamically */
#define DISAS_TB_JUMP DISAS_TARGET_2 /* only pc was modified statically */

static TCGv_i32 cpu_R[32];
static TCGv_i32 vdsp_Rl[16];
static TCGv_i32 vdsp_Rh[16];
static TCGv_i32 cpu_c;
static TCGv_i32 cpu_v;
static TCGv_i32 cpu_hi ;
static TCGv_i32 cpu_lo;
static TCGv_i32 cpu_hi_guard;
static TCGv_i32 cpu_lo_guard;

static TCGv_i32 cpu_F0s, cpu_F1s;
static TCGv_i64 cpu_F0d, cpu_F1d;

#include "exec/gen-icount.h"

static const char *regnames[] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "sp", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31" };

static const char *vreglnames[] = {
    "vr0l", "vr1l", "vr2l", "vr3l", "vr4l", "vr5l", "vr6l", "vr7l",
    "vr8l", "vr9l", "vr10l", "vr11l", "vr12l", "vr13l", "rv14l", "vr15l" };

static const char *vreghnames[] = {
    "vr0h", "vr1h", "vr2h", "vr3h", "vr4h", "vr5h", "vr6h", "vr7h",
    "vr8h", "vr9h", "vr10h", "vr11h", "vr12h", "vr13h", "rv14h", "vr15h" };

#if defined(CONFIG_USER_ONLY)
#define IS_SUPER(ctx)   0
#define IS_TRUST(ctx)   0
#else
#define IS_SUPER(ctx)   (ctx->super)
#define IS_TRUST(ctx)   (ctx->trust)
#endif

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

static inline void mulsw(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv t2 = tcg_temp_new();

    tcg_gen_ext16s_tl(t2, cpu_R[rx]);
    tcg_gen_ext_tl_i64(t0, t2);
    tcg_gen_ext_tl_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 16);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);

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

/* initialize TCG globals. */
void csky_translate_init(void)
{
    int i;

    for (i = 0; i < 32; i++) {
        cpu_R[i] = tcg_global_mem_new_i32(cpu_env,
            offsetof(CPUCSKYState, regs[i]),
            regnames[i]);
    }

    for (i = 0; i < 16; i++) {
        tcg_gen_extrl_i64_i32(vdsp_Rl[i], (tcg_global_mem_new_i64(cpu_env,
            offsetof(CPUCSKYState, vfp.reg[i].udspl[0]),
            vreglnames[i])));
    }

    for (i = 0; i < 16; i++) {
        tcg_gen_extrl_i64_i32(vdsp_Rh[i], (tcg_global_mem_new_i64(cpu_env,
            offsetof(CPUCSKYState, vfp.reg[i].udspl[1]),
            vreghnames[i])));
    }

    cpu_c = tcg_global_mem_new_i32(cpu_env,
        offsetof(CPUCSKYState, psr_c), "cpu_c");
    cpu_v = tcg_global_mem_new_i32(cpu_env,
        offsetof(CPUCSKYState, dcsr_v), "cpu_v");
    cpu_hi = tcg_global_mem_new_i32(cpu_env,
        offsetof(CPUCSKYState, hi), "cpu_hi");
    cpu_lo = tcg_global_mem_new_i32(cpu_env,
        offsetof(CPUCSKYState, lo), "cpu_lo");
    cpu_hi_guard = tcg_global_mem_new_i32(cpu_env,
        offsetof(CPUCSKYState, hi_guard), "cpu_hi_guard");
    cpu_lo_guard = tcg_global_mem_new_i32(cpu_env,
        offsetof(CPUCSKYState, lo_guard), "cpu_lo_guard");
}

static TCGv_i32 new_tmp(void)
{
    return tcg_temp_new_i32();
}

static void dead_tmp(TCGv tmp)
{
    tcg_temp_free(tmp);
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

    t0 = tcg_const_tl(pc);
    store_cpu_field(t0, pc);

    tcg_temp_free(t0);
}

static inline void generate_exception(DisasContext *ctx, int excp)
{
    TCGv t0 = tcg_temp_new();

    print_exception(ctx, excp);

    t0 = tcg_const_tl(excp);
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


#define ldst(name, rx, rz, imm, isize)                         \
do {                                                           \
    if (ctx->bctm) {                                           \
        TCGLabel *l1 = gen_new_label();                        \
        tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[rx], 0, l1);     \
        tcg_gen_movi_tl(cpu_R[15], ctx->pc + isize);           \
        tcg_gen_subi_tl(t0, cpu_R[SVBR], 4);                   \
        store_cpu_field(t0, pc);                               \
        tcg_gen_exit_tb(0);                                    \
        gen_set_label(l1);                                     \
        tcg_gen_addi_tl(t0, cpu_R[rx], imm);                   \
        tcg_gen_qemu_##name(cpu_R[rz], t0, ctx->mem_idx);      \
        gen_goto_tb(ctx, 1, ctx->pc + isize);                  \
        ctx->is_jmp = DISAS_TB_JUMP;                           \
    } else {                                                   \
        tcg_gen_addi_tl(t0, cpu_R[rx], imm);                   \
        tcg_gen_qemu_##name(cpu_R[rz], t0, ctx->mem_idx);      \
    }                                                          \
} while (0)

#define ldrstr(name, rx, ry, rz, imm)                          \
do {                                                           \
    if (ctx->bctm) {                                           \
        TCGLabel *l1 = gen_new_label();                        \
        tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[rx], 0, l1);     \
        tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);               \
        tcg_gen_subi_tl(t0, cpu_R[SVBR], 4);                   \
        store_cpu_field(t0, pc);                               \
        tcg_gen_exit_tb(0);                                    \
        gen_set_label(l1);                                     \
        tcg_gen_shli_tl(t0, cpu_R[ry], imm);                   \
        tcg_gen_add_tl(t0, cpu_R[rx], t0);                     \
        tcg_gen_qemu_##name(cpu_R[rz], t0, ctx->mem_idx);      \
        gen_goto_tb(ctx, 1, ctx->pc + 4);                      \
        ctx->is_jmp = DISAS_TB_JUMP;                           \
        break;                                                 \
    } else {                                                   \
        tcg_gen_shli_tl(t0, cpu_R[ry], imm);                   \
        tcg_gen_add_tl(t0, cpu_R[rx], t0);                     \
        tcg_gen_qemu_##name(cpu_R[rz], t0, ctx->mem_idx);      \
    }                                                          \
} while (0)

/* The insn support the cpu contained in the argument 'flags'.  */
static inline void check_insn(DisasContext *ctx, uint32_t flags)
{
    if (unlikely(!has_insn(ctx, flags))) {
        generate_exception(ctx, EXCP_CSKY_UDEF);
    }
}

/* The insn not support the cpu contained in the argument 'flags'.  */
static inline void check_insn_except(DisasContext *ctx, uint32_t flags)
{
    if (unlikely(has_insn(ctx, flags))) {
        generate_exception(ctx, EXCP_CSKY_UDEF);
    }
}

/* Set a variable to the value of a CPU register.  */
static void load_reg_var(DisasContext *s, TCGv var, int reg)
{
    tcg_gen_mov_i32(var, cpu_R[reg]);
}

/* Create a new temporary and set it to the value of a CPU register.  */
static inline TCGv load_reg(DisasContext *s, int reg)
{
    TCGv tmp = new_tmp();
    load_reg_var(s, tmp, reg);
    return tmp;
}

static inline void addc(int rz, int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_local_new();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();

    tcg_gen_mov_tl(t1, cpu_R[rx]);
    tcg_gen_add_tl(t0, t1, cpu_R[ry]);
    tcg_gen_add_tl(cpu_R[rz], t0, cpu_c);
    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
    tcg_gen_setcond_tl(TCG_COND_LTU, cpu_c, cpu_R[rz], t1);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_setcond_tl(TCG_COND_LEU, cpu_c, cpu_R[rz], t1);
    gen_set_label(l2);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static inline void subc(int rz, int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_local_new();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();

    tcg_gen_subfi_tl(t0, 1, cpu_c);
    tcg_gen_sub_tl(t1, cpu_R[rx], cpu_R[ry]);
    tcg_gen_sub_tl(t2, t1, t0);
    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
    tcg_gen_setcond_tl(TCG_COND_GTU, cpu_c, cpu_R[rx], cpu_R[ry]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_setcond_tl(TCG_COND_GEU, cpu_c, cpu_R[rx], cpu_R[ry]);
    gen_set_label(l2);
    tcg_gen_mov_tl(cpu_R[rz], t2);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);

}


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

static inline void lsl(int rz, int rx, int ry)
{
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();

    TCGLabel *l1 = gen_new_label();

    tcg_gen_mov_tl(t1, cpu_R[rx]);
    tcg_gen_andi_tl(t0, cpu_R[ry], 0x3f);
    tcg_gen_movi_tl(cpu_R[rz], 0);
    tcg_gen_brcondi_tl(TCG_COND_GTU, t0, 31, l1);
    tcg_gen_shl_tl(cpu_R[rz], t1, t0);
    gen_set_label(l1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static inline void lsr(int rz, int rx, int ry)
{
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();

    TCGLabel *l1 = gen_new_label();

    tcg_gen_mov_tl(t1, cpu_R[rx]);
    tcg_gen_andi_tl(t0, cpu_R[ry], 0x3f);
    tcg_gen_movi_tl(cpu_R[rz], 0);
    tcg_gen_brcondi_tl(TCG_COND_GTU, t0, 31, l1);
    tcg_gen_shr_tl(cpu_R[rz], t1, t0);
    gen_set_label(l1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static inline void asr(int rz, int rx, int ry)
{
    TCGv t0 = tcg_temp_local_new();

    TCGLabel *l1 = gen_new_label();
    tcg_gen_andi_tl(t0, cpu_R[ry], 0x3f);
    tcg_gen_brcondi_tl(TCG_COND_LEU, t0, 31, l1);
    tcg_gen_movi_tl(t0, 31);
    gen_set_label(l1);
    tcg_gen_sar_tl(cpu_R[rz], cpu_R[rx], t0);

    tcg_temp_free(t0);
}

static inline void rotl(int rz, int rx, int ry)
{
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();

    TCGLabel *l1 = gen_new_label();

    tcg_gen_mov_tl(t1, cpu_R[rx]);
    tcg_gen_andi_tl(t0, cpu_R[ry], 0x3f);
    tcg_gen_movi_tl(cpu_R[rz], 0);
    tcg_gen_brcondi_tl(TCG_COND_GTU, t0, 31, l1);
    tcg_gen_rotl_tl(cpu_R[rz], t1, t0);
    gen_set_label(l1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static inline void branch16(DisasContext *ctx, TCGCond cond, int offset)
{
    int val;
    TCGLabel *l1 = gen_new_label();
    TCGv t0 = tcg_temp_new();

    val = offset << 1;
    if (val & 0x400) {
        val |= 0xfffffc00;
    }
    val += ctx->pc;

    tcg_gen_brcondi_tl(cond, cpu_c, 0, l1);
    gen_goto_tb(ctx, 1, ctx->pc + 2);
    gen_set_label(l1);

    gen_goto_tb(ctx, 0, val);

    ctx->is_jmp = DISAS_TB_JUMP;

    tcg_temp_free(t0);
}

static inline void bsr16(DisasContext *ctx, int offset)
{
    int val;

    val = offset << 1;
    if (val & 0x400) {
        val |= 0xfffffc00;
    }
    val += ctx->pc;

    gen_goto_tb(ctx, 0, val);

    ctx->is_jmp = DISAS_TB_JUMP;
}

static inline void pop16(DisasContext *ctx, int imm)
{
    int i;
    TCGv t0 = tcg_temp_new();
    tcg_gen_mov_tl(t0, cpu_R[sp]);
    if (imm & 0xf) {
        for (i = 0; i < (imm & 0xf); i++) {
            tcg_gen_qemu_ld32u(cpu_R[i + 4], t0, ctx->mem_idx);
            tcg_gen_addi_i32(t0, t0, 4);
        }
    }
    if (imm & 0x10) {
        tcg_gen_qemu_ld32u(cpu_R[15], t0, ctx->mem_idx);
        tcg_gen_addi_i32(t0, t0, 4);
    }
    tcg_gen_mov_tl(cpu_R[sp], t0);

    tcg_gen_andi_tl(t0, cpu_R[15], 0xfffffffe);
    store_cpu_field(t0, pc);
    ctx->is_jmp = DISAS_JUMP;
    tcg_temp_free(t0);
}

static inline void push16(DisasContext *ctx, int imm)
{
    int i;
    TCGv t0 = tcg_temp_new();

    tcg_gen_mov_tl(t0, cpu_R[sp]);

    if (imm & 0x10) {
        tcg_gen_subi_i32(t0, t0, 4);
        tcg_gen_qemu_st32(cpu_R[15], t0, ctx->mem_idx);
    }

    if (imm & 0xf) {
        for (i = (imm & 0xf); i > 0; i--) {
            tcg_gen_subi_i32(t0, t0, 4);
            tcg_gen_qemu_st32(cpu_R[i + 3], t0, ctx->mem_idx);
        }
    }
    tcg_gen_mov_tl(cpu_R[sp], t0);
    tcg_temp_free(t0);
}

static inline void gen_cmp16(DisasContext *ctx, uint32_t sop, int rz, int rx)
{
    switch (sop) {
    case 0x0:
        /* cmphs */
        tcg_gen_setcond_tl(TCG_COND_GEU, cpu_c, cpu_R[rx], cpu_R[rz]);
        break;
    case 0x1:
        /* cmplt */
        tcg_gen_setcond_tl(TCG_COND_LT, cpu_c, cpu_R[rx], cpu_R[rz]);
        break;
    case 0x2:
        /* cmpne */
        tcg_gen_setcond_tl(TCG_COND_NE, cpu_c, cpu_R[rx], cpu_R[rz]);
        break;
    case 0x3:
        /* mvcv */
        tcg_gen_subfi_tl(cpu_R[rz], 1, cpu_c);
        break;
    default:
        break;
    }
}

static inline void gen_logic_and16(DisasContext *ctx, uint32_t sop,
                                   int rz, int rx)
{
    switch (sop) {
    case 0x0:
        /* and */
        tcg_gen_and_tl(cpu_R[rz], cpu_R[rz], cpu_R[rx]);
        break;
    case 0x1:
        /* andn */
        tcg_gen_andc_tl(cpu_R[rz], cpu_R[rz], cpu_R[rx]);
        break;
    case 0x2:
        /* tst */
        {
            TCGv t0 = tcg_temp_new();
            tcg_gen_and_tl(t0, cpu_R[rx], cpu_R[rz]);
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, t0, 0);

            tcg_temp_free(t0);
        }
        break;
    case 0x3:
        /* tstnbz */
        tstnbz(rx);
        break;
    default:
        break;
    }
}

static inline void gen_logic_or16(DisasContext *ctx, uint32_t sop,
                                  int rz, int rx)
{
    switch (sop) {
    case 0x0:
        /* or */
        tcg_gen_or_tl(cpu_R[rz], cpu_R[rz], cpu_R[rx]);
        break;
    case 0x1:
        /* xor */
        tcg_gen_xor_tl(cpu_R[rz], cpu_R[rz], cpu_R[rx]);
        break;
    case 0x2:
        /* nor */
        tcg_gen_nor_tl(cpu_R[rz], cpu_R[rz], cpu_R[rx]);
        break;
    case 0x3:
        /* mov */
        tcg_gen_mov_tl(cpu_R[rz], cpu_R[rx]);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void gen_shift_reg16(DisasContext *ctx, uint32_t sop,
                                   int rz, int rx)
{
    switch (sop) {
    case 0x0:
        /* lsl */
        lsl(rz, rz, rx);
        break;
    case 0x1:
        /* lsr */
        lsr(rz, rz, rx);
        break;
    case 0x2:
        /* asr */
        asr(rz, rz, rx);
        break;
    case 0x3:
        /* rotl */
        rotl(rz, rz, rx);
        break;
    default:
        break;
    }
}

static inline void gen_ext16(DisasContext *ctx, uint32_t sop, int rz, int rx)
{
    switch (sop) {
    case 0x0:
        /* zextb */
        tcg_gen_andi_tl(cpu_R[rz], cpu_R[rx], 0xff);
        break;
    case 0x1:
        /* zexth */
        tcg_gen_andi_tl(cpu_R[rz], cpu_R[rx], 0xffff);
        break;
    case 0x2:
        /* sextb */
        tcg_gen_shli_tl(cpu_R[rz], cpu_R[rx], 24);
        tcg_gen_sari_tl(cpu_R[rz], cpu_R[rz], 24);
        break;
    case 0x3:
        /* sexth */
        tcg_gen_shli_tl(cpu_R[rz], cpu_R[rx], 16);
        tcg_gen_sari_tl(cpu_R[rz], cpu_R[rz], 16);
        break;
    default:
        break;
    }
}

static inline void gen_arith_misc16(DisasContext *ctx, uint32_t sop,
                                    int rz, int rx)
{
    switch (sop) {
    case 0x0:
        /* jmp */
        {
            TCGv t0 = tcg_temp_new();
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
            ctx->is_jmp = DISAS_JUMP;
            tcg_temp_free(t0);
        }
        break;
    case 0x1:
        /* jsr */
        {
            TCGv t0 = tcg_temp_new();
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
            ctx->is_jmp = DISAS_JUMP;
            tcg_temp_free(t0);
            break;
        }
        break;
    case 0x2:
        /* revb */
        check_insn_except(ctx, CPU_801);
        tcg_gen_bswap32_tl(cpu_R[rz], cpu_R[rx]);
        break;
    case 0x3:
        /* revh */
        {
            check_insn_except(ctx, CPU_801);
            TCGv t0 = tcg_temp_new();
            TCGv t1 = tcg_temp_new();

            tcg_gen_bswap32_tl(t0, cpu_R[rx]);
            tcg_gen_shri_tl(t1, t0, 16);
            tcg_gen_shli_tl(t0, t0, 16);
            tcg_gen_or_tl(cpu_R[rz], t0, t1);

            tcg_temp_free(t0);
            tcg_temp_free(t1);
        }
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void gen_mul16(DisasContext *ctx, uint32_t sop, int rz, int rx)
{
    switch (sop) {
    case 0x0:
        /* mult */
        tcg_gen_mul_tl(cpu_R[rz], cpu_R[rz], cpu_R[rx]);
        break;
    case 0x1:
        /* mulsh */
        check_insn_except(ctx, CPU_801 | CPU_802);
        {
            TCGv t0 = tcg_temp_new();

            tcg_gen_ext16s_tl(t0, cpu_R[rx]);
            tcg_gen_ext16s_tl(cpu_R[rz], cpu_R[rz]);
            tcg_gen_mul_tl(cpu_R[rz], cpu_R[rz], t0);

            tcg_temp_free(t0);
        }
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}



static void gen_branch16(DisasContext *ctx, uint32_t op, int offset)
{
    switch (op) {
    case 0x0:
        if (offset == 0) {
            /*bkpt16*/
            if (is_gdbserver_start == TRUE) {
                generate_exception(ctx, EXCP_DEBUG);
                ctx->is_jmp = DISAS_JUMP;
            } else {
                generate_exception(ctx, EXCP_CSKY_BKPT);
            }
#if !defined(CONFIG_USER_ONLY)
            ctx->cannot_be_traced = 1;
#endif
        } else if (!has_insn(ctx, ABIV2_ELRW)) {
            /*bsr16*/
            tcg_gen_movi_tl(cpu_R[15], ctx->pc + 2);
            bsr16(ctx, offset);
        } else {
            /* lrw16 extend */
            unsigned int imm, rz, addr;
            TCGv t0 = tcg_temp_new();

            imm = ((ctx->insn & 0x300) >> 3) | (ctx->insn & 0x1f);
            imm = (~imm & 0x7f) | 0x80;
            rz = (ctx->insn >> 5) & 0x7;
            addr = (ctx->pc + (imm << 2)) & 0xfffffffc ;
            tcg_gen_movi_tl(t0, addr);
            tcg_gen_qemu_ld32u(cpu_R[rz], t0, ctx->mem_idx);
            tcg_temp_free(t0);
        }
        break;
    case 0x1:
        /*br16*/
        {
            int val;

            val = offset << 1;
            if (val & 0x400) {
                val |= 0xfffffc00;
            }
            val += ctx->pc;

            gen_goto_tb(ctx, 0, val);

            ctx->is_jmp = DISAS_TB_JUMP;
        }
        break;
    case 0x2:
        /*bt16*/
        branch16(ctx, TCG_COND_NE, offset);
        break;
    case 0x3:
        /*bf16*/
        branch16(ctx, TCG_COND_EQ, offset);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void gen_nvic_insn(DisasContext *ctx, uint32_t op, int imm)
{
    int i;
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_mov_i32(t0, cpu_R[sp]);

    switch (imm) {
    case 0x0:
        /* nie */
        t1 = load_cpu_field(cp0.epc);
        tcg_gen_subi_i32(t0, t0, 4);
        tcg_gen_qemu_st32(t1, t0, ctx->mem_idx);
        t1 = load_cpu_field(cp0.epsr);
        tcg_gen_subi_i32(t0, t0, 4);
        tcg_gen_qemu_st32(t1, t0, ctx->mem_idx);
        tcg_gen_mov_i32(cpu_R[sp], t0);
        t1 = load_cpu_field(cp0.psr);
        tcg_gen_ori_i32(t1, t1, PSR_EE_MASK);
        tcg_gen_ori_i32(t1, t1, PSR_IE_MASK);
        store_cpu_field(t1, cp0.psr);
        break;
    case 0x1:
        /* nir */
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            tcg_gen_qemu_ld32u(t1, t0, ctx->mem_idx);
            store_cpu_field(t1, cp0.epsr);
            tcg_gen_addi_i32(t0, t0, 4);
            tcg_gen_qemu_ld32u(t1, t0, ctx->mem_idx);
            store_cpu_field(t1, cp0.epc);
            tcg_gen_addi_i32(t0, t0, 4);
            tcg_gen_mov_i32(cpu_R[sp], t0);
            t0 = tcg_const_i32(0);
            store_cpu_field(t0, idly4_counter);
            gen_helper_rte(cpu_env);
            ctx->is_jmp = DISAS_UPDATE;
        }
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x2:
        /* ipush */
        tcg_gen_subi_i32(t0, t0, 4);
        tcg_gen_qemu_st32(cpu_R[13], t0, ctx->mem_idx);
        tcg_gen_subi_i32(t0, t0, 4);
        tcg_gen_qemu_st32(cpu_R[12], t0, ctx->mem_idx);
        for (i = 3; i >= 0; i--) {
            tcg_gen_subi_i32(t0, t0, 4);
            tcg_gen_qemu_st32(cpu_R[i], t0, ctx->mem_idx);
        }
        tcg_gen_mov_i32(cpu_R[sp], t0);
        break;
    case 0x3:
        /* ipop */
        for (i = 0; i <= 3; i++) {
            tcg_gen_qemu_ld32u(cpu_R[i], t0, ctx->mem_idx);
            tcg_gen_addi_i32(t0, t0, 4);
        }
        tcg_gen_qemu_ld32u(cpu_R[12], t0, ctx->mem_idx);
        tcg_gen_addi_i32(t0, t0, 4);
        tcg_gen_qemu_ld32u(cpu_R[13], t0, ctx->mem_idx);
        tcg_gen_addi_i32(t0, t0, 4);
        tcg_gen_mov_i32(cpu_R[sp], t0);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
}

static void gen_imm7_arith16(DisasContext *ctx, uint32_t op, uint16_t sop,
                             int imm)
{
    switch (sop) {
    case 0x0:
        /*addisp(2)*/
        tcg_gen_addi_tl(cpu_R[sp], cpu_R[sp], imm << 2);
        break;
    case 0x1:
        /*subisp*/
        tcg_gen_subi_tl(cpu_R[sp], cpu_R[sp], imm << 2);
        break;
    case 0x3:
        check_insn(ctx, CPU_801 | CPU_802 | CPU_803S);
        gen_nvic_insn(ctx, op, imm);
        break;
    case 0x4:
        /*pop16*/
        if (ctx->bctm) {
            TCGv t0 = tcg_temp_new();
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[sp], 0, l1);
            tcg_gen_movi_tl(cpu_R[15], ctx->pc + 2);
            tcg_gen_subi_tl(t0, cpu_R[SVBR], 4);
            store_cpu_field(t0, pc);
            tcg_gen_exit_tb(0);
            gen_set_label(l1);
            pop16(ctx, imm & 0x1f);
            tcg_temp_free(t0);
            break;
        } else {
            pop16(ctx, imm & 0x1f);
            break;
        }
    case 0x6:
        /*push16*/
        if (ctx->bctm) {
            TCGv t0 = tcg_temp_new();
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[sp], 0, l1);
            tcg_gen_movi_tl(cpu_R[15], ctx->pc + 2);
            tcg_gen_subi_tl(t0, cpu_R[SVBR], 4);
            store_cpu_field(t0, pc);
            tcg_gen_exit_tb(0);
            gen_set_label(l1);
            push16(ctx, imm & 0x1f);
            gen_goto_tb(ctx, 1, ctx->pc + 2);
            ctx->is_jmp = DISAS_TB_JUMP;
            tcg_temp_free(t0);
            break;
        } else {
            push16(ctx, imm & 0x1f);
            break;
        }
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void gen_imm8_arith16(DisasContext *ctx, uint32_t op, int rz, int imm)
{
    switch (op) {
    case 0x6:
    case 0x7:
        /*addisp(1)*/
        tcg_gen_addi_tl(cpu_R[rz], cpu_R[sp], imm << 2);
        break;
    case 0x8:
    case 0x9:
        /*addi16(1)*/
        tcg_gen_addi_tl(cpu_R[rz], cpu_R[rz], imm + 1);
        break;
    case 0xa:
    case 0xb:
        /*subi16(1)*/
        tcg_gen_subi_tl(cpu_R[rz], cpu_R[rz], imm + 1);
        break;
    case 0xc:
    case 0xd:
        /*movi16*/
        tcg_gen_movi_tl(cpu_R[rz], imm);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void gen_imm5_arith16(DisasContext *ctx, uint32_t op, uint16_t sop,
                             int rx, int imm)
{
    switch (sop) {
    case 0x0:
        /*cmphsi16*/
        tcg_gen_setcondi_tl(TCG_COND_GEU, cpu_c, cpu_R[rx], imm + 1);
        break;
    case 0x1:
        /*cmplti16*/
        tcg_gen_setcondi_tl(TCG_COND_LT, cpu_c, cpu_R[rx], imm + 1);
        break;
    case 0x2:
        /*cmpnei16*/
        tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rx], imm);
        break;
    case 0x4:
        /*bclri16*/
        tcg_gen_andi_tl(cpu_R[rx], cpu_R[rx], ~(1 << imm));
        break;
    case 0x5:
        /*bseti16*/
        tcg_gen_ori_tl(cpu_R[rx], cpu_R[rx], 1 << imm);
        break;
    case 0x7:
        /* jmpix16 */
        check_insn(ctx, ABIV2_JAVA);
        if (ctx->bctm) {
            TCGv t0 = tcg_temp_new();
            TCGv t1 = tcg_temp_new();
            /*
            tcg_gen_addi_tl(t0, cpu_R[rx], 0xff);
            tcg_gen_and_tl(t0, t0, (16 + ((imm & 0x3) << 3)));
            tcg_gen_add_tl(t0, cpu_R[SVBR], t0);
            */
            tcg_gen_andi_tl(t0, cpu_R[rx], 0xff);
            switch (imm & 0x3) {
            case 0x0:
                tcg_gen_shli_tl(t0, t0, 4);
                break;
            case 0x1:
                tcg_gen_shli_tl(t1, t0, 4);
                tcg_gen_shli_tl(t0, t0, 3);
                tcg_gen_add_tl(t0, t0, t1);
                break;
            case 0x2:
                tcg_gen_shli_tl(t0, t0, 5);
                break;
            case 0x3:
                tcg_gen_shli_tl(t1, t0, 5);
                tcg_gen_shli_tl(t0, t0, 3);
                tcg_gen_add_tl(t0, t0, t1);
                break;
            default:
                break;
            }
            tcg_gen_add_tl(t0, cpu_R[SVBR], t0);
            store_cpu_field(t0, pc);

            ctx->is_jmp = DISAS_JUMP;
            tcg_temp_free(t1);
            tcg_temp_free(t0);
            break;
        }
    case 0x6:
        if (has_insn(ctx, ABIV2_ELRW)) {
            /* btsti16 */
            tcg_gen_andi_tl(cpu_c, cpu_R[rx], 1 << imm);
            tcg_gen_shri_tl(cpu_c, cpu_c, imm);
            break;
        }
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void gen_reg1_arith16(DisasContext *ctx, uint32_t op, int rz,
                             int rx, int imm)
{
    switch (op) {
    case 0x10:
    case 0x11:
        /*lsli16*/
        tcg_gen_shli_tl(cpu_R[rz], cpu_R[rx], imm);
        break;
    case 0x12:
    case 0x13:
        /*lsri16*/
        tcg_gen_shri_tl(cpu_R[rz], cpu_R[rx], imm);
        break;
    case 0x14:
    case 0x15:
        /*asri16*/
        tcg_gen_sari_tl(cpu_R[rz], cpu_R[rx], imm);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void gen_reg3_arith16(DisasContext *ctx, uint32_t op, uint16_t sop,
                             int rz, int rx, int ry, int imm)
{
    switch (sop) {
    case 0x0:
        /*addu16(2)*/
        tcg_gen_add_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case 0x1:
        /*subu16(2)*/
        tcg_gen_sub_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case 0x2:
        /*addi16(2)*/
        tcg_gen_addi_tl(cpu_R[rz], cpu_R[rx], imm + 1);
        break;
    case 0x3:
        /*subi16(2)*/
        tcg_gen_subi_tl(cpu_R[rz], cpu_R[rx], imm + 1);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void gen_reg2_arith16(DisasContext *ctx, uint32_t op, uint16_t sop,
                             int rx, int rz)
{
    switch (op) {
    case 0x18:
        switch (sop) {
        case 0x0:
            /*addu16(1)*/
            tcg_gen_add_tl(cpu_R[rz], cpu_R[rz], cpu_R[rx]);
            break;
        case 0x1:
            /*addc16*/
            addc(rz, rz, rx);
            break;
        case 0x2:
            /*subu16(1)*/
            tcg_gen_sub_tl(cpu_R[rz], cpu_R[rz], cpu_R[rx]);
            break;
        case 0x3:
            /*subc16*/
            subc(rz, rz, rx);
            break;
        default:
            generate_exception(ctx, EXCP_CSKY_UDEF);
            break;
        }
        break;
    case 0x19:
        gen_cmp16(ctx, sop, rz, rx);
        break;
    case 0x1a:
        gen_logic_and16(ctx, sop, rz, rx);
        break;
    case 0x1b:
        gen_logic_or16(ctx, sop, rz, rx);
        break;
    case 0x1c:
        gen_shift_reg16(ctx, sop, rz, rx);
        break;
    case 0x1d:
        gen_ext16(ctx, sop, rz, rx);
        break;
    case 0x1e:
        gen_arith_misc16(ctx, sop, rz, rx);
        break;
    case 0x1f:
        gen_mul16(ctx, sop, rz, rx);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void gen_ldst16(DisasContext *ctx, uint32_t op, int rz, int rx, int imm)
{
    TCGv t0 = tcg_temp_new();

    switch (op) {
    case 0x20:
    case 0x21:
        /*ld.b16*/
        ldst(ld8u, rx, rz, imm, 2);
        break;
    case 0x22:
    case 0x23:
        /*ld.h16*/
        ldst(ld16u, rx, rz, imm << 1, 2);
        break;
    case 0x24:
    case 0x25:
        /*ld.w16*/
        ldst(ld32u, rx, rz, imm << 2, 2);
        break;
    case 0x28:
    case 0x29:
        /*st.b16*/
        ldst(st8, rx, rz, imm, 2);
        break;
    case 0x2a:
    case 0x2b:
        /*st.h16*/
        ldst(st16, rx, rz, imm << 1, 2);
        break;
    case 0x2c:
    case 0x2d:
        /*st.w16*/
        ldst(st32, rx, rz, imm << 2, 2);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }

    tcg_temp_free(t0);

}

static void disas_csky_16_insn(CPUCSKYState *env, DisasContext *ctx)
{
    unsigned int insn, rz, rx, ry;
    uint32_t op, sop;
    int imm;
    target_ulong addr;
    TCGv t0;

    insn = ctx->insn;

    op = (insn >> 10) & 0x3f;
    switch (op) {
    case 0x0 ... 0x3:
        imm = insn & 0x3ff;
        gen_branch16(ctx, op, imm);
        break;
    case 0x4:
        /*lrw16*/
        t0 = tcg_temp_new();
        imm = ((insn & 0x300) >> 3) | (insn & 0x1f);
        rz = (insn >> 5) & 0x7;
        addr = (ctx->pc + (imm << 2)) & 0xfffffffc ;
        tcg_gen_movi_tl(t0, addr);
        tcg_gen_qemu_ld32u(cpu_R[rz], t0, ctx->mem_idx);
        tcg_temp_free(t0);
        break;
    case 0x5:
        imm = ((insn >> 3) & 0x60) | (insn & 0x1f);
        sop = (insn >> 5) & 0x7;
        gen_imm7_arith16(ctx, op, sop, imm);
        break;
    case 0x6 ... 0xd:
        imm = insn & 0xff;
        rz = (insn >> 8) & 0x7;
        gen_imm8_arith16(ctx, op, rz, imm);
        break;
    case 0xe:
    case 0xf:
        imm = insn & 0x1f;
        sop = (insn >> 5) & 0x7;
        rx = (insn >> 8) & 0x7;
        gen_imm5_arith16(ctx, op, sop, rx, imm);
        break;
    case 0x10 ... 0x15:
        imm = insn & 0x1f;
        rz = (insn >> 5) & 0x7;
        rx = (insn >> 8) & 0x7;
        gen_reg1_arith16(ctx, op, rz, rx, imm);
        break;
    case 0x16:
    case 0x17:
        sop = insn & 0x3;
        imm = (insn >> 2) & 0x7;
        ry = (insn >> 2) & 0x7;
        rz = (insn >> 5) & 0x7;
        rx = (insn >> 8) & 0x7;
        gen_reg3_arith16(ctx, op, sop, rz, rx, ry, imm);
        break;
    case 0x18 ... 0x1f:
        sop = insn & 0x3;
        rx = (insn >> 2) & 0xf;
        rz = (insn >> 6) & 0xf;
        gen_reg2_arith16(ctx, op, sop, rx, rz);
        break;
    case 0x20 ... 0x25:
        imm = insn & 0x1f;
        rz = (insn >> 5) & 0x7;
        rx = (insn >> 8) & 0x7;
        gen_ldst16(ctx, op, rz, rx, imm);
        break;
    case 0x26:
    case 0x27:
        /*ld16.w(sp)*/
        t0 = tcg_temp_new();
        imm = ((insn & 0x700) >> 3) | (insn & 0x1f);
        rz = (insn >> 5) & 0x7;
        ldst(ld32u, sp, rz, imm << 2, 2);
        tcg_temp_free(t0);
        break;
    case 0x28 ... 0x2d:
        imm = insn & 0x1f;
        rz = (insn >> 5) & 0x7;
        rx = (insn >> 8) & 0x7;
        gen_ldst16(ctx, op, rz, rx, imm);
        break;
    case 0x2e:
    case 0x2f:
        /*st16.w(sp)*/
        t0 = tcg_temp_new();
        imm = ((insn & 0x700) >> 3) | (insn & 0x1f);
        rz = (insn >> 5) & 0x7;
        ldst(st32, sp, rz, imm << 2, 2);
        tcg_temp_free(t0);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void branch32(DisasContext *ctx, TCGCond cond, int rx, int offset)
{
    int val;
    TCGLabel *l1 = gen_new_label();
    TCGv t0 = tcg_temp_new();

    val = offset << 1;
    if (val & 0x10000) {
        val |= 0xffff0000;
    }
    val += ctx->pc;

    if (rx != -1) {
        tcg_gen_brcondi_tl(cond, cpu_R[rx], 0, l1);
    } else {
        tcg_gen_brcondi_tl(cond, cpu_c, 0, l1);
    }
    gen_goto_tb(ctx, 1, ctx->pc + 4);
    gen_set_label(l1);

    gen_goto_tb(ctx, 0, val);

    ctx->is_jmp = DISAS_TB_JUMP;

    tcg_temp_free(t0);

}

static inline void bsr32(DisasContext *ctx, int offset)
{
    int val;

    val = offset << 1;
    if (val & 0x4000000) {
        val |= 0xfc000000;
    }
    val += ctx->pc;

    gen_goto_tb(ctx, 0, val);

    ctx->is_jmp = DISAS_TB_JUMP;
}

static inline void sce(DisasContext *ctx, int cond)
{
    TCGv t0 = tcg_temp_local_new();
    TCGLabel *l1 = gen_new_label();

    tcg_gen_movi_tl(t0, cond);
    tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
    tcg_gen_not_tl(t0, t0);
    tcg_gen_andi_tl(t0, t0, 0xf);
    gen_set_label(l1);
    tcg_gen_ori_tl(t0, t0, 0x10);
    store_cpu_field(t0, sce_condexec_bits);

    gen_save_pc(ctx->pc + 4);
    ctx->is_jmp = DISAS_UPDATE;
}

#ifndef CONFIG_USER_ONLY
static inline void gen_mfcr_cpu(DisasContext *ctx, uint32_t rz, uint32_t cr_num)
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
        gen_helper_mfcr_cpidr(cpu_R[rz], cpu_env);
        break;
    case 0xe:
        /* dcsr */
        t0 = load_cpu_field(cp0.dcsr);
        tcg_gen_mov_tl(cpu_R[rz], t0);

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
        if (ctx->features & ABIV2_TEE) {
            gen_helper_tee_mfcr_cr19(cpu_R[rz], cpu_env);
        } else {
            t0 = load_cpu_field(cp0.capr);
            tcg_gen_mov_tl(cpu_R[rz], t0);
            tcg_temp_free(t0);
        }
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

static inline void gen_mfcr_tee(DisasContext *ctx, uint32_t rz, uint32_t cr_num)
{
    TCGv t0;
    switch (cr_num) {
    case 0x0:
        /* NT_PSR */
        t0 = load_cpu_field(tee.nt_psr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        tcg_temp_free(t0);
        break;
    case 0x1:
        /* NT_VBR */
        t0 = load_cpu_field(tee.nt_vbr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        tcg_temp_free(t0);
        break;
    case 0x2:
        /* NT_EPSR */
        t0 = load_cpu_field(tee.nt_epsr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        tcg_temp_free(t0);
        break;
    case 0x4:
        /* NT_EPC */
        t0 = load_cpu_field(tee.nt_epc);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        tcg_temp_free(t0);
        break;
    case 0x6:
        /* NT_SSP */
        t0 = load_cpu_field(stackpoint.nt_ssp);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        tcg_temp_free(t0);
        break;
    case 0x7:
        /* T_USP */
        t0 = load_cpu_field(stackpoint.t_usp);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        tcg_temp_free(t0);
        break;
    case 0x8:
        /* T_DCR */
        t0 = load_cpu_field(tee.t_dcr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        tcg_temp_free(t0);
        break;
    case 0x9:
        /* T_PCR */
        t0 = load_cpu_field(tee.t_pcr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        tcg_temp_free(t0);
        break;
    case 0xa:
        /* NT_EBR */
        t0 = load_cpu_field(tee.nt_ebr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        tcg_temp_free(t0);
        break;

    default:
        break;
    }
}

/* Read MMU Coprocessor Contronl Registers */
static inline void
gen_mfcr_mmu(DisasContext *ctx, uint32_t rz, uint32_t cr_num)
{
    TCGv t0;

    switch (cr_num) {
    case 0x0:
        /* MIR */
        t0 = load_cpu_field(mmu.mir);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x1:
        /* MRR */
        t0 = load_cpu_field(mmu.mrr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x2:
        /* MEL0 */
        t0 = load_cpu_field(mmu.mel0);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x3:
        /* MEL1 */
        t0 = load_cpu_field(mmu.mel1);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x4:
        /* MEH */
        t0 = load_cpu_field(mmu.meh);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x5:
        /* MCR */
        t0 = load_cpu_field(mmu.mcr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x6:
        /* MPR */
        t0 = load_cpu_field(mmu.mpr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x7:
        /* MWR */
        t0 = load_cpu_field(mmu.mwr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x8:
        /* MCIR */
        t0 = load_cpu_field(mmu.mcir);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x9:
        /* CP15_CR9 */
        t0 = load_cpu_field(mmu.cr9);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0xa:
        /* CP15_CR10 */
        t0 = load_cpu_field(mmu.cr10);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0xb:
        /* CP15_CR11 */
        t0 = load_cpu_field(mmu.cr11);
        tcg_gen_mov_tl(cpu_R[rz], t0);
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
        t0 = load_cpu_field(mmu.mpar);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x1e:
        t0 = load_cpu_field(mmu.msa0);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    case 0x1f:
        t0 = load_cpu_field(mmu.msa1);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        break;
    default:
        t0 = tcg_temp_new();
        break;
    }
    tcg_temp_free(t0);
}

static inline void gen_mtcr_cpu(DisasContext *ctx, uint32_t cr_num, uint32_t rx)
{

    switch (cr_num) {
    case 0x0:
        /* psr */
        gen_helper_mtcr_cr0(cpu_env, cpu_R[rx]);
        gen_save_pc(ctx->pc + 4);
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
        store_cpu_field(cpu_R[rx], cp0.dcsr);
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
        gen_save_pc(ctx->pc + 4);
        ctx->is_jmp = DISAS_UPDATE;
        break;
    case 0x13:
        /* capr */
        if (ctx->features & ABIV2_TEE) {
            gen_helper_tee_mtcr_cr19(cpu_env, cpu_R[rx]);
        } else {
            store_cpu_field(cpu_R[rx], cp0.capr);
        }
        break;
    case 0x14:
        /* pacr */
        gen_helper_mtcr_cr20(cpu_env, cpu_R[rx]);
        gen_save_pc(ctx->pc + 4);
        ctx->is_jmp = DISAS_UPDATE;
        break;
    case 0x15:
        /* prsr */
        store_cpu_field(cpu_R[rx], cp0.prsr);
        break;
    default:
        break;
    }
}

static inline void gen_mtcr_tee(DisasContext *ctx, uint32_t cr_num, uint32_t rx)
{
    TCGv t0 = tcg_temp_new();
    switch (cr_num) {
    case 0x0:
        /* NT_PSR */
        if (!(ctx->features & ABIV2_JAVA)) {
            tcg_gen_andi_tl(t0, cpu_R[rx], ~0x400);
            store_cpu_field(t0, tee.nt_psr);
        } else {
            store_cpu_field(cpu_R[rx], tee.nt_psr);
        }
        break;
    case 0x1:
        /* NT_VBR */
        tcg_gen_andi_tl(t0, cpu_R[rx], ~0x3ff);
        store_cpu_field(t0, tee.nt_vbr);
        break;
    case 0x2:
        /* NT_EPSR */
        store_cpu_field(cpu_R[rx], tee.nt_epsr);
        break;
    case 0x4:
        /* NT_EPC */
        store_cpu_field(cpu_R[rx], tee.nt_epc);
        break;
    case 0x6:
        /* NT_SSP */
        store_cpu_field(cpu_R[rx], stackpoint.nt_ssp);
        break;
    case 0x7:
        /* T_USP */
        store_cpu_field(cpu_R[rx], stackpoint.t_usp);
        break;
    case 0x8:
        /* T_DCR */
        tcg_gen_andi_tl(t0, cpu_R[rx], 0x3);
        store_cpu_field(t0, tee.t_dcr);
        break;
    case 0x9:
        /* T_PCR */
        tcg_gen_andi_tl(t0, cpu_R[rx], 0x1);
        store_cpu_field(t0, tee.t_pcr);
        break;
    case 0xa:
        /* NT_EBR */
        store_cpu_field(cpu_R[rx], tee.nt_ebr);
        break;

    default:
        break;
    }
    tcg_temp_free(t0);
}

static inline void gen_mtcr_mmu(DisasContext *ctx, uint32_t cr_num, uint32_t rx)
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
        gen_save_pc(ctx->pc + 4);
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
        gen_save_pc(ctx->pc + 4);
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
        store_cpu_field(cpu_R[rx], mmu.mpar);
        gen_save_pc(ctx->pc + 4);
        ctx->is_jmp = DISAS_UPDATE;
        break;
    case 0x1e:
        store_cpu_field(cpu_R[rx], mmu.msa0);
        break;
    case 0x1f:
        store_cpu_field(cpu_R[rx], mmu.msa1);
        break;
    default:
        break;
    }
}
#endif /* !CONFIG_USER_ONLY */


static inline void gen_mfcr_vfp(DisasContext *ctx,  int rz, int rx)
{
    TCGv t0;

    switch (rx) {
    case 0x0:
        /* fid*/
        t0 = load_cpu_field(vfp.fid);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        tcg_temp_free(t0);
        break;
    case 0x1:
        /* fcr*/
        t0 = load_cpu_field(vfp.fcr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        tcg_temp_free(t0);
        break;
    case 0x2:
        /* fesr*/
        t0 = load_cpu_field(vfp.fesr);
        tcg_gen_mov_tl(cpu_R[rz], t0);
        tcg_temp_free(t0);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "wrong mfcr vfp control register: pc=%x rx=%d\n",
                      ctx->pc, rx);
        break;
    }
}


static inline void gen_mtcr_vfp(DisasContext *ctx,  int rz, int rx)
{
    switch (rz) {
    case 0x1:
        /* store_cpu_field(cpu_R[rx], vfp.fcr); */
        store_cpu_field(cpu_R[rx], vfp.fcr);
        gen_helper_vfp_update_fcr(cpu_env);
        gen_save_pc(ctx->pc + 4);
        ctx->is_jmp = DISAS_UPDATE;
        break;
    case 0x2:
        store_cpu_field(cpu_R[rx], vfp.fesr);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "wrong mfcr vfp control register: pc=%x rz=%d\n",
                      ctx->pc, rz);
        break;
    }
}

static inline void add_ix(int rz, int rx, int ry, int imm)
{
    TCGv t0 = tcg_temp_new();

    tcg_gen_shli_tl(t0, cpu_R[ry], imm);
    tcg_gen_add_tl(cpu_R[rz], cpu_R[rx], t0);

    tcg_temp_free(t0);
}

static inline void lslc(int rz, int rx, int imm)
{
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();
    TCGv t2 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();

    t1 = tcg_const_tl(imm);
    tcg_gen_mov_tl(t0, cpu_R[rx]);
    tcg_gen_andi_tl(cpu_c, t0, 0x1);
    tcg_gen_movi_tl(cpu_R[rz], 0);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 32, l1);
    tcg_gen_shl_tl(cpu_R[rz], t0, t1);
    tcg_gen_rotl_tl(t2, t0, t1);
    tcg_gen_andi_tl(cpu_c, t2, 0x1);
    gen_set_label(l1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
}

static inline void lsrc(int rz, int rx, int imm)
{
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();
    TCGv t2 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();

    t1 = tcg_const_tl(imm);
    tcg_gen_mov_tl(t0, cpu_R[rx]);
    tcg_gen_andi_tl(cpu_c, t0, 0x80000000);
    tcg_gen_shri_tl(cpu_c, cpu_c, 31);
    tcg_gen_movi_tl(cpu_R[rz], 0);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 32, l1);
    tcg_gen_shr_tl(cpu_R[rz], t0, t1);
    tcg_gen_shri_tl(t2, t0, imm - 1);
    tcg_gen_andi_tl(cpu_c, t2, 0x1);
    gen_set_label(l1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
}

static inline void asrc(int rz, int rx, int imm)
{
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();
    TCGv t2 = tcg_temp_new();
    TCGLabel *l1 = gen_new_label();

    t1 = tcg_const_tl(imm);
    tcg_gen_mov_tl(t0, cpu_R[rx]);
    tcg_gen_sari_tl(cpu_R[rz], t0, 31);
    tcg_gen_andi_tl(cpu_c, cpu_R[rz], 0x1);
    tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 32, l1);
    tcg_gen_sar_tl(cpu_R[rz], t0, t1);
    tcg_gen_shri_tl(t2, t0, imm - 1);
    tcg_gen_andi_tl(cpu_c, t2, 0x1);
    gen_set_label(l1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
}

static inline void divu(DisasContext *ctx, int rz, int rx, int ry)
{
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_R[ry], 0, l1);
    tcg_gen_divu_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
    tcg_gen_br(l2);
    gen_set_label(l1);

    TCGv t0 = tcg_temp_new();

    t0 = tcg_const_tl(EXCP_CSKY_DIV);
    gen_save_pc(ctx->pc);
    gen_helper_exception(cpu_env, t0);
    ctx->is_jmp = DISAS_NEXT;

    tcg_temp_free(t0);

    gen_set_label(l2);
}

static inline void divs(DisasContext *ctx, int rz, int rx, int ry)
{
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_R[ry], 0, l1);
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_div_i64(t0, t0, t1);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_gen_br(l2);
    gen_set_label(l1);

    TCGv t2 = tcg_temp_new();

    t2 = tcg_const_tl(EXCP_CSKY_DIV);
    gen_save_pc(ctx->pc);
    gen_helper_exception(cpu_env, t2);
    ctx->is_jmp = DISAS_NEXT;

    tcg_temp_free(t2);

    gen_set_label(l2);
}

static inline int csky_log2(uint32_t s)
{
    int i = 0;
    if (s == 0) {
        return -1;
    }

    while (s != 1) {
        s >>= 1;
        i++;
    }
    return i;
}
#define ldbistbi(name, rx, rz, imm)                             \
do {                                                            \
    tcg_gen_qemu_##name(cpu_R[rz], cpu_R[rx], ctx->mem_idx);    \
    tcg_gen_addi_tl(cpu_R[rx], cpu_R[rx], imm);                 \
} while (0)

#define ldbirstbir(name, rx, rz, ry)                            \
do {                                                            \
    tcg_gen_mov_tl(t0, cpu_R[ry]);                              \
    tcg_gen_qemu_##name(cpu_R[rz], cpu_R[rx], ctx->mem_idx);    \
    tcg_gen_add_tl(cpu_R[rx], cpu_R[rx], t0);                   \
} while (0)

static inline void dspv2_insn_pldbi_d(DisasContext *s, int rz, int rx)
{
    /* Rz[31:0] <- mem(Rx)
     * Rz+1[31:0] <- mem(Rx + 4)
     * Rx[31:0] <- Rx[31:0] + 8 */
    tcg_gen_qemu_ld32u(cpu_R[rz], cpu_R[rx], s->mem_idx);
    tcg_gen_addi_i32(cpu_R[rx], cpu_R[rx], 4);
    tcg_gen_qemu_ld32u(cpu_R[(rz + 1) % 32], cpu_R[rx], s->mem_idx);
    tcg_gen_addi_i32(cpu_R[rx], cpu_R[rx], 4);
}

static inline void dspv2_insn_pldbir_d(DisasContext *s, int rz, int rx, int ry)
{
    /* Rz[31:0] <- mem(Rx)
     * Rz+1[31:0] <- mem(Rx + Ry)
     * Rx[31:0] <- Rx[31:0] + 2*Ry */
    TCGv_i32 t0 = tcg_temp_new_i32();
    tcg_gen_mov_i32(t0, cpu_R[ry]);
    tcg_gen_qemu_ld32u(cpu_R[rz], cpu_R[rx], s->mem_idx);
    tcg_gen_add_i32(cpu_R[rx], cpu_R[rx], t0);
    tcg_gen_qemu_ld32u(cpu_R[(rz + 1) % 32], cpu_R[rx], s->mem_idx);
    tcg_gen_add_i32(cpu_R[rx], cpu_R[rx], t0);
    tcg_temp_free_i32(t0);
}

static void ldr(DisasContext *ctx, uint32_t sop,
                 uint32_t pcode, int rz, int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    int imm = 0;
    if (sop < 32) {
        /* basic ld instructions. */
        imm = csky_log2(pcode);
        if (imm == -1) {
            generate_exception(ctx, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        check_insn(ctx, ABIV2_EDSP);
        if (pcode != 0) {
            generate_exception(ctx, EXCP_CSKY_UDEF);
            return;
        }
    }

    switch (sop) {
    case 0x0:
        /* ldr.b */
        check_insn_except(ctx, CPU_801 | CPU_802);
        ldrstr(ld8u, rx, ry, rz, imm);
        break;
    case 0x1:
        /* ldr.h */
        check_insn_except(ctx, CPU_801 | CPU_802);
        ldrstr(ld16u, rx, ry, rz, imm);
        break;
    case 0x2:
        /* ldr.w */
        check_insn_except(ctx, CPU_801 | CPU_802);
        ldrstr(ld32u, rx, ry, rz, imm);
        break;
    case 0x4:
        /* ldr.bs */
        check_insn_except(ctx, CPU_801 | CPU_802);
        ldrstr(ld8s, rx, ry, rz, imm);
        break;
    case 0x5:
        /* ldr.hs */
        check_insn_except(ctx, CPU_801 | CPU_802);
        ldrstr(ld16s, rx, ry, rz, imm);
        break;
    case 0x7:
        /* ldm or ldq */
        check_insn_except(ctx, CPU_801);
        if (ctx->bctm) {
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[rx], 0, l1);
            tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);
            tcg_gen_subi_tl(t0, cpu_R[SVBR], 4);
            store_cpu_field(t0, pc);
            tcg_gen_exit_tb(0);
            gen_set_label(l1);
            tcg_gen_mov_tl(t0, cpu_R[rx]);
            for (imm = 0; imm <= rz; imm++) {
                tcg_gen_qemu_ld32u(cpu_R[ry + imm], t0, ctx->mem_idx);
                tcg_gen_addi_tl(t0, t0, 4);
            }
            gen_goto_tb(ctx, 1, ctx->pc + 4);
            ctx->is_jmp = DISAS_TB_JUMP;
            break;
        } else {
            tcg_gen_mov_tl(t0, cpu_R[rx]);
            for (imm = 0; imm <= rz; imm++) {
                tcg_gen_qemu_ld32u(cpu_R[ry + imm], t0, ctx->mem_idx);
                tcg_gen_addi_tl(t0, t0, 4);
            }
            break;
        }
    case OP_LDBI_B:
        ldbistbi(ld8u, rx, rz, 1);
        break;
    case OP_LDBI_H:
        ldbistbi(ld16u, rx, rz, 2);
        break;
    case OP_LDBI_W:
        ldbistbi(ld32u, rx, rz, 4);
        break;
    case OP_PLDBI_D:
        dspv2_insn_pldbi_d(ctx, rz, rx);
        break;
    case OP_LDBI_BS:
        ldbistbi(ld8s, rx, rz, 1);
        break;
    case OP_LDBI_HS:
        ldbistbi(ld16s, rx, rz, 2);
        break;
    case OP_LDBIR_B:
        ldbirstbir(ld8u, rx, rz, ry);
        break;
    case OP_LDBIR_H:
        ldbirstbir(ld16u, rx, rz, ry);
        break;
    case OP_LDBIR_W:
        ldbirstbir(ld32u, rx, rz, ry);
        break;
    case OP_PLDBIR_D:
        dspv2_insn_pldbir_d(ctx, rz, rx, ry);
        break;
    case OP_LDBIR_BS:
        ldbirstbir(ld8s, rx, rz, ry);
        break;
    case OP_LDBIR_HS:
        ldbirstbir(ld16s, rx, rz, ry);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
    tcg_temp_free(t0);
}

static void str(DisasContext *ctx, uint32_t sop,
                 uint32_t pcode, int rz, int rx, int ry)
{
    TCGv t0 = tcg_temp_new();
    int imm = 0;

    if (sop < 32) {
        imm = csky_log2(pcode);

        if (imm == -1) {
            generate_exception(ctx, EXCP_CSKY_UDEF);
            return;
        }
    } else {
        check_insn(ctx, ABIV2_EDSP);
        if (pcode != 0) {
            generate_exception(ctx, EXCP_CSKY_UDEF);
            return;
        }
    }

    switch (sop) {
    case 0x0:
        /* str.b */
        check_insn_except(ctx, CPU_801 | CPU_802);
        ldrstr(st8, rx, ry, rz, imm);
        break;
    case 0x1:
        /* str.h */
        check_insn_except(ctx, CPU_801 | CPU_802);
        ldrstr(st16, rx, ry, rz, imm);
        break;
    case 0x2:
        /* str.w */
        check_insn_except(ctx, CPU_801 | CPU_802);
        ldrstr(st32, rx, ry, rz, imm);
        break;
    case 0x7:
        /* stm or stq */
        check_insn_except(ctx, CPU_801);
        if (ctx->bctm) {
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[rx], 0, l1);
            tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);
            tcg_gen_subi_tl(t0, cpu_R[SVBR], 4);
            store_cpu_field(t0, pc);
            tcg_gen_exit_tb(0);
            gen_set_label(l1);
            tcg_gen_mov_tl(t0, cpu_R[rx]);
            for (imm = 0; imm <= rz; imm++) {
                tcg_gen_qemu_st32(cpu_R[ry + imm], t0, ctx->mem_idx);
                tcg_gen_addi_tl(t0, t0, 4);
            }
            gen_goto_tb(ctx, 1, ctx->pc + 4);
            ctx->is_jmp = DISAS_TB_JUMP;
            break;
        } else {
            tcg_gen_mov_tl(t0, cpu_R[rx]);
            for (imm = 0; imm <= rz; imm++) {
                tcg_gen_qemu_st32(cpu_R[ry + imm], t0, ctx->mem_idx);
                tcg_gen_addi_tl(t0, t0, 4);
            }
            break;
        }
    case OP_STBI_B:
        ldbistbi(st8, rx, rz, 1);
        break;
    case OP_STBI_H:
        ldbistbi(st16, rx, rz, 2);
        break;
    case OP_STBI_W:
        ldbistbi(st32, rx, rz, 4);
        break;
    case OP_STBIR_B:
        ldbirstbir(st8, rx, rz, ry);
        break;
    case OP_STBIR_H:
        ldbirstbir(st16, rx, rz, ry);
        break;
    case OP_STBIR_W:
        ldbirstbir(st32, rx, rz, ry);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void pop(DisasContext *ctx, int imm)
{
    int i;
    TCGv t0 = tcg_temp_new();
    tcg_gen_mov_tl(t0, cpu_R[sp]);

    if (imm & 0xf) {
        for (i = 0; i < (imm & 0xf); i++) {
            tcg_gen_qemu_ld32u(cpu_R[4 + i], t0, ctx->mem_idx);
            tcg_gen_addi_i32(t0, t0, 4);
        }
    }

    if (imm & 0x10) {
        tcg_gen_qemu_ld32u(cpu_R[15], t0, ctx->mem_idx);
        tcg_gen_addi_i32(t0, t0, 4);
    }

    if ((imm >> 5) & 0x7) {
        for (i = 0; i < ((imm >> 5) & 0x7); i++) {
            tcg_gen_qemu_ld32u(cpu_R[16 + i], t0, ctx->mem_idx);
            tcg_gen_addi_i32(t0, t0, 4);
        }
    }

    if (imm & 0x100) {
        tcg_gen_qemu_ld32u(cpu_R[28], t0, ctx->mem_idx);
        tcg_gen_addi_i32(t0, t0, 4);
    }
    tcg_gen_mov_tl(cpu_R[sp], t0);

    tcg_gen_andi_tl(t0, cpu_R[15], 0xfffffffe);
    store_cpu_field(t0, pc);
    ctx->is_jmp = DISAS_JUMP;
    tcg_temp_free(t0);
}

static void ldi(DisasContext *ctx, uint32_t sop,
                int rz, int rx, int imm)
{
    TCGv t0 = tcg_temp_new();

    switch (sop) {
    case 0x0:
        /* ld.b */
        ldst(ld8u, rx, rz, imm, 4);
        break;
    case 0x1:
        /* ld.h */
        ldst(ld16u, rx, rz, imm << 1, 4);
        break;
    case 0x2:
        /* ld.w */
        ldst(ld32u, rx, rz, imm << 2, 4);
        break;
    case 0x3:
        /* ld.d */
        check_insn(ctx, CPU_810 | CPU_807);
        tcg_gen_addi_tl(t0, cpu_R[rx], imm << 2);
        tcg_gen_qemu_ld32u(cpu_R[rz], t0, ctx->mem_idx);
        tcg_gen_addi_tl(t0, t0, 4);
        tcg_gen_qemu_ld32u(cpu_R[(rz + 1) % 32], t0, ctx->mem_idx);
        break;
    case 0x4:
        /* ld.bs */
        check_insn_except(ctx, CPU_801);
        ldst(ld8s, rx, rz, imm, 4);
        break;
    case 0x5:
        /* ld.hs */
        check_insn_except(ctx, CPU_801);
        ldst(ld16s, rx, rz, imm << 1, 4);
        break;
    case 0x6:
        /* pldr */    /*ignore this instruction*/
        check_insn(ctx, CPU_807 | CPU_810);
        break;
    case 0x7:
        /* ldex.w */  /*ignore this instruction*/
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }

    tcg_temp_free(t0);
}

static inline void push(DisasContext *ctx, int imm)
{
    TCGv t0 =  tcg_temp_new();
    int i;

    tcg_gen_mov_tl(t0, cpu_R[sp]);

    if (imm & 0x100) {
        tcg_gen_subi_i32(t0, t0, 4);
        tcg_gen_qemu_st32(cpu_R[28], t0, ctx->mem_idx);
    }

    if ((imm >> 5) & 0x7) {
        for (i = (imm >> 5) & 0x7; i > 0; i--) {
            tcg_gen_subi_i32(t0, t0, 4);
            tcg_gen_qemu_st32(cpu_R[15 + i], t0, ctx->mem_idx);
        }
    }

    if (imm & 0x10) {
        tcg_gen_subi_i32(t0, t0, 4);
        tcg_gen_qemu_st32(cpu_R[15], t0, ctx->mem_idx);
    }

    if (imm & 0xf) {
        for (i = (imm & 0xf); i > 0; i--) {
            tcg_gen_subi_i32(t0, t0, 4);
            tcg_gen_qemu_st32(cpu_R[3 + i], t0, ctx->mem_idx);
        }
    }
    tcg_gen_mov_tl(cpu_R[sp], t0);
    tcg_temp_free(t0);

}

static void sti(DisasContext *ctx, uint32_t sop, int rz, int rx, int imm)
{
    TCGv t0 = tcg_temp_new();

    switch (sop) {
    case 0x0:
        /* st.b */
        ldst(st8, rx, rz, imm, 4);
        break;
    case 0x1:
        /* st.h */
        ldst(st16, rx, rz, imm << 1, 4);
        break;
    case 0x2:
        /* st.w */
        ldst(st32, rx, rz, imm << 2, 4);
        break;
    case 0x3:
        /* st.d */
        check_insn(ctx, CPU_810 | CPU_807);
        tcg_gen_addi_tl(t0, cpu_R[rx], imm << 2);
        tcg_gen_qemu_st32(cpu_R[rz], t0, ctx->mem_idx);
        tcg_gen_addi_tl(t0, t0, 4);
        tcg_gen_qemu_st32(cpu_R[(rz + 1) % 32], t0, ctx->mem_idx);
        break;
    case 0x6:
        /* pldw */    /*ignore this instruction*/
        check_insn(ctx, CPU_807 | CPU_810);
        break;
    case 0x7:
        /* stex.w */    /*ignore this instruction*/
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void special(DisasContext *ctx, int rx, uint32_t sop,
                            int rz, int ry)
{
    /* ry:25-21, rx:20-16, sop:15-10, rz:4-0 */
    TCGv t0;

    switch (sop) {
    case 0x1:
        /* sync */
        break;
    case 0x4:
        /* bmset */
        check_insn(ctx, ABIV2_JAVA);
        t0 = tcg_temp_new();

        t0 = tcg_const_tl(1);
        store_cpu_field(t0, psr_bm);
        ctx->is_jmp = DISAS_UPDATE;

        tcg_temp_free(t0);
        break;
    case 0x5:
        /* bmclr */
        check_insn(ctx, ABIV2_JAVA);
        t0 = tcg_temp_new();

        t0 = tcg_const_tl(0);
        store_cpu_field(t0, psr_bm);
        ctx->is_jmp = DISAS_UPDATE;

        tcg_temp_free(t0);
        break;
    case 0x6:
        /* sce */
        check_insn(ctx, CPU_810 | CPU_803S | CPU_807);
        sce(ctx, ry & 0xf);
        break;
    case 0x7:
        /* idly */
        check_insn_except(ctx, CPU_801 | CPU_802);
#if !defined(CONFIG_USER_ONLY)
        if (ctx->trace_mode == NORMAL_MODE) {
            t0 = tcg_temp_new();
            TCGLabel *l1 = gen_new_label();

            t0 = load_cpu_field(idly4_counter);
            tcg_gen_brcondi_tl(TCG_COND_NE, t0, 0, l1);

            t0 = tcg_const_tl(4);
            store_cpu_field(t0, idly4_counter);
            tcg_gen_movi_tl(cpu_c, 0);

            gen_set_label(l1);
            ctx->is_jmp = DISAS_UPDATE;
            gen_save_pc(ctx->pc + 4);
            tcg_temp_free(t0);
        }
#endif
        break;
    case 0x8:
        /* trap 0 */
        generate_exception(ctx, EXCP_CSKY_TRAP0);
#if !defined(CONFIG_USER_ONLY)
        ctx->cannot_be_traced = 1;
#endif
        break;
    case 0x9:
        /* trap 1 */
#if !defined(CONFIG_USER_ONLY)
        generate_exception(ctx, EXCP_CSKY_TRAP1);
        ctx->cannot_be_traced = 1;
#endif
        break;
    case 0xa:
        /* trap 2 */
        generate_exception(ctx, EXCP_CSKY_TRAP2);
#if !defined(CONFIG_USER_ONLY)
        ctx->cannot_be_traced = 1;
#endif
        break;
    case 0xb:
        /* trap 3 */
        generate_exception(ctx, EXCP_CSKY_TRAP3);
#if !defined(CONFIG_USER_ONLY)
        ctx->cannot_be_traced = 1;
#endif
        break;
    case 0xf:
        /* wsc */
        check_insn(ctx, ABIV2_TEE);
#ifndef CONFIG_USER_ONLY
        t0 = tcg_temp_new();
        tcg_gen_movi_tl(t0, ctx->pc);
        store_cpu_field(t0, pc);
        t0 = tcg_const_tl(0);
        store_cpu_field(t0, idly4_counter);
        tcg_temp_free(t0);
        gen_helper_wsc(cpu_env);
        ctx->is_jmp = DISAS_UPDATE;
        ctx->cannot_be_traced = 1;
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x15:
        /* we */
        break;
    case 0x16:
        /* se */
        break;
    case 0x10:
        /* rte */
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            t0 = tcg_temp_new();

            t0 = tcg_const_tl(0);
            store_cpu_field(t0, idly4_counter);
            tcg_temp_free(t0);

            gen_helper_rte(cpu_env);
            ctx->is_jmp = DISAS_UPDATE;
            ctx->cannot_be_traced = 1;
        }
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x11:
        /* rfi */
        check_insn_except(ctx, CPU_801 | CPU_802);
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            t0 = tcg_temp_new();

            t0 = tcg_const_tl(0);
            store_cpu_field(t0, idly4_counter);
            tcg_temp_free(t0);

            gen_helper_rfi(cpu_env);
            ctx->is_jmp = DISAS_UPDATE;
            ctx->cannot_be_traced = 1;
        }
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x12:
        /* stop */
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            t0 = tcg_temp_new();

            t0 = tcg_const_tl(0);
            store_cpu_field(t0, idly4_counter);
            tcg_temp_free(t0);

            gen_save_pc(ctx->pc + 4);
            gen_helper_stop(cpu_env);
            ctx->is_jmp = DISAS_UPDATE;
            ctx->cannot_be_traced = 1;
        }
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x13:
        /* wait */
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            t0 = tcg_temp_new();

            t0 = tcg_const_tl(0);
            store_cpu_field(t0, idly4_counter);
            tcg_temp_free(t0);

            gen_save_pc(ctx->pc + 4);
            gen_helper_wait(cpu_env);
            ctx->is_jmp = DISAS_UPDATE;
            ctx->cannot_be_traced = 1;
        }
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x14:
        /* doze */
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            t0 = tcg_temp_new();

            t0 = tcg_const_tl(0);
            store_cpu_field(t0, idly4_counter);
            tcg_temp_free(t0);

            gen_save_pc(ctx->pc + 4);
            gen_helper_doze(cpu_env);
            ctx->is_jmp = DISAS_UPDATE;
            ctx->cannot_be_traced = 1;
        }
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x18:
        /* mfcr */
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        }

        if (ry == 0) { /* sel */
            gen_mfcr_cpu(ctx, rz, rx);
        } else if (ry == 2) {
            gen_mfcr_vfp(ctx, rz, rx);
        } else if (ry == 3) {  /* tee */
            check_insn(ctx, ABIV2_TEE);
            if (IS_TRUST(ctx)) {
                gen_mfcr_tee(ctx, rz, rx);
            } else {
                generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
            }
        } else if (ry == 15) {
            check_insn(ctx, CSKY_MMU);
            gen_mfcr_mmu(ctx, rz, rx);
        } else if ((ry == 1) && (rx == 14)) {
            gen_helper_mfcr_cr14(cpu_R[rz], cpu_env);
        } else if ((ry == 1) && (rx == 1)) {  /* mfcr cr<1, 1> */
            check_insn(ctx, ABIV2_TEE);
            TCGv t0;
            if (IS_TRUST(ctx)) {
                t0 = load_cpu_field(tee.t_ebr);
            } else {
                t0 = load_cpu_field(tee.nt_ebr);
            }
            tcg_gen_mov_tl(cpu_R[rz], t0);
            tcg_temp_free(t0);
        }
        break;
#else
        if (ry == 2) {
            gen_mfcr_vfp(ctx, rz, rx);
        } else {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        }
        break;
#endif
    case 0x19:
        /* mtcr */
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            if (ry == 0) {  /* sel */
                gen_mtcr_cpu(ctx, rz, rx);
            } else if (ry == 2) {
                gen_mtcr_vfp(ctx, rz, rx);
            } else if (ry == 3) {  /* tee */
                check_insn(ctx, ABIV2_TEE);
                if (IS_TRUST(ctx)) {
                    gen_mtcr_tee(ctx, rz, rx);
                } else {
                    generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                }
            } else if (ry == 15) {
                check_insn(ctx, CSKY_MMU);
                gen_mtcr_mmu(ctx, rz, rx);
            } else if ((ry == 1) && (rz == 14)) { /* mtcr cr<14, 1> */
                gen_helper_mtcr_cr14(cpu_env, cpu_R[rx]);
            } else if ((ry == 1) && (rz == 1)) {  /* mtcr cr<1, 1> */
                check_insn(ctx, ABIV2_TEE);
                TCGv t0 = tcg_temp_new();
                tcg_gen_andi_tl(t0, cpu_R[rx], ~0x3);
                if (IS_TRUST(ctx)) {
                    store_cpu_field(t0, tee.t_ebr);
                } else {
                    store_cpu_field(t0, tee.nt_ebr);
                }
                tcg_temp_free(t0);
            }
            break;
        }
#else
        if (ry == 2) {
            gen_mtcr_vfp(ctx, rz, rx);
        } else {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        }
        break;
#endif
    case 0x1c:
        /* psrclr */
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            t0 = tcg_temp_new();

            t0 = tcg_const_tl(ry);
            gen_helper_psrclr(cpu_env, t0);
            tcg_temp_free(t0);

            gen_save_pc(ctx->pc + 4);
            ctx->is_jmp = DISAS_UPDATE;
        }
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    case 0x1d:
        /* psrset */
#ifndef CONFIG_USER_ONLY
        if (!IS_SUPER(ctx)) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
        } else {
            t0 = tcg_temp_new();

            t0 = tcg_const_tl(ry);
            gen_helper_psrset(cpu_env, t0);
            tcg_temp_free(t0);

            gen_save_pc(ctx->pc + 4);
            ctx->is_jmp = DISAS_UPDATE;
        }
#else
        generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
#endif
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void arth_reg32(DisasContext *ctx,  int ry, int rx, uint32_t sop,
                              uint32_t pcode, int rz)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    int lsb;
    int msb;

    switch (sop) {
    case 0x0:
        if (pcode == 0x1) {
            /* addu */
            check_insn_except(ctx, CPU_801);
            tcg_gen_add_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x2) {
            /* addc */
            check_insn_except(ctx, CPU_801);
            addc(rz, rx, ry);
        } else if (pcode == 0x4) {
            /* subu or rsub */
            check_insn_except(ctx, CPU_801);
            tcg_gen_sub_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x8) {
            /* subc */
            check_insn_except(ctx, CPU_801);
            subc(rz, rx, ry);
        } else if (pcode == 0x10) {
            /* abs */
            check_insn_except(ctx, CPU_801 | CPU_802);
            TCGLabel *l1 = gen_new_label();
            tcg_gen_mov_tl(cpu_R[rz], cpu_R[rx]);
            tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_R[rx], 0x80000000, l1);
            tcg_gen_brcondi_tl(TCG_COND_GE, cpu_R[rx], 0, l1);
            tcg_gen_neg_tl(cpu_R[rz], cpu_R[rx]);
            gen_set_label(l1);
        } else {
            goto illegal_op;
        }
        break;
    case 0x1:
        if (pcode == 0x1) {
            /* cmphs */
            check_insn_except(ctx, CPU_801 | CPU_802);
            tcg_gen_setcond_tl(TCG_COND_GEU, cpu_c, cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x2) {
            /* cmplt */
            check_insn_except(ctx, CPU_801 | CPU_802);
            tcg_gen_setcond_tl(TCG_COND_LT, cpu_c, cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x4) {
            /* cmpne */
            check_insn_except(ctx, CPU_801 | CPU_802);
            tcg_gen_setcond_tl(TCG_COND_NE, cpu_c, cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x8) {
            /* mvc */
            check_insn_except(ctx, CPU_801);
            tcg_gen_mov_tl(cpu_R[rz], cpu_c);
        } else if (pcode == 0x10) {
            /* mvcv */
            check_insn_except(ctx, CPU_801 | CPU_802);
            tcg_gen_subfi_tl(cpu_R[rz], 1, cpu_c);
        } else {
            goto illegal_op;
        }
        break;
    case 0x2:
        if (pcode == 0x1) {
            /* ixh */
            check_insn_except(ctx, CPU_801);
            add_ix(rz, rx, ry, 1);
        } else if (pcode == 0x2) {
            /* ixw */
            check_insn_except(ctx, CPU_801);
            add_ix(rz, rx, ry, 2);
        } else if (pcode == 0x4) {
            /* ixd */
            check_insn_except(ctx, CPU_801 | CPU_802);
            add_ix(rz, rx, ry, 3);
        } else {
            goto illegal_op;
        }
        break;
    case 0x3:
        check_insn_except(ctx, CPU_801);
        if (pcode == 0x1) {
            /* incf */
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
            tcg_gen_addi_tl(cpu_R[ry], cpu_R[rx], rz);
            gen_set_label(l1);
        } else if (pcode == 0x2) {
            /* inct */
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_c, 0, l1);
            tcg_gen_addi_tl(cpu_R[ry], cpu_R[rx], rz);
            gen_set_label(l1);
        } else if (pcode == 0x4) {
            /* decf */
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
            tcg_gen_subi_tl(cpu_R[ry], cpu_R[rx], rz);
            gen_set_label(l1);
        } else if (pcode == 0x8) {
            /* dect */
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_c, 0, l1);
            tcg_gen_subi_tl(cpu_R[ry], cpu_R[rx], rz);
            gen_set_label(l1);
        } else {
            goto illegal_op;
        }
        break;
    case 0x4:
        if (pcode == 0x1) {
            /* decgt */
            check_insn_except(ctx, CPU_801 | CPU_802);
            tcg_gen_subi_tl(cpu_R[rz], cpu_R[rx], ry);
            tcg_gen_setcondi_tl(TCG_COND_GT, cpu_c, cpu_R[rz], 0);
        } else if (pcode == 0x2) {
            /* declt */
            check_insn_except(ctx, CPU_801 | CPU_802);
            tcg_gen_subi_tl(cpu_R[rz], cpu_R[rx], ry);
            tcg_gen_setcondi_tl(TCG_COND_LT, cpu_c, cpu_R[rz], 0);
        } else if (pcode == 0x4) {
            /* decne */
            check_insn_except(ctx, CPU_801 | CPU_802);
            tcg_gen_subi_tl(cpu_R[rz], cpu_R[rx], ry);
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rz], 0);
        } else {
            goto illegal_op;
        }
        break;
    case 0x7:
        if (pcode == 1) {
            /* cmpix */
            check_insn(ctx, ABIV2_JAVA);
            if (ctx->bctm) {
                TCGLabel *l1 = gen_new_label();
                tcg_gen_brcond_tl(TCG_COND_LT, cpu_R[rx], cpu_R[ry], l1);
                tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);
                tcg_gen_subi_tl(t0, cpu_R[SVBR], 8);
                store_cpu_field(t0, pc);
                ctx->is_jmp = DISAS_JUMP;
                gen_set_label(l1);
            } else {
                break;
            }
        } else {
            goto illegal_op;
        }
        break;
    case 0x8:
        if (pcode == 0x1) {
            /* and */
            check_insn_except(ctx, CPU_801);
            tcg_gen_and_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x2) {
            /* andn */
            check_insn_except(ctx, CPU_801);
            tcg_gen_andc_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x4) {
            /* tst */
            check_insn_except(ctx, CPU_801 | CPU_802);
            tcg_gen_and_tl(t0, cpu_R[rx], cpu_R[ry]);
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, t0, 0);
        } else if (pcode == 0x8) {
            /* tstnbz */
            check_insn_except(ctx, CPU_801 | CPU_802);
            tstnbz(rx);
        } else {
            goto illegal_op;
        }
        break;
    case 0x9:
        check_insn_except(ctx, CPU_801);
        if (pcode == 0x1) {
            /* or */
            tcg_gen_or_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x2) {
            /* xor */
            tcg_gen_xor_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        } else if (pcode == 0x4) {
            /* nor */
            tcg_gen_nor_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        } else {
            goto illegal_op;
        }
        break;
    case 0xa:
        check_insn_except(ctx, CPU_801);
        if (pcode == 0x1) {
            /* bclri */
            tcg_gen_andi_tl(cpu_R[rz], cpu_R[rx], ~(1 << ry));
        } else if (pcode == 0x2) {
            /* bseti */
            tcg_gen_ori_tl(cpu_R[rz], cpu_R[rx], 1 << ry);
        } else if (pcode == 0x4) {
            /* btsti */
            tcg_gen_andi_tl(cpu_c, cpu_R[rx], 1 << ry);
            tcg_gen_shri_tl(cpu_c, cpu_c, ry);
        } else {
            goto illegal_op;
        }
        break;
    case 0xb:
        if (pcode == 0x1) {
            /* clrf */
            check_insn_except(ctx, CPU_801 | CPU_802);
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_c, 0, l1);
            tcg_gen_movi_tl(cpu_R[ry], 0);
            gen_set_label(l1);
        } else if (pcode == 0x2) {
            /* clrt */
            check_insn_except(ctx, CPU_801 | CPU_802);
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_c, 0, l1);
            tcg_gen_movi_tl(cpu_R[ry], 0);
            gen_set_label(l1);
        } else {
            goto illegal_op;
        }
        break;
    case 0x10:
        check_insn_except(ctx, CPU_801);
        if (pcode == 0x1) {
            /* lsl */
            lsl(rz, rx, ry);
        } else if (pcode == 0x2) {
            /* lsr */
            lsr(rz, rx, ry);
        } else if (pcode == 0x4) {
            /* asr */
            asr(rz, rx, ry);
        } else if (pcode == 0x8) {
            /* rotl */
            rotl(rz, rx, ry);
        } else {
            goto illegal_op;
        }
        break;
    case 0x12:
        check_insn_except(ctx, CPU_801);
        if (pcode == 0x1) {
            /* lsli */
            tcg_gen_shli_tl(cpu_R[rz], cpu_R[rx], ry);
        } else if (pcode == 0x2) {
            /* lsri */
            tcg_gen_shri_tl(cpu_R[rz], cpu_R[rx], ry);
        } else if (pcode == 0x4) {
            /* asri */
            tcg_gen_sari_tl(cpu_R[rz], cpu_R[rx], ry);
        } else if (pcode == 0x8) {
            /* rotli */
            tcg_gen_rotli_tl(cpu_R[rz], cpu_R[rx], ry);
        } else {
            goto illegal_op;
        }
        break;
    case 0x13:
        check_insn_except(ctx, CPU_801);
        if (pcode == 0x1) {
            /* lslc */
            lslc(rz, rx, ry + 1);
        } else if (pcode == 0x2) {
            /* lsrc */
            lsrc(rz, rx, ry + 1);
        } else if (pcode == 0x4) {
            /* asrc */
            asrc(rz, rx, ry + 1);
        } else if (pcode == 0x8) {
            /* xsr */
            t0 = tcg_const_tl(ry + 1);
            gen_helper_xsr(cpu_R[rz], cpu_env, cpu_R[rx], t0);
        } else {
            goto illegal_op;
        }
        break;
    case 0x14:
        if (pcode == 0x1) {
            /* bmaski */
            check_insn_except(ctx, CPU_801);
            ry += 1;
            if (ry == 32) {
                tcg_gen_movi_tl(cpu_R[rz], 0xffffffff);
            } else {
                tcg_gen_movi_tl(cpu_R[rz], (1 << ry) - 1);
            }
        } else if (pcode == 0x2) {
            /* bgenr */
            check_insn_except(ctx, CPU_801 | CPU_802);
            TCGv t2 = tcg_temp_local_new();
            TCGLabel *l1 = gen_new_label();

            tcg_gen_mov_tl(t2, cpu_R[rx]);
            tcg_gen_movi_tl(cpu_R[rz], 0);
            tcg_gen_andi_tl(t1, t2, 0x20);
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
            tcg_gen_movi_tl(t1, 1);
            tcg_gen_andi_tl(t2, t2, 0x1f);
            tcg_gen_shl_tl(cpu_R[rz], t1, t2);
            gen_set_label(l1);

            tcg_temp_free(t2);
        } else {
            goto illegal_op;
        }
        break;
    case 0x15:
        /* zext or zextb or zexth */
        check_insn_except(ctx, CPU_801 | CPU_802);
        lsb = ry;
        msb = pcode;
        if (lsb == 0 && msb == 31) {
            tcg_gen_mov_tl(cpu_R[rz], cpu_R[rx]);
        } else {
            tcg_gen_movi_tl(t0, 0);
            tcg_gen_shri_tl(cpu_R[rz], cpu_R[rx], lsb);
            tcg_gen_deposit_tl(cpu_R[rz], t0, cpu_R[rz], 0, msb - lsb + 1);
        }
        break;
    case 0x16:
        /* sext or sextb or sexth */
        check_insn_except(ctx, CPU_801 | CPU_802);
        lsb = ry;
        msb = pcode;
        if (lsb == 0 && msb == 31) {
            tcg_gen_mov_tl(cpu_R[rz], cpu_R[rx]);
        } else {
            tcg_gen_shri_tl(cpu_R[rz], cpu_R[rx], lsb);
            tcg_gen_movi_tl(t0, 0);
            tcg_gen_deposit_tl(t0, t0, cpu_R[rz], 0, msb - lsb + 1);
            tcg_gen_shli_tl(t0, t0, 32 - (msb - lsb + 1));
            tcg_gen_sari_tl(cpu_R[rz], t0, 32 - (msb - lsb + 1));
        }
        break;
    case 0x17:
        /* ins */
        check_insn_except(ctx, CPU_801 | CPU_802);
        lsb = rz;
        if (pcode == 31) {
            tcg_gen_mov_tl(cpu_R[ry], cpu_R[rx]);
        } else {
            tcg_gen_deposit_tl(cpu_R[ry], cpu_R[ry], cpu_R[rx], lsb, pcode + 1);
        }
        break;
    case 0x18:
        if (pcode == 0x4) {
            /* revb */
            check_insn_except(ctx, CPU_801 | CPU_802);
            tcg_gen_bswap32_tl(cpu_R[rz], cpu_R[rx]);
        } else if (pcode == 0x8) {
            /* revh */
            check_insn_except(ctx, CPU_801 | CPU_802);
            tcg_gen_bswap32_tl(t0, cpu_R[rx]);
            tcg_gen_shri_tl(t1, t0, 16);
            tcg_gen_shli_tl(t0, t0, 16);
            tcg_gen_or_tl(cpu_R[rz], t0, t1);
        } else if (pcode == 0x10) {
            /* brev */
            check_insn_except(ctx, CPU_801 | CPU_802);
            gen_helper_brev(cpu_R[rz], cpu_R[rx]);
        } else {
            goto illegal_op;
        }
        break;
    case 0x1c:
        check_insn_except(ctx, CPU_801);
        if (pcode == 0x1) {
            /* xtrb0 */
            tcg_gen_shri_tl(cpu_R[rz], cpu_R[rx], 24);
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rz], 0);
        } else if (pcode == 0x2) {
            /* xtrb1 */
            tcg_gen_andi_tl(cpu_R[rz], cpu_R[rx], 0x00ff0000);
            tcg_gen_shri_tl(cpu_R[rz], cpu_R[rz], 16);
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rz], 0);
        } else if (pcode == 0x4) {
            /* xtrb2 */
            tcg_gen_andi_tl(cpu_R[rz], cpu_R[rx], 0x0000ff00);
            tcg_gen_shri_tl(cpu_R[rz], cpu_R[rz], 8);
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rz], 0);
        } else if (pcode == 0x8) {
            /* xtrb3 */
            tcg_gen_andi_tl(cpu_R[rz], cpu_R[rx], 0xff);
            tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rz], 0);
        } else {
            goto illegal_op;
        }
        break;
    case 0x1f:
        check_insn_except(ctx, CPU_801);
        if (pcode == 0x1) {
            /* ff0 */
            gen_helper_ff0(cpu_R[rz], cpu_R[rx]);
        } else if (pcode == 0x2) {
            /* ff1 */
            gen_helper_ff1(cpu_R[rz], cpu_R[rx]);
        } else {
            goto illegal_op;
        }
        break;
    case 0x20:
        if (pcode == 0x1) {
            /* divu */
            check_insn_except(ctx, CPU_801 | CPU_802);
            divu(ctx, rz, rx, ry);
        } else if (pcode == 0x2) {
            /* divs */
            check_insn_except(ctx, CPU_801 | CPU_802);
            divs(ctx, rz, rx, ry);
        } else {
            goto illegal_op;
        }
        break;
    case 0x21:
        check_insn_except(ctx, CPU_801);
        if (pcode == 0x1) {
            /* mult */
            tcg_gen_mul_tl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        } else {
            goto illegal_op;
        }
        break;
    case 0x22:
        if (pcode == 0x1) {
            /* mulu */
            check_insn(ctx, CPU_810 | CPU_807 | ABIV2_DSP);
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i64 t3 = tcg_temp_new_i64();

            tcg_gen_extu_tl_i64(t2, cpu_R[rx]);
            tcg_gen_extu_tl_i64(t3, cpu_R[ry]);
            tcg_gen_mul_i64(t2, t2, t3);
            tcg_temp_free_i64(t3);
            tcg_gen_trunc_i64_tl(cpu_lo, t2);
            tcg_gen_shri_i64(t2, t2, 32);
            tcg_gen_trunc_i64_tl(cpu_hi, t2);
            tcg_temp_free_i64(t2);
        } else if (pcode == 0x2) {
            /* mulua */
            check_insn(ctx, CPU_810 | CPU_807 | ABIV2_DSP);
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i64 t3 = tcg_temp_new_i64();

            tcg_gen_extu_tl_i64(t2, cpu_R[rx]);
            tcg_gen_extu_tl_i64(t3, cpu_R[ry]);
            tcg_gen_mul_i64(t3, t3, t2);
            tcg_gen_concat_tl_i64(t2, cpu_lo, cpu_hi);
            tcg_gen_add_i64(t3, t3, t2);
            tcg_temp_free_i64(t2);
            tcg_gen_trunc_i64_tl(cpu_lo, t3);
            tcg_gen_shri_i64(t3, t3, 32);
            tcg_gen_trunc_i64_tl(cpu_hi, t3);
            tcg_temp_free_i64(t3);
        } else if (pcode == 0x4) {
            /* mulus */
            check_insn(ctx, CPU_810 | CPU_807 | ABIV2_DSP);
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i64 t3 = tcg_temp_new_i64();

            tcg_gen_extu_tl_i64(t2, cpu_R[rx]);
            tcg_gen_extu_tl_i64(t3, cpu_R[ry]);
            tcg_gen_mul_i64(t3, t3, t2);
            tcg_gen_concat_tl_i64(t2, cpu_lo, cpu_hi);
            tcg_gen_sub_i64(t3, t2, t3);
            tcg_temp_free_i64(t2);
            tcg_gen_trunc_i64_tl(cpu_lo, t3);
            tcg_gen_shri_i64(t3, t3, 32);
            tcg_gen_trunc_i64_tl(cpu_hi, t3);
            tcg_temp_free_i64(t3);
        } else {
            goto illegal_op;
        }
        break;
    case 0x23:
        if (pcode == 0x1) {
            /* muls */
            check_insn(ctx, CPU_810 | CPU_807 | ABIV2_DSP);
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i64 t3 = tcg_temp_new_i64();

            tcg_gen_ext_tl_i64(t2, cpu_R[rx]);
            tcg_gen_ext_tl_i64(t3, cpu_R[ry]);
            tcg_gen_mul_i64(t2, t2, t3);
            tcg_temp_free_i64(t3);
            tcg_gen_trunc_i64_tl(cpu_lo, t2);
            tcg_gen_shri_i64(t2, t2, 32);
            tcg_gen_trunc_i64_tl(cpu_hi, t2);
            tcg_temp_free_i64(t2);
        } else if (pcode == 0x2) {
            /* mulsa */
            check_insn(ctx, CPU_810 | CPU_807 | ABIV2_DSP);
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i64 t3 = tcg_temp_new_i64();

            tcg_gen_ext_tl_i64(t2, cpu_R[rx]);
            tcg_gen_ext_tl_i64(t3, cpu_R[ry]);
            tcg_gen_mul_i64(t3, t3, t2);
            tcg_gen_concat_tl_i64(t2, cpu_lo, cpu_hi);
            tcg_gen_add_i64(t3, t3, t2);
            tcg_temp_free_i64(t2);
            tcg_gen_trunc_i64_tl(cpu_lo, t3);
            tcg_gen_shri_i64(t3, t3, 32);
            tcg_gen_trunc_i64_tl(cpu_hi, t3);
            tcg_temp_free_i64(t3);
        } else if (pcode == 0x4) {
            /* mulss */
            check_insn(ctx, CPU_810 | CPU_807 | ABIV2_DSP);
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i64 t3 = tcg_temp_new_i64();

            tcg_gen_ext_tl_i64(t2, cpu_R[rx]);
            tcg_gen_ext_tl_i64(t3, cpu_R[ry]);
            tcg_gen_mul_i64(t3, t3, t2);
            tcg_gen_concat_tl_i64(t2, cpu_lo, cpu_hi);
            tcg_gen_sub_i64(t3, t2, t3);
            tcg_temp_free_i64(t2);
            tcg_gen_trunc_i64_tl(cpu_lo, t3);
            tcg_gen_shri_i64(t3, t3, 32);
            tcg_gen_trunc_i64_tl(cpu_hi, t3);
            tcg_temp_free_i64(t3);
        } else {
            goto illegal_op;
        }
        break;
    case 0x24:
        if (pcode == 0x1) {
            /* mulsh */
            check_insn_except(ctx, CPU_801 | CPU_802);
            tcg_gen_ext16s_tl(t0, cpu_R[rx]);
            tcg_gen_ext16s_tl(t1, cpu_R[ry]);
            tcg_gen_mul_tl(cpu_R[rz], t0, t1);
        } else if (pcode == 0x2) {
            /* mulsha */
            check_insn(ctx, CPU_810 | CPU_807 | ABIV2_DSP);
            mulsha(rx, ry);
        } else if (pcode == 0x4) {
            /* mulshs */
            check_insn(ctx, CPU_810 | CPU_807 | ABIV2_DSP);
            mulshs(rx, ry);
        } else {
            goto illegal_op;
        }
        break;
    case 0x25:
        if (pcode == 0x1) {
            /* mulsw */
            check_insn(ctx, CPU_810 | CPU_807 | ABIV2_DSP);
            mulsw(rz, rx, ry);
        } else if (pcode == 0x2) {
            /* mulswa */
            check_insn(ctx, CPU_810 | CPU_807 | ABIV2_DSP);
            mulswa(rx, ry);
        } else if (pcode == 0x4) {
            /* mulsws */
            check_insn(ctx, CPU_810 | CPU_807 | ABIV2_DSP);
            mulsws(rx, ry);
        } else {
            goto illegal_op;
        }
        break;
    case 0x26:
        if (pcode == 0x10) {
            /*mvtc*/
            check_insn(ctx, CPU_807 | CPU_810 | ABIV2_DSP);
            tcg_gen_mov_tl(cpu_c, cpu_v);
        } else {
            goto illegal_op;
        }
        break;
    case 0x27:
        if (pcode == 0x1) {
            /* mfhi */
            check_insn(ctx, CPU_807 | CPU_810 | ABIV2_DSP);
            tcg_gen_mov_tl(cpu_R[rz], cpu_hi);
        } else if (pcode == 0x2) {
            /* mthi */
            check_insn(ctx, CPU_807 | CPU_810 | ABIV2_DSP);
            tcg_gen_mov_tl(cpu_hi, cpu_R[rx]);
        } else if (pcode == 0x4) {
            /* mflo */
            check_insn(ctx, CPU_807 | CPU_810 | ABIV2_DSP);
            tcg_gen_mov_tl(cpu_R[rz], cpu_lo);
        } else if (pcode == 0x8) {
            /* mtlo */
            check_insn(ctx, CPU_807 | CPU_810 | ABIV2_DSP);
            tcg_gen_mov_tl(cpu_lo, cpu_R[rx]);
        } else {
            goto illegal_op;
        }
        break;
    default:
illegal_op:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }

    tcg_temp_free(t0);
    tcg_temp_free(t1);

}

static inline void lrs(DisasContext *ctx, int rz, uint32_t sop, int imm)
{
    TCGv t0 = tcg_temp_new();
    switch (sop) {
    case 0x0: /*lrs.b*/
        check_insn_except(ctx, CPU_801 | CPU_802);
        ldst(ld8u, 28, rz, imm, 4);
        break;
    case 0x1: /*lrs.h*/
        check_insn_except(ctx, CPU_801 | CPU_802);
        ldst(ld16u, 28, rz, imm << 1, 4);
        break;
    case 0x2: /*lrs.w*/
        check_insn_except(ctx, CPU_801 | CPU_802);
        ldst(ld32u, 28, rz, imm << 2, 4);
        break;
    case 0x3: /*grs*/
        {
            check_insn_except(ctx, CPU_801 | CPU_802);
            int t1;
            t1 = imm << 1;
            if (t1 & 0x40000) {
                t1 |= 0xfffc0000;
            }
            t1 += ctx->pc;
            tcg_gen_movi_tl(cpu_R[rz], t1);
        }
        break;
    case 0x4: /*srs.b*/
        check_insn_except(ctx, CPU_801 | CPU_802);
        ldst(st8, 28, rz, imm, 4);
        break;
    case 0x5: /*srs.h*/
        check_insn_except(ctx, CPU_801 | CPU_802);
        ldst(st16, 28, rz, imm << 1, 4);
        break;
    case 0x6: /*srs.w*/
        check_insn_except(ctx, CPU_801 | CPU_802);
        ldst(st32, 28, rz, imm << 2, 4);
        break;
    case 0x7:  /* addi */
        tcg_gen_addi_tl(cpu_R[rz], cpu_R[28], imm + 1);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
    tcg_temp_free(t0);
}

static inline void imm_2op(DisasContext *ctx, int rz, int rx, uint32_t sop,
                            int imm)
{
    TCGv t0;

    check_insn_except(ctx, CPU_801);
    switch (sop) {
    case 0x0:  /* addi */
        tcg_gen_addi_tl(cpu_R[rz], cpu_R[rx], imm + 1);
        break;
    case 0x1: /* subi */
        tcg_gen_subi_tl(cpu_R[rz], cpu_R[rx], imm + 1);
        break;
    case 0x2: /* andi */
        tcg_gen_andi_tl(cpu_R[rz], cpu_R[rx], imm);
        break;
    case 0x3: /* andni */
        t0 = tcg_temp_new();
        t0 = tcg_const_tl(imm);
        tcg_gen_andc_tl(cpu_R[rz], cpu_R[rx], t0);
        tcg_temp_free(t0);
        break;
    case 0x4: /* xori */
        tcg_gen_xori_tl(cpu_R[rz], cpu_R[rx], imm);
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void imm_1op(DisasContext *ctx, uint32_t sop, int rx, int imm)
{
    target_ulong addr;
    int val = 0;
    TCGv t0;
    TCGv t1;

    switch (sop) {
    case 0x0: /* br */
        val = imm << 1;
        if (val & 0x10000) {
            val |= 0xffff0000;
        }
        val += ctx->pc;

        gen_goto_tb(ctx, 0, val);
        ctx->is_jmp = DISAS_TB_JUMP;

        break;
    case 0x2: /* bf */
        check_insn_except(ctx, CPU_801);
        branch32(ctx, TCG_COND_EQ, -1, imm);
        break;
    case 0x3: /* bt */
        check_insn_except(ctx, CPU_801);
        branch32(ctx, TCG_COND_NE, -1, imm);
        break;
    case 0x6: /* jmp */
        check_insn_except(ctx, CPU_801 | CPU_802);
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
        ctx->is_jmp = DISAS_JUMP;
        tcg_temp_free(t0);
        break;
    case 0x7:/* jsr */
        check_insn_except(ctx, CPU_801 | CPU_802);
        t0 = tcg_temp_new();
        tcg_gen_andi_tl(t0, cpu_R[rx], 0xfffffffe);
        tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);
        store_cpu_field(t0, pc);

#if !defined(CONFIG_USER_ONLY)
        if ((ctx->trace_mode == BRAN_TRACE_MODE)
            || (ctx->trace_mode == INST_TRACE_MODE)) {
            t0 = tcg_const_i32(EXCP_CSKY_TRACE);
            gen_helper_exception(cpu_env, t0);
        }
        ctx->maybe_change_flow = 1;
#endif
        ctx->is_jmp = DISAS_JUMP;
        tcg_temp_free(t0);
        break;
    case 0x8: /* bez */
        check_insn_except(ctx, CPU_801 | CPU_802);
        branch32(ctx, TCG_COND_EQ, rx, imm);
        break;
    case 0x9: /* bnez */
        check_insn_except(ctx, CPU_801 | CPU_802);
        branch32(ctx, TCG_COND_NE, rx, imm);
        break;
    case 0xa:  /* bhz */
        check_insn_except(ctx, CPU_801 | CPU_802);
        branch32(ctx, TCG_COND_GT, rx, imm);
        break;
    case 0xb: /* blsz */
        check_insn_except(ctx, CPU_801 | CPU_802);
        branch32(ctx, TCG_COND_LE, rx, imm);
        break;
    case 0xc: /* blz */
        check_insn_except(ctx, CPU_801 | CPU_802);
        branch32(ctx, TCG_COND_LT, rx, imm);
        break;
    case 0xd: /* bhsz */
        check_insn_except(ctx, CPU_801 | CPU_802);
        branch32(ctx, TCG_COND_GE, rx, imm);
        break;
    case 0xe: /* bloop */
        check_insn(ctx, ABIV2_EDSP);
        if (imm & 0x800) {
            val = imm | 0xf000;
        }
        tcg_gen_subi_tl(cpu_R[rx], cpu_R[rx], 1);
        branch32(ctx, TCG_COND_NE, rx, val);
        break;
    case 0xf: /* jmpix */
        check_insn(ctx, ABIV2_JAVA);
        if (ctx->bctm) {
            t0 = tcg_temp_new();
            t1 = tcg_temp_new();

            tcg_gen_andi_tl(t0, cpu_R[rx], 0xff);
            switch (imm & 0x3) {
            case 0x0:
                tcg_gen_shli_tl(t0, t0, 4);
                break;
            case 0x1:
                tcg_gen_shli_tl(t1, t0, 4);
                tcg_gen_shli_tl(t0, t0, 3);
                tcg_gen_add_tl(t0, t0, t1);
                break;
            case 0x2:
                tcg_gen_shli_tl(t0, t0, 5);
                break;
            case 0x3:
                tcg_gen_shli_tl(t1, t0, 5);
                tcg_gen_shli_tl(t0, t0, 3);
                tcg_gen_add_tl(t0, t0, t1);
                break;
            default:
                break;
            }
            tcg_gen_add_tl(t0, cpu_R[SVBR], t0);
            store_cpu_field(t0, pc);

            ctx->is_jmp = DISAS_JUMP;
            tcg_temp_free(t1);
            tcg_temp_free(t0);
            break;
        } else {
            generate_exception(ctx, EXCP_CSKY_UDEF);
            break;
        }
    case 0x10: /* movi */
        check_insn_except(ctx, CPU_801);
        tcg_gen_movi_tl(cpu_R[rx], imm);
        break;
    case 0x11: /* movih */
        check_insn_except(ctx, CPU_801);
        tcg_gen_movi_tl(cpu_R[rx], imm << 16);
        break;
    case 0x14:/* lrw */
        t0 = tcg_temp_new();

        addr = (ctx->pc + (imm << 2)) & 0xfffffffc ;
        tcg_gen_movi_tl(t0, addr);
        tcg_gen_qemu_ld32u(cpu_R[rx], t0, ctx->mem_idx);
        tcg_temp_free(t0);
        break;
    case 0x16: /* jmpi */
        check_insn_except(ctx, CPU_801 | CPU_802);
        t0 = tcg_temp_new();

        addr = (ctx->pc + (imm << 2)) & 0xfffffffc ;
        tcg_gen_movi_tl(t0, addr);
        tcg_gen_qemu_ld32u(t0, t0, ctx->mem_idx);
        store_cpu_field(t0, pc);

#if !defined(CONFIG_USER_ONLY)
        if ((ctx->trace_mode == BRAN_TRACE_MODE)
            || (ctx->trace_mode == INST_TRACE_MODE)) {
            t0 = tcg_const_i32(EXCP_CSKY_TRACE);
            gen_helper_exception(cpu_env, t0);
        }
        ctx->maybe_change_flow = 1;
#endif
        ctx->is_jmp = DISAS_JUMP;

        tcg_temp_free(t0);
        break;
    case 0x17: /* jsri */
        check_insn_except(ctx, CPU_801 | CPU_802);
        t0 = tcg_temp_new();

        addr =  (ctx->pc + (imm << 2)) & 0xfffffffc;
        tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);
        tcg_gen_movi_tl(t0, addr);
        tcg_gen_qemu_ld32u(t0, t0, ctx->mem_idx);
        store_cpu_field(t0, pc);

#if !defined(CONFIG_USER_ONLY)
        if ((ctx->trace_mode == BRAN_TRACE_MODE)
            || (ctx->trace_mode == INST_TRACE_MODE)) {
            t0 = tcg_const_i32(EXCP_CSKY_TRACE);
            gen_helper_exception(cpu_env, t0);
        }
        ctx->maybe_change_flow = 1;
#endif
        ctx->is_jmp = DISAS_JUMP;

        tcg_temp_free(t0);
        break;
    case 0x18: /* cmphsi */
        check_insn_except(ctx, CPU_801);
        tcg_gen_setcondi_tl(TCG_COND_GEU, cpu_c, cpu_R[rx], imm + 1);
        break;
    case 0x19:  /* cmplti */
        check_insn_except(ctx, CPU_801);
        tcg_gen_setcondi_tl(TCG_COND_LT, cpu_c, cpu_R[rx], imm + 1);
        break;
    case 0x1a: /* cmpnei */
        check_insn_except(ctx, CPU_801);
        tcg_gen_setcondi_tl(TCG_COND_NE, cpu_c, cpu_R[rx], imm);
        break;
    case 0x1e:   /* pop */
        check_insn_except(ctx, CPU_801 | CPU_802);
        if (ctx->bctm) {
            t0 = tcg_temp_new();
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[sp], 0, l1);
            tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);
            tcg_gen_subi_tl(t0, cpu_R[SVBR], 4);
            store_cpu_field(t0, pc);
            tcg_gen_exit_tb(0);
            gen_set_label(l1);
            pop(ctx, imm & 0x1ff);
            tcg_temp_free(t0);
            break;
        } else {
            pop(ctx, imm & 0x1ff);
            break;
        }
    case 0x1f:    /* push */
        check_insn_except(ctx, CPU_801 | CPU_802);
        if (ctx->bctm) {
            t0 = tcg_temp_new();
            TCGLabel *l1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, cpu_R[sp], 0, l1);
            tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);
            tcg_gen_subi_tl(t0, cpu_R[SVBR], 4);
            store_cpu_field(t0, pc);
            tcg_gen_exit_tb(0);
            gen_set_label(l1);
            push(ctx, imm & 0x1ff);
            gen_goto_tb(ctx, 1, ctx->pc + 4);
            ctx->is_jmp = DISAS_TB_JUMP;
            tcg_temp_free(t0);
            break;
        } else {
            push(ctx, imm & 0x1ff);
            break;
        }
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static inline void gen_vfp_ld(DisasContext *s, int dp, TCGv addr)
{
    if (dp) {
        /*doesn't cause alignment exception*/
#if !defined TARGET_WORDS_BIGENDIAN
        tcg_gen_qemu_ld32u(cpu_F0s, addr, s->mem_idx);
        tcg_gen_addi_i32(addr, addr, 4);
        tcg_gen_qemu_ld32u(cpu_F1s, addr, s->mem_idx);
        tcg_gen_concat_i32_i64(cpu_F0d, cpu_F0s, cpu_F1s);
#else
        tcg_gen_qemu_ld32u(cpu_F0s, addr, s->mem_idx);
        tcg_gen_addi_i32(addr, addr, 4);
        tcg_gen_qemu_ld32u(cpu_F1s, addr, s->mem_idx);
        tcg_gen_concat_i32_i64(cpu_F0d, cpu_F1s, cpu_F0s);
#endif
    } else {
        tcg_gen_qemu_ld32u(cpu_F0s, addr, s->mem_idx);
    }
}

static inline void gen_vfp_st(DisasContext *s, int dp, TCGv addr)
{
    if (dp) {
        /*doesn't cause alignment exception*/
#if !defined TARGET_WORDS_BIGENDIAN
        tcg_gen_extrl_i64_i32(cpu_F0s, cpu_F0d);
        tcg_gen_qemu_st32(cpu_F0s, addr, s->mem_idx);
        tcg_gen_shri_i64(cpu_F0d, cpu_F0d, 32);
        tcg_gen_extrl_i64_i32(cpu_F1s, cpu_F0d);
        tcg_gen_addi_i32(addr, addr, 4);
        tcg_gen_qemu_st32(cpu_F1s, addr, s->mem_idx);
#else
        tcg_gen_extrl_i64_i32(cpu_F0s, cpu_F0d);
        tcg_gen_shri_i64(cpu_F0d, cpu_F0d, 32);
        tcg_gen_extrl_i64_i32(cpu_F1s, cpu_F0d);
        tcg_gen_qemu_st32(cpu_F1s, addr, s->mem_idx);
        tcg_gen_addi_i32(addr, addr, 4);
        tcg_gen_qemu_st32(cpu_F0s, addr, s->mem_idx);
#endif
    } else {
        tcg_gen_qemu_st32(cpu_F0s, addr, s->mem_idx);
    }
}

#define vfp_reg_offset(reg) offsetof(CPUCSKYState, vfp.reg[reg])

#define tcg_gen_ld_f32 tcg_gen_ld_i32
#define tcg_gen_ld_f64 tcg_gen_ld_i64
#define tcg_gen_st_f32 tcg_gen_st_i32
#define tcg_gen_st_f64 tcg_gen_st_i64

static inline void gen_mov_F0_vreg(int dp, int reg)
{
    if (dp) {
        tcg_gen_ld_f64(cpu_F0d, cpu_env, vfp_reg_offset(reg));
    } else {
        tcg_gen_ld_f32(cpu_F0s, cpu_env, vfp_reg_offset(reg));
    }
}

static inline void gen_mov_F0_vreg_hi(int dp, int reg)
{
    if (dp) {
        tcg_gen_ld_f64(cpu_F0d, cpu_env, vfp_reg_offset(reg) + 4);
    } else {
        tcg_gen_ld_f32(cpu_F0s, cpu_env, vfp_reg_offset(reg) + 4);
    }
}

static inline void gen_mov_F1_vreg(int dp, int reg)
{
    if (dp) {
        tcg_gen_ld_f64(cpu_F1d, cpu_env, vfp_reg_offset(reg));
    } else {
        tcg_gen_ld_f32(cpu_F1s, cpu_env, vfp_reg_offset(reg));
    }
}

static inline void gen_mov_F1_vreg_hi(int dp, int reg)
{
    if (dp) {
        tcg_gen_ld_f64(cpu_F1d, cpu_env, vfp_reg_offset(reg) + 4);
    } else {
        tcg_gen_ld_f32(cpu_F1s, cpu_env, vfp_reg_offset(reg) + 4);
    }
}

static inline void gen_mov_vreg_F0(int dp, int reg)
{
    if (dp) {
        tcg_gen_st_f64(cpu_F0d, cpu_env, vfp_reg_offset(reg));
    } else {
        tcg_gen_st_f32(cpu_F0s, cpu_env, vfp_reg_offset(reg));
    }
}

static inline void gen_mov_vreg_F0_hi(int dp, int reg)
{
    if (dp) {
        tcg_gen_st_f64(cpu_F0d, cpu_env, vfp_reg_offset(reg) + 4);
    } else {
        tcg_gen_st_f32(cpu_F0s, cpu_env, vfp_reg_offset(reg) + 4);
    }
}

static inline void gen_vfp_F1_ld0(int dp)
{
    if (dp) {
        tcg_gen_movi_i64(cpu_F1d, 0);
    } else {
        tcg_gen_movi_i32(cpu_F1s, 0);
    }
}

static inline void gen_vfp_add(int dp)
{
    if (dp) {
        gen_helper_vfp_addd(cpu_F0d, cpu_F0d, cpu_F1d, cpu_env);
    } else {
        gen_helper_vfp_adds(cpu_F0s, cpu_F0s, cpu_F1s, cpu_env);
    }
}

static inline void gen_vfp_sub(int dp)
{
    if (dp) {
        gen_helper_vfp_subd(cpu_F0d, cpu_F0d, cpu_F1d, cpu_env);
    } else {
        gen_helper_vfp_subs(cpu_F0s, cpu_F0s, cpu_F1s, cpu_env);
    }
}

static inline void gen_vfp_mul(int dp)
{
    if (dp) {
        gen_helper_vfp_muld(cpu_F0d, cpu_F0d, cpu_F1d, cpu_env);
    } else {
        gen_helper_vfp_muls(cpu_F0s, cpu_F0s, cpu_F1s, cpu_env);
    }
}

static inline void gen_vfp_div(int dp)
{
    if (dp) {
        gen_helper_vfp_divd(cpu_F0d, cpu_F0d, cpu_F1d, cpu_env);
    } else {
        gen_helper_vfp_divs(cpu_F0s, cpu_F0s, cpu_F1s, cpu_env);
    }
}



static inline void gen_vfp_abs(int dp)
{
    if (dp) {
        gen_helper_vfp_absd(cpu_F0d, cpu_F0d);
    } else {
        gen_helper_vfp_abss(cpu_F0s, cpu_F0s);
    }
}

static inline void gen_vfp_neg(int dp)
{
    if (dp) {
        gen_helper_vfp_negd(cpu_F0d, cpu_F0d);
    } else {
        gen_helper_vfp_negs(cpu_F0s, cpu_F0s);
    }
}

static inline void gen_vfp_sqrt(int dp)
{
    if (dp) {
        gen_helper_vfp_sqrtd(cpu_F0d, cpu_F0d, cpu_env);
    } else {
        gen_helper_vfp_sqrts(cpu_F0s, cpu_F0s, cpu_env);
    }
}

static inline void gen_vfp_recip(int dp)
{
    if (dp) {
        gen_helper_vfp_recipd(cpu_F0d, cpu_F0d, cpu_env);
    } else {
        gen_helper_vfp_recips(cpu_F0s, cpu_F0s, cpu_env);
    }
}


static inline void gen_vfp_cmp_ge(int dp)
{
    if (dp) {
        gen_helper_vfp_cmp_ged(cpu_F0d, cpu_F1d, cpu_env);
    } else {
        gen_helper_vfp_cmp_ges(cpu_F0s, cpu_F1s, cpu_env);
    }
}

static inline void gen_vfp_cmp_l(int dp)
{
    if (dp) {
        gen_helper_vfp_cmp_ld(cpu_F0d, cpu_F1d, cpu_env);
    } else {
        gen_helper_vfp_cmp_ls(cpu_F0s, cpu_F1s, cpu_env);
    }
}

static inline void gen_vfp_cmp_ls(int dp)
{
    if (dp) {
        gen_helper_vfp_cmp_lsd(cpu_F0d, cpu_F1d, cpu_env);
    } else {
        gen_helper_vfp_cmp_lss(cpu_F0s, cpu_F1s, cpu_env);
    }
}

static inline void gen_vfp_cmp_ne(int dp)
{
    if (dp) {
        gen_helper_vfp_cmp_ned(cpu_F0d, cpu_F1d, cpu_env);
    } else {
        gen_helper_vfp_cmp_nes(cpu_F0s, cpu_F1s, cpu_env);
    }
}

static inline void gen_vfp_cmp_isNAN(int dp)
{
    if (dp) {
        gen_helper_vfp_cmp_isNANd(cpu_F0d, cpu_F1d, cpu_env);
    } else {
        gen_helper_vfp_cmp_isNANs(cpu_F0s, cpu_F1s, cpu_env);
    }
}

static inline void gen_vfp_tosirn(int dp)
{
    if (dp) {
        gen_helper_vfp_tosirnd(cpu_F0s, cpu_F0d, cpu_env);
    } else {
        gen_helper_vfp_tosirns(cpu_F0s, cpu_F0s, cpu_env);
    }
}

static inline void gen_vfp_tosirz(int dp)
{
    if (dp) {
        gen_helper_vfp_tosirzd(cpu_F0s, cpu_F0d, cpu_env);
    } else {
        gen_helper_vfp_tosirzs(cpu_F0s, cpu_F0s, cpu_env);
    }
}

static inline void gen_vfp_tosirpi(int dp)
{
    if (dp) {
        gen_helper_vfp_tosirpid(cpu_F0s, cpu_F0d, cpu_env);
    } else {
        gen_helper_vfp_tosirpis(cpu_F0s, cpu_F0s, cpu_env);
    }
}

static inline void gen_vfp_tosirni(int dp)
{
    if (dp) {
        gen_helper_vfp_tosirnid(cpu_F0s, cpu_F0d, cpu_env);
    } else {
        gen_helper_vfp_tosirnis(cpu_F0s, cpu_F0s, cpu_env);
    }
}

static inline void gen_vfp_touirn(int dp)
{
    if (dp) {
        gen_helper_vfp_touirnd(cpu_F0s, cpu_F0d, cpu_env);
    } else {
        gen_helper_vfp_touirns(cpu_F0s, cpu_F0s, cpu_env);
    }
}

static inline void gen_vfp_touirz(int dp)
{
    if (dp) {
        gen_helper_vfp_touirzd(cpu_F0s, cpu_F0d, cpu_env);
    } else {
        gen_helper_vfp_touirzs(cpu_F0s, cpu_F0s, cpu_env);
    }
}

static inline void gen_vfp_touirpi(int dp)
{
    if (dp) {
        gen_helper_vfp_touirpid(cpu_F0s, cpu_F0d, cpu_env);
    } else {
        gen_helper_vfp_touirpis(cpu_F0s, cpu_F0s, cpu_env);
    }
}

static inline void gen_vfp_touirni(int dp)
{
    if (dp) {
        gen_helper_vfp_touirnid(cpu_F0s, cpu_F0d, cpu_env);
    } else {
        gen_helper_vfp_touirnis(cpu_F0s, cpu_F0s, cpu_env);
    }
}

static inline void gen_vfp_uito(int dp)
{
    if (dp) {
        gen_helper_vfp_uitod(cpu_F0d, cpu_F0s, cpu_env);
    } else {
        gen_helper_vfp_uitos(cpu_F0s, cpu_F0s, cpu_env);
    }
}

static inline void gen_vfp_sito(int dp)
{
    if (dp) {
        gen_helper_vfp_sitod(cpu_F0d, cpu_F0s, cpu_env);
    } else {
        gen_helper_vfp_sitos(cpu_F0s, cpu_F0s, cpu_env);
    }
}

/* Move between integer and VFP cores.  */
static TCGv gen_vfp_mrs(void)
{
    TCGv tmp = new_tmp();
    tcg_gen_mov_i32(tmp, cpu_F0s);
    return tmp;
}

static void gen_vfp_msr(TCGv tmp)
{
    tcg_gen_mov_i32(cpu_F0s, tmp);
    dead_tmp(tmp);
}

static inline void fpu_insn_fmovi(int32_t insn)
{
    int vrz = insn & 0xf;
    int imm = (((insn >> 21) & 0xf) << 7) | (((insn >> 4) & 0xf) << 3) | 0x800;
    int pos = (insn >> 16) & 0xf;
    int sign = (insn >> 20) & 0x1;
    int dp = (insn >> 9) & 0x1;
    TCGv_i32 t0, t1, t2;
    t0 = tcg_const_i32(imm);
    t1 = tcg_const_i32(pos);
    t2 = tcg_const_i32(sign);
    if (dp) {
        gen_helper_vfp_fmovid(cpu_F0d, t0, t1, t2, cpu_env);
    } else {
        gen_helper_vfp_fmovis(cpu_F0s, t0, t1, t2, cpu_env);
    }
    gen_mov_vreg_F0(dp, vrz);
}

static void disas_vfp_insn(CPUCSKYState *env, DisasContext *s, uint32_t insn)
{
    int op1, op2, dp, imm, shift, i;
    int vrx, vry, vrz;
    int rx, ry, rz;

    TCGv addr;
    TCGv tmp;
    op1 = (insn >> 8) & 0xff;
    op2 = (insn >> 4) & 0xf;

    switch (op1) {
    case 0x0: /*single-alu*/
        check_insn(s, ABIV2_FLOAT_S);
        switch (op2) {
        case 0x0:/* fadds */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* fsubs */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x8:/* fmovs */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xc:/* fabss */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_abs(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xe:/* fnegs */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_neg(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case 0x1:/*single-compare*/
        check_insn(s, ABIV2_FLOAT_S);
        switch (op2) {
        case 0x0:/* fcmpzhss */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_F1_ld0(dp);
            gen_vfp_cmp_ge(dp);
            break;
        case 0x2:/* fcmpzlss  */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_F1_ld0(dp);
            gen_vfp_cmp_ls(dp);
            break;
        case 0x4:/* fcmpznes  */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_F1_ld0(dp);
            gen_vfp_cmp_ne(dp);
            break;
        case 0x6:/* fcmpzuos  */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vrx);
            gen_vfp_cmp_isNAN(dp);
            break;
        case 0x8:/* fcmphss */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_cmp_ge(dp);
            break;
        case 0xa:/* fcmplts */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_cmp_l(dp);
            break;
        case 0xc:/* fcmpnes */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_cmp_ne(dp);
            break;
        case 0xe:/* fcmpuos */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_cmp_isNAN(dp);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x2:/* single-mul */
        check_insn(s, ABIV2_FLOAT_S);
        switch (op2) {
        case 0x0:/* fmuls */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* fnmuls  */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x8:/* fmacs */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xa:/* fmscs */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xc:/* fnmacs */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xe:/* fnmscs */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x3:/* single-div */
        check_insn(s, ABIV2_FLOAT_S);
        switch (op2) {
        case 0x0:/* fdivs*/
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_div(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* frecips  */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_recip(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x4:/* fsqrts  */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_sqrt(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x8:/* double-alu */
        check_insn(s, ABIV2_FLOAT_D);
        switch (op2) {
        case 0x0:/* faddd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* fsubd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x8:/* fmovd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xc:/* fabsd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_abs(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xe:/* fnegd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_neg(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;

        }
        break;

    case 0x9:/* double-compare */
        check_insn(s, ABIV2_FLOAT_D);
        switch (op2) {
        case 0x0:/* fcmpzhsd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_F1_ld0(dp);
            gen_vfp_cmp_ge(dp);
            break;
        case 0x2:/* fcmpzlsd  */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_F1_ld0(dp);
            gen_vfp_cmp_ls(dp);
            break;
        case 0x4:/* fcmpzned  */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_F1_ld0(dp);
            gen_vfp_cmp_ne(dp);
            break;
        case 0x6:/* fcmpzuod  */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vrx);
            gen_vfp_cmp_isNAN(dp);
            break;
        case 0x8:/* fcmphsd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_cmp_ge(dp);
            break;
        case 0xa:/* fcmpltd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_cmp_l(dp);
            break;
        case 0xc:/* fcmpned */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_cmp_ne(dp);
            break;
        case 0xe:/* fcmpuod */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_cmp_isNAN(dp);
            break;
        default:
            goto wrong;
            break;

        }
        break;

    case 0xa:/* double-mul */
        check_insn(s, ABIV2_FLOAT_D);
        switch (op2) {
        case 0x0:/* fmuld */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* fnmuld  */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x8:/* fmacd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xa:/* fmscd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xc:/* fnmacd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xe:/* fnmscd */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0xb:/* double-div */
        check_insn(s, ABIV2_FLOAT_D);
        switch (op2) {
        case 0x0:/* fdivd*/
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vry = (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_div(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* frecipd  */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_recip(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x4:/* fsqrtd  */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_sqrt(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x10:/* simd-alu */
        switch (op2) {
        case 0x0:/* faddm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_mov_F1_vreg_hi(dp, vry);
            gen_vfp_add(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        case 0x2:/* fsubm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_mov_F1_vreg_hi(dp, vry);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        case 0x8:/* fmovm */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xc:/* fabsm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_abs(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_vfp_abs(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        case 0xe:/* fnegm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_neg(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_vfp_neg(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x12:/* simd-mul  --fixe case 0x11:*/
        switch (op2) {
        case 0x0:/* fmulm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_mov_F1_vreg_hi(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        case 0x2:/* fnmulm  */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_mov_F1_vreg_hi(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        case 0x8:/* fmacm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_mov_F1_vreg_hi(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_F1_vreg_hi(dp, vrz);
            gen_vfp_add(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        case 0xa:/* fmscm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_mov_F1_vreg_hi(dp, vry);
            gen_vfp_mul(dp);
            gen_mov_F1_vreg_hi(dp, vrz);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        case 0xc:/* fnmacm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_add(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_mov_F1_vreg_hi(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_F1_vreg_hi(dp, vrz);
            gen_vfp_add(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        case 0xe:/* fnmscm */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vry =  (insn >> 21) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_mov_F1_vreg(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_F1_vreg(dp, vrz);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0(dp, vrz);
            gen_mov_F0_vreg_hi(dp, vrx);
            gen_mov_F1_vreg_hi(dp, vry);
            gen_vfp_mul(dp);
            gen_vfp_neg(dp);
            gen_mov_F1_vreg_hi(dp, vrz);
            gen_vfp_sub(dp);
            gen_mov_vreg_F0_hi(dp, vrz);
            break;
        default:
            goto wrong;
            break;

        }
        break;

    case 0x18:/* for-sti */
        switch (op2) {
            /* fstosi */
        case 0x0:/* fstosi.rn */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_tosirn(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* fstosi.rz */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_tosirz(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x4:/* fstosi.rpi */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_tosirpi(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x6:/* fstosi.rni */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_tosirni(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
            /* fstoui */
        case 0x8:/* fstoui.rn */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_touirn(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xa:/* fstoui.rz */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_touirz(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xc:/* fstoui.rpi */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_touirpi(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xe:/* fstoui.rni */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_touirni(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;

        }
        break;

    case 0x19:/* for-dti */
        switch (op2) {
            /* fdtosi */
        case 0x0:/* fdtosi.rn */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_tosirn(dp);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* fdtosi.rz */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_tosirz(dp);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x4:/* fdtosi.rpi */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_tosirpi(dp);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x6:/* fdtosi.rni */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_tosirni(dp);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
            /* fdtoui */
        case 0x8:/* fdtoui.rn */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_touirn(dp);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xa:/* fdtoui.rz */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_touirz(dp);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xc:/* fdtoui.rpi */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_touirpi(dp);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xe:/* fdtoui.rni */
            dp = 1;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_touirni(dp);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x1a:/* for-misc */
        switch (op2) {
        case 0x0:/* fsitos */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_sito(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x2:/* fuitos  */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            gen_vfp_uito(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0x8:/* fsitod */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            dp = 1;
            gen_vfp_sito(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xa:/* fuitod */
            dp = 0;
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            gen_mov_F0_vreg(dp, vrx);
            dp = 1;
            gen_vfp_uito(dp);
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xc:/* fdtos */
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            dp = 1;
            gen_mov_F0_vreg(dp, vrx);
            gen_helper_vfp_tosd(cpu_F0s, cpu_F0d, cpu_env);
            dp = 0;
            gen_mov_vreg_F0(dp, vrz);
            break;
        case 0xe:/* fstod */
            vrx = (insn >> 16) & 0xf;
            vrz = insn & 0xf;
            dp = 0;
            gen_mov_F0_vreg(dp, vrx);
            gen_helper_vfp_tods(cpu_F0d, cpu_F0s, cpu_env);
            dp = 1;
            gen_mov_vreg_F0(dp, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;

    case 0x1b:/* for-fmvr */
        switch (op2) {
        case 0x0:
        case 0x1:/* fmfvrh*/
            vrx = (insn >> 16) & 0xf;
            rz = insn & 0x1f;
            gen_mov_F0_vreg_hi(0, vrx);
            tmp = gen_vfp_mrs();
            tcg_gen_mov_i32(cpu_R[rz], tmp);
            tcg_temp_free_i32(tmp);
            break;
        case 0x2:
        case 0x3:/* fmfvrl */
            vrx = (insn >> 16) & 0xf;
            rz = insn & 0x1f;
            gen_mov_F0_vreg(0, vrx);
            tmp = gen_vfp_mrs();
            tcg_gen_mov_i32(cpu_R[rz], tmp);
            tcg_temp_free_i32(tmp);
            break;
        case 0x4:/* fmtvrh */
            rx = (insn >> 16) & 0x1f;
            vrz = insn & 0xf;
            tmp = load_reg(s, rx);
            gen_vfp_msr(tmp);
            gen_mov_vreg_F0_hi(0, vrz);
            break;
        case 0x6:/* fmtvrl */
            rx = (insn >> 16) & 0x1f;
            vrz = insn & 0xf;
            tmp = load_reg(s, rx);
            gen_vfp_msr(tmp);
            gen_mov_vreg_F0(0, vrz);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    /* fmovis */
    case 0x1c:
    case 0x1e:
        fpu_insn_fmovi(insn);
        break;
     /* flsu */
    case 0x20:/* flds */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = ((insn >> 17) & 0xf0) | ((insn >> 4) & 0xf);
        addr =  load_reg(s, rx);
        tcg_gen_addi_i32(addr, addr, imm << 2);
        gen_vfp_ld(s, 0, addr);
        gen_mov_vreg_F0(0, vrz);
        dead_tmp(addr);
        break;
    case 0x21:/* fldd */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = ((insn >> 17) & 0xf0) | ((insn >> 4) & 0xf);
        addr =  load_reg(s, rx);
        tcg_gen_addi_i32(addr, addr, imm << 2);
        gen_vfp_ld(s, 1, addr);
        gen_mov_vreg_F0(1, vrz);
        dead_tmp(addr);
        break;
    case 0x22:/* fldm */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = ((insn >> 17) & 0xf0) | ((insn >> 4) & 0xf);
        addr =  load_reg(s, rx);
        tcg_gen_addi_i32(addr, addr, imm << 2);
        gen_vfp_ld(s, 1, addr);
        gen_mov_vreg_F0(1, vrz);
        dead_tmp(addr);
        break;
    case 0x24:/* fsts */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = ((insn >> 17) & 0xf0) | ((insn >> 4) & 0xf);
        addr =  load_reg(s, rx);
        tcg_gen_addi_i32(addr, addr, imm << 2);
        gen_mov_F0_vreg(0, vrz);
        gen_vfp_st(s, 0, addr);
        dead_tmp(addr);
        break;
    case 0x25:/* fstd */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = ((insn >> 17) & 0xf0) | ((insn >> 4) & 0xf);
        addr =  load_reg(s, rx);
        tcg_gen_addi_i32(addr, addr, imm << 2);
        gen_mov_F0_vreg(1, vrz);
        gen_vfp_st(s, 1, addr);
        dead_tmp(addr);
        break;
    case 0x26:/* fstm */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = ((insn >> 17) & 0xf0) | ((insn >> 4) & 0xf);
        addr =  load_reg(s, rx);
        tcg_gen_addi_i32(addr, addr, imm << 2);
        gen_mov_F0_vreg(1, vrz);
        gen_vfp_st(s, 1, addr);
        dead_tmp(addr);
        break;
    case 0x28:/* fldrs */
        rx = (insn >> 16) & 0x1f;
        ry = (insn >> 21) & 0x1f;
        shift = (insn >> 0x5) & 0x3;
        vrz = insn & 0xf;
        addr =  load_reg(s, rx);
        tmp =  load_reg(s, ry);
        tcg_gen_shli_i32(tmp, tmp, shift);
        tcg_gen_add_i32(addr, addr, tmp);
        dead_tmp(tmp);
        gen_vfp_ld(s, 0, addr);
        gen_mov_vreg_F0(0, vrz);
        dead_tmp(addr);
        break;
    case 0x29:/* fldrd */
        rx = (insn >> 16) & 0x1f;
        ry = (insn >> 21) & 0x1f;
        shift = (insn >> 0x5) & 0x3;
        vrz = insn & 0xf;
        addr =  load_reg(s, rx);
        tmp =  load_reg(s, ry);
        tcg_gen_shli_i32(tmp, tmp, shift);
        tcg_gen_add_i32(addr, addr, tmp);
        dead_tmp(tmp);
        gen_vfp_ld(s, 1, addr);
        gen_mov_vreg_F0(1, vrz);
        dead_tmp(addr);
        break;
    case 0x2a:/* fldrm */
        rx = (insn >> 16) & 0x1f;
        ry = (insn >> 21) & 0x1f;
        shift = (insn >> 0x5) & 0x3;
        vrz = insn & 0xf;
        addr =  load_reg(s, rx);
        tmp =  load_reg(s, ry);
        tcg_gen_shli_i32(tmp, tmp, shift);
        tcg_gen_add_i32(addr, addr, tmp);
        dead_tmp(tmp);
        gen_vfp_ld(s, 1, addr);
        gen_mov_vreg_F0(1, vrz);
        dead_tmp(addr);
        break;
    case 0x2c:/* fstrs */
        rx = (insn >> 16) & 0x1f;
        ry = (insn >> 21) & 0x1f;
        shift = (insn >> 0x5) & 0x3;
        vrz = insn & 0xf;
        addr =  load_reg(s, rx);
        tmp =  load_reg(s, ry);
        tcg_gen_shli_i32(tmp, tmp, shift);
        tcg_gen_add_i32(addr, addr, tmp);
        dead_tmp(tmp);
        gen_mov_F0_vreg(0, vrz);
        gen_vfp_st(s, 0, addr);
        dead_tmp(addr);
        break;
    case 0x2d:/* fstrd */
        rx = (insn >> 16) & 0x1f;
        ry = (insn >> 21) & 0x1f;
        shift = (insn >> 0x5) & 0x3;
        vrz = insn & 0xf;
        addr =  load_reg(s, rx);
        tmp =  load_reg(s, ry);
        tcg_gen_shli_i32(tmp, tmp, shift);
        tcg_gen_add_i32(addr, addr, tmp);
        dead_tmp(tmp);
        gen_mov_F0_vreg(1, vrz);
        gen_vfp_st(s, 1, addr);
        dead_tmp(addr);
        break;
    case 0x2e:/* fstrm */
        rx = (insn >> 16) & 0x1f;
        ry = (insn >> 21) & 0x1f;
        shift = (insn >> 0x5) & 0x3;
        vrz = insn & 0xf;
        addr =  load_reg(s, rx);
        tmp =  load_reg(s, ry);
        tcg_gen_shli_i32(tmp, tmp, shift);
        tcg_gen_add_i32(addr, addr, tmp);
        dead_tmp(tmp);
        gen_mov_F0_vreg(1, vrz);
        gen_vfp_st(s, 1, addr);
        dead_tmp(addr);
        break;
    case 0x30:/* fldms */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = (insn >> 21) & 0xf;
        addr =  load_reg(s, rx);
        for (i = 0; i <= imm; i++) {
            gen_vfp_ld(s, 0, addr);
            gen_mov_vreg_F0(0, vrz);
            tcg_gen_addi_i32(addr, addr, 4);
            vrz++;
        }
        dead_tmp(addr);
        break;
    case 0x31:/* fldmd */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = (insn >> 21) & 0xf;
        addr =  load_reg(s, rx);
        for (i = 0; i <= imm; i++) {
            gen_vfp_ld(s, 1, addr);
            gen_mov_vreg_F0(1, vrz);
            tcg_gen_addi_i32(addr, addr, 4);
            vrz++;
        }
        dead_tmp(addr);
        break;
    case 0x32:/* fldmm */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = (insn >> 21) & 0xf;
        addr =  load_reg(s, rx);
        for (i = 0; i <= imm; i++) {
            gen_vfp_ld(s, 1, addr);
            gen_mov_vreg_F0(1, vrz);
            tcg_gen_addi_i32(addr, addr, 4);
            vrz++;
        }
        dead_tmp(addr);
        break;
    case 0x34:/* fstms */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = (insn >> 21) & 0xf;
        addr =  load_reg(s, rx);
        for (i = 0; i <= imm; i++) {
            gen_mov_F0_vreg(0, vrz);
            gen_vfp_st(s, 0, addr);
            tcg_gen_addi_i32(addr, addr, 4);
            vrz++;
        }
        dead_tmp(addr);
        break;
    case 0x35:/* fstmd */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = (insn >> 21) & 0xf;
        addr =  load_reg(s, rx);
        for (i = 0; i <= imm; i++) {
            gen_mov_F0_vreg(1, vrz);
            gen_vfp_st(s, 1, addr);
            tcg_gen_addi_i32(addr, addr, 4);
            vrz++;
        }
        dead_tmp(addr);
        break;
    case 0x36:/* fstmm */
        rx = (insn >> 16) & 0x1f;
        vrz = insn & 0xf;
        imm = (insn >> 21) & 0xf;
        addr = load_reg(s, rx);
        for (i = 0; i <= imm; i++) {
            gen_mov_F0_vreg(1, vrz);
            gen_vfp_st(s, 1, addr);
            tcg_gen_addi_i32(addr, addr, 4);
            vrz++;
         }
        dead_tmp(addr);
        break;
    default:
wrong:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR, "unknown vdsp insn pc=%x opc=%x\n",
                      s->pc, insn);
        break;
    }

}

static void disas_vdsp_insn128(CPUState *cs, DisasContext *s, uint32_t insn)
{
    int op1, op2, op3, wid, shft, immd_i, rx, ry, vrz_i;
    TCGv_i64 t0 = tcg_temp_new_i64();
    op1 = (insn >> CSKY_VDSP_SOP_SHI_M) & CSKY_VDSP_SOP_MASK_M;
    op2 = (insn >> CSKY_VDSP_SOP_SHI_S) & CSKY_VDSP_SOP_MASK_S;
    op3 = (insn >> CSKY_VDSP_SOP_SHI_E) & CSKY_VDSP_SOP_MASK_E;
    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));

    TCGv vrz = tcg_const_tl(insn & CSKY_VDSP_REG_MASK);
    TCGv vdsp_insn = tcg_const_tl(insn);

    switch (op1) {
    case VDSP_VADD: /*VADD*/
        switch  (op2) {
        case 0x0:  /*VADD.T*/
            gen_helper_vdsp_vadd128(cpu_env, vdsp_insn);
            break;
        case 0x1:       /*VADD.ET*/
            gen_helper_vdsp_vadde128(cpu_env, vdsp_insn);
            break;
        case 0x2:       /*VCADD.T*/
            gen_helper_vdsp_vcadd128(cpu_env, vdsp_insn);
            break;
        case 0x3:       /*VCADD.ET*/
            gen_helper_vdsp_vcadde128(cpu_env, vdsp_insn);
            break;
        case 0xa:      /*VADD.XT.SL*/
            gen_helper_vdsp_vaddxsl128(cpu_env, vdsp_insn);
            break;
        case 0xb:      /*VADD.XT*/
            gen_helper_vdsp_vaddx128(cpu_env, vdsp_insn);
            break;
        case 0xc:      /*VADDH.T */
            gen_helper_vdsp_vaddh128(cpu_env, vdsp_insn);
            break;
        case 0xd:      /*VADDH.T.R*/
            gen_helper_vdsp_vaddhr128(cpu_env, vdsp_insn);
            break;
        case 0xe:      /*VADD.T.S*/
            gen_helper_vdsp_vadds128(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VSUB: /*VSUB*/
        switch (op2) {
        case 0x0:       /*VSUB.T*/
            gen_helper_vdsp_vsub128(cpu_env, vdsp_insn);
            break;
        case 0x1:       /*VSUB.ET*/
            gen_helper_vdsp_vsube128(cpu_env, vdsp_insn);
            break;
        case 0x2:       /*VSABS.T*/
            gen_helper_vdsp_vsabs128(cpu_env, vdsp_insn);
            break;
        case 0x3:       /*VSABS.ET*/
            gen_helper_vdsp_vsabse128(cpu_env, vdsp_insn);
            break;
        case 0x4:       /*VSABSA.T*/
            gen_helper_vdsp_vsabsa128(cpu_env, vdsp_insn);
            break;
        case 0x5:       /*VSABSA.ET */
            gen_helper_vdsp_vsabsae128(cpu_env, vdsp_insn);
            break;
        case 0xb:      /*VSUB.XT */
            gen_helper_vdsp_vsubx128(cpu_env, vdsp_insn);
            break;
        case 0xc:      /*VSUBH.T*/
            gen_helper_vdsp_vsubh128(cpu_env, vdsp_insn);
            break;
        case 0xd:      /*VSUBH.T.R*/
            gen_helper_vdsp_vsubhr128(cpu_env, vdsp_insn);
            break;
        case 0xe:      /*VSUB.T.S*/
            gen_helper_vdsp_vsubs128(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VMUL: /*VMUL*/
        switch (op2) {
        case 0x0:       /*VMUL.T*/
            gen_helper_vdsp_vmul128(cpu_env, vdsp_insn);
            break;
        case 0x1:       /*VMUL.ET*/
            gen_helper_vdsp_vmule128(cpu_env, vdsp_insn);
            break;
        case 0x2:       /*VMULA.T*/
            gen_helper_vdsp_vmula128(cpu_env, vdsp_insn);
            break;
        case 0x3:       /*VMULA.ET*/
            gen_helper_vdsp_vmulae128(cpu_env, vdsp_insn);
            break;
        case 0x4:       /*VMULS.T*/
            gen_helper_vdsp_vmuls128(cpu_env, vdsp_insn);
            break;
        case 0x5:       /*VMULS.ET*/
            gen_helper_vdsp_vmulse128(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
       break;
    case VDSP_VSH:  /*VSH*/
        switch (op2) {
        case 0x0:       /*VSHRI.T*/
        case 0x1:
            gen_helper_vdsp_vshri128(cpu_env, vdsp_insn);
            break;
        case 0x2:       /*VSHRI.T.R*/
        case 0x3:
            gen_helper_vdsp_vshrir128(cpu_env, vdsp_insn);
            break;
        case 0x4:       /*VSHR.T*/
            gen_helper_vdsp_vshr128(cpu_env, vdsp_insn);
            break;
        case 0x6:       /*VSHR.T.R*/
            gen_helper_vdsp_vshrr128(cpu_env, vdsp_insn);
            break;
        case 0x8:       /*VSHLI.T*/
        case 0x9:
            gen_helper_vdsp_vshli128(cpu_env, vdsp_insn);
            break;
        case 0xa:      /*VSHLI.T.S*/
        case 0xb:
            gen_helper_vdsp_vshlis128(cpu_env, vdsp_insn);
            break;
        case 0xc:      /*VSHL.T*/
            gen_helper_vdsp_vshl128(cpu_env, vdsp_insn);
            break;
        case 0xe:      /*VSHL.T.S*/
            gen_helper_vdsp_vshls128(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VCMP: /*VCMP*/
        switch (op2) {
        case 0x0:       /*VCMPHS.T*/
            gen_helper_vdsp_vcmphs128(cpu_env, vdsp_insn);
            break;
        case 0x1:       /*VCMPLT.T*/
            gen_helper_vdsp_vcmplt128(cpu_env, vdsp_insn);
            break;
        case 0x2:       /*VCMPNE.T*/
            gen_helper_vdsp_vcmpne128(cpu_env, vdsp_insn);
            break;
        case 0x4:       /*VCMPHSZ.T*/
            gen_helper_vdsp_vcmphsz128(cpu_env, vdsp_insn);
            break;
        case 0x5:       /*VCMPLTZ.T*/
            gen_helper_vdsp_vcmpltz128(cpu_env, vdsp_insn);
            break;
        case 0x6:       /*VCMPNEZ.T */
            gen_helper_vdsp_vcmpnez128(cpu_env, vdsp_insn);
            break;
        case 0x8:       /*VMAX.T*/
            gen_helper_vdsp_vmax128(cpu_env, vdsp_insn);
            break;
        case 0x9:       /*VMIN.T*/
            gen_helper_vdsp_vmin128(cpu_env, vdsp_insn);
            break;
        case 0xc:      /*VCMAX.T*/
            gen_helper_vdsp_vcmax128(cpu_env, vdsp_insn);
            break;
        case 0xd:      /*VCMIN.T*/
            gen_helper_vdsp_vcmin128(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VAND: /*VAND*/
        switch (op2) {
        case 0x0:       /*VAND.T*/
            gen_helper_vdsp_vand128(cpu_env, vdsp_insn);
            break;
        case 0x1:       /*VANDN.T*/
            gen_helper_vdsp_vandn128(cpu_env, vdsp_insn);
            break;
        case 0x2:       /*VOR.T*/
            gen_helper_vdsp_vor128(cpu_env, vdsp_insn);
            break;
        case 0x3:       /*VNOR.T*/
            gen_helper_vdsp_vnor128(cpu_env, vdsp_insn);
            break;
        case 0x4:       /*VXOR.T*/
            gen_helper_vdsp_vxor128(cpu_env, vdsp_insn);
            break;
        case 0x9:       /*VTST.T */
            gen_helper_vdsp_vtst128(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VMOV: /*VMOV*/
        switch (op2) {
        case 0x0:       /*VMOV*/
            gen_helper_vdsp_vmov128(cpu_env, vdsp_insn);
            break;
        case 0x1:       /*VMOV.ET*/
            gen_helper_vdsp_vmove128(cpu_env, vdsp_insn);
            break;
        case 0x8:       /*VMOV.T.L*/
            gen_helper_vdsp_vmovl128(cpu_env, vdsp_insn);
            break;
        case 0xa:      /*VMOV.T.SL*/
            gen_helper_vdsp_vmovsl128(cpu_env, vdsp_insn);
            break;
        case 0xb:      /*VMOV.T.H*/
            gen_helper_vdsp_vmovh128(cpu_env, vdsp_insn);
            break;
        case 0xc:      /*VMOV.T.RH*/
            gen_helper_vdsp_vmovrh128(cpu_env, vdsp_insn);
            break;
        case 0xe:      /*VSTOU.T.SL*/
            gen_helper_vdsp_vstousl128(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VSPE: /*VSPE*/
        switch (op2) {
        case 0x3:       /*VREV.T */
            gen_helper_vdsp_vrev128(cpu_env, vdsp_insn);
            break;
        case 0x4:       /*VDUP.T*/
            gen_helper_vdsp_vdup128(cpu_env, vdsp_insn);
            break;
        case 0x5:       /*VCNT1.T*/
            gen_helper_vdsp_vcnt1128(cpu_env, vdsp_insn);
            break;
        case 0x6:       /*VCLZ.T*/
            gen_helper_vdsp_vclz128(cpu_env, vdsp_insn);
            break;
        case 0x7:       /*VCLS.T*/
            gen_helper_vdsp_vcls128(cpu_env, vdsp_insn);
            break;
        case 0x8:       /*VBPERMZ.T*/
            gen_helper_vdsp_vbpermz128(cpu_env, vdsp_insn);
            break;
        case 0x9:       /*VBPERM.T*/
            gen_helper_vdsp_vbperm128(cpu_env, vdsp_insn);
            break;
        case 0xa:      /*VTRCH.T*/
            gen_helper_vdsp_vtrch128(cpu_env, vdsp_insn);
            break;
        case 0xb:      /*VTRCL.T*/
            gen_helper_vdsp_vtrcl128(cpu_env, vdsp_insn);
            break;
        case 0xc:      /*VICH.T*/
            gen_helper_vdsp_vich128(cpu_env, vdsp_insn);
            break;
        case 0xd:      /*VICL.T*/
            gen_helper_vdsp_vicl128(cpu_env, vdsp_insn);
            break;
        case 0xe:      /*VDCH.T*/
            gen_helper_vdsp_vdch128(cpu_env, vdsp_insn);
            break;
        case 0xf:      /*VDCL.T*/
            gen_helper_vdsp_vdcl128(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VABS: /*VABS*/
        switch (op2) {
        case 0x0:       /*VABS.T*/
            gen_helper_vdsp_vabs128(cpu_env, vdsp_insn);
            break;
        case 0x2:       /*VABS.T.S*/
            gen_helper_vdsp_vabss128(cpu_env, vdsp_insn);
            break;
        case 0x4:       /*VNEG.T*/
            gen_helper_vdsp_vneg128(cpu_env, vdsp_insn);
            break;
        case 0x6:       /*VNEG.T.S*/
            gen_helper_vdsp_vnegs128(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VMVVR:        /*VM*VR*/
        switch (op2) {
        case 0x0:       /*VMFVR.U8*/
            gen_helper_vdsp_vmfvru8(cpu_env, vdsp_insn);
            break;
        case 0x1:       /*VMFVR.U16*/
            gen_helper_vdsp_vmfvru16(cpu_env, vdsp_insn);
            break;
        case 0x2:       /*VMFVR.U32*/
            gen_helper_vdsp_vmfvru32(cpu_env, vdsp_insn);
            break;
        case 0x4:       /*VMFVR.S8*/
            gen_helper_vdsp_vmfvrs8(cpu_env, vdsp_insn);
            break;
        case 0x5:       /*VMFVR.S16*/
            gen_helper_vdsp_vmfvrs16(cpu_env, vdsp_insn);
            break;
        case 0x8:       /*VMTVR.U8*/
            gen_helper_vdsp_vmtvru8(cpu_env, vdsp_insn);
            break;
        case 0x9:       /*VMTVR.U16*/
            gen_helper_vdsp_vmtvru16(cpu_env, vdsp_insn);
            break;
        case 0xa:      /*VMTVR.U32*/
            gen_helper_vdsp_vmtvru32(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VINS: /*VINS*/
        switch ((int)(8 * pow(2, wid))) {
        case 8:
            gen_helper_vdsp_vins8(cpu_env, vdsp_insn);
            break;
        case 16:
            gen_helper_vdsp_vins16(cpu_env, vdsp_insn);
            break;
        case 32:
            gen_helper_vdsp_vins32(cpu_env, vdsp_insn);
            break;
        }
        break;
    default:
        rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
        ry = ((insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK);
        vrz_i = (insn & CSKY_VDSP_REG_MASK);
        shft = (insn >> CSKY_VDSP_SOP_SHI_S) & 0x3;
        immd_i = ((insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK) << 4 |
            ((insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_REG_MASK);
        TCGv_i64 tmp1 = tcg_temp_new_i64();
        TCGv_i64 tmp2 = tcg_temp_new_i64();
        TCGv tmp3 = tcg_temp_new_i32();
        switch (op3) {
        case 0x8:       /*VLDD*/
            tcg_gen_addi_tl(tmp3, cpu_R[rx], immd_i << 3);
            tcg_gen_qemu_ld64(tmp1, tmp3, s->mem_idx);
            gen_helper_vdsp_store(vrz, tmp1, cpu_env);
            break;
        case 0x9:       /*VLDQ*/
            tcg_gen_addi_tl(tmp3, cpu_R[rx], immd_i << 3);
            tcg_gen_qemu_ld64(tmp1, tmp3, s->mem_idx);
            tcg_gen_addi_tl(tmp3, tmp3, 8);
            tcg_gen_qemu_ld64(tmp2, tmp3, s->mem_idx);
            gen_helper_vdsp_store2(vrz, tmp1, tmp2, cpu_env);
            break;
        case 0xa:      /*VSTD*/
            tcg_gen_ext_i32_i64(t0, vdsp_Rl[vrz_i]);
            tcg_gen_mov_i64(tmp1, t0);
            tcg_gen_addi_tl(tmp3, cpu_R[rx], immd_i << 3);
            tcg_gen_qemu_st64(tmp1, tmp3, s->mem_idx);
            break;
        case 0xb:      /*VSTQ*/
            tcg_gen_ext_i32_i64(t0, vdsp_Rl[vrz_i]);
            tcg_gen_mov_i64(tmp1, t0);
            tcg_gen_ext_i32_i64(t0, vdsp_Rh[vrz_i]);
            tcg_gen_mov_i64(tmp2, t0);
            tcg_gen_addi_tl(tmp3, cpu_R[rx], immd_i << 3);
            tcg_gen_qemu_st64(tmp1, tmp3, s->mem_idx);
            tcg_gen_addi_tl(tmp3, tmp3, 8);
            tcg_gen_qemu_st64(tmp2, tmp3, s->mem_idx);
            break;
        case 0xc:      /*VLDRD*/
            tcg_gen_mov_tl(tmp3, cpu_R[ry]);
            tcg_gen_shli_tl(tmp3, tmp3, shft);
            tcg_gen_add_tl(tmp3, cpu_R[rx], tmp3);
            tcg_gen_qemu_ld64(tmp1, tmp3, s->mem_idx);
            gen_helper_vdsp_store(vrz, tmp1, cpu_env);
            break;
        case 0xd:      /*VLDRQ*/
            tcg_gen_mov_tl(tmp3, cpu_R[ry]);
            tcg_gen_shli_tl(tmp3, tmp3, shft);
            tcg_gen_add_tl(tmp3, cpu_R[rx], tmp3);
            tcg_gen_qemu_ld64(tmp1, tmp3, s->mem_idx);
            tcg_gen_addi_tl(tmp3, tmp3, 8);
            tcg_gen_qemu_ld64(tmp2, tmp3, s->mem_idx);
            gen_helper_vdsp_store2(vrz, tmp1, tmp2, cpu_env);
            break;
        case 0xe:      /*VSTRD*/
            tcg_gen_ext_i32_i64(t0, vdsp_Rl[vrz_i]);
            tcg_gen_mov_i64(tmp1, t0);
            tcg_gen_mov_tl(tmp3, cpu_R[ry]);
            tcg_gen_shli_tl(tmp3, tmp3, shft);
            tcg_gen_add_tl(tmp3, cpu_R[rx], tmp3);
            tcg_gen_qemu_st64(tmp1, tmp3, s->mem_idx);
            break;
        case 0xf:      /*VSTRQ*/
            tcg_gen_ext_i32_i64(t0, vdsp_Rl[vrz_i]);
            tcg_gen_mov_i64(tmp1, t0);
            tcg_gen_ext_i32_i64(t0, vdsp_Rh[vrz_i]);
            tcg_gen_mov_i64(tmp2, t0);
            tcg_gen_mov_tl(tmp3, cpu_R[ry]);
            tcg_gen_shli_tl(tmp3, tmp3, shft);
            tcg_gen_add_tl(tmp3, cpu_R[rx], tmp3);
            tcg_gen_qemu_st64(tmp1, tmp3, s->mem_idx);
            tcg_gen_addi_tl(tmp3, tmp3, 8);
            tcg_gen_qemu_st64(tmp2, tmp3, s->mem_idx);
            break;
        default:
wrong:
            generate_exception(s, EXCP_CSKY_UDEF);
            qemu_log_mask(LOG_GUEST_ERROR, "unknown vdsp insn pc=%x opc=%x\n",
                      s->pc, insn);
            break;
        }
    }
}

static void disas_vdsp_insn64(CPUState *cs, DisasContext *s, uint32_t insn)
{
    int op1, op2, op3, wid, shft, immd_i, rx, ry, vrz_i;
    TCGv_i64 t0 = tcg_temp_new_i64();
    op1 = (insn >> CSKY_VDSP_SOP_SHI_M) & CSKY_VDSP_SOP_MASK_M;
    op2 = (insn >> CSKY_VDSP_SOP_SHI_S) & CSKY_VDSP_SOP_MASK_S;
    op3 = (insn >> CSKY_VDSP_SOP_SHI_E) & CSKY_VDSP_SOP_MASK_E;
    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));

    TCGv vrz = tcg_const_tl(insn & CSKY_VDSP_REG_MASK);
    TCGv vdsp_insn = tcg_const_tl(insn);

    switch (op1) {
    case VDSP_VADD: /*VADD*/
        switch (op2) {
        case 0x0:  /*VADD.T*/
            gen_helper_vdsp_vadd64(cpu_env, vdsp_insn);
            break;
        case 0x1:       /*VADD.ET*/
            gen_helper_vdsp_vadde64(cpu_env, vdsp_insn);
            break;
        case 0x2:       /*VCADD.T*/
            gen_helper_vdsp_vcadd64(cpu_env, vdsp_insn);
            break;
        case 0x3:       /*VCADD.ET*/
            gen_helper_vdsp_vcadde64(cpu_env, vdsp_insn);
            break;
        case 0xa:      /*VADD.XT.SL*/
            gen_helper_vdsp_vaddxsl64(cpu_env, vdsp_insn);
            break;
        case 0xb:      /*VADD.XT*/
            gen_helper_vdsp_vaddx64(cpu_env, vdsp_insn);
            break;
        case 0xc:      /*VADDH.T */
            gen_helper_vdsp_vaddh64(cpu_env, vdsp_insn);
            break;
        case 0xd:      /*VADDH.T.R*/
            gen_helper_vdsp_vaddhr64(cpu_env, vdsp_insn);
            break;
        case 0xe:      /*VADD.T.S*/
            gen_helper_vdsp_vadds64(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VSUB: /*VSUB*/
        switch (op2) {
        case 0x0:       /*VSUB.T*/
            gen_helper_vdsp_vsub64(cpu_env, vdsp_insn);
            break;
        case 0x1:       /*VSUB.ET*/
            gen_helper_vdsp_vsube64(cpu_env, vdsp_insn);
            break;
        case 0x2:       /*VSABS.T*/
            gen_helper_vdsp_vsabs64(cpu_env, vdsp_insn);
            break;
        case 0x3:       /*VSABS.ET*/
            gen_helper_vdsp_vsabse64(cpu_env, vdsp_insn);
            break;
        case 0x4:       /*VSABSA.T*/
            gen_helper_vdsp_vsabsa64(cpu_env, vdsp_insn);
            break;
        case 0x5:       /*VSABSA.ET */
            gen_helper_vdsp_vsabsae64(cpu_env, vdsp_insn);
            break;
        case 0xb:      /*VSUB.XT */
            gen_helper_vdsp_vsubx64(cpu_env, vdsp_insn);
            break;
        case 0xc:      /*VSUBH.T*/
            gen_helper_vdsp_vsubh64(cpu_env, vdsp_insn);
            break;
        case 0xd:      /*VSUBH.T.R*/
            gen_helper_vdsp_vsubhr64(cpu_env, vdsp_insn);
            break;
        case 0xe:      /*VSUB.T.S*/
            gen_helper_vdsp_vsubs64(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VMUL: /*VMUL*/
        switch (op2) {
        case 0x0:       /*VMUL.T*/
            gen_helper_vdsp_vmul64(cpu_env, vdsp_insn);
            break;
        case 0x1:       /*VMUL.ET*/
            gen_helper_vdsp_vmule64(cpu_env, vdsp_insn);
            break;
        case 0x2:       /*VMULA.T*/
            gen_helper_vdsp_vmula64(cpu_env, vdsp_insn);
            break;
        case 0x3:       /*VMULA.ET*/
            gen_helper_vdsp_vmulae64(cpu_env, vdsp_insn);
            break;
        case 0x4:       /*VMULS.T*/
            gen_helper_vdsp_vmuls64(cpu_env, vdsp_insn);
            break;
        case 0x5:       /*VMULS.ET*/
            gen_helper_vdsp_vmulse64(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VSH:  /*VSH*/
        switch (op2) {
        case 0x0:       /*VSHRI.T*/
        case 0x1:
            gen_helper_vdsp_vshri64(cpu_env, vdsp_insn);
            break;
        case 0x2:       /*VSHRI.T.R*/
        case 0x3:
            gen_helper_vdsp_vshrir64(cpu_env, vdsp_insn);
            break;
        case 0x4:       /*VSHR.T*/
            gen_helper_vdsp_vshr64(cpu_env, vdsp_insn);
            break;
        case 0x6:       /*VSHR.T.R*/
            gen_helper_vdsp_vshrr64(cpu_env, vdsp_insn);
            break;
        case 0x8:       /*VSHLI.T*/
        case 0x9:
            gen_helper_vdsp_vshli64(cpu_env, vdsp_insn);
            break;
        case 0xa:      /*VSHLI.T.S*/
        case 0xb:
            gen_helper_vdsp_vshlis64(cpu_env, vdsp_insn);
            break;
        case 0xc:      /*VSHL.T*/
            gen_helper_vdsp_vshl64(cpu_env, vdsp_insn);
            break;
        case 0xe:      /*VSHL.T.S*/
            gen_helper_vdsp_vshls64(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VCMP: /*VCMP*/
        switch (op2) {
        case 0x0:       /*VCMPHS.T*/
            gen_helper_vdsp_vcmphs64(cpu_env, vdsp_insn);
            break;
        case 0x1:       /*VCMPLT.T*/
            gen_helper_vdsp_vcmplt64(cpu_env, vdsp_insn);
            break;
        case 0x2:       /*VCMPNE.T*/
            gen_helper_vdsp_vcmpne64(cpu_env, vdsp_insn);
            break;
        case 0x4:       /*VCMPHSZ.T*/
            gen_helper_vdsp_vcmphsz64(cpu_env, vdsp_insn);
            break;
        case 0x5:       /*VCMPLTZ.T*/
            gen_helper_vdsp_vcmpltz64(cpu_env, vdsp_insn);
            break;
        case 0x6:       /*VCMPNEZ.T */
            gen_helper_vdsp_vcmpnez64(cpu_env, vdsp_insn);
            break;
        case 0x8:       /*VMAX.T*/
            gen_helper_vdsp_vmax64(cpu_env, vdsp_insn);
            break;
        case 0x9:       /*VMIN.T*/
            gen_helper_vdsp_vmin64(cpu_env, vdsp_insn);
            break;
        case 0xc:      /*VCMAX.T*/
            gen_helper_vdsp_vcmax64(cpu_env, vdsp_insn);
            break;
        case 0xd:      /*VCMIN.T*/
            gen_helper_vdsp_vcmin64(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VAND: /*VAND*/
        switch (op2) {
        case 0x0:       /*VAND.T*/
            gen_helper_vdsp_vand64(cpu_env, vdsp_insn);
            break;
        case 0x1:       /*VANDN.T*/
            gen_helper_vdsp_vandn64(cpu_env, vdsp_insn);
            break;
        case 0x2:       /*VOR.T*/
            gen_helper_vdsp_vor64(cpu_env, vdsp_insn);
            break;
        case 0x3:       /*VNOR.T*/
            gen_helper_vdsp_vnor64(cpu_env, vdsp_insn);
            break;
        case 0x4:       /*VXOR.T*/
            gen_helper_vdsp_vxor64(cpu_env, vdsp_insn);
            break;
        case 0x9:       /*VTST.T */
            gen_helper_vdsp_vtst64(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VMOV: /*VMOV*/
        switch (op2) {
        case 0x0:       /*VMOV*/
            gen_helper_vdsp_vmov64(cpu_env, vdsp_insn);
            break;
        case 0x1:       /*VMOV.ET*/
            gen_helper_vdsp_vmove64(cpu_env, vdsp_insn);
            break;
        case 0x8:       /*VMOV.T.L*/
            gen_helper_vdsp_vmovl64(cpu_env, vdsp_insn);
            break;
        case 0xa:      /*VMOV.T.SL*/
            gen_helper_vdsp_vmovsl64(cpu_env, vdsp_insn);
            break;
        case 0xb:      /*VMOV.T.H*/
            gen_helper_vdsp_vmovh64(cpu_env, vdsp_insn);
            break;
        case 0xc:      /*VMOV.T.RH*/
            gen_helper_vdsp_vmovrh64(cpu_env, vdsp_insn);
            break;
        case 0xe:      /*VSTOU.T.SL*/
            gen_helper_vdsp_vstousl64(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VSPE: /*VSPE*/
        switch (op2) {
        case 0x3:       /*VREV.T */
            gen_helper_vdsp_vrev64(cpu_env, vdsp_insn);
            break;
        case 0x4:       /*VDUP.T*/
            gen_helper_vdsp_vdup64(cpu_env, vdsp_insn);
            break;
        case 0x5:       /*VCNT1.T*/
            gen_helper_vdsp_vcnt164(cpu_env, vdsp_insn);
            break;
        case 0x6:       /*VCLZ.T*/
            gen_helper_vdsp_vclz64(cpu_env, vdsp_insn);
            break;
        case 0x7:       /*VCLS.T*/
            gen_helper_vdsp_vcls64(cpu_env, vdsp_insn);
            break;
        case 0x8:       /*VBPERMZ.T*/
            gen_helper_vdsp_vbpermz64(cpu_env, vdsp_insn);
            break;
        case 0x9:       /*VBPERM.T*/
            gen_helper_vdsp_vbperm64(cpu_env, vdsp_insn);
            break;
        case 0xa:      /*VTRCH.T*/
            gen_helper_vdsp_vtrch64(cpu_env, vdsp_insn);
            break;
        case 0xb:      /*VTRCL.T*/
            gen_helper_vdsp_vtrcl64(cpu_env, vdsp_insn);
            break;
        case 0xc:      /*VICH.T*/
            gen_helper_vdsp_vich64(cpu_env, vdsp_insn);
            break;
        case 0xd:      /*VICL.T*/
            gen_helper_vdsp_vicl64(cpu_env, vdsp_insn);
            break;
        case 0xe:      /*VDCH.T*/
            gen_helper_vdsp_vdch64(cpu_env, vdsp_insn);
            break;
        case 0xf:      /*VDCL.T*/
            gen_helper_vdsp_vdcl64(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VABS: /*VABS*/
        switch (op2) {
        case 0x0:       /*VABS.T*/
            gen_helper_vdsp_vabs64(cpu_env, vdsp_insn);
            break;
        case 0x2:       /*VABS.T.S*/
            gen_helper_vdsp_vabss64(cpu_env, vdsp_insn);
            break;
        case 0x4:       /*VNEG.T*/
            gen_helper_vdsp_vneg64(cpu_env, vdsp_insn);
            break;
        case 0x6:       /*VNEG.T.S*/
            gen_helper_vdsp_vnegs64(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VMVVR:        /*VM*VR*/
        switch (op2) {
        case 0x0:       /*VMFVR.U8*/
            gen_helper_vdsp_vmfvru8(cpu_env, vdsp_insn);
            break;
        case 0x1:       /*VMFVR.U16*/
            gen_helper_vdsp_vmfvru16(cpu_env, vdsp_insn);
            break;
        case 0x2:       /*VMFVR.U32*/
            gen_helper_vdsp_vmfvru32(cpu_env, vdsp_insn);
            break;
        case 0x4:       /*VMFVR.S8*/
            gen_helper_vdsp_vmfvrs8(cpu_env, vdsp_insn);
            break;
        case 0x5:       /*VMFVR.S16*/
            gen_helper_vdsp_vmfvrs16(cpu_env, vdsp_insn);
            break;
        case 0x8:       /*VMTVR.U8*/
            gen_helper_vdsp_vmtvru8(cpu_env, vdsp_insn);
            break;
        case 0x9:       /*VMTVR.U16*/
            gen_helper_vdsp_vmtvru16(cpu_env, vdsp_insn);
            break;
        case 0xa:      /*VMTVR.U32*/
            gen_helper_vdsp_vmtvru32(cpu_env, vdsp_insn);
            break;
        default:
            goto wrong;
            break;
        }
        break;
    case VDSP_VINS: /*VINS*/
        switch ((int)(8 * pow(2, wid))) {
        case 8:
            gen_helper_vdsp_vins8(cpu_env, vdsp_insn);
            break;
        case 16:
            gen_helper_vdsp_vins16(cpu_env, vdsp_insn);
            break;
        case 32:
            gen_helper_vdsp_vins32(cpu_env, vdsp_insn);
            break;
        }
        break;
    default:
        rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
        ry = ((insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK);
        vrz_i = (insn & CSKY_VDSP_REG_MASK);
        shft = (insn >> CSKY_VDSP_SOP_SHI_S) & 0x3;
        immd_i = ((insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK) << 4 |
            ((insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_REG_MASK);
        TCGv_i64 tmp1 = tcg_temp_new_i64();
        TCGv_i64 tmp2 = tcg_temp_new_i64();
        TCGv tmp3 = tcg_temp_new_i32();
        switch (op3) {
        case 0x8:       /*VLDD*/
            tcg_gen_addi_tl(tmp3, cpu_R[rx], immd_i << 3);
            tcg_gen_qemu_ld64(tmp1, tmp3, s->mem_idx);
            gen_helper_vdsp_store(vrz, tmp1, cpu_env);
            break;
        case 0x9:       /*VLDQ*/
            tcg_gen_addi_tl(tmp3, cpu_R[rx], immd_i << 3);
            tcg_gen_qemu_ld64(tmp1, tmp3, s->mem_idx);
            tcg_gen_addi_tl(tmp3, tmp3, 8);
            tcg_gen_qemu_ld64(tmp2, tmp3, s->mem_idx);
            gen_helper_vdsp_store2(vrz, tmp1, tmp2, cpu_env);
            break;
        case 0xa:      /*VSTD*/
            tcg_gen_ext_i32_i64(t0, vdsp_Rl[vrz_i]);
            tcg_gen_mov_i64(tmp1, t0);
            tcg_gen_addi_tl(tmp3, cpu_R[rx], immd_i << 3);
            tcg_gen_qemu_st64(tmp1, tmp3, s->mem_idx);
            break;
        case 0xb:      /*VSTQ*/
            tcg_gen_ext_i32_i64(t0, vdsp_Rh[vrz_i]);
            tcg_gen_mov_i64(tmp1, t0);
            tcg_gen_ext_i32_i64(t0, vdsp_Rl[vrz_i]);
            tcg_gen_mov_i64(tmp2, t0);
            tcg_gen_addi_tl(tmp3, cpu_R[rx], immd_i << 3);
            tcg_gen_qemu_st64(tmp1, tmp3, s->mem_idx);
            tcg_gen_addi_tl(tmp3, tmp3, 8);
            tcg_gen_qemu_st64(tmp2, tmp3, s->mem_idx);
        case 0xc:         /*VLDRD*/
            /* break; */
            tcg_gen_mov_tl(tmp3, cpu_R[ry]);
            tcg_gen_shli_tl(tmp3, tmp3, shft);
            tcg_gen_add_tl(tmp3, cpu_R[rx], tmp3);
            tcg_gen_qemu_ld64(tmp1, tmp3, s->mem_idx);
            gen_helper_vdsp_store(vrz, tmp1, cpu_env);
            break;
        case 0xd:         /*VLDRQ*/
            tcg_gen_mov_tl(tmp3, cpu_R[ry]);
            tcg_gen_shli_tl(tmp3, tmp3, shft);
            tcg_gen_add_tl(tmp3, cpu_R[rx], tmp3);
            tcg_gen_qemu_ld64(tmp1, tmp3, s->mem_idx);
            tcg_gen_addi_tl(tmp3, tmp3, 8);
            tcg_gen_qemu_ld64(tmp2, tmp3, s->mem_idx);
            gen_helper_vdsp_store2(vrz, tmp1, tmp2, cpu_env);
            break;
        case 0xe:         /*VSTRD*/
            tcg_gen_ext_i32_i64(t0, vdsp_Rl[vrz_i]);
            tcg_gen_mov_i64(tmp1, t0);
            tcg_gen_mov_tl(tmp3, cpu_R[ry]);
            tcg_gen_shli_tl(tmp3, tmp3, shft);
            tcg_gen_add_tl(tmp3, cpu_R[rx], tmp3);
            tcg_gen_qemu_st64(tmp1, tmp3, s->mem_idx);
            break;
        case 0xf:         /*VSTRQ*/
            tcg_gen_ext_i32_i64(t0, vdsp_Rl[vrz_i]);
            tcg_gen_mov_i64(tmp1, t0);
            tcg_gen_ext_i32_i64(t0, vdsp_Rh[vrz_i]);
            tcg_gen_mov_i64(tmp2, t0);
            tcg_gen_mov_tl(tmp3, cpu_R[ry]);
            tcg_gen_shli_tl(tmp3, tmp3, shft);
            tcg_gen_add_tl(tmp3, cpu_R[rx], tmp3);
            tcg_gen_qemu_st64(tmp1, tmp3, s->mem_idx);
            tcg_gen_addi_tl(tmp3, tmp3, 8);
            tcg_gen_qemu_st64(tmp2, tmp3, s->mem_idx);
        break;
        default:
wrong:
            generate_exception(s, EXCP_CSKY_UDEF);
            qemu_log_mask(LOG_GUEST_ERROR, "unknown vdsp insn pc=%x opc=%x\n",
                      s->pc, insn);
            break;
        }
    }
}

static inline void dspv2_insn_padd_8(int rz, int rx, int ry)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    /* rz[7:0] = rx[7:0] + ry[7:0]*/
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xff);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xff);
    tcg_gen_add_i32(t2, t0, t1);
    tcg_gen_andi_i32(t2, t2, 0xff);
    /* rz[15:8] = rx[15:8] + ry[15:8]*/
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xff00);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xff00);
    tcg_gen_add_i32(t0, t0, t1);
    tcg_gen_andi_i32(t0, t0, 0xff00);
    tcg_gen_or_i32(t2, t2, t0);
    /* rz[23:16] = rx[23:16] + ry[23:16]*/
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xff0000);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xff0000);
    tcg_gen_add_i32(t0, t0, t1);
    tcg_gen_andi_i32(t0, t0, 0xff0000);
    tcg_gen_or_i32(t2, t2, t0);
    /* rz[31:24] = rx[31:24] + ry[31:24]*/
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xff000000);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xff000000);
    tcg_gen_add_i32(t0, t0, t1);
    tcg_gen_andi_i32(t0, t0, 0xff000000);
    tcg_gen_or_i32(cpu_R[rz], t2, t0);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static inline void dspv2_insn_padd_16(int rz, int rx, int ry)
{
    /* rz = {(hi_x + hi_y)[15:0], (lo_x + lo_y)[15:0]} */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    /* lo_x + lo_y */
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff);
    tcg_gen_add_i32(t2, t0, t1);
    tcg_gen_andi_i32(t2, t2, 0xffff);
    /* hi_x + hi_y */
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff0000);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff0000);
    tcg_gen_add_i32(t0, t0, t1);
    tcg_gen_or_i32(cpu_R[rz], t2, t0);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static inline void dspv2_insn_psub_8(int rz, int rx, int ry)
{
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    /* rz[7:0] = rx[7:0] - ry[7:0]*/
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xff);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xff);
    tcg_gen_sub_i32(t2, t0, t1);
    tcg_gen_andi_i32(t2, t2, 0xff);
    /* rz[15:8] = rx[15:8] - ry[15:8]*/
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xff00);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xff00);
    tcg_gen_sub_i32(t0, t0, t1);
    tcg_gen_andi_i32(t0, t0, 0xff00);
    tcg_gen_or_i32(t2, t2, t0);
    /* rz[23:16] = rx[23:16] - ry[23:16]*/
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xff0000);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xff0000);
    tcg_gen_sub_i32(t0, t0, t1);
    tcg_gen_andi_i32(t0, t0, 0xff0000);
    tcg_gen_or_i32(t2, t2, t0);
    /* rz[31:24] = rx[31:24] - ry[31:24]*/
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xff000000);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xff000000);
    tcg_gen_sub_i32(t0, t0, t1);
    tcg_gen_andi_i32(t0, t0, 0xff000000);
    tcg_gen_or_i32(cpu_R[rz], t2, t0);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static inline void dspv2_insn_psub_16(int rz, int rx, int ry)
{
    /* rz = {(hi_x - hi_y)[15:0], (lo_x - lo_y)[15:0]} */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff);
    tcg_gen_sub_i32(t2, t0, t1);
    tcg_gen_andi_i32(t2, t2, 0xffff);
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff0000);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff0000);
    tcg_gen_sub_i32(t0, t0, t1);
    tcg_gen_or_i32(cpu_R[rz], t2, t0);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static inline void dspv2_insn_addh_s32(int rz, int rx, int ry)
{
    /* rz = (rx[31:0] + ry[31:0]) / 2, signed */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 1);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_addh_u32(int rz, int rx, int ry)
{
    /* rz = (rx[31:0] + ry[31:0]) / 2, unsigned */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(t0, cpu_R[rx]);
    tcg_gen_extu_i32_i64(t1, cpu_R[ry]);
    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 1);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_subh_s32(int rz, int rx, int ry)
{
    /* rz = (rx[31:0] - ry[31:0]) / 2, signed */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_sub_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 1);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_subh_u32(int rz, int rx, int ry)
{
    /* rz = (rx[31:0] - ry[31:0]) / 2, unsigned */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(t0, cpu_R[rx]);
    tcg_gen_extu_i32_i64(t1, cpu_R[ry]);
    tcg_gen_sub_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 1);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_add_64(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(t0, cpu_R[rx], cpu_R[(rx + 1) % 32]);
    tcg_gen_concat_i32_i64(t1, cpu_R[ry], cpu_R[(ry + 1) % 32]);
    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_sub_64(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(t0, cpu_R[rx], cpu_R[(rx + 1) % 32]);
    tcg_gen_concat_i32_i64(t1, cpu_R[ry], cpu_R[(ry + 1) % 32]);
    tcg_gen_sub_i64(t0, t0, t1);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_add_s64_s(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(t0, cpu_R[rx], cpu_R[(rx + 1) % 32]);
    tcg_gen_concat_i32_i64(t1, cpu_R[ry], cpu_R[(ry + 1) % 32]);
    gen_helper_dspv2_add_s64_s(t0, t0, t1);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_add_u64_s(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(t0, cpu_R[rx], cpu_R[(rx + 1) % 32]);
    tcg_gen_concat_i32_i64(t1, cpu_R[ry], cpu_R[(ry + 1) % 32]);
    gen_helper_dspv2_add_u64_s(t0, t0, t1);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_sub_s64_s(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(t0, cpu_R[rx], cpu_R[(rx + 1) % 32]);
    tcg_gen_concat_i32_i64(t1, cpu_R[ry], cpu_R[(ry + 1) % 32]);
    gen_helper_dspv2_sub_s64_s(t0, t0, t1);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_sub_u64_s(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(t0, cpu_R[rx], cpu_R[(rx + 1) % 32]);
    tcg_gen_concat_i32_i64(t1, cpu_R[ry], cpu_R[(ry + 1) % 32]);
    gen_helper_dspv2_sub_u64_s(t0, t0, t1);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_sop_add_sub(CPUState *cs,
                                     DisasContext *s, uint32_t insn)
{
    int thop, rz, rx, ry;
    thop = (insn >> CSKY_DSPV2_THOP_SHI) & CSKY_DSPV2_THOP_MASK;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;

    switch (thop) {
    case OP_PADD_8_1st:
    case OP_PADD_8_2nd:
        dspv2_insn_padd_8(rz, rx, ry);
        break;
    case OP_PADD_16_1st:
    case OP_PADD_16_2nd:
        dspv2_insn_padd_16(rz, rx, ry);
        break;
    case OP_PADD_U8_S:
        gen_helper_dspv2_padd_u8_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PADD_S8_S:
        gen_helper_dspv2_padd_s8_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PADD_U16_S:
        gen_helper_dspv2_padd_u16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PADD_S16_S:
        gen_helper_dspv2_padd_s16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_ADD_U32_S:
        gen_helper_dspv2_add_u32_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_ADD_S32_S:
        gen_helper_dspv2_add_s32_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSUB_8_1st:
    case OP_PSUB_8_2nd:
        dspv2_insn_psub_8(rz, rx, ry);
        break;
    case OP_PSUB_16_1st:
    case OP_PSUB_16_2nd:
        dspv2_insn_psub_16(rz, rx, ry);
        break;
    case OP_PSUB_U8_S:
        gen_helper_dspv2_psub_u8_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSUB_S8_S:
        gen_helper_dspv2_psub_s8_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSUB_U16_S:
        gen_helper_dspv2_psub_u16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSUB_S16_S:
        gen_helper_dspv2_psub_s16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_SUB_U32_S:
        gen_helper_dspv2_sub_u32_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_SUB_S32_S:
        gen_helper_dspv2_sub_s32_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PADDH_U8:
        gen_helper_dspv2_paddh_u8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PADDH_S8:
        gen_helper_dspv2_paddh_s8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PADDH_U16:
        gen_helper_dspv2_paddh_u16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PADDH_S16:
        gen_helper_dspv2_paddh_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_ADDH_U32:
        dspv2_insn_addh_u32(rz, rx, ry);
        break;
    case OP_ADDH_S32:
        dspv2_insn_addh_s32(rz, rx, ry);
        break;
    case OP_PSUBH_U8:
        gen_helper_dspv2_psubh_u8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSUBH_S8:
        gen_helper_dspv2_psubh_s8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSUBH_U16:
        gen_helper_dspv2_psubh_u16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSUBH_S16:
        gen_helper_dspv2_psubh_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_SUBH_U32:
        dspv2_insn_subh_u32(rz, rx, ry);
        break;
    case OP_SUBH_S32:
        dspv2_insn_subh_s32(rz, rx, ry);
        break;
    case OP_ADD_64_1st:
    case OP_ADD_64_2nd:
        dspv2_insn_add_64(rz, rx, ry);
        break;
    case OP_SUB_64_1st:
    case OP_SUB_64_2nd:
        dspv2_insn_sub_64(rz, rx, ry);
        break;
    case OP_ADD_U64_S:
        dspv2_insn_add_u64_s(rz, rx, ry);
        break;
    case OP_ADD_S64_S:
        dspv2_insn_add_s64_s(rz, rx, ry);
        break;
    case OP_SUB_U64_S:
        dspv2_insn_sub_u64_s(rz, rx, ry);
        break;
    case OP_SUB_S64_S:
        dspv2_insn_sub_s64_s(rz, rx, ry);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n",
                      s->pc, insn);
        break;
    }
}

static inline void dspv2_insn_pasx_16(int rz, int rx, int ry)
{
    /* rz[31:16] = (rx[31:16] + ry[15:0]),
     * rz[15:0] = (rx[15:0] - ry[31:16]) */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff0000);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff);
    tcg_gen_shli_i32(t1, t1, 16);
    tcg_gen_add_i32(t2, t0, t1);
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff0000);
    tcg_gen_shri_i32(t1, t1, 16);
    tcg_gen_sub_i32(t0, t0, t1);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_or_i32(cpu_R[rz], t2, t0);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static inline void dspv2_insn_psax_16(int rz, int rx, int ry)
{
    /* rz[31:16] = (rx[31:16] - ry[15:0]),
     * rz[15:0] = (rx[15:0] + ry[31:16]) */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff0000);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff);
    tcg_gen_shli_i32(t1, t1, 16);
    tcg_gen_sub_i32(t2, t0, t1);
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff0000);
    tcg_gen_shri_i32(t1, t1, 16);
    tcg_gen_add_i32(t0, t0, t1);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_or_i32(cpu_R[rz], t2, t0);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static inline void dspv2_insn_max_u32(int rz, int rx, int ry)
{
    /* rz = max(rx, ry), unsigned */
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_brcond_i32(TCG_COND_GTU, cpu_R[rx], cpu_R[ry], l1);
    tcg_gen_mov_i32(cpu_R[rz], cpu_R[ry]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_mov_i32(cpu_R[rz], cpu_R[rx]);
    gen_set_label(l2);
}

static inline void dspv2_insn_max_s32(int rz, int rx, int ry)
{
    /* rz = max(rx, ry), signed */
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_brcond_i32(TCG_COND_GT, cpu_R[rx], cpu_R[ry], l1);
    tcg_gen_mov_i32(cpu_R[rz], cpu_R[ry]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_mov_i32(cpu_R[rz], cpu_R[rx]);
    gen_set_label(l2);
}

static inline void dspv2_insn_pmax_u16(int rz, int rx, int ry)
{
    /* rz = {max(rx[31:16], ry[31:16], max(rx[15:0], ry[15:0])}, unsigned */
    TCGv_i32 t0 = tcg_temp_local_new_i32();
    TCGv_i32 t1 = tcg_temp_local_new_i32();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    /* lo_rz = max(lo_rx, lo_ry) */
    tcg_gen_ext16u_i32(t0, cpu_R[rx]);
    tcg_gen_ext16u_i32(t1, cpu_R[ry]);
    tcg_gen_brcond_i32(TCG_COND_GTU, t0, t1, l1);
    tcg_gen_mov_i32(t0, t1);
    gen_set_label(l1);
    /* hi_rz = max(hi_rx, hi_ry) */
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_andi_i32(cpu_R[rz], cpu_R[ry], 0xffff0000);
    tcg_gen_brcond_i32(TCG_COND_GTU, t1, cpu_R[rz], l2);
    tcg_gen_mov_i32(t1, cpu_R[rz]);
    gen_set_label(l2);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
}

static inline void dspv2_insn_pmax_s16(int rz, int rx, int ry)
{
    /* rz = {max(rx[31:16], ry[31:16], max(rx[15:0], ry[15:0])}, signed */
    TCGv_i32 t0 = tcg_temp_local_new_i32();
    TCGv_i32 t1 = tcg_temp_local_new_i32();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    /* lo_rz = max(lo_rx, lo_ry) */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_brcond_i32(TCG_COND_GT, t0, t1, l1);
    tcg_gen_mov_i32(t0, t1);
    gen_set_label(l1);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    /* hi_rz = max(hi_rx, hi_ry) */
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_andi_i32(cpu_R[rz], cpu_R[ry], 0xffff0000);
    tcg_gen_brcond_i32(TCG_COND_GT, t1, cpu_R[rz], l2);
    tcg_gen_mov_i32(t1, cpu_R[rz]);
    gen_set_label(l2);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
}

static inline void dspv2_insn_min_u32(int rz, int rx, int ry)
{
    /* rz = min(rx, ry), unsigned */
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_brcond_i32(TCG_COND_LTU, cpu_R[rx], cpu_R[ry], l1);
    tcg_gen_mov_i32(cpu_R[rz], cpu_R[ry]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_mov_i32(cpu_R[rz], cpu_R[rx]);
    gen_set_label(l2);
}

static inline void dspv2_insn_min_s32(int rz, int rx, int ry)
{
    /* rz = min(rx, ry), signed */
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_brcond_i32(TCG_COND_LT, cpu_R[rx], cpu_R[ry], l1);
    tcg_gen_mov_i32(cpu_R[rz], cpu_R[ry]);
    tcg_gen_br(l2);
    gen_set_label(l1);
    tcg_gen_mov_i32(cpu_R[rz], cpu_R[rx]);
    gen_set_label(l2);
}

static inline void dspv2_insn_pmin_u16(int rz, int rx, int ry)
{
    /* rz = {min(rx[31:16], ry[31:16], min(rx[15:0], ry[15:0])}, unsigned */
    TCGv_i32 t0 = tcg_temp_local_new_i32();
    TCGv_i32 t1 = tcg_temp_local_new_i32();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    /* lo_rz = min(lo_rx, lo_ry) */
    tcg_gen_ext16u_i32(t0, cpu_R[rx]);
    tcg_gen_ext16u_i32(t1, cpu_R[ry]);
    tcg_gen_brcond_i32(TCG_COND_LTU, t0, t1, l1);
    tcg_gen_mov_i32(t0, t1);
    gen_set_label(l1);
    /* hi_rz = min(hi_rx, hi_ry) */
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_andi_i32(cpu_R[rz], cpu_R[ry], 0xffff0000);
    tcg_gen_brcond_i32(TCG_COND_LTU, t1, cpu_R[rz], l2);
    tcg_gen_mov_i32(t1, cpu_R[rz]);
    gen_set_label(l2);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
}

static inline void dspv2_insn_pmin_s16(int rz, int rx, int ry)
{
    /* rz = {min(rx[31:16], ry[31:16], min(rx[15:0], ry[15:0])}, signed */
    TCGv_i32 t0 = tcg_temp_local_new_i32();
    TCGv_i32 t1 = tcg_temp_local_new_i32();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    /* lo_rz = min(lo_rx, lo_ry) */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_brcond_i32(TCG_COND_LT, t0, t1, l1);
    tcg_gen_mov_i32(t0, t1);
    gen_set_label(l1);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    /* hi_rz = min(hi_rx, hi_ry) */
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_andi_i32(cpu_R[rz], cpu_R[ry], 0xffff0000);
    tcg_gen_brcond_i32(TCG_COND_LT, t1, cpu_R[rz], l2);
    tcg_gen_mov_i32(t1, cpu_R[rz]);
    gen_set_label(l2);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
}

static inline void dspv2_sop_cmp(CPUState *cs,
                                  DisasContext *s, uint32_t insn)
{
    int thop, rz, rx, ry;
    thop = (insn >> CSKY_DSPV2_THOP_SHI) & CSKY_DSPV2_THOP_MASK;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;

    switch (thop) {
    case OP_PASX_16_1st:
    case OP_PASX_16_2nd:
        dspv2_insn_pasx_16(rz, rx, ry);
        break;
    case OP_PSAX_16_1st:
    case OP_PSAX_16_2nd:
        dspv2_insn_psax_16(rz, rx, ry);
        break;
    case OP_PASX_U16_S:
        gen_helper_dspv2_pasx_u16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PASX_S16_S:
        gen_helper_dspv2_pasx_s16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSAX_U16_S:
        gen_helper_dspv2_psax_u16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSAX_S16_S:
        gen_helper_dspv2_psax_s16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PASXH_U16:
        gen_helper_dspv2_pasxh_u16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PASXH_S16:
        gen_helper_dspv2_pasxh_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSAXH_U16:
        gen_helper_dspv2_psaxh_u16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSAXH_S16:
        gen_helper_dspv2_psaxh_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPNE_8_1st:
    case OP_PCMPNE_8_2nd:
        gen_helper_dspv2_pcmpne_8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPNE_16_1st:
    case OP_PCMPNE_16_2nd:
        gen_helper_dspv2_pcmpne_16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPHS_U8:
        gen_helper_dspv2_pcmphs_u8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPHS_S8:
        gen_helper_dspv2_pcmphs_s8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPHS_U16:
        gen_helper_dspv2_pcmphs_u16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPHS_S16:
        gen_helper_dspv2_pcmphs_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPLT_U8:
        gen_helper_dspv2_pcmplt_u8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPLT_S8:
        gen_helper_dspv2_pcmplt_s8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPLT_U16:
        gen_helper_dspv2_pcmplt_u16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCMPLT_S16:
        gen_helper_dspv2_pcmplt_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PMAX_U8:
        gen_helper_dspv2_pmax_u8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PMAX_S8:
        gen_helper_dspv2_pmax_s8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PMAX_U16:
        dspv2_insn_pmax_u16(rz, rx, ry);
        break;
    case OP_PMAX_S16:
        dspv2_insn_pmax_s16(rz, rx, ry);
        break;
    case OP_MAX_U32:
        dspv2_insn_max_u32(rz, rx, ry);
        break;
    case OP_MAX_S32:
        dspv2_insn_max_s32(rz, rx, ry);
        break;
    case OP_PMIN_U8:
        gen_helper_dspv2_pmin_u8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PMIN_S8:
        gen_helper_dspv2_pmin_s8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PMIN_U16:
        dspv2_insn_pmin_u16(rz, rx, ry);
        break;
    case OP_PMIN_S16:
        dspv2_insn_pmin_s16(rz, rx, ry);
        break;
    case OP_MIN_U32:
        dspv2_insn_min_u32(rz, rx, ry);
        break;
    case OP_MIN_S32:
        dspv2_insn_min_s32(rz, rx, ry);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n",
                      s->pc, insn);
        break;
    }
}

static inline void dspv2_insn_sel(int rz, int rx, int ry, int rs)
{
    /* for(i=0;i=31;i++) Rz[i] = Rs[i] ? Rx[i] : Ry[i] */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_and_i32(t0, cpu_R[rx], cpu_R[rs]);
    tcg_gen_not_i32(t1, cpu_R[rs]);
    tcg_gen_and_i32(cpu_R[rz], cpu_R[ry], t1);
    tcg_gen_or_i32(cpu_R[rz], cpu_R[rz], t0);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
}

static inline void dspv2_sop_sel(CPUState *cs,
                                 DisasContext *s, uint32_t insn)
{
    int rz, rx, ry, rs;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;
    rs = (insn >> 5) & CSKY_DSPV2_REG_MASK;
    if ((insn & (1 << 10)) == 0) {
        dspv2_insn_sel(rz, rx, ry, rs);
    } else {
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n",
                      s->pc, insn);
    }
}

static inline void dspv2_insn_mulaca_s8(int rz, int rx, int ry)
{
    /* rz = rx[7:0] * ry[7:0] + rx[15:8] * ry[15:8]
     *  + rx[23:16] * ry[23:16] + rx[31:24] * ry[31:24], signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();

    /* rz = rz + ll_x * ll_y */
    tcg_gen_ext8s_i32(t0, cpu_R[rx]);
    tcg_gen_ext8s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t2, t0, t1);
    /* rz = rz + lh_x * lh_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 8);
    tcg_gen_ext8s_i32(t0, t0);
    tcg_gen_sari_i32(t1, cpu_R[ry], 8);
    tcg_gen_ext8s_i32(t1, t1);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_add_i32(t2, t2, t0);
    /* rz= rz + hl_x * hl_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_ext8s_i32(t0, t0);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_ext8s_i32(t1, t1);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_add_i32(t2, t2, t0);
    /* rz = rz +hh_x * hh_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 24);
    tcg_gen_ext8s_i32(t0, t0);
    tcg_gen_sari_i32(t1, cpu_R[ry], 24);
    tcg_gen_ext8s_i32(t1, t1);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_add_i32(cpu_R[rz], t2, t0);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
}

static inline void dspv2_insn_divul(DisasContext *ctx, int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rx+1, rx} / ry */
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_R[ry], 0, l1);
    TCGv_i64  t0 = tcg_temp_new_i64();
    TCGv_i64  t1 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(t0, cpu_R[rx], cpu_R[(rx + 1) % 32]);
    tcg_gen_extu_i32_i64(t1, cpu_R[ry]);
    tcg_gen_divu_i64(t0, t0, t1);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_gen_br(l2);
    gen_set_label(l1);

    TCGv_i32 t2 = tcg_temp_new();
    t2 = tcg_const_i32(EXCP_CSKY_DIV);
    gen_save_pc(ctx->pc);
    gen_helper_exception(cpu_env, t2);
    ctx->is_jmp = DISAS_NEXT;
    tcg_temp_free(t2);
    gen_set_label(l2);
}

static inline void dspv2_insn_divsl(DisasContext *ctx, int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rx+1, rx} / ry */
    TCGv_i64  t0 = tcg_temp_local_new_i64();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();
    TCGLabel *l3 = gen_new_label();
    tcg_gen_concat_i32_i64(t0, cpu_R[rx], cpu_R[(rx + 1) % 32]);
    tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_R[ry], 0, l1);
    tcg_gen_brcondi_i64(TCG_COND_NE, t0, 0x8000000000000000, l3);
    tcg_gen_brcondi_i32(TCG_COND_NE, cpu_R[ry], 0xffffffff, l3);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_gen_br(l2);

    gen_set_label(l3);
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_div_i64(t0, t0, t1);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_gen_br(l2);
    gen_set_label(l1);

    TCGv_i32 t2 = tcg_temp_new();
    t2 = tcg_const_i32(EXCP_CSKY_DIV);
    gen_save_pc(ctx->pc);
    gen_helper_exception(cpu_env, t2);
    ctx->is_jmp = DISAS_NEXT;
    tcg_temp_free(t2);

    gen_set_label(l2);
}

static inline void dspv2_sop_misc(CPUState *cs,
                                  DisasContext *s, uint32_t insn)
{
    int thop, rz, rx, ry;
    thop = (insn >> CSKY_DSPV2_THOP_SHI) & CSKY_DSPV2_THOP_MASK;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;

    switch (thop) {
    case OP_PSABSA_U8_1st:
    case OP_PSABSA_U8_2nd:
        gen_helper_dspv2_psabsa_u8(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PSABSAA_U8_1st:
    case OP_PSABSAA_U8_2nd:
        gen_helper_dspv2_psabsaa_u8(cpu_R[rz], cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_DIVUL:
        dspv2_insn_divul(s, rz, rx, ry);
        break;
    case OP_DIVSL:
        dspv2_insn_divsl(s, rz, rx, ry);
        break;
    case OP_MULACA_S8:
        dspv2_insn_mulaca_s8(rz, rx, ry);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n",
                      s->pc, insn);
        break;
    }
}

static inline void dspv2_insn_asri_s32_r(int rz, int rx, int imm)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_shli_i64(t0, t0, 32);
    tcg_gen_sari_i64(t0, t0, imm);
    tcg_gen_addi_i64(t0, t0, 0x80000000);
    tcg_gen_extrh_i64_i32(cpu_R[rz], t0);
    tcg_temp_free_i64(t0);
}

static inline void dspv2_insn_asr_s32_r(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_andi_i64(t1, t1, 0x3f);
    tcg_gen_shli_i64(t0, t0, 32);
    tcg_gen_sar_i64(t0, t0, t1);
    tcg_gen_addi_i64(t0, t0, 0x80000000);
    tcg_gen_extrh_i64_i32(cpu_R[rz], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_lsri_u32_r(int rz, int rx, int imm)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_shli_i64(t0, t0, 32);
    tcg_gen_shri_i64(t0, t0, imm);
    tcg_gen_addi_i64(t0, t0, 0x80000000);
    tcg_gen_extrh_i64_i32(cpu_R[rz], t0);
    tcg_temp_free_i64(t0);
}

static inline void dspv2_insn_lsr_u32_r(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_andi_i64(t1, t1, 0x3f);
    tcg_gen_shli_i64(t0, t0, 32);
    tcg_gen_shr_i64(t0, t0, t1);
    tcg_gen_addi_i64(t0, t0, 0x80000000);
    tcg_gen_extrh_i64_i32(cpu_R[rz], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_pasri_s16(int rz, int rx, int imm)
{
    /* rz = {rx[31:16] >> imm, rx[15:0] >> imm} */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_sari_i32(t0, t0, imm);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_sari_i32(t1, t1, imm);
    tcg_gen_andi_i32(t1, t1, 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
}

static inline void dspv2_insn_pasr_s16(int rz, int rx, int ry)
{
    /* rz = {rx[31:16] >> ry, rx[15:0] >> ry} */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t2, cpu_R[ry], 0x1f);
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_sar_i32(t0, t0, t2);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_sar_i32(t1, t1, t2);
    tcg_gen_andi_i32(t1, t1, 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static inline void dspv2_insn_pasri_s16_r(int rz, int rx, int imm)
{
    /* rz[31:16] = Round(rx[31:16] >> imm),
     * rz[15:0] = Round(rx[15:0] >> imm) */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_shri_i32(t2, t0, imm - 1);
    tcg_gen_andi_i32(t2, t2, 0x1);
    tcg_gen_sari_i32(t0, t0, imm);
    tcg_gen_add_i32(t0, t0, t2);
    tcg_gen_andi_i32(t0, t0, 0xffff);

    tcg_gen_sari_i32(t1, cpu_R[rx], imm);
    tcg_gen_addi_i32(t1, t1, 0x8000);
    tcg_gen_andi_i32(t1, t1, 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static inline void dspv2_insn_pasr_s16_r(int rz, int rx, int ry)
{
    /* rz[31:16] = Round(rx[31:16] >> imm),
     * rz[15:0] = Round(rx[15:0] >> imm) */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    TCGv_i32 t3 = tcg_temp_new_i32();

    tcg_gen_andi_i32(t3, cpu_R[ry], 0x1f);
    tcg_gen_andi_i32(t2, cpu_R[rx], 0xffff);
    tcg_gen_shli_i32(t2, t2, 16);
    tcg_gen_sar_i32(t2, t2, t3);
    tcg_gen_addi_i32(t2, t2, 0x8000);
    tcg_gen_shri_i32(t2, t2, 16);
    tcg_gen_andi_i32(t0, t2, 0xffff);

    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_sar_i32(t1, t1, t3);
    tcg_gen_addi_i32(t1, t1, 0x8000);
    tcg_gen_andi_i32(t1, t1, 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t3);
}

static inline void dspv2_insn_plsri_u16(int rz, int rx, int imm)
{
    /* rz = {rx[31:16] >> imm, rx[15:0] >> imm} logical shift */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_ext16u_i32(t0, cpu_R[rx]);
    tcg_gen_shri_i32(t0, t0, imm);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_shri_i32(t1, t1, imm);
    tcg_gen_andi_i32(t1, t1, 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
}

static inline void dspv2_insn_plsr_u16(int rz, int rx, int ry)
{
    /* rz = {rx[31:16] >> imm, rx[15:0] >> imm} logical shift */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t2, cpu_R[ry], 0x1f);
    tcg_gen_ext16u_i32(t0, cpu_R[rx]);
    tcg_gen_shr_i32(t0, t0, t2);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_shr_i32(t1, t1, t2);
    tcg_gen_andi_i32(t1, t1, 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static inline void dspv2_insn_plsri_u16_r(int rz, int rx, int imm)
{
    /* rz[31:16] = Round(rx[31:16] >> imm),
     * rz[15:0] = Round(rx[15:0] >> imm) */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i32 t3 = tcg_temp_new_i32();

    tcg_gen_andi_i32(t3, cpu_R[rx], 0xffff);
    tcg_gen_ext_i32_i64(t0, t3);
    tcg_gen_shli_i64(t0, t0, 32);
    tcg_gen_shri_i64(t0, t0, imm);
    tcg_gen_addi_i64(t0, t0, 0x80000000);
    tcg_gen_andi_i64(t0, t0, 0xffff00000000);


    tcg_gen_andi_i32(t3, cpu_R[rx], 0xffff0000);
    tcg_gen_ext_i32_i64(t1, t3);
    tcg_gen_shli_i64(t1, t1, 32);
    tcg_gen_shri_i64(t1, t1, imm);
    tcg_gen_addi_i64(t1, t1, 0x800000000000);
    tcg_gen_andi_i64(t1, t1, 0xffff000000000000);


    tcg_gen_or_i64(t2, t0, t1);
    tcg_gen_extrh_i64_i32(cpu_R[rz], t2);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free_i32(t3);
}

static inline void dspv2_insn_plsr_u16_r(int rz, int rx, int ry)
{
    /* rz[31:16] = Round(rx[31:16] >> imm),
     * rz[15:0] = Round(rx[15:0] >> imm) */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i32 t3 = tcg_temp_new_i32();
    TCGv_i32 t4 = tcg_temp_new_i32();

    tcg_gen_andi_i32(t3, cpu_R[ry], 0x1f);
    tcg_gen_ext_i32_i64(t1, t3);
    tcg_gen_andi_i32(t4, cpu_R[rx], 0xffff);
    tcg_gen_ext_i32_i64(t0, t4);
    tcg_gen_shli_i64(t0, t0, 32);
    tcg_gen_shr_i64(t0, t0, t1);
    tcg_gen_addi_i64(t0, t0, 0x80000000);
    tcg_gen_andi_i64(t0, t0, 0xffff00000000);


    tcg_gen_andi_i32(t4, cpu_R[rx], 0xffff0000);
    tcg_gen_ext_i32_i64(t2, t4);
    tcg_gen_shli_i64(t2, t2, 32);
    tcg_gen_shr_i64(t2, t2, t1);
    tcg_gen_addi_i64(t2, t2, 0x800000000000);
    tcg_gen_andi_i64(t2, t2, 0xffff000000000000);


    tcg_gen_or_i64(t1, t0, t2);
    tcg_gen_extrh_i64_i32(cpu_R[rz], t1);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free_i32(t3);
    tcg_temp_free_i32(t4);

}

static inline void dspv2_insn_plsli_u16(int rz, int rx, int imm)
{
    /* rz = {rx[31:16] << imm, rx[15:0] << imm} */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff);
    tcg_gen_shli_i32(t0, t0, imm);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_shli_i32(t1, t1, imm);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
}

static inline void dspv2_insn_plsl_u16(int rz, int rx, int ry)
{
    /* rz = {rx[31:16] >> imm, rx[15:0] >> imm}, logical shift */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t2, cpu_R[ry], 0x1f);
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff);
    tcg_gen_shl_i32(t0, t0, t2);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[rx], 0xffff0000);
    tcg_gen_shl_i32(t1, t1, t2);
    tcg_gen_andi_i32(t1, t1, 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static inline void dspv2_sop_shift(CPUState *cs,
                                  DisasContext *s, uint32_t insn)
{
    int thop, rz, rx, ry, imm;
    TCGv t0;
    thop = (insn >> CSKY_DSPV2_THOP_SHI) & CSKY_DSPV2_THOP_MASK;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;
    imm = ry + 1;  /* bits ry equal to oimm. */

    switch (thop) {
    case OP_ASRI_S32_R:
        dspv2_insn_asri_s32_r(rz, rx, imm);
        break;
    case OP_ASR_S32_R:
        dspv2_insn_asr_s32_r(rz, rx, ry);
        break;
    case OP_LSRI_U32_R:
        dspv2_insn_lsri_u32_r(rz, rx, imm);
        break;
    case OP_LSR_U32_R:
        dspv2_insn_lsr_u32_r(rz, rx, ry);
        break;
    case OP_LSLI_U32_S:
        t0 = tcg_temp_new_i32();
        t0 = tcg_const_tl(imm);
        gen_helper_dspv2_lsli_u32_s(cpu_R[rz], cpu_R[rx], t0);
        tcg_temp_free_i32(t0);
        break;
    case OP_LSLI_S32_S:
        t0 = tcg_temp_new_i32();
        t0 = tcg_const_tl(imm);
        gen_helper_dspv2_lsli_s32_s(cpu_R[rz], cpu_R[rx], t0);
        tcg_temp_free_i32(t0);
        break;
    case OP_LSL_U32_S:
        gen_helper_dspv2_lsl_u32_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_LSL_S32_S:
        gen_helper_dspv2_lsl_s32_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PASRI_S16:
        dspv2_insn_pasri_s16(rz, rx, imm);
        break;
    case OP_PASR_S16:
        dspv2_insn_pasr_s16(rz, rx, ry);
        break;
    case OP_PASRI_S16_R:
        dspv2_insn_pasri_s16_r(rz, rx, imm);
        break;
    case OP_PASR_S16_R:
        dspv2_insn_pasr_s16_r(rz, rx, ry);
        break;
    case OP_PLSRI_U16:
        dspv2_insn_plsri_u16(rz, rx, imm);
        break;
    case OP_PLSR_U16:
        dspv2_insn_plsr_u16(rz, rx, ry);
        break;
    case OP_PLSRI_U16_R:
        dspv2_insn_plsri_u16_r(rz, rx, imm);
        break;
    case OP_PLSR_U16_R:
        dspv2_insn_plsr_u16_r(rz, rx, ry);
        break;
    case OP_PLSLI_U16:
        dspv2_insn_plsli_u16(rz, rx, imm);
        break;
    case OP_PLSL_U16:
        dspv2_insn_plsl_u16(rz, rx, ry);
        break;
    case OP_PLSLI_U16_S:
        t0 = tcg_temp_new_i32();
        t0 = tcg_const_tl(imm);
        gen_helper_dspv2_plsli_u16_s(cpu_R[rz], cpu_R[rx], t0);
        tcg_temp_free_i32(t0);
        break;
    case OP_PLSLI_S16_S:
        t0 = tcg_temp_new_i32();
        t0 = tcg_const_tl(imm);
        gen_helper_dspv2_plsli_s16_s(cpu_R[rz], cpu_R[rx], t0);
        tcg_temp_free_i32(t0);
        break;
    case OP_PLSL_U16_S:
        gen_helper_dspv2_plsl_u16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PLSL_S16_S:
        gen_helper_dspv2_plsl_s16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n",
                      s->pc, insn);
        break;
    }
}

static inline void dspv2_insn_pkg(int rz, int rx, int imm4a, int ry, int imm4b)
{
    /* rz = {(ry >> imm4b)[15:0], (rx >> imm4a)[15:0]}, logical shift */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_shri_i32(t0, cpu_R[rx], imm4a);
    tcg_gen_andi_i32(t0, t0, 0xffff);
    tcg_gen_shli_i32(t1, cpu_R[ry], 16 - imm4b);
    tcg_gen_andi_i32(t1, t1, 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
}

static inline void dspv2_sop_pkg(CPUState *cs,
                                 DisasContext *s, uint32_t insn)
{
    int rz, rx, ry, imm4a, imm4b;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;
    imm4a = (insn >> 5) & 0xf;
    imm4b = ((insn >> 9) & 0xf) + 1;
    dspv2_insn_pkg(rz, rx, imm4a, ry, imm4b);

}

static inline void dspv2_insn_dexti(int rz, int rx, int ry, int imm)
{
        /* rz = ({ry, rx} >> imm)[31:0] */
        TCGv_i64 t0 = tcg_temp_new_i64();
        tcg_gen_concat_i32_i64(t0, cpu_R[rx], cpu_R[ry]);
        tcg_gen_shri_i64(t0, t0, imm);
        tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
        tcg_temp_free_i64(t0);
}

static inline void dspv2_insn_dext(int rz, int rx, int ry, int rs)
{
        /* rz = ({ry, rx} >> rs)[31:0]
         * NOTE: 0 <= rs <= 32. */
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i32 t1 = tcg_temp_new_i32();
        TCGv_i64 t2 = tcg_temp_new_i64();
        tcg_gen_andi_i32(t1, cpu_R[rs], 0x3f);
        tcg_gen_ext_i32_i64(t2, t1);
        tcg_gen_concat_i32_i64(t0, cpu_R[rx], cpu_R[ry]);
        tcg_gen_shr_i64(t0, t0, t2);
        tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
        tcg_temp_free_i64(t0);
        tcg_temp_free_i32(t1);
        tcg_temp_free_i64(t2);
}

static inline void dspv2_sop_dext(CPUState *cs,
                                 DisasContext *s, uint32_t insn)
{
    int rz, rx, ry;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;
    if ((insn & (1 << 10)) == 0) {
        /* dexti */
        int imm5 = (insn >> 5) & 0x1f;
        dspv2_insn_dexti(rz, rx, ry, imm5);
    } else {
        /* dext */
        int rs = (insn >> 5) & CSKY_DSPV2_REG_MASK;
        dspv2_insn_dext(rz, rx, ry, rs);
    }
}

static inline void dspv2_insn_pkgll(int rz, int rx, int ry)
{
    /* rz = {ry[15:0], rx[15:0]} */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff);
    tcg_gen_shli_i32(t1, t1, 16);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
}

static inline void dspv2_insn_pkghh(int rz, int rx, int ry)
{
    /* rz = {ry[31:16], rx[31:16]} */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, cpu_R[rx], 0xffff0000);
    tcg_gen_shri_i32(t0, t0, 16);
    tcg_gen_andi_i32(t1, cpu_R[ry], 0xffff0000);
    tcg_gen_or_i32(cpu_R[rz], t0, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
}

static inline void dspv2_sop_pkg_clip(CPUState *cs,
                                      DisasContext *s, uint32_t insn)
{
    int thop, rz, rx, ry, index;
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i64 t1 = tcg_temp_new_i64();
    thop = (insn >> CSKY_DSPV2_THOP_SHI) & CSKY_DSPV2_THOP_MASK;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;

    switch (thop) {
    case OP_PKGLL_1st:
    case OP_PKGLL_2nd:
        dspv2_insn_pkgll(rz, rx, ry);
        break;
    case OP_PKGHH_1st:
    case OP_PKGHH_2nd:
        dspv2_insn_pkghh(rz, rx, ry);
        break;
    case OP_PEXT_U8_E:
        if (ry == 0) {
            gen_helper_dspv2_pext_u8_e(t1, cpu_R[rx]);
            tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t1);
        } else {
            goto wrong;
        }
        break;
    case OP_PEXT_S8_E:
        if (ry == 0) {
            gen_helper_dspv2_pext_s8_e(t1, cpu_R[rx]);
            tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t1);
        } else {
            goto wrong;
        }
        break;
    case OP_PEXTX_U8_E:
        if (ry == 0) {
            gen_helper_dspv2_pextx_u8_e(t1, cpu_R[rx]);
            tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t1);
        } else {
            goto wrong;
        }
        break;
    case OP_PEXTX_S8_E:
        if (ry == 0) {
            gen_helper_dspv2_pextx_s8_e(t1, cpu_R[rx]);
            tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t1);
        } else {
            goto wrong;
        }
        break;
    case OP_NARL_1st:
    case OP_NARL_2nd:
        gen_helper_dspv2_narl(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_NARH_1st:
    case OP_NARH_2nd:
        gen_helper_dspv2_narh(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_NARLX_1st:
    case OP_NARLX_2nd:
        gen_helper_dspv2_narlx(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_NARHX_1st:
    case OP_NARHX_2nd:
        gen_helper_dspv2_narhx(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_CLIPI_U32:
        t0 = tcg_const_i32(ry);
        gen_helper_dspv2_clipi_u32(cpu_R[rz], cpu_R[rx], t0);
        break;
    case OP_CLIPI_S32:
        t0 = tcg_const_i32(ry);
        gen_helper_dspv2_clipi_s32(cpu_R[rz], cpu_R[rx], t0);
        break;
    case OP_CLIP_U32:
        gen_helper_dspv2_clip_u32(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_CLIP_S32:
        gen_helper_dspv2_clip_s32(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCLIPI_U16:
        t0 = tcg_const_i32(ry);
        gen_helper_dspv2_pclipi_u16(cpu_R[rz], cpu_R[rx], t0);
        break;
    case OP_PCLIPI_S16:
        t0 = tcg_const_i32(ry);
        gen_helper_dspv2_pclipi_s16(cpu_R[rz], cpu_R[rx], t0);
        break;
    case OP_PCLIP_U16:
        gen_helper_dspv2_pclip_u16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PCLIP_S16:
        gen_helper_dspv2_pclip_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PABS_S8_S:
        if (ry == 0) {
            gen_helper_dspv2_pabs_s8_s(cpu_R[rz], cpu_R[rx]);
        } else {
            goto wrong;
        }
        break;
    case OP_PABS_S16_S:
        if (ry == 0) {
            gen_helper_dspv2_pabs_s16_s(cpu_R[rz], cpu_R[rx]);
        } else {
            goto wrong;
        }
        break;
    case OP_ABS_S32_S:
        if (ry == 0) {
            gen_helper_dspv2_abs_s32_s(cpu_R[rz], cpu_R[rx]);
        } else {
            goto wrong;
        }
        break;
    case OP_PNEG_S8_S:
        if (ry == 0) {
            gen_helper_dspv2_pneg_s8_s(cpu_R[rz], cpu_R[rx]);
        } else {
            goto wrong;
        }
        break;
    case OP_PNEG_S16_S:
        if (ry == 0) {
            gen_helper_dspv2_pneg_s16_s(cpu_R[rz], cpu_R[rx]);
        } else {
            goto wrong;
        }
        break;
    case OP_NEG_S32_S:
        if (ry == 0) {
            gen_helper_dspv2_neg_s32_s(cpu_R[rz], cpu_R[rx]);
        } else {
            goto wrong;
        }
        break;
    case OP_DUP_8_begin ... OP_DUP_8_end:
        if (ry == 0) {
            index = (insn >> 5) & 0x3;
            t0 = tcg_const_i32(index);
            gen_helper_dspv2_dup_8(cpu_R[rz], cpu_R[rx], t0);
        } else {
            goto wrong;
        }
        break;
    case OP_DUP_16_begin ... OP_DUP_16_end:
        if (ry == 0) {
            index = (insn >> 5) & 0x1;
            t0 = tcg_const_i32(index);
            gen_helper_dspv2_dup_16(cpu_R[rz], cpu_R[rx], t0);
        } else {
            goto wrong;
        }
        break;
    default:
wrong:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n",
                      s->pc, insn);
        break;
    }
    tcg_temp_free_i32(t0);
}

static inline void dspv2_insn_mul_u32(int rz, int rx, int ry)
{
    tcg_gen_mulu2_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], cpu_R[rx], cpu_R[ry]);
}

static inline void dspv2_insn_mul_s32(int rz, int rx, int ry)
{
    tcg_gen_muls2_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], cpu_R[rx], cpu_R[ry]);
}

static inline void dspv2_insn_mula_u32(int rz, int rx, int ry)
{
    /* {r[z+1],rz} = (rx * ry) + {r[z+1],rz}, unsigned */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(t0, cpu_R[rx]);
    tcg_gen_extu_i32_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_concat_i32_i64(t1, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_mula_s32(int rz, int rx, int ry)
{
    /* {r[z+1],rz} = (rx * ry) + {r[z+1],rz}, signed */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_concat_i32_i64(t1, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    tcg_gen_add_i64(t0, t0, t1);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_muls_u32(int rz, int rx, int ry)
{
    /* {Rz+1[31:0],Rz[31:0]} = {Rz+1[31:0],Rz[31:0]} + Rx[31:0] X Ry[31:0] */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(t0, cpu_R[rx]);
    tcg_gen_extu_i32_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_concat_i32_i64(t1, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    tcg_gen_sub_i64(t0, t1, t0);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_muls_s32(int rz, int rx, int ry)
{
    /* {Rz+1[31:0],Rz[31:0]} = {Rz+1[31:0],Rz[31:0]} + Rx[31:0] X Ry[31:0] */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_concat_i32_i64(t1, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    tcg_gen_sub_i64(t0, t1, t0);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_mula_u32_s(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_helper_dspv2_mula_u32_s(t0, cpu_R[rz], cpu_R[(rz + 1) % 32],
                                cpu_R[rx], cpu_R[ry]);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
}

static inline void dspv2_insn_mula_s32_s(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_helper_dspv2_mula_s32_s(t0, cpu_R[rz], cpu_R[(rz + 1) % 32],
                                cpu_R[rx], cpu_R[ry]);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
}

static inline void dspv2_insn_muls_u32_s(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_helper_dspv2_muls_u32_s(t0, cpu_R[rz], cpu_R[(rz + 1) % 32],
                                cpu_R[rx], cpu_R[ry]);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
}

static inline void dspv2_insn_muls_s32_s(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_helper_dspv2_muls_s32_s(t0, cpu_R[rz], cpu_R[(rz + 1) % 32],
                                cpu_R[rx], cpu_R[ry]);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
}

static inline void dspv2_insn_mul_s32_h(int rz, int rx, int ry)
{
    /* rz = (rx * ry)[63:32], signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    tcg_gen_muls2_i32(t0, cpu_R[rz], cpu_R[rx], cpu_R[ry]);
    tcg_temp_free_i32(t0);
}

static inline void dspv2_insn_mul_s32_rh(int rz, int rx, int ry)
{
    /* rz = (rx * ry + 0x80000000)[63:32] , signed */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_addi_i64(t0, t0, 0x80000000);
    tcg_gen_extrh_i64_i32(cpu_R[rz], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_mulll_s16(int rz, int rx, int ry)
{
    /* rz = rx[15:0] * ry[15:0], signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(cpu_R[rz], cpu_R[ry]);
    tcg_gen_mul_i32(cpu_R[rz], t0, cpu_R[rz]);
    tcg_temp_free_i32(t0);
}

static inline void dspv2_insn_mulhh_s16(int rz, int rx, int ry)
{
    /* rz = rx[31:16] * ry[31:16], signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_sari_i32(cpu_R[rz], cpu_R[ry], 16);
    tcg_gen_mul_i32(cpu_R[rz], t0, cpu_R[rz]);
    tcg_temp_free_i32(t0);
}

static inline void dspv2_insn_mulhl_s16(int rz, int rx, int ry)
{
    /* rz = rx[31:16] * ry[15:0], signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_ext16s_i32(cpu_R[rz], cpu_R[ry]);
    tcg_gen_mul_i32(cpu_R[rz], t0, cpu_R[rz]);
    tcg_temp_free_i32(t0);
}

static inline void dspv2_insn_mulall_s16_e(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_helper_dspv2_mulall_s16_e(t0, cpu_R[rz], cpu_R[(rz + 1) % 32],
                                  cpu_R[rx], cpu_R[ry]);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
}

static inline void dspv2_insn_mulahh_s16_e(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_helper_dspv2_mulahh_s16_e(t0, cpu_R[rz], cpu_R[(rz + 1) % 32],
                                  cpu_R[rx], cpu_R[ry]);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
}

static inline void dspv2_insn_mulahl_s16_e(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_helper_dspv2_mulahl_s16_e(t0, cpu_R[rz], cpu_R[(rz + 1) % 32],
                                  cpu_R[rx], cpu_R[ry]);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
}

static inline void dspv2_insn_pmul_u16(int rz, int rx, int ry)
{
    /* rz = lo_x * lo_y, rn = hi_x * hi_y, unsigned */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    TCGv_i32 t3 = tcg_temp_new_i32();
    /* lo_x, hi_x */
    tcg_gen_ext16u_i32(t2, cpu_R[rx]);
    tcg_gen_shri_i32(t3, cpu_R[rx], 16);
    /* lo_y, hi_y */
    tcg_gen_ext16u_i32(t0, cpu_R[ry]);
    tcg_gen_shri_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(cpu_R[rz], t2, t0);
    tcg_gen_mul_i32(cpu_R[(rz + 1) % 32], t3, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t3);
}

static inline void dspv2_insn_pmul_s16(int rz, int rx, int ry)
{
    /* rz = lo_x * lo_y, rn = hi_x * hi_y, signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    TCGv_i32 t3 = tcg_temp_new_i32();
    /* lo_x, hi_x */
    tcg_gen_ext16s_i32(t2, cpu_R[rx]);
    tcg_gen_sari_i32(t3, cpu_R[rx], 16);
    /* lo_y, hi_y */
    tcg_gen_ext16s_i32(t0, cpu_R[ry]);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(cpu_R[rz], t2, t0);
    tcg_gen_mul_i32(cpu_R[(rz + 1) % 32], t3, t1);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t3);
}

static inline void dspv2_insn_pmulx_u16(int rz, int rx, int ry)
{
    /* rz = lo_x * lo_y, rn = hi_x * hi_y, unsigned */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    TCGv_i32 t3 = tcg_temp_new_i32();
    /* lo_x, hi_x */
    tcg_gen_ext16u_i32(t2, cpu_R[rx]);
    tcg_gen_shri_i32(t3, cpu_R[rx], 16);
    /* lo_y, hi_y */
    tcg_gen_ext16u_i32(t0, cpu_R[ry]);
    tcg_gen_shri_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(cpu_R[rz], t2, t1);
    tcg_gen_mul_i32(cpu_R[(rz + 1) % 32], t3, t0);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t3);
}

static inline void dspv2_insn_pmulx_s16(int rz, int rx, int ry)
{
    /* rz = lo_x * lo_y, rn = hi_x * hi_y, signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    TCGv_i32 t3 = tcg_temp_new_i32();
    /* lo_x, hi_x */
    tcg_gen_ext16s_i32(t2, cpu_R[rx]);
    tcg_gen_sari_i32(t3, cpu_R[rx], 16);
    /* lo_y, hi_y */
    tcg_gen_ext16s_i32(t0, cpu_R[ry]);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(cpu_R[rz], t2, t1);
    tcg_gen_mul_i32(cpu_R[(rz + 1) % 32], t3, t0);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(t3);
}

static inline void dspv2_insn_prmul_s16(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_helper_dspv2_prmul_s16(t0, cpu_R[rx], cpu_R[ry]);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
}

static inline void dspv2_insn_prmulx_s16(int rz, int rx, int ry)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    gen_helper_dspv2_prmulx_s16(t0, cpu_R[rx], cpu_R[ry]);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t0);
    tcg_temp_free_i64(t0);
}

static inline void dspv2_sop_mul_1st(CPUState *cs,
                                     DisasContext *s, uint32_t insn)
{
    int thop, rz, rx, ry;
    thop = (insn >> CSKY_DSPV2_THOP_SHI) & CSKY_DSPV2_THOP_MASK;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;
    if (thop != OP_MULA_32_L && thop != OP_MULALL_S16_S
        && thop != OP_MUL_U32 && thop != OP_MULA_U32
        && thop != OP_MUL_S32 && thop != OP_MULA_S32) {
        check_insn(s, ABIV2_EDSP);
    } else {
        check_insn(s, ABIV2_803S_R1);
    }
    switch (thop) {
    case OP_MUL_U32:
        dspv2_insn_mul_u32(rz, rx, ry);
        break;
    case OP_MUL_S32:
        dspv2_insn_mul_s32(rz, rx, ry);
        break;
    case OP_MULA_U32:
        dspv2_insn_mula_u32(rz, rx, ry);
        break;
    case OP_MULA_S32:
        dspv2_insn_mula_s32(rz, rx, ry);
        break;
    case OP_MULS_U32:
        dspv2_insn_muls_u32(rz, rx, ry);
        break;
    case OP_MULS_S32:
        dspv2_insn_muls_s32(rz, rx, ry);
        break;
    case OP_MULA_U32_S:
        dspv2_insn_mula_u32_s(rz, rx, ry);
        break;
    case OP_MULA_S32_S:
        dspv2_insn_mula_s32_s(rz, rx, ry);
        break;
    case OP_MULS_U32_S:
        dspv2_insn_muls_u32_s(rz, rx, ry);
        break;
    case OP_MULS_S32_S:
        dspv2_insn_muls_s32_s(rz, rx, ry);
        break;
    case OP_MULA_32_L:
        gen_helper_dspv2_mula_32_l(cpu_R[rz], cpu_R[rz],
                                   cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MUL_S32_H:
        dspv2_insn_mul_s32_h(rz, rx, ry);
        break;
    case OP_MUL_S32_RH:
        dspv2_insn_mul_s32_rh(rz, rx, ry);
        break;
    case OP_RMUL_S32_H:
        gen_helper_dspv2_rmul_s32_h(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_RMUL_S32_RH:
        gen_helper_dspv2_rmul_s32_rh(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULA_S32_HS:
        gen_helper_dspv2_mula_s32_hs(cpu_R[rz], cpu_R[rz],
                                     cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULS_S32_HS:
        gen_helper_dspv2_muls_s32_hs(cpu_R[rz], cpu_R[rz],
                                     cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULA_S32_RHS:
        gen_helper_dspv2_mula_s32_rhs(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULS_S32_RHS:
        gen_helper_dspv2_muls_s32_rhs(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULLL_S16:
        dspv2_insn_mulll_s16(rz, rx, ry);
        break;
    case OP_MULHH_S16:
        dspv2_insn_mulhh_s16(rz, rx, ry);
        break;
    case OP_MULHL_S16:
        dspv2_insn_mulhl_s16(rz, rx, ry);
        break;
    case OP_RMULLL_S16:
        gen_helper_dspv2_rmulll_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_RMULHH_S16:
        gen_helper_dspv2_rmulhh_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_RMULHL_S16:
        gen_helper_dspv2_rmulhl_s16(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULALL_S16_S:
        gen_helper_dspv2_mulall_s16_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULAHH_S16_S:
        gen_helper_dspv2_mulahh_s16_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULAHL_S16_S:
        gen_helper_dspv2_mulahl_s16_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULALL_S16_E:
        dspv2_insn_mulall_s16_e(rz, rx, ry);
        break;
    case OP_MULAHH_S16_E:
        dspv2_insn_mulahh_s16_e(rz, rx, ry);
        break;
    case OP_MULAHL_S16_E:
        dspv2_insn_mulahl_s16_e(rz, rx, ry);
        break;
    case OP_PMUL_U16:
        dspv2_insn_pmul_u16(rz, rx, ry);
        break;
    case OP_PMULX_U16:
        dspv2_insn_pmulx_u16(rz, rx, ry);
        break;
    case OP_PMUL_S16:
        dspv2_insn_pmul_s16(rz, rx, ry);
        break;
    case OP_PMULX_S16:
        dspv2_insn_pmulx_s16(rz, rx, ry);
        break;
    case OP_PRMUL_S16:
        dspv2_insn_prmul_s16(rz, rx, ry);
        break;
    case OP_PRMULX_S16:
        dspv2_insn_prmulx_s16(rz, rx, ry);
        break;
    case OP_PRMUL_S16_H:
        gen_helper_dspv2_prmul_s16_h(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PRMUL_S16_RH:
        gen_helper_dspv2_prmul_s16_rh(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PRMULX_S16_H:
        gen_helper_dspv2_prmulx_s16_h(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_PRMULX_S16_RH:
        gen_helper_dspv2_prmulx_s16_rh(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n",
                      s->pc, insn);
        break;
    }
}

static inline void dspv2_insn_mulxl_s32(int rz, int rx, int ry)
{
    /* Rz[31:0] = {Rx[31:0] X Ry[15:0]}[47:16] */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_ext16s_i64(t1, t1);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 16);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_mulxl_s32_r(int rz, int rx, int ry)
{
    /* Rz[31:0] = {Rx[31:0] X Ry[15:0]}[47:16] */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_ext16s_i64(t1, t1);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_addi_i64(t0, t0, 0x8000);
    tcg_gen_shri_i64(t0, t0, 16);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_mulxh_s32(int rz, int rx, int ry)
{
    /* Rz[31:0] = {Rx[31:0] X Ry[15:0]}[47:16] */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_shri_i64(t1, t1, 16);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_shri_i64(t0, t0, 16);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_mulxh_s32_r(int rz, int rx, int ry)
{
    /* Rz[31:0] = {Rx[31:0] X Ry[15:0]}[47:16] */
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(t0, cpu_R[rx]);
    tcg_gen_ext_i32_i64(t1, cpu_R[ry]);
    tcg_gen_shri_i64(t1, t1, 16);
    tcg_gen_mul_i64(t0, t0, t1);
    tcg_gen_addi_i64(t0, t0, 0x8000);
    tcg_gen_shri_i64(t0, t0, 16);
    tcg_gen_extrl_i64_i32(cpu_R[rz], t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static inline void dspv2_insn_mulcs_s16(int rz, int rx, int ry)
{
    /* rz = rx[15:0] * ry[15:0] - rx[31:16] * ry[31:16], signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    /* lo_rx * lo_ry */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t2, t0, t1);
    /* hi_rx * hi_ry */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_sub_i32(cpu_R[rz], t2, t0);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static inline void dspv2_insn_mulcsr_s16(int rz, int rx, int ry)
{
    /* rz = rx[31:16] * ry[31:16] - rx[15:0] * ry[15:0], signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    /* lo_rx * lo_ry */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t2, t0, t1);
    /* hi_rx * hi_ry */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_sub_i32(cpu_R[rz], t0, t2);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static inline void dspv2_insn_mulcsx_s16(int rz, int rx, int ry)
{
    /* rz = rx[15:0] * ry[31:16] - rx[31:16] * ry[15:0], signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    /* lo_rx * hi_ry */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t2, t0, t1);
    /* hi_rx * lo_ry */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_sub_i32(cpu_R[rz], t2, t0);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static inline void dspv2_insn_mulaca_s16_e(int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rz+1, rz} + rx[15:0] * ry[15:0] + rx[31:16] * ry[31:16],
     * signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(t2, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    /* {rz+1, rz} = {rz+1, rz} + lo_x * lo_y */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_add_i64(t2, t2, t3);
    /* {rz+1, rz} = {rz+1, rz} + hi_x * hi_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_add_i64(t2, t2, t3);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t2);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free_i64(t3);
}

static inline void dspv2_insn_mulacax_s16_e(int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rz+1, rz} + rx[31:16] * ry[15:0] + rx[15:0] * ry[31:16],
     * signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(t2, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    /* {rz+1, rz} = {rz+1, rz} + hi_x * lo_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_add_i64(t2, t2, t3);
    /* {rz+1, rz} = {rz+1, rz} + lo_x * hi_y */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_add_i64(t2, t2, t3);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t2);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free_i64(t3);
}

static inline void dspv2_insn_mulacs_s16_e(int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rz+1, rz} + rx[15:0] * ry[15:0] - rx[31:16] * ry[31:16],
     * signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(t2, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    /* {rz+1, rz} = {rz+1, rz} + lo_x * lo_y */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_add_i64(t2, t2, t3);
    /* {rz+1, rz} = {rz+1, rz} - hi_x * hi_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_sub_i64(t2, t2, t3);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t2);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free_i64(t3);
}

static inline void dspv2_insn_mulacsr_s16_e(int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rz+1, rz} + rx[31:16] * ry[31:16] - rx[15:0] * ry[15:0],
     * signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(t2, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    /* {rz+1, rz} = {rz+1, rz} - lo_x * lo_y */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_sub_i64(t2, t2, t3);
    /* {rz+1, rz} = {rz+1, rz} + hi_x * hi_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_add_i64(t2, t2, t3);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t2);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free_i64(t3);
}

static inline void dspv2_insn_mulacsx_s16_e(int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rz+1, rz} + rx[15:0] * ry[31:16] - rx[31:16] * ry[15:0],
     * signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(t2, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    /* {rz+1, rz} = {rz+1, rz} - hi_x * lo_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_sub_i64(t2, t2, t3);
    /* {rz+1, rz} = {rz+1, rz} + lo_x * hi_y */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_add_i64(t2, t2, t3);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t2);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free_i64(t3);
}

static inline void dspv2_insn_mulsca_s16_e(int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rz+1, rz} - rx[15:0] * ry[15:0] - rx[31:16] * ry[31:16],
     * signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(t2, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    /* {rz+1, rz} = {rz+1, rz} + lo_x * lo_y */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_sub_i64(t2, t2, t3);
    /* {rz+1, rz} = {rz+1, rz} + hi_x * hi_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_sub_i64(t2, t2, t3);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t2);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free_i64(t3);
}

static inline void dspv2_insn_mulscax_s16_e(int rz, int rx, int ry)
{
    /* {rz+1, rz} = {rz+1, rz} - rx[31:16] * ry[15:0] - rx[15:0] * ry[31:16],
     * signed */
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i64 t2 = tcg_temp_new_i64();
    TCGv_i64 t3 = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(t2, cpu_R[rz], cpu_R[(rz + 1) % 32]);
    /* {rz+1, rz} = {rz+1, rz} + hi_x * lo_y */
    tcg_gen_sari_i32(t0, cpu_R[rx], 16);
    tcg_gen_ext16s_i32(t1, cpu_R[ry]);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_sub_i64(t2, t2, t3);
    /* {rz+1, rz} = {rz+1, rz} + lo_x * hi_y */
    tcg_gen_ext16s_i32(t0, cpu_R[rx]);
    tcg_gen_sari_i32(t1, cpu_R[ry], 16);
    tcg_gen_mul_i32(t0, t0, t1);
    tcg_gen_ext_i32_i64(t3, t0);
    tcg_gen_sub_i64(t2, t2, t3);
    tcg_gen_extr_i64_i32(cpu_R[rz], cpu_R[(rz + 1) % 32], t2);
    tcg_temp_free_i32(t0);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i64(t2);
    tcg_temp_free_i64(t3);
}

static inline void dspv2_sop_mul_2nd(CPUState *cs,
                                     DisasContext *s, uint32_t insn)
{
    int thop, rz, rx, ry;
    thop = (insn >> CSKY_DSPV2_THOP_SHI) & CSKY_DSPV2_THOP_MASK;
    rz = (insn >> CSKY_DSPV2_REG_SHI_RZ) & CSKY_DSPV2_REG_MASK;
    rx = (insn >> CSKY_DSPV2_REG_SHI_RX) & CSKY_DSPV2_REG_MASK;
    ry = (insn >> CSKY_DSPV2_REG_SHI_RY) & CSKY_DSPV2_REG_MASK;

    switch (thop) {
    case OP_MULXL_S32:
        dspv2_insn_mulxl_s32(rz, rx, ry);
        break;
    case OP_MULXL_S32_R:
        dspv2_insn_mulxl_s32_r(rz, rx, ry);
        break;
    case OP_MULXH_S32:
        dspv2_insn_mulxh_s32(rz, rx, ry);
        break;
    case OP_MULXH_S32_R:
        dspv2_insn_mulxh_s32_r(rz, rx, ry);
        break;
    case OP_RMULXL_S32:
        gen_helper_dspv2_rmulxl_s32(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_RMULXL_S32_R:
        gen_helper_dspv2_rmulxl_s32_r(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_RMULXH_S32:
        gen_helper_dspv2_rmulxh_s32(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_RMULXH_S32_R:
        gen_helper_dspv2_rmulxh_s32_r(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULAXL_S32_S:
        gen_helper_dspv2_mulaxl_s32_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULAXL_S32_RS:
        gen_helper_dspv2_mulaxl_s32_rs(cpu_R[rz], cpu_R[rz],
                                       cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULAXH_S32_S:
        gen_helper_dspv2_mulaxh_s32_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULAXH_S32_RS:
        gen_helper_dspv2_mulaxh_s32_rs(cpu_R[rz], cpu_R[rz],
                                       cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULCA_S16_S:
        gen_helper_dspv2_mulca_s16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULCAX_S16_S:
        gen_helper_dspv2_mulcax_s16_s(cpu_R[rz], cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULCS_S16:
        dspv2_insn_mulcs_s16(rz, rx, ry);
        break;
    case OP_MULCSR_S16:
        dspv2_insn_mulcsr_s16(rz, rx, ry);
        break;
    case OP_MULCSX_S16:
        dspv2_insn_mulcsx_s16(rz, rx, ry);
        break;
    case OP_MULACA_S16_S:
        gen_helper_dspv2_mulaca_s16_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULACAX_S16_S:
        gen_helper_dspv2_mulacax_s16_s(cpu_R[rz], cpu_R[rz],
                                       cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULACS_S16_S:
        gen_helper_dspv2_mulacs_s16_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULACSR_S16_S:
        gen_helper_dspv2_mulacsr_s16_s(cpu_R[rz], cpu_R[rz],
                                       cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULACSX_S16_S:
        gen_helper_dspv2_mulacsx_s16_s(cpu_R[rz], cpu_R[rz],
                                       cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULSCA_S16_S:
        gen_helper_dspv2_mulsca_s16_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULSCAX_S16_S:
        gen_helper_dspv2_mulscax_s16_s(cpu_R[rz], cpu_R[rz],
                                      cpu_R[rx], cpu_R[ry]);
        break;
    case OP_MULACA_S16_E:
        dspv2_insn_mulaca_s16_e(rz, rx, ry);
        break;
    case OP_MULACAX_S16_E:
        dspv2_insn_mulacax_s16_e(rz, rx, ry);
        break;
    case OP_MULACS_S16_E:
        dspv2_insn_mulacs_s16_e(rz, rx, ry);
        break;
    case OP_MULACSR_S16_E:
        dspv2_insn_mulacsr_s16_e(rz, rx, ry);
        break;
    case OP_MULACSX_S16_E:
        dspv2_insn_mulacsx_s16_e(rz, rx, ry);
        break;
    case OP_MULSCA_S16_E:
        dspv2_insn_mulsca_s16_e(rz, rx, ry);
        break;
    case OP_MULSCAX_S16_E:
        dspv2_insn_mulscax_s16_e(rz, rx, ry);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n",
                      s->pc, insn);
        break;
    }
}

static void disas_dspv2_insn(CPUState *cs, DisasContext *s, uint32_t insn)
{
    int sop;
    sop = (insn >> CSKY_DSPV2_SOP_SHI) & CSKY_DSPV2_SOP_MASK;
    if (sop != DSPV2_MUL_1st) {
        check_insn(s, ABIV2_EDSP);
    }
    switch (sop) {
    case DSPV2_ADD_SUB:
        dspv2_sop_add_sub(cs, s, insn);
        break;
    case DSPV2_CMP:
        dspv2_sop_cmp(cs, s, insn);
        break;
    case DSPV2_SEL:
        dspv2_sop_sel(cs, s, insn);
        break;
    case DSPV2_MISC:
        dspv2_sop_misc(cs, s, insn);
        break;
    case DSPV2_SHIFT:
        dspv2_sop_shift(cs, s, insn);
        break;
    case DSPV2_PKG_begin ... DSPV2_PKG_end:
        dspv2_sop_pkg(cs, s, insn);
        break;
    case DSPV2_DEXT:
        dspv2_sop_dext(cs, s, insn);
        break;
    case DSPV2_PKG_CLIP:
        dspv2_sop_pkg_clip(cs, s, insn);
        break;
    case DSPV2_MUL_1st:
        dspv2_sop_mul_1st(cs, s, insn);
        break;
    case DSPV2_MUL_2nd:
        dspv2_sop_mul_2nd(cs, s, insn);
        break;
    default:
        generate_exception(s, EXCP_CSKY_UDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown dspv2 insn pc=%x opc=%x\n", s->pc, insn);
        break;
    }
}

static inline void cp(DisasContext *ctx, int cprz, int rx, uint32_t sop,
                       int imm)
{
    switch (sop) {
    case 0x0: /*cprgr*/
        break;
    case 0x1: /*cpwgr*/
        break;
    case 0x2: /*cprcr*/
        break;
    case 0x3: /*cpwcr*/
        break;
    case 0x4: /*cprc*/
        break;
    case 0x8: /*ldcpr*/
        break;
    case 0xa: /*stcpr*/
        break;
    case 0xc: /*cpop*/
        break;
    default:
        generate_exception(ctx, EXCP_CSKY_UDEF);
        break;
    }
}

static void disas_csky_32_insn(CPUCSKYState *env, DisasContext *ctx)
{
    uint32_t insn, op, sop, pcode;
    int rz, rx, ry, imm;
    CSKYCPU *cpu = csky_env_get_cpu(env);
    CPUState *cs = CPU(cpu);

    insn = ctx->insn;
    op = (insn >> 26) & 0xf; /* bits:29-26 */

    switch (op) {
    case 0x0: /*special*/
        rx = (insn >> 16) & 0x1f;
        ry = (insn >> 21) & 0x1f;
        sop = (insn >> 10) & 0x3f;
        rz = insn & 0x1f;
        special(ctx, rx, sop, rz, ry);
        break;
    case 0x1:/*arth_reg*/
        ry = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        sop = (insn >> 10) & 0x3f;
        pcode = (insn >> 5) & 0x1f;
        rz = insn & 0x1f;
        arth_reg32(ctx, ry, rx, sop, pcode, rz);
        break;
    case 0x3:/*lrs_srs*/
        rz = (insn >> 21) & 0x1f;
        sop = (insn >> 18) & 0x7;
        imm = insn & 0x3ffff;
        lrs(ctx, rz, sop, imm);
        break;
    case 0x4:/*ldr*/
        ry = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        sop = (insn >> 10) & 0x3f;
        pcode = (insn >> 5) & 0x1f;
        rz = insn & 0x1f;
        ldr(ctx, sop, pcode, rz, rx, ry);
        break;
    case 0x5:/*str*/
        ry = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        sop = (insn >> 10) & 0x3f;
        pcode = (insn >> 5) & 0x1f;
        rz = insn & 0x1f;
        str(ctx, sop, pcode, rz, rx, ry);
        break;
    case 0x6:/*ldi*/
        rz = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        sop = (insn >> 12) & 0xf;
        imm = insn & 0xfff;
        ldi(ctx, sop, rz, rx, imm);
        break;
    case 0x7:/*sti*/
        rz = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        sop = (insn >> 12) & 0xf;
        imm = insn & 0xfff;
        sti(ctx, sop, rz, rx, imm);
        break;
    case 0x8:/*bsr*/
        imm = insn & 0x3ffffff;
        tcg_gen_movi_tl(cpu_R[15], ctx->pc + 4);
        bsr32(ctx, imm);
        break;
    case 0x9:/*imm_2op*/
        rz = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        sop = (insn >> 12) & 0xf;
        imm = insn & 0xfff;
        imm_2op(ctx, rz, rx, sop, imm);
        break;
    case 0xa:/*imm_1op*/
        sop = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        imm = insn & 0xffff;
        imm_1op(ctx, sop, rx, imm);
        break;
    case 0xb:/*ori*/
        check_insn_except(ctx, CPU_801);
        rz = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        imm = insn & 0xffff;
        tcg_gen_ori_tl(cpu_R[rz], cpu_R[rx], imm);
        break;
    case 0xd:/*vfp*/
        disas_vfp_insn(env, ctx, insn);
        gen_save_pc(ctx->pc);
        gen_helper_vfp_check_exception(cpu_env);
        break;
    case 0xe:/*vdsp or dspv2 instructions. */
        if (has_insn(ctx, ABIV2_VDSP128)) {
            disas_vdsp_insn128(cs, ctx, insn);
        } else if (has_insn(ctx, ABIV2_VDSP64)) {
            disas_vdsp_insn64(cs, ctx, insn);
        } else if (has_insn(ctx, CPU_803S)) {
            disas_dspv2_insn(cs, ctx, insn);
        } else {
            generate_exception(ctx, EXCP_CSKY_UDEF);
            qemu_log_mask(LOG_GUEST_ERROR,
                          "unknown dsp insn pc=%x opc=%x\n", ctx->pc, insn);
        }
        break;
    case 0xf:/*cp*/
        rz = (insn >> 21) & 0x1f;
        rx = (insn >> 16) & 0x1f;
        sop = (insn >> 12) & 0xf;
        imm = insn & 0xfff;
        cp(ctx, rz, rx, sop, imm);
        break;
    default:
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
    DisasContext ctx1, *ctx = &ctx1;
    target_ulong pc_start;
    uint32_t next_page_start;
    int max_insns;
    int num_insns;
    uint32_t cond;
    TCGv t0 = tcg_temp_new();

    cpu_F0s = tcg_temp_new_i32();
    cpu_F1s = tcg_temp_new_i32();
    cpu_F0d = tcg_temp_new_i64();
    cpu_F1d = tcg_temp_new_i64();

    pc_start = tb->pc;

    ctx->pc = pc_start;
    ctx->tb = tb;
    ctx->singlestep_enabled = cs->singlestep_enabled;
    ctx->is_jmp = DISAS_NEXT;
    ctx->bctm = CSKY_TBFLAG_PSR_BM(tb->flags);
    ctx->features = env->features;

#ifndef CONFIG_USER_ONLY
    ctx->super = CSKY_TBFLAG_PSR_S(tb->flags);
    ctx->trust = CSKY_TBFLAG_PSR_T(tb->flags);
    ctx->current_cp = CSKY_TBFLAG_CPID(tb->flags);
    ctx->trace_mode = (TraceMode)CSKY_TBFLAG_PSR_TM(tb->flags);
#endif

#ifdef CONFIG_USER_ONLY
    ctx->mem_idx = CSKY_USERMODE;
#else
    ctx->mem_idx = ctx->super;
#endif

    next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }
    if (max_insns > TCG_MAX_INSNS) {
        max_insns = TCG_MAX_INSNS;
    }
    cond = env->sce_condexec_bits;

    gen_tb_start(tb);
    if (env->jcount_start != 0) {
        gen_csky_jcount_start(env, tb);
    }

    if (env->tb_trace == 1) {
        csky_tb_start(env, tb);
    }

    /* for sce */
    if (unlikely(cond != 1)) {
        TCGv t0 = tcg_temp_new();
        do {
#if !defined(CONFIG_USER_ONLY)
            ctx->cannot_be_traced = 0;
            ctx->maybe_change_flow = 0;
#endif
#ifdef CONFIG_USER_ONLY
            if (ctx->pc >= 0x80000000) {
                generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
                break;
            }
#endif

            tcg_gen_insn_start(ctx->pc);
            num_insns++;

            if (unlikely(cpu_breakpoint_test(cs, ctx->pc, BP_ANY))) {
                generate_exception(ctx, EXCP_DEBUG);
                ctx->is_jmp = DISAS_JUMP;
                ctx->pc += 2;
                goto done_generating;
                break;
            }

            if (num_insns == max_insns && (tb->cflags & CF_LAST_IO)) {
                gen_io_start();
            }

            ctx->insn = cpu_lduw_code(env, ctx->pc);

            if ((cond & 0x1) == 1) {
                if ((ctx->insn & 0xc000) != 0xc000) {
                    /* 16 bit instruction*/
                    disas_csky_16_insn(env, ctx);
                    ctx->pc += 2;
                } else {
                    /*32 bit instruction*/
                    ctx->insn = (ctx->insn << 16) |
                        cpu_lduw_code(env, ctx->pc + 2);
                    disas_csky_32_insn(env, ctx);
                    ctx->pc += 4;
                }
#if !defined(CONFIG_USER_ONLY)
                if (ctx->trace_mode == INST_TRACE_MODE) {
                    cond >>= 1;
                    t0 = tcg_const_tl(cond);
                    store_cpu_field(t0, sce_condexec_bits);
                    generate_exception(ctx, EXCP_CSKY_TRACE);
                    num_insns++;
                    goto done_translation;
                }
#endif
            } else {
                if ((ctx->insn & 0xc000) != 0xc000) {
                    /* 16 bit instruction*/
                    ctx->pc += 2;
                } else {
                    /*32 bit instruction*/
                    ctx->pc += 4;
                }
            }
            cond >>= 1;

            if (cond == 0x1) {
                break;
            }
        } while (!ctx->is_jmp && !tcg_op_buf_full() &&
            !cs->singlestep_enabled &&
            !singlestep &&
            ctx->pc < next_page_start &&
            num_insns < max_insns);

        t0 = tcg_const_tl(cond);
        store_cpu_field(t0, sce_condexec_bits);
        gen_save_pc(ctx->pc);
        ctx->is_jmp = DISAS_UPDATE;

        goto done_translation;
    }

    /* for idly */
#if !defined(CONFIG_USER_ONLY)
    uint32_t idly4_counter;
    idly4_counter = env->idly4_counter;
    if (unlikely(idly4_counter != 0)) {
        TCGv t0 = tcg_temp_new();
        do {
            tcg_gen_insn_start(ctx->pc);
            num_insns++;

            if (unlikely(cpu_breakpoint_test(cs, ctx->pc, BP_ANY))) {
                generate_exception(ctx, EXCP_DEBUG);
                ctx->is_jmp = DISAS_JUMP;
                ctx->pc += 2;
                goto done_generating;
                break;
            }

            if (num_insns == max_insns && (tb->cflags & CF_LAST_IO)) {
                gen_io_start();
            }

            ctx->insn = cpu_lduw_code(env, ctx->pc);

            if ((cond & 0x1) == 1) {
                if ((ctx->insn & 0xc000) != 0xc000) {
                    /* 16 bit instruction*/
                    disas_csky_16_insn(env, ctx);
                    ctx->pc += 2;
                } else {
                    /*32 bit instruction*/
                    ctx->insn = (ctx->insn << 16) |
                        cpu_lduw_code(env, ctx->pc + 2);
                    disas_csky_32_insn(env, ctx);
                    ctx->pc += 4;
                }
            } else {
                if ((ctx->insn & 0xc000) != 0xc000) {
                    /* 16 bit instruction*/
                    ctx->pc += 2;
                } else {
                    /*32 bit instruction*/
                    ctx->pc += 4;
                }
            }

            idly4_counter--;
            num_insns++;

            if (!idly4_counter) {
                break;
            }
        } while (!ctx->is_jmp && !tcg_op_buf_full() &&
            !cs->singlestep_enabled &&
            !singlestep &&
            ctx->pc < next_page_start &&
            num_insns < max_insns);

        t0 = tcg_const_tl(idly4_counter);
        store_cpu_field(t0, idly4_counter);

        goto done_translation;
    }
#endif

    do {
#if !defined(CONFIG_USER_ONLY)
        ctx->cannot_be_traced = 0;
        ctx->maybe_change_flow = 0;
#endif
#ifdef CONFIG_USER_ONLY
        if (ctx->pc >= 0x80000000) {
            generate_exception(ctx, EXCP_CSKY_PRIVILEGE);
            break;
        }
#endif

        tcg_gen_insn_start(ctx->pc);
        num_insns++;

        if (unlikely(cpu_breakpoint_test(cs, ctx->pc, BP_ANY))) {
            generate_exception(ctx, EXCP_DEBUG);
            ctx->is_jmp = DISAS_JUMP;
            ctx->pc += 2;
            goto done_generating;
            break;
        }

        if (num_insns == max_insns && (tb->cflags & CF_LAST_IO)) {
            gen_io_start();
        }

        ctx->insn = cpu_lduw_code(env, ctx->pc);

        if ((ctx->insn & 0xc000) != 0xc000) {
            /* 16 bit instruction*/
            disas_csky_16_insn(env, ctx);
            ctx->pc += 2;
        } else {
            /*32 bit instruction*/
            ctx->insn = (ctx->insn << 16) | cpu_lduw_code(env, ctx->pc + 2);
            disas_csky_32_insn(env, ctx);
            ctx->pc += 4;
        }

#if !defined(CONFIG_USER_ONLY)
        if (ctx->cannot_be_traced) {
            break;
        }
        if (ctx->trace_mode == INST_TRACE_MODE) {
            if (!ctx->maybe_change_flow) {
                generate_exception(ctx, EXCP_CSKY_TRACE);
            }
            break;
        }
#endif

    } while (!ctx->is_jmp && !tcg_op_buf_full() &&
        !cs->singlestep_enabled &&
        !singlestep &&
        ctx->pc < next_page_start &&
        num_insns < max_insns);

done_translation:
    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }

    if (unlikely(cs->singlestep_enabled)) {
        if (!ctx->is_jmp) {
            generate_exception(ctx, EXCP_DEBUG);
        } else if (ctx->is_jmp != DISAS_TB_JUMP) {
            t0 = tcg_const_tl(EXCP_DEBUG);
            gen_helper_exception(cpu_env, t0);
        }
    } else {
        switch (ctx->is_jmp) {
        case DISAS_NEXT:
            gen_goto_tb(ctx, 1, ctx->pc);
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

    tcg_temp_free(t0);
    tcg_temp_free_i32(cpu_F0s);
    tcg_temp_free_i32(cpu_F1s);
    tcg_temp_free_i64(cpu_F0d);
    tcg_temp_free_i64(cpu_F1d);

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("----------------\n");
        qemu_log("IN: %s\n", lookup_symbol(pc_start));
        log_target_disas(cs, pc_start, ctx->pc - pc_start);
        qemu_log("\n");
    }
#endif

    tb->size = ctx->pc - pc_start;
    tb->icount = num_insns;
    if (env->tb_trace == 1) {
        csky_dump_tb_map(env, tb);
    }
}


void csky_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                         int flags)
{
    CSKYCPU *cpu = CSKY_CPU(cs);
    CPUCSKYState *env = &cpu->env;
    int i;

    for (i = 0; i < 32; i++) {
        cpu_fprintf(f, "R%02d=0x%08x", i, env->regs[i]);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }

    for (i = 0; i < 16; i++) {
        cpu_fprintf(f, "vr%02d=0x%16llx", i,
                    (long long unsigned int)env->vfp.reg[i].fpu[0]);
        if ((i % 3) == 2) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }

    cpu_fprintf(f, "pc=%08x\n", env->pc);

    env->cp0.psr &= ~0x8000c401;
    env->cp0.psr |= env->psr_s << 31;
    env->cp0.psr |= env->psr_tm << 14;
    env->cp0.psr |= env->psr_bm << 10;
    env->cp0.psr |= env->psr_c;
    cpu_fprintf(f, "psr=%08x\n", env->cp0.psr);
    cpu_fprintf(f, "sp=%08x\n", env->regs[14]);
    cpu_fprintf(f, "spv_sp=%08x\n", env->stackpoint.nt_ssp);
    cpu_fprintf(f, "epsr=%08x ", env->cp0.epsr);
    cpu_fprintf(f, "epc=%08x ", env->cp0.epc);
    cpu_fprintf(f, "cr18=%08x\n", env->cp0.capr);
}

void restore_state_to_opc(CPUCSKYState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->pc = data[0];
}
