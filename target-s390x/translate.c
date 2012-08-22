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

/* #define DEBUG_INLINE_BRANCHES */
#define S390X_DEBUG_DISAS
/* #define S390X_DEBUG_DISAS_VERBOSE */

#ifdef S390X_DEBUG_DISAS_VERBOSE
#  define LOG_DISAS(...) qemu_log(__VA_ARGS__)
#else
#  define LOG_DISAS(...) do { } while (0)
#endif

#include "cpu.h"
#include "disas/disas.h"
#include "tcg-op.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"

/* global register indexes */
static TCGv_ptr cpu_env;

#include "exec/gen-icount.h"
#include "helper.h"
#define GEN_HELPER 1
#include "helper.h"


/* Information that (most) every instruction needs to manipulate.  */
typedef struct DisasContext DisasContext;
typedef struct DisasInsn DisasInsn;
typedef struct DisasFields DisasFields;

struct DisasContext {
    struct TranslationBlock *tb;
    const DisasInsn *insn;
    DisasFields *fields;
    uint64_t pc, next_pc;
    enum cc_op cc_op;
    bool singlestep_enabled;
    int is_jmp;
};

/* Information carried about a condition to be evaluated.  */
typedef struct {
    TCGCond cond:8;
    bool is_64;
    bool g1;
    bool g2;
    union {
        struct { TCGv_i64 a, b; } s64;
        struct { TCGv_i32 a, b; } s32;
    } u;
} DisasCompare;

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

void cpu_dump_state(CPUS390XState *env, FILE *f, fprintf_function cpu_fprintf,
                    int flags)
{
    int i;

    if (env->cc_op > 3) {
        cpu_fprintf(f, "PSW=mask %016" PRIx64 " addr %016" PRIx64 " cc %15s\n",
                    env->psw.mask, env->psw.addr, cc_name(env->cc_op));
    } else {
        cpu_fprintf(f, "PSW=mask %016" PRIx64 " addr %016" PRIx64 " cc %02x\n",
                    env->psw.mask, env->psw.addr, env->cc_op);
    }

    for (i = 0; i < 16; i++) {
        cpu_fprintf(f, "R%02d=%016" PRIx64, i, env->regs[i]);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }

    for (i = 0; i < 16; i++) {
        cpu_fprintf(f, "F%02d=%016" PRIx64, i, env->fregs[i].ll);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }

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

#ifdef DEBUG_INLINE_BRANCHES
    for (i = 0; i < CC_OP_MAX; i++) {
        cpu_fprintf(f, "  %15s = %10ld\t%10ld\n", cc_name(i),
                    inline_branch_miss[i], inline_branch_hit[i]);
    }
#endif

    cpu_fprintf(f, "\n");
}

static TCGv_i64 psw_addr;
static TCGv_i64 psw_mask;

static TCGv_i32 cc_op;
static TCGv_i64 cc_src;
static TCGv_i64 cc_dst;
static TCGv_i64 cc_vr;

static char cpu_reg_names[32][4];
static TCGv_i64 regs[16];
static TCGv_i64 fregs[16];

static uint8_t gen_opc_cc_op[OPC_BUF_SIZE];

void s390x_translate_init(void)
{
    int i;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");
    psw_addr = tcg_global_mem_new_i64(TCG_AREG0,
                                      offsetof(CPUS390XState, psw.addr),
                                      "psw_addr");
    psw_mask = tcg_global_mem_new_i64(TCG_AREG0,
                                      offsetof(CPUS390XState, psw.mask),
                                      "psw_mask");

    cc_op = tcg_global_mem_new_i32(TCG_AREG0, offsetof(CPUS390XState, cc_op),
                                   "cc_op");
    cc_src = tcg_global_mem_new_i64(TCG_AREG0, offsetof(CPUS390XState, cc_src),
                                    "cc_src");
    cc_dst = tcg_global_mem_new_i64(TCG_AREG0, offsetof(CPUS390XState, cc_dst),
                                    "cc_dst");
    cc_vr = tcg_global_mem_new_i64(TCG_AREG0, offsetof(CPUS390XState, cc_vr),
                                   "cc_vr");

    for (i = 0; i < 16; i++) {
        snprintf(cpu_reg_names[i], sizeof(cpu_reg_names[0]), "r%d", i);
        regs[i] = tcg_global_mem_new(TCG_AREG0,
                                     offsetof(CPUS390XState, regs[i]),
                                     cpu_reg_names[i]);
    }

    for (i = 0; i < 16; i++) {
        snprintf(cpu_reg_names[i + 16], sizeof(cpu_reg_names[0]), "f%d", i);
        fregs[i] = tcg_global_mem_new(TCG_AREG0,
                                      offsetof(CPUS390XState, fregs[i].d),
                                      cpu_reg_names[i + 16]);
    }

    /* register helpers */
#define GEN_HELPER 2
#include "helper.h"
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
    tcg_gen_mov_i64(r, fregs[reg]);
    return r;
}

static inline TCGv_i32 load_freg32(int reg)
{
    TCGv_i32 r = tcg_temp_new_i32();
#if HOST_LONG_BITS == 32
    tcg_gen_mov_i32(r, TCGV_HIGH(fregs[reg]));
#else
    tcg_gen_shri_i64(MAKE_TCGV_I64(GET_TCGV_I32(r)), fregs[reg], 32);
#endif
    return r;
}

static inline TCGv_i64 load_freg32_i64(int reg)
{
    TCGv_i64 r = tcg_temp_new_i64();
    tcg_gen_shri_i64(r, fregs[reg], 32);
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
    tcg_gen_mov_i64(fregs[reg], v);
}

static inline void store_reg32(int reg, TCGv_i32 v)
{
    /* 32 bit register writes keep the upper half */
#if HOST_LONG_BITS == 32
    tcg_gen_mov_i32(TCGV_LOW(regs[reg]), v);
#else
    tcg_gen_deposit_i64(regs[reg], regs[reg],
                        MAKE_TCGV_I64(GET_TCGV_I32(v)), 0, 32);
#endif
}

static inline void store_reg32_i64(int reg, TCGv_i64 v)
{
    /* 32 bit register writes keep the upper half */
    tcg_gen_deposit_i64(regs[reg], regs[reg], v, 0, 32);
}

static inline void store_reg32h_i64(int reg, TCGv_i64 v)
{
    tcg_gen_deposit_i64(regs[reg], regs[reg], v, 32, 32);
}

static inline void store_freg32(int reg, TCGv_i32 v)
{
    /* 32 bit register writes keep the lower half */
#if HOST_LONG_BITS == 32
    tcg_gen_mov_i32(TCGV_HIGH(fregs[reg]), v);
#else
    tcg_gen_deposit_i64(fregs[reg], fregs[reg],
                        MAKE_TCGV_I64(GET_TCGV_I32(v)), 32, 32);
#endif
}

static inline void store_freg32_i64(int reg, TCGv_i64 v)
{
    tcg_gen_deposit_i64(fregs[reg], fregs[reg], v, 32, 32);
}

