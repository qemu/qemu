/*
 * RISC-V emulation for qemu: main translation routines.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
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

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "tcg/tcg-op.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "exec/translator.h"
#include "exec/translation-block.h"
#include "exec/log.h"
#include "semihosting/semihost.h"

#include "internals.h"

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H

#include "tcg/tcg-cpu.h"

/* global register indices */
static TCGv cpu_gpr[32], cpu_gprh[32], cpu_pc, cpu_vl, cpu_vstart;
static TCGv_i64 cpu_fpr[32]; /* assume F and D extensions */
static TCGv load_res;
static TCGv load_val;

/*
 * If an operation is being performed on less than TARGET_LONG_BITS,
 * it may require the inputs to be sign- or zero-extended; which will
 * depend on the exact operation being performed.
 */
typedef enum {
    EXT_NONE,
    EXT_SIGN,
    EXT_ZERO,
} DisasExtend;

typedef struct DisasContext {
    DisasContextBase base;
    target_ulong cur_insn_len;
    target_ulong pc_save;
    target_ulong priv_ver;
    RISCVMXL misa_mxl_max;
    RISCVMXL xl;
    RISCVMXL address_xl;
    uint32_t misa_ext;
    uint32_t opcode;
    RISCVExtStatus mstatus_fs;
    RISCVExtStatus mstatus_vs;
    uint32_t mem_idx;
    uint32_t priv;
    /*
     * Remember the rounding mode encoded in the previous fp instruction,
     * which we have already installed into env->fp_status.  Or -1 for
     * no previous fp instruction.  Note that we exit the TB when writing
     * to any system register, which includes CSR_FRM, so we do not have
     * to reset this known value.
     */
    int frm;
    RISCVMXL ol;
    bool virt_inst_excp;
    bool virt_enabled;
    const RISCVCPUConfig *cfg_ptr;
    /* vector extension */
    bool vill;
    /*
     * Encode LMUL to lmul as follows:
     *     LMUL    vlmul    lmul
     *      1       000       0
     *      2       001       1
     *      4       010       2
     *      8       011       3
     *      -       100       -
     *     1/8      101      -3
     *     1/4      110      -2
     *     1/2      111      -1
     */
    int8_t lmul;
    uint8_t sew;
    uint8_t vta;
    uint8_t vma;
    bool cfg_vta_all_1s;
    bool vstart_eq_zero;
    bool vl_eq_vlmax;
    CPUState *cs;
    TCGv zero;
    /* actual address width */
    uint8_t addr_xl;
    bool addr_signed;
    /* Ztso */
    bool ztso;
    /* Use icount trigger for native debug */
    bool itrigger;
    /* FRM is known to contain a valid value. */
    bool frm_valid;
    bool insn_start_updated;
    const GPtrArray *decoders;
    /* zicfilp extension. fcfi_enabled, lp expected or not */
    bool fcfi_enabled;
    bool fcfi_lp_expected;
    /* zicfiss extension, if shadow stack was enabled during TB gen */
    bool bcfi_enabled;
} DisasContext;

static inline bool has_ext(DisasContext *ctx, uint32_t ext)
{
    return ctx->misa_ext & ext;
}

#ifdef TARGET_RISCV32
#define get_xl(ctx)    MXL_RV32
#elif defined(CONFIG_USER_ONLY)
#define get_xl(ctx)    MXL_RV64
#else
#define get_xl(ctx)    ((ctx)->xl)
#endif

#ifdef TARGET_RISCV32
#define get_address_xl(ctx)    MXL_RV32
#elif defined(CONFIG_USER_ONLY)
#define get_address_xl(ctx)    MXL_RV64
#else
#define get_address_xl(ctx)    ((ctx)->address_xl)
#endif

#define mxl_memop(ctx) ((get_xl(ctx) + 1) | MO_TE)

/* The word size for this machine mode. */
static inline int __attribute__((unused)) get_xlen(DisasContext *ctx)
{
    return 16 << get_xl(ctx);
}

/* The operation length, as opposed to the xlen. */
#ifdef TARGET_RISCV32
#define get_ol(ctx)    MXL_RV32
#else
#define get_ol(ctx)    ((ctx)->ol)
#endif

static inline int get_olen(DisasContext *ctx)
{
    return 16 << get_ol(ctx);
}

/* The maximum register length */
#ifdef TARGET_RISCV32
#define get_xl_max(ctx)    MXL_RV32
#else
#define get_xl_max(ctx)    ((ctx)->misa_mxl_max)
#endif

/*
 * RISC-V requires NaN-boxing of narrower width floating point values.
 * This applies when a 32-bit value is assigned to a 64-bit FP register.
 * For consistency and simplicity, we nanbox results even when the RVD
 * extension is not present.
 */
static void gen_nanbox_s(TCGv_i64 out, TCGv_i64 in)
{
    tcg_gen_ori_i64(out, in, MAKE_64BIT_MASK(32, 32));
}

static void gen_nanbox_h(TCGv_i64 out, TCGv_i64 in)
{
    tcg_gen_ori_i64(out, in, MAKE_64BIT_MASK(16, 48));
}

/*
 * A narrow n-bit operation, where n < FLEN, checks that input operands
 * are correctly Nan-boxed, i.e., all upper FLEN - n bits are 1.
 * If so, the least-significant bits of the input are used, otherwise the
 * input value is treated as an n-bit canonical NaN (v2.2 section 9.2).
 *
 * Here, the result is always nan-boxed, even the canonical nan.
 */
static void gen_check_nanbox_h(TCGv_i64 out, TCGv_i64 in)
{
    TCGv_i64 t_max = tcg_constant_i64(0xffffffffffff0000ull);
    TCGv_i64 t_nan = tcg_constant_i64(0xffffffffffff7e00ull);

    tcg_gen_movcond_i64(TCG_COND_GEU, out, in, t_max, in, t_nan);
}

