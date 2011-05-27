/*
 *  S/390 translation
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2010 Alexander Graf
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
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

/* #define DEBUG_ILLEGAL_INSTRUCTIONS */
/* #define DEBUG_INLINE_BRANCHES */
#define S390X_DEBUG_DISAS
/* #define S390X_DEBUG_DISAS_VERBOSE */

#ifdef S390X_DEBUG_DISAS_VERBOSE
#  define LOG_DISAS(...) qemu_log(__VA_ARGS__)
#else
#  define LOG_DISAS(...) do { } while (0)
#endif

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"
#include "tcg-op.h"
#include "qemu-log.h"

/* global register indexes */
static TCGv_ptr cpu_env;

#include "gen-icount.h"
#include "helpers.h"
#define GEN_HELPER 1
#include "helpers.h"

typedef struct DisasContext DisasContext;
struct DisasContext {
    uint64_t pc;
    int is_jmp;
    enum cc_op cc_op;
    struct TranslationBlock *tb;
};

#define DISAS_EXCP 4

static void gen_op_calc_cc(DisasContext *s);

#ifdef DEBUG_INLINE_BRANCHES
static uint64_t inline_branch_hit[CC_OP_MAX];
static uint64_t inline_branch_miss[CC_OP_MAX];
#endif

static inline void debug_insn(uint64_t insn)
{
    LOG_DISAS("insn: 0x%" PRIx64 "\n", insn);
}

static inline uint64_t pc_to_link_info(DisasContext *s, uint64_t pc)
{
    if (!(s->tb->flags & FLAG_MASK_64)) {
        if (s->tb->flags & FLAG_MASK_32) {
            return pc | 0x80000000;
        }
    }
    return pc;
}

void cpu_dump_state(CPUState *env, FILE *f, fprintf_function cpu_fprintf,
                    int flags)
{
    int i;

    for (i = 0; i < 16; i++) {
        cpu_fprintf(f, "R%02d=%016" PRIx64, i, env->regs[i]);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }

    for (i = 0; i < 16; i++) {
        cpu_fprintf(f, "F%02d=%016" PRIx64, i, *(uint64_t *)&env->fregs[i]);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }

    cpu_fprintf(f, "\n");

#ifndef CONFIG_USER_ONLY
    for (i = 0; i < 16; i++) {
        cpu_fprintf(f, "C%02d=%016" PRIx64, i, env->cregs[i]);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }
#endif

    cpu_fprintf(f, "\n");

    if (env->cc_op > 3) {
        cpu_fprintf(f, "PSW=mask %016" PRIx64 " addr %016" PRIx64 " cc %15s\n",
                    env->psw.mask, env->psw.addr, cc_name(env->cc_op));
    } else {
        cpu_fprintf(f, "PSW=mask %016" PRIx64 " addr %016" PRIx64 " cc %02x\n",
                    env->psw.mask, env->psw.addr, env->cc_op);
    }

#ifdef DEBUG_INLINE_BRANCHES
    for (i = 0; i < CC_OP_MAX; i++) {
        cpu_fprintf(f, "  %15s = %10ld\t%10ld\n", cc_name(i),
                    inline_branch_miss[i], inline_branch_hit[i]);
    }
#endif
}

static TCGv_i64 psw_addr;
static TCGv_i64 psw_mask;

static TCGv_i32 cc_op;
static TCGv_i64 cc_src;
static TCGv_i64 cc_dst;
static TCGv_i64 cc_vr;

static char cpu_reg_names[10*3 + 6*4];
static TCGv_i64 regs[16];

static uint8_t gen_opc_cc_op[OPC_BUF_SIZE];

void s390x_translate_init(void)
{
    int i;
    size_t cpu_reg_names_size = sizeof(cpu_reg_names);
    char *p;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");
    psw_addr = tcg_global_mem_new_i64(TCG_AREG0, offsetof(CPUState, psw.addr),
                                      "psw_addr");
    psw_mask = tcg_global_mem_new_i64(TCG_AREG0, offsetof(CPUState, psw.mask),
                                      "psw_mask");

    cc_op = tcg_global_mem_new_i32(TCG_AREG0, offsetof(CPUState, cc_op),
                                   "cc_op");
    cc_src = tcg_global_mem_new_i64(TCG_AREG0, offsetof(CPUState, cc_src),
                                    "cc_src");
    cc_dst = tcg_global_mem_new_i64(TCG_AREG0, offsetof(CPUState, cc_dst),
                                    "cc_dst");
    cc_vr = tcg_global_mem_new_i64(TCG_AREG0, offsetof(CPUState, cc_vr),
                                   "cc_vr");

    p = cpu_reg_names;
    for (i = 0; i < 16; i++) {
        snprintf(p, cpu_reg_names_size, "r%d", i);
        regs[i] = tcg_global_mem_new(TCG_AREG0,
                                     offsetof(CPUState, regs[i]), p);
        p += (i < 10) ? 3 : 4;
        cpu_reg_names_size -= (i < 10) ? 3 : 4;
    }
}

static inline TCGv_i64 load_reg(int reg)
{
    TCGv_i64 r = tcg_temp_new_i64();
    tcg_gen_mov_i64(r, regs[reg]);
    return r;
}

static inline TCGv_i64 load_freg(int reg)
{
    TCGv_i64 r = tcg_temp_new_i64();
    tcg_gen_ld_i64(r, cpu_env, offsetof(CPUState, fregs[reg].d));
    return r;
}

static inline TCGv_i32 load_freg32(int reg)
{
    TCGv_i32 r = tcg_temp_new_i32();
    tcg_gen_ld_i32(r, cpu_env, offsetof(CPUState, fregs[reg].l.upper));
    return r;
}

static inline TCGv_i32 load_reg32(int reg)
{
    TCGv_i32 r = tcg_temp_new_i32();
    tcg_gen_trunc_i64_i32(r, regs[reg]);
    return r;
}

static inline TCGv_i64 load_reg32_i64(int reg)
{
    TCGv_i64 r = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(r, regs[reg]);
    return r;
}

static inline void store_reg(int reg, TCGv_i64 v)
{
    tcg_gen_mov_i64(regs[reg], v);
}

static inline void store_freg(int reg, TCGv_i64 v)
{
    tcg_gen_st_i64(v, cpu_env, offsetof(CPUState, fregs[reg].d));
}

static inline void store_reg32(int reg, TCGv_i32 v)
{
#if HOST_LONG_BITS == 32
    tcg_gen_mov_i32(TCGV_LOW(regs[reg]), v);
#else
    TCGv_i64 tmp = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(tmp, v);
    /* 32 bit register writes keep the upper half */
    tcg_gen_deposit_i64(regs[reg], regs[reg], tmp, 0, 32);
    tcg_temp_free_i64(tmp);
#endif
}

static inline void store_reg32_i64(int reg, TCGv_i64 v)
{
    /* 32 bit register writes keep the upper half */
#if HOST_LONG_BITS == 32
    tcg_gen_mov_i32(TCGV_LOW(regs[reg]), TCGV_LOW(v));
#else
    tcg_gen_deposit_i64(regs[reg], regs[reg], v, 0, 32);
#endif
}

static inline void store_reg16(int reg, TCGv_i32 v)
{
    TCGv_i64 tmp = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(tmp, v);
    /* 16 bit register writes keep the upper bytes */
    tcg_gen_deposit_i64(regs[reg], regs[reg], tmp, 0, 16);
    tcg_temp_free_i64(tmp);
}

static inline void store_reg8(int reg, TCGv_i64 v)
{
    /* 8 bit register writes keep the upper bytes */
    tcg_gen_deposit_i64(regs[reg], regs[reg], v, 0, 8);
}

static inline void store_freg32(int reg, TCGv_i32 v)
{
    tcg_gen_st_i32(v, cpu_env, offsetof(CPUState, fregs[reg].l.upper));
}

static inline void update_psw_addr(DisasContext *s)
{
    /* psw.addr */
    tcg_gen_movi_i64(psw_addr, s->pc);
}

static inline void potential_page_fault(DisasContext *s)
{
#ifndef CONFIG_USER_ONLY
    update_psw_addr(s);
    gen_op_calc_cc(s);
#endif
}

static inline uint64_t ld_code2(uint64_t pc)
{
    return (uint64_t)lduw_code(pc);
}

static inline uint64_t ld_code4(uint64_t pc)
{
    return (uint64_t)ldl_code(pc);
}

static inline uint64_t ld_code6(uint64_t pc)
{
    uint64_t opc;
    opc = (uint64_t)lduw_code(pc) << 32;
    opc |= (uint64_t)(uint32_t)ldl_code(pc+2);
    return opc;
}

static inline int get_mem_index(DisasContext *s)
{
    switch (s->tb->flags & FLAG_MASK_ASC) {
    case PSW_ASC_PRIMARY >> 32:
        return 0;
    case PSW_ASC_SECONDARY >> 32:
        return 1;
    case PSW_ASC_HOME >> 32:
        return 2;
    default:
        tcg_abort();
        break;
    }
}

static inline void gen_debug(DisasContext *s)
{
    TCGv_i32 tmp = tcg_const_i32(EXCP_DEBUG);
    update_psw_addr(s);
    gen_op_calc_cc(s);
    gen_helper_exception(tmp);
    tcg_temp_free_i32(tmp);
    s->is_jmp = DISAS_EXCP;
}

#ifdef CONFIG_USER_ONLY

static void gen_illegal_opcode(DisasContext *s, int ilc)
{
    TCGv_i32 tmp = tcg_const_i32(EXCP_SPEC);
    update_psw_addr(s);
    gen_op_calc_cc(s);
    gen_helper_exception(tmp);
    tcg_temp_free_i32(tmp);
    s->is_jmp = DISAS_EXCP;
}

#else /* CONFIG_USER_ONLY */

static void debug_print_inst(DisasContext *s, int ilc)
{
#ifdef DEBUG_ILLEGAL_INSTRUCTIONS
    uint64_t inst = 0;

    switch (ilc & 3) {
    case 1:
        inst = ld_code2(s->pc);
        break;
    case 2:
        inst = ld_code4(s->pc);
        break;
    case 3:
        inst = ld_code6(s->pc);
        break;
    }

    fprintf(stderr, "Illegal instruction [%d at %016" PRIx64 "]: 0x%016"
            PRIx64 "\n", ilc, s->pc, inst);
#endif
}

static void gen_program_exception(DisasContext *s, int ilc, int code)
{
    TCGv_i32 tmp;

    debug_print_inst(s, ilc);

    /* remember what pgm exeption this was */
    tmp = tcg_const_i32(code);
    tcg_gen_st_i32(tmp, cpu_env, offsetof(CPUState, int_pgm_code));
    tcg_temp_free_i32(tmp);

    tmp = tcg_const_i32(ilc);
    tcg_gen_st_i32(tmp, cpu_env, offsetof(CPUState, int_pgm_ilc));
    tcg_temp_free_i32(tmp);

    /* advance past instruction */
    s->pc += (ilc * 2);
    update_psw_addr(s);

    /* save off cc */
    gen_op_calc_cc(s);

    /* trigger exception */
    tmp = tcg_const_i32(EXCP_PGM);
    gen_helper_exception(tmp);
    tcg_temp_free_i32(tmp);

    /* end TB here */
    s->is_jmp = DISAS_EXCP;
}


static void gen_illegal_opcode(DisasContext *s, int ilc)
{
    gen_program_exception(s, ilc, PGM_SPECIFICATION);
}

static void gen_privileged_exception(DisasContext *s, int ilc)
{
    gen_program_exception(s, ilc, PGM_PRIVILEGED);
}

static void check_privileged(DisasContext *s, int ilc)
{
    if (s->tb->flags & (PSW_MASK_PSTATE >> 32)) {
        gen_privileged_exception(s, ilc);
    }
}

#endif /* CONFIG_USER_ONLY */

static TCGv_i64 get_address(DisasContext *s, int x2, int b2, int d2)
{
    TCGv_i64 tmp;

    /* 31-bitify the immediate part; register contents are dealt with below */
    if (!(s->tb->flags & FLAG_MASK_64)) {
        d2 &= 0x7fffffffUL;
    }

    if (x2) {
        if (d2) {
            tmp = tcg_const_i64(d2);
            tcg_gen_add_i64(tmp, tmp, regs[x2]);
        } else {
            tmp = load_reg(x2);
        }
        if (b2) {
            tcg_gen_add_i64(tmp, tmp, regs[b2]);
        }
    } else if (b2) {
        if (d2) {
            tmp = tcg_const_i64(d2);
            tcg_gen_add_i64(tmp, tmp, regs[b2]);
        } else {
            tmp = load_reg(b2);
        }
    } else {
        tmp = tcg_const_i64(d2);
    }

    /* 31-bit mode mask if there are values loaded from registers */
    if (!(s->tb->flags & FLAG_MASK_64) && (x2 || b2)) {
        tcg_gen_andi_i64(tmp, tmp, 0x7fffffffUL);
    }

    return tmp;
}

static void gen_op_movi_cc(DisasContext *s, uint32_t val)
{
    s->cc_op = CC_OP_CONST0 + val;
}

static void gen_op_update1_cc_i64(DisasContext *s, enum cc_op op, TCGv_i64 dst)
{
    tcg_gen_discard_i64(cc_src);
    tcg_gen_mov_i64(cc_dst, dst);
    tcg_gen_discard_i64(cc_vr);
    s->cc_op = op;
}

static void gen_op_update1_cc_i32(DisasContext *s, enum cc_op op, TCGv_i32 dst)
{
    tcg_gen_discard_i64(cc_src);
    tcg_gen_extu_i32_i64(cc_dst, dst);
    tcg_gen_discard_i64(cc_vr);
    s->cc_op = op;
}

static void gen_op_update2_cc_i64(DisasContext *s, enum cc_op op, TCGv_i64 src,
                                  TCGv_i64 dst)
{
    tcg_gen_mov_i64(cc_src, src);
    tcg_gen_mov_i64(cc_dst, dst);
    tcg_gen_discard_i64(cc_vr);
    s->cc_op = op;
}

static void gen_op_update2_cc_i32(DisasContext *s, enum cc_op op, TCGv_i32 src,
                                  TCGv_i32 dst)
{
    tcg_gen_extu_i32_i64(cc_src, src);
    tcg_gen_extu_i32_i64(cc_dst, dst);
    tcg_gen_discard_i64(cc_vr);
    s->cc_op = op;
}

static void gen_op_update3_cc_i64(DisasContext *s, enum cc_op op, TCGv_i64 src,
                                  TCGv_i64 dst, TCGv_i64 vr)
{
    tcg_gen_mov_i64(cc_src, src);
    tcg_gen_mov_i64(cc_dst, dst);
    tcg_gen_mov_i64(cc_vr, vr);
    s->cc_op = op;
}

static void gen_op_update3_cc_i32(DisasContext *s, enum cc_op op, TCGv_i32 src,
                                  TCGv_i32 dst, TCGv_i32 vr)
{
    tcg_gen_extu_i32_i64(cc_src, src);
    tcg_gen_extu_i32_i64(cc_dst, dst);
    tcg_gen_extu_i32_i64(cc_vr, vr);
    s->cc_op = op;
}

static inline void set_cc_nz_u32(DisasContext *s, TCGv_i32 val)
{
    gen_op_update1_cc_i32(s, CC_OP_NZ, val);
}

static inline void set_cc_nz_u64(DisasContext *s, TCGv_i64 val)
{
    gen_op_update1_cc_i64(s, CC_OP_NZ, val);
}

static inline void cmp_32(DisasContext *s, TCGv_i32 v1, TCGv_i32 v2,
                          enum cc_op cond)
{
    gen_op_update2_cc_i32(s, cond, v1, v2);
}

static inline void cmp_64(DisasContext *s, TCGv_i64 v1, TCGv_i64 v2,
                          enum cc_op cond)
{
    gen_op_update2_cc_i64(s, cond, v1, v2);
}

static inline void cmp_s32(DisasContext *s, TCGv_i32 v1, TCGv_i32 v2)
{
    cmp_32(s, v1, v2, CC_OP_LTGT_32);
}

static inline void cmp_u32(DisasContext *s, TCGv_i32 v1, TCGv_i32 v2)
{
    cmp_32(s, v1, v2, CC_OP_LTUGTU_32);
}

static inline void cmp_s32c(DisasContext *s, TCGv_i32 v1, int32_t v2)
{
    /* XXX optimize for the constant? put it in s? */
    TCGv_i32 tmp = tcg_const_i32(v2);
    cmp_32(s, v1, tmp, CC_OP_LTGT_32);
    tcg_temp_free_i32(tmp);
}

static inline void cmp_u32c(DisasContext *s, TCGv_i32 v1, uint32_t v2)
{
    TCGv_i32 tmp = tcg_const_i32(v2);
    cmp_32(s, v1, tmp, CC_OP_LTUGTU_32);
    tcg_temp_free_i32(tmp);
}

static inline void cmp_s64(DisasContext *s, TCGv_i64 v1, TCGv_i64 v2)
{
    cmp_64(s, v1, v2, CC_OP_LTGT_64);
}

static inline void cmp_u64(DisasContext *s, TCGv_i64 v1, TCGv_i64 v2)
{
    cmp_64(s, v1, v2, CC_OP_LTUGTU_64);
}

static inline void cmp_s64c(DisasContext *s, TCGv_i64 v1, int64_t v2)
{
    TCGv_i64 tmp = tcg_const_i64(v2);
    cmp_s64(s, v1, tmp);
    tcg_temp_free_i64(tmp);
}

static inline void cmp_u64c(DisasContext *s, TCGv_i64 v1, uint64_t v2)
{
    TCGv_i64 tmp = tcg_const_i64(v2);
    cmp_u64(s, v1, tmp);
    tcg_temp_free_i64(tmp);
}

static inline void set_cc_s32(DisasContext *s, TCGv_i32 val)
{
    gen_op_update1_cc_i32(s, CC_OP_LTGT0_32, val);
}

static inline void set_cc_s64(DisasContext *s, TCGv_i64 val)
{
    gen_op_update1_cc_i64(s, CC_OP_LTGT0_64, val);
}

static void set_cc_add64(DisasContext *s, TCGv_i64 v1, TCGv_i64 v2, TCGv_i64 vr)
{
    gen_op_update3_cc_i64(s, CC_OP_ADD_64, v1, v2, vr);
}

static void set_cc_addu64(DisasContext *s, TCGv_i64 v1, TCGv_i64 v2,
                          TCGv_i64 vr)
{
    gen_op_update3_cc_i64(s, CC_OP_ADDU_64, v1, v2, vr);
}

static void set_cc_sub64(DisasContext *s, TCGv_i64 v1, TCGv_i64 v2, TCGv_i64 vr)
{
    gen_op_update3_cc_i64(s, CC_OP_SUB_64, v1, v2, vr);
}

static void set_cc_subu64(DisasContext *s, TCGv_i64 v1, TCGv_i64 v2,
                          TCGv_i64 vr)
{
    gen_op_update3_cc_i64(s, CC_OP_SUBU_64, v1, v2, vr);
}

static void set_cc_abs64(DisasContext *s, TCGv_i64 v1)
{
    gen_op_update1_cc_i64(s, CC_OP_ABS_64, v1);
}

static void set_cc_nabs64(DisasContext *s, TCGv_i64 v1)
{
    gen_op_update1_cc_i64(s, CC_OP_NABS_64, v1);
}

static void set_cc_add32(DisasContext *s, TCGv_i32 v1, TCGv_i32 v2, TCGv_i32 vr)
{
    gen_op_update3_cc_i32(s, CC_OP_ADD_32, v1, v2, vr);
}

static void set_cc_addu32(DisasContext *s, TCGv_i32 v1, TCGv_i32 v2,
                          TCGv_i32 vr)
{
    gen_op_update3_cc_i32(s, CC_OP_ADDU_32, v1, v2, vr);
}

static void set_cc_sub32(DisasContext *s, TCGv_i32 v1, TCGv_i32 v2, TCGv_i32 vr)
{
    gen_op_update3_cc_i32(s, CC_OP_SUB_32, v1, v2, vr);
}

static void set_cc_subu32(DisasContext *s, TCGv_i32 v1, TCGv_i32 v2,
                          TCGv_i32 vr)
{
    gen_op_update3_cc_i32(s, CC_OP_SUBU_32, v1, v2, vr);
}

static void set_cc_abs32(DisasContext *s, TCGv_i32 v1)
{
    gen_op_update1_cc_i32(s, CC_OP_ABS_32, v1);
}

static void set_cc_nabs32(DisasContext *s, TCGv_i32 v1)
{
    gen_op_update1_cc_i32(s, CC_OP_NABS_32, v1);
}

static void set_cc_comp32(DisasContext *s, TCGv_i32 v1)
{
    gen_op_update1_cc_i32(s, CC_OP_COMP_32, v1);
}

static void set_cc_comp64(DisasContext *s, TCGv_i64 v1)
{
    gen_op_update1_cc_i64(s, CC_OP_COMP_64, v1);
}

static void set_cc_icm(DisasContext *s, TCGv_i32 v1, TCGv_i32 v2)
{
    gen_op_update2_cc_i32(s, CC_OP_ICM, v1, v2);
}

static void set_cc_cmp_f32_i64(DisasContext *s, TCGv_i32 v1, TCGv_i64 v2)
{
    tcg_gen_extu_i32_i64(cc_src, v1);
    tcg_gen_mov_i64(cc_dst, v2);
    tcg_gen_discard_i64(cc_vr);
    s->cc_op = CC_OP_LTGT_F32;
}

static void set_cc_nz_f32(DisasContext *s, TCGv_i32 v1)
{
    gen_op_update1_cc_i32(s, CC_OP_NZ_F32, v1);
}

static inline void set_cc_nz_f64(DisasContext *s, TCGv_i64 v1)
{
    gen_op_update1_cc_i64(s, CC_OP_NZ_F64, v1);
}