static inline void return_low128(TCGv_i64 dest)
{
    tcg_gen_ld_i64(dest, cpu_env, offsetof(CPUS390XState, retxl));
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

static inline uint64_t ld_code2(CPUS390XState *env, uint64_t pc)
{
    return (uint64_t)cpu_lduw_code(env, pc);
}

static inline uint64_t ld_code4(CPUS390XState *env, uint64_t pc)
{
    return (uint64_t)(uint32_t)cpu_ldl_code(env, pc);
}

static inline uint64_t ld_code6(CPUS390XState *env, uint64_t pc)
{
    return (ld_code2(env, pc) << 32) | ld_code4(env, pc + 2);
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

static void gen_exception(int excp)
{
    TCGv_i32 tmp = tcg_const_i32(excp);
    gen_helper_exception(cpu_env, tmp);
    tcg_temp_free_i32(tmp);
}

static void gen_program_exception(DisasContext *s, int code)
{
    TCGv_i32 tmp;

    /* Remember what pgm exeption this was.  */
    tmp = tcg_const_i32(code);
    tcg_gen_st_i32(tmp, cpu_env, offsetof(CPUS390XState, int_pgm_code));
    tcg_temp_free_i32(tmp);

    tmp = tcg_const_i32(s->next_pc - s->pc);
    tcg_gen_st_i32(tmp, cpu_env, offsetof(CPUS390XState, int_pgm_ilen));
    tcg_temp_free_i32(tmp);

    /* Advance past instruction.  */
    s->pc = s->next_pc;
    update_psw_addr(s);

    /* Save off cc.  */
    gen_op_calc_cc(s);

    /* Trigger exception.  */
    gen_exception(EXCP_PGM);

    /* End TB here.  */
    s->is_jmp = DISAS_EXCP;
}

static inline void gen_illegal_opcode(DisasContext *s)
{
    gen_program_exception(s, PGM_SPECIFICATION);
}

static inline void check_privileged(DisasContext *s)
{
    if (s->tb->flags & (PSW_MASK_PSTATE >> 32)) {
        gen_program_exception(s, PGM_PRIVILEGED);
    }
}

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

static void set_cc_cmp_f32_i64(DisasContext *s, TCGv_i32 v1, TCGv_i64 v2)
{
    tcg_gen_extu_i32_i64(cc_src, v1);
    tcg_gen_mov_i64(cc_dst, v2);
    tcg_gen_discard_i64(cc_vr);
    s->cc_op = CC_OP_LTGT_F32;
}

static void gen_set_cc_nz_f32(DisasContext *s, TCGv_i32 v1)
{
    gen_op_update1_cc_i32(s, CC_OP_NZ_F32, v1);
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
        gen_helper_calc_cc(cc_op, cpu_env, local_cc_op, dummy, cc_dst, dummy);
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
    case CC_OP_SLA_32:
    case CC_OP_SLA_64:
        /* 2 arguments */
        gen_helper_calc_cc(cc_op, cpu_env, local_cc_op, cc_src, cc_dst, dummy);
        break;
    case CC_OP_ADD_64:
    case CC_OP_ADDU_64:
    case CC_OP_ADDC_64:
    case CC_OP_SUB_64:
    case CC_OP_SUBU_64:
    case CC_OP_SUBB_64:
    case CC_OP_ADD_32:
    case CC_OP_ADDU_32:
    case CC_OP_ADDC_32:
    case CC_OP_SUB_32:
    case CC_OP_SUBU_32:
    case CC_OP_SUBB_32:
        /* 3 arguments */
        gen_helper_calc_cc(cc_op, cpu_env, local_cc_op, cc_src, cc_dst, cc_vr);
        break;
    case CC_OP_DYNAMIC:
        /* unknown operation - assume 3 arguments and cc_op in env */
        gen_helper_calc_cc(cc_op, cpu_env, cc_op, cc_src, cc_dst, cc_vr);
        break;
    default:
        tcg_abort();
    }

    tcg_temp_free_i32(local_cc_op);
    tcg_temp_free_i64(dummy);

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

static int use_goto_tb(DisasContext *s, uint64_t dest)
{
    /* NOTE: we handle the case where the TB spans two pages here */
    return (((dest & TARGET_PAGE_MASK) == (s->tb->pc & TARGET_PAGE_MASK)
             || (dest & TARGET_PAGE_MASK) == ((s->pc - 1) & TARGET_PAGE_MASK))
            && !s->singlestep_enabled
            && !(s->tb->cflags & CF_LAST_IO));
}

static inline void gen_goto_tb(DisasContext *s, int tb_num, target_ulong pc)
{
    gen_update_cc_op(s);

    if (use_goto_tb(s, pc)) {
        tcg_gen_goto_tb(tb_num);
        tcg_gen_movi_i64(psw_addr, pc);
        tcg_gen_exit_tb((tcg_target_long)s->tb + tb_num);
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

static inline void account_inline_branch(DisasContext *s, int cc_op)
{
#ifdef DEBUG_INLINE_BRANCHES
    inline_branch_hit[cc_op]++;
#endif
}

/* Table of mask values to comparison codes, given a comparison as input.
   For a true comparison CC=3 will never be set, but we treat this
   conservatively for possible use when CC=3 indicates overflow.  */
static const TCGCond ltgt_cond[16] = {
    TCG_COND_NEVER,  TCG_COND_NEVER,     /*    |    |    | x */
    TCG_COND_GT,     TCG_COND_NEVER,     /*    |    | GT | x */
    TCG_COND_LT,     TCG_COND_NEVER,     /*    | LT |    | x */
    TCG_COND_NE,     TCG_COND_NEVER,     /*    | LT | GT | x */
    TCG_COND_EQ,     TCG_COND_NEVER,     /* EQ |    |    | x */
    TCG_COND_GE,     TCG_COND_NEVER,     /* EQ |    | GT | x */
    TCG_COND_LE,     TCG_COND_NEVER,     /* EQ | LT |    | x */
    TCG_COND_ALWAYS, TCG_COND_ALWAYS,    /* EQ | LT | GT | x */
};

/* Table of mask values to comparison codes, given a logic op as input.
   For such, only CC=0 and CC=1 should be possible.  */
static const TCGCond nz_cond[16] = {
    /*    |    | x | x */
    TCG_COND_NEVER, TCG_COND_NEVER, TCG_COND_NEVER, TCG_COND_NEVER,
    /*    | NE | x | x */
    TCG_COND_NE, TCG_COND_NE, TCG_COND_NE, TCG_COND_NE,
    /* EQ |    | x | x */
    TCG_COND_EQ, TCG_COND_EQ, TCG_COND_EQ, TCG_COND_EQ,
    /* EQ | NE | x | x */
    TCG_COND_ALWAYS, TCG_COND_ALWAYS, TCG_COND_ALWAYS, TCG_COND_ALWAYS,
};

/* Interpret MASK in terms of S->CC_OP, and fill in C with all the
   details required to generate a TCG comparison.  */
static void disas_jcc(DisasContext *s, DisasCompare *c, uint32_t mask)
{
    TCGCond cond;
    enum cc_op old_cc_op = s->cc_op;

    if (mask == 15 || mask == 0) {
        c->cond = (mask ? TCG_COND_ALWAYS : TCG_COND_NEVER);
        c->u.s32.a = cc_op;
        c->u.s32.b = cc_op;
        c->g1 = c->g2 = true;
        c->is_64 = false;
        return;
    }

    /* Find the TCG condition for the mask + cc op.  */
    switch (old_cc_op) {
    case CC_OP_LTGT0_32:
    case CC_OP_LTGT0_64:
    case CC_OP_LTGT_32:
    case CC_OP_LTGT_64:
        cond = ltgt_cond[mask];
        if (cond == TCG_COND_NEVER) {
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    case CC_OP_LTUGTU_32:
    case CC_OP_LTUGTU_64:
        cond = tcg_unsigned_cond(ltgt_cond[mask]);
        if (cond == TCG_COND_NEVER) {
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    case CC_OP_NZ:
        cond = nz_cond[mask];
        if (cond == TCG_COND_NEVER) {
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    case CC_OP_TM_32:
    case CC_OP_TM_64:
        switch (mask) {
        case 8:
            cond = TCG_COND_EQ;
            break;
        case 4 | 2 | 1:
            cond = TCG_COND_NE;
            break;
        default:
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    case CC_OP_ICM:
        switch (mask) {
        case 8:
            cond = TCG_COND_EQ;
            break;
        case 4 | 2 | 1:
        case 4 | 2:
            cond = TCG_COND_NE;
            break;
        default:
            goto do_dynamic;
        }
        account_inline_branch(s, old_cc_op);
        break;

    default:
    do_dynamic:
        /* Calculate cc value.  */
        gen_op_calc_cc(s);
        /* FALLTHRU */

    case CC_OP_STATIC:
        /* Jump based on CC.  We'll load up the real cond below;
           the assignment here merely avoids a compiler warning.  */
        account_noninline_branch(s, old_cc_op);
        old_cc_op = CC_OP_STATIC;
        cond = TCG_COND_NEVER;
        break;
    }

    /* Load up the arguments of the comparison.  */
    c->is_64 = true;
    c->g1 = c->g2 = false;
    switch (old_cc_op) {
    case CC_OP_LTGT0_32:
        c->is_64 = false;
        c->u.s32.a = tcg_temp_new_i32();
        tcg_gen_trunc_i64_i32(c->u.s32.a, cc_dst);
        c->u.s32.b = tcg_const_i32(0);
        break;
    case CC_OP_LTGT_32:
    case CC_OP_LTUGTU_32:
        c->is_64 = false;
        c->u.s32.a = tcg_temp_new_i32();
        tcg_gen_trunc_i64_i32(c->u.s32.a, cc_src);
        c->u.s32.b = tcg_temp_new_i32();
        tcg_gen_trunc_i64_i32(c->u.s32.b, cc_dst);
        break;

    case CC_OP_LTGT0_64:
    case CC_OP_NZ:
        c->u.s64.a = cc_dst;
        c->u.s64.b = tcg_const_i64(0);
        c->g1 = true;
        break;
    case CC_OP_LTGT_64:
    case CC_OP_LTUGTU_64:
        c->u.s64.a = cc_src;
        c->u.s64.b = cc_dst;
        c->g1 = c->g2 = true;
        break;

    case CC_OP_TM_32:
    case CC_OP_TM_64:
    case CC_OP_ICM:
        c->u.s64.a = tcg_temp_new_i64();
        c->u.s64.b = tcg_const_i64(0);
        tcg_gen_and_i64(c->u.s64.a, cc_src, cc_dst);
        break;

    case CC_OP_STATIC:
        c->is_64 = false;
        c->u.s32.a = cc_op;
        c->g1 = true;
        switch (mask) {
        case 0x8 | 0x4 | 0x2: /* cc != 3 */
            cond = TCG_COND_NE;
            c->u.s32.b = tcg_const_i32(3);
            break;
        case 0x8 | 0x4 | 0x1: /* cc != 2 */
            cond = TCG_COND_NE;
            c->u.s32.b = tcg_const_i32(2);
            break;
        case 0x8 | 0x2 | 0x1: /* cc != 1 */
            cond = TCG_COND_NE;
            c->u.s32.b = tcg_const_i32(1);
            break;
        case 0x8 | 0x2: /* cc == 0 || cc == 2 => (cc & 1) == 0 */
            cond = TCG_COND_EQ;
            c->g1 = false;
            c->u.s32.a = tcg_temp_new_i32();
            c->u.s32.b = tcg_const_i32(0);
            tcg_gen_andi_i32(c->u.s32.a, cc_op, 1);
            break;
        case 0x8 | 0x4: /* cc < 2 */
            cond = TCG_COND_LTU;
            c->u.s32.b = tcg_const_i32(2);
            break;
        case 0x8: /* cc == 0 */
            cond = TCG_COND_EQ;
            c->u.s32.b = tcg_const_i32(0);
            break;
        case 0x4 | 0x2 | 0x1: /* cc != 0 */
            cond = TCG_COND_NE;
            c->u.s32.b = tcg_const_i32(0);
            break;
        case 0x4 | 0x1: /* cc == 1 || cc == 3 => (cc & 1) != 0 */
            cond = TCG_COND_NE;
            c->g1 = false;
            c->u.s32.a = tcg_temp_new_i32();
            c->u.s32.b = tcg_const_i32(0);
            tcg_gen_andi_i32(c->u.s32.a, cc_op, 1);
            break;
        case 0x4: /* cc == 1 */
            cond = TCG_COND_EQ;
            c->u.s32.b = tcg_const_i32(1);
            break;
        case 0x2 | 0x1: /* cc > 1 */
            cond = TCG_COND_GTU;
            c->u.s32.b = tcg_const_i32(1);
            break;
        case 0x2: /* cc == 2 */
            cond = TCG_COND_EQ;
            c->u.s32.b = tcg_const_i32(2);
            break;
        case 0x1: /* cc == 3 */
            cond = TCG_COND_EQ;
            c->u.s32.b = tcg_const_i32(3);
            break;
        default:
            /* CC is masked by something else: (8 >> cc) & mask.  */
            cond = TCG_COND_NE;
            c->g1 = false;
            c->u.s32.a = tcg_const_i32(8);
            c->u.s32.b = tcg_const_i32(0);
            tcg_gen_shr_i32(c->u.s32.a, c->u.s32.a, cc_op);
            tcg_gen_andi_i32(c->u.s32.a, c->u.s32.a, mask);
            break;
        }
        break;

    default:
        abort();
    }
    c->cond = cond;
}

static void free_compare(DisasCompare *c)
{
    if (!c->g1) {
        if (c->is_64) {
            tcg_temp_free_i64(c->u.s64.a);
        } else {
            tcg_temp_free_i32(c->u.s32.a);
        }
    }
    if (!c->g2) {
        if (c->is_64) {
            tcg_temp_free_i64(c->u.s64.b);
        } else {
            tcg_temp_free_i32(c->u.s32.b);
        }
    }
}

static void disas_e3(CPUS390XState *env, DisasContext* s, int op, int r1,
                     int x2, int b2, int d2)
{
    TCGv_i64 addr, tmp2;
    TCGv_i32 tmp32_1;

    LOG_DISAS("disas_e3: op 0x%x r1 %d x2 %d b2 %d d2 %d\n",
              op, r1, x2, b2, d2);
    addr = get_address(s, x2, b2, d2);
    switch (op) {
    case 0x17: /* LLGT      R1,D2(X2,B2)     [RXY] */
        tmp2 = tcg_temp_new_i64();
        tcg_gen_qemu_ld32u(tmp2, addr, get_mem_index(s));
        tcg_gen_andi_i64(tmp2, tmp2, 0x7fffffffULL);
        store_reg(r1, tmp2);
        tcg_temp_free_i64(tmp2);
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
    default:
        LOG_DISAS("illegal e3 operation 0x%x\n", op);
        gen_illegal_opcode(s);
        break;
    }
    tcg_temp_free_i64(addr);
}

static void disas_ed(CPUS390XState *env, DisasContext *s, int op, int r1,
                     int x2, int b2, int d2, int r1b)
{
    TCGv_i32 tmp_r1, tmp32;
    TCGv_i64 addr, tmp;
    addr = get_address(s, x2, b2, d2);
    tmp_r1 = tcg_const_i32(r1);
    switch (op) {
    case 0x4: /* LDEB R1,D2(X2,B2) [RXE] */
        potential_page_fault(s);
        gen_helper_ldeb(cpu_env, tmp_r1, addr);
        break;
    case 0x5: /* LXDB R1,D2(X2,B2) [RXE] */
        potential_page_fault(s);
        gen_helper_lxdb(cpu_env, tmp_r1, addr);
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
        gen_helper_aeb(cpu_env, tmp_r1, tmp32);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32);

        tmp32 = load_freg32(r1);
        gen_set_cc_nz_f32(s, tmp32);
        tcg_temp_free_i32(tmp32);
        break;
    case 0xb: /* SEB    R1,D2(X2,B2)       [RXE] */
        tmp = tcg_temp_new_i64();
        tmp32 = tcg_temp_new_i32();
        tcg_gen_qemu_ld32u(tmp, addr, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32, tmp);
        gen_helper_seb(cpu_env, tmp_r1, tmp32);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32);

        tmp32 = load_freg32(r1);
        gen_set_cc_nz_f32(s, tmp32);
        tcg_temp_free_i32(tmp32);
        break;
    case 0xd: /* DEB    R1,D2(X2,B2)       [RXE] */
        tmp = tcg_temp_new_i64();
        tmp32 = tcg_temp_new_i32();
        tcg_gen_qemu_ld32u(tmp, addr, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32, tmp);
        gen_helper_deb(cpu_env, tmp_r1, tmp32);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32);
        break;
    case 0x10: /* TCEB   R1,D2(X2,B2)       [RXE] */
        potential_page_fault(s);
        gen_helper_tceb(cc_op, cpu_env, tmp_r1, addr);
        set_cc_static(s);
        break;
    case 0x11: /* TCDB   R1,D2(X2,B2)       [RXE] */
        potential_page_fault(s);
        gen_helper_tcdb(cc_op, cpu_env, tmp_r1, addr);
        set_cc_static(s);
        break;
    case 0x12: /* TCXB   R1,D2(X2,B2)       [RXE] */
        potential_page_fault(s);
        gen_helper_tcxb(cc_op, cpu_env, tmp_r1, addr);
        set_cc_static(s);
        break;
    case 0x17: /* MEEB   R1,D2(X2,B2)       [RXE] */
        tmp = tcg_temp_new_i64();
        tmp32 = tcg_temp_new_i32();
        tcg_gen_qemu_ld32u(tmp, addr, get_mem_index(s));
        tcg_gen_trunc_i64_i32(tmp32, tmp);
        gen_helper_meeb(cpu_env, tmp_r1, tmp32);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32);
        break;
    case 0x19: /* CDB    R1,D2(X2,B2)       [RXE] */
        potential_page_fault(s);
        gen_helper_cdb(cc_op, cpu_env, tmp_r1, addr);
        set_cc_static(s);
        break;
    case 0x1a: /* ADB    R1,D2(X2,B2)       [RXE] */
        potential_page_fault(s);
        gen_helper_adb(cc_op, cpu_env, tmp_r1, addr);
        set_cc_static(s);
        break;
    case 0x1b: /* SDB    R1,D2(X2,B2)       [RXE] */
        potential_page_fault(s);
        gen_helper_sdb(cc_op, cpu_env, tmp_r1, addr);
        set_cc_static(s);
        break;
    case 0x1c: /* MDB    R1,D2(X2,B2)       [RXE] */
        potential_page_fault(s);
        gen_helper_mdb(cpu_env, tmp_r1, addr);
        break;
    case 0x1d: /* DDB    R1,D2(X2,B2)       [RXE] */
        potential_page_fault(s);
        gen_helper_ddb(cpu_env, tmp_r1, addr);
        break;
    case 0x1e: /* MADB  R1,R3,D2(X2,B2) [RXF] */
        /* for RXF insns, r1 is R3 and r1b is R1 */
        tmp32 = tcg_const_i32(r1b);
        potential_page_fault(s);
        gen_helper_madb(cpu_env, tmp32, addr, tmp_r1);
        tcg_temp_free_i32(tmp32);
        break;
    default:
        LOG_DISAS("illegal ed operation 0x%x\n", op);
        gen_illegal_opcode(s);
        return;
    }
    tcg_temp_free_i32(tmp_r1);
    tcg_temp_free_i64(addr);
}

static void disas_b2(CPUS390XState *env, DisasContext *s, int op,
                     uint32_t insn)
{
    TCGv_i64 tmp, tmp2, tmp3;
    TCGv_i32 tmp32_1, tmp32_2, tmp32_3;
    int r1, r2;
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
        gen_helper_ipm(cpu_env, cc_op, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x41: /* CKSM    R1,R2     [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r2);
        potential_page_fault(s);
        gen_helper_cksm(cpu_env, tmp32_1, tmp32_2);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        gen_op_movi_cc(s, 0);
        break;
    case 0x4e: /* SAR     R1,R2     [RRE] */
        tmp32_1 = load_reg32(r2);
        tcg_gen_st_i32(tmp32_1, cpu_env, offsetof(CPUS390XState, aregs[r1]));
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x4f: /* EAR     R1,R2     [RRE] */
        tmp32_1 = tcg_temp_new_i32();
        tcg_gen_ld_i32(tmp32_1, cpu_env, offsetof(CPUS390XState, aregs[r2]));
        store_reg32(r1, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x54: /* MVPG     R1,R2     [RRE] */
        tmp = load_reg(0);
        tmp2 = load_reg(r1);
        tmp3 = load_reg(r2);
        potential_page_fault(s);
        gen_helper_mvpg(cpu_env, tmp, tmp2, tmp3);
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
        gen_helper_mvst(cpu_env, tmp32_1, tmp32_2, tmp32_3);
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
        gen_helper_clst(cc_op, cpu_env, tmp32_1, tmp32_2, tmp32_3);
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
        gen_helper_srst(cc_op, cpu_env, tmp32_1, tmp32_2, tmp32_3);
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;

#ifndef CONFIG_USER_ONLY
    case 0x02: /* STIDP     D2(B2)     [S] */
        /* Store CPU ID */
        check_privileged(s);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_stidp(cpu_env, tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x04: /* SCK       D2(B2)     [S] */
        /* Set Clock */
        check_privileged(s);
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
        gen_helper_stck(cc_op, cpu_env, tmp);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        break;
    case 0x06: /* SCKC     D2(B2)     [S] */
        /* Set Clock Comparator */
        check_privileged(s);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_sckc(cpu_env, tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x07: /* STCKC    D2(B2)     [S] */
        /* Store Clock Comparator */
        check_privileged(s);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_stckc(cpu_env, tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x08: /* SPT      D2(B2)     [S] */
        /* Set CPU Timer */
        check_privileged(s);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_spt(cpu_env, tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x09: /* STPT     D2(B2)     [S] */
        /* Store CPU Timer */
        check_privileged(s);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_stpt(cpu_env, tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x0a: /* SPKA     D2(B2)     [S] */
        /* Set PSW Key from Address */
        check_privileged(s);
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
        check_privileged(s);
        gen_helper_ptlb(cpu_env);
        break;
    case 0x10: /* SPX      D2(B2)     [S] */
        /* Set Prefix Register */
        check_privileged(s);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_spx(cpu_env, tmp);
        tcg_temp_free_i64(tmp);
        break;
    case 0x11: /* STPX     D2(B2)     [S] */
        /* Store Prefix */
        check_privileged(s);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp2 = tcg_temp_new_i64();
        tcg_gen_ld_i64(tmp2, cpu_env, offsetof(CPUS390XState, psa));
        tcg_gen_qemu_st32(tmp2, tmp, get_mem_index(s));
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x12: /* STAP     D2(B2)     [S] */
        /* Store CPU Address */
        check_privileged(s);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp2 = tcg_temp_new_i64();
        tmp32_1 = tcg_temp_new_i32();
        tcg_gen_ld_i32(tmp32_1, cpu_env, offsetof(CPUS390XState, cpu_num));
        tcg_gen_extu_i32_i64(tmp2, tmp32_1);
        tcg_gen_qemu_st32(tmp2, tmp, get_mem_index(s));
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x21: /* IPTE     R1,R2      [RRE] */
        /* Invalidate PTE */
        check_privileged(s);
        r1 = (insn >> 4) & 0xf;
        r2 = insn & 0xf;
        tmp = load_reg(r1);
        tmp2 = load_reg(r2);
        gen_helper_ipte(cpu_env, tmp, tmp2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x29: /* ISKE     R1,R2      [RRE] */
        /* Insert Storage Key Extended */
        check_privileged(s);
        r1 = (insn >> 4) & 0xf;
        r2 = insn & 0xf;
        tmp = load_reg(r2);
        tmp2 = tcg_temp_new_i64();
        gen_helper_iske(tmp2, cpu_env, tmp);
        store_reg(r1, tmp2);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        break;
    case 0x2a: /* RRBE     R1,R2      [RRE] */
        /* Set Storage Key Extended */
        check_privileged(s);
        r1 = (insn >> 4) & 0xf;
        r2 = insn & 0xf;
        tmp32_1 = load_reg32(r1);
        tmp = load_reg(r2);
        gen_helper_rrbe(cc_op, cpu_env, tmp32_1, tmp);
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp);
        break;
    case 0x2b: /* SSKE     R1,R2      [RRE] */
        /* Set Storage Key Extended */
        check_privileged(s);
        r1 = (insn >> 4) & 0xf;
        r2 = insn & 0xf;
        tmp32_1 = load_reg32(r1);
        tmp = load_reg(r2);
        gen_helper_sske(cpu_env, tmp32_1, tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp);
        break;
    case 0x34: /* STCH ? */
        /* Store Subchannel */
        check_privileged(s);
        gen_op_movi_cc(s, 3);
        break;
    case 0x46: /* STURA    R1,R2      [RRE] */
        /* Store Using Real Address */
        check_privileged(s);
        r1 = (insn >> 4) & 0xf;
        r2 = insn & 0xf;
        tmp32_1 = load_reg32(r1);
        tmp = load_reg(r2);
        potential_page_fault(s);
        gen_helper_stura(cpu_env, tmp, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp);
        break;
    case 0x50: /* CSP      R1,R2      [RRE] */
        /* Compare And Swap And Purge */
        check_privileged(s);
        r1 = (insn >> 4) & 0xf;
        r2 = insn & 0xf;
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r2);
        gen_helper_csp(cc_op, cpu_env, tmp32_1, tmp32_2);
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        break;
    case 0x5f: /* CHSC ? */
        /* Channel Subsystem Call */
        check_privileged(s);
        gen_op_movi_cc(s, 3);
        break;
    case 0x78: /* STCKE    D2(B2)     [S] */
        /* Store Clock Extended */
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_stcke(cc_op, cpu_env, tmp);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        break;
    case 0x79: /* SACF    D2(B2)     [S] */
        /* Set Address Space Control Fast */
        check_privileged(s);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        potential_page_fault(s);
        gen_helper_sacf(cpu_env, tmp);
        tcg_temp_free_i64(tmp);
        /* addressing mode has changed, so end the block */
        s->pc = s->next_pc;
        update_psw_addr(s);
        s->is_jmp = DISAS_JUMP;
        break;
    case 0x7d: /* STSI     D2,(B2)     [S] */
        check_privileged(s);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp32_1 = load_reg32(0);
        tmp32_2 = load_reg32(1);
        potential_page_fault(s);
        gen_helper_stsi(cc_op, cpu_env, tmp, tmp32_1, tmp32_2);
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
        tcg_gen_st_i32(tmp32_1, cpu_env, offsetof(CPUS390XState, fpc));
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0xb1: /* STFL     D2(B2)     [S] */
        /* Store Facility List (CPU features) at 200 */
        check_privileged(s);
        tmp2 = tcg_const_i64(0xc0000000);
        tmp = tcg_const_i64(200);
        tcg_gen_qemu_st32(tmp2, tmp, get_mem_index(s));
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp);
        break;
    case 0xb2: /* LPSWE    D2(B2)     [S] */
        /* Load PSW Extended */
        check_privileged(s);
        decode_rs(s, insn, &r1, &r3, &b2, &d2);
        tmp = get_address(s, 0, b2, d2);
        tmp2 = tcg_temp_new_i64();
        tmp3 = tcg_temp_new_i64();
        tcg_gen_qemu_ld64(tmp2, tmp, get_mem_index(s));
        tcg_gen_addi_i64(tmp, tmp, 8);
        tcg_gen_qemu_ld64(tmp3, tmp, get_mem_index(s));
        gen_helper_load_psw(cpu_env, tmp2, tmp3);
        /* we need to keep cc_op intact */
        s->is_jmp = DISAS_JUMP;
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(tmp2);
        tcg_temp_free_i64(tmp3);
        break;
    case 0x20: /* SERVC     R1,R2     [RRE] */
        /* SCLP Service call (PV hypercall) */
        check_privileged(s);
        potential_page_fault(s);
        tmp32_1 = load_reg32(r2);
        tmp = load_reg(r1);
        gen_helper_servc(cc_op, cpu_env, tmp32_1, tmp);
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp);
        break;
#endif
    default:
        LOG_DISAS("illegal b2 operation 0x%x\n", op);
        gen_illegal_opcode(s);
        break;
    }
}

static void disas_b3(CPUS390XState *env, DisasContext *s, int op, int m3,
                     int r1, int r2)
{
    TCGv_i64 tmp;
    TCGv_i32 tmp32_1, tmp32_2, tmp32_3;
    LOG_DISAS("disas_b3: op 0x%x m3 0x%x r1 %d r2 %d\n", op, m3, r1, r2);
#define FP_HELPER(i) \
    tmp32_1 = tcg_const_i32(r1); \
    tmp32_2 = tcg_const_i32(r2); \
    gen_helper_ ## i(cpu_env, tmp32_1, tmp32_2); \
    tcg_temp_free_i32(tmp32_1); \
    tcg_temp_free_i32(tmp32_2);

#define FP_HELPER_CC(i) \
    tmp32_1 = tcg_const_i32(r1); \
    tmp32_2 = tcg_const_i32(r2); \
    gen_helper_ ## i(cc_op, cpu_env, tmp32_1, tmp32_2); \
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
            gen_helper_maebr(cpu_env, tmp32_1, tmp32_3, tmp32_2);
            break;
        case 0x1e:
            gen_helper_madbr(cpu_env, tmp32_1, tmp32_3, tmp32_2);
            break;
        case 0x1f:
            gen_helper_msdbr(cpu_env, tmp32_1, tmp32_3, tmp32_2);
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
        gen_helper_lzer(cpu_env, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x75: /* LZDR        R1                [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        gen_helper_lzdr(cpu_env, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x76: /* LZXR        R1                [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        gen_helper_lzxr(cpu_env, tmp32_1);
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x84: /* SFPC        R1                [RRE] */
        tmp32_1 = load_reg32(r1);
        tcg_gen_st_i32(tmp32_1, cpu_env, offsetof(CPUS390XState, fpc));
        tcg_temp_free_i32(tmp32_1);
        break;
    case 0x94: /* CEFBR       R1,R2             [RRE] */
    case 0x95: /* CDFBR       R1,R2             [RRE] */
    case 0x96: /* CXFBR       R1,R2             [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = load_reg32(r2);
        switch (op) {
        case 0x94:
            gen_helper_cefbr(cpu_env, tmp32_1, tmp32_2);
            break;
        case 0x95:
            gen_helper_cdfbr(cpu_env, tmp32_1, tmp32_2);
            break;
        case 0x96:
            gen_helper_cxfbr(cpu_env, tmp32_1, tmp32_2);
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
            gen_helper_cfebr(cc_op, cpu_env, tmp32_1, tmp32_2, tmp32_3);
            break;
        case 0x99:
            gen_helper_cfdbr(cc_op, cpu_env, tmp32_1, tmp32_2, tmp32_3);
            break;
        case 0x9a:
            gen_helper_cfxbr(cc_op, cpu_env, tmp32_1, tmp32_2, tmp32_3);
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
            gen_helper_cegbr(cpu_env, tmp32_1, tmp);
            break;
        case 0xa5:
            gen_helper_cdgbr(cpu_env, tmp32_1, tmp);
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
        gen_helper_cxgbr(cpu_env, tmp32_1, tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp);
        break;
    case 0xa8: /* CGEBR       R1,R2             [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r2);
        tmp32_3 = tcg_const_i32(m3);
        gen_helper_cgebr(cc_op, cpu_env, tmp32_1, tmp32_2, tmp32_3);
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    case 0xa9: /* CGDBR       R1,R2             [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r2);
        tmp32_3 = tcg_const_i32(m3);
        gen_helper_cgdbr(cc_op, cpu_env, tmp32_1, tmp32_2, tmp32_3);
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    case 0xaa: /* CGXBR       R1,R2             [RRE] */
        tmp32_1 = tcg_const_i32(r1);
        tmp32_2 = tcg_const_i32(r2);
        tmp32_3 = tcg_const_i32(m3);
        gen_helper_cgxbr(cc_op, cpu_env, tmp32_1, tmp32_2, tmp32_3);
        set_cc_static(s);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i32(tmp32_2);
        tcg_temp_free_i32(tmp32_3);
        break;
    default:
        LOG_DISAS("illegal b3 operation 0x%x\n", op);
        gen_illegal_opcode(s);
        break;
    }

#undef FP_HELPER_CC
#undef FP_HELPER
}

static void disas_b9(CPUS390XState *env, DisasContext *s, int op, int r1,
                     int r2)
{
    TCGv_i64 tmp;
    TCGv_i32 tmp32_1;

    LOG_DISAS("disas_b9: op 0x%x r1 %d r2 %d\n", op, r1, r2);
    switch (op) {
    case 0x17: /* LLGTR      R1,R2     [RRE] */
        tmp32_1 = load_reg32(r2);
        tmp = tcg_temp_new_i64();
        tcg_gen_andi_i32(tmp32_1, tmp32_1, 0x7fffffffUL);
        tcg_gen_extu_i32_i64(tmp, tmp32_1);
        store_reg(r1, tmp);
        tcg_temp_free_i32(tmp32_1);
        tcg_temp_free_i64(tmp);
        break;
    case 0x83: /* FLOGR R1,R2 [RRE] */
        tmp = load_reg(r2);
        tmp32_1 = tcg_const_i32(r1);
        gen_helper_flogr(cc_op, cpu_env, tmp32_1, tmp);
        set_cc_static(s);
        tcg_temp_free_i64(tmp);
        tcg_temp_free_i32(tmp32_1);
        break;
    default:
        LOG_DISAS("illegal b9 operation 0x%x\n", op);
        gen_illegal_opcode(s);
        break;
    }
}

static void disas_s390_insn(CPUS390XState *env, DisasContext *s)
{
    unsigned char opc;
    uint64_t insn;
    int op, r1, r2, r3, d2, x2, b2, r1b;

    opc = cpu_ldub_code(env, s->pc);
    LOG_DISAS("opc 0x%x\n", opc);

    switch (opc) {
    case 0xb2:
        insn = ld_code4(env, s->pc);
        op = (insn >> 16) & 0xff;
        disas_b2(env, s, op, insn);
        break;
    case 0xb3:
        insn = ld_code4(env, s->pc);
        op = (insn >> 16) & 0xff;
        r3 = (insn >> 12) & 0xf; /* aka m3 */
        r1 = (insn >> 4) & 0xf;
        r2 = insn & 0xf;
        disas_b3(env, s, op, r3, r1, r2);
        break;
    case 0xb9:
        insn = ld_code4(env, s->pc);
        r1 = (insn >> 4) & 0xf;
        r2 = insn & 0xf;
        op = (insn >> 16) & 0xff;
        disas_b9(env, s, op, r1, r2);
        break;
    case 0xe3:
        insn = ld_code6(env, s->pc);
        debug_insn(insn);
        op = insn & 0xff;
        r1 = (insn >> 36) & 0xf;
        x2 = (insn >> 32) & 0xf;
        b2 = (insn >> 28) & 0xf;
        d2 = ((int)((((insn >> 16) & 0xfff)
           | ((insn << 4) & 0xff000)) << 12)) >> 12;
        disas_e3(env, s, op,  r1, x2, b2, d2 );
        break;
    case 0xed:
        insn = ld_code6(env, s->pc);
        debug_insn(insn);
        op = insn & 0xff;
        r1 = (insn >> 36) & 0xf;
        x2 = (insn >> 32) & 0xf;
        b2 = (insn >> 28) & 0xf;
        d2 = (short)((insn >> 16) & 0xfff);
        r1b = (insn >> 12) & 0xf;
        disas_ed(env, s, op, r1, x2, b2, d2, r1b);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "unimplemented opcode 0x%x\n", opc);
        gen_illegal_opcode(s);
        break;
    }
}

/* ====================================================================== */
/* Define the insn format enumeration.  */
#define F0(N)                         FMT_##N,
#define F1(N, X1)                     F0(N)
#define F2(N, X1, X2)                 F0(N)
#define F3(N, X1, X2, X3)             F0(N)
#define F4(N, X1, X2, X3, X4)         F0(N)
#define F5(N, X1, X2, X3, X4, X5)     F0(N)

typedef enum {
#include "insn-format.def"
} DisasFormat;

#undef F0
#undef F1
#undef F2
#undef F3
#undef F4
#undef F5

/* Define a structure to hold the decoded fields.  We'll store each inside
   an array indexed by an enum.  In order to conserve memory, we'll arrange
   for fields that do not exist at the same time to overlap, thus the "C"
   for compact.  For checking purposes there is an "O" for original index
   as well that will be applied to availability bitmaps.  */

enum DisasFieldIndexO {
    FLD_O_r1,
    FLD_O_r2,
    FLD_O_r3,
    FLD_O_m1,
    FLD_O_m3,
    FLD_O_m4,
    FLD_O_b1,
    FLD_O_b2,
    FLD_O_b4,
    FLD_O_d1,
    FLD_O_d2,
    FLD_O_d4,
    FLD_O_x2,
    FLD_O_l1,
    FLD_O_l2,
    FLD_O_i1,
    FLD_O_i2,
    FLD_O_i3,
    FLD_O_i4,
    FLD_O_i5
};

enum DisasFieldIndexC {
    FLD_C_r1 = 0,
    FLD_C_m1 = 0,
    FLD_C_b1 = 0,
    FLD_C_i1 = 0,

    FLD_C_r2 = 1,
    FLD_C_b2 = 1,
    FLD_C_i2 = 1,

    FLD_C_r3 = 2,
    FLD_C_m3 = 2,
    FLD_C_i3 = 2,

    FLD_C_m4 = 3,
    FLD_C_b4 = 3,
    FLD_C_i4 = 3,
    FLD_C_l1 = 3,

    FLD_C_i5 = 4,
    FLD_C_d1 = 4,

    FLD_C_d2 = 5,

    FLD_C_d4 = 6,
    FLD_C_x2 = 6,
    FLD_C_l2 = 6,

    NUM_C_FIELD = 7
};

struct DisasFields {
    unsigned op:8;
    unsigned op2:8;
    unsigned presentC:16;
    unsigned int presentO;
    int c[NUM_C_FIELD];
};

/* This is the way fields are to be accessed out of DisasFields.  */
#define have_field(S, F)  have_field1((S), FLD_O_##F)
#define get_field(S, F)   get_field1((S), FLD_O_##F, FLD_C_##F)

static bool have_field1(const DisasFields *f, enum DisasFieldIndexO c)
{
    return (f->presentO >> c) & 1;
}

static int get_field1(const DisasFields *f, enum DisasFieldIndexO o,
                      enum DisasFieldIndexC c)
{
    assert(have_field1(f, o));
    return f->c[c];
}

/* Describe the layout of each field in each format.  */
typedef struct DisasField {
    unsigned int beg:8;
    unsigned int size:8;
    unsigned int type:2;
    unsigned int indexC:6;
    enum DisasFieldIndexO indexO:8;
} DisasField;

typedef struct DisasFormatInfo {
    DisasField op[NUM_C_FIELD];
} DisasFormatInfo;

#define R(N, B)       {  B,  4, 0, FLD_C_r##N, FLD_O_r##N }
#define M(N, B)       {  B,  4, 0, FLD_C_m##N, FLD_O_m##N }
#define BD(N, BB, BD) { BB,  4, 0, FLD_C_b##N, FLD_O_b##N }, \
                      { BD, 12, 0, FLD_C_d##N, FLD_O_d##N }
#define BXD(N)        { 16,  4, 0, FLD_C_b##N, FLD_O_b##N }, \
                      { 12,  4, 0, FLD_C_x##N, FLD_O_x##N }, \
                      { 20, 12, 0, FLD_C_d##N, FLD_O_d##N }
#define BDL(N)        { 16,  4, 0, FLD_C_b##N, FLD_O_b##N }, \
                      { 20, 20, 2, FLD_C_d##N, FLD_O_d##N }
#define BXDL(N)       { 16,  4, 0, FLD_C_b##N, FLD_O_b##N }, \
                      { 12,  4, 0, FLD_C_x##N, FLD_O_x##N }, \
                      { 20, 20, 2, FLD_C_d##N, FLD_O_d##N }
#define I(N, B, S)    {  B,  S, 1, FLD_C_i##N, FLD_O_i##N }
#define L(N, B, S)    {  B,  S, 0, FLD_C_l##N, FLD_O_l##N }

#define F0(N)                     { { } },
#define F1(N, X1)                 { { X1 } },
#define F2(N, X1, X2)             { { X1, X2 } },
#define F3(N, X1, X2, X3)         { { X1, X2, X3 } },
#define F4(N, X1, X2, X3, X4)     { { X1, X2, X3, X4 } },
#define F5(N, X1, X2, X3, X4, X5) { { X1, X2, X3, X4, X5 } },

static const DisasFormatInfo format_info[] = {
#include "insn-format.def"
};

#undef F0
#undef F1
#undef F2
#undef F3
#undef F4
#undef F5
#undef R
#undef M
#undef BD
#undef BXD
#undef BDL
#undef BXDL
#undef I
#undef L

/* Generally, we'll extract operands into this structures, operate upon
   them, and store them back.  See the "in1", "in2", "prep", "wout" sets
   of routines below for more details.  */
typedef struct {
    bool g_out, g_out2, g_in1, g_in2;
    TCGv_i64 out, out2, in1, in2;
    TCGv_i64 addr1;
} DisasOps;

/* Return values from translate_one, indicating the state of the TB.  */
typedef enum {
    /* Continue the TB.  */
    NO_EXIT,
    /* We have emitted one or more goto_tb.  No fixup required.  */
    EXIT_GOTO_TB,
    /* We are not using a goto_tb (for whatever reason), but have updated
       the PC (for whatever reason), so there's no need to do it again on
       exiting the TB.  */
    EXIT_PC_UPDATED,
    /* We are exiting the TB, but have neither emitted a goto_tb, nor
       updated the PC for the next instruction to be executed.  */
    EXIT_PC_STALE,
    /* We are ending the TB with a noreturn function call, e.g. longjmp.
       No following code will be executed.  */
    EXIT_NORETURN,
} ExitStatus;

typedef enum DisasFacility {
    FAC_Z,                  /* zarch (default) */
    FAC_CASS,               /* compare and swap and store */
    FAC_CASS2,              /* compare and swap and store 2*/
    FAC_DFP,                /* decimal floating point */
    FAC_DFPR,               /* decimal floating point rounding */
    FAC_DO,                 /* distinct operands */
    FAC_EE,                 /* execute extensions */
    FAC_EI,                 /* extended immediate */
    FAC_FPE,                /* floating point extension */
    FAC_FPSSH,              /* floating point support sign handling */
    FAC_FPRGR,              /* FPR-GR transfer */
    FAC_GIE,                /* general instructions extension */
    FAC_HFP_MA,             /* HFP multiply-and-add/subtract */
    FAC_HW,                 /* high-word */
    FAC_IEEEE_SIM,          /* IEEE exception sumilation */
    FAC_LOC,                /* load/store on condition */
    FAC_LD,                 /* long displacement */
    FAC_PC,                 /* population count */
    FAC_SCF,                /* store clock fast */
    FAC_SFLE,               /* store facility list extended */
} DisasFacility;

struct DisasInsn {
    unsigned opc:16;
    DisasFormat fmt:6;
    DisasFacility fac:6;

    const char *name;

    void (*help_in1)(DisasContext *, DisasFields *, DisasOps *);
    void (*help_in2)(DisasContext *, DisasFields *, DisasOps *);
    void (*help_prep)(DisasContext *, DisasFields *, DisasOps *);
    void (*help_wout)(DisasContext *, DisasFields *, DisasOps *);
    void (*help_cout)(DisasContext *, DisasOps *);
    ExitStatus (*help_op)(DisasContext *, DisasOps *);

    uint64_t data;
};

/* ====================================================================== */
/* Miscelaneous helpers, used by several operations.  */

static void help_l2_shift(DisasContext *s, DisasFields *f,
                          DisasOps *o, int mask)
{
    int b2 = get_field(f, b2);
    int d2 = get_field(f, d2);

    if (b2 == 0) {
        o->in2 = tcg_const_i64(d2 & mask);
    } else {
        o->in2 = get_address(s, 0, b2, d2);
        tcg_gen_andi_i64(o->in2, o->in2, mask);
    }
}

static ExitStatus help_goto_direct(DisasContext *s, uint64_t dest)
{
    if (dest == s->next_pc) {
        return NO_EXIT;
    }
    if (use_goto_tb(s, dest)) {
        gen_update_cc_op(s);
        tcg_gen_goto_tb(0);
        tcg_gen_movi_i64(psw_addr, dest);
        tcg_gen_exit_tb((tcg_target_long)s->tb);
        return EXIT_GOTO_TB;
    } else {
        tcg_gen_movi_i64(psw_addr, dest);
        return EXIT_PC_UPDATED;
    }
}

static ExitStatus help_branch(DisasContext *s, DisasCompare *c,
                              bool is_imm, int imm, TCGv_i64 cdest)
{
    ExitStatus ret;
    uint64_t dest = s->pc + 2 * imm;
    int lab;

    /* Take care of the special cases first.  */
    if (c->cond == TCG_COND_NEVER) {
        ret = NO_EXIT;
        goto egress;
    }
    if (is_imm) {
        if (dest == s->next_pc) {
            /* Branch to next.  */
            ret = NO_EXIT;
            goto egress;
        }
        if (c->cond == TCG_COND_ALWAYS) {
            ret = help_goto_direct(s, dest);
            goto egress;
        }
    } else {
        if (TCGV_IS_UNUSED_I64(cdest)) {
            /* E.g. bcr %r0 -> no branch.  */
            ret = NO_EXIT;
            goto egress;
        }
        if (c->cond == TCG_COND_ALWAYS) {
            tcg_gen_mov_i64(psw_addr, cdest);
            ret = EXIT_PC_UPDATED;
            goto egress;
        }
    }

    if (use_goto_tb(s, s->next_pc)) {
        if (is_imm && use_goto_tb(s, dest)) {
            /* Both exits can use goto_tb.  */
            gen_update_cc_op(s);

            lab = gen_new_label();
            if (c->is_64) {
                tcg_gen_brcond_i64(c->cond, c->u.s64.a, c->u.s64.b, lab);
            } else {
                tcg_gen_brcond_i32(c->cond, c->u.s32.a, c->u.s32.b, lab);
            }

            /* Branch not taken.  */
            tcg_gen_goto_tb(0);
            tcg_gen_movi_i64(psw_addr, s->next_pc);
            tcg_gen_exit_tb((tcg_target_long)s->tb + 0);

            /* Branch taken.  */
            gen_set_label(lab);
            tcg_gen_goto_tb(1);
            tcg_gen_movi_i64(psw_addr, dest);
            tcg_gen_exit_tb((tcg_target_long)s->tb + 1);

            ret = EXIT_GOTO_TB;
        } else {
            /* Fallthru can use goto_tb, but taken branch cannot.  */
            /* Store taken branch destination before the brcond.  This
               avoids having to allocate a new local temp to hold it.
               We'll overwrite this in the not taken case anyway.  */
            if (!is_imm) {
                tcg_gen_mov_i64(psw_addr, cdest);
            }

            lab = gen_new_label();
            if (c->is_64) {
                tcg_gen_brcond_i64(c->cond, c->u.s64.a, c->u.s64.b, lab);
            } else {
                tcg_gen_brcond_i32(c->cond, c->u.s32.a, c->u.s32.b, lab);
            }

            /* Branch not taken.  */
            gen_update_cc_op(s);
            tcg_gen_goto_tb(0);
            tcg_gen_movi_i64(psw_addr, s->next_pc);
            tcg_gen_exit_tb((tcg_target_long)s->tb + 0);

            gen_set_label(lab);
            if (is_imm) {
                tcg_gen_movi_i64(psw_addr, dest);
            }
            ret = EXIT_PC_UPDATED;
        }
    } else {
        /* Fallthru cannot use goto_tb.  This by itself is vanishingly rare.
           Most commonly we're single-stepping or some other condition that
           disables all use of goto_tb.  Just update the PC and exit.  */

        TCGv_i64 next = tcg_const_i64(s->next_pc);
        if (is_imm) {
            cdest = tcg_const_i64(dest);
        }

        if (c->is_64) {
            tcg_gen_movcond_i64(c->cond, psw_addr, c->u.s64.a, c->u.s64.b,
                                cdest, next);
        } else {
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i64 t1 = tcg_temp_new_i64();
            TCGv_i64 z = tcg_const_i64(0);
            tcg_gen_setcond_i32(c->cond, t0, c->u.s32.a, c->u.s32.b);
            tcg_gen_extu_i32_i64(t1, t0);
            tcg_temp_free_i32(t0);
            tcg_gen_movcond_i64(TCG_COND_NE, psw_addr, t1, z, cdest, next);
            tcg_temp_free_i64(t1);
            tcg_temp_free_i64(z);
        }

        if (is_imm) {
            tcg_temp_free_i64(cdest);
        }
        tcg_temp_free_i64(next);

        ret = EXIT_PC_UPDATED;
    }

 egress:
    free_compare(c);
    return ret;
}

/* ====================================================================== */
/* The operations.  These perform the bulk of the work for any insn,
   usually after the operands have been loaded and output initialized.  */

static ExitStatus op_abs(DisasContext *s, DisasOps *o)
{
    gen_helper_abs_i64(o->out, o->in2);
    return NO_EXIT;
}

static ExitStatus op_add(DisasContext *s, DisasOps *o)
{
    tcg_gen_add_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_addc(DisasContext *s, DisasOps *o)
{
    TCGv_i64 cc;

    tcg_gen_add_i64(o->out, o->in1, o->in2);

    /* XXX possible optimization point */
    gen_op_calc_cc(s);
    cc = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(cc, cc_op);
    tcg_gen_shri_i64(cc, cc, 1);

    tcg_gen_add_i64(o->out, o->out, cc);
    tcg_temp_free_i64(cc);
    return NO_EXIT;
}

static ExitStatus op_and(DisasContext *s, DisasOps *o)
{
    tcg_gen_and_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_andi(DisasContext *s, DisasOps *o)
{
    int shift = s->insn->data & 0xff;
    int size = s->insn->data >> 8;
    uint64_t mask = ((1ull << size) - 1) << shift;

    assert(!o->g_in2);
    tcg_gen_shli_i64(o->in2, o->in2, shift);
    tcg_gen_ori_i64(o->in2, o->in2, ~mask);
    tcg_gen_and_i64(o->out, o->in1, o->in2);

    /* Produce the CC from only the bits manipulated.  */
    tcg_gen_andi_i64(cc_dst, o->out, mask);
    set_cc_nz_u64(s, cc_dst);
    return NO_EXIT;
}

static ExitStatus op_bas(DisasContext *s, DisasOps *o)
{
    tcg_gen_movi_i64(o->out, pc_to_link_info(s, s->next_pc));
    if (!TCGV_IS_UNUSED_I64(o->in2)) {
        tcg_gen_mov_i64(psw_addr, o->in2);
        return EXIT_PC_UPDATED;
    } else {
        return NO_EXIT;
    }
}

static ExitStatus op_basi(DisasContext *s, DisasOps *o)
{
    tcg_gen_movi_i64(o->out, pc_to_link_info(s, s->next_pc));
    return help_goto_direct(s, s->pc + 2 * get_field(s->fields, i2));
}

static ExitStatus op_bc(DisasContext *s, DisasOps *o)
{
    int m1 = get_field(s->fields, m1);
    bool is_imm = have_field(s->fields, i2);
    int imm = is_imm ? get_field(s->fields, i2) : 0;
    DisasCompare c;

    disas_jcc(s, &c, m1);
    return help_branch(s, &c, is_imm, imm, o->in2);
}

static ExitStatus op_bct32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    bool is_imm = have_field(s->fields, i2);
    int imm = is_imm ? get_field(s->fields, i2) : 0;
    DisasCompare c;
    TCGv_i64 t;

    c.cond = TCG_COND_NE;
    c.is_64 = false;
    c.g1 = false;
    c.g2 = false;

    t = tcg_temp_new_i64();
    tcg_gen_subi_i64(t, regs[r1], 1);
    store_reg32_i64(r1, t);
    c.u.s32.a = tcg_temp_new_i32();
    c.u.s32.b = tcg_const_i32(0);
    tcg_gen_trunc_i64_i32(c.u.s32.a, t);
    tcg_temp_free_i64(t);

    return help_branch(s, &c, is_imm, imm, o->in2);
}

static ExitStatus op_bct64(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    bool is_imm = have_field(s->fields, i2);
    int imm = is_imm ? get_field(s->fields, i2) : 0;
    DisasCompare c;

    c.cond = TCG_COND_NE;
    c.is_64 = true;
    c.g1 = true;
    c.g2 = false;

    tcg_gen_subi_i64(regs[r1], regs[r1], 1);
    c.u.s64.a = regs[r1];
    c.u.s64.b = tcg_const_i64(0);

    return help_branch(s, &c, is_imm, imm, o->in2);
}

static ExitStatus op_clc(DisasContext *s, DisasOps *o)
{
    int l = get_field(s->fields, l1);
    TCGv_i32 vl;

    switch (l + 1) {
    case 1:
        tcg_gen_qemu_ld8u(cc_src, o->addr1, get_mem_index(s));
        tcg_gen_qemu_ld8u(cc_dst, o->in2, get_mem_index(s));
        break;
    case 2:
        tcg_gen_qemu_ld16u(cc_src, o->addr1, get_mem_index(s));
        tcg_gen_qemu_ld16u(cc_dst, o->in2, get_mem_index(s));
        break;
    case 4:
        tcg_gen_qemu_ld32u(cc_src, o->addr1, get_mem_index(s));
        tcg_gen_qemu_ld32u(cc_dst, o->in2, get_mem_index(s));
        break;
    case 8:
        tcg_gen_qemu_ld64(cc_src, o->addr1, get_mem_index(s));
        tcg_gen_qemu_ld64(cc_dst, o->in2, get_mem_index(s));
        break;
    default:
        potential_page_fault(s);
        vl = tcg_const_i32(l);
        gen_helper_clc(cc_op, cpu_env, vl, o->addr1, o->in2);
        tcg_temp_free_i32(vl);
        set_cc_static(s);
        return NO_EXIT;
    }
    gen_op_update2_cc_i64(s, CC_OP_LTUGTU_64, cc_src, cc_dst);
    return NO_EXIT;
}

static ExitStatus op_clcle(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    potential_page_fault(s);
    gen_helper_clcle(cc_op, cpu_env, r1, o->in2, r3);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r3);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_clm(DisasContext *s, DisasOps *o)
{
    TCGv_i32 m3 = tcg_const_i32(get_field(s->fields, m3));
    TCGv_i32 t1 = tcg_temp_new_i32();
    tcg_gen_trunc_i64_i32(t1, o->in1);
    potential_page_fault(s);
    gen_helper_clm(cc_op, cpu_env, t1, m3, o->in2);
    set_cc_static(s);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(m3);
    return NO_EXIT;
}

static ExitStatus op_cs(DisasContext *s, DisasOps *o)
{
    int r3 = get_field(s->fields, r3);
    potential_page_fault(s);
    gen_helper_cs(o->out, cpu_env, o->in1, o->in2, regs[r3]);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_csg(DisasContext *s, DisasOps *o)
{
    int r3 = get_field(s->fields, r3);
    potential_page_fault(s);
    gen_helper_csg(o->out, cpu_env, o->in1, o->in2, regs[r3]);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_cds(DisasContext *s, DisasOps *o)
{
    int r3 = get_field(s->fields, r3);
    TCGv_i64 in3 = tcg_temp_new_i64();
    tcg_gen_deposit_i64(in3, regs[r3 + 1], regs[r3], 32, 32);
    potential_page_fault(s);
    gen_helper_csg(o->out, cpu_env, o->in1, o->in2, in3);
    tcg_temp_free_i64(in3);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_cdsg(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    potential_page_fault(s);
    /* XXX rewrite in tcg */
    gen_helper_cdsg(cc_op, cpu_env, r1, o->in2, r3);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_cvd(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i32 t2 = tcg_temp_new_i32();
    tcg_gen_trunc_i64_i32(t2, o->in1);
    gen_helper_cvd(t1, t2);
    tcg_temp_free_i32(t2);
    tcg_gen_qemu_st64(t1, o->in2, get_mem_index(s));
    tcg_temp_free_i64(t1);
    return NO_EXIT;
}

#ifndef CONFIG_USER_ONLY
static ExitStatus op_diag(DisasContext *s, DisasOps *o)
{
    TCGv_i32 tmp;

    check_privileged(s);
    potential_page_fault(s);

    /* We pretend the format is RX_a so that D2 is the field we want.  */
    tmp = tcg_const_i32(get_field(s->fields, d2) & 0xfff);
    gen_helper_diag(regs[2], cpu_env, tmp, regs[2], regs[1]);
    tcg_temp_free_i32(tmp);
    return NO_EXIT;
}
#endif

static ExitStatus op_divs32(DisasContext *s, DisasOps *o)
{
    gen_helper_divs32(o->out2, cpu_env, o->in1, o->in2);
    return_low128(o->out);
    return NO_EXIT;
}

static ExitStatus op_divu32(DisasContext *s, DisasOps *o)
{
    gen_helper_divu32(o->out2, cpu_env, o->in1, o->in2);
    return_low128(o->out);
    return NO_EXIT;
}

static ExitStatus op_divs64(DisasContext *s, DisasOps *o)
{
    gen_helper_divs64(o->out2, cpu_env, o->in1, o->in2);
    return_low128(o->out);
    return NO_EXIT;
}

static ExitStatus op_divu64(DisasContext *s, DisasOps *o)
{
    gen_helper_divu64(o->out2, cpu_env, o->out, o->out2, o->in2);
    return_low128(o->out);
    return NO_EXIT;
}

static ExitStatus op_efpc(DisasContext *s, DisasOps *o)
{
    tcg_gen_ld32u_i64(o->out, cpu_env, offsetof(CPUS390XState, fpc));
    return NO_EXIT;
}

static ExitStatus op_ex(DisasContext *s, DisasOps *o)
{
    /* ??? Perhaps a better way to implement EXECUTE is to set a bit in
       tb->flags, (ab)use the tb->cs_base field as the address of
       the template in memory, and grab 8 bits of tb->flags/cflags for
       the contents of the register.  We would then recognize all this
       in gen_intermediate_code_internal, generating code for exactly
       one instruction.  This new TB then gets executed normally.

       On the other hand, this seems to be mostly used for modifying
       MVC inside of memcpy, which needs a helper call anyway.  So
       perhaps this doesn't bear thinking about any further.  */

    TCGv_i64 tmp;

    update_psw_addr(s);
    gen_op_calc_cc(s);

    tmp = tcg_const_i64(s->next_pc);
    gen_helper_ex(cc_op, cpu_env, cc_op, o->in1, o->in2, tmp);
    tcg_temp_free_i64(tmp);

    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_icm(DisasContext *s, DisasOps *o)
{
    int m3 = get_field(s->fields, m3);
    int pos, len, base = s->insn->data;
    TCGv_i64 tmp = tcg_temp_new_i64();
    uint64_t ccm;

    switch (m3) {
    case 0xf:
        /* Effectively a 32-bit load.  */
        tcg_gen_qemu_ld32u(tmp, o->in2, get_mem_index(s));
        len = 32;
        goto one_insert;

    case 0xc:
    case 0x6:
    case 0x3:
        /* Effectively a 16-bit load.  */
        tcg_gen_qemu_ld16u(tmp, o->in2, get_mem_index(s));
        len = 16;
        goto one_insert;

    case 0x8:
    case 0x4:
    case 0x2:
    case 0x1:
        /* Effectively an 8-bit load.  */
        tcg_gen_qemu_ld8u(tmp, o->in2, get_mem_index(s));
        len = 8;
        goto one_insert;

    one_insert:
        pos = base + ctz32(m3) * 8;
        tcg_gen_deposit_i64(o->out, o->out, tmp, pos, len);
        ccm = ((1ull << len) - 1) << pos;
        break;

    default:
        /* This is going to be a sequence of loads and inserts.  */
        pos = base + 32 - 8;
        ccm = 0;
        while (m3) {
            if (m3 & 0x8) {
                tcg_gen_qemu_ld8u(tmp, o->in2, get_mem_index(s));
                tcg_gen_addi_i64(o->in2, o->in2, 1);
                tcg_gen_deposit_i64(o->out, o->out, tmp, pos, 8);
                ccm |= 0xff << pos;
            }
            m3 = (m3 << 1) & 0xf;
            pos -= 8;
        }
        break;
    }

    tcg_gen_movi_i64(tmp, ccm);
    gen_op_update2_cc_i64(s, CC_OP_ICM, tmp, o->out);
    tcg_temp_free_i64(tmp);
    return NO_EXIT;
}

static ExitStatus op_insi(DisasContext *s, DisasOps *o)
{
    int shift = s->insn->data & 0xff;
    int size = s->insn->data >> 8;
    tcg_gen_deposit_i64(o->out, o->in1, o->in2, shift, size);
    return NO_EXIT;
}

static ExitStatus op_ld8s(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld8s(o->out, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_ld8u(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld8u(o->out, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_ld16s(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld16s(o->out, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_ld16u(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld16u(o->out, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_ld32s(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld32s(o->out, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_ld32u(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld32u(o->out, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_ld64(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_ld64(o->out, o->in2, get_mem_index(s));
    return NO_EXIT;
}

#ifndef CONFIG_USER_ONLY
static ExitStatus op_lctl(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_lctl(cpu_env, r1, o->in2, r3);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r3);
    return NO_EXIT;
}

static ExitStatus op_lctlg(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_lctlg(cpu_env, r1, o->in2, r3);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r3);
    return NO_EXIT;
}
static ExitStatus op_lra(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_lra(o->out, cpu_env, o->in2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_lpsw(DisasContext *s, DisasOps *o)
{
    TCGv_i64 t1, t2;

    check_privileged(s);

    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    tcg_gen_qemu_ld32u(t1, o->in2, get_mem_index(s));
    tcg_gen_addi_i64(o->in2, o->in2, 4);
    tcg_gen_qemu_ld32u(t2, o->in2, get_mem_index(s));
    /* Convert the 32-bit PSW_MASK into the 64-bit PSW_MASK.  */
    tcg_gen_shli_i64(t1, t1, 32);
    gen_helper_load_psw(cpu_env, t1, t2);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t2);
    return EXIT_NORETURN;
}
#endif

static ExitStatus op_lam(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    potential_page_fault(s);
    gen_helper_lam(cpu_env, r1, o->in2, r3);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r3);
    return NO_EXIT;
}

static ExitStatus op_lm32(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    int r3 = get_field(s->fields, r3);
    TCGv_i64 t = tcg_temp_new_i64();
    TCGv_i64 t4 = tcg_const_i64(4);

    while (1) {
        tcg_gen_qemu_ld32u(t, o->in2, get_mem_index(s));
        store_reg32_i64(r1, t);
        if (r1 == r3) {
            break;
        }
        tcg_gen_add_i64(o->in2, o->in2, t4);
        r1 = (r1 + 1) & 15;
    }

    tcg_temp_free_i64(t);
    tcg_temp_free_i64(t4);
    return NO_EXIT;
}

static ExitStatus op_lmh(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    int r3 = get_field(s->fields, r3);
    TCGv_i64 t = tcg_temp_new_i64();
    TCGv_i64 t4 = tcg_const_i64(4);

    while (1) {
        tcg_gen_qemu_ld32u(t, o->in2, get_mem_index(s));
        store_reg32h_i64(r1, t);
        if (r1 == r3) {
            break;
        }
        tcg_gen_add_i64(o->in2, o->in2, t4);
        r1 = (r1 + 1) & 15;
    }

    tcg_temp_free_i64(t);
    tcg_temp_free_i64(t4);
    return NO_EXIT;
}

static ExitStatus op_lm64(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    int r3 = get_field(s->fields, r3);
    TCGv_i64 t8 = tcg_const_i64(8);

    while (1) {
        tcg_gen_qemu_ld64(regs[r1], o->in2, get_mem_index(s));
        if (r1 == r3) {
            break;
        }
        tcg_gen_add_i64(o->in2, o->in2, t8);
        r1 = (r1 + 1) & 15;
    }

    tcg_temp_free_i64(t8);
    return NO_EXIT;
}

static ExitStatus op_mov2(DisasContext *s, DisasOps *o)
{
    o->out = o->in2;
    o->g_out = o->g_in2;
    TCGV_UNUSED_I64(o->in2);
    o->g_in2 = false;
    return NO_EXIT;
}

static ExitStatus op_movx(DisasContext *s, DisasOps *o)
{
    o->out = o->in1;
    o->out2 = o->in2;
    o->g_out = o->g_in1;
    o->g_out2 = o->g_in2;
    TCGV_UNUSED_I64(o->in1);
    TCGV_UNUSED_I64(o->in2);
    o->g_in1 = o->g_in2 = false;
    return NO_EXIT;
}

static ExitStatus op_mvc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_const_i32(get_field(s->fields, l1));
    potential_page_fault(s);
    gen_helper_mvc(cpu_env, l, o->addr1, o->in2);
    tcg_temp_free_i32(l);
    return NO_EXIT;
}

static ExitStatus op_mvcl(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r2 = tcg_const_i32(get_field(s->fields, r2));
    potential_page_fault(s);
    gen_helper_mvcl(cc_op, cpu_env, r1, r2);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_mvcle(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    potential_page_fault(s);
    gen_helper_mvcle(cc_op, cpu_env, r1, o->in2, r3);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r3);
    set_cc_static(s);
    return NO_EXIT;
}

#ifndef CONFIG_USER_ONLY
static ExitStatus op_mvcp(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, l1);
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_mvcp(cc_op, cpu_env, regs[r1], o->addr1, o->in2);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_mvcs(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, l1);
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_mvcs(cc_op, cpu_env, regs[r1], o->addr1, o->in2);
    set_cc_static(s);
    return NO_EXIT;
}
#endif

static ExitStatus op_mul(DisasContext *s, DisasOps *o)
{
    tcg_gen_mul_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_mul128(DisasContext *s, DisasOps *o)
{
    gen_helper_mul128(o->out, cpu_env, o->in1, o->in2);
    return_low128(o->out2);
    return NO_EXIT;
}

static ExitStatus op_nabs(DisasContext *s, DisasOps *o)
{
    gen_helper_nabs_i64(o->out, o->in2);
    return NO_EXIT;
}

static ExitStatus op_nc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_const_i32(get_field(s->fields, l1));
    potential_page_fault(s);
    gen_helper_nc(cc_op, cpu_env, l, o->addr1, o->in2);
    tcg_temp_free_i32(l);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_neg(DisasContext *s, DisasOps *o)
{
    tcg_gen_neg_i64(o->out, o->in2);
    return NO_EXIT;
}

static ExitStatus op_oc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_const_i32(get_field(s->fields, l1));
    potential_page_fault(s);
    gen_helper_oc(cc_op, cpu_env, l, o->addr1, o->in2);
    tcg_temp_free_i32(l);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_or(DisasContext *s, DisasOps *o)
{
    tcg_gen_or_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_ori(DisasContext *s, DisasOps *o)
{
    int shift = s->insn->data & 0xff;
    int size = s->insn->data >> 8;
    uint64_t mask = ((1ull << size) - 1) << shift;

    assert(!o->g_in2);
    tcg_gen_shli_i64(o->in2, o->in2, shift);
    tcg_gen_or_i64(o->out, o->in1, o->in2);

    /* Produce the CC from only the bits manipulated.  */
    tcg_gen_andi_i64(cc_dst, o->out, mask);
    set_cc_nz_u64(s, cc_dst);
    return NO_EXIT;
}

static ExitStatus op_rev16(DisasContext *s, DisasOps *o)
{
    tcg_gen_bswap16_i64(o->out, o->in2);
    return NO_EXIT;
}

static ExitStatus op_rev32(DisasContext *s, DisasOps *o)
{
    tcg_gen_bswap32_i64(o->out, o->in2);
    return NO_EXIT;
}

static ExitStatus op_rev64(DisasContext *s, DisasOps *o)
{
    tcg_gen_bswap64_i64(o->out, o->in2);
    return NO_EXIT;
}

static ExitStatus op_rll32(DisasContext *s, DisasOps *o)
{
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();
    TCGv_i32 to = tcg_temp_new_i32();
    tcg_gen_trunc_i64_i32(t1, o->in1);
    tcg_gen_trunc_i64_i32(t2, o->in2);
    tcg_gen_rotl_i32(to, t1, t2);
    tcg_gen_extu_i32_i64(o->out, to);
    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
    tcg_temp_free_i32(to);
    return NO_EXIT;
}

static ExitStatus op_rll64(DisasContext *s, DisasOps *o)
{
    tcg_gen_rotl_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

#ifndef CONFIG_USER_ONLY
static ExitStatus op_sigp(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_sigp(cc_op, cpu_env, o->in2, r1, o->in1);
    tcg_temp_free_i32(r1);
    return NO_EXIT;
}
#endif

static ExitStatus op_sla(DisasContext *s, DisasOps *o)
{
    uint64_t sign = 1ull << s->insn->data;
    enum cc_op cco = s->insn->data == 31 ? CC_OP_SLA_32 : CC_OP_SLA_64;
    gen_op_update2_cc_i64(s, cco, o->in1, o->in2);
    tcg_gen_shl_i64(o->out, o->in1, o->in2);
    /* The arithmetic left shift is curious in that it does not affect
       the sign bit.  Copy that over from the source unchanged.  */
    tcg_gen_andi_i64(o->out, o->out, ~sign);
    tcg_gen_andi_i64(o->in1, o->in1, sign);
    tcg_gen_or_i64(o->out, o->out, o->in1);
    return NO_EXIT;
}

static ExitStatus op_sll(DisasContext *s, DisasOps *o)
{
    tcg_gen_shl_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_sra(DisasContext *s, DisasOps *o)
{
    tcg_gen_sar_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_srl(DisasContext *s, DisasOps *o)
{
    tcg_gen_shr_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

#ifndef CONFIG_USER_ONLY
static ExitStatus op_ssm(DisasContext *s, DisasOps *o)
{
    check_privileged(s);
    tcg_gen_deposit_i64(psw_mask, psw_mask, o->in2, 56, 8);
    return NO_EXIT;
}

static ExitStatus op_stctg(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_stctg(cpu_env, r1, o->in2, r3);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r3);
    return NO_EXIT;
}

static ExitStatus op_stctl(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    check_privileged(s);
    potential_page_fault(s);
    gen_helper_stctl(cpu_env, r1, o->in2, r3);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r3);
    return NO_EXIT;
}

static ExitStatus op_stnosm(DisasContext *s, DisasOps *o)
{
    uint64_t i2 = get_field(s->fields, i2);
    TCGv_i64 t;

    check_privileged(s);

    /* It is important to do what the instruction name says: STORE THEN.
       If we let the output hook perform the store then if we fault and
       restart, we'll have the wrong SYSTEM MASK in place.  */
    t = tcg_temp_new_i64();
    tcg_gen_shri_i64(t, psw_mask, 56);
    tcg_gen_qemu_st8(t, o->addr1, get_mem_index(s));
    tcg_temp_free_i64(t);

    if (s->fields->op == 0xac) {
        tcg_gen_andi_i64(psw_mask, psw_mask,
                         (i2 << 56) | 0x00ffffffffffffffull);
    } else {
        tcg_gen_ori_i64(psw_mask, psw_mask, i2 << 56);
    }
    return NO_EXIT;
}
#endif

static ExitStatus op_st8(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st8(o->in1, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_st16(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st16(o->in1, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_st32(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st32(o->in1, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_st64(DisasContext *s, DisasOps *o)
{
    tcg_gen_qemu_st64(o->in1, o->in2, get_mem_index(s));
    return NO_EXIT;
}

static ExitStatus op_stam(DisasContext *s, DisasOps *o)
{
    TCGv_i32 r1 = tcg_const_i32(get_field(s->fields, r1));
    TCGv_i32 r3 = tcg_const_i32(get_field(s->fields, r3));
    potential_page_fault(s);
    gen_helper_stam(cpu_env, r1, o->in2, r3);
    tcg_temp_free_i32(r1);
    tcg_temp_free_i32(r3);
    return NO_EXIT;
}

static ExitStatus op_stcm(DisasContext *s, DisasOps *o)
{
    int m3 = get_field(s->fields, m3);
    int pos, base = s->insn->data;
    TCGv_i64 tmp = tcg_temp_new_i64();

    pos = base + ctz32(m3) * 8;
    switch (m3) {
    case 0xf:
        /* Effectively a 32-bit store.  */
        tcg_gen_shri_i64(tmp, o->in1, pos);
        tcg_gen_qemu_st32(tmp, o->in2, get_mem_index(s));
        break;

    case 0xc:
    case 0x6:
    case 0x3:
        /* Effectively a 16-bit store.  */
        tcg_gen_shri_i64(tmp, o->in1, pos);
        tcg_gen_qemu_st16(tmp, o->in2, get_mem_index(s));
        break;

    case 0x8:
    case 0x4:
    case 0x2:
    case 0x1:
        /* Effectively an 8-bit store.  */
        tcg_gen_shri_i64(tmp, o->in1, pos);
        tcg_gen_qemu_st8(tmp, o->in2, get_mem_index(s));
        break;

    default:
        /* This is going to be a sequence of shifts and stores.  */
        pos = base + 32 - 8;
        while (m3) {
            if (m3 & 0x8) {
                tcg_gen_shri_i64(tmp, o->in1, pos);
                tcg_gen_qemu_st8(tmp, o->in2, get_mem_index(s));
                tcg_gen_addi_i64(o->in2, o->in2, 1);
            }
            m3 = (m3 << 1) & 0xf;
            pos -= 8;
        }
        break;
    }
    tcg_temp_free_i64(tmp);
    return NO_EXIT;
}

static ExitStatus op_stm(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    int r3 = get_field(s->fields, r3);
    int size = s->insn->data;
    TCGv_i64 tsize = tcg_const_i64(size);

    while (1) {
        if (size == 8) {
            tcg_gen_qemu_st64(regs[r1], o->in2, get_mem_index(s));
        } else {
            tcg_gen_qemu_st32(regs[r1], o->in2, get_mem_index(s));
        }
        if (r1 == r3) {
            break;
        }
        tcg_gen_add_i64(o->in2, o->in2, tsize);
        r1 = (r1 + 1) & 15;
    }

    tcg_temp_free_i64(tsize);
    return NO_EXIT;
}

static ExitStatus op_stmh(DisasContext *s, DisasOps *o)
{
    int r1 = get_field(s->fields, r1);
    int r3 = get_field(s->fields, r3);
    TCGv_i64 t = tcg_temp_new_i64();
    TCGv_i64 t4 = tcg_const_i64(4);
    TCGv_i64 t32 = tcg_const_i64(32);

    while (1) {
        tcg_gen_shl_i64(t, regs[r1], t32);
        tcg_gen_qemu_st32(t, o->in2, get_mem_index(s));
        if (r1 == r3) {
            break;
        }
        tcg_gen_add_i64(o->in2, o->in2, t4);
        r1 = (r1 + 1) & 15;
    }

    tcg_temp_free_i64(t);
    tcg_temp_free_i64(t4);
    tcg_temp_free_i64(t32);
    return NO_EXIT;
}

static ExitStatus op_sub(DisasContext *s, DisasOps *o)
{
    tcg_gen_sub_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_subb(DisasContext *s, DisasOps *o)
{
    TCGv_i64 cc;

    assert(!o->g_in2);
    tcg_gen_not_i64(o->in2, o->in2);
    tcg_gen_add_i64(o->out, o->in1, o->in2);

    /* XXX possible optimization point */
    gen_op_calc_cc(s);
    cc = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(cc, cc_op);
    tcg_gen_shri_i64(cc, cc, 1);
    tcg_gen_add_i64(o->out, o->out, cc);
    tcg_temp_free_i64(cc);
    return NO_EXIT;
}

static ExitStatus op_svc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 t;

    update_psw_addr(s);
    gen_op_calc_cc(s);

    t = tcg_const_i32(get_field(s->fields, i1) & 0xff);
    tcg_gen_st_i32(t, cpu_env, offsetof(CPUS390XState, int_svc_code));
    tcg_temp_free_i32(t);

    t = tcg_const_i32(s->next_pc - s->pc);
    tcg_gen_st_i32(t, cpu_env, offsetof(CPUS390XState, int_svc_ilen));
    tcg_temp_free_i32(t);

    gen_exception(EXCP_SVC);
    return EXIT_NORETURN;
}

#ifndef CONFIG_USER_ONLY
static ExitStatus op_tprot(DisasContext *s, DisasOps *o)
{
    potential_page_fault(s);
    gen_helper_tprot(cc_op, o->addr1, o->in2);
    set_cc_static(s);
    return NO_EXIT;
}
#endif

static ExitStatus op_tr(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_const_i32(get_field(s->fields, l1));
    potential_page_fault(s);
    gen_helper_tr(cpu_env, l, o->addr1, o->in2);
    tcg_temp_free_i32(l);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_unpk(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_const_i32(get_field(s->fields, l1));
    potential_page_fault(s);
    gen_helper_unpk(cpu_env, l, o->addr1, o->in2);
    tcg_temp_free_i32(l);
    return NO_EXIT;
}

static ExitStatus op_xc(DisasContext *s, DisasOps *o)
{
    TCGv_i32 l = tcg_const_i32(get_field(s->fields, l1));
    potential_page_fault(s);
    gen_helper_xc(cc_op, cpu_env, l, o->addr1, o->in2);
    tcg_temp_free_i32(l);
    set_cc_static(s);
    return NO_EXIT;
}

static ExitStatus op_xor(DisasContext *s, DisasOps *o)
{
    tcg_gen_xor_i64(o->out, o->in1, o->in2);
    return NO_EXIT;
}

static ExitStatus op_xori(DisasContext *s, DisasOps *o)
{
    int shift = s->insn->data & 0xff;
    int size = s->insn->data >> 8;
    uint64_t mask = ((1ull << size) - 1) << shift;

    assert(!o->g_in2);
    tcg_gen_shli_i64(o->in2, o->in2, shift);
    tcg_gen_xor_i64(o->out, o->in1, o->in2);

    /* Produce the CC from only the bits manipulated.  */
    tcg_gen_andi_i64(cc_dst, o->out, mask);
    set_cc_nz_u64(s, cc_dst);
    return NO_EXIT;
}

/* ====================================================================== */
/* The "Cc OUTput" generators.  Given the generated output (and in some cases
   the original inputs), update the various cc data structures in order to
   be able to compute the new condition code.  */

static void cout_abs32(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_ABS_32, o->out);
}

static void cout_abs64(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_ABS_64, o->out);
}

static void cout_adds32(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_ADD_32, o->in1, o->in2, o->out);
}

static void cout_adds64(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_ADD_64, o->in1, o->in2, o->out);
}

static void cout_addu32(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_ADDU_32, o->in1, o->in2, o->out);
}

static void cout_addu64(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_ADDU_64, o->in1, o->in2, o->out);
}

static void cout_addc32(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_ADDC_32, o->in1, o->in2, o->out);
}

static void cout_addc64(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_ADDC_64, o->in1, o->in2, o->out);
}

static void cout_cmps32(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_LTGT_32, o->in1, o->in2);
}

static void cout_cmps64(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_LTGT_64, o->in1, o->in2);
}

static void cout_cmpu32(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_LTUGTU_32, o->in1, o->in2);
}

static void cout_cmpu64(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_LTUGTU_64, o->in1, o->in2);
}

static void cout_nabs32(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_NABS_32, o->out);
}

static void cout_nabs64(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_NABS_64, o->out);
}

static void cout_neg32(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_COMP_32, o->out);
}

static void cout_neg64(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_COMP_64, o->out);
}

static void cout_nz32(DisasContext *s, DisasOps *o)
{
    tcg_gen_ext32u_i64(cc_dst, o->out);
    gen_op_update1_cc_i64(s, CC_OP_NZ, cc_dst);
}

static void cout_nz64(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_NZ, o->out);
}

static void cout_s32(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_LTGT0_32, o->out);
}

static void cout_s64(DisasContext *s, DisasOps *o)
{
    gen_op_update1_cc_i64(s, CC_OP_LTGT0_64, o->out);
}

static void cout_subs32(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_SUB_32, o->in1, o->in2, o->out);
}

static void cout_subs64(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_SUB_64, o->in1, o->in2, o->out);
}

static void cout_subu32(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_SUBU_32, o->in1, o->in2, o->out);
}

static void cout_subu64(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_SUBU_64, o->in1, o->in2, o->out);
}

static void cout_subb32(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_SUBB_32, o->in1, o->in2, o->out);
}

static void cout_subb64(DisasContext *s, DisasOps *o)
{
    gen_op_update3_cc_i64(s, CC_OP_SUBB_64, o->in1, o->in2, o->out);
}

static void cout_tm32(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_TM_32, o->in1, o->in2);
}

static void cout_tm64(DisasContext *s, DisasOps *o)
{
    gen_op_update2_cc_i64(s, CC_OP_TM_64, o->in1, o->in2);
}

/* ====================================================================== */
/* The "PREPeration" generators.  These initialize the DisasOps.OUT fields
   with the TCG register to which we will write.  Used in combination with
   the "wout" generators, in some cases we need a new temporary, and in
   some cases we can write to a TCG global.  */

static void prep_new(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->out = tcg_temp_new_i64();
}

static void prep_new_P(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->out = tcg_temp_new_i64();
    o->out2 = tcg_temp_new_i64();
}

static void prep_r1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->out = regs[get_field(f, r1)];
    o->g_out = true;
}

static void prep_r1_P(DisasContext *s, DisasFields *f, DisasOps *o)
{
    /* ??? Specification exception: r1 must be even.  */
    int r1 = get_field(f, r1);
    o->out = regs[r1];
    o->out2 = regs[(r1 + 1) & 15];
    o->g_out = o->g_out2 = true;
}

/* ====================================================================== */
/* The "Write OUTput" generators.  These generally perform some non-trivial
   copy of data to TCG globals, or to main memory.  The trivial cases are
   generally handled by having a "prep" generator install the TCG global
   as the destination of the operation.  */

static void wout_r1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    store_reg(get_field(f, r1), o->out);
}

static void wout_r1_8(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int r1 = get_field(f, r1);
    tcg_gen_deposit_i64(regs[r1], regs[r1], o->out, 0, 8);
}

static void wout_r1_16(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int r1 = get_field(f, r1);
    tcg_gen_deposit_i64(regs[r1], regs[r1], o->out, 0, 16);
}

static void wout_r1_32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    store_reg32_i64(get_field(f, r1), o->out);
}

static void wout_r1_P32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    /* ??? Specification exception: r1 must be even.  */
    int r1 = get_field(f, r1);
    store_reg32_i64(r1, o->out);
    store_reg32_i64((r1 + 1) & 15, o->out2);
}

static void wout_r1_D32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    /* ??? Specification exception: r1 must be even.  */
    int r1 = get_field(f, r1);
    store_reg32_i64((r1 + 1) & 15, o->out);
    tcg_gen_shri_i64(o->out, o->out, 32);
    store_reg32_i64(r1, o->out);
}

static void wout_e1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    store_freg32_i64(get_field(f, r1), o->out);
}

static void wout_f1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    store_freg(get_field(f, r1), o->out);
}

static void wout_x1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int f1 = get_field(s->fields, r1);
    store_freg(f1, o->out);
    store_freg((f1 + 2) & 15, o->out2);
}

static void wout_cond_r1r2_32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    if (get_field(f, r1) != get_field(f, r2)) {
        store_reg32_i64(get_field(f, r1), o->out);
    }
}

static void wout_cond_e1e2(DisasContext *s, DisasFields *f, DisasOps *o)
{
    if (get_field(f, r1) != get_field(f, r2)) {
        store_freg32_i64(get_field(f, r1), o->out);
    }
}

static void wout_m1_8(DisasContext *s, DisasFields *f, DisasOps *o)
{
    tcg_gen_qemu_st8(o->out, o->addr1, get_mem_index(s));
}

static void wout_m1_16(DisasContext *s, DisasFields *f, DisasOps *o)
{
    tcg_gen_qemu_st16(o->out, o->addr1, get_mem_index(s));
}

static void wout_m1_32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    tcg_gen_qemu_st32(o->out, o->addr1, get_mem_index(s));
}

static void wout_m1_64(DisasContext *s, DisasFields *f, DisasOps *o)
{
    tcg_gen_qemu_st64(o->out, o->addr1, get_mem_index(s));
}

static void wout_m2_32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    tcg_gen_qemu_st32(o->out, o->in2, get_mem_index(s));
}

/* ====================================================================== */
/* The "INput 1" generators.  These load the first operand to an insn.  */

static void in1_r1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = load_reg(get_field(f, r1));
}

static void in1_r1_o(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = regs[get_field(f, r1)];
    o->g_in1 = true;
}

static void in1_r1_32s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(o->in1, regs[get_field(f, r1)]);
}