static void gen_check_nanbox_s(TCGv_i64 out, TCGv_i64 in)
{
    TCGv_i64 t_max = tcg_constant_i64(0xffffffff00000000ull);
    TCGv_i64 t_nan = tcg_constant_i64(0xffffffff7fc00000ull);

    tcg_gen_movcond_i64(TCG_COND_GEU, out, in, t_max, in, t_nan);
}

static void decode_save_opc(DisasContext *ctx, target_ulong excp_uw2)
{
    assert(!ctx->insn_start_updated);
    ctx->insn_start_updated = true;
    tcg_set_insn_start_param(ctx->base.insn_start, 1, ctx->opcode);
    tcg_set_insn_start_param(ctx->base.insn_start, 2, excp_uw2);
}

static void gen_pc_plus_diff(TCGv target, DisasContext *ctx,
                             target_long diff)
{
    target_ulong dest = ctx->base.pc_next + diff;

    assert(ctx->pc_save != -1);
    if (tb_cflags(ctx->base.tb) & CF_PCREL) {
        tcg_gen_addi_tl(target, cpu_pc, dest - ctx->pc_save);
        if (get_xl(ctx) == MXL_RV32) {
            tcg_gen_ext32s_tl(target, target);
        }
    } else {
        if (get_xl(ctx) == MXL_RV32) {
            dest = (int32_t)dest;
        }
        tcg_gen_movi_tl(target, dest);
    }
}

static void gen_update_pc(DisasContext *ctx, target_long diff)
{
    gen_pc_plus_diff(cpu_pc, ctx, diff);
    ctx->pc_save = ctx->base.pc_next + diff;
}