/* CC value is in env->cc_op */
static inline void set_cc_static(DisasContext *s)
{
    tcg_gen_discard_i64(cc_src);
    tcg_gen_discard_i64(cc_dst);
    tcg_gen_discard_i64(cc_vr);
    s->cc_op = CC_OP_STATIC;
}

static inline void gen_op_set_cc_op(DisasContext *s)
{
    if (s->cc_op != CC_OP_DYNAMIC && s->cc_op != CC_OP_STATIC) {
        tcg_gen_movi_i32(cc_op, s->cc_op);
    }
}

static inline void gen_update_cc_op(DisasContext *s)
{
    gen_op_set_cc_op(s);
}

/* calculates cc into cc_op */
static void gen_op_calc_cc(DisasContext *s)
{
    TCGv_i32 local_cc_op = tcg_const_i32(s->cc_op);
    TCGv_i64 dummy = tcg_const_i64(0);

    switch (s->cc_op) {
    case CC_OP_CONST0:
    case CC_OP_CONST1:
    case CC_OP_CONST2:
    case CC_OP_CONST3:
        /* s->cc_op is the cc value */
        tcg_gen_movi_i32(cc_op, s->cc_op - CC_OP_CONST0);
        break;
    case CC_OP_STATIC:
        /* env->cc_op already is the cc value */
        break;
    case CC_OP_NZ:
    case CC_OP_ABS_64:
    case CC_OP_NABS_64:
    case CC_OP_ABS_32:
    case CC_OP_NABS_32:
    case CC_OP_LTGT0_32:
    case CC_OP_LTGT0_64:
    case CC_OP_COMP_32:
    case CC_OP_COMP_64:
    case CC_OP_NZ_F32:
    case CC_OP_NZ_F64:
        /* 1 argument */
        gen_helper_calc_cc(cc_op, local_cc_op, dummy, cc_dst, dummy);
        break;
    case CC_OP_ICM:
    case CC_OP_LTGT_32:
    case CC_OP_LTGT_64:
    case CC_OP_LTUGTU_32:
    case CC_OP_LTUGTU_64:
    case CC_OP_TM_32:
    case CC_OP_TM_64:
    case CC_OP_LTGT_F32:
    case CC_OP_LTGT_F64:
    case CC_OP_SLAG:
        /* 2 arguments */
        gen_helper_calc_cc(cc_op, local_cc_op, cc_src, cc_dst, dummy);
        break;
    case CC_OP_ADD_64:
    case CC_OP_ADDU_64:
    case CC_OP_SUB_64:
    case CC_OP_SUBU_64:
    case CC_OP_ADD_32:
    case CC_OP_ADDU_32:
    case CC_OP_SUB_32:
    case CC_OP_SUBU_32:
        /* 3 arguments */
        gen_helper_calc_cc(cc_op, local_cc_op, cc_src, cc_dst, cc_vr);
        break;
    case CC_OP_DYNAMIC:
        /* unknown operation - assume 3 arguments and cc_op in env */
        gen_helper_calc_cc(cc_op, cc_op, cc_src, cc_dst, cc_vr);
        break;
    default:
        tcg_abort();
    }

    tcg_temp_free_i32(local_cc_op);

    /* We now have cc in cc_op as constant */
    set_cc_static(s);
}

static inline void decode_rr(DisasContext *s, uint64_t insn, int *r1, int *r2)
{
    debug_insn(insn);

    *r1 = (insn >> 4) & 0xf;
    *r2 = insn & 0xf;
}

static inline TCGv_i64 decode_rx(DisasContext *s, uint64_t insn, int *r1,
                                 int *x2, int *b2, int *d2)
{
    debug_insn(insn);

    *r1 = (insn >> 20) & 0xf;
    *x2 = (insn >> 16) & 0xf;
    *b2 = (insn >> 12) & 0xf;
    *d2 = insn & 0xfff;

    return get_address(s, *x2, *b2, *d2);
}

static inline void decode_rs(DisasContext *s, uint64_t insn, int *r1, int *r3,
                             int *b2, int *d2)
{
    debug_insn(insn);

    *r1 = (insn >> 20) & 0xf;
    /* aka m3 */
    *r3 = (insn >> 16) & 0xf;
    *b2 = (insn >> 12) & 0xf;
    *d2 = insn & 0xfff;
}

static inline TCGv_i64 decode_si(DisasContext *s, uint64_t insn, int *i2,
                                 int *b1, int *d1)
{
    debug_insn(insn);

    *i2 = (insn >> 16) & 0xff;
    *b1 = (insn >> 12) & 0xf;
    *d1 = insn & 0xfff;

    return get_address(s, 0, *b1, *d1);
}

static inline void gen_goto_tb(DisasContext *s, int tb_num, target_ulong pc)
{
    TranslationBlock *tb;

    gen_update_cc_op(s);

    tb = s->tb;
    /* NOTE: we handle the case where the TB spans two pages here */
    if ((pc & TARGET_PAGE_MASK) == (tb->pc & TARGET_PAGE_MASK) ||
        (pc & TARGET_PAGE_MASK) == ((s->pc - 1) & TARGET_PAGE_MASK))  {
        /* jump to same page: we can use a direct jump */
        tcg_gen_goto_tb(tb_num);
        tcg_gen_movi_i64(psw_addr, pc);
        tcg_gen_exit_tb((long)tb + tb_num);
    } else {
        /* jump to another page: currently not optimized */
        tcg_gen_movi_i64(psw_addr, pc);
        tcg_gen_exit_tb(0);
    }
}

static inline void account_noninline_branch(DisasContext *s, int cc_op)
{
#ifdef DEBUG_INLINE_BRANCHES
    inline_branch_miss[cc_op]++;
#endif
}

static inline void account_inline_branch(DisasContext *s)
{
#ifdef DEBUG_INLINE_BRANCHES
    inline_branch_hit[s->cc_op]++;
#endif
}

static void gen_jcc(DisasContext *s, uint32_t mask, int skip)
{
    TCGv_i32 tmp, tmp2, r;
    TCGv_i64 tmp64;
    int old_cc_op;

    switch (s->cc_op) {
    case CC_OP_LTGT0_32:
        tmp = tcg_temp_new_i32();
        tcg_gen_trunc_i64_i32(tmp, cc_dst);
        switch (mask) {
        case 0x8 | 0x4: /* dst <= 0 */
            tcg_gen_brcondi_i32(TCG_COND_GT, tmp, 0, skip);
            break;
        case 0x8 | 0x2: /* dst >= 0 */
            tcg_gen_brcondi_i32(TCG_COND_LT, tmp, 0, skip);
            break;
        case 0x8: /* dst == 0 */
            tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, skip);
            break;
        case 0x7: /* dst != 0 */
        case 0x6: /* dst != 0 */
            tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, skip);
            break;
        case 0x4: /* dst < 0 */
            tcg_gen_brcondi_i32(TCG_COND_GE, tmp, 0, skip);
            break;
        case 0x2: /* dst > 0 */
            tcg_gen_brcondi_i32(TCG_COND_LE, tmp, 0, skip);
            break;
        default:
            tcg_temp_free_i32(tmp);
            goto do_dynamic;
        }
        account_inline_branch(s);
        tcg_temp_free_i32(tmp);
        break;
    case CC_OP_LTGT0_64:
        switch (mask) {
        case 0x8 | 0x4: /* dst <= 0 */
            tcg_gen_brcondi_i64(TCG_COND_GT, cc_dst, 0, skip);
            break;
        case 0x8 | 0x2: /* dst >= 0 */
            tcg_gen_brcondi_i64(TCG_COND_LT, cc_dst, 0, skip);
            break;
        case 0x8: /* dst == 0 */
            tcg_gen_brcondi_i64(TCG_COND_NE, cc_dst, 0, skip);
            break;
        case 0x7: /* dst != 0 */
        case 0x6: /* dst != 0 */
            tcg_gen_brcondi_i64(TCG_COND_EQ, cc_dst, 0, skip);
            break;
        case 0x4: /* dst < 0 */
            tcg_gen_brcondi_i64(TCG_COND_GE, cc_dst, 0, skip);
            break;
        case 0x2: /* dst > 0 */
            tcg_gen_brcondi_i64(TCG_COND_LE, cc_dst, 0, skip);
            break;
        default:
            goto do_dynamic;
        }
        account_inline_branch(s);
        break;
    case CC_OP_LTGT_32:
        tmp = tcg_temp_new_i32();
        tmp2 = tcg_temp_new_i32();
        tcg_gen_trunc_i64_i32(tmp, cc_src);
        tcg_gen_trunc_i64_i32(tmp2, cc_dst);
        switch (mask) {
        case 0x8 | 0x4: /* src <= dst */
            tcg_gen_brcond_i32(TCG_COND_GT, tmp, tmp2, skip);
            break;
        case 0x8 | 0x2: /* src >= dst */
            tcg_gen_brcond_i32(TCG_COND_LT, tmp, tmp2, skip);
            break;
        case 0x8: /* src == dst */
            tcg_gen_brcond_i32(TCG_COND_NE, tmp, tmp2, skip);
            break;
        case 0x7: /* src != dst */
        case 0x6: /* src != dst */
            tcg_gen_brcond_i32(TCG_COND_EQ, tmp, tmp2, skip);
            break;
        case 0x4: /* src < dst */
            tcg_gen_brcond_i32(TCG_COND_GE, tmp, tmp2, skip);
            break;
        case 0x2: /* src > dst */
            tcg_gen_brcond_i32(TCG_COND_LE, tmp, tmp2, skip);
            break;
        default:
            tcg_temp_free_i32(tmp);
            tcg_temp_free_i32(tmp2);
            goto do_dynamic;
        }
        account_inline_branch(s);
        tcg_temp_free_i32(tmp);
        tcg_temp_free_i32(tmp2);
        break;
    case CC_OP_LTGT_64:
        switch (mask) {
        case 0x8 | 0x4: /* src <= dst */
            tcg_gen_brcond_i64(TCG_COND_GT, cc_src, cc_dst, skip);
            break;
        case 0x8 | 0x2: /* src >= dst */
            tcg_gen_brcond_i64(TCG_COND_LT, cc_src, cc_dst, skip);
            break;
        case 0x8: /* src == dst */
            tcg_gen_brcond_i64(TCG_COND_NE, cc_src, cc_dst, skip);
            break;
        case 0x7: /* src != dst */
        case 0x6: /* src != dst */
            tcg_gen_brcond_i64(TCG_COND_EQ, cc_src, cc_dst, skip);
            break;
        case 0x4: /* src < dst */
            tcg_gen_brcond_i64(TCG_COND_GE, cc_src, cc_dst, skip);
            break;
        case 0x2: /* src > dst */
            tcg_gen_brcond_i64(TCG_COND_LE, cc_src, cc_dst, skip);
            break;
        default:
            goto do_dynamic;
        }
        account_inline_branch(s);
        break;
    case CC_OP_LTUGTU_32:
        tmp = tcg_temp_new_i32();
        tmp2 = tcg_temp_new_i32();
        tcg_gen_trunc_i64_i32(tmp, cc_src);
        tcg_gen_trunc_i64_i32(tmp2, cc_dst);
        switch (mask) {
        case 0x8 | 0x4: /* src <= dst */
            tcg_gen_brcond_i32(TCG_COND_GTU, tmp, tmp2, skip);
            break;
        case 0x8 | 0x2: /* src >= dst */
            tcg_gen_brcond_i32(TCG_COND_LTU, tmp, tmp2, skip);
            break;
        case 0x8: /* src == dst */
            tcg_gen_brcond_i32(TCG_COND_NE, tmp, tmp2, skip);
            break;
        case 0x7: /* src != dst */
        case 0x6: /* src != dst */
            tcg_gen_brcond_i32(TCG_COND_EQ, tmp, tmp2, skip);
            break;
        case 0x4: /* src < dst */
            tcg_gen_brcond_i32(TCG_COND_GEU, tmp, tmp2, skip);
            break;
        case 0x2: /* src > dst */
            tcg_gen_brcond_i32(TCG_COND_LEU, tmp, tmp2, skip);
            break;
        default:
            tcg_temp_free_i32(tmp);
            tcg_temp_free_i32(tmp2);
            goto do_dynamic;
        }
        account_inline_branch(s);
        tcg_temp_free_i32(tmp);
        tcg_temp_free_i32(tmp2);
        break;
    case CC_OP_LTUGTU_64:
        switch (mask) {
        case 0x8 | 0x4: /* src <= dst */
            tcg_gen_brcond_i64(TCG_COND_GTU, cc_src, cc_dst, skip);
            break;
        case 0x8 | 0x2: /* src >= dst */
            tcg_gen_brcond_i64(TCG_COND_LTU, cc_src, cc_dst, skip);
            break;
        case 0x8: /* src == dst */
            tcg_gen_brcond_i64(TCG_COND_NE, cc_src, cc_dst, skip);
            break;
        case 0x7: /* src != dst */
        case 0x6: /* src != dst */
            tcg_gen_brcond_i64(TCG_COND_EQ, cc_src, cc_dst, skip);
            break;
        case 0x4: /* src < dst */
            tcg_gen_brcond_i64(TCG_COND_GEU, cc_src, cc_dst, skip);
            break;
        case 0x2: /* src > dst */
            tcg_gen_brcond_i64(TCG_COND_LEU, cc_src, cc_dst, skip);
            break;
        default:
            goto do_dynamic;
        }
        account_inline_branch(s);
        break;
    case CC_OP_NZ:
        switch (mask) {
        /* dst == 0 || dst != 0 */
        case 0x8 | 0x4:
        case 0x8 | 0x4 | 0x2:
        case 0x8 | 0x4 | 0x2 | 0x1:
        case 0x8 | 0x4 | 0x1:
            break;
        /* dst == 0 */
        case 0x8:
        case 0x8 | 0x2:
        case 0x8 | 0x2 | 0x1:
        case 0x8 | 0x1:
            tcg_gen_brcondi_i64(TCG_COND_NE, cc_dst, 0, skip);
            break;
        /* dst != 0 */
        case 0x4:
        case 0x4 | 0x2:
        case 0x4 | 0x2 | 0x1:
        case 0x4 | 0x1:
            tcg_gen_brcondi_i64(TCG_COND_EQ, cc_dst, 0, skip);
            break;
        default:
            goto do_dynamic;
        }
        account_inline_branch(s);
        break;
    case CC_OP_TM_32:
        tmp = tcg_temp_new_i32();
        tmp2 = tcg_temp_new_i32();

        tcg_gen_trunc_i64_i32(tmp, cc_src);
        tcg_gen_trunc_i64_i32(tmp2, cc_dst);
        tcg_gen_and_i32(tmp, tmp, tmp2);
        switch (mask) {
        case 0x8: /* val & mask == 0 */
            tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, skip);
            break;
        case 0x4 | 0x2 | 0x1: /* val & mask != 0 */
            tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, skip);
            break;
        default:
            tcg_temp_free_i32(tmp);
            tcg_temp_free_i32(tmp2);
            goto do_dynamic;
        }
        tcg_temp_free_i32(tmp);
        tcg_temp_free_i32(tmp2);
        account_inline_branch(s);
        break;
    case CC_OP_TM_64:
        tmp64 = tcg_temp_new_i64();

        tcg_gen_and_i64(tmp64, cc_src, cc_dst);
        switch (mask) {
        case 0x8: /* val & mask == 0 */
            tcg_gen_brcondi_i64(TCG_COND_NE, tmp64, 0, skip);
            break;
        case 0x4 | 0x2 | 0x1: /* val & mask != 0 */
            tcg_gen_brcondi_i64(TCG_COND_EQ, tmp64, 0, skip);
            break;
        default:
            tcg_temp_free_i64(tmp64);
            goto do_dynamic;
        }
        tcg_temp_free_i64(tmp64);
        account_inline_branch(s);
        break;
    case CC_OP_ICM:
        switch (mask) {
        case 0x8: /* val == 0 */
            tcg_gen_brcondi_i64(TCG_COND_NE, cc_dst, 0, skip);
            break;
        case 0x4 | 0x2 | 0x1: /* val != 0 */
        case 0x4 | 0x2: /* val != 0 */
            tcg_gen_brcondi_i64(TCG_COND_EQ, cc_dst, 0, skip);
            break;
        default:
            goto do_dynamic;
        }
        account_inline_branch(s);
        break;
    case CC_OP_STATIC:
        old_cc_op = s->cc_op;
        goto do_dynamic_nocccalc;
    case CC_OP_DYNAMIC:
    default:
do_dynamic:
        old_cc_op = s->cc_op;
        /* calculate cc value */
        gen_op_calc_cc(s);

do_dynamic_nocccalc:
        /* jump based on cc */
        account_noninline_branch(s, old_cc_op);

        switch (mask) {
        case 0x8 | 0x4 | 0x2 | 0x1:
            /* always true */
            break;
        case 0x8 | 0x4 | 0x2: /* cc != 3 */
            tcg_gen_brcondi_i32(TCG_COND_EQ, cc_op, 3, skip);
            break;
        case 0x8 | 0x4 | 0x1: /* cc != 2 */
            tcg_gen_brcondi_i32(TCG_COND_EQ, cc_op, 2, skip);
            break;
        case 0x8 | 0x2 | 0x1: /* cc != 1 */
            tcg_gen_brcondi_i32(TCG_COND_EQ, cc_op, 1, skip);
            break;
        case 0x8 | 0x2: /* cc == 0 || cc == 2 */
            tmp = tcg_temp_new_i32();
            tcg_gen_andi_i32(tmp, cc_op, 1);
            tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, skip);
            tcg_temp_free_i32(tmp);
            break;
        case 0x8 | 0x4: /* cc < 2 */
            tcg_gen_brcondi_i32(TCG_COND_GEU, cc_op, 2, skip);
            break;
        case 0x8: /* cc == 0 */
            tcg_gen_brcondi_i32(TCG_COND_NE, cc_op, 0, skip);
            break;
        case 0x4 | 0x2 | 0x1: /* cc != 0 */
            tcg_gen_brcondi_i32(TCG_COND_EQ, cc_op, 0, skip);
            break;
        case 0x4 | 0x1: /* cc == 1 || cc == 3 */
            tmp = tcg_temp_new_i32();
            tcg_gen_andi_i32(tmp, cc_op, 1);
            tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, skip);
            tcg_temp_free_i32(tmp);
            break;
        case 0x4: /* cc == 1 */
            tcg_gen_brcondi_i32(TCG_COND_NE, cc_op, 1, skip);
            break;
        case 0x2 | 0x1: /* cc > 1 */
            tcg_gen_brcondi_i32(TCG_COND_LEU, cc_op, 1, skip);
            break;
        case 0x2: /* cc == 2 */
            tcg_gen_brcondi_i32(TCG_COND_NE, cc_op, 2, skip);
            break;
        case 0x1: /* cc == 3 */
            tcg_gen_brcondi_i32(TCG_COND_NE, cc_op, 3, skip);
            break;
        default: /* cc is masked by something else */
            tmp = tcg_const_i32(3);
            /* 3 - cc */
            tcg_gen_sub_i32(tmp, tmp, cc_op);
            tmp2 = tcg_const_i32(1);
            /* 1 << (3 - cc) */
            tcg_gen_shl_i32(tmp2, tmp2, tmp);
            r = tcg_const_i32(mask);
            /* mask & (1 << (3 - cc)) */
            tcg_gen_and_i32(r, r, tmp2);
            tcg_temp_free_i32(tmp);
            tcg_temp_free_i32(tmp2);

            tcg_gen_brcondi_i32(TCG_COND_EQ, r, 0, skip);
            tcg_temp_free_i32(r);
            break;
        }
        break;
    }
}

static void gen_bcr(DisasContext *s, uint32_t mask, TCGv_i64 target,
                    uint64_t offset)
{
    int skip;

    if (mask == 0xf) {
        /* unconditional */
        tcg_gen_mov_i64(psw_addr, target);
        tcg_gen_exit_tb(0);
    } else if (mask == 0) {
        /* ignore cc and never match */
        gen_goto_tb(s, 0, offset + 2);
    } else {
        TCGv_i64 new_addr = tcg_temp_local_new_i64();

        tcg_gen_mov_i64(new_addr, target);
        skip = gen_new_label();
        gen_jcc(s, mask, skip);
        tcg_gen_mov_i64(psw_addr, new_addr);
        tcg_temp_free_i64(new_addr);
        tcg_gen_exit_tb(0);
        gen_set_label(skip);
        tcg_temp_free_i64(new_addr);
        gen_goto_tb(s, 1, offset + 2);
    }
}

static void gen_brc(uint32_t mask, DisasContext *s, int32_t offset)
{
    int skip;

    if (mask == 0xf) {
        /* unconditional */
        gen_goto_tb(s, 0, s->pc + offset);
    } else if (mask == 0) {
        /* ignore cc and never match */
        gen_goto_tb(s, 0, s->pc + 4);
    } else {
        skip = gen_new_label();
        gen_jcc(s, mask, skip);
        gen_goto_tb(s, 0, s->pc + offset);
        gen_set_label(skip);
        gen_goto_tb(s, 1, s->pc + 4);
    }
    s->is_jmp = DISAS_TB_JUMP;
}