static void in1_r1_32u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in1, regs[get_field(f, r1)]);
}

static void in1_r1_sr32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_shri_i64(o->in1, regs[get_field(f, r1)], 32);
}

static void in1_r1p1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    /* ??? Specification exception: r1 must be even.  */
    int r1 = get_field(f, r1);
    o->in1 = load_reg((r1 + 1) & 15);
}

static void in1_r1p1_32s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    /* ??? Specification exception: r1 must be even.  */
    int r1 = get_field(f, r1);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(o->in1, regs[(r1 + 1) & 15]);
}

static void in1_r1p1_32u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    /* ??? Specification exception: r1 must be even.  */
    int r1 = get_field(f, r1);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in1, regs[(r1 + 1) & 15]);
}

static void in1_r1_D32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    /* ??? Specification exception: r1 must be even.  */
    int r1 = get_field(f, r1);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_concat32_i64(o->in1, regs[r1 + 1], regs[r1]);
}

static void in1_r2(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = load_reg(get_field(f, r2));
}

static void in1_r3(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = load_reg(get_field(f, r3));
}

static void in1_r3_o(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = regs[get_field(f, r3)];
    o->g_in1 = true;
}

static void in1_r3_32s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(o->in1, regs[get_field(f, r3)]);
}

static void in1_r3_32u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in1, regs[get_field(f, r3)]);
}