static void generate_exception(DisasContext *ctx, RISCVException excp)
{
    gen_update_pc(ctx, 0);
    gen_helper_raise_exception(tcg_env, tcg_constant_i32(excp));
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception_illegal(DisasContext *ctx)
{
    tcg_gen_st_i32(tcg_constant_i32(ctx->opcode), tcg_env,
                   offsetof(CPURISCVState, bins));
    if (ctx->virt_inst_excp) {
        generate_exception(ctx, RISCV_EXCP_VIRT_INSTRUCTION_FAULT);
    } else {
        generate_exception(ctx, RISCV_EXCP_ILLEGAL_INST);
    }
}

static void gen_exception_inst_addr_mis(DisasContext *ctx, TCGv target)
{
    tcg_gen_st_tl(target, tcg_env, offsetof(CPURISCVState, badaddr));
    generate_exception(ctx, RISCV_EXCP_INST_ADDR_MIS);
}

static void lookup_and_goto_ptr(DisasContext *ctx)
{
#ifndef CONFIG_USER_ONLY
    if (ctx->itrigger) {
        gen_helper_itrigger_match(tcg_env);
    }
#endif
    tcg_gen_lookup_and_goto_ptr();
}

static void exit_tb(DisasContext *ctx)
{
#ifndef CONFIG_USER_ONLY
    if (ctx->itrigger) {
        gen_helper_itrigger_match(tcg_env);
    }
#endif
    tcg_gen_exit_tb(NULL, 0);
}

static void gen_goto_tb(DisasContext *ctx, int n, target_long diff)
{
    target_ulong dest = ctx->base.pc_next + diff;

     /*
      * Under itrigger, instruction executes one by one like singlestep,
      * direct block chain benefits will be small.
      */
    if (translator_use_goto_tb(&ctx->base, dest) && !ctx->itrigger) {
        /*
         * For pcrel, the pc must always be up-to-date on entry to
         * the linked TB, so that it can use simple additions for all
         * further adjustments.  For !pcrel, the linked TB is compiled
         * to know its full virtual address, so we can delay the
         * update to pc to the unlinked path.  A long chain of links
         * can thus avoid many updates to the PC.
         */
        if (tb_cflags(ctx->base.tb) & CF_PCREL) {
            gen_update_pc(ctx, diff);
            tcg_gen_goto_tb(n);
        } else {
            tcg_gen_goto_tb(n);
            gen_update_pc(ctx, diff);
        }
        tcg_gen_exit_tb(ctx->base.tb, n);
    } else {
        gen_update_pc(ctx, diff);
        lookup_and_goto_ptr(ctx);
    }
}

/*
 * Wrappers for getting reg values.
 *
 * The $zero register does not have cpu_gpr[0] allocated -- we supply the
 * constant zero as a source, and an uninitialized sink as destination.
 *
 * Further, we may provide an extension for word operations.
 */
static TCGv get_gpr(DisasContext *ctx, int reg_num, DisasExtend ext)
{
    TCGv t;

    if (reg_num == 0) {
        return ctx->zero;
    }

    switch (get_ol(ctx)) {
    case MXL_RV32:
        switch (ext) {
        case EXT_NONE:
            break;
        case EXT_SIGN:
            t = tcg_temp_new();
            tcg_gen_ext32s_tl(t, cpu_gpr[reg_num]);
            return t;
        case EXT_ZERO:
            t = tcg_temp_new();
            tcg_gen_ext32u_tl(t, cpu_gpr[reg_num]);
            return t;
        default:
            g_assert_not_reached();
        }
        break;
    case MXL_RV64:
    case MXL_RV128:
        break;
    default:
        g_assert_not_reached();
    }
    return cpu_gpr[reg_num];
}

static TCGv get_gprh(DisasContext *ctx, int reg_num)
{
    assert(get_xl(ctx) == MXL_RV128);
    if (reg_num == 0) {
        return ctx->zero;
    }
    return cpu_gprh[reg_num];
}

static TCGv dest_gpr(DisasContext *ctx, int reg_num)
{
    if (reg_num == 0 || get_olen(ctx) < TARGET_LONG_BITS) {
        return tcg_temp_new();
    }
    return cpu_gpr[reg_num];
}

static TCGv dest_gprh(DisasContext *ctx, int reg_num)
{
    if (reg_num == 0) {
        return tcg_temp_new();
    }
    return cpu_gprh[reg_num];
}

static void gen_set_gpr(DisasContext *ctx, int reg_num, TCGv t)
{
    if (reg_num != 0) {
        switch (get_ol(ctx)) {
        case MXL_RV32:
            tcg_gen_ext32s_tl(cpu_gpr[reg_num], t);
            break;
        case MXL_RV64:
        case MXL_RV128:
            tcg_gen_mov_tl(cpu_gpr[reg_num], t);
            break;
        default:
            g_assert_not_reached();
        }

        if (get_xl_max(ctx) == MXL_RV128) {
            tcg_gen_sari_tl(cpu_gprh[reg_num], cpu_gpr[reg_num], 63);
        }
    }
}

static void gen_set_gpri(DisasContext *ctx, int reg_num, target_long imm)
{
    if (reg_num != 0) {
        switch (get_ol(ctx)) {
        case MXL_RV32:
            tcg_gen_movi_tl(cpu_gpr[reg_num], (int32_t)imm);
            break;
        case MXL_RV64:
        case MXL_RV128:
            tcg_gen_movi_tl(cpu_gpr[reg_num], imm);
            break;
        default:
            g_assert_not_reached();
        }

        if (get_xl_max(ctx) == MXL_RV128) {
            tcg_gen_movi_tl(cpu_gprh[reg_num], -(imm < 0));
        }
    }
}

static void gen_set_gpr128(DisasContext *ctx, int reg_num, TCGv rl, TCGv rh)
{
    assert(get_ol(ctx) == MXL_RV128);
    if (reg_num != 0) {
        tcg_gen_mov_tl(cpu_gpr[reg_num], rl);
        tcg_gen_mov_tl(cpu_gprh[reg_num], rh);
    }
}

static TCGv_i64 get_fpr_hs(DisasContext *ctx, int reg_num)
{
    if (!ctx->cfg_ptr->ext_zfinx) {
        return cpu_fpr[reg_num];
    }

    if (reg_num == 0) {
        return tcg_constant_i64(0);
    }
    switch (get_xl(ctx)) {
    case MXL_RV32:
#ifdef TARGET_RISCV32
    {
        TCGv_i64 t = tcg_temp_new_i64();
        tcg_gen_ext_i32_i64(t, cpu_gpr[reg_num]);
        return t;
    }
#else
    /* fall through */
    case MXL_RV64:
        return cpu_gpr[reg_num];
#endif
    default:
        g_assert_not_reached();
    }
}

static TCGv_i64 get_fpr_d(DisasContext *ctx, int reg_num)
{
    if (!ctx->cfg_ptr->ext_zfinx) {
        return cpu_fpr[reg_num];
    }

    if (reg_num == 0) {
        return tcg_constant_i64(0);
    }
    switch (get_xl(ctx)) {
    case MXL_RV32:
    {
        TCGv_i64 t = tcg_temp_new_i64();
        tcg_gen_concat_tl_i64(t, cpu_gpr[reg_num], cpu_gpr[reg_num + 1]);
        return t;
    }
#ifdef TARGET_RISCV64
    case MXL_RV64:
        return cpu_gpr[reg_num];
#endif
    default:
        g_assert_not_reached();
    }
}

static TCGv_i64 dest_fpr(DisasContext *ctx, int reg_num)
{
    if (!ctx->cfg_ptr->ext_zfinx) {
        return cpu_fpr[reg_num];
    }

    if (reg_num == 0) {
        return tcg_temp_new_i64();
    }

    switch (get_xl(ctx)) {
    case MXL_RV32:
        return tcg_temp_new_i64();
#ifdef TARGET_RISCV64
    case MXL_RV64:
        return cpu_gpr[reg_num];
#endif
    default:
        g_assert_not_reached();
    }
}

/* assume it is nanboxing (for normal) or sign-extended (for zfinx) */
static void gen_set_fpr_hs(DisasContext *ctx, int reg_num, TCGv_i64 t)
{
    if (!ctx->cfg_ptr->ext_zfinx) {
        tcg_gen_mov_i64(cpu_fpr[reg_num], t);
        return;
    }
    if (reg_num != 0) {
        switch (get_xl(ctx)) {
        case MXL_RV32:
#ifdef TARGET_RISCV32
            tcg_gen_extrl_i64_i32(cpu_gpr[reg_num], t);
            break;
#else
        /* fall through */
        case MXL_RV64:
            tcg_gen_mov_i64(cpu_gpr[reg_num], t);
            break;
#endif
        default:
            g_assert_not_reached();
        }
    }
}

static void gen_set_fpr_d(DisasContext *ctx, int reg_num, TCGv_i64 t)
{
    if (!ctx->cfg_ptr->ext_zfinx) {
        tcg_gen_mov_i64(cpu_fpr[reg_num], t);
        return;
    }

    if (reg_num != 0) {
        switch (get_xl(ctx)) {
        case MXL_RV32:
#ifdef TARGET_RISCV32
            tcg_gen_extr_i64_i32(cpu_gpr[reg_num], cpu_gpr[reg_num + 1], t);
            break;
#else
            tcg_gen_ext32s_i64(cpu_gpr[reg_num], t);
            tcg_gen_sari_i64(cpu_gpr[reg_num + 1], t, 32);
            break;
        case MXL_RV64:
            tcg_gen_mov_i64(cpu_gpr[reg_num], t);
            break;
#endif
        default:
            g_assert_not_reached();
        }
    }
}

static void gen_jal(DisasContext *ctx, int rd, target_ulong imm)
{
    TCGv succ_pc = dest_gpr(ctx, rd);

    /* check misaligned: */
    if (!has_ext(ctx, RVC) && !ctx->cfg_ptr->ext_zca) {
        if ((imm & 0x3) != 0) {
            TCGv target_pc = tcg_temp_new();
            gen_pc_plus_diff(target_pc, ctx, imm);
            gen_exception_inst_addr_mis(ctx, target_pc);
            return;
        }
    }

    gen_pc_plus_diff(succ_pc, ctx, ctx->cur_insn_len);
    gen_set_gpr(ctx, rd, succ_pc);

    gen_goto_tb(ctx, 0, imm); /* must use this for safety */
    ctx->base.is_jmp = DISAS_NORETURN;
}

/* Compute a canonical address from a register plus offset. */
static TCGv get_address(DisasContext *ctx, int rs1, int imm)
{
    TCGv addr = tcg_temp_new();
    TCGv src1 = get_gpr(ctx, rs1, EXT_NONE);

    tcg_gen_addi_tl(addr, src1, imm);
    if (ctx->addr_signed) {
        tcg_gen_sextract_tl(addr, addr, 0, ctx->addr_xl);
    } else {
        tcg_gen_extract_tl(addr, addr, 0, ctx->addr_xl);
    }

    return addr;
}

/* Compute a canonical address from a register plus reg offset. */
static TCGv get_address_indexed(DisasContext *ctx, int rs1, TCGv offs)
{
    TCGv addr = tcg_temp_new();
    TCGv src1 = get_gpr(ctx, rs1, EXT_NONE);

    tcg_gen_add_tl(addr, src1, offs);
    if (ctx->addr_signed) {
        tcg_gen_sextract_tl(addr, addr, 0, ctx->addr_xl);
    } else {
        tcg_gen_extract_tl(addr, addr, 0, ctx->addr_xl);
    }

    return addr;
}

#ifndef CONFIG_USER_ONLY
/*
 * We will have already diagnosed disabled state,
 * and need to turn initial/clean into dirty.
 */
static void mark_fs_dirty(DisasContext *ctx)
{
    TCGv tmp;

    if (!has_ext(ctx, RVF)) {
        return;
    }

    if (ctx->mstatus_fs != EXT_STATUS_DIRTY) {
        /* Remember the state change for the rest of the TB. */
        ctx->mstatus_fs = EXT_STATUS_DIRTY;

        tmp = tcg_temp_new();
        tcg_gen_ld_tl(tmp, tcg_env, offsetof(CPURISCVState, mstatus));
        tcg_gen_ori_tl(tmp, tmp, MSTATUS_FS);
        tcg_gen_st_tl(tmp, tcg_env, offsetof(CPURISCVState, mstatus));

        if (ctx->virt_enabled) {
            tcg_gen_ld_tl(tmp, tcg_env, offsetof(CPURISCVState, mstatus_hs));
            tcg_gen_ori_tl(tmp, tmp, MSTATUS_FS);
            tcg_gen_st_tl(tmp, tcg_env, offsetof(CPURISCVState, mstatus_hs));
        }
    }
}
#else
static inline void mark_fs_dirty(DisasContext *ctx) { }
#endif

#ifndef CONFIG_USER_ONLY
/*
 * We will have already diagnosed disabled state,
 * and need to turn initial/clean into dirty.
 */
static void mark_vs_dirty(DisasContext *ctx)
{
    TCGv tmp;

    if (ctx->mstatus_vs != EXT_STATUS_DIRTY) {
        /* Remember the state change for the rest of the TB.  */
        ctx->mstatus_vs = EXT_STATUS_DIRTY;

        tmp = tcg_temp_new();
        tcg_gen_ld_tl(tmp, tcg_env, offsetof(CPURISCVState, mstatus));
        tcg_gen_ori_tl(tmp, tmp, MSTATUS_VS);
        tcg_gen_st_tl(tmp, tcg_env, offsetof(CPURISCVState, mstatus));

        if (ctx->virt_enabled) {
            tcg_gen_ld_tl(tmp, tcg_env, offsetof(CPURISCVState, mstatus_hs));
            tcg_gen_ori_tl(tmp, tmp, MSTATUS_VS);
            tcg_gen_st_tl(tmp, tcg_env, offsetof(CPURISCVState, mstatus_hs));
        }
    }
}
#else
static inline void mark_vs_dirty(DisasContext *ctx) { }
#endif

static void finalize_rvv_inst(DisasContext *ctx)
{
    mark_vs_dirty(ctx);
    ctx->vstart_eq_zero = true;
}

static void gen_set_rm(DisasContext *ctx, int rm)
{
    if (ctx->frm == rm) {
        return;
    }
    ctx->frm = rm;

    if (rm == RISCV_FRM_DYN) {
        /* The helper will return only if frm valid. */
        ctx->frm_valid = true;
    }

    /* The helper may raise ILLEGAL_INSN -- record binv for unwind. */
    decode_save_opc(ctx, 0);
    gen_helper_set_rounding_mode(tcg_env, tcg_constant_i32(rm));
}

static void gen_set_rm_chkfrm(DisasContext *ctx, int rm)
{
    if (ctx->frm == rm && ctx->frm_valid) {
        return;
    }
    ctx->frm = rm;
    ctx->frm_valid = true;

    /* The helper may raise ILLEGAL_INSN -- record binv for unwind. */
    decode_save_opc(ctx, 0);
    gen_helper_set_rounding_mode_chkfrm(tcg_env, tcg_constant_i32(rm));
}

static int ex_plus_1(DisasContext *ctx, int nf)
{
    return nf + 1;
}

#define EX_SH(amount) \
    static int ex_shift_##amount(DisasContext *ctx, int imm) \
    {                                         \
        return imm << amount;                 \
    }
EX_SH(1)
EX_SH(2)
EX_SH(3)
EX_SH(4)
EX_SH(12)

#define REQUIRE_EXT(ctx, ext) do { \
    if (!has_ext(ctx, ext)) {      \
        return false;              \
    }                              \
} while (0)