static void gen_op_mvc(DisasContext *s, int l, TCGv_i64 s1, TCGv_i64 s2)
{
    TCGv_i64 tmp, tmp2;
    int i;
    int l_memset = gen_new_label();
    int l_out = gen_new_label();
    TCGv_i64 dest = tcg_temp_local_new_i64();
    TCGv_i64 src = tcg_temp_local_new_i64();
    TCGv_i32 vl;

    /* Find out if we should use the inline version of mvc */
    switch (l) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 11:
    case 15:
        /* use inline */
        break;
    default:
        /* Fall back to helper */
        vl = tcg_const_i32(l);
        potential_page_fault(s);
        gen_helper_mvc(vl, s1, s2);
        tcg_temp_free_i32(vl);
        return;
    }

    tcg_gen_mov_i64(dest, s1);
    tcg_gen_mov_i64(src, s2);

    if (!(s->tb->flags & FLAG_MASK_64)) {
        /* XXX what if we overflow while moving? */
        tcg_gen_andi_i64(dest, dest, 0x7fffffffUL);
        tcg_gen_andi_i64(src, src, 0x7fffffffUL);
    }

    tmp = tcg_temp_new_i64();
    tcg_gen_addi_i64(tmp, src, 1);
    tcg_gen_brcond_i64(TCG_COND_EQ, dest, tmp, l_memset);
    tcg_temp_free_i64(tmp);

    switch (l) {
    case 0:
        tmp = tcg_temp_new_i64();

        tcg_gen_qemu_ld8u(tmp, src, get_mem_index(s));
        tcg_gen_qemu_st8(tmp, dest, get_mem_index(s));

        tcg_temp_free_i64(tmp);
        break;
    case 1:
        tmp = tcg_temp_new_i64();

        tcg_gen_qemu_ld16u(tmp, src, get_mem_index(s));
        tcg_gen_qemu_st16(tmp, dest, get_mem_index(s));

        tcg_temp_free_i64(tmp);
        break;
    case 3:
        tmp = tcg_temp_new_i64();

        tcg_gen_qemu_ld32u(tmp, src, get_mem_index(s));
        tcg_gen_qemu_st32(tmp, dest, get_mem_index(s));

        tcg_temp_free_i64(tmp);
        break;
    case 4:
        tmp = tcg_temp_new_i64();
        tmp2 = tcg_temp_new_i64();

        tcg_gen_qemu_ld32u(tmp, src, get_mem_index(s));
        tcg_gen_addi_i64(src, src, 4);
        tcg_gen_qemu_ld8u(tmp2, src, get_mem_index(s));
        tcg_gen_qemu_st32(tmp, dest, get_mem_index(s));
        tcg_gen_addi_i64(dest, dest, 4);
        tcg_gen_qemu_st8(tmp2, dest, get_mem_index(s));

        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 7:
        tmp = tcg_temp_new_i64();

        tcg_gen_qemu_ld64(tmp, src, get_mem_index(s));
        tcg_gen_qemu_st64(tmp, dest, get_mem_index(s));

        tcg_temp_free_i64(tmp);
        break;
    default:
        /* The inline version can become too big for too uneven numbers, only
           use it on known good lengths */
        tmp = tcg_temp_new_i64();
        tmp2 = tcg_const_i64(8);
        for (i = 0; (i + 7) <= l; i += 8) {
            tcg_gen_qemu_ld64(tmp, src, get_mem_index(s));
            tcg_gen_qemu_st64(tmp, dest, get_mem_index(s));

            tcg_gen_add_i64(src, src, tmp2);
            tcg_gen_add_i64(dest, dest, tmp2);
        }

        tcg_temp_free_i64(tmp2);
        tmp2 = tcg_const_i64(1);

        for (; i <= l; i++) {
            tcg_gen_qemu_ld8u(tmp, src, get_mem_index(s));
            tcg_gen_qemu_st8(tmp, dest, get_mem_index(s));

            tcg_gen_add_i64(src, src, tmp2);
            tcg_gen_add_i64(dest, dest, tmp2);
        }

        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp);
        break;
    }

    tcg_gen_br(l_out);

    gen_set_label(l_memset);
    /* memset case (dest == (src + 1)) */

    tmp = tcg_temp_new_i64();
    tmp2 = tcg_temp_new_i64();
    /* fill tmp with the byte */
    tcg_gen_qemu_ld8u(tmp, src, get_mem_index(s));
    tcg_gen_shli_i64(tmp2, tmp, 8);
    tcg_gen_or_i64(tmp, tmp, tmp2);
    tcg_gen_shli_i64(tmp2, tmp, 16);
    tcg_gen_or_i64(tmp, tmp, tmp2);
    tcg_gen_shli_i64(tmp2, tmp, 32);
    tcg_gen_or_i64(tmp, tmp, tmp2);
    tcg_temp_free_i64(tmp2);

    tmp2 = tcg_const_i64(8);

    for (i = 0; (i + 7) <= l; i += 8) {
        tcg_gen_qemu_st64(tmp, dest, get_mem_index(s));
        tcg_gen_addi_i64(dest, dest, 8);
    }

    tcg_temp_free_i64(tmp2);
    tmp2 = tcg_const_i64(1);

    for (; i <= l; i++) {
        tcg_gen_qemu_st8(tmp, dest, get_mem_index(s));
        tcg_gen_addi_i64(dest, dest, 1);
    }

    tcg_temp_free_i64(tmp2);
    tcg_temp_free_i64(tmp);

    gen_set_label(l_out);

    tcg_temp_free(dest);
    tcg_temp_free(src);
}

static void gen_op_clc(DisasContext *s, int l, TCGv_i64 s1, TCGv_i64 s2)
{
    TCGv_i64 tmp;
    TCGv_i64 tmp2;
    TCGv_i32 vl;

    /* check for simple 32bit or 64bit match */
    switch (l) {
    case 0:
        tmp = tcg_temp_new_i64();
        tmp2 = tcg_temp_new_i64();

        tcg_gen_qemu_ld8u(tmp, s1, get_mem_index(s));
        tcg_gen_qemu_ld8u(tmp2, s2, get_mem_index(s));
        cmp_u64(s, tmp, tmp2);

        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        return;
    case 1:
        tmp = tcg_temp_new_i64();
        tmp2 = tcg_temp_new_i64();

        tcg_gen_qemu_ld16u(tmp, s1, get_mem_index(s));
        tcg_gen_qemu_ld16u(tmp2, s2, get_mem_index(s));
        cmp_u64(s, tmp, tmp2);

        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        return;
    case 3:
        tmp = tcg_temp_new_i64();
        tmp2 = tcg_temp_new_i64();

        tcg_gen_qemu_ld32u(tmp, s1, get_mem_index(s));
        tcg_gen_qemu_ld32u(tmp2, s2, get_mem_index(s));
        cmp_u64(s, tmp, tmp2);

        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        return;
    case 7:
        tmp = tcg_temp_new_i64();
        tmp2 = tcg_temp_new_i64();

        tcg_gen_qemu_ld64(tmp, s1, get_mem_index(s));
        tcg_gen_qemu_ld64(tmp2, s2, get_mem_index(s));
        cmp_u64(s, tmp, tmp2);

        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        return;
    }

    potential_page_fault(s);
    vl = tcg_const_i32(l);
    gen_helper_clc(cc_op, vl, s1, s2);
    tcg_temp_free_i32(vl);
    set_cc_static(s);
}

static void disas_e3(DisasContext* s, int op, int r1, int x2, int b2, int d2)
{
    TCGv_i64 addr, tmp, tmp2, tmp3, tmp4;
    TCGv_i32 tmp32_1, tmp32_2, tmp32_3;

    LOG_DISAS("disas_e3: op 0x%x r1 %d x2 %d b2 %d d2 %d\n",
              op, r1, x2, b2, d2);
    addr = get_address(s, x2, b2, d2);
    switch (op) {
    case 0x2: /* LTG R1,D2(X2,B2) [RXY] */
    case 0x4: /* lg r1,d2(x2,b2) */
        tcg_gen_qemu_ld64(regs[r1], addr, get_mem_index(s));
        if (op == 0x2) {
            set_cc_s64(s, regs[r1]);
        }
        break;
    case 0x12: /* LT R1,D2(X2,B2) [RXY] */
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = tcg_temp_new_i32();
        tcg_gen_qemu_ld32s(tmp2, addr, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_1, tmp2);
        store_reg32(r1, tmp32_1);
        set_cc_s32(s, tmp32_1);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0xc: /* MSG      R1,D2(X2,B2)     [RXY] */
    case 0x1c: /* MSGF     R1,D2(X2,B2)     [RXY] */
        tmp2 = tcg_temp_new_i64();
        if (op == 0xc) {
            tcg_gen_qemu_ld64(tmp2, addr, get_mem_index(s));
        } else {
            tcg_gen_qemu_ld32s(tmp2, addr, get_mem_index(s));
        }
        tcg_gen_mul_i64(regs[r1], regs[r1], tmp2);
        tcg_temp_free_i64(tmp2);
        break;
    case 0xd: /* DSG      R1,D2(X2,B2)     [RXY] */
    case 0x1d: /* DSGF      R1,D2(X2,B2)     [RXY] */
        tmp2 = tcg_temp_new_i64();
        if (op == 0x1d) {
            tcg_gen_qemu_ld32s(tmp2, addr, get_mem_index(s));
        } else {
            tcg_gen_qemu_ld64(tmp2, addr, get_mem_index(s));
        }
        tmp4 = load_reg(r1 + 1);
        tmp3 = tcg_temp_new_i64();
        tcg_gen_div_i64(tmp3, tmp4, tmp2);
        store_reg(r1 + 1, tmp3);
        tcg_gen_rem_i64(tmp3, tmp4, tmp2);
        store_reg(r1, tmp3);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        tcg_temp_free_i64(tmp4);
        break;
    case 0x8: /* AG      R1,D2(X2,B2)     [RXY] */
    case 0xa: /* ALG      R1,D2(X2,B2)     [RXY] */
    case 0x18: /* AGF       R1,D2(X2,B2)     [RXY] */
    case 0x1a: /* ALGF      R1,D2(X2,B2)     [RXY] */
        if (op == 0x1a) {
            tmp2 = tcg_temp_new_i64();
            tcg_gen_qemu_ld32u(tmp2, addr, get_mem_index(s));
        } else if (op == 0x18) {
            tmp2 = tcg_temp_new_i64();
            tcg_gen_qemu_ld32s(tmp2, addr, get_mem_index(s));
        } else {
            tmp2 = tcg_temp_new_i64();
            tcg_gen_qemu_ld64(tmp2, addr, get_mem_index(s));
        }
        tmp4 = load_reg(r1);
        tmp3 = tcg_temp_new_i64();
        tcg_gen_add_i64(tmp3, tmp4, tmp2);
        store_reg(r1, tmp3);
        switch (op) {
        case 0x8:
        case 0x18:
            set_cc_add64(s, tmp4, tmp2, tmp3);
            break;
        case 0xa:
        case 0x1a:
            set_cc_addu64(s, tmp4, tmp2, tmp3);
            break;
        default:
            tcg_abort();
        }
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        tcg_temp_free_i64(tmp4);
        break;
    case 0x9: /* SG      R1,D2(X2,B2)     [RXY] */
    case 0xb: /* SLG      R1,D2(X2,B2)     [RXY] */
    case 0x19: /* SGF      R1,D2(X2,B2)     [RXY] */
    case 0x1b: /* SLGF     R1,D2(X2,B2)     [RXY] */
        tmp2 = tcg_temp_new_i64();
        if (op == 0x19) {
            tcg_gen_qemu_ld32s(tmp2, addr, get_mem_index(s));
        } else if (op == 0x1b) {
            tcg_gen_qemu_ld32u(tmp2, addr, get_mem_index(s));
        } else {
            tcg_gen_qemu_ld64(tmp2, addr, get_mem_index(s));
        }
        tmp4 = load_reg(r1);
        tmp3 = tcg_temp_new_i64();
        tcg_gen_sub_i64(tmp3, tmp4, tmp2);
        store_reg(r1, tmp3);
        switch (op) {
        case 0x9:
        case 0x19:
            set_cc_sub64(s, tmp4, tmp2, tmp3);
            break;
        case 0xb:
        case 0x1b:
            set_cc_subu64(s, tmp4, tmp2, tmp3);
            break;
        default:
            tcg_abort();
        }
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        tcg_temp_free_i64(tmp4);
        break;
    case 0xf: /* LRVG     R1,D2(X2,B2)     [RXE] */
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld64(tmp2, addr, get_mem_index(s));
        tcg_gen_bswap64_i64(tmp2, tmp2);
        store_reg(r1, tmp2);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x14: /* LGF      R1,D2(X2,B2)     [RXY] */
    case 0x16: /* LLGF      R1,D2(X2,B2)     [RXY] */
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld32u(tmp2, addr, get_mem_index(s));
        if (op == 0x14) {
            tcg_gen_ext32s_i64(tmp2, tmp2);
        }
        store_reg(r1, tmp2);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x15: /* LGH     R1,D2(X2,B2)     [RXY] */
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld16s(tmp2, addr, get_mem_index(s));
        store_reg(r1, tmp2);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x17: /* LLGT      R1,D2(X2,B2)     [RXY] */
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld32u(tmp2, addr, get_mem_index(s));
        tcg_gen_andi_i64(tmp2, tmp2, 0x7fffffffULL);
        store_reg(r1, tmp2);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x1e: /* LRV R1,D2(X2,B2) [RXY] */
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = tcg_temp_new_i32();
        tcg_gen_qemu_ld32u(tmp2, addr, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_1, tmp2);
        tcg_temp_free_i64(tmp2);
        tcg_gen_bswap32_i32(tmp32_1, tmp32_1);
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x1f: /* LRVH R1,D2(X2,B2) [RXY] */
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = tcg_temp_new_i32();
        tcg_gen_qemu_ld16u(tmp2, addr, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_1, tmp2);
        tcg_temp_free_i64(tmp2);
        tcg_gen_bswap16_i32(tmp32_1, tmp32_1);
        store_reg16(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x20: /* CG      R1,D2(X2,B2)     [RXY] */
    case 0x21: /* CLG      R1,D2(X2,B2) */
    case 0x30: /* CGF       R1,D2(X2,B2)     [RXY] */
    case 0x31: /* CLGF      R1,D2(X2,B2)     [RXY] */
        tmp2 = tcg_temp_new_i64();
        switch (op) {
        case 0x20:
        case 0x21:
            tcg_gen_qemu_ld64(tmp2, addr, get_mem_index(s));
            break;
        case 0x30:
            tcg_gen_qemu_ld32s(tmp2, addr, get_mem_index(s));
            break;
        case 0x31:
            tcg_gen_qemu_ld32u(tmp2, addr, get_mem_index(s));
            break;
        default:
            tcg_abort();
        }
        switch (op) {
        case 0x20:
        case 0x30:
            cmp_s64(s, regs[r1], tmp2);
            break;
        case 0x21:
        case 0x31:
            cmp_u64(s, regs[r1], tmp2);
            break;
        default:
            tcg_abort();
        }
        tcg_temp_free_i64(tmp2);
        break;
    case 0x24: /* stg r1, d2(x2,b2) */
        tcg_gen_qemu_st64(regs[r1], addr, get_mem_index(s));
        break;
    case 0x3e: /* STRV R1,D2(X2,B2) [RXY] */
        tmp32_1 = load_reg32(r1);
        tmp2 = tcg_temp_new_i64();
        tcg_gen_bswap32_i32(tmp32_1, tmp32_1);
        tcg_gen_extu_i32_i64(tmp2, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        tcg_gen_qemu_st32(tmp2, addr, get_mem_index(s));
        tcg_temp_free_i64(tmp2);
        break;
    case 0x50: /* STY  R1,D2(X2,B2) [RXY] */
        tmp32_1 = load_reg32(r1);
        tmp2 = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(tmp2, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        tcg_gen_qemu_st32(tmp2, addr, get_mem_index(s));
        tcg_temp_free_i64(tmp2);
        break;
    case 0x57: /* XY R1,D2(X2,B2) [RXY] */
        tmp32_1 = load_reg32(r1);
        tmp32_2 = tcg_temp_new_i32();
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld32u(tmp2, addr, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_2, tmp2);
        tcg_temp_free_i64(tmp2);
        tcg_gen_xor_i32(tmp32_2, tmp32_1, tmp32_2);
        store_reg32(r1, tmp32_2);
        set_cc_nz_u32(s, tmp32_2);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x58: /* LY R1,D2(X2,B2) [RXY] */
        tmp3 = tcg_temp_new_i64();
        tcg_gen_qemu_ld32u(tmp3, addr, get_mem_index(s));
        store_reg32_i64(r1, tmp3);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x5a: /* AY R1,D2(X2,B2) [RXY] */
    case 0x5b: /* SY R1,D2(X2,B2) [RXY] */
        tmp32_1 = load_reg32(r1);
        tmp32_2 = tcg_temp_new_i32();
        tmp32_3 = tcg_temp_new_i32();
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld32s(tmp2, addr, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_2, tmp2);
        tcg_temp_free_i64(tmp2);
        switch (op) {
        case 0x5a:
            tcg_gen_add_i32(tmp32_3, tmp32_1, tmp32_2);
            break;
        case 0x5b:
            tcg_gen_sub_i32(tmp32_3, tmp32_1, tmp32_2);
            break;
        default:
            tcg_abort();
        }
        store_reg32(r1, tmp32_3);
        switch (op) {
        case 0x5a:
            set_cc_add32(s, tmp32_1, tmp32_2, tmp32_3);
            break;
        case 0x5b:
            set_cc_sub32(s, tmp32_1, tmp32_2, tmp32_3);
            break;
        default:
            tcg_abort();
        }
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    case 0x71: /* LAY R1,D2(X2,B2) [RXY] */
        store_reg(r1, addr);
        break;
    case 0x72: /* STCY R1,D2(X2,B2) [RXY] */
        tmp32_1 = load_reg32(r1);
        tmp2 = tcg_temp_new_i64();
        tcg_gen_ext_i32_i64(tmp2, tmp32_1);
        tcg_gen_qemu_st8(tmp2, addr, get_mem_index(s));
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x73: /* ICY R1,D2(X2,B2) [RXY] */
        tmp3 = tcg_temp_new_i64();
        tcg_gen_qemu_ld8u(tmp3, addr, get_mem_index(s));
        store_reg8(r1, tmp3);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x76: /* LB R1,D2(X2,B2) [RXY] */
    case 0x77: /* LGB R1,D2(X2,B2) [RXY] */
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld8s(tmp2, addr, get_mem_index(s));
        switch (op) {
        case 0x76:
            tcg_gen_ext8s_i64(tmp2, tmp2);
            store_reg32_i64(r1, tmp2);
            break;
        case 0x77:
            tcg_gen_ext8s_i64(tmp2, tmp2);
            store_reg(r1, tmp2);
            break;
        default:
            tcg_abort();
        }
        tcg_temp_free_i64(tmp2);
        break;
    case 0x78: /* LHY R1,D2(X2,B2) [RXY] */
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld16s(tmp2, addr, get_mem_index(s));
        store_reg32_i64(r1, tmp2);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x80: /* NG      R1,D2(X2,B2)     [RXY] */
    case 0x81: /* OG      R1,D2(X2,B2)     [RXY] */
    case 0x82: /* XG      R1,D2(X2,B2)     [RXY] */
        tmp3 = tcg_temp_new_i64();
        tcg_gen_qemu_ld64(tmp3, addr, get_mem_index(s));
        switch (op) {
        case 0x80:
            tcg_gen_and_i64(regs[r1], regs[r1], tmp3);
            break;
        case 0x81:
            tcg_gen_or_i64(regs[r1], regs[r1], tmp3);
            break;
        case 0x82:
            tcg_gen_xor_i64(regs[r1], regs[r1], tmp3);
            break;
        default:
            tcg_abort();
        }
        set_cc_nz_u64(s, regs[r1]);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x86: /* MLG      R1,D2(X2,B2)     [RXY] */
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = tcg_const_i32(r1);
        tcg_gen_qemu_ld64(tmp2, addr, get_mem_index(s));
        gen_helper_mlg(tmp32_1, tmp2);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x87: /* DLG      R1,D2(X2,B2)     [RXY] */
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = tcg_const_i32(r1);
        tcg_gen_qemu_ld64(tmp2, addr, get_mem_index(s));
        gen_helper_dlg(tmp32_1, tmp2);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x88: /* ALCG      R1,D2(X2,B2)     [RXY] */
        tmp2 = tcg_temp_new_i64();
        tmp3 = tcg_temp_new_i64();
        tcg_gen_qemu_ld64(tmp2, addr, get_mem_index(s));
        /* XXX possible optimization point */
        gen_op_calc_cc(s);
        tcg_gen_extu_i32_i64(tmp3, cc_op);
        tcg_gen_shri_i64(tmp3, tmp3, 1);
        tcg_gen_andi_i64(tmp3, tmp3, 1);
        tcg_gen_add_i64(tmp3, tmp2, tmp3);
        tcg_gen_add_i64(tmp3, regs[r1], tmp3);
        store_reg(r1, tmp3);
        set_cc_addu64(s, regs[r1], tmp2, tmp3);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x89: /* SLBG      R1,D2(X2,B2)     [RXY] */
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = tcg_const_i32(r1);
        tcg_gen_qemu_ld64(tmp2, addr, get_mem_index(s));
        /* XXX possible optimization point */
        gen_op_calc_cc(s);
        gen_helper_slbg(cc_op, cc_op, tmp32_1, regs[r1], tmp2);
        set_cc_static(s);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x90: /* LLGC      R1,D2(X2,B2)     [RXY] */
        tcg_gen_qemu_ld8u(regs[r1], addr, get_mem_index(s));
        break;
    case 0x91: /* LLGH      R1,D2(X2,B2)     [RXY] */
        tcg_gen_qemu_ld16u(regs[r1], addr, get_mem_index(s));
        break;
    case 0x94: /* LLC     R1,D2(X2,B2)     [RXY] */
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld8u(tmp2, addr, get_mem_index(s));
        store_reg32_i64(r1, tmp2);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x95: /* LLH     R1,D2(X2,B2)     [RXY] */
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld16u(tmp2, addr, get_mem_index(s));
        store_reg32_i64(r1, tmp2);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x96: /* ML      R1,D2(X2,B2)     [RXY] */
        tmp2 = tcg_temp_new_i64();
        tmp3 = load_reg((r1 + 1) & 15);
        tcg_gen_ext32u_i64(tmp3, tmp3);
        tcg_gen_qemu_ld32u(tmp2, addr, get_mem_index(s));
        tcg_gen_mul_i64(tmp2, tmp2, tmp3);
        store_reg32_i64((r1 + 1) & 15, tmp2);
        tcg_gen_shri_i64(tmp2, tmp2, 32);
        store_reg32_i64(r1, tmp2);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x97: /* DL     R1,D2(X2,B2)     [RXY] */
        /* reg(r1) = reg(r1, r1+1) % ld32(addr) */
        /* reg(r1+1) = reg(r1, r1+1) / ld32(addr) */
        tmp = load_reg(r1);
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld32u(tmp2, addr, get_mem_index(s));
        tmp3 = load_reg((r1 + 1) & 15);
        tcg_gen_ext32u_i64(tmp2, tmp2);
        tcg_gen_ext32u_i64(tmp3, tmp3);
        tcg_gen_shli_i64(tmp, tmp, 32);
        tcg_gen_or_i64(tmp, tmp, tmp3);

        tcg_gen_rem_i64(tmp3, tmp, tmp2);
        tcg_gen_div_i64(tmp, tmp, tmp2);
        store_reg32_i64((r1 + 1) & 15, tmp);
        store_reg32_i64(r1, tmp3);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x98: /* ALC     R1,D2(X2,B2)     [RXY] */
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = load_reg32(r1);
        tmp32_2 = tcg_temp_new_i32();
        tmp32_3 = tcg_temp_new_i32();
        tcg_gen_qemu_ld32u(tmp2, addr, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_2, tmp2);
        /* XXX possible optimization point */
        gen_op_calc_cc(s);
        gen_helper_addc_u32(tmp32_3, cc_op, tmp32_1, tmp32_2);
        set_cc_addu32(s, tmp32_1, tmp32_2, tmp32_3);
        store_reg32(r1, tmp32_3);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    case 0x99: /* SLB     R1,D2(X2,B2)     [RXY] */
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_temp_new_i32();
        tcg_gen_qemu_ld32u(tmp2, addr, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_2, tmp2);
        /* XXX possible optimization point */
        gen_op_calc_cc(s);
        gen_helper_slb(cc_op, cc_op, tmp32_1, tmp32_2);
        set_cc_static(s);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    default:
        LOG_DISAS("illegal e3 operation 0x%x\n", op);
        gen_illegal_opcode(s, 3);
        break;
    }
    tcg_temp_free_i64(addr);
}

#ifndef CONFIG_USER_ONLY
static void disas_e5(DisasContext* s, uint64_t insn)
{
    TCGv_i64 tmp, tmp2;
    int op = (insn >> 32) & 0xff;

    tmp = get_address(s, 0, (insn >> 28) & 0xf, (insn >> 16) & 0xfff);
    tmp2 = get_address(s, 0, (insn >> 12) & 0xf, insn & 0xfff);

    LOG_DISAS("disas_e5: insn %" PRIx64 "\n", insn);
    switch (op) {
    case 0x01: /* TPROT    D1(B1),D2(B2)  [SSE] */
        /* Test Protection */
        potential_page_fault(s);
        gen_helper_tprot(cc_op, tmp, tmp2);
        set_cc_static(s);
        break;
    default:
        LOG_DISAS("illegal e5 operation 0x%x\n", op);
        gen_illegal_opcode(s, 3);
        break;
    }

    tcg_temp_free_i64(tmp);
    tcg_temp_free_i64(tmp2);
}
#endif

static void disas_eb(DisasContext *s, int op, int r1, int r3, int b2, int d2)
{
    TCGv_i64 tmp, tmp2, tmp3, tmp4;
    TCGv_i32 tmp32_1, tmp32_2;
    int i, stm_len;
    int ilc = 3;

    LOG_DISAS("disas_eb: op 0x%x r1 %d r3 %d b2 %d d2 0x%x\n",
              op, r1, r3, b2, d2);
    switch (op) {
    case 0xc: /* SRLG     R1,R3,D2(B2)     [RSY] */
    case 0xd: /* SLLG     R1,R3,D2(B2)     [RSY] */
    case 0xa: /* SRAG     R1,R3,D2(B2)     [RSY] */
    case 0xb: /* SLAG     R1,R3,D2(B2)     [RSY] */
    case 0x1c: /* RLLG     R1,R3,D2(B2)     [RSY] */
        if (b2) {
            tmp = get_address(s, 0, b2, d2);
            tcg_gen_andi_i64(tmp, tmp, 0x3f);
        } else {
            tmp = tcg_const_i64(d2 & 0x3f);
        }
        switch (op) {
        case 0xc:
            tcg_gen_shr_i64(regs[r1], regs[r3], tmp);
            break;
        case 0xd:
            tcg_gen_shl_i64(regs[r1], regs[r3], tmp);
            break;
        case 0xa:
            tcg_gen_sar_i64(regs[r1], regs[r3], tmp);
            break;
        case 0xb:
            tmp2 = tcg_temp_new_i64();
            tmp3 = tcg_temp_new_i64();
            gen_op_update2_cc_i64(s, CC_OP_SLAG, regs[r3], tmp);
            tcg_gen_shl_i64(tmp2, regs[r3], tmp);
            /* override sign bit with source sign */
            tcg_gen_andi_i64(tmp2, tmp2, ~0x8000000000000000ULL);
            tcg_gen_andi_i64(tmp3, regs[r3], 0x8000000000000000ULL);
            tcg_gen_or_i64(regs[r1], tmp2, tmp3);
            tcg_temp_free_i64(tmp2);
            tcg_temp_free_i64(tmp3);
            break;
        case 0x1c:
            tcg_gen_rotl_i64(regs[r1], regs[r3], tmp);
            break;
        default:
            tcg_abort();
            break;
        }
        if (op == 0xa) {
            set_cc_s64(s, regs[r1]);
        }
        tcg_temp_free_i64(tmp);
        break;
    case 0x1d: /* RLL    R1,R3,D2(B2)        [RSY] */
        if (b2) {
            tmp = get_address(s, 0, b2, d2);
            tcg_gen_andi_i64(tmp, tmp, 0x3f);
        } else {
            tmp = tcg_const_i64(d2 & 0x3f);
        }
        tmp32_1 = tcg_temp_new_i32();
        tmp32_2 = load_reg32(r3);
        tcg_gen_trunc_i64_i32(tmp32_1, tmp);
        switch (op) {
        case 0x1d:
            tcg_gen_rotl_i32(tmp32_1, tmp32_2, tmp32_1);
            break;
        default:
            tcg_abort();
            break;
        }
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x4:  /* LMG      R1,R3,D2(B2)     [RSE] */
    case 0x24: /* STMG     R1,R3,D2(B2)     [RSE] */
        stm_len = 8;
        goto do_mh;
    case 0x26: /* STMH     R1,R3,D2(B2)     [RSE] */
    case 0x96: /* LMH      R1,R3,D2(B2)     [RSE] */
        stm_len = 4;
do_mh:
        /* Apparently, unrolling lmg/stmg of any size gains performance -
           even for very long ones... */
        tmp = get_address(s, 0, b2, d2);
        tmp3 = tcg_const_i64(stm_len);
        tmp4 = tcg_const_i64(op == 0x26 ? 32 : 4);
        for (i = r1;; i = (i + 1) % 16) {
            switch (op) {
            case 0x4:
                tcg_gen_qemu_ld64(regs[i], tmp, get_mem_index(s));
                break;
            case 0x96:
                tmp2 = tcg_temp_new_i64();
#if HOST_LONG_BITS == 32
                tcg_gen_qemu_ld32u(tmp2, tmp, get_mem_index(s));
                tcg_gen_trunc_i64_i32(TCGV_HIGH(regs[i]), tmp2);
#else
                tcg_gen_qemu_ld32u(tmp2, tmp, get_mem_index(s));
                tcg_gen_shl_i64(tmp2, tmp2, tmp4);
                tcg_gen_ext32u_i64(regs[i], regs[i]);
                tcg_gen_or_i64(regs[i], regs[i], tmp2);
#endif
                tcg_temp_free_i64(tmp2);
                break;
            case 0x24:
                tcg_gen_qemu_st64(regs[i], tmp, get_mem_index(s));
                break;
            case 0x26:
                tmp2 = tcg_temp_new_i64();
                tcg_gen_shr_i64(tmp2, regs[i], tmp4);
                tcg_gen_qemu_st32(tmp2, tmp, get_mem_index(s));
                tcg_temp_free_i64(tmp2);
                break;
            default:
                tcg_abort();
            }
            if (i == r3) {
                break;
            }
            tcg_gen_add_i64(tmp, tmp, tmp3);
        }
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp3);
        tcg_temp_free_i64(tmp4);
        break;
    case 0x2c: /* STCMH R1,M3,D2(B2) [RSY] */
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r3);
        potential_page_fault(s);
        gen_helper_stcmh(tmp32_1, tmp, tmp32_2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
#ifndef CONFIG_USER_ONLY
    case 0x2f: /* LCTLG     R1,R3,D2(B2)     [RSE] */
        /* Load Control */
        check_privileged(s, ilc);
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r3);
        potential_page_fault(s);
        gen_helper_lctlg(tmp32_1, tmp, tmp32_2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x25: /* STCTG     R1,R3,D2(B2)     [RSE] */
        /* Store Control */
        check_privileged(s, ilc);
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r3);
        potential_page_fault(s);
        gen_helper_stctg(tmp32_1, tmp, tmp32_2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
#endif
    case 0x30: /* CSG     R1,R3,D2(B2)     [RSY] */
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r3);
        potential_page_fault(s);
        /* XXX rewrite in tcg */
        gen_helper_csg(cc_op, tmp32_1, tmp, tmp32_2);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x3e: /* CDSG R1,R3,D2(B2) [RSY] */
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r3);
        potential_page_fault(s);
        /* XXX rewrite in tcg */
        gen_helper_cdsg(cc_op, tmp32_1, tmp, tmp32_2);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x51: /* TMY D1(B1),I2 [SIY] */
        tmp = get_address(s, 0, b2, d2); /* SIY -> this is the destination */
        tmp2 = tcg_const_i64((r1 << 4) | r3);
        tcg_gen_qemu_ld8u(tmp, tmp, get_mem_index(s));
        /* yes, this is a 32 bit operation with 64 bit tcg registers, because
           that incurs less conversions */
        cmp_64(s, tmp, tmp2, CC_OP_TM_32);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x52: /* MVIY D1(B1),I2 [SIY] */
        tmp = get_address(s, 0, b2, d2); /* SIY -> this is the destination */
        tmp2 = tcg_const_i64((r1 << 4) | r3);
        tcg_gen_qemu_st8(tmp2, tmp, get_mem_index(s));
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x55: /* CLIY D1(B1),I2 [SIY] */
        tmp3 = get_address(s, 0, b2, d2); /* SIY -> this is the 1st operand */
        tmp = tcg_temp_new_i64();
        tmp32_1 = tcg_temp_new_i32();
        tcg_gen_qemu_ld8u(tmp, tmp3, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_1, tmp);
        cmp_u32c(s, tmp32_1, (r1 << 4) | r3);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp3);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x80: /* ICMH      R1,M3,D2(B2)     [RSY] */
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r3);
        potential_page_fault(s);
        /* XXX split CC calculation out */
        gen_helper_icmh(cc_op, tmp32_1, tmp, tmp32_2);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    default:
        LOG_DISAS("illegal eb operation 0x%x\n", op);
        gen_illegal_opcode(s, ilc);
        break;
    }
}