static void in1_e1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = load_freg32_i64(get_field(f, r1));
}

static void in1_f1_o(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in1 = fregs[get_field(f, r1)];
    o->g_in1 = true;
}

static void in1_la1(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->addr1 = get_address(s, 0, get_field(f, b1), get_field(f, d1));
}

static void in1_m1_8u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in1_la1(s, f, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld8u(o->in1, o->addr1, get_mem_index(s));
}

static void in1_m1_16s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in1_la1(s, f, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld16s(o->in1, o->addr1, get_mem_index(s));
}

static void in1_m1_16u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in1_la1(s, f, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld16u(o->in1, o->addr1, get_mem_index(s));
}

static void in1_m1_32s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in1_la1(s, f, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld32s(o->in1, o->addr1, get_mem_index(s));
}

static void in1_m1_32u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in1_la1(s, f, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld32u(o->in1, o->addr1, get_mem_index(s));
}

static void in1_m1_64(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in1_la1(s, f, o);
    o->in1 = tcg_temp_new_i64();
    tcg_gen_qemu_ld64(o->in1, o->addr1, get_mem_index(s));
}

/* ====================================================================== */
/* The "INput 2" generators.  These load the second operand to an insn.  */

static void in2_r2(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = load_reg(get_field(f, r2));
}