#define REQUIRE_32BIT(ctx) do {    \
    if (get_xl(ctx) != MXL_RV32) { \
        return false;              \
    }                              \
} while (0)

#define REQUIRE_64BIT(ctx) do {     \
    if (get_xl(ctx) != MXL_RV64) {  \
        return false;               \
    }                               \
} while (0)

#define REQUIRE_128BIT(ctx) do {    \
    if (get_xl(ctx) != MXL_RV128) { \
        return false;               \
    }                               \
} while (0)

#define REQUIRE_64_OR_128BIT(ctx) do { \
    if (get_xl(ctx) == MXL_RV32) {     \
        return false;                  \
    }                                  \
} while (0)

#define REQUIRE_EITHER_EXT(ctx, A, B) do {       \
    if (!ctx->cfg_ptr->ext_##A &&                \
        !ctx->cfg_ptr->ext_##B) {                \
        return false;                            \
    }                                            \
} while (0)

static int ex_rvc_register(DisasContext *ctx, int reg)
{
    return 8 + reg;
}

static int ex_sreg_register(DisasContext *ctx, int reg)
{
    return reg < 2 ? reg + 8 : reg + 16;
}

static int ex_rvc_shiftli(DisasContext *ctx, int imm)
{
    /* For RV128 a shamt of 0 means a shift by 64. */
    if (get_ol(ctx) == MXL_RV128) {
        imm = imm ? imm : 64;
    }
    return imm;
}