static void disas_ed(DisasContext *s, int op, int r1, int x2, int b2, int d2,
                     int r1b)
{
    TCGv_i32 tmp_r1, tmp32;
    TCGv_i64 addr, tmp;
    addr = get_address(s, x2, b2, d2);
    tmp_r1 = tcg_const_i32(r1);
    switch (op) {
    case 0x5: /* LXDB R1,D2(X2,B2) [RXE] */
        potential_page_fault(s);
        gen_helper_lxdb(tmp_r1, addr);
        break;
    case 0x9: /* CEB    R1,D2(X2,B2)       [RXE] */
        tmp = tcg_temp_new_i64();
        tmp32 = load_freg32(r1);
        tcg_gen_qemu_ld32u(tmp, addr, get_mem_index(s));
        set_cc_cmp_f32_i64(s, tmp32, tmp);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32);
        break;
    case 0xa: /* AEB    R1,D2(X2,B2)       [RXE] */
        tmp = tcg_temp_new_i64();
        tmp32 = tcg_temp_new_i32();
        tcg_gen_qemu_ld32u(tmp, addr, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32, tmp);
        gen_helper_aeb(tmp_r1, tmp32);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32);

        tmp32 = load_freg32(r1);
        set_cc_nz_f32(s, tmp32);
        tcg_temp_free_i32(tmp32);
        break;
    case 0xb: /* SEB    R1,D2(X2,B2)       [RXE] */
        tmp = tcg_temp_new_i64();
        tmp32 = tcg_temp_new_i32();
        tcg_gen_qemu_ld32u(tmp, addr, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32, tmp);
        gen_helper_seb(tmp_r1, tmp32);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32);

        tmp32 = load_freg32(r1);
        set_cc_nz_f32(s, tmp32);
        tcg_temp_free_i32(tmp32);
        break;
    case 0xd: /* DEB    R1,D2(X2,B2)       [RXE] */
        tmp = tcg_temp_new_i64();
        tmp32 = tcg_temp_new_i32();
        tcg_gen_qemu_ld32u(tmp, addr, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32, tmp);
        gen_helper_deb(tmp_r1, tmp32);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32);
        break;
    case 0x10: /* TCEB   R1,D2(X2,B2)       [RXE] */
        potential_page_fault(s);
        gen_helper_tceb(cc_op, tmp_r1, addr);
        set_cc_static(s);
        break;
    case 0x11: /* TCDB   R1,D2(X2,B2)       [RXE] */
        potential_page_fault(s);
        gen_helper_tcdb(cc_op, tmp_r1, addr);
        set_cc_static(s);
        break;
    case 0x12: /* TCXB   R1,D2(X2,B2)       [RXE] */
        potential_page_fault(s);
        gen_helper_tcxb(cc_op, tmp_r1, addr);
        set_cc_static(s);
        break;
    case 0x17: /* MEEB   R1,D2(X2,B2)       [RXE] */
        tmp = tcg_temp_new_i64();
        tmp32 = tcg_temp_new_i32();
        tcg_gen_qemu_ld32u(tmp, addr, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32, tmp);
        gen_helper_meeb(tmp_r1, tmp32);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32);
        break;
    case 0x19: /* CDB    R1,D2(X2,B2)       [RXE] */
        potential_page_fault(s);
        gen_helper_cdb(cc_op, tmp_r1, addr);
        set_cc_static(s);
        break;
    case 0x1a: /* ADB    R1,D2(X2,B2)       [RXE] */
        potential_page_fault(s);
        gen_helper_adb(cc_op, tmp_r1, addr);
        set_cc_static(s);
        break;
    case 0x1b: /* SDB    R1,D2(X2,B2)       [RXE] */
        potential_page_fault(s);
        gen_helper_sdb(cc_op, tmp_r1, addr);
        set_cc_static(s);
        break;
    case 0x1c: /* MDB    R1,D2(X2,B2)       [RXE] */
        potential_page_fault(s);
        gen_helper_mdb(tmp_r1, addr);
        break;
    case 0x1d: /* DDB    R1,D2(X2,B2)       [RXE] */
        potential_page_fault(s);
        gen_helper_ddb(tmp_r1, addr);
        break;
    case 0x1e: /* MADB  R1,R3,D2(X2,B2) [RXF] */
        /* for RXF insns, r1 is R3 and r1b is R1 */
        tmp32 = tcg_const_i32(r1b);
        potential_page_fault(s);
        gen_helper_madb(tmp32, addr, tmp_r1);
        tcg_temp_free_i32(tmp32);
        break;
    default:
        LOG_DISAS("illegal ed operation 0x%x\n", op);
        gen_illegal_opcode(s, 3);
        return;
    }
    tcg_temp_free_i32(tmp_r1);
    tcg_temp_free_i64(addr);
}

static void disas_a5(DisasContext *s, int op, int r1, int i2)
{
    TCGv_i64 tmp, tmp2;
    TCGv_i32 tmp32;
    LOG_DISAS("disas_a5: op 0x%x r1 %d i2 0x%x\n", op, r1, i2);
    switch (op) {
    case 0x0: /* IIHH     R1,I2     [RI] */
        tmp = tcg_const_i64(i2);
        tcg_gen_deposit_i64(regs[r1], regs[r1], tmp, 48, 16);
        break;
    case 0x1: /* IIHL     R1,I2     [RI] */
        tmp = tcg_const_i64(i2);
        tcg_gen_deposit_i64(regs[r1], regs[r1], tmp, 32, 16);
        break;
    case 0x2: /* IILH     R1,I2     [RI] */
        tmp = tcg_const_i64(i2);
        tcg_gen_deposit_i64(regs[r1], regs[r1], tmp, 16, 16);
        break;
    case 0x3: /* IILL     R1,I2     [RI] */
        tmp = tcg_const_i64(i2);
        tcg_gen_deposit_i64(regs[r1], regs[r1], tmp, 0, 16);
        break;
    case 0x4: /* NIHH     R1,I2     [RI] */
    case 0x8: /* OIHH     R1,I2     [RI] */
        tmp = load_reg(r1);
        tmp32 = tcg_temp_new_i32();
        switch (op) {
        case 0x4:
            tmp2 = tcg_const_i64((((uint64_t)i2) << 48)
                               | 0x0000ffffffffffffULL);
            tcg_gen_and_i64(tmp, tmp, tmp2);
            break;
        case 0x8:
            tmp2 = tcg_const_i64(((uint64_t)i2) << 48);
            tcg_gen_or_i64(tmp, tmp, tmp2);
            break;
        default:
            tcg_abort();
        }
        store_reg(r1, tmp);
        tcg_gen_shri_i64(tmp2, tmp, 48);
        tcg_gen_trunc_i64_i32(tmp32, tmp2);
        set_cc_nz_u32(s, tmp32);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32);
        break;
    case 0x5: /* NIHL     R1,I2     [RI] */
    case 0x9: /* OIHL     R1,I2     [RI] */
        tmp = load_reg(r1);
        tmp32 = tcg_temp_new_i32();
        switch (op) {
        case 0x5:
            tmp2 = tcg_const_i64((((uint64_t)i2) << 32)
                               | 0xffff0000ffffffffULL);
            tcg_gen_and_i64(tmp, tmp, tmp2);
            break;
        case 0x9:
            tmp2 = tcg_const_i64(((uint64_t)i2) << 32);
            tcg_gen_or_i64(tmp, tmp, tmp2);
            break;
        default:
            tcg_abort();
        }
        store_reg(r1, tmp);
        tcg_gen_shri_i64(tmp2, tmp, 32);
        tcg_gen_trunc_i64_i32(tmp32, tmp2);
        tcg_gen_andi_i32(tmp32, tmp32, 0xffff);
        set_cc_nz_u32(s, tmp32);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32);
        break;
    case 0x6: /* NILH     R1,I2     [RI] */
    case 0xa: /* OILH     R1,I2     [RI] */
        tmp = load_reg(r1);
        tmp32 = tcg_temp_new_i32();
        switch (op) {
        case 0x6:
            tmp2 = tcg_const_i64((((uint64_t)i2) << 16)
                               | 0xffffffff0000ffffULL);
            tcg_gen_and_i64(tmp, tmp, tmp2);
            break;
        case 0xa:
            tmp2 = tcg_const_i64(((uint64_t)i2) << 16);
            tcg_gen_or_i64(tmp, tmp, tmp2);
            break;
        default:
            tcg_abort();
        }
        store_reg(r1, tmp);
        tcg_gen_shri_i64(tmp, tmp, 16);
        tcg_gen_trunc_i64_i32(tmp32, tmp);
        tcg_gen_andi_i32(tmp32, tmp32, 0xffff);
        set_cc_nz_u32(s, tmp32);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32);
        break;
    case 0x7: /* NILL     R1,I2     [RI] */
    case 0xb: /* OILL     R1,I2     [RI] */
        tmp = load_reg(r1);
        tmp32 = tcg_temp_new_i32();
        switch (op) {
        case 0x7:
            tmp2 = tcg_const_i64(i2 | 0xffffffffffff0000ULL);
            tcg_gen_and_i64(tmp, tmp, tmp2);
            break;
        case 0xb:
            tmp2 = tcg_const_i64(i2);
            tcg_gen_or_i64(tmp, tmp, tmp2);
            break;
        default:
            tcg_abort();
        }
        store_reg(r1, tmp);
        tcg_gen_trunc_i64_i32(tmp32, tmp);
        tcg_gen_andi_i32(tmp32, tmp32, 0xffff);
        set_cc_nz_u32(s, tmp32);        /* signedness should not matter here */
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32);
        break;
    case 0xc: /* LLIHH     R1,I2     [RI] */
        tmp = tcg_const_i64( ((uint64_t)i2) << 48 );
        store_reg(r1, tmp);
        break;
    case 0xd: /* LLIHL     R1,I2     [RI] */
        tmp = tcg_const_i64( ((uint64_t)i2) << 32 );
        store_reg(r1, tmp);
        break;
    case 0xe: /* LLILH     R1,I2     [RI] */
        tmp = tcg_const_i64( ((uint64_t)i2) << 16 );
        store_reg(r1, tmp);
        break;
    case 0xf: /* LLILL     R1,I2     [RI] */
        tmp = tcg_const_i64(i2);
        store_reg(r1, tmp);
        break;
    default:
        LOG_DISAS("illegal a5 operation 0x%x\n", op);
        gen_illegal_opcode(s, 2);
        return;
    }
    tcg_temp_free_i64(tmp);
}