static void in2_r2_o(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = regs[get_field(f, r2)];
    o->g_in2 = true;
}

static void in2_r2_nz(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int r2 = get_field(f, r2);
    if (r2 != 0) {
        o->in2 = load_reg(r2);
    }
}

static void in2_r2_8s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext8s_i64(o->in2, regs[get_field(f, r2)]);
}

static void in2_r2_8u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext8u_i64(o->in2, regs[get_field(f, r2)]);
}

static void in2_r2_16s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext16s_i64(o->in2, regs[get_field(f, r2)]);
}

static void in2_r2_16u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext16u_i64(o->in2, regs[get_field(f, r2)]);
}

static void in2_r3(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = load_reg(get_field(f, r3));
}

static void in2_r2_32s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext32s_i64(o->in2, regs[get_field(f, r2)]);
}

static void in2_r2_32u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_temp_new_i64();
    tcg_gen_ext32u_i64(o->in2, regs[get_field(f, r2)]);
}

static void in2_e2(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = load_freg32_i64(get_field(f, r2));
}

static void in2_f2_o(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = fregs[get_field(f, r2)];
    o->g_in2 = true;
}

static void in2_x2_o(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int f2 = get_field(f, r2);
    o->in1 = fregs[f2];
    o->in2 = fregs[(f2 + 2) & 15];
    o->g_in1 = o->g_in2 = true;
}