static int ex_rvc_shiftri(DisasContext *ctx, int imm)
{
    /*
     * For RV128 a shamt of 0 means a shift by 64, furthermore, for right
     * shifts, the shamt is sign-extended.
     */
    if (get_ol(ctx) == MXL_RV128) {
        imm = imm | (imm & 32) << 1;
        imm = imm ? imm : 64;
    }
    return imm;
}

/* Include the auto-generated decoder for 32 bit insn */
#include "decode-insn32.c.inc"

static bool gen_logic_imm_fn(DisasContext *ctx, arg_i *a,
                             void (*func)(TCGv, TCGv, target_long))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);

    func(dest, src1, a->imm);

    if (get_xl(ctx) == MXL_RV128) {
        TCGv src1h = get_gprh(ctx, a->rs1);
        TCGv desth = dest_gprh(ctx, a->rd);

        func(desth, src1h, -(a->imm < 0));
        gen_set_gpr128(ctx, a->rd, dest, desth);
    } else {
        gen_set_gpr(ctx, a->rd, dest);
    }

    return true;
}

static bool gen_logic(DisasContext *ctx, arg_r *a,
                      void (*func)(TCGv, TCGv, TCGv))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, EXT_NONE);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    func(dest, src1, src2);

    if (get_xl(ctx) == MXL_RV128) {
        TCGv src1h = get_gprh(ctx, a->rs1);
        TCGv src2h = get_gprh(ctx, a->rs2);
        TCGv desth = dest_gprh(ctx, a->rd);

        func(desth, src1h, src2h);
        gen_set_gpr128(ctx, a->rd, dest, desth);
    } else {
        gen_set_gpr(ctx, a->rd, dest);
    }

    return true;
}

static bool gen_arith_imm_fn(DisasContext *ctx, arg_i *a, DisasExtend ext,
                             void (*func)(TCGv, TCGv, target_long),
                             void (*f128)(TCGv, TCGv, TCGv, TCGv, target_long))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);

    if (get_ol(ctx) < MXL_RV128) {
        func(dest, src1, a->imm);
        gen_set_gpr(ctx, a->rd, dest);
    } else {
        if (f128 == NULL) {
            return false;
        }

        TCGv src1h = get_gprh(ctx, a->rs1);
        TCGv desth = dest_gprh(ctx, a->rd);

        f128(dest, desth, src1, src1h, a->imm);
        gen_set_gpr128(ctx, a->rd, dest, desth);
    }
    return true;
}

static bool gen_arith_imm_tl(DisasContext *ctx, arg_i *a, DisasExtend ext,
                             void (*func)(TCGv, TCGv, TCGv),
                             void (*f128)(TCGv, TCGv, TCGv, TCGv, TCGv, TCGv))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);
    TCGv src2 = tcg_constant_tl(a->imm);

    if (get_ol(ctx) < MXL_RV128) {
        func(dest, src1, src2);
        gen_set_gpr(ctx, a->rd, dest);
    } else {
        if (f128 == NULL) {
            return false;
        }

        TCGv src1h = get_gprh(ctx, a->rs1);
        TCGv src2h = tcg_constant_tl(-(a->imm < 0));
        TCGv desth = dest_gprh(ctx, a->rd);

        f128(dest, desth, src1, src1h, src2, src2h);
        gen_set_gpr128(ctx, a->rd, dest, desth);
    }
    return true;
}

static bool gen_arith(DisasContext *ctx, arg_r *a, DisasExtend ext,
                      void (*func)(TCGv, TCGv, TCGv),
                      void (*f128)(TCGv, TCGv, TCGv, TCGv, TCGv, TCGv))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);
    TCGv src2 = get_gpr(ctx, a->rs2, ext);

    if (get_ol(ctx) < MXL_RV128) {
        func(dest, src1, src2);
        gen_set_gpr(ctx, a->rd, dest);
    } else {
        if (f128 == NULL) {
            return false;
        }

        TCGv src1h = get_gprh(ctx, a->rs1);
        TCGv src2h = get_gprh(ctx, a->rs2);
        TCGv desth = dest_gprh(ctx, a->rd);

        f128(dest, desth, src1, src1h, src2, src2h);
        gen_set_gpr128(ctx, a->rd, dest, desth);
    }
    return true;
}

static bool gen_arith_per_ol(DisasContext *ctx, arg_r *a, DisasExtend ext,
                             void (*f_tl)(TCGv, TCGv, TCGv),
                             void (*f_32)(TCGv, TCGv, TCGv),
                             void (*f_128)(TCGv, TCGv, TCGv, TCGv, TCGv, TCGv))
{
    int olen = get_olen(ctx);

    if (olen != TARGET_LONG_BITS) {
        if (olen == 32) {
            f_tl = f_32;
        } else if (olen != 128) {
            g_assert_not_reached();
        }
    }
    return gen_arith(ctx, a, ext, f_tl, f_128);
}