static void disas_a7(DisasContext *s, int op, int r1, int i2)
{
    TCGv_i64 tmp, tmp2;
    TCGv_i32 tmp32_1, tmp32_2, tmp32_3;
    int l1;

    LOG_DISAS("disas_a7: op 0x%x r1 %d i2 0x%x\n", op, r1, i2);
    switch (op) {
    case 0x0: /* TMLH or TMH     R1,I2     [RI] */
    case 0x1: /* TMLL or TML     R1,I2     [RI] */
    case 0x2: /* TMHH     R1,I2     [RI] */
    case 0x3: /* TMHL     R1,I2     [RI] */
        tmp = load_reg(r1);
        tmp2 = tcg_const_i64((uint16_t)i2);
        switch (op) {
        case 0x0:
            tcg_gen_shri_i64(tmp, tmp, 16);
            break;
        case 0x1:
            break;
        case 0x2:
            tcg_gen_shri_i64(tmp, tmp, 48);
            break;
        case 0x3:
            tcg_gen_shri_i64(tmp, tmp, 32);
            break;
        }
        tcg_gen_andi_i64(tmp, tmp, 0xffff);
        cmp_64(s, tmp, tmp2, CC_OP_TM_64);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x4: /* brc m1, i2 */
        gen_brc(r1, s, i2 * 2LL);
        return;
    case 0x5: /* BRAS     R1,I2     [RI] */
        tmp = tcg_const_i64(pc_to_link_info(s, s->pc + 4));
        store_reg(r1, tmp);
        tcg_temp_free_i64(tmp);
        gen_goto_tb(s, 0, s->pc + i2 * 2LL);
        s->is_jmp = DISAS_TB_JUMP;
        break;
    case 0x6: /* BRCT     R1,I2     [RI] */
        tmp32_1 = load_reg32(r1);
        tcg_gen_subi_i32(tmp32_1, tmp32_1, 1);
        store_reg32(r1, tmp32_1);
        gen_update_cc_op(s);
        l1 = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp32_1, 0, l1);
        gen_goto_tb(s, 0, s->pc + (i2 * 2LL));
        gen_set_label(l1);
        gen_goto_tb(s, 1, s->pc + 4);
        s->is_jmp = DISAS_TB_JUMP;
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x7: /* BRCTG     R1,I2     [RI] */
        tmp = load_reg(r1);
        tcg_gen_subi_i64(tmp, tmp, 1);
        store_reg(r1, tmp);
        gen_update_cc_op(s);
        l1 = gen_new_label();
        tcg_gen_brcondi_i64(TCG_COND_EQ, tmp, 0, l1);
        gen_goto_tb(s, 0, s->pc + (i2 * 2LL));
        gen_set_label(l1);
        gen_goto_tb(s, 1, s->pc + 4);
        s->is_jmp = DISAS_TB_JUMP;
        tcg_temp_free_i64(tmp);
        break;
    case 0x8: /* lhi r1, i2 */
        tmp32_1 = tcg_const_i32(i2);
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x9: /* lghi r1, i2 */
        tmp = tcg_const_i64(i2);
        store_reg(r1, tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0xa: /* AHI     R1,I2     [RI] */
        tmp32_1 = load_reg32(r1);
        tmp32_2 = tcg_temp_new_i32();
        tmp32_3 = tcg_const_i32(i2);

        if (i2 < 0) {
            tcg_gen_subi_i32(tmp32_2, tmp32_1, -i2);
        } else {
            tcg_gen_add_i32(tmp32_2, tmp32_1, tmp32_3);
        }

        store_reg32(r1, tmp32_2);
        set_cc_add32(s, tmp32_1, tmp32_3, tmp32_2);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    case 0xb: /* aghi r1, i2 */
        tmp = load_reg(r1);
        tmp2 = tcg_const_i64(i2);

        if (i2 < 0) {
            tcg_gen_subi_i64(regs[r1], tmp, -i2);
        } else {
            tcg_gen_add_i64(regs[r1], tmp, tmp2);
        }
        set_cc_add64(s, tmp, tmp2, regs[r1]);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0xc: /* MHI     R1,I2     [RI] */
        tmp32_1 = load_reg32(r1);
        tcg_gen_muli_i32(tmp32_1, tmp32_1, i2);
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0xd: /* MGHI     R1,I2     [RI] */
        tmp = load_reg(r1);
        tcg_gen_muli_i64(tmp, tmp, i2);
        store_reg(r1, tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0xe: /* CHI     R1,I2     [RI] */
        tmp32_1 = load_reg32(r1);
        cmp_s32c(s, tmp32_1, i2);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0xf: /* CGHI     R1,I2     [RI] */
        tmp = load_reg(r1);
        cmp_s64c(s, tmp, i2);
        tcg_temp_free_i64(tmp);
        break;
    default:
        LOG_DISAS("illegal a7 operation 0x%x\n", op);
        gen_illegal_opcode(s, 2);
        return;
    }
}

static void disas_b2(DisasContext *s, int op, uint32_t insn)
{
    TCGv_i64 tmp, tmp2, tmp3;
    TCGv_i32 tmp32_1, tmp32_2, tmp32_3;
    int r1, r2;
    int ilc = 2;
#ifndef CONFIG_USER_ONLY
    int r3, d2, b2;
#endif

    r1 = (insn >> 4) & 0xf;
    r2 = insn & 0xf;

    LOG_DISAS("disas_b2: op 0x%x r1 %d r2 %d\n", op, r1, r2);

    switch (op) {
    case 0x22: /* IPM    R1               [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        gen_op_calc_cc(s);
        gen_helper_ipm(cc_op, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x41: /* CKSM    R1,R2     [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r2);
        potential_page_fault(s);
        gen_helper_cksm(tmp32_1, tmp32_2);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        gen_op_movi_cc(s, 0);
        break;
    case 0x4e: /* SAR     R1,R2     [RRE] */
        tmp32_1 = load_reg32(r2);
        tcg_gen_st_i32(tmp32_1, cpu_env, offsetof(CPUState, aregs[r1]));
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x4f: /* EAR     R1,R2     [RRE] */
        tmp32_1 = tcg_temp_new_i32();
        tcg_gen_ld_i32(tmp32_1, cpu_env, offsetof(CPUState, aregs[r2]));
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x52: /* MSR     R1,R2     [RRE] */
        tmp32_1 = load_reg32(r1);
        tmp32_2 = load_reg32(r2);
        tcg_gen_mul_i32(tmp32_1, tmp32_1, tmp32_2);
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x54: /* MVPG     R1,R2     [RRE] */
        tmp = load_reg(0);
        tmp2 = load_reg(r1);
        tmp3 = load_reg(r2);
        potential_page_fault(s);
        gen_helper_mvpg(tmp, tmp2, tmp3);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        /* XXX check CCO bit and set CC accordingly */
        gen_op_movi_cc(s, 0);
        break;
    case 0x55: /* MVST     R1,R2     [RRE] */
        tmp32_1 = load_reg32(0);
        tmp32_2 = tcg_const_i32(r1);
        tmp32_3 = tcg_const_i32(r2);
        potential_page_fault(s);
        gen_helper_mvst(tmp32_1, tmp32_2, tmp32_3);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        gen_op_movi_cc(s, 1);
        break;
    case 0x5d: /* CLST     R1,R2     [RRE] */
        tmp32_1 = load_reg32(0);
        tmp32_2 = tcg_const_i32(r1);
        tmp32_3 = tcg_const_i32(r2);
        potential_page_fault(s);
        gen_helper_clst(cc_op, tmp32_1, tmp32_2, tmp32_3);
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    case 0x5e: /* SRST     R1,R2     [RRE] */
        tmp32_1 = load_reg32(0);
        tmp32_2 = tcg_const_i32(r1);
        tmp32_3 = tcg_const_i32(r2);
        potential_page_fault(s);
        gen_helper_srst(cc_op, tmp32_1, tmp32_2, tmp32_3);
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;

#ifndef CONFIG_USER_ONLY
    case 0x02: /* STIDP     D2(B2)     [S] */
        /* Store CPU ID */
        check_privileged(s, ilc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_stidp(tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x04: /* SCK       D2(B2)     [S] */
        /* Set Clock */
        check_privileged(s, ilc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_sck(cc_op, tmp);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        break;
    case 0x05: /* STCK     D2(B2)     [S] */
        /* Store Clock */
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_stck(cc_op, tmp);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        break;
    case 0x06: /* SCKC     D2(B2)     [S] */
        /* Set Clock Comparator */
        check_privileged(s, ilc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_sckc(tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x07: /* STCKC    D2(B2)     [S] */
        /* Store Clock Comparator */
        check_privileged(s, ilc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_stckc(tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x08: /* SPT      D2(B2)     [S] */
        /* Set CPU Timer */
        check_privileged(s, ilc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_spt(tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x09: /* STPT     D2(B2)     [S] */
        /* Store CPU Timer */
        check_privileged(s, ilc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_stpt(tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x0a: /* SPKA     D2(B2)     [S] */
        /* Set PSW Key from Address */
        check_privileged(s, ilc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp2 = tcg_temp_new_i64();
        tcg_gen_andi_i64(tmp2, psw_mask, ~PSW_MASK_KEY);
        tcg_gen_shli_i64(tmp, tmp, PSW_SHIFT_KEY - 4);
        tcg_gen_or_i64(psw_mask, tmp2, tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp);
        break;
    case 0x0d: /* PTLB                [S] */
        /* Purge TLB */
        check_privileged(s, ilc);
        gen_helper_ptlb();
        break;
    case 0x10: /* SPX      D2(B2)     [S] */
        /* Set Prefix Register */
        check_privileged(s, ilc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_spx(tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x11: /* STPX     D2(B2)     [S] */
        /* Store Prefix */
        check_privileged(s, ilc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp2 = tcg_temp_new_i64();
        tcg_gen_ld_i64(tmp2, cpu_env, offsetof(CPUState, psa));
        tcg_gen_qemu_st32(tmp2, tmp, get_mem_index(s));
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x12: /* STAP     D2(B2)     [S] */
        /* Store CPU Address */
        check_privileged(s, ilc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = tcg_temp_new_i32();
        tcg_gen_ld_i32(tmp32_1, cpu_env, offsetof(CPUState, cpu_num));
        tcg_gen_extu_i32_i64(tmp2, tmp32_1);
        tcg_gen_qemu_st32(tmp2, tmp, get_mem_index(s));
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x21: /* IPTE     R1,R2      [RRE] */
        /* Invalidate PTE */
        check_privileged(s, ilc);
        r1 = (insn >> 4) & 0xf;
        r2 = insn & 0xf;
        tmp = load_reg(r1);
        tmp2 = load_reg(r2);
        gen_helper_ipte(tmp, tmp2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x29: /* ISKE     R1,R2      [RRE] */
        /* Insert Storage Key Extended */
        check_privileged(s, ilc);
        r1 = (insn >> 4) & 0xf;
        r2 = insn & 0xf;
        tmp = load_reg(r2);
        tmp2 = tcg_temp_new_i64();
        gen_helper_iske(tmp2, tmp);
        store_reg(r1, tmp2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x2a: /* RRBE     R1,R2      [RRE] */
        /* Set Storage Key Extended */
        check_privileged(s, ilc);
        r1 = (insn >> 4) & 0xf;
        r2 = insn & 0xf;
        tmp32_1 = load_reg32(r1);
        tmp = load_reg(r2);
        gen_helper_rrbe(cc_op, tmp32_1, tmp);
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp);
        break;
    case 0x2b: /* SSKE     R1,R2      [RRE] */
        /* Set Storage Key Extended */
        check_privileged(s, ilc);
        r1 = (insn >> 4) & 0xf;
        r2 = insn & 0xf;
        tmp32_1 = load_reg32(r1);
        tmp = load_reg(r2);
        gen_helper_sske(tmp32_1, tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp);
        break;
    case 0x34: /* STCH ? */
        /* Store Subchannel */
        check_privileged(s, ilc);
        gen_op_movi_cc(s, 3);
        break;
    case 0x46: /* STURA    R1,R2      [RRE] */
        /* Store Using Real Address */
        check_privileged(s, ilc);
        r1 = (insn >> 4) & 0xf;
        r2 = insn & 0xf;
        tmp32_1 = load_reg32(r1);
        tmp = load_reg(r2);
        potential_page_fault(s);
        gen_helper_stura(tmp, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp);
        break;
    case 0x50: /* CSP      R1,R2      [RRE] */
        /* Compare And Swap And Purge */
        check_privileged(s, ilc);
        r1 = (insn >> 4) & 0xf;
        r2 = insn & 0xf;
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r2);
        gen_helper_csp(cc_op, tmp32_1, tmp32_2);
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x5f: /* CHSC ? */
        /* Channel Subsystem Call */
        check_privileged(s, ilc);
        gen_op_movi_cc(s, 3);
        break;
    case 0x78: /* STCKE    D2(B2)     [S] */
        /* Store Clock Extended */
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_stcke(cc_op, tmp);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        break;
    case 0x79: /* SACF    D2(B2)     [S] */
        /* Store Clock Extended */
        check_privileged(s, ilc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_sacf(tmp);
        tcg_temp_free_i64(tmp);
        /* addressing mode has changed, so end the block */
        s->pc += ilc * 2;
        update_psw_addr(s);
        s->is_jmp = DISAS_EXCP;
        break;
    case 0x7d: /* STSI     D2,(B2)     [S] */
        check_privileged(s, ilc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = load_reg32(0);
        tmp32_2 = load_reg32(1);
        potential_page_fault(s);
        gen_helper_stsi(cc_op, tmp, tmp32_1, tmp32_2);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x9d: /* LFPC      D2(B2)   [S] */
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = tcg_temp_new_i32();
        tcg_gen_qemu_ld32u(tmp2, tmp, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_1, tmp2);
        tcg_gen_st_i32(tmp32_1, cpu_env, offsetof(CPUState, fpc));
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0xb1: /* STFL     D2(B2)     [S] */
        /* Store Facility List (CPU features) at 200 */
        check_privileged(s, ilc);
        tmp2 = tcg_const_i64(0xc0000000);
        tmp = tcg_const_i64(200);
        tcg_gen_qemu_st32(tmp2, tmp, get_mem_index(s));
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp);
        break;
    case 0xb2: /* LPSWE    D2(B2)     [S] */
        /* Load PSW Extended */
        check_privileged(s, ilc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp2 = tcg_temp_new_i64();
        tmp3 = tcg_temp_new_i64();
        tcg_gen_qemu_ld64(tmp2, tmp, get_mem_index(s));
        tcg_gen_addi_i64(tmp, tmp, 8);
        tcg_gen_qemu_ld64(tmp3, tmp, get_mem_index(s));
        gen_helper_load_psw(tmp2, tmp3);
        /* we need to keep cc_op intact */
        s->is_jmp = DISAS_JUMP;
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x20: /* SERVC     R1,R2     [RRE] */
        /* SCLP Service call (PV hypercall) */
        check_privileged(s, ilc);
        potential_page_fault(s);
        tmp32_1 = load_reg32(r2);
        tmp = load_reg(r1);
        gen_helper_servc(cc_op, tmp32_1, tmp);
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp);
        break;
#endif
    default:
        LOG_DISAS("illegal b2 operation 0x%x\n", op);
        gen_illegal_opcode(s, ilc);
        break;
    }
}

static void disas_b3(DisasContext *s, int op, int m3, int r1, int r2)
{
    TCGv_i64 tmp;
    TCGv_i32 tmp32_1, tmp32_2, tmp32_3;
    LOG_DISAS("disas_b3: op 0x%x m3 0x%x r1 %d r2 %d\n", op, m3, r1, r2);
#define FP_HELPER(i) \
    tmp32_1 = tcg_const_i32(r1); \
    tmp32_2 = tcg_const_i32(r2); \
    gen_helper_ ## i (tmp32_1, tmp32_2); \
    tcg_temp_free_i32(tmp32_1); \
    tcg_temp_free_i32(tmp32_2);

#define FP_HELPER_CC(i) \
    tmp32_1 = tcg_const_i32(r1); \
    tmp32_2 = tcg_const_i32(r2); \
    gen_helper_ ## i (cc_op, tmp32_1, tmp32_2); \
    set_cc_static(s); \
    tcg_temp_free_i32(tmp32_1); \
    tcg_temp_free_i32(tmp32_2);

    switch (op) {
    case 0x0: /* LPEBR       R1,R2             [RRE] */
        FP_HELPER_CC(lpebr);
        break;
    case 0x2: /* LTEBR       R1,R2             [RRE] */
        FP_HELPER_CC(ltebr);
        break;
    case 0x3: /* LCEBR       R1,R2             [RRE] */
        FP_HELPER_CC(lcebr);
        break;
    case 0x4: /* LDEBR       R1,R2             [RRE] */
        FP_HELPER(ldebr);
        break;
    case 0x5: /* LXDBR       R1,R2             [RRE] */
        FP_HELPER(lxdbr);
        break;
    case 0x9: /* CEBR        R1,R2             [RRE] */
        FP_HELPER_CC(cebr);
        break;
    case 0xa: /* AEBR        R1,R2             [RRE] */
        FP_HELPER_CC(aebr);
        break;
    case 0xb: /* SEBR        R1,R2             [RRE] */
        FP_HELPER_CC(sebr);
        break;
    case 0xd: /* DEBR        R1,R2             [RRE] */
        FP_HELPER(debr);
        break;
    case 0x10: /* LPDBR       R1,R2             [RRE] */
        FP_HELPER_CC(lpdbr);
        break;
    case 0x12: /* LTDBR       R1,R2             [RRE] */
        FP_HELPER_CC(ltdbr);
        break;
    case 0x13: /* LCDBR       R1,R2             [RRE] */
        FP_HELPER_CC(lcdbr);
        break;
    case 0x15: /* SQBDR       R1,R2             [RRE] */
        FP_HELPER(sqdbr);
        break;
    case 0x17: /* MEEBR       R1,R2             [RRE] */
        FP_HELPER(meebr);
        break;
    case 0x19: /* CDBR        R1,R2             [RRE] */
        FP_HELPER_CC(cdbr);
        break;
    case 0x1a: /* ADBR        R1,R2             [RRE] */
        FP_HELPER_CC(adbr);
        break;
    case 0x1b: /* SDBR        R1,R2             [RRE] */
        FP_HELPER_CC(sdbr);
        break;
    case 0x1c: /* MDBR        R1,R2             [RRE] */
        FP_HELPER(mdbr);
        break;
    case 0x1d: /* DDBR        R1,R2             [RRE] */
        FP_HELPER(ddbr);
        break;
    case 0xe: /* MAEBR  R1,R3,R2 [RRF] */
    case 0x1e: /* MADBR R1,R3,R2 [RRF] */
    case 0x1f: /* MSDBR R1,R3,R2 [RRF] */
        /* for RRF insns, m3 is R1, r1 is R3, and r2 is R2 */
        tmp32_1 = tcg_const_i32(m3);
        tmp32_2 = tcg_const_i32(r2);
        tmp32_3 = tcg_const_i32(r1);
        switch (op) {
        case 0xe:
            gen_helper_maebr(tmp32_1, tmp32_3, tmp32_2);
            break;
        case 0x1e:
            gen_helper_madbr(tmp32_1, tmp32_3, tmp32_2);
            break;
        case 0x1f:
            gen_helper_msdbr(tmp32_1, tmp32_3, tmp32_2);
            break;
        default:
            tcg_abort();
        }
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    case 0x40: /* LPXBR       R1,R2             [RRE] */
        FP_HELPER_CC(lpxbr);
        break;
    case 0x42: /* LTXBR       R1,R2             [RRE] */
        FP_HELPER_CC(ltxbr);
        break;
    case 0x43: /* LCXBR       R1,R2             [RRE] */
        FP_HELPER_CC(lcxbr);
        break;
    case 0x44: /* LEDBR       R1,R2             [RRE] */
        FP_HELPER(ledbr);
        break;
    case 0x45: /* LDXBR       R1,R2             [RRE] */
        FP_HELPER(ldxbr);
        break;
    case 0x46: /* LEXBR       R1,R2             [RRE] */
        FP_HELPER(lexbr);
        break;
    case 0x49: /* CXBR        R1,R2             [RRE] */
        FP_HELPER_CC(cxbr);
        break;
    case 0x4a: /* AXBR        R1,R2             [RRE] */
        FP_HELPER_CC(axbr);
        break;
    case 0x4b: /* SXBR        R1,R2             [RRE] */
        FP_HELPER_CC(sxbr);
        break;
    case 0x4c: /* MXBR        R1,R2             [RRE] */
        FP_HELPER(mxbr);
        break;
    case 0x4d: /* DXBR        R1,R2             [RRE] */
        FP_HELPER(dxbr);
        break;
    case 0x65: /* LXR         R1,R2             [RRE] */
        tmp = load_freg(r2);
        store_freg(r1, tmp);
        tcg_temp_free_i64(tmp);
        tmp = load_freg(r2 + 2);
        store_freg(r1 + 2, tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x74: /* LZER        R1                [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        gen_helper_lzer(tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x75: /* LZDR        R1                [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        gen_helper_lzdr(tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x76: /* LZXR        R1                [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        gen_helper_lzxr(tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x84: /* SFPC        R1                [RRE] */
        tmp32_1 = load_reg32(r1);
        tcg_gen_st_i32(tmp32_1, cpu_env, offsetof(CPUState, fpc));
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x8c: /* EFPC        R1                [RRE] */
        tmp32_1 = tcg_temp_new_i32();
        tcg_gen_ld_i32(tmp32_1, cpu_env, offsetof(CPUState, fpc));
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x94: /* CEFBR       R1,R2             [RRE] */
    case 0x95: /* CDFBR       R1,R2             [RRE] */
    case 0x96: /* CXFBR       R1,R2             [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = load_reg32(r2);
        switch (op) {
        case 0x94:
            gen_helper_cefbr(tmp32_1, tmp32_2);
            break;
        case 0x95:
            gen_helper_cdfbr(tmp32_1, tmp32_2);
            break;
        case 0x96:
            gen_helper_cxfbr(tmp32_1, tmp32_2);
            break;
        default:
            tcg_abort();
        }
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x98: /* CFEBR       R1,R2             [RRE] */
    case 0x99: /* CFDBR              R1,R2             [RRE] */
    case 0x9a: /* CFXBR       R1,R2             [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r2);
        tmp32_3 = tcg_const_i32(m3);
        switch (op) {
        case 0x98:
            gen_helper_cfebr(cc_op, tmp32_1, tmp32_2, tmp32_3);
            break;
        case 0x99:
            gen_helper_cfdbr(cc_op, tmp32_1, tmp32_2, tmp32_3);
            break;
        case 0x9a:
            gen_helper_cfxbr(cc_op, tmp32_1, tmp32_2, tmp32_3);
            break;
        default:
            tcg_abort();
        }
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    case 0xa4: /* CEGBR       R1,R2             [RRE] */
    case 0xa5: /* CDGBR       R1,R2             [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        tmp = load_reg(r2);
        switch (op) {
        case 0xa4:
            gen_helper_cegbr(tmp32_1, tmp);
            break;
        case 0xa5:
            gen_helper_cdgbr(tmp32_1, tmp);
            break;
        default:
            tcg_abort();
        }
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp);
        break;
    case 0xa6: /* CXGBR       R1,R2             [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        tmp = load_reg(r2);
        gen_helper_cxgbr(tmp32_1, tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp);
        break;
    case 0xa8: /* CGEBR       R1,R2             [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r2);
        tmp32_3 = tcg_const_i32(m3);
        gen_helper_cgebr(cc_op, tmp32_1, tmp32_2, tmp32_3);
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    case 0xa9: /* CGDBR       R1,R2             [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r2);
        tmp32_3 = tcg_const_i32(m3);
        gen_helper_cgdbr(cc_op, tmp32_1, tmp32_2, tmp32_3);
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    case 0xaa: /* CGXBR       R1,R2             [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r2);
        tmp32_3 = tcg_const_i32(m3);
        gen_helper_cgxbr(cc_op, tmp32_1, tmp32_2, tmp32_3);
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    default:
        LOG_DISAS("illegal b3 operation 0x%x\n", op);
        gen_illegal_opcode(s, 2);
        break;
    }

#undef FP_HELPER_CC
#undef FP_HELPER
}

static void disas_b9(DisasContext *s, int op, int r1, int r2)
{
    TCGv_i64 tmp, tmp2, tmp3;
    TCGv_i32 tmp32_1, tmp32_2, tmp32_3;

    LOG_DISAS("disas_b9: op 0x%x r1 %d r2 %d\n", op, r1, r2);
    switch (op) {
    case 0x0: /* LPGR     R1,R2     [RRE] */
    case 0x1: /* LNGR     R1,R2     [RRE] */
    case 0x2: /* LTGR R1,R2 [RRE] */
    case 0x3: /* LCGR     R1,R2     [RRE] */
    case 0x10: /* LPGFR R1,R2 [RRE] */
    case 0x11: /* LNFGR     R1,R2     [RRE] */
    case 0x12: /* LTGFR R1,R2 [RRE] */
    case 0x13: /* LCGFR    R1,R2     [RRE] */
        if (op & 0x10) {
            tmp = load_reg32_i64(r2);
        } else {
            tmp = load_reg(r2);
        }
        switch (op & 0xf) {
        case 0x0: /* LP?GR */
            set_cc_abs64(s, tmp);
            gen_helper_abs_i64(tmp, tmp);
            store_reg(r1, tmp);
            break;
        case 0x1: /* LN?GR */
            set_cc_nabs64(s, tmp);
            gen_helper_nabs_i64(tmp, tmp);
            store_reg(r1, tmp);
            break;
        case 0x2: /* LT?GR */
            if (r1 != r2) {
                store_reg(r1, tmp);
            }
            set_cc_s64(s, tmp);
            break;
        case 0x3: /* LC?GR */
            tcg_gen_neg_i64(regs[r1], tmp);
            set_cc_comp64(s, regs[r1]);
            break;
        }
        tcg_temp_free_i64(tmp);
        break;
    case 0x4: /* LGR R1,R2 [RRE] */
        store_reg(r1, regs[r2]);
        break;
    case 0x6: /* LGBR R1,R2 [RRE] */
        tmp2 = load_reg(r2);
        tcg_gen_ext8s_i64(tmp2, tmp2);
        store_reg(r1, tmp2);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x8: /* AGR     R1,R2     [RRE] */
    case 0xa: /* ALGR     R1,R2     [RRE] */
        tmp = load_reg(r1);
        tmp2 = load_reg(r2);
        tmp3 = tcg_temp_new_i64();
        tcg_gen_add_i64(tmp3, tmp, tmp2);
        store_reg(r1, tmp3);
        switch (op) {
        case 0x8:
            set_cc_add64(s, tmp, tmp2, tmp3);
            break;
        case 0xa:
            set_cc_addu64(s, tmp, tmp2, tmp3);
            break;
        default:
            tcg_abort();
        }
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x9: /* SGR     R1,R2     [RRE] */
    case 0xb: /* SLGR     R1,R2     [RRE] */
    case 0x1b: /* SLGFR     R1,R2     [RRE] */
    case 0x19: /* SGFR     R1,R2     [RRE] */
        tmp = load_reg(r1);
        switch (op) {
        case 0x1b:
            tmp32_1 = load_reg32(r2);
            tmp2 = tcg_temp_new_i64();
            tcg_gen_extu_i32_i64(tmp2, tmp32_1);
            tcg_temp_free_i32(tmp32_1);
            break;
        case 0x19:
            tmp32_1 = load_reg32(r2);
            tmp2 = tcg_temp_new_i64();
            tcg_gen_ext_i32_i64(tmp2, tmp32_1);
            tcg_temp_free_i32(tmp32_1);
            break;
        default:
            tmp2 = load_reg(r2);
            break;
        }
        tmp3 = tcg_temp_new_i64();
        tcg_gen_sub_i64(tmp3, tmp, tmp2);
        store_reg(r1, tmp3);
        switch (op) {
        case 0x9:
        case 0x19:
            set_cc_sub64(s, tmp, tmp2, tmp3);
            break;
        case 0xb:
        case 0x1b:
            set_cc_subu64(s, tmp, tmp2, tmp3);
            break;
        default:
            tcg_abort();
        }
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0xc: /* MSGR      R1,R2     [RRE] */
    case 0x1c: /* MSGFR      R1,R2     [RRE] */
        tmp = load_reg(r1);
        tmp2 = load_reg(r2);
        if (op == 0x1c) {
            tcg_gen_ext32s_i64(tmp2, tmp2);
        }
        tcg_gen_mul_i64(tmp, tmp, tmp2);
        store_reg(r1, tmp);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0xd: /* DSGR      R1,R2     [RRE] */
    case 0x1d: /* DSGFR      R1,R2     [RRE] */
        tmp = load_reg(r1 + 1);
        if (op == 0xd) {
            tmp2 = load_reg(r2);
        } else {
            tmp32_1 = load_reg32(r2);
            tmp2 = tcg_temp_new_i64();
            tcg_gen_ext_i32_i64(tmp2, tmp32_1);
            tcg_temp_free_i32(tmp32_1);
        }
        tmp3 = tcg_temp_new_i64();
        tcg_gen_div_i64(tmp3, tmp, tmp2);
        store_reg(r1 + 1, tmp3);
        tcg_gen_rem_i64(tmp3, tmp, tmp2);
        store_reg(r1, tmp3);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x14: /* LGFR     R1,R2     [RRE] */
        tmp32_1 = load_reg32(r2);
        tmp = tcg_temp_new_i64();
        tcg_gen_ext_i32_i64(tmp, tmp32_1);
        store_reg(r1, tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp);
        break;
    case 0x16: /* LLGFR      R1,R2     [RRE] */
        tmp32_1 = load_reg32(r2);
        tmp = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(tmp, tmp32_1);
        store_reg(r1, tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp);
        break;
    case 0x17: /* LLGTR      R1,R2     [RRE] */
        tmp32_1 = load_reg32(r2);
        tmp = tcg_temp_new_i64();
        tcg_gen_andi_i32(tmp32_1, tmp32_1, 0x7fffffffUL);
        tcg_gen_extu_i32_i64(tmp, tmp32_1);
        store_reg(r1, tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp);
        break;
    case 0x18: /* AGFR     R1,R2     [RRE] */
    case 0x1a: /* ALGFR     R1,R2     [RRE] */
        tmp32_1 = load_reg32(r2);
        tmp2 = tcg_temp_new_i64();
        if (op == 0x18) {
            tcg_gen_ext_i32_i64(tmp2, tmp32_1);
        } else {
            tcg_gen_extu_i32_i64(tmp2, tmp32_1);
        }
        tcg_temp_free_i32(tmp32_1);
        tmp = load_reg(r1);
        tmp3 = tcg_temp_new_i64();
        tcg_gen_add_i64(tmp3, tmp, tmp2);
        store_reg(r1, tmp3);
        if (op == 0x18) {
            set_cc_add64(s, tmp, tmp2, tmp3);
        } else {
            set_cc_addu64(s, tmp, tmp2, tmp3);
        }
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x1f: /* LRVR     R1,R2     [RRE] */
        tmp32_1 = load_reg32(r2);
        tcg_gen_bswap32_i32(tmp32_1, tmp32_1);
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x20: /* CGR      R1,R2     [RRE] */
    case 0x30: /* CGFR     R1,R2     [RRE] */
        tmp2 = load_reg(r2);
        if (op == 0x30) {
            tcg_gen_ext32s_i64(tmp2, tmp2);
        }
        tmp = load_reg(r1);
        cmp_s64(s, tmp, tmp2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x21: /* CLGR     R1,R2     [RRE] */
    case 0x31: /* CLGFR    R1,R2     [RRE] */
        tmp2 = load_reg(r2);
        if (op == 0x31) {
            tcg_gen_ext32u_i64(tmp2, tmp2);
        }
        tmp = load_reg(r1);
        cmp_u64(s, tmp, tmp2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x26: /* LBR R1,R2 [RRE] */
        tmp32_1 = load_reg32(r2);
        tcg_gen_ext8s_i32(tmp32_1, tmp32_1);
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x27: /* LHR R1,R2 [RRE] */
        tmp32_1 = load_reg32(r2);
        tcg_gen_ext16s_i32(tmp32_1, tmp32_1);
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x80: /* NGR R1,R2 [RRE] */
    case 0x81: /* OGR R1,R2 [RRE] */
    case 0x82: /* XGR R1,R2 [RRE] */
        tmp = load_reg(r1);
        tmp2 = load_reg(r2);
        switch (op) {
        case 0x80:
            tcg_gen_and_i64(tmp, tmp, tmp2);
            break;
        case 0x81:
            tcg_gen_or_i64(tmp, tmp, tmp2);
            break;
        case 0x82:
            tcg_gen_xor_i64(tmp, tmp, tmp2);
            break;
        default:
            tcg_abort();
        }
        store_reg(r1, tmp);
        set_cc_nz_u64(s, tmp);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x83: /* FLOGR R1,R2 [RRE] */
        tmp = load_reg(r2);
        tmp32_1 = tcg_const_i32(r1);
        gen_helper_flogr(cc_op, tmp32_1, tmp);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x84: /* LLGCR R1,R2 [RRE] */
        tmp = load_reg(r2);
        tcg_gen_andi_i64(tmp, tmp, 0xff);
        store_reg(r1, tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x85: /* LLGHR R1,R2 [RRE] */
        tmp = load_reg(r2);
        tcg_gen_andi_i64(tmp, tmp, 0xffff);
        store_reg(r1, tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x87: /* DLGR      R1,R2     [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        tmp = load_reg(r2);
        gen_helper_dlg(tmp32_1, tmp);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x88: /* ALCGR     R1,R2     [RRE] */
        tmp = load_reg(r1);
        tmp2 = load_reg(r2);
        tmp3 = tcg_temp_new_i64();
        gen_op_calc_cc(s);
        tcg_gen_extu_i32_i64(tmp3, cc_op);
        tcg_gen_shri_i64(tmp3, tmp3, 1);
        tcg_gen_andi_i64(tmp3, tmp3, 1);
        tcg_gen_add_i64(tmp3, tmp2, tmp3);
        tcg_gen_add_i64(tmp3, tmp, tmp3);
        store_reg(r1, tmp3);
        set_cc_addu64(s, tmp, tmp2, tmp3);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x89: /* SLBGR   R1,R2     [RRE] */
        tmp = load_reg(r1);
        tmp2 = load_reg(r2);
        tmp32_1 = tcg_const_i32(r1);
        gen_op_calc_cc(s);
        gen_helper_slbg(cc_op, cc_op, tmp32_1, tmp, tmp2);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x94: /* LLCR R1,R2 [RRE] */
        tmp32_1 = load_reg32(r2);
        tcg_gen_andi_i32(tmp32_1, tmp32_1, 0xff);
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x95: /* LLHR R1,R2 [RRE] */
        tmp32_1 = load_reg32(r2);
        tcg_gen_andi_i32(tmp32_1, tmp32_1, 0xffff);
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x96: /* MLR     R1,R2     [RRE] */
        /* reg(r1, r1+1) = reg(r1+1) * reg(r2) */
        tmp2 = load_reg(r2);
        tmp3 = load_reg((r1 + 1) & 15);
        tcg_gen_ext32u_i64(tmp2, tmp2);
        tcg_gen_ext32u_i64(tmp3, tmp3);
        tcg_gen_mul_i64(tmp2, tmp2, tmp3);
        store_reg32_i64((r1 + 1) & 15, tmp2);
        tcg_gen_shri_i64(tmp2, tmp2, 32);
        store_reg32_i64(r1, tmp2);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x97: /* DLR     R1,R2     [RRE] */
        /* reg(r1) = reg(r1, r1+1) % reg(r2) */
        /* reg(r1+1) = reg(r1, r1+1) / reg(r2) */
        tmp = load_reg(r1);
        tmp2 = load_reg(r2);
        tmp3 = load_reg((r1 + 1) & 15);
        tcg_gen_ext32u_i64(tmp2, tmp2);
        tcg_gen_ext32u_i64(tmp3, tmp3);
        tcg_gen_shli_i64(tmp, tmp, 32);
        tcg_gen_or_i64(tmp, tmp, tmp3);

        tcg_gen_rem_i64(tmp3, tmp, tmp2);
        tcg_gen_div_i64(tmp, tmp, tmp2);
        store_reg32_i64((r1 + 1) & 15, tmp);
        store_reg32_i64(r1, tmp3);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x98: /* ALCR    R1,R2     [RRE] */
        tmp32_1 = load_reg32(r1);
        tmp32_2 = load_reg32(r2);
        tmp32_3 = tcg_temp_new_i32();
        /* XXX possible optimization point */
        gen_op_calc_cc(s);
        gen_helper_addc_u32(tmp32_3, cc_op, tmp32_1, tmp32_2);
        set_cc_addu32(s, tmp32_1, tmp32_2, tmp32_3);
        store_reg32(r1, tmp32_3);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    case 0x99: /* SLBR    R1,R2     [RRE] */
        tmp32_1 = load_reg32(r2);
        tmp32_2 = tcg_const_i32(r1);
        gen_op_calc_cc(s);
        gen_helper_slb(cc_op, cc_op, tmp32_2, tmp32_1);
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    default:
        LOG_DISAS("illegal b9 operation 0x%x\n", op);
        gen_illegal_opcode(s, 2);
        break;
    }
}

static void disas_c0(DisasContext *s, int op, int r1, int i2)
{
    TCGv_i64 tmp;
    TCGv_i32 tmp32_1, tmp32_2;
    uint64_t target = s->pc + i2 * 2LL;
    int l1;

    LOG_DISAS("disas_c0: op 0x%x r1 %d i2 %d\n", op, r1, i2);

    switch (op) {
    case 0: /* larl r1, i2 */
        tmp = tcg_const_i64(target);
        store_reg(r1, tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x1: /* LGFI R1,I2 [RIL] */
        tmp = tcg_const_i64((int64_t)i2);
        store_reg(r1, tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x4: /* BRCL     M1,I2     [RIL] */
        /* m1 & (1 << (3 - cc)) */
        tmp32_1 = tcg_const_i32(3);
        tmp32_2 = tcg_const_i32(1);
        gen_op_calc_cc(s);
        tcg_gen_sub_i32(tmp32_1, tmp32_1, cc_op);
        tcg_gen_shl_i32(tmp32_2, tmp32_2, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        tmp32_1 = tcg_const_i32(r1); /* m1 == r1 */
        tcg_gen_and_i32(tmp32_1, tmp32_1, tmp32_2);
        l1 = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp32_1, 0, l1);
        gen_goto_tb(s, 0, target);
        gen_set_label(l1);
        gen_goto_tb(s, 1, s->pc + 6);
        s->is_jmp = DISAS_TB_JUMP;
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x5: /* brasl r1, i2 */
        tmp = tcg_const_i64(pc_to_link_info(s, s->pc + 6));
        store_reg(r1, tmp);
        tcg_temp_free_i64(tmp);
        gen_goto_tb(s, 0, target);
        s->is_jmp = DISAS_TB_JUMP;
        break;
    case 0x7: /* XILF R1,I2 [RIL] */
    case 0xb: /* NILF R1,I2 [RIL] */
    case 0xd: /* OILF R1,I2 [RIL] */
        tmp32_1 = load_reg32(r1);
        switch (op) {
        case 0x7:
            tcg_gen_xori_i32(tmp32_1, tmp32_1, (uint32_t)i2);
            break;
        case 0xb:
            tcg_gen_andi_i32(tmp32_1, tmp32_1, (uint32_t)i2);
            break;
        case 0xd:
            tcg_gen_ori_i32(tmp32_1, tmp32_1, (uint32_t)i2);
            break;
        default:
            tcg_abort();
        }
        store_reg32(r1, tmp32_1);
        set_cc_nz_u32(s, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x9: /* IILF R1,I2 [RIL] */
        tmp32_1 = tcg_const_i32((uint32_t)i2);
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0xa: /* NIHF R1,I2 [RIL] */
        tmp = load_reg(r1);
        tmp32_1 = tcg_temp_new_i32();
        tcg_gen_andi_i64(tmp, tmp, (((uint64_t)((uint32_t)i2)) << 32)
                                   | 0xffffffffULL);
        store_reg(r1, tmp);
        tcg_gen_shri_i64(tmp, tmp, 32);
        tcg_gen_trunc_i64_i32(tmp32_1, tmp);
        set_cc_nz_u32(s, tmp32_1);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0xe: /* LLIHF R1,I2 [RIL] */
        tmp = tcg_const_i64(((uint64_t)(uint32_t)i2) << 32);
        store_reg(r1, tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0xf: /* LLILF R1,I2 [RIL] */
        tmp = tcg_const_i64((uint32_t)i2);
        store_reg(r1, tmp);
        tcg_temp_free_i64(tmp);
        break;
    default:
        LOG_DISAS("illegal c0 operation 0x%x\n", op);
        gen_illegal_opcode(s, 3);
        break;
    }
}

static void disas_c2(DisasContext *s, int op, int r1, int i2)
{
    TCGv_i64 tmp, tmp2, tmp3;
    TCGv_i32 tmp32_1, tmp32_2, tmp32_3;

    switch (op) {
    case 0x4: /* SLGFI R1,I2 [RIL] */
    case 0xa: /* ALGFI R1,I2 [RIL] */
        tmp = load_reg(r1);
        tmp2 = tcg_const_i64((uint64_t)(uint32_t)i2);
        tmp3 = tcg_temp_new_i64();
        switch (op) {
        case 0x4:
            tcg_gen_sub_i64(tmp3, tmp, tmp2);
            set_cc_subu64(s, tmp, tmp2, tmp3);
            break;
        case 0xa:
            tcg_gen_add_i64(tmp3, tmp, tmp2);
            set_cc_addu64(s, tmp, tmp2, tmp3);
            break;
        default:
            tcg_abort();
        }
        store_reg(r1, tmp3);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x5: /* SLFI R1,I2 [RIL] */
    case 0xb: /* ALFI R1,I2 [RIL] */
        tmp32_1 = load_reg32(r1);
        tmp32_2 = tcg_const_i32(i2);
        tmp32_3 = tcg_temp_new_i32();
        switch (op) {
        case 0x5:
            tcg_gen_sub_i32(tmp32_3, tmp32_1, tmp32_2);
            set_cc_subu32(s, tmp32_1, tmp32_2, tmp32_3);
            break;
        case 0xb:
            tcg_gen_add_i32(tmp32_3, tmp32_1, tmp32_2);
            set_cc_addu32(s, tmp32_1, tmp32_2, tmp32_3);
            break;
        default:
            tcg_abort();
        }
        store_reg32(r1, tmp32_3);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    case 0xc: /* CGFI R1,I2 [RIL] */
        tmp = load_reg(r1);
        cmp_s64c(s, tmp, (int64_t)i2);
        tcg_temp_free_i64(tmp);
        break;
    case 0xe: /* CLGFI R1,I2 [RIL] */
        tmp = load_reg(r1);
        cmp_u64c(s, tmp, (uint64_t)(uint32_t)i2);
        tcg_temp_free_i64(tmp);
        break;
    case 0xd: /* CFI R1,I2 [RIL] */
        tmp32_1 = load_reg32(r1);
        cmp_s32c(s, tmp32_1, i2);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0xf: /* CLFI R1,I2 [RIL] */
        tmp32_1 = load_reg32(r1);
        cmp_u32c(s, tmp32_1, i2);
        tcg_temp_free_i32(tmp32_1);
        break;
    default:
        LOG_DISAS("illegal c2 operation 0x%x\n", op);
        gen_illegal_opcode(s, 3);
        break;
    }
}

static void gen_and_or_xor_i32(int opc, TCGv_i32 tmp, TCGv_i32 tmp2)
{
    switch (opc & 0xf) {
    case 0x4:
        tcg_gen_and_i32(tmp, tmp, tmp2);
        break;
    case 0x6:
        tcg_gen_or_i32(tmp, tmp, tmp2);
        break;
    case 0x7:
        tcg_gen_xor_i32(tmp, tmp, tmp2);
        break;
    default:
        tcg_abort();
    }
}

static void disas_s390_insn(DisasContext *s)
{
    TCGv_i64 tmp, tmp2, tmp3, tmp4;
    TCGv_i32 tmp32_1, tmp32_2, tmp32_3, tmp32_4;
    unsigned char opc;
    uint64_t insn;
    int op, r1, r2, r3, d1, d2, x2, b1, b2, i, i2, r1b;
    TCGv_i32 vl;
    int ilc;
    int l1;

    opc = ldub_code(s->pc);
    LOG_DISAS("opc 0x%x\n", opc);

    ilc = get_ilc(opc);

    switch (opc) {
#ifndef CONFIG_USER_ONLY
    case 0x01: /* SAM */
        insn = ld_code2(s->pc);
        /* set addressing mode, but we only do 64bit anyways */
        break;
#endif
    case 0x6: /* BCTR     R1,R2     [RR] */
        insn = ld_code2(s->pc);
        decode_rr(s, insn, &r1, &r2);
        tmp32_1 = load_reg32(r1);
        tcg_gen_subi_i32(tmp32_1, tmp32_1, 1);
        store_reg32(r1, tmp32_1);

        if (r2) {
            gen_update_cc_op(s);
            l1 = gen_new_label();
            tcg_gen_brcondi_i32(TCG_COND_NE, tmp32_1, 0, l1);

            /* not taking the branch, jump to after the instruction */
            gen_goto_tb(s, 0, s->pc + 2);
            gen_set_label(l1);

            /* take the branch, move R2 into psw.addr */
            tmp32_1 = load_reg32(r2);
            tmp = tcg_temp_new_i64();
            tcg_gen_extu_i32_i64(tmp, tmp32_1);
            tcg_gen_mov_i64(psw_addr, tmp);
            s->is_jmp = DISAS_JUMP;
            tcg_temp_free_i32(tmp32_1);
            tcg_temp_free_i64(tmp);
        }
        break;
    case 0x7: /* BCR    M1,R2     [RR] */
        insn = ld_code2(s->pc);
        decode_rr(s, insn, &r1, &r2);
        if (r2) {
            tmp = load_reg(r2);
            gen_bcr(s, r1, tmp, s->pc);
            tcg_temp_free_i64(tmp);
            s->is_jmp = DISAS_TB_JUMP;
        } else {
            /* XXX: "serialization and checkpoint-synchronization function"? */
        }
        break;
    case 0xa: /* SVC    I         [RR] */
        insn = ld_code2(s->pc);
        debug_insn(insn);
        i = insn & 0xff;
        update_psw_addr(s);
        gen_op_calc_cc(s);
        tmp32_1 = tcg_const_i32(i);
        tmp32_2 = tcg_const_i32(ilc * 2);
        tmp32_3 = tcg_const_i32(EXCP_SVC);
        tcg_gen_st_i32(tmp32_1, cpu_env, offsetof(CPUState, int_svc_code));
        tcg_gen_st_i32(tmp32_2, cpu_env, offsetof(CPUState, int_svc_ilc));
        gen_helper_exception(tmp32_3);
        s->is_jmp = DISAS_EXCP;
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    case 0xd: /* BASR   R1,R2     [RR] */
        insn = ld_code2(s->pc);
        decode_rr(s, insn, &r1, &r2);
        tmp = tcg_const_i64(pc_to_link_info(s, s->pc + 2));
        store_reg(r1, tmp);
        if (r2) {
            tmp2 = load_reg(r2);
            tcg_gen_mov_i64(psw_addr, tmp2);
            tcg_temp_free_i64(tmp2);
            s->is_jmp = DISAS_JUMP;
        }
        tcg_temp_free_i64(tmp);
        break;
    case 0xe: /* MVCL   R1,R2     [RR] */
        insn = ld_code2(s->pc);
        decode_rr(s, insn, &r1, &r2);
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r2);
        potential_page_fault(s);
        gen_helper_mvcl(cc_op, tmp32_1, tmp32_2);
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x10: /* LPR    R1,R2     [RR] */
        insn = ld_code2(s->pc);
        decode_rr(s, insn, &r1, &r2);
        tmp32_1 = load_reg32(r2);
        set_cc_abs32(s, tmp32_1);
        gen_helper_abs_i32(tmp32_1, tmp32_1);
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x11: /* LNR    R1,R2     [RR] */
        insn = ld_code2(s->pc);
        decode_rr(s, insn, &r1, &r2);
        tmp32_1 = load_reg32(r2);
        set_cc_nabs32(s, tmp32_1);
        gen_helper_nabs_i32(tmp32_1, tmp32_1);
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x12: /* LTR    R1,R2     [RR] */
        insn = ld_code2(s->pc);
        decode_rr(s, insn, &r1, &r2);
        tmp32_1 = load_reg32(r2);
        if (r1 != r2) {
            store_reg32(r1, tmp32_1);
        }
        set_cc_s32(s, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x13: /* LCR    R1,R2     [RR] */
        insn = ld_code2(s->pc);
        decode_rr(s, insn, &r1, &r2);
        tmp32_1 = load_reg32(r2);
        tcg_gen_neg_i32(tmp32_1, tmp32_1);
        store_reg32(r1, tmp32_1);
        set_cc_comp32(s, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x14: /* NR     R1,R2     [RR] */
    case 0x16: /* OR     R1,R2     [RR] */
    case 0x17: /* XR     R1,R2     [RR] */
        insn = ld_code2(s->pc);
        decode_rr(s, insn, &r1, &r2);
        tmp32_2 = load_reg32(r2);
        tmp32_1 = load_reg32(r1);
        gen_and_or_xor_i32(opc, tmp32_1, tmp32_2);
        store_reg32(r1, tmp32_1);
        set_cc_nz_u32(s, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x18: /* LR     R1,R2     [RR] */
        insn = ld_code2(s->pc);
        decode_rr(s, insn, &r1, &r2);
        tmp32_1 = load_reg32(r2);
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x15: /* CLR    R1,R2     [RR] */
    case 0x19: /* CR     R1,R2     [RR] */
        insn = ld_code2(s->pc);
        decode_rr(s, insn, &r1, &r2);
        tmp32_1 = load_reg32(r1);
        tmp32_2 = load_reg32(r2);
        if (opc == 0x15) {
            cmp_u32(s, tmp32_1, tmp32_2);
        } else {
            cmp_s32(s, tmp32_1, tmp32_2);
        }
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x1a: /* AR     R1,R2     [RR] */
    case 0x1e: /* ALR    R1,R2     [RR] */
        insn = ld_code2(s->pc);
        decode_rr(s, insn, &r1, &r2);
        tmp32_1 = load_reg32(r1);
        tmp32_2 = load_reg32(r2);
        tmp32_3 = tcg_temp_new_i32();
        tcg_gen_add_i32(tmp32_3, tmp32_1, tmp32_2);
        store_reg32(r1, tmp32_3);
        if (opc == 0x1a) {
            set_cc_add32(s, tmp32_1, tmp32_2, tmp32_3);
        } else {
            set_cc_addu32(s, tmp32_1, tmp32_2, tmp32_3);
        }
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    case 0x1b: /* SR     R1,R2     [RR] */
    case 0x1f: /* SLR    R1,R2     [RR] */
        insn = ld_code2(s->pc);
        decode_rr(s, insn, &r1, &r2);
        tmp32_1 = load_reg32(r1);
        tmp32_2 = load_reg32(r2);
        tmp32_3 = tcg_temp_new_i32();
        tcg_gen_sub_i32(tmp32_3, tmp32_1, tmp32_2);
        store_reg32(r1, tmp32_3);
        if (opc == 0x1b) {
            set_cc_sub32(s, tmp32_1, tmp32_2, tmp32_3);
        } else {
            set_cc_subu32(s, tmp32_1, tmp32_2, tmp32_3);
        }
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    case 0x1c: /* MR     R1,R2     [RR] */
        /* reg(r1, r1+1) = reg(r1+1) * reg(r2) */
        insn = ld_code2(s->pc);
        decode_rr(s, insn, &r1, &r2);
        tmp2 = load_reg(r2);
        tmp3 = load_reg((r1 + 1) & 15);
        tcg_gen_ext32s_i64(tmp2, tmp2);
        tcg_gen_ext32s_i64(tmp3, tmp3);
        tcg_gen_mul_i64(tmp2, tmp2, tmp3);
        store_reg32_i64((r1 + 1) & 15, tmp2);
        tcg_gen_shri_i64(tmp2, tmp2, 32);
        store_reg32_i64(r1, tmp2);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x1d: /* DR     R1,R2               [RR] */
        insn = ld_code2(s->pc);
        decode_rr(s, insn, &r1, &r2);
        tmp32_1 = load_reg32(r1);
        tmp32_2 = load_reg32(r1 + 1);
        tmp32_3 = load_reg32(r2);

        tmp = tcg_temp_new_i64(); /* dividend */
        tmp2 = tcg_temp_new_i64(); /* divisor */
        tmp3 = tcg_temp_new_i64();

        /* dividend is r(r1 << 32) | r(r1 + 1) */
        tcg_gen_extu_i32_i64(tmp, tmp32_1);
        tcg_gen_extu_i32_i64(tmp2, tmp32_2);
        tcg_gen_shli_i64(tmp, tmp, 32);
        tcg_gen_or_i64(tmp, tmp, tmp2);

        /* divisor is r(r2) */
        tcg_gen_ext_i32_i64(tmp2, tmp32_3);

        tcg_gen_div_i64(tmp3, tmp, tmp2);
        tcg_gen_rem_i64(tmp, tmp, tmp2);

        tcg_gen_trunc_i64_i32(tmp32_1, tmp);
        tcg_gen_trunc_i64_i32(tmp32_2, tmp3);

        store_reg32(r1, tmp32_1); /* remainder */
        store_reg32(r1 + 1, tmp32_2); /* quotient */
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x28: /* LDR    R1,R2               [RR] */
        insn = ld_code2(s->pc);
        decode_rr(s, insn, &r1, &r2);
        tmp = load_freg(r2);
        store_freg(r1, tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x38: /* LER    R1,R2               [RR] */
        insn = ld_code2(s->pc);
        decode_rr(s, insn, &r1, &r2);
        tmp32_1 = load_freg32(r2);
        store_freg32(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x40: /* STH    R1,D2(X2,B2)     [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = load_reg(r1);
        tcg_gen_qemu_st16(tmp2, tmp, get_mem_index(s));
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x41:        /* la */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        store_reg(r1, tmp); /* FIXME: 31/24-bit addressing */
        tcg_temp_free_i64(tmp);
        break;
    case 0x42: /* STC    R1,D2(X2,B2)     [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = load_reg(r1);
        tcg_gen_qemu_st8(tmp2, tmp, get_mem_index(s));
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x43: /* IC     R1,D2(X2,B2)     [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld8u(tmp2, tmp, get_mem_index(s));
        store_reg8(r1, tmp2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x44: /* EX     R1,D2(X2,B2)     [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = load_reg(r1);
        tmp3 = tcg_const_i64(s->pc + 4);
        update_psw_addr(s);
        gen_op_calc_cc(s);
        gen_helper_ex(cc_op, cc_op, tmp2, tmp, tmp3);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x46: /* BCT    R1,D2(X2,B2)     [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tcg_temp_free_i64(tmp);

        tmp32_1 = load_reg32(r1);
        tcg_gen_subi_i32(tmp32_1, tmp32_1, 1);
        store_reg32(r1, tmp32_1);

        gen_update_cc_op(s);
        l1 = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_NE, tmp32_1, 0, l1);

        /* not taking the branch, jump to after the instruction */
        gen_goto_tb(s, 0, s->pc + 4);
        gen_set_label(l1);

        /* take the branch, move R2 into psw.addr */
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tcg_gen_mov_i64(psw_addr, tmp);
        s->is_jmp = DISAS_JUMP;
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp);
        break;
    case 0x47: /* BC     M1,D2(X2,B2)     [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        gen_bcr(s, r1, tmp, s->pc + 4);
        tcg_temp_free_i64(tmp);
        s->is_jmp = DISAS_TB_JUMP;
        break;
    case 0x48: /* LH     R1,D2(X2,B2)     [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld16s(tmp2, tmp, get_mem_index(s));
        store_reg32_i64(r1, tmp2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x49: /* CH     R1,D2(X2,B2)     [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp32_1 = load_reg32(r1);
        tmp32_2 = tcg_temp_new_i32();
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld16s(tmp2, tmp, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_2, tmp2);
        cmp_s32(s, tmp32_1, tmp32_2);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x4a: /* AH     R1,D2(X2,B2)     [RX] */
    case 0x4b: /* SH     R1,D2(X2,B2)     [RX] */
    case 0x4c: /* MH     R1,D2(X2,B2)     [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = load_reg32(r1);
        tmp32_2 = tcg_temp_new_i32();
        tmp32_3 = tcg_temp_new_i32();

        tcg_gen_qemu_ld16s(tmp2, tmp, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_2, tmp2);
        switch (opc) {
        case 0x4a:
            tcg_gen_add_i32(tmp32_3, tmp32_1, tmp32_2);
            set_cc_add32(s, tmp32_1, tmp32_2, tmp32_3);
            break;
        case 0x4b:
            tcg_gen_sub_i32(tmp32_3, tmp32_1, tmp32_2);
            set_cc_sub32(s, tmp32_1, tmp32_2, tmp32_3);
            break;
        case 0x4c:
            tcg_gen_mul_i32(tmp32_3, tmp32_1, tmp32_2);
            break;
        default:
            tcg_abort();
        }
        store_reg32(r1, tmp32_3);

        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x4d: /* BAS    R1,D2(X2,B2)     [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = tcg_const_i64(pc_to_link_info(s, s->pc + 4));
        store_reg(r1, tmp2);
        tcg_gen_mov_i64(psw_addr, tmp);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        s->is_jmp = DISAS_JUMP;
        break;
    case 0x4e: /* CVD    R1,D2(X2,B2)     [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = tcg_temp_new_i32();
        tcg_gen_trunc_i64_i32(tmp32_1, regs[r1]);
        gen_helper_cvd(tmp2, tmp32_1);
        tcg_gen_qemu_st64(tmp2, tmp, get_mem_index(s));
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x50: /* st r1, d2(x2, b2) */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = load_reg(r1);
        tcg_gen_qemu_st32(tmp2, tmp, get_mem_index(s));
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x55: /* CL     R1,D2(X2,B2)     [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = tcg_temp_new_i32();
        tmp32_2 = load_reg32(r1);
        tcg_gen_qemu_ld32u(tmp2, tmp, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_1, tmp2);
        cmp_u32(s, tmp32_2, tmp32_1);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x54: /* N      R1,D2(X2,B2)     [RX] */
    case 0x56: /* O      R1,D2(X2,B2)     [RX] */
    case 0x57: /* X      R1,D2(X2,B2)     [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = load_reg32(r1);
        tmp32_2 = tcg_temp_new_i32();
        tcg_gen_qemu_ld32u(tmp2, tmp, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_2, tmp2);
        gen_and_or_xor_i32(opc, tmp32_1, tmp32_2);
        store_reg32(r1, tmp32_1);
        set_cc_nz_u32(s, tmp32_1);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x58: /* l r1, d2(x2, b2) */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = tcg_temp_new_i32();
        tcg_gen_qemu_ld32u(tmp2, tmp, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_1, tmp2);
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x59: /* C      R1,D2(X2,B2)     [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = tcg_temp_new_i32();
        tmp32_2 = load_reg32(r1);
        tcg_gen_qemu_ld32s(tmp2, tmp, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_1, tmp2);
        cmp_s32(s, tmp32_2, tmp32_1);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x5a: /* A      R1,D2(X2,B2)     [RX] */
    case 0x5b: /* S      R1,D2(X2,B2)     [RX] */
    case 0x5e: /* AL     R1,D2(X2,B2)     [RX] */
    case 0x5f: /* SL     R1,D2(X2,B2)     [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp32_1 = load_reg32(r1);
        tmp32_2 = tcg_temp_new_i32();
        tmp32_3 = tcg_temp_new_i32();
        tcg_gen_qemu_ld32s(tmp, tmp, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_2, tmp);
        switch (opc) {
        case 0x5a:
        case 0x5e:
            tcg_gen_add_i32(tmp32_3, tmp32_1, tmp32_2);
            break;
        case 0x5b:
        case 0x5f:
            tcg_gen_sub_i32(tmp32_3, tmp32_1, tmp32_2);
            break;
        default:
            tcg_abort();
        }
        store_reg32(r1, tmp32_3);
        switch (opc) {
        case 0x5a:
            set_cc_add32(s, tmp32_1, tmp32_2, tmp32_3);
            break;
        case 0x5e:
            set_cc_addu32(s, tmp32_1, tmp32_2, tmp32_3);
            break;
        case 0x5b:
            set_cc_sub32(s, tmp32_1, tmp32_2, tmp32_3);
            break;
        case 0x5f:
            set_cc_subu32(s, tmp32_1, tmp32_2, tmp32_3);
            break;
        default:
            tcg_abort();
        }
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    case 0x5c: /* M      R1,D2(X2,B2)        [RX] */
        /* reg(r1, r1+1) = reg(r1+1) * *(s32*)addr */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld32s(tmp2, tmp, get_mem_index(s));
        tmp3 = load_reg((r1 + 1) & 15);
        tcg_gen_ext32s_i64(tmp2, tmp2);
        tcg_gen_ext32s_i64(tmp3, tmp3);
        tcg_gen_mul_i64(tmp2, tmp2, tmp3);
        store_reg32_i64((r1 + 1) & 15, tmp2);
        tcg_gen_shri_i64(tmp2, tmp2, 32);
        store_reg32_i64(r1, tmp2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x5d: /* D      R1,D2(X2,B2)        [RX] */
        insn = ld_code4(s->pc);
        tmp3 = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp32_1 = load_reg32(r1);
        tmp32_2 = load_reg32(r1 + 1);

        tmp = tcg_temp_new_i64();
        tmp2 = tcg_temp_new_i64();

        /* dividend is r(r1 << 32) | r(r1 + 1) */
        tcg_gen_extu_i32_i64(tmp, tmp32_1);
        tcg_gen_extu_i32_i64(tmp2, tmp32_2);
        tcg_gen_shli_i64(tmp, tmp, 32);
        tcg_gen_or_i64(tmp, tmp, tmp2);

        /* divisor is in memory */
        tcg_gen_qemu_ld32s(tmp2, tmp3, get_mem_index(s));

        /* XXX divisor == 0 -> FixP divide exception */

        tcg_gen_div_i64(tmp3, tmp, tmp2);
        tcg_gen_rem_i64(tmp, tmp, tmp2);

        tcg_gen_trunc_i64_i32(tmp32_1, tmp);
        tcg_gen_trunc_i64_i32(tmp32_2, tmp3);

        store_reg32(r1, tmp32_1); /* remainder */
        store_reg32(r1 + 1, tmp32_2); /* quotient */
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x60: /* STD    R1,D2(X2,B2)        [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = load_freg(r1);
        tcg_gen_qemu_st64(tmp2, tmp, get_mem_index(s));
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x68: /* LD    R1,D2(X2,B2)        [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld64(tmp2, tmp, get_mem_index(s));
        store_freg(r1, tmp2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x70: /* STE R1,D2(X2,B2) [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = load_freg32(r1);
        tcg_gen_extu_i32_i64(tmp2, tmp32_1);
        tcg_gen_qemu_st32(tmp2, tmp, get_mem_index(s));
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x71: /* MS      R1,D2(X2,B2)     [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = load_reg32(r1);
        tmp32_2 = tcg_temp_new_i32();
        tcg_gen_qemu_ld32s(tmp2, tmp, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_2, tmp2);
        tcg_gen_mul_i32(tmp32_1, tmp32_1, tmp32_2);
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x78: /* LE     R1,D2(X2,B2)        [RX] */
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = tcg_temp_new_i32();
        tcg_gen_qemu_ld32u(tmp2, tmp, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32_1, tmp2);
        store_freg32(r1, tmp32_1);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        break;
#ifndef CONFIG_USER_ONLY
    case 0x80: /* SSM      D2(B2)       [S] */
        /* Set System Mask */
        check_privileged(s, ilc);
        insn = ld_code4(s->pc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp2 = tcg_temp_new_i64();
        tmp3 = tcg_temp_new_i64();
        tcg_gen_andi_i64(tmp3, psw_mask, ~0xff00000000000000ULL);
        tcg_gen_qemu_ld8u(tmp2, tmp, get_mem_index(s));
        tcg_gen_shli_i64(tmp2, tmp2, 56);
        tcg_gen_or_i64(psw_mask, tmp3, tmp2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x82: /* LPSW     D2(B2)       [S] */
        /* Load PSW */
        check_privileged(s, ilc);
        insn = ld_code4(s->pc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp2 = tcg_temp_new_i64();
        tmp3 = tcg_temp_new_i64();
        tcg_gen_qemu_ld32u(tmp2, tmp, get_mem_index(s));
        tcg_gen_addi_i64(tmp, tmp, 4);
        tcg_gen_qemu_ld32u(tmp3, tmp, get_mem_index(s));
        gen_helper_load_psw(tmp2, tmp3);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        /* we need to keep cc_op intact */
        s->is_jmp = DISAS_JUMP;
        break;
    case 0x83: /* DIAG     R1,R3,D2     [RS] */
        /* Diagnose call (KVM hypercall) */
        check_privileged(s, ilc);
        potential_page_fault(s);
        insn = ld_code4(s->pc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp32_1 = tcg_const_i32(insn & 0xfff);
        tmp2 = load_reg(2);
        tmp3 = load_reg(1);
        gen_helper_diag(tmp2, tmp32_1, tmp2, tmp3);
        store_reg(2, tmp2);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
#endif
    case 0x88: /* SRL    R1,D2(B2)        [RS] */
    case 0x89: /* SLL    R1,D2(B2)        [RS] */
    case 0x8a: /* SRA    R1,D2(B2)        [RS] */
        insn = ld_code4(s->pc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = load_reg32(r1);
        tmp32_2 = tcg_temp_new_i32();
        tcg_gen_trunc_i64_i32(tmp32_2, tmp);
        tcg_gen_andi_i32(tmp32_2, tmp32_2, 0x3f);
        switch (opc) {
        case 0x88:
            tcg_gen_shr_i32(tmp32_1, tmp32_1, tmp32_2);
            break;
        case 0x89:
            tcg_gen_shl_i32(tmp32_1, tmp32_1, tmp32_2);
            break;
        case 0x8a:
            tcg_gen_sar_i32(tmp32_1, tmp32_1, tmp32_2);
            set_cc_s32(s, tmp32_1);
            break;
        default:
            tcg_abort();
        }
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x8c: /* SRDL   R1,D2(B2)        [RS] */
    case 0x8d: /* SLDL   R1,D2(B2)        [RS] */
    case 0x8e: /* SRDA   R1,D2(B2)        [RS] */
        insn = ld_code4(s->pc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2); /* shift */
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = load_reg32(r1);
        tmp32_2 = load_reg32(r1 + 1);
        tcg_gen_concat_i32_i64(tmp2, tmp32_2, tmp32_1); /* operand */
        switch (opc) {
        case 0x8c:
            tcg_gen_shr_i64(tmp2, tmp2, tmp);
            break;
        case 0x8d:
            tcg_gen_shl_i64(tmp2, tmp2, tmp);
            break;
        case 0x8e:
            tcg_gen_sar_i64(tmp2, tmp2, tmp);
            set_cc_s64(s, tmp2);
            break;
        }
        tcg_gen_shri_i64(tmp, tmp2, 32);
        tcg_gen_trunc_i64_i32(tmp32_1, tmp);
        store_reg32(r1, tmp32_1);
        tcg_gen_trunc_i64_i32(tmp32_2, tmp2);
        store_reg32(r1 + 1, tmp32_2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x98: /* LM     R1,R3,D2(B2)     [RS] */
    case 0x90: /* STM    R1,R3,D2(B2)     [RS] */
        insn = ld_code4(s->pc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);

        tmp = get_address(s, 0, b2, d2);
        tmp2 = tcg_temp_new_i64();
        tmp3 = tcg_const_i64(4);
        tmp4 = tcg_const_i64(0xffffffff00000000ULL);
        for (i = r1;; i = (i + 1) % 16) {
            if (opc == 0x98) {
                tcg_gen_qemu_ld32u(tmp2, tmp, get_mem_index(s));
                tcg_gen_and_i64(regs[i], regs[i], tmp4);
                tcg_gen_or_i64(regs[i], regs[i], tmp2);
            } else {
                tcg_gen_qemu_st32(regs[i], tmp, get_mem_index(s));
            }
            if (i == r3) {
                break;
            }
            tcg_gen_add_i64(tmp, tmp, tmp3);
        }
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        tcg_temp_free_i64(tmp4);
        break;
    case 0x91: /* TM     D1(B1),I2        [SI] */
        insn = ld_code4(s->pc);
        tmp = decode_si(s, insn, &i2, &b1, &d1);
        tmp2 = tcg_const_i64(i2);
        tcg_gen_qemu_ld8u(tmp, tmp, get_mem_index(s));
        cmp_64(s, tmp, tmp2, CC_OP_TM_32);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x92: /* MVI    D1(B1),I2        [SI] */
        insn = ld_code4(s->pc);
        tmp = decode_si(s, insn, &i2, &b1, &d1);
        tmp2 = tcg_const_i64(i2);
        tcg_gen_qemu_st8(tmp2, tmp, get_mem_index(s));
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x94: /* NI     D1(B1),I2        [SI] */
    case 0x96: /* OI     D1(B1),I2        [SI] */
    case 0x97: /* XI     D1(B1),I2        [SI] */
        insn = ld_code4(s->pc);
        tmp = decode_si(s, insn, &i2, &b1, &d1);
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld8u(tmp2, tmp, get_mem_index(s));
        switch (opc) {
        case 0x94:
            tcg_gen_andi_i64(tmp2, tmp2, i2);
            break;
        case 0x96:
            tcg_gen_ori_i64(tmp2, tmp2, i2);
            break;
        case 0x97:
            tcg_gen_xori_i64(tmp2, tmp2, i2);
            break;
        default:
            tcg_abort();
        }
        tcg_gen_qemu_st8(tmp2, tmp, get_mem_index(s));
        set_cc_nz_u64(s, tmp2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x95: /* CLI    D1(B1),I2        [SI] */
        insn = ld_code4(s->pc);
        tmp = decode_si(s, insn, &i2, &b1, &d1);
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld8u(tmp2, tmp, get_mem_index(s));
        cmp_u64c(s, tmp2, i2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x9a: /* LAM      R1,R3,D2(B2)     [RS] */
        insn = ld_code4(s->pc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r3);
        potential_page_fault(s);
        gen_helper_lam(tmp32_1, tmp, tmp32_2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x9b: /* STAM     R1,R3,D2(B2)     [RS] */
        insn = ld_code4(s->pc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r3);
        potential_page_fault(s);
        gen_helper_stam(tmp32_1, tmp, tmp32_2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0xa5:
        insn = ld_code4(s->pc);
        r1 = (insn >> 20) & 0xf;
        op = (insn >> 16) & 0xf;
        i2 = insn & 0xffff;
        disas_a5(s, op, r1, i2);
        break;
    case 0xa7:
        insn = ld_code4(s->pc);
        r1 = (insn >> 20) & 0xf;
        op = (insn >> 16) & 0xf;
        i2 = (short)insn;
        disas_a7(s, op, r1, i2);
        break;
    case 0xa8: /* MVCLE   R1,R3,D2(B2)     [RS] */
        insn = ld_code4(s->pc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r3);
        potential_page_fault(s);
        gen_helper_mvcle(cc_op, tmp32_1, tmp, tmp32_2);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0xa9: /* CLCLE   R1,R3,D2(B2)     [RS] */
        insn = ld_code4(s->pc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r3);
        potential_page_fault(s);
        gen_helper_clcle(cc_op, tmp32_1, tmp, tmp32_2);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
#ifndef CONFIG_USER_ONLY
    case 0xac: /* STNSM   D1(B1),I2     [SI] */
    case 0xad: /* STOSM   D1(B1),I2     [SI] */
        check_privileged(s, ilc);
        insn = ld_code4(s->pc);
        tmp = decode_si(s, insn, &i2, &b1, &d1);
        tmp2 = tcg_temp_new_i64();
        tcg_gen_shri_i64(tmp2, psw_mask, 56);
        tcg_gen_qemu_st8(tmp2, tmp, get_mem_index(s));
        if (opc == 0xac) {
            tcg_gen_andi_i64(psw_mask, psw_mask,
                    ((uint64_t)i2 << 56) | 0x00ffffffffffffffULL);
        } else {
            tcg_gen_ori_i64(psw_mask, psw_mask, (uint64_t)i2 << 56);
        }
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0xae: /* SIGP   R1,R3,D2(B2)     [RS] */
        check_privileged(s, ilc);
        insn = ld_code4(s->pc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp2 = load_reg(r3);
        tmp32_1 = tcg_const_i32(r1);
        potential_page_fault(s);
        gen_helper_sigp(cc_op, tmp, tmp32_1, tmp2);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0xb1: /* LRA    R1,D2(X2, B2)     [RX] */
        check_privileged(s, ilc);
        insn = ld_code4(s->pc);
        tmp = decode_rx(s, insn, &r1, &x2, &b2, &d2);
        tmp32_1 = tcg_const_i32(r1);
        potential_page_fault(s);
        gen_helper_lra(cc_op, tmp, tmp32_1);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        break;
#endif
    case 0xb2:
        insn = ld_code4(s->pc);
        op = (insn >> 16) & 0xff;
        switch (op) {
        case 0x9c: /* STFPC    D2(B2) [S] */
            d2 = insn & 0xfff;
            b2 = (insn >> 12) & 0xf;
            tmp32_1 = tcg_temp_new_i32();
            tmp = tcg_temp_new_i64();
            tmp2 = get_address(s, 0, b2, d2);
            tcg_gen_ld_i32(tmp32_1, cpu_env, offsetof(CPUState, fpc));
            tcg_gen_extu_i32_i64(tmp, tmp32_1);
            tcg_gen_qemu_st32(tmp, tmp2, get_mem_index(s));
            tcg_temp_free_i32(tmp32_1);
            tcg_temp_free_i64(tmp);
            tcg_temp_free_i64(tmp2);
            break;
        default:
            disas_b2(s, op, insn);
            break;
        }
        break;
    case 0xb3:
        insn = ld_code4(s->pc);
        op = (insn >> 16) & 0xff;
        r3 = (insn >> 12) & 0xf; /* aka m3 */
        r1 = (insn >> 4) & 0xf;
        r2 = insn & 0xf;
        disas_b3(s, op, r3, r1, r2);
        break;
#ifndef CONFIG_USER_ONLY
    case 0xb6: /* STCTL     R1,R3,D2(B2)     [RS] */
        /* Store Control */
        check_privileged(s, ilc);
        insn = ld_code4(s->pc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r3);
        potential_page_fault(s);
        gen_helper_stctl(tmp32_1, tmp, tmp32_2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0xb7: /* LCTL      R1,R3,D2(B2)     [RS] */
        /* Load Control */
        check_privileged(s, ilc);
        insn = ld_code4(s->pc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r3);
        potential_page_fault(s);
        gen_helper_lctl(tmp32_1, tmp, tmp32_2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
#endif
    case 0xb9:
        insn = ld_code4(s->pc);
        r1 = (insn >> 4) & 0xf;
        r2 = insn & 0xf;
        op = (insn >> 16) & 0xff;
        disas_b9(s, op, r1, r2);
        break;
    case 0xba: /* CS     R1,R3,D2(B2)     [RS] */
        insn = ld_code4(s->pc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r3);
        potential_page_fault(s);
        gen_helper_cs(cc_op, tmp32_1, tmp, tmp32_2);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0xbd: /* CLM    R1,M3,D2(B2)     [RS] */
        insn = ld_code4(s->pc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = load_reg32(r1);
        tmp32_2 = tcg_const_i32(r3);
        potential_page_fault(s);
        gen_helper_clm(cc_op, tmp32_1, tmp32_2, tmp);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0xbe: /* STCM R1,M3,D2(B2) [RS] */
        insn = ld_code4(s->pc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = load_reg32(r1);
        tmp32_2 = tcg_const_i32(r3);
        potential_page_fault(s);
        gen_helper_stcm(tmp32_1, tmp32_2, tmp);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0xbf: /* ICM    R1,M3,D2(B2)     [RS] */
        insn = ld_code4(s->pc);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        if (r3 == 15) {
            /* effectively a 32-bit load */
            tmp = get_address(s, 0, b2, d2);
            tmp32_1 = tcg_temp_new_i32();
            tmp32_2 = tcg_const_i32(r3);
            tcg_gen_qemu_ld32u(tmp, tmp, get_mem_index(s));
            store_reg32_i64(r1, tmp);
            tcg_gen_trunc_i64_i32(tmp32_1, tmp);
            set_cc_icm(s, tmp32_2, tmp32_1);
            tcg_temp_free_i64(tmp);
            tcg_temp_free_i32(tmp32_1);
            tcg_temp_free_i32(tmp32_2);
        } else if (r3) {
            uint32_t mask = 0x00ffffffUL;
            uint32_t shift = 24;
            int m3 = r3;
            tmp = get_address(s, 0, b2, d2);
            tmp2 = tcg_temp_new_i64();
            tmp32_1 = load_reg32(r1);
            tmp32_2 = tcg_temp_new_i32();
            tmp32_3 = tcg_const_i32(r3);
            tmp32_4 = tcg_const_i32(0);
            while (m3) {
                if (m3 & 8) {
                    tcg_gen_qemu_ld8u(tmp2, tmp, get_mem_index(s));
                    tcg_gen_trunc_i64_i32(tmp32_2, tmp2);
                    if (shift) {
                        tcg_gen_shli_i32(tmp32_2, tmp32_2, shift);
                    }
                    tcg_gen_andi_i32(tmp32_1, tmp32_1, mask);
                    tcg_gen_or_i32(tmp32_1, tmp32_1, tmp32_2);
                    tcg_gen_or_i32(tmp32_4, tmp32_4, tmp32_2);
                    tcg_gen_addi_i64(tmp, tmp, 1);
                }
                m3 = (m3 << 1) & 0xf;
                mask = (mask >> 8) | 0xff000000UL;
                shift -= 8;
            }
            store_reg32(r1, tmp32_1);
            set_cc_icm(s, tmp32_3, tmp32_4);
            tcg_temp_free_i64(tmp);
            tcg_temp_free_i64(tmp2);
            tcg_temp_free_i32(tmp32_1);
            tcg_temp_free_i32(tmp32_2);
            tcg_temp_free_i32(tmp32_3);
            tcg_temp_free_i32(tmp32_4);
        } else {
            /* i.e. env->cc = 0 */
            gen_op_movi_cc(s, 0);
        }
        break;
    case 0xc0:
    case 0xc2:
        insn = ld_code6(s->pc);
        r1 = (insn >> 36) & 0xf;
        op = (insn >> 32) & 0xf;
        i2 = (int)insn;
        switch (opc) {
        case 0xc0:
            disas_c0(s, op, r1, i2);
            break;
        case 0xc2:
            disas_c2(s, op, r1, i2);
            break;
        default:
            tcg_abort();
        }
        break;
    case 0xd2: /* MVC    D1(L,B1),D2(B2)         [SS] */
    case 0xd4: /* NC     D1(L,B1),D2(B2)         [SS] */
    case 0xd5: /* CLC    D1(L,B1),D2(B2)         [SS] */
    case 0xd6: /* OC     D1(L,B1),D2(B2)         [SS] */
    case 0xd7: /* XC     D1(L,B1),D2(B2)         [SS] */
    case 0xdc: /* TR     D1(L,B1),D2(B2)         [SS] */
    case 0xf3: /* UNPK   D1(L1,B1),D2(L2,B2)     [SS] */
        insn = ld_code6(s->pc);
        vl = tcg_const_i32((insn >> 32) & 0xff);
        b1 = (insn >> 28) & 0xf;
        b2 = (insn >> 12) & 0xf;
        d1 = (insn >> 16) & 0xfff;
        d2 = insn & 0xfff;
        tmp = get_address(s, 0, b1, d1);
        tmp2 = get_address(s, 0, b2, d2);
        switch (opc) {
        case 0xd2:
            gen_op_mvc(s, (insn >> 32) & 0xff, tmp, tmp2);
            break;
        case 0xd4:
            potential_page_fault(s);
            gen_helper_nc(cc_op, vl, tmp, tmp2);
            set_cc_static(s);
            break;
        case 0xd5:
            gen_op_clc(s, (insn >> 32) & 0xff, tmp, tmp2);
            break;
        case 0xd6:
            potential_page_fault(s);
            gen_helper_oc(cc_op, vl, tmp, tmp2);
            set_cc_static(s);
            break;
        case 0xd7:
            potential_page_fault(s);
            gen_helper_xc(cc_op, vl, tmp, tmp2);
            set_cc_static(s);
            break;
        case 0xdc:
            potential_page_fault(s);
            gen_helper_tr(vl, tmp, tmp2);
            set_cc_static(s);
            break;
        case 0xf3:
            potential_page_fault(s);
            gen_helper_unpk(vl, tmp, tmp2);
            break;
        default:
            tcg_abort();
        }
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
#ifndef CONFIG_USER_ONLY
    case 0xda: /* MVCP     D1(R1,B1),D2(B2),R3   [SS] */
    case 0xdb: /* MVCS     D1(R1,B1),D2(B2),R3   [SS] */
        check_privileged(s, ilc);
        potential_page_fault(s);
        insn = ld_code6(s->pc);
        r1 = (insn >> 36) & 0xf;
        r3 = (insn >> 32) & 0xf;
        b1 = (insn >> 28) & 0xf;
        d1 = (insn >> 16) & 0xfff;
        b2 = (insn >> 12) & 0xf;
        d2 = insn & 0xfff;
        tmp = load_reg(r1);
        /* XXX key in r3 */
        tmp2 = get_address(s, 0, b1, d1);
        tmp3 = get_address(s, 0, b2, d2);
        if (opc == 0xda) {
            gen_helper_mvcp(cc_op, tmp, tmp2, tmp3);
        } else {
            gen_helper_mvcs(cc_op, tmp, tmp2, tmp3);
        }
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
#endif
    case 0xe3:
        insn = ld_code6(s->pc);
        debug_insn(insn);
        op = insn & 0xff;
        r1 = (insn >> 36) & 0xf;
        x2 = (insn >> 32) & 0xf;
        b2 = (insn >> 28) & 0xf;
        d2 = ((int)((((insn >> 16) & 0xfff)
           | ((insn << 4) & 0xff000)) << 12)) >> 12;
        disas_e3(s, op,  r1, x2, b2, d2 );
        break;
#ifndef CONFIG_USER_ONLY
    case 0xe5:
        /* Test Protection */
        check_privileged(s, ilc);
        insn = ld_code6(s->pc);
        debug_insn(insn);
        disas_e5(s, insn);
        break;
#endif
    case 0xeb:
        insn = ld_code6(s->pc);
        debug_insn(insn);
        op = insn & 0xff;
        r1 = (insn >> 36) & 0xf;
        r3 = (insn >> 32) & 0xf;
        b2 = (insn >> 28) & 0xf;
        d2 = ((int)((((insn >> 16) & 0xfff)
           | ((insn << 4) & 0xff000)) << 12)) >> 12;
        disas_eb(s, op, r1, r3, b2, d2);
        break;
    case 0xed:
        insn = ld_code6(s->pc);
        debug_insn(insn);
        op = insn & 0xff;
        r1 = (insn >> 36) & 0xf;
        x2 = (insn >> 32) & 0xf;
        b2 = (insn >> 28) & 0xf;
        d2 = (short)((insn >> 16) & 0xfff);
        r1b = (insn >> 12) & 0xf;
        disas_ed(s, op, r1, x2, b2, d2, r1b);
        break;
    default:
        LOG_DISAS("unimplemented opcode 0x%x\n", opc);
        gen_illegal_opcode(s, ilc);
        break;
    }

    /* Instruction length is encoded in the opcode */
    s->pc += (ilc * 2);
}

static inline void gen_intermediate_code_internal(CPUState *env,
                                                  TranslationBlock *tb,
                                                  int search_pc)
{
    DisasContext dc;
    target_ulong pc_start;
    uint64_t next_page_start;
    uint16_t *gen_opc_end;
    int j, lj = -1;
    int num_insns, max_insns;
    CPUBreakpoint *bp;

    pc_start = tb->pc;

    /* 31-bit mode */
    if (!(tb->flags & FLAG_MASK_64)) {
        pc_start &= 0x7fffffff;
    }

    dc.pc = pc_start;
    dc.is_jmp = DISAS_NEXT;
    dc.tb = tb;
    dc.cc_op = CC_OP_DYNAMIC;

    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;

    next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;

    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }

    gen_icount_start();

    do {
        if (unlikely(!QTAILQ_EMPTY(&env->breakpoints))) {
            QTAILQ_FOREACH(bp, &env->breakpoints, entry) {
                if (bp->pc == dc.pc) {
                    gen_debug(&dc);
                    break;
                }
            }
        }
        if (search_pc) {
            j = gen_opc_ptr - gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j) {
                    gen_opc_instr_start[lj++] = 0;
                }
            }
            gen_opc_pc[lj] = dc.pc;
            gen_opc_cc_op[lj] = dc.cc_op;
            gen_opc_instr_start[lj] = 1;
            gen_opc_icount[lj] = num_insns;
        }
        if (num_insns + 1 == max_insns && (tb->cflags & CF_LAST_IO)) {
            gen_io_start();
        }
#if defined(S390X_DEBUG_DISAS_VERBOSE)
        LOG_DISAS("pc " TARGET_FMT_lx "\n",
                  dc.pc);
#endif
        disas_s390_insn(&dc);

        num_insns++;
        if (env->singlestep_enabled) {
            gen_debug(&dc);
        }
    } while (!dc.is_jmp && gen_opc_ptr < gen_opc_end && dc.pc < next_page_start
             && num_insns < max_insns && !env->singlestep_enabled
             && !singlestep);

    if (!dc.is_jmp) {
        update_psw_addr(&dc);
    }

    if (singlestep && dc.cc_op != CC_OP_DYNAMIC) {
        gen_op_calc_cc(&dc);
    } else {
        /* next TB starts off with CC_OP_DYNAMIC, so make sure the cc op type
           is in env */
        gen_op_set_cc_op(&dc);
    }

    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }
    /* Generate the return instruction */
    if (dc.is_jmp != DISAS_TB_JUMP) {
        tcg_gen_exit_tb(0);
    }
    gen_icount_end(tb, num_insns);
    *gen_opc_ptr = INDEX_op_end;
    if (search_pc) {
        j = gen_opc_ptr - gen_opc_buf;
        lj++;
        while (lj <= j) {
            gen_opc_instr_start[lj++] = 0;
        }
    } else {
        tb->size = dc.pc - pc_start;
        tb->icount = num_insns;
    }
#if defined(S390X_DEBUG_DISAS)
    log_cpu_state_mask(CPU_LOG_TB_CPU, env, 0);
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("IN: %s\n", lookup_symbol(pc_start));
        log_target_disas(pc_start, dc.pc - pc_start, 1);
        qemu_log("\n");
    }
#endif
}

void gen_intermediate_code (CPUState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 0);
}

void gen_intermediate_code_pc (CPUState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 1);
}

void restore_state_to_opc(CPUState *env, TranslationBlock *tb, int pc_pos)
{
    int cc_op;
    env->psw.addr = gen_opc_pc[pc_pos];
    cc_op = gen_opc_cc_op[pc_pos];
    if ((cc_op != CC_OP_DYNAMIC) && (cc_op != CC_OP_STATIC)) {
        env->cc_op = cc_op;
    }
}