static void in2_a2(DisasContext *s, DisasFields *f, DisasOps *o)
{
    int x2 = have_field(f, x2) ? get_field(f, x2) : 0;
    o->in2 = get_address(s, x2, get_field(f, b2), get_field(f, d2));
}

static void in2_ri2(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_const_i64(s->pc + (int64_t)get_field(f, i2) * 2);
}

static void in2_sh32(DisasContext *s, DisasFields *f, DisasOps *o)
{
    help_l2_shift(s, f, o, 31);
}

static void in2_sh64(DisasContext *s, DisasFields *f, DisasOps *o)
{
    help_l2_shift(s, f, o, 63);
}

static void in2_m2_8u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_a2(s, f, o);
    tcg_gen_qemu_ld8u(o->in2, o->in2, get_mem_index(s));
}

static void in2_m2_16s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_a2(s, f, o);
    tcg_gen_qemu_ld16s(o->in2, o->in2, get_mem_index(s));
}

static void in2_m2_16u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_a2(s, f, o);
    tcg_gen_qemu_ld16u(o->in2, o->in2, get_mem_index(s));
}

static void in2_m2_32s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_a2(s, f, o);
    tcg_gen_qemu_ld32s(o->in2, o->in2, get_mem_index(s));
}

static void in2_m2_32u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_a2(s, f, o);
    tcg_gen_qemu_ld32u(o->in2, o->in2, get_mem_index(s));
}