static bool gen_shift_imm_fn(DisasContext *ctx, arg_shift *a, DisasExtend ext,
                             void (*func)(TCGv, TCGv, target_long),
                             void (*f128)(TCGv, TCGv, TCGv, TCGv, target_long))
{
    TCGv dest, src1;
    int max_len = get_olen(ctx);

    if (a->shamt >= max_len) {
        return false;
    }

    dest = dest_gpr(ctx, a->rd);
    src1 = get_gpr(ctx, a->rs1, ext);

    if (max_len < 128) {
        func(dest, src1, a->shamt);
        gen_set_gpr(ctx, a->rd, dest);
    } else {
        TCGv src1h = get_gprh(ctx, a->rs1);
        TCGv desth = dest_gprh(ctx, a->rd);

        if (f128 == NULL) {
            return false;
        }
        f128(dest, desth, src1, src1h, a->shamt);
        gen_set_gpr128(ctx, a->rd, dest, desth);
    }
    return true;
}

static bool gen_shift_imm_fn_per_ol(DisasContext *ctx, arg_shift *a,
                                    DisasExtend ext,
                                    void (*f_tl)(TCGv, TCGv, target_long),
                                    void (*f_32)(TCGv, TCGv, target_long),
                                    void (*f_128)(TCGv, TCGv, TCGv, TCGv,
                                                  target_long))
{
    int olen = get_olen(ctx);
    if (olen != TARGET_LONG_BITS) {
        if (olen == 32) {
            f_tl = f_32;
        } else if (olen != 128) {
            g_assert_not_reached();
        }
    }
    return gen_shift_imm_fn(ctx, a, ext, f_tl, f_128);
}

static bool gen_shift_imm_tl(DisasContext *ctx, arg_shift *a, DisasExtend ext,
                             void (*func)(TCGv, TCGv, TCGv))
{
    TCGv dest, src1, src2;
    int max_len = get_olen(ctx);

    if (a->shamt >= max_len) {
        return false;
    }

    dest = dest_gpr(ctx, a->rd);
    src1 = get_gpr(ctx, a->rs1, ext);
    src2 = tcg_constant_tl(a->shamt);

    func(dest, src1, src2);

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool gen_shift(DisasContext *ctx, arg_r *a, DisasExtend ext,
                      void (*func)(TCGv, TCGv, TCGv),
                      void (*f128)(TCGv, TCGv, TCGv, TCGv, TCGv))
{
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    TCGv ext2 = tcg_temp_new();
    int max_len = get_olen(ctx);

    tcg_gen_andi_tl(ext2, src2, max_len - 1);

    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);

    if (max_len < 128) {
        func(dest, src1, ext2);
        gen_set_gpr(ctx, a->rd, dest);
    } else {
        TCGv src1h = get_gprh(ctx, a->rs1);
        TCGv desth = dest_gprh(ctx, a->rd);

        if (f128 == NULL) {
            return false;
        }
        f128(dest, desth, src1, src1h, ext2);
        gen_set_gpr128(ctx, a->rd, dest, desth);
    }
    return true;
}

static bool gen_shift_per_ol(DisasContext *ctx, arg_r *a, DisasExtend ext,
                             void (*f_tl)(TCGv, TCGv, TCGv),
                             void (*f_32)(TCGv, TCGv, TCGv),
                             void (*f_128)(TCGv, TCGv, TCGv, TCGv, TCGv))
{
    int olen = get_olen(ctx);
    if (olen != TARGET_LONG_BITS) {
        if (olen == 32) {
            f_tl = f_32;
        } else if (olen != 128) {
            g_assert_not_reached();
        }
    }
    return gen_shift(ctx, a, ext, f_tl, f_128);
}

static bool gen_unary(DisasContext *ctx, arg_r2 *a, DisasExtend ext,
                      void (*func)(TCGv, TCGv))
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1 = get_gpr(ctx, a->rs1, ext);

    func(dest, src1);

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool gen_unary_per_ol(DisasContext *ctx, arg_r2 *a, DisasExtend ext,
                             void (*f_tl)(TCGv, TCGv),
                             void (*f_32)(TCGv, TCGv))
{
    int olen = get_olen(ctx);

    if (olen != TARGET_LONG_BITS) {
        if (olen == 32) {
            f_tl = f_32;
        } else {
            g_assert_not_reached();
        }
    }
    return gen_unary(ctx, a, ext, f_tl);
}

static bool gen_amo(DisasContext *ctx, arg_atomic *a,
                    void(*func)(TCGv, TCGv, TCGv, TCGArg, MemOp),
                    MemOp mop)
{
    TCGv dest = dest_gpr(ctx, a->rd);
    TCGv src1, src2 = get_gpr(ctx, a->rs2, EXT_NONE);
    MemOp size = mop & MO_SIZE;

    if (ctx->cfg_ptr->ext_zama16b && size >= MO_32) {
        mop |= MO_ATOM_WITHIN16;
    } else {
        mop |= MO_ALIGN;
    }

    decode_save_opc(ctx, RISCV_UW2_ALWAYS_STORE_AMO);
    src1 = get_address(ctx, a->rs1, 0);
    func(dest, src1, src2, ctx->mem_idx, mop);

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static bool gen_cmpxchg(DisasContext *ctx, arg_atomic *a, MemOp mop)
{
    TCGv dest = get_gpr(ctx, a->rd, EXT_NONE);
    TCGv src1 = get_address(ctx, a->rs1, 0);
    TCGv src2 = get_gpr(ctx, a->rs2, EXT_NONE);

    decode_save_opc(ctx, RISCV_UW2_ALWAYS_STORE_AMO);
    tcg_gen_atomic_cmpxchg_tl(dest, src1, dest, src2, ctx->mem_idx, mop);

    gen_set_gpr(ctx, a->rd, dest);
    return true;
}

static uint32_t opcode_at(DisasContextBase *dcbase, target_ulong pc)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPUState *cpu = ctx->cs;
    CPURISCVState *env = cpu_env(cpu);

    return translator_ldl(env, &ctx->base, pc);
}

#define SS_MMU_INDEX(ctx) (ctx->mem_idx | MMU_IDX_SS_WRITE)

/* Include insn module translation function */
#include "insn_trans/trans_rvi.c.inc"
#include "insn_trans/trans_rvm.c.inc"
#include "insn_trans/trans_rva.c.inc"
#include "insn_trans/trans_rvf.c.inc"
#include "insn_trans/trans_rvd.c.inc"
#include "insn_trans/trans_rvh.c.inc"
#include "insn_trans/trans_rvv.c.inc"
#include "insn_trans/trans_rvb.c.inc"
#include "insn_trans/trans_rvzicond.c.inc"
#include "insn_trans/trans_rvzacas.c.inc"
#include "insn_trans/trans_rvzabha.c.inc"
#include "insn_trans/trans_rvzawrs.c.inc"
#include "insn_trans/trans_rvzicbo.c.inc"
#include "insn_trans/trans_rvzimop.c.inc"
#include "insn_trans/trans_rvzfa.c.inc"
#include "insn_trans/trans_rvzfh.c.inc"
#include "insn_trans/trans_rvk.c.inc"
#include "insn_trans/trans_rvvk.c.inc"
#include "insn_trans/trans_privileged.c.inc"
#include "insn_trans/trans_svinval.c.inc"
#include "insn_trans/trans_rvbf16.c.inc"
#include "decode-xthead.c.inc"
#include "insn_trans/trans_xthead.c.inc"
#include "insn_trans/trans_xventanacondops.c.inc"

/* Include the auto-generated decoder for 16 bit insn */
#include "decode-insn16.c.inc"
#include "insn_trans/trans_rvzce.c.inc"
#include "insn_trans/trans_rvzcmop.c.inc"
#include "insn_trans/trans_rvzicfiss.c.inc"

/* Include decoders for factored-out extensions */
#include "decode-XVentanaCondOps.c.inc"

/* The specification allows for longer insns, but not supported by qemu. */
#define MAX_INSN_LEN  4

static inline int insn_len(uint16_t first_word)
{
    return (first_word & 3) == 3 ? 4 : 2;
}

const RISCVDecoder decoder_table[] = {
    { always_true_p, decode_insn32 },
    { has_xthead_p, decode_xthead},
    { has_XVentanaCondOps_p, decode_XVentanaCodeOps},
};

const size_t decoder_table_size = ARRAY_SIZE(decoder_table);

static void decode_opc(CPURISCVState *env, DisasContext *ctx, uint16_t opcode)
{
    ctx->virt_inst_excp = false;
    ctx->cur_insn_len = insn_len(opcode);
    /* Check for compressed insn */
    if (ctx->cur_insn_len == 2) {
        ctx->opcode = opcode;
        /*
         * The Zca extension is added as way to refer to instructions in the C
         * extension that do not include the floating-point loads and stores
         */
        if ((has_ext(ctx, RVC) || ctx->cfg_ptr->ext_zca) &&
            decode_insn16(ctx, opcode)) {
            return;
        }
    } else {
        uint32_t opcode32 = opcode;
        opcode32 = deposit32(opcode32, 16, 16,
                             translator_lduw(env, &ctx->base,
                                             ctx->base.pc_next + 2));
        ctx->opcode = opcode32;

        for (guint i = 0; i < ctx->decoders->len; ++i) {
            riscv_cpu_decode_fn func = g_ptr_array_index(ctx->decoders, i);
            if (func(ctx, opcode32)) {
                return;
            }
        }
    }

    gen_exception_illegal(ctx);
}

static void riscv_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPURISCVState *env = cpu_env(cs);
    RISCVCPUClass *mcc = RISCV_CPU_GET_CLASS(cs);
    RISCVCPU *cpu = RISCV_CPU(cs);
    uint32_t tb_flags = ctx->base.tb->flags;

    ctx->pc_save = ctx->base.pc_first;
    ctx->priv = FIELD_EX32(tb_flags, TB_FLAGS, PRIV);
    ctx->mem_idx = FIELD_EX32(tb_flags, TB_FLAGS, MEM_IDX);
    ctx->mstatus_fs = FIELD_EX32(tb_flags, TB_FLAGS, FS);
    ctx->mstatus_vs = FIELD_EX32(tb_flags, TB_FLAGS, VS);
    ctx->priv_ver = env->priv_ver;
    ctx->virt_enabled = FIELD_EX32(tb_flags, TB_FLAGS, VIRT_ENABLED);
    ctx->misa_ext = env->misa_ext;
    ctx->frm = -1;  /* unknown rounding mode */
    ctx->cfg_ptr = &(cpu->cfg);
    ctx->vill = FIELD_EX32(tb_flags, TB_FLAGS, VILL);
    ctx->sew = FIELD_EX32(tb_flags, TB_FLAGS, SEW);
    ctx->lmul = sextract32(FIELD_EX32(tb_flags, TB_FLAGS, LMUL), 0, 3);
    ctx->vta = FIELD_EX32(tb_flags, TB_FLAGS, VTA) && cpu->cfg.rvv_ta_all_1s;
    ctx->vma = FIELD_EX32(tb_flags, TB_FLAGS, VMA) && cpu->cfg.rvv_ma_all_1s;
    ctx->cfg_vta_all_1s = cpu->cfg.rvv_ta_all_1s;
    ctx->vstart_eq_zero = FIELD_EX32(tb_flags, TB_FLAGS, VSTART_EQ_ZERO);
    ctx->vl_eq_vlmax = FIELD_EX32(tb_flags, TB_FLAGS, VL_EQ_VLMAX);
    ctx->misa_mxl_max = mcc->misa_mxl_max;
    ctx->xl = FIELD_EX32(tb_flags, TB_FLAGS, XL);
    ctx->address_xl = FIELD_EX32(tb_flags, TB_FLAGS, AXL);
    ctx->cs = cs;
    if (get_xl(ctx) == MXL_RV32) {
        ctx->addr_xl = 32;
        ctx->addr_signed = false;
    } else {
        int pm_pmm = FIELD_EX32(tb_flags, TB_FLAGS, PM_PMM);
        ctx->addr_xl = 64 - riscv_pm_get_pmlen(pm_pmm);
        ctx->addr_signed = FIELD_EX32(tb_flags, TB_FLAGS, PM_SIGNEXTEND);
    }
    ctx->ztso = cpu->cfg.ext_ztso;
    ctx->itrigger = FIELD_EX32(tb_flags, TB_FLAGS, ITRIGGER);
    ctx->bcfi_enabled = FIELD_EX32(tb_flags, TB_FLAGS, BCFI_ENABLED);
    ctx->fcfi_lp_expected = FIELD_EX32(tb_flags, TB_FLAGS, FCFI_LP_EXPECTED);
    ctx->fcfi_enabled = FIELD_EX32(tb_flags, TB_FLAGS, FCFI_ENABLED);
    ctx->zero = tcg_constant_tl(0);
    ctx->virt_inst_excp = false;
    ctx->decoders = cpu->decoders;
}