static void in2_m2_64(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_a2(s, f, o);
    tcg_gen_qemu_ld64(o->in2, o->in2, get_mem_index(s));
}

static void in2_mri2_16u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_ri2(s, f, o);
    tcg_gen_qemu_ld16u(o->in2, o->in2, get_mem_index(s));
}

static void in2_mri2_32s(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_ri2(s, f, o);
    tcg_gen_qemu_ld32s(o->in2, o->in2, get_mem_index(s));
}

static void in2_mri2_32u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_ri2(s, f, o);
    tcg_gen_qemu_ld32u(o->in2, o->in2, get_mem_index(s));
}

static void in2_mri2_64(DisasContext *s, DisasFields *f, DisasOps *o)
{
    in2_ri2(s, f, o);
    tcg_gen_qemu_ld64(o->in2, o->in2, get_mem_index(s));
}

static void in2_i2(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_const_i64(get_field(f, i2));
}

static void in2_i2_8u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_const_i64((uint8_t)get_field(f, i2));
}

static void in2_i2_16u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_const_i64((uint16_t)get_field(f, i2));
}

static void in2_i2_32u(DisasContext *s, DisasFields *f, DisasOps *o)
{
    o->in2 = tcg_const_i64((uint32_t)get_field(f, i2));
}

static void in2_i2_16u_shl(DisasContext *s, DisasFields *f, DisasOps *o)
{
    uint64_t i2 = (uint16_t)get_field(f, i2);
    o->in2 = tcg_const_i64(i2 << s->insn->data);
}

static void in2_i2_32u_shl(DisasContext *s, DisasFields *f, DisasOps *o)
{
    uint64_t i2 = (uint32_t)get_field(f, i2);
    o->in2 = tcg_const_i64(i2 << s->insn->data);
}

/* ====================================================================== */

/* Find opc within the table of insns.  This is formulated as a switch
   statement so that (1) we get compile-time notice of cut-paste errors
   for duplicated opcodes, and (2) the compiler generates the binary
   search tree, rather than us having to post-process the table.  */

#define C(OPC, NM, FT, FC, I1, I2, P, W, OP, CC) \
    D(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, 0)

#define D(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, D) insn_ ## NM,

enum DisasInsnEnum {
#include "insn-data.def"
};

#undef D
#define D(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, D) { \
    .opc = OPC,                           \
    .fmt = FMT_##FT,                      \
    .fac = FAC_##FC,                      \
    .name = #NM,                          \
    .help_in1 = in1_##I1,                 \
    .help_in2 = in2_##I2,                 \
    .help_prep = prep_##P,                \
    .help_wout = wout_##W,                \
    .help_cout = cout_##CC,               \
    .help_op = op_##OP,                   \
    .data = D                             \
 },

/* Allow 0 to be used for NULL in the table below.  */
#define in1_0  NULL
#define in2_0  NULL
#define prep_0  NULL
#define wout_0  NULL
#define cout_0  NULL
#define op_0  NULL

static const DisasInsn insn_info[] = {
#include "insn-data.def"
};

#undef D
#define D(OPC, NM, FT, FC, I1, I2, P, W, OP, CC, D) \
    case OPC: return &insn_info[insn_ ## NM];

static const DisasInsn *lookup_opc(uint16_t opc)
{
    switch (opc) {
#include "insn-data.def"
    default:
        return NULL;
    }
}

#undef D
#undef C

/* Extract a field from the insn.  The INSN should be left-aligned in
   the uint64_t so that we can more easily utilize the big-bit-endian
   definitions we extract from the Principals of Operation.  */

static void extract_field(DisasFields *o, const DisasField *f, uint64_t insn)
{
    uint32_t r, m;

    if (f->size == 0) {
        return;
    }

    /* Zero extract the field from the insn.  */
    r = (insn << f->beg) >> (64 - f->size);

    /* Sign-extend, or un-swap the field as necessary.  */
    switch (f->type) {
    case 0: /* unsigned */
        break;
    case 1: /* signed */
        assert(f->size <= 32);
        m = 1u << (f->size - 1);
        r = (r ^ m) - m;
        break;
    case 2: /* dl+dh split, signed 20 bit. */
        r = ((int8_t)r << 12) | (r >> 8);
        break;
    default:
        abort();
    }

    /* Validate that the "compressed" encoding we selected above is valid.
       I.e. we havn't make two different original fields overlap.  */
    assert(((o->presentC >> f->indexC) & 1) == 0);
    o->presentC |= 1 << f->indexC;
    o->presentO |= 1 << f->indexO;

    o->c[f->indexC] = r;
}

/* Lookup the insn at the current PC, extracting the operands into O and
   returning the info struct for the insn.  Returns NULL for invalid insn.  */

static const DisasInsn *extract_insn(CPUS390XState *env, DisasContext *s,
                                     DisasFields *f)
{
    uint64_t insn, pc = s->pc;
    int op, op2, ilen;
    const DisasInsn *info;

    insn = ld_code2(env, pc);
    op = (insn >> 8) & 0xff;
    ilen = get_ilen(op);
    s->next_pc = s->pc + ilen;

    switch (ilen) {
    case 2:
        insn = insn << 48;
        break;
    case 4:
        insn = ld_code4(env, pc) << 32;
        break;
    case 6:
        insn = (insn << 48) | (ld_code4(env, pc + 2) << 16);
        break;
    default:
        abort();
    }

    /* We can't actually determine the insn format until we've looked up
       the full insn opcode.  Which we can't do without locating the
       secondary opcode.  Assume by default that OP2 is at bit 40; for
       those smaller insns that don't actually have a secondary opcode
       this will correctly result in OP2 = 0. */
    switch (op) {
    case 0x01: /* E */
    case 0x80: /* S */
    case 0x82: /* S */
    case 0x93: /* S */
    case 0xb2: /* S, RRF, RRE */
    case 0xb3: /* RRE, RRD, RRF */
    case 0xb9: /* RRE, RRF */
    case 0xe5: /* SSE, SIL */
        op2 = (insn << 8) >> 56;
        break;
    case 0xa5: /* RI */
    case 0xa7: /* RI */
    case 0xc0: /* RIL */
    case 0xc2: /* RIL */
    case 0xc4: /* RIL */
    case 0xc6: /* RIL */
    case 0xc8: /* SSF */
    case 0xcc: /* RIL */
        op2 = (insn << 12) >> 60;
        break;
    case 0xd0 ... 0xdf: /* SS */
    case 0xe1: /* SS */
    case 0xe2: /* SS */
    case 0xe8: /* SS */
    case 0xe9: /* SS */
    case 0xea: /* SS */
    case 0xee ... 0xf3: /* SS */
    case 0xf8 ... 0xfd: /* SS */
        op2 = 0;
        break;
    default:
        op2 = (insn << 40) >> 56;
        break;
    }

    memset(f, 0, sizeof(*f));
    f->op = op;
    f->op2 = op2;

    /* Lookup the instruction.  */
    info = lookup_opc(op << 8 | op2);

    /* If we found it, extract the operands.  */
    if (info != NULL) {
        DisasFormat fmt = info->fmt;
        int i;

        for (i = 0; i < NUM_C_FIELD; ++i) {
            extract_field(f, &format_info[fmt].op[i], insn);
        }
    }
    return info;
}

static ExitStatus translate_one(CPUS390XState *env, DisasContext *s)
{
    const DisasInsn *insn;
    ExitStatus ret = NO_EXIT;
    DisasFields f;
    DisasOps o;

    insn = extract_insn(env, s, &f);

    /* If not found, try the old interpreter.  This includes ILLOPC.  */
    if (insn == NULL) {
        disas_s390_insn(env, s);
        switch (s->is_jmp) {
        case DISAS_NEXT:
            ret = NO_EXIT;
            break;
        case DISAS_TB_JUMP:
            ret = EXIT_GOTO_TB;
            break;
        case DISAS_JUMP:
            ret = EXIT_PC_UPDATED;
            break;
        case DISAS_EXCP:
            ret = EXIT_NORETURN;
            break;
        default:
            abort();
        }

        s->pc = s->next_pc;
        return ret;
    }

    /* Set up the strutures we use to communicate with the helpers. */
    s->insn = insn;
    s->fields = &f;
    o.g_out = o.g_out2 = o.g_in1 = o.g_in2 = false;
    TCGV_UNUSED_I64(o.out);
    TCGV_UNUSED_I64(o.out2);
    TCGV_UNUSED_I64(o.in1);
    TCGV_UNUSED_I64(o.in2);
    TCGV_UNUSED_I64(o.addr1);

    /* Implement the instruction.  */
    if (insn->help_in1) {
        insn->help_in1(s, &f, &o);
    }
    if (insn->help_in2) {
        insn->help_in2(s, &f, &o);
    }
    if (insn->help_prep) {
        insn->help_prep(s, &f, &o);
    }
    if (insn->help_op) {
        ret = insn->help_op(s, &o);
    }
    if (insn->help_wout) {
        insn->help_wout(s, &f, &o);
    }
    if (insn->help_cout) {
        insn->help_cout(s, &o);
    }

    /* Free any temporaries created by the helpers.  */
    if (!TCGV_IS_UNUSED_I64(o.out) && !o.g_out) {
        tcg_temp_free_i64(o.out);
    }
    if (!TCGV_IS_UNUSED_I64(o.out2) && !o.g_out2) {
        tcg_temp_free_i64(o.out2);
    }
    if (!TCGV_IS_UNUSED_I64(o.in1) && !o.g_in1) {
        tcg_temp_free_i64(o.in1);
    }
    if (!TCGV_IS_UNUSED_I64(o.in2) && !o.g_in2) {
        tcg_temp_free_i64(o.in2);
    }
    if (!TCGV_IS_UNUSED_I64(o.addr1)) {
        tcg_temp_free_i64(o.addr1);
    }

    /* Advance to the next instruction.  */
    s->pc = s->next_pc;
    return ret;
}

static inline void gen_intermediate_code_internal(CPUS390XState *env,
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
    ExitStatus status;
    bool do_debug;

    pc_start = tb->pc;

    /* 31-bit mode */
    if (!(tb->flags & FLAG_MASK_64)) {
        pc_start &= 0x7fffffff;
    }

    dc.tb = tb;
    dc.pc = pc_start;
    dc.cc_op = CC_OP_DYNAMIC;
    do_debug = dc.singlestep_enabled = env->singlestep_enabled;
    dc.is_jmp = DISAS_NEXT;

    gen_opc_end = tcg_ctx.gen_opc_buf + OPC_MAX_SIZE;

    next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;

    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }

    gen_icount_start();

    do {
        if (search_pc) {
            j = tcg_ctx.gen_opc_ptr - tcg_ctx.gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j) {
                    tcg_ctx.gen_opc_instr_start[lj++] = 0;
                }
            }
            tcg_ctx.gen_opc_pc[lj] = dc.pc;
            gen_opc_cc_op[lj] = dc.cc_op;
            tcg_ctx.gen_opc_instr_start[lj] = 1;
            tcg_ctx.gen_opc_icount[lj] = num_insns;
        }
        if (++num_insns == max_insns && (tb->cflags & CF_LAST_IO)) {
            gen_io_start();
        }

        if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP | CPU_LOG_TB_OP_OPT))) {
            tcg_gen_debug_insn_start(dc.pc);
        }

        status = NO_EXIT;
        if (unlikely(!QTAILQ_EMPTY(&env->breakpoints))) {
            QTAILQ_FOREACH(bp, &env->breakpoints, entry) {
                if (bp->pc == dc.pc) {
                    status = EXIT_PC_STALE;
                    do_debug = true;
                    break;
                }
            }
        }
        if (status == NO_EXIT) {
            status = translate_one(env, &dc);
        }

        /* If we reach a page boundary, are single stepping,
           or exhaust instruction count, stop generation.  */
        if (status == NO_EXIT
            && (dc.pc >= next_page_start
                || tcg_ctx.gen_opc_ptr >= gen_opc_end
                || num_insns >= max_insns
                || singlestep
                || env->singlestep_enabled)) {
            status = EXIT_PC_STALE;
        }
    } while (status == NO_EXIT);

    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }

    switch (status) {
    case EXIT_GOTO_TB:
    case EXIT_NORETURN:
        break;
    case EXIT_PC_STALE:
        update_psw_addr(&dc);
        /* FALLTHRU */
    case EXIT_PC_UPDATED:
        if (singlestep && dc.cc_op != CC_OP_DYNAMIC) {
            gen_op_calc_cc(&dc);
        } else {
            /* Next TB starts off with CC_OP_DYNAMIC,
               so make sure the cc op type is in env */
            gen_op_set_cc_op(&dc);
        }
        if (do_debug) {
            gen_exception(EXCP_DEBUG);
        } else {
            /* Generate the return instruction */
            tcg_gen_exit_tb(0);
        }
        break;
    default:
        abort();
    }

    gen_icount_end(tb, num_insns);
    *tcg_ctx.gen_opc_ptr = INDEX_op_end;
    if (search_pc) {
        j = tcg_ctx.gen_opc_ptr - tcg_ctx.gen_opc_buf;
        lj++;
        while (lj <= j) {
            tcg_ctx.gen_opc_instr_start[lj++] = 0;
        }
    } else {
        tb->size = dc.pc - pc_start;
        tb->icount = num_insns;
    }

#if defined(S390X_DEBUG_DISAS)
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("IN: %s\n", lookup_symbol(pc_start));
        log_target_disas(env, pc_start, dc.pc - pc_start, 1);
        qemu_log("\n");
    }
#endif
}

void gen_intermediate_code (CPUS390XState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 0);
}

void gen_intermediate_code_pc (CPUS390XState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 1);
}

void restore_state_to_opc(CPUS390XState *env, TranslationBlock *tb, int pc_pos)
{
    int cc_op;
    env->psw.addr = tcg_ctx.gen_opc_pc[pc_pos];
    cc_op = gen_opc_cc_op[pc_pos];
    if ((cc_op != CC_OP_DYNAMIC) && (cc_op != CC_OP_STATIC)) {
        env->cc_op = cc_op;
    }
}