static void riscv_tr_tb_start(DisasContextBase *db, CPUState *cpu)
{
}

static void riscv_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    target_ulong pc_next = ctx->base.pc_next;

    if (tb_cflags(dcbase->tb) & CF_PCREL) {
        pc_next &= ~TARGET_PAGE_MASK;
    }

    tcg_gen_insn_start(pc_next, 0, 0);
    ctx->insn_start_updated = false;
}

static void riscv_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPURISCVState *env = cpu_env(cpu);
    uint16_t opcode16 = translator_lduw(env, &ctx->base, ctx->base.pc_next);

    ctx->ol = ctx->xl;
    decode_opc(env, ctx, opcode16);
    ctx->base.pc_next += ctx->cur_insn_len;

    /*
     * If 'fcfi_lp_expected' is still true after processing the instruction,
     * then we did not see an 'lpad' instruction, and must raise an exception.
     * Insert code to raise the exception at the start of the insn; any other
     * code the insn may have emitted will be deleted as dead code following
     * the noreturn exception
     */
    if (ctx->fcfi_lp_expected) {
        /* Emit after insn_start, i.e. before the op following insn_start. */
        tcg_ctx->emit_before_op = QTAILQ_NEXT(ctx->base.insn_start, link);
        tcg_gen_st_tl(tcg_constant_tl(RISCV_EXCP_SW_CHECK_FCFI_TVAL),
                      tcg_env, offsetof(CPURISCVState, sw_check_code));
        gen_helper_raise_exception(tcg_env,
                      tcg_constant_i32(RISCV_EXCP_SW_CHECK));
        tcg_ctx->emit_before_op = NULL;
        ctx->base.is_jmp = DISAS_NORETURN;
    }

    /* Only the first insn within a TB is allowed to cross a page boundary. */
    if (ctx->base.is_jmp == DISAS_NEXT) {
        if (ctx->itrigger || !translator_is_same_page(&ctx->base, ctx->base.pc_next)) {
            ctx->base.is_jmp = DISAS_TOO_MANY;
        } else {
            unsigned page_ofs = ctx->base.pc_next & ~TARGET_PAGE_MASK;

            if (page_ofs > TARGET_PAGE_SIZE - MAX_INSN_LEN) {
                uint16_t next_insn =
                    translator_lduw(env, &ctx->base, ctx->base.pc_next);
                int len = insn_len(next_insn);

                if (!translator_is_same_page(&ctx->base, ctx->base.pc_next + len - 1)) {
                    ctx->base.is_jmp = DISAS_TOO_MANY;
                }
            }
        }
    }
}

static void riscv_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_TOO_MANY:
        gen_goto_tb(ctx, 0, 0);
        break;
    case DISAS_NORETURN:
        break;
    default:
        g_assert_not_reached();
    }
}

static const TranslatorOps riscv_tr_ops = {
    .init_disas_context = riscv_tr_init_disas_context,
    .tb_start           = riscv_tr_tb_start,
    .insn_start         = riscv_tr_insn_start,
    .translate_insn     = riscv_tr_translate_insn,
    .tb_stop            = riscv_tr_tb_stop,
};

void riscv_translate_code(CPUState *cs, TranslationBlock *tb,
                          int *max_insns, vaddr pc, void *host_pc)
{
    DisasContext ctx;

    translator_loop(cs, tb, max_insns, pc, host_pc, &riscv_tr_ops, &ctx.base);
}

void riscv_translate_init(void)
{
    int i;

    /*
     * cpu_gpr[0] is a placeholder for the zero register. Do not use it.
     * Use the gen_set_gpr and get_gpr helper functions when accessing regs,
     * unless you specifically block reads/writes to reg 0.
     */
    cpu_gpr[0] = NULL;
    cpu_gprh[0] = NULL;

    for (i = 1; i < 32; i++) {
        cpu_gpr[i] = tcg_global_mem_new(tcg_env,
            offsetof(CPURISCVState, gpr[i]), riscv_int_regnames[i]);
        cpu_gprh[i] = tcg_global_mem_new(tcg_env,
            offsetof(CPURISCVState, gprh[i]), riscv_int_regnamesh[i]);
    }

    for (i = 0; i < 32; i++) {
        cpu_fpr[i] = tcg_global_mem_new_i64(tcg_env,
            offsetof(CPURISCVState, fpr[i]), riscv_fpr_regnames[i]);
    }

    cpu_pc = tcg_global_mem_new(tcg_env, offsetof(CPURISCVState, pc), "pc");
    cpu_vl = tcg_global_mem_new(tcg_env, offsetof(CPURISCVState, vl), "vl");
    cpu_vstart = tcg_global_mem_new(tcg_env, offsetof(CPURISCVState, vstart),
                            "vstart");
    load_res = tcg_global_mem_new(tcg_env, offsetof(CPURISCVState, load_res),
                             "load_res");
    load_val = tcg_global_mem_new(tcg_env, offsetof(CPURISCVState, load_val),
                             "load_val");
}
