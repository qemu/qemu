/*
 *  m68k translation
 *
 *  Copyright (c) 2005-2007 CodeSourcery
 *  Written by Paul Brook
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

#include "qemu/osdep.h"
#include "cpu.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "qemu/log.h"
#include "qemu/qemu-print.h"
#include "exec/cpu_ldst.h"
#include "exec/translator.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "exec/log.h"
#include "fpu/softfloat.h"


//#define DEBUG_DISPATCH 1

#define DEFO32(name, offset) static TCGv QREG_##name;
#define DEFO64(name, offset) static TCGv_i64 QREG_##name;
#include "qregs.h.inc"
#undef DEFO32
#undef DEFO64

static TCGv_i32 cpu_halted;
static TCGv_i32 cpu_exception_index;

static char cpu_reg_names[2 * 8 * 3 + 5 * 4];
static TCGv cpu_dregs[8];
static TCGv cpu_aregs[8];
static TCGv_i64 cpu_macc[4];

#define REG(insn, pos)  (((insn) >> (pos)) & 7)
#define DREG(insn, pos) cpu_dregs[REG(insn, pos)]
#define AREG(insn, pos) get_areg(s, REG(insn, pos))
#define MACREG(acc)     cpu_macc[acc]
#define QREG_SP         get_areg(s, 7)

static TCGv NULL_QREG;
#define IS_NULL_QREG(t) (t == NULL_QREG)
/* Used to distinguish stores from bad addressing modes.  */
static TCGv store_dummy;

#include "exec/gen-icount.h"

void m68k_tcg_init(void)
{
    char *p;
    int i;

#define DEFO32(name, offset) \
    QREG_##name = tcg_global_mem_new_i32(cpu_env, \
        offsetof(CPUM68KState, offset), #name);
#define DEFO64(name, offset) \
    QREG_##name = tcg_global_mem_new_i64(cpu_env, \
        offsetof(CPUM68KState, offset), #name);
#include "qregs.h.inc"
#undef DEFO32
#undef DEFO64

    cpu_halted = tcg_global_mem_new_i32(cpu_env,
                                        -offsetof(M68kCPU, env) +
                                        offsetof(CPUState, halted), "HALTED");
    cpu_exception_index = tcg_global_mem_new_i32(cpu_env,
                                                 -offsetof(M68kCPU, env) +
                                                 offsetof(CPUState, exception_index),
                                                 "EXCEPTION");

    p = cpu_reg_names;
    for (i = 0; i < 8; i++) {
        sprintf(p, "D%d", i);
        cpu_dregs[i] = tcg_global_mem_new(cpu_env,
                                          offsetof(CPUM68KState, dregs[i]), p);
        p += 3;
        sprintf(p, "A%d", i);
        cpu_aregs[i] = tcg_global_mem_new(cpu_env,
                                          offsetof(CPUM68KState, aregs[i]), p);
        p += 3;
    }
    for (i = 0; i < 4; i++) {
        sprintf(p, "ACC%d", i);
        cpu_macc[i] = tcg_global_mem_new_i64(cpu_env,
                                         offsetof(CPUM68KState, macc[i]), p);
        p += 5;
    }

    NULL_QREG = tcg_global_mem_new(cpu_env, -4, "NULL");
    store_dummy = tcg_global_mem_new(cpu_env, -8, "NULL");
}

/* internal defines */
typedef struct DisasContext {
    DisasContextBase base;
    CPUM68KState *env;
    target_ulong pc;
    target_ulong pc_prev;
    CCOp cc_op; /* Current CC operation */
    int cc_op_synced;
    TCGv_i64 mactmp;
    int done_mac;
    int writeback_mask;
    TCGv writeback[8];
    bool ss_active;
} DisasContext;

static TCGv get_areg(DisasContext *s, unsigned regno)
{
    if (s->writeback_mask & (1 << regno)) {
        return s->writeback[regno];
    } else {
        return cpu_aregs[regno];
    }
}

static void delay_set_areg(DisasContext *s, unsigned regno,
                           TCGv val, bool give_temp)
{
    if (s->writeback_mask & (1 << regno)) {
        if (give_temp) {
            tcg_temp_free(s->writeback[regno]);
            s->writeback[regno] = val;
        } else {
            tcg_gen_mov_i32(s->writeback[regno], val);
        }
    } else {
        s->writeback_mask |= 1 << regno;
        if (give_temp) {
            s->writeback[regno] = val;
        } else {
            TCGv tmp = tcg_temp_new();
            s->writeback[regno] = tmp;
            tcg_gen_mov_i32(tmp, val);
        }
    }
}

static void do_writebacks(DisasContext *s)
{
    unsigned mask = s->writeback_mask;
    if (mask) {
        s->writeback_mask = 0;
        do {
            unsigned regno = ctz32(mask);
            tcg_gen_mov_i32(cpu_aregs[regno], s->writeback[regno]);
            tcg_temp_free(s->writeback[regno]);
            mask &= mask - 1;
        } while (mask);
    }
}

/* is_jmp field values */
#define DISAS_JUMP      DISAS_TARGET_0 /* only pc was modified dynamically */
#define DISAS_EXIT      DISAS_TARGET_1 /* cpu state was modified dynamically */

#if defined(CONFIG_USER_ONLY)
#define IS_USER(s) 1
#else
#define IS_USER(s)   (!(s->base.tb->flags & TB_FLAGS_MSR_S))
#define SFC_INDEX(s) ((s->base.tb->flags & TB_FLAGS_SFC_S) ? \
                      MMU_KERNEL_IDX : MMU_USER_IDX)
#define DFC_INDEX(s) ((s->base.tb->flags & TB_FLAGS_DFC_S) ? \
                      MMU_KERNEL_IDX : MMU_USER_IDX)
#endif

typedef void (*disas_proc)(CPUM68KState *env, DisasContext *s, uint16_t insn);

#ifdef DEBUG_DISPATCH
#define DISAS_INSN(name)                                                \
    static void real_disas_##name(CPUM68KState *env, DisasContext *s,   \
                                  uint16_t insn);                       \
    static void disas_##name(CPUM68KState *env, DisasContext *s,        \
                             uint16_t insn)                             \
    {                                                                   \
        qemu_log("Dispatch " #name "\n");                               \
        real_disas_##name(env, s, insn);                                \
    }                                                                   \
    static void real_disas_##name(CPUM68KState *env, DisasContext *s,   \
                                  uint16_t insn)
#else
#define DISAS_INSN(name)                                                \
    static void disas_##name(CPUM68KState *env, DisasContext *s,        \
                             uint16_t insn)
#endif

static const uint8_t cc_op_live[CC_OP_NB] = {
    [CC_OP_DYNAMIC] = CCF_C | CCF_V | CCF_Z | CCF_N | CCF_X,
    [CC_OP_FLAGS] = CCF_C | CCF_V | CCF_Z | CCF_N | CCF_X,
    [CC_OP_ADDB ... CC_OP_ADDL] = CCF_X | CCF_N | CCF_V,
    [CC_OP_SUBB ... CC_OP_SUBL] = CCF_X | CCF_N | CCF_V,
    [CC_OP_CMPB ... CC_OP_CMPL] = CCF_X | CCF_N | CCF_V,
    [CC_OP_LOGIC] = CCF_X | CCF_N
};

static void set_cc_op(DisasContext *s, CCOp op)
{
    CCOp old_op = s->cc_op;
    int dead;

    if (old_op == op) {
        return;
    }
    s->cc_op = op;
    s->cc_op_synced = 0;

    /*
     * Discard CC computation that will no longer be used.
     * Note that X and N are never dead.
     */
    dead = cc_op_live[old_op] & ~cc_op_live[op];
    if (dead & CCF_C) {
        tcg_gen_discard_i32(QREG_CC_C);
    }
    if (dead & CCF_Z) {
        tcg_gen_discard_i32(QREG_CC_Z);
    }
    if (dead & CCF_V) {
        tcg_gen_discard_i32(QREG_CC_V);
    }
}

/* Update the CPU env CC_OP state.  */
static void update_cc_op(DisasContext *s)
{
    if (!s->cc_op_synced) {
        s->cc_op_synced = 1;
        tcg_gen_movi_i32(QREG_CC_OP, s->cc_op);
    }
}

/* Generate a jump to an immediate address.  */
static void gen_jmp_im(DisasContext *s, uint32_t dest)
{
    update_cc_op(s);
    tcg_gen_movi_i32(QREG_PC, dest);
    s->base.is_jmp = DISAS_JUMP;
}

/* Generate a jump to the address in qreg DEST.  */
static void gen_jmp(DisasContext *s, TCGv dest)
{
    update_cc_op(s);
    tcg_gen_mov_i32(QREG_PC, dest);
    s->base.is_jmp = DISAS_JUMP;
}

static void gen_raise_exception(int nr)
{
    TCGv_i32 tmp;

    tmp = tcg_const_i32(nr);
    gen_helper_raise_exception(cpu_env, tmp);
    tcg_temp_free_i32(tmp);
}

static void gen_raise_exception_format2(DisasContext *s, int nr,
                                        target_ulong this_pc)
{
    /*
     * Pass the address of the insn to the exception handler,
     * for recording in the Format $2 (6-word) stack frame.
     * Re-use mmu.ar for the purpose, since that's only valid
     * after tlb_fill.
     */
    tcg_gen_st_i32(tcg_constant_i32(this_pc), cpu_env,
                   offsetof(CPUM68KState, mmu.ar));
    gen_raise_exception(nr);
    s->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception(DisasContext *s, uint32_t dest, int nr)
{
    update_cc_op(s);
    tcg_gen_movi_i32(QREG_PC, dest);

    gen_raise_exception(nr);

    s->base.is_jmp = DISAS_NORETURN;
}

static inline void gen_addr_fault(DisasContext *s)
{
    gen_exception(s, s->base.pc_next, EXCP_ADDRESS);
}

/*
 * Generate a load from the specified address.  Narrow values are
 *  sign extended to full register width.
 */
static inline TCGv gen_load(DisasContext *s, int opsize, TCGv addr,
                            int sign, int index)
{
    TCGv tmp;
    tmp = tcg_temp_new_i32();
    switch(opsize) {
    case OS_BYTE:
        if (sign)
            tcg_gen_qemu_ld8s(tmp, addr, index);
        else
            tcg_gen_qemu_ld8u(tmp, addr, index);
        break;
    case OS_WORD:
        if (sign)
            tcg_gen_qemu_ld16s(tmp, addr, index);
        else
            tcg_gen_qemu_ld16u(tmp, addr, index);
        break;
    case OS_LONG:
        tcg_gen_qemu_ld32u(tmp, addr, index);
        break;
    default:
        g_assert_not_reached();
    }
    return tmp;
}

/* Generate a store.  */
static inline void gen_store(DisasContext *s, int opsize, TCGv addr, TCGv val,
                             int index)
{
    switch(opsize) {
    case OS_BYTE:
        tcg_gen_qemu_st8(val, addr, index);
        break;
    case OS_WORD:
        tcg_gen_qemu_st16(val, addr, index);
        break;
    case OS_LONG:
        tcg_gen_qemu_st32(val, addr, index);
        break;
    default:
        g_assert_not_reached();
    }
}

typedef enum {
    EA_STORE,
    EA_LOADU,
    EA_LOADS
} ea_what;

/*
 * Generate an unsigned load if VAL is 0 a signed load if val is -1,
 * otherwise generate a store.
 */
static TCGv gen_ldst(DisasContext *s, int opsize, TCGv addr, TCGv val,
                     ea_what what, int index)
{
    if (what == EA_STORE) {
        gen_store(s, opsize, addr, val, index);
        return store_dummy;
    } else {
        return gen_load(s, opsize, addr, what == EA_LOADS, index);
    }
}

/* Read a 16-bit immediate constant */
static inline uint16_t read_im16(CPUM68KState *env, DisasContext *s)
{
    uint16_t im;
    im = translator_lduw(env, &s->base, s->pc);
    s->pc += 2;
    return im;
}

/* Read an 8-bit immediate constant */
static inline uint8_t read_im8(CPUM68KState *env, DisasContext *s)
{
    return read_im16(env, s);
}

/* Read a 32-bit immediate constant.  */
static inline uint32_t read_im32(CPUM68KState *env, DisasContext *s)
{
    uint32_t im;
    im = read_im16(env, s) << 16;
    im |= 0xffff & read_im16(env, s);
    return im;
}

/* Read a 64-bit immediate constant.  */
static inline uint64_t read_im64(CPUM68KState *env, DisasContext *s)
{
    uint64_t im;
    im = (uint64_t)read_im32(env, s) << 32;
    im |= (uint64_t)read_im32(env, s);
    return im;
}

/* Calculate and address index.  */
static TCGv gen_addr_index(DisasContext *s, uint16_t ext, TCGv tmp)
{
    TCGv add;
    int scale;

    add = (ext & 0x8000) ? AREG(ext, 12) : DREG(ext, 12);
    if ((ext & 0x800) == 0) {
        tcg_gen_ext16s_i32(tmp, add);
        add = tmp;
    }
    scale = (ext >> 9) & 3;
    if (scale != 0) {
        tcg_gen_shli_i32(tmp, add, scale);
        add = tmp;
    }
    return add;
}

/*
 * Handle a base + index + displacement effective address.
 * A NULL_QREG base means pc-relative.
 */
static TCGv gen_lea_indexed(CPUM68KState *env, DisasContext *s, TCGv base)
{
    uint32_t offset;
    uint16_t ext;
    TCGv add;
    TCGv tmp;
    uint32_t bd, od;

    offset = s->pc;
    ext = read_im16(env, s);

    if ((ext & 0x800) == 0 && !m68k_feature(s->env, M68K_FEATURE_WORD_INDEX))
        return NULL_QREG;

    if (m68k_feature(s->env, M68K_FEATURE_M68K) &&
        !m68k_feature(s->env, M68K_FEATURE_SCALED_INDEX)) {
        ext &= ~(3 << 9);
    }

    if (ext & 0x100) {
        /* full extension word format */
        if (!m68k_feature(s->env, M68K_FEATURE_EXT_FULL))
            return NULL_QREG;

        if ((ext & 0x30) > 0x10) {
            /* base displacement */
            if ((ext & 0x30) == 0x20) {
                bd = (int16_t)read_im16(env, s);
            } else {
                bd = read_im32(env, s);
            }
        } else {
            bd = 0;
        }
        tmp = tcg_temp_new();
        if ((ext & 0x44) == 0) {
            /* pre-index */
            add = gen_addr_index(s, ext, tmp);
        } else {
            add = NULL_QREG;
        }
        if ((ext & 0x80) == 0) {
            /* base not suppressed */
            if (IS_NULL_QREG(base)) {
                base = tcg_const_i32(offset + bd);
                bd = 0;
            }
            if (!IS_NULL_QREG(add)) {
                tcg_gen_add_i32(tmp, add, base);
                add = tmp;
            } else {
                add = base;
            }
        }
        if (!IS_NULL_QREG(add)) {
            if (bd != 0) {
                tcg_gen_addi_i32(tmp, add, bd);
                add = tmp;
            }
        } else {
            add = tcg_const_i32(bd);
        }
        if ((ext & 3) != 0) {
            /* memory indirect */
            base = gen_load(s, OS_LONG, add, 0, IS_USER(s));
            if ((ext & 0x44) == 4) {
                add = gen_addr_index(s, ext, tmp);
                tcg_gen_add_i32(tmp, add, base);
                add = tmp;
            } else {
                add = base;
            }
            if ((ext & 3) > 1) {
                /* outer displacement */
                if ((ext & 3) == 2) {
                    od = (int16_t)read_im16(env, s);
                } else {
                    od = read_im32(env, s);
                }
            } else {
                od = 0;
            }
            if (od != 0) {
                tcg_gen_addi_i32(tmp, add, od);
                add = tmp;
            }
        }
    } else {
        /* brief extension word format */
        tmp = tcg_temp_new();
        add = gen_addr_index(s, ext, tmp);
        if (!IS_NULL_QREG(base)) {
            tcg_gen_add_i32(tmp, add, base);
            if ((int8_t)ext)
                tcg_gen_addi_i32(tmp, tmp, (int8_t)ext);
        } else {
            tcg_gen_addi_i32(tmp, add, offset + (int8_t)ext);
        }
        add = tmp;
    }
    return add;
}

/* Sign or zero extend a value.  */

static inline void gen_ext(TCGv res, TCGv val, int opsize, int sign)
{
    switch (opsize) {
    case OS_BYTE:
        if (sign) {
            tcg_gen_ext8s_i32(res, val);
        } else {
            tcg_gen_ext8u_i32(res, val);
        }
        break;
    case OS_WORD:
        if (sign) {
            tcg_gen_ext16s_i32(res, val);
        } else {
            tcg_gen_ext16u_i32(res, val);
        }
        break;
    case OS_LONG:
        tcg_gen_mov_i32(res, val);
        break;
    default:
        g_assert_not_reached();
    }
}

/* Evaluate all the CC flags.  */

static void gen_flush_flags(DisasContext *s)
{
    TCGv t0, t1;

    switch (s->cc_op) {
    case CC_OP_FLAGS:
        return;

    case CC_OP_ADDB:
    case CC_OP_ADDW:
    case CC_OP_ADDL:
        tcg_gen_mov_i32(QREG_CC_C, QREG_CC_X);
        tcg_gen_mov_i32(QREG_CC_Z, QREG_CC_N);
        /* Compute signed overflow for addition.  */
        t0 = tcg_temp_new();
        t1 = tcg_temp_new();
        tcg_gen_sub_i32(t0, QREG_CC_N, QREG_CC_V);
        gen_ext(t0, t0, s->cc_op - CC_OP_ADDB, 1);
        tcg_gen_xor_i32(t1, QREG_CC_N, QREG_CC_V);
        tcg_gen_xor_i32(QREG_CC_V, QREG_CC_V, t0);
        tcg_temp_free(t0);
        tcg_gen_andc_i32(QREG_CC_V, t1, QREG_CC_V);
        tcg_temp_free(t1);
        break;

    case CC_OP_SUBB:
    case CC_OP_SUBW:
    case CC_OP_SUBL:
        tcg_gen_mov_i32(QREG_CC_C, QREG_CC_X);
        tcg_gen_mov_i32(QREG_CC_Z, QREG_CC_N);
        /* Compute signed overflow for subtraction.  */
        t0 = tcg_temp_new();
        t1 = tcg_temp_new();
        tcg_gen_add_i32(t0, QREG_CC_N, QREG_CC_V);
        gen_ext(t0, t0, s->cc_op - CC_OP_SUBB, 1);
        tcg_gen_xor_i32(t1, QREG_CC_N, t0);
        tcg_gen_xor_i32(QREG_CC_V, QREG_CC_V, t0);
        tcg_temp_free(t0);
        tcg_gen_and_i32(QREG_CC_V, QREG_CC_V, t1);
        tcg_temp_free(t1);
        break;

    case CC_OP_CMPB:
    case CC_OP_CMPW:
    case CC_OP_CMPL:
        tcg_gen_setcond_i32(TCG_COND_LTU, QREG_CC_C, QREG_CC_N, QREG_CC_V);
        tcg_gen_sub_i32(QREG_CC_Z, QREG_CC_N, QREG_CC_V);
        gen_ext(QREG_CC_Z, QREG_CC_Z, s->cc_op - CC_OP_CMPB, 1);
        /* Compute signed overflow for subtraction.  */
        t0 = tcg_temp_new();
        tcg_gen_xor_i32(t0, QREG_CC_Z, QREG_CC_N);
        tcg_gen_xor_i32(QREG_CC_V, QREG_CC_V, QREG_CC_N);
        tcg_gen_and_i32(QREG_CC_V, QREG_CC_V, t0);
        tcg_temp_free(t0);
        tcg_gen_mov_i32(QREG_CC_N, QREG_CC_Z);
        break;

    case CC_OP_LOGIC:
        tcg_gen_mov_i32(QREG_CC_Z, QREG_CC_N);
        tcg_gen_movi_i32(QREG_CC_C, 0);
        tcg_gen_movi_i32(QREG_CC_V, 0);
        break;

    case CC_OP_DYNAMIC:
        gen_helper_flush_flags(cpu_env, QREG_CC_OP);
        s->cc_op_synced = 1;
        break;

    default:
        t0 = tcg_const_i32(s->cc_op);
        gen_helper_flush_flags(cpu_env, t0);
        tcg_temp_free(t0);
        s->cc_op_synced = 1;
        break;
    }

    /* Note that flush_flags also assigned to env->cc_op.  */
    s->cc_op = CC_OP_FLAGS;
}

static inline TCGv gen_extend(DisasContext *s, TCGv val, int opsize, int sign)
{
    TCGv tmp;

    if (opsize == OS_LONG) {
        tmp = val;
    } else {
        tmp = tcg_temp_new();
        gen_ext(tmp, val, opsize, sign);
    }

    return tmp;
}

static void gen_logic_cc(DisasContext *s, TCGv val, int opsize)
{
    gen_ext(QREG_CC_N, val, opsize, 1);
    set_cc_op(s, CC_OP_LOGIC);
}

static void gen_update_cc_cmp(DisasContext *s, TCGv dest, TCGv src, int opsize)
{
    tcg_gen_mov_i32(QREG_CC_N, dest);
    tcg_gen_mov_i32(QREG_CC_V, src);
    set_cc_op(s, CC_OP_CMPB + opsize);
}

static void gen_update_cc_add(TCGv dest, TCGv src, int opsize)
{
    gen_ext(QREG_CC_N, dest, opsize, 1);
    tcg_gen_mov_i32(QREG_CC_V, src);
}

static inline int opsize_bytes(int opsize)
{
    switch (opsize) {
    case OS_BYTE: return 1;
    case OS_WORD: return 2;
    case OS_LONG: return 4;
    case OS_SINGLE: return 4;
    case OS_DOUBLE: return 8;
    case OS_EXTENDED: return 12;
    case OS_PACKED: return 12;
    default:
        g_assert_not_reached();
    }
}

static inline int insn_opsize(int insn)
{
    switch ((insn >> 6) & 3) {
    case 0: return OS_BYTE;
    case 1: return OS_WORD;
    case 2: return OS_LONG;
    default:
        g_assert_not_reached();
    }
}

static inline int ext_opsize(int ext, int pos)
{
    switch ((ext >> pos) & 7) {
    case 0: return OS_LONG;
    case 1: return OS_SINGLE;
    case 2: return OS_EXTENDED;
    case 3: return OS_PACKED;
    case 4: return OS_WORD;
    case 5: return OS_DOUBLE;
    case 6: return OS_BYTE;
    default:
        g_assert_not_reached();
    }
}

/*
 * Assign value to a register.  If the width is less than the register width
 * only the low part of the register is set.
 */
static void gen_partset_reg(int opsize, TCGv reg, TCGv val)
{
    TCGv tmp;
    switch (opsize) {
    case OS_BYTE:
        tcg_gen_andi_i32(reg, reg, 0xffffff00);
        tmp = tcg_temp_new();
        tcg_gen_ext8u_i32(tmp, val);
        tcg_gen_or_i32(reg, reg, tmp);
        tcg_temp_free(tmp);
        break;
    case OS_WORD:
        tcg_gen_andi_i32(reg, reg, 0xffff0000);
        tmp = tcg_temp_new();
        tcg_gen_ext16u_i32(tmp, val);
        tcg_gen_or_i32(reg, reg, tmp);
        tcg_temp_free(tmp);
        break;
    case OS_LONG:
    case OS_SINGLE:
        tcg_gen_mov_i32(reg, val);
        break;
    default:
        g_assert_not_reached();
    }
}

/*
 * Generate code for an "effective address".  Does not adjust the base
 * register for autoincrement addressing modes.
 */
static TCGv gen_lea_mode(CPUM68KState *env, DisasContext *s,
                         int mode, int reg0, int opsize)
{
    TCGv reg;
    TCGv tmp;
    uint16_t ext;
    uint32_t offset;

    switch (mode) {
    case 0: /* Data register direct.  */
    case 1: /* Address register direct.  */
        return NULL_QREG;
    case 3: /* Indirect postincrement.  */
        if (opsize == OS_UNSIZED) {
            return NULL_QREG;
        }
        /* fallthru */
    case 2: /* Indirect register */
        return get_areg(s, reg0);
    case 4: /* Indirect predecrememnt.  */
        if (opsize == OS_UNSIZED) {
            return NULL_QREG;
        }
        reg = get_areg(s, reg0);
        tmp = tcg_temp_new();
        if (reg0 == 7 && opsize == OS_BYTE &&
            m68k_feature(s->env, M68K_FEATURE_M68K)) {
            tcg_gen_subi_i32(tmp, reg, 2);
        } else {
            tcg_gen_subi_i32(tmp, reg, opsize_bytes(opsize));
        }
        return tmp;
    case 5: /* Indirect displacement.  */
        reg = get_areg(s, reg0);
        tmp = tcg_temp_new();
        ext = read_im16(env, s);
        tcg_gen_addi_i32(tmp, reg, (int16_t)ext);
        return tmp;
    case 6: /* Indirect index + displacement.  */
        reg = get_areg(s, reg0);
        return gen_lea_indexed(env, s, reg);
    case 7: /* Other */
        switch (reg0) {
        case 0: /* Absolute short.  */
            offset = (int16_t)read_im16(env, s);
            return tcg_const_i32(offset);
        case 1: /* Absolute long.  */
            offset = read_im32(env, s);
            return tcg_const_i32(offset);
        case 2: /* pc displacement  */
            offset = s->pc;
            offset += (int16_t)read_im16(env, s);
            return tcg_const_i32(offset);
        case 3: /* pc index+displacement.  */
            return gen_lea_indexed(env, s, NULL_QREG);
        case 4: /* Immediate.  */
        default:
            return NULL_QREG;
        }
    }
    /* Should never happen.  */
    return NULL_QREG;
}

static TCGv gen_lea(CPUM68KState *env, DisasContext *s, uint16_t insn,
                    int opsize)
{
    int mode = extract32(insn, 3, 3);
    int reg0 = REG(insn, 0);
    return gen_lea_mode(env, s, mode, reg0, opsize);
}

/*
 * Generate code to load/store a value from/into an EA.  If WHAT > 0 this is
 * a write otherwise it is a read (0 == sign extend, -1 == zero extend).
 * ADDRP is non-null for readwrite operands.
 */
static TCGv gen_ea_mode(CPUM68KState *env, DisasContext *s, int mode, int reg0,
                        int opsize, TCGv val, TCGv *addrp, ea_what what,
                        int index)
{
    TCGv reg, tmp, result;
    int32_t offset;

    switch (mode) {
    case 0: /* Data register direct.  */
        reg = cpu_dregs[reg0];
        if (what == EA_STORE) {
            gen_partset_reg(opsize, reg, val);
            return store_dummy;
        } else {
            return gen_extend(s, reg, opsize, what == EA_LOADS);
        }
    case 1: /* Address register direct.  */
        reg = get_areg(s, reg0);
        if (what == EA_STORE) {
            tcg_gen_mov_i32(reg, val);
            return store_dummy;
        } else {
            return gen_extend(s, reg, opsize, what == EA_LOADS);
        }
    case 2: /* Indirect register */
        reg = get_areg(s, reg0);
        return gen_ldst(s, opsize, reg, val, what, index);
    case 3: /* Indirect postincrement.  */
        reg = get_areg(s, reg0);
        result = gen_ldst(s, opsize, reg, val, what, index);
        if (what == EA_STORE || !addrp) {
            TCGv tmp = tcg_temp_new();
            if (reg0 == 7 && opsize == OS_BYTE &&
                m68k_feature(s->env, M68K_FEATURE_M68K)) {
                tcg_gen_addi_i32(tmp, reg, 2);
            } else {
                tcg_gen_addi_i32(tmp, reg, opsize_bytes(opsize));
            }
            delay_set_areg(s, reg0, tmp, true);
        }
        return result;
    case 4: /* Indirect predecrememnt.  */
        if (addrp && what == EA_STORE) {
            tmp = *addrp;
        } else {
            tmp = gen_lea_mode(env, s, mode, reg0, opsize);
            if (IS_NULL_QREG(tmp)) {
                return tmp;
            }
            if (addrp) {
                *addrp = tmp;
            }
        }
        result = gen_ldst(s, opsize, tmp, val, what, index);
        if (what == EA_STORE || !addrp) {
            delay_set_areg(s, reg0, tmp, false);
        }
        return result;
    case 5: /* Indirect displacement.  */
    case 6: /* Indirect index + displacement.  */
    do_indirect:
        if (addrp && what == EA_STORE) {
            tmp = *addrp;
        } else {
            tmp = gen_lea_mode(env, s, mode, reg0, opsize);
            if (IS_NULL_QREG(tmp)) {
                return tmp;
            }
            if (addrp) {
                *addrp = tmp;
            }
        }
        return gen_ldst(s, opsize, tmp, val, what, index);
    case 7: /* Other */
        switch (reg0) {
        case 0: /* Absolute short.  */
        case 1: /* Absolute long.  */
        case 2: /* pc displacement  */
        case 3: /* pc index+displacement.  */
            goto do_indirect;
        case 4: /* Immediate.  */
            /* Sign extend values for consistency.  */
            switch (opsize) {
            case OS_BYTE:
                if (what == EA_LOADS) {
                    offset = (int8_t)read_im8(env, s);
                } else {
                    offset = read_im8(env, s);
                }
                break;
            case OS_WORD:
                if (what == EA_LOADS) {
                    offset = (int16_t)read_im16(env, s);
                } else {
                    offset = read_im16(env, s);
                }
                break;
            case OS_LONG:
                offset = read_im32(env, s);
                break;
            default:
                g_assert_not_reached();
            }
            return tcg_const_i32(offset);
        default:
            return NULL_QREG;
        }
    }
    /* Should never happen.  */
    return NULL_QREG;
}

static TCGv gen_ea(CPUM68KState *env, DisasContext *s, uint16_t insn,
                   int opsize, TCGv val, TCGv *addrp, ea_what what, int index)
{
    int mode = extract32(insn, 3, 3);
    int reg0 = REG(insn, 0);
    return gen_ea_mode(env, s, mode, reg0, opsize, val, addrp, what, index);
}

static TCGv_ptr gen_fp_ptr(int freg)
{
    TCGv_ptr fp = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(fp, cpu_env, offsetof(CPUM68KState, fregs[freg]));
    return fp;
}

static TCGv_ptr gen_fp_result_ptr(void)
{
    TCGv_ptr fp = tcg_temp_new_ptr();
    tcg_gen_addi_ptr(fp, cpu_env, offsetof(CPUM68KState, fp_result));
    return fp;
}

static void gen_fp_move(TCGv_ptr dest, TCGv_ptr src)
{
    TCGv t32;
    TCGv_i64 t64;

    t32 = tcg_temp_new();
    tcg_gen_ld16u_i32(t32, src, offsetof(FPReg, l.upper));
    tcg_gen_st16_i32(t32, dest, offsetof(FPReg, l.upper));
    tcg_temp_free(t32);

    t64 = tcg_temp_new_i64();
    tcg_gen_ld_i64(t64, src, offsetof(FPReg, l.lower));
    tcg_gen_st_i64(t64, dest, offsetof(FPReg, l.lower));
    tcg_temp_free_i64(t64);
}

static void gen_load_fp(DisasContext *s, int opsize, TCGv addr, TCGv_ptr fp,
                        int index)
{
    TCGv tmp;
    TCGv_i64 t64;

    t64 = tcg_temp_new_i64();
    tmp = tcg_temp_new();
    switch (opsize) {
    case OS_BYTE:
        tcg_gen_qemu_ld8s(tmp, addr, index);
        gen_helper_exts32(cpu_env, fp, tmp);
        break;
    case OS_WORD:
        tcg_gen_qemu_ld16s(tmp, addr, index);
        gen_helper_exts32(cpu_env, fp, tmp);
        break;
    case OS_LONG:
        tcg_gen_qemu_ld32u(tmp, addr, index);
        gen_helper_exts32(cpu_env, fp, tmp);
        break;
    case OS_SINGLE:
        tcg_gen_qemu_ld32u(tmp, addr, index);
        gen_helper_extf32(cpu_env, fp, tmp);
        break;
    case OS_DOUBLE:
        tcg_gen_qemu_ld64(t64, addr, index);
        gen_helper_extf64(cpu_env, fp, t64);
        break;
    case OS_EXTENDED:
        if (m68k_feature(s->env, M68K_FEATURE_CF_FPU)) {
            gen_exception(s, s->base.pc_next, EXCP_FP_UNIMP);
            break;
        }
        tcg_gen_qemu_ld32u(tmp, addr, index);
        tcg_gen_shri_i32(tmp, tmp, 16);
        tcg_gen_st16_i32(tmp, fp, offsetof(FPReg, l.upper));
        tcg_gen_addi_i32(tmp, addr, 4);
        tcg_gen_qemu_ld64(t64, tmp, index);
        tcg_gen_st_i64(t64, fp, offsetof(FPReg, l.lower));
        break;
    case OS_PACKED:
        /*
         * unimplemented data type on 68040/ColdFire
         * FIXME if needed for another FPU
         */
        gen_exception(s, s->base.pc_next, EXCP_FP_UNIMP);
        break;
    default:
        g_assert_not_reached();
    }
    tcg_temp_free(tmp);
    tcg_temp_free_i64(t64);
}

static void gen_store_fp(DisasContext *s, int opsize, TCGv addr, TCGv_ptr fp,
                         int index)
{
    TCGv tmp;
    TCGv_i64 t64;

    t64 = tcg_temp_new_i64();
    tmp = tcg_temp_new();
    switch (opsize) {
    case OS_BYTE:
        gen_helper_reds32(tmp, cpu_env, fp);
        tcg_gen_qemu_st8(tmp, addr, index);
        break;
    case OS_WORD:
        gen_helper_reds32(tmp, cpu_env, fp);
        tcg_gen_qemu_st16(tmp, addr, index);
        break;
    case OS_LONG:
        gen_helper_reds32(tmp, cpu_env, fp);
        tcg_gen_qemu_st32(tmp, addr, index);
        break;
    case OS_SINGLE:
        gen_helper_redf32(tmp, cpu_env, fp);
        tcg_gen_qemu_st32(tmp, addr, index);
        break;
    case OS_DOUBLE:
        gen_helper_redf64(t64, cpu_env, fp);
        tcg_gen_qemu_st64(t64, addr, index);
        break;
    case OS_EXTENDED:
        if (m68k_feature(s->env, M68K_FEATURE_CF_FPU)) {
            gen_exception(s, s->base.pc_next, EXCP_FP_UNIMP);
            break;
        }
        tcg_gen_ld16u_i32(tmp, fp, offsetof(FPReg, l.upper));
        tcg_gen_shli_i32(tmp, tmp, 16);
        tcg_gen_qemu_st32(tmp, addr, index);
        tcg_gen_addi_i32(tmp, addr, 4);
        tcg_gen_ld_i64(t64, fp, offsetof(FPReg, l.lower));
        tcg_gen_qemu_st64(t64, tmp, index);
        break;
    case OS_PACKED:
        /*
         * unimplemented data type on 68040/ColdFire
         * FIXME if needed for another FPU
         */
        gen_exception(s, s->base.pc_next, EXCP_FP_UNIMP);
        break;
    default:
        g_assert_not_reached();
    }
    tcg_temp_free(tmp);
    tcg_temp_free_i64(t64);
}

static void gen_ldst_fp(DisasContext *s, int opsize, TCGv addr,
                        TCGv_ptr fp, ea_what what, int index)
{
    if (what == EA_STORE) {
        gen_store_fp(s, opsize, addr, fp, index);
    } else {
        gen_load_fp(s, opsize, addr, fp, index);
    }
}

static int gen_ea_mode_fp(CPUM68KState *env, DisasContext *s, int mode,
                          int reg0, int opsize, TCGv_ptr fp, ea_what what,
                          int index)
{
    TCGv reg, addr, tmp;
    TCGv_i64 t64;

    switch (mode) {
    case 0: /* Data register direct.  */
        reg = cpu_dregs[reg0];
        if (what == EA_STORE) {
            switch (opsize) {
            case OS_BYTE:
            case OS_WORD:
            case OS_LONG:
                gen_helper_reds32(reg, cpu_env, fp);
                break;
            case OS_SINGLE:
                gen_helper_redf32(reg, cpu_env, fp);
                break;
            default:
                g_assert_not_reached();
            }
        } else {
            tmp = tcg_temp_new();
            switch (opsize) {
            case OS_BYTE:
                tcg_gen_ext8s_i32(tmp, reg);
                gen_helper_exts32(cpu_env, fp, tmp);
                break;
            case OS_WORD:
                tcg_gen_ext16s_i32(tmp, reg);
                gen_helper_exts32(cpu_env, fp, tmp);
                break;
            case OS_LONG:
                gen_helper_exts32(cpu_env, fp, reg);
                break;
            case OS_SINGLE:
                gen_helper_extf32(cpu_env, fp, reg);
                break;
            default:
                g_assert_not_reached();
            }
            tcg_temp_free(tmp);
        }
        return 0;
    case 1: /* Address register direct.  */
        return -1;
    case 2: /* Indirect register */
        addr = get_areg(s, reg0);
        gen_ldst_fp(s, opsize, addr, fp, what, index);
        return 0;
    case 3: /* Indirect postincrement.  */
        addr = cpu_aregs[reg0];
        gen_ldst_fp(s, opsize, addr, fp, what, index);
        tcg_gen_addi_i32(addr, addr, opsize_bytes(opsize));
        return 0;
    case 4: /* Indirect predecrememnt.  */
        addr = gen_lea_mode(env, s, mode, reg0, opsize);
        if (IS_NULL_QREG(addr)) {
            return -1;
        }
        gen_ldst_fp(s, opsize, addr, fp, what, index);
        tcg_gen_mov_i32(cpu_aregs[reg0], addr);
        return 0;
    case 5: /* Indirect displacement.  */
    case 6: /* Indirect index + displacement.  */
    do_indirect:
        addr = gen_lea_mode(env, s, mode, reg0, opsize);
        if (IS_NULL_QREG(addr)) {
            return -1;
        }
        gen_ldst_fp(s, opsize, addr, fp, what, index);
        return 0;
    case 7: /* Other */
        switch (reg0) {
        case 0: /* Absolute short.  */
        case 1: /* Absolute long.  */
        case 2: /* pc displacement  */
        case 3: /* pc index+displacement.  */
            goto do_indirect;
        case 4: /* Immediate.  */
            if (what == EA_STORE) {
                return -1;
            }
            switch (opsize) {
            case OS_BYTE:
                tmp = tcg_const_i32((int8_t)read_im8(env, s));
                gen_helper_exts32(cpu_env, fp, tmp);
                tcg_temp_free(tmp);
                break;
            case OS_WORD:
                tmp = tcg_const_i32((int16_t)read_im16(env, s));
                gen_helper_exts32(cpu_env, fp, tmp);
                tcg_temp_free(tmp);
                break;
            case OS_LONG:
                tmp = tcg_const_i32(read_im32(env, s));
                gen_helper_exts32(cpu_env, fp, tmp);
                tcg_temp_free(tmp);
                break;
            case OS_SINGLE:
                tmp = tcg_const_i32(read_im32(env, s));
                gen_helper_extf32(cpu_env, fp, tmp);
                tcg_temp_free(tmp);
                break;
            case OS_DOUBLE:
                t64 = tcg_const_i64(read_im64(env, s));
                gen_helper_extf64(cpu_env, fp, t64);
                tcg_temp_free_i64(t64);
                break;
            case OS_EXTENDED:
                if (m68k_feature(s->env, M68K_FEATURE_CF_FPU)) {
                    gen_exception(s, s->base.pc_next, EXCP_FP_UNIMP);
                    break;
                }
                tmp = tcg_const_i32(read_im32(env, s) >> 16);
                tcg_gen_st16_i32(tmp, fp, offsetof(FPReg, l.upper));
                tcg_temp_free(tmp);
                t64 = tcg_const_i64(read_im64(env, s));
                tcg_gen_st_i64(t64, fp, offsetof(FPReg, l.lower));
                tcg_temp_free_i64(t64);
                break;
            case OS_PACKED:
                /*
                 * unimplemented data type on 68040/ColdFire
                 * FIXME if needed for another FPU
                 */
                gen_exception(s, s->base.pc_next, EXCP_FP_UNIMP);
                break;
            default:
                g_assert_not_reached();
            }
            return 0;
        default:
            return -1;
        }
    }
    return -1;
}

static int gen_ea_fp(CPUM68KState *env, DisasContext *s, uint16_t insn,
                       int opsize, TCGv_ptr fp, ea_what what, int index)
{
    int mode = extract32(insn, 3, 3);
    int reg0 = REG(insn, 0);
    return gen_ea_mode_fp(env, s, mode, reg0, opsize, fp, what, index);
}

typedef struct {
    TCGCond tcond;
    bool g1;
    bool g2;
    TCGv v1;
    TCGv v2;
} DisasCompare;

static void gen_cc_cond(DisasCompare *c, DisasContext *s, int cond)
{
    TCGv tmp, tmp2;
    TCGCond tcond;
    CCOp op = s->cc_op;

    /* The CC_OP_CMP form can handle most normal comparisons directly.  */
    if (op == CC_OP_CMPB || op == CC_OP_CMPW || op == CC_OP_CMPL) {
        c->g1 = c->g2 = 1;
        c->v1 = QREG_CC_N;
        c->v2 = QREG_CC_V;
        switch (cond) {
        case 2: /* HI */
        case 3: /* LS */
            tcond = TCG_COND_LEU;
            goto done;
        case 4: /* CC */
        case 5: /* CS */
            tcond = TCG_COND_LTU;
            goto done;
        case 6: /* NE */
        case 7: /* EQ */
            tcond = TCG_COND_EQ;
            goto done;
        case 10: /* PL */
        case 11: /* MI */
            c->g1 = c->g2 = 0;
            c->v2 = tcg_const_i32(0);
            c->v1 = tmp = tcg_temp_new();
            tcg_gen_sub_i32(tmp, QREG_CC_N, QREG_CC_V);
            gen_ext(tmp, tmp, op - CC_OP_CMPB, 1);
            /* fallthru */
        case 12: /* GE */
        case 13: /* LT */
            tcond = TCG_COND_LT;
            goto done;
        case 14: /* GT */
        case 15: /* LE */
            tcond = TCG_COND_LE;
            goto done;
        }
    }

    c->g1 = 1;
    c->g2 = 0;
    c->v2 = tcg_const_i32(0);

    switch (cond) {
    case 0: /* T */
    case 1: /* F */
        c->v1 = c->v2;
        tcond = TCG_COND_NEVER;
        goto done;
    case 14: /* GT (!(Z || (N ^ V))) */
    case 15: /* LE (Z || (N ^ V)) */
        /*
         * Logic operations clear V, which simplifies LE to (Z || N),
         * and since Z and N are co-located, this becomes a normal
         * comparison vs N.
         */
        if (op == CC_OP_LOGIC) {
            c->v1 = QREG_CC_N;
            tcond = TCG_COND_LE;
            goto done;
        }
        break;
    case 12: /* GE (!(N ^ V)) */
    case 13: /* LT (N ^ V) */
        /* Logic operations clear V, which simplifies this to N.  */
        if (op != CC_OP_LOGIC) {
            break;
        }
        /* fallthru */
    case 10: /* PL (!N) */
    case 11: /* MI (N) */
        /* Several cases represent N normally.  */
        if (op == CC_OP_ADDB || op == CC_OP_ADDW || op == CC_OP_ADDL ||
            op == CC_OP_SUBB || op == CC_OP_SUBW || op == CC_OP_SUBL ||
            op == CC_OP_LOGIC) {
            c->v1 = QREG_CC_N;
            tcond = TCG_COND_LT;
            goto done;
        }
        break;
    case 6: /* NE (!Z) */
    case 7: /* EQ (Z) */
        /* Some cases fold Z into N.  */
        if (op == CC_OP_ADDB || op == CC_OP_ADDW || op == CC_OP_ADDL ||
            op == CC_OP_SUBB || op == CC_OP_SUBW || op == CC_OP_SUBL ||
            op == CC_OP_LOGIC) {
            tcond = TCG_COND_EQ;
            c->v1 = QREG_CC_N;
            goto done;
        }
        break;
    case 4: /* CC (!C) */
    case 5: /* CS (C) */
        /* Some cases fold C into X.  */
        if (op == CC_OP_ADDB || op == CC_OP_ADDW || op == CC_OP_ADDL ||
            op == CC_OP_SUBB || op == CC_OP_SUBW || op == CC_OP_SUBL) {
            tcond = TCG_COND_NE;
            c->v1 = QREG_CC_X;
            goto done;
        }
        /* fallthru */
    case 8: /* VC (!V) */
    case 9: /* VS (V) */
        /* Logic operations clear V and C.  */
        if (op == CC_OP_LOGIC) {
            tcond = TCG_COND_NEVER;
            c->v1 = c->v2;
            goto done;
        }
        break;
    }

    /* Otherwise, flush flag state to CC_OP_FLAGS.  */
    gen_flush_flags(s);

    switch (cond) {
    case 0: /* T */
    case 1: /* F */
    default:
        /* Invalid, or handled above.  */
        abort();
    case 2: /* HI (!C && !Z) -> !(C || Z)*/
    case 3: /* LS (C || Z) */
        c->v1 = tmp = tcg_temp_new();
        c->g1 = 0;
        tcg_gen_setcond_i32(TCG_COND_EQ, tmp, QREG_CC_Z, c->v2);
        tcg_gen_or_i32(tmp, tmp, QREG_CC_C);
        tcond = TCG_COND_NE;
        break;
    case 4: /* CC (!C) */
    case 5: /* CS (C) */
        c->v1 = QREG_CC_C;
        tcond = TCG_COND_NE;
        break;
    case 6: /* NE (!Z) */
    case 7: /* EQ (Z) */
        c->v1 = QREG_CC_Z;
        tcond = TCG_COND_EQ;
        break;
    case 8: /* VC (!V) */
    case 9: /* VS (V) */
        c->v1 = QREG_CC_V;
        tcond = TCG_COND_LT;
        break;
    case 10: /* PL (!N) */
    case 11: /* MI (N) */
        c->v1 = QREG_CC_N;
        tcond = TCG_COND_LT;
        break;
    case 12: /* GE (!(N ^ V)) */
    case 13: /* LT (N ^ V) */
        c->v1 = tmp = tcg_temp_new();
        c->g1 = 0;
        tcg_gen_xor_i32(tmp, QREG_CC_N, QREG_CC_V);
        tcond = TCG_COND_LT;
        break;
    case 14: /* GT (!(Z || (N ^ V))) */
    case 15: /* LE (Z || (N ^ V)) */
        c->v1 = tmp = tcg_temp_new();
        c->g1 = 0;
        tcg_gen_setcond_i32(TCG_COND_EQ, tmp, QREG_CC_Z, c->v2);
        tcg_gen_neg_i32(tmp, tmp);
        tmp2 = tcg_temp_new();
        tcg_gen_xor_i32(tmp2, QREG_CC_N, QREG_CC_V);
        tcg_gen_or_i32(tmp, tmp, tmp2);
        tcg_temp_free(tmp2);
        tcond = TCG_COND_LT;
        break;
    }

 done:
    if ((cond & 1) == 0) {
        tcond = tcg_invert_cond(tcond);
    }
    c->tcond = tcond;
}

static void free_cond(DisasCompare *c)
{
    if (!c->g1) {
        tcg_temp_free(c->v1);
    }
    if (!c->g2) {
        tcg_temp_free(c->v2);
    }
}

static void gen_jmpcc(DisasContext *s, int cond, TCGLabel *l1)
{
  DisasCompare c;

  gen_cc_cond(&c, s, cond);
  update_cc_op(s);
  tcg_gen_brcond_i32(c.tcond, c.v1, c.v2, l1);
  free_cond(&c);
}

/* Force a TB lookup after an instruction that changes the CPU state.  */
static void gen_exit_tb(DisasContext *s)
{
    update_cc_op(s);
    tcg_gen_movi_i32(QREG_PC, s->pc);
    s->base.is_jmp = DISAS_EXIT;
}

#define SRC_EA(env, result, opsize, op_sign, addrp) do {                \
        result = gen_ea(env, s, insn, opsize, NULL_QREG, addrp,         \
                        op_sign ? EA_LOADS : EA_LOADU, IS_USER(s));     \
        if (IS_NULL_QREG(result)) {                                     \
            gen_addr_fault(s);                                          \
            return;                                                     \
        }                                                               \
    } while (0)

#define DEST_EA(env, insn, opsize, val, addrp) do {                     \
        TCGv ea_result = gen_ea(env, s, insn, opsize, val, addrp,       \
                                EA_STORE, IS_USER(s));                  \
        if (IS_NULL_QREG(ea_result)) {                                  \
            gen_addr_fault(s);                                          \
            return;                                                     \
        }                                                               \
    } while (0)

/* Generate a jump to an immediate address.  */
static void gen_jmp_tb(DisasContext *s, int n, target_ulong dest,
                       target_ulong src)
{
    if (unlikely(s->ss_active)) {
        update_cc_op(s);
        tcg_gen_movi_i32(QREG_PC, dest);
        gen_raise_exception_format2(s, EXCP_TRACE, src);
    } else if (translator_use_goto_tb(&s->base, dest)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_i32(QREG_PC, dest);
        tcg_gen_exit_tb(s->base.tb, n);
    } else {
        gen_jmp_im(s, dest);
        tcg_gen_exit_tb(NULL, 0);
    }
    s->base.is_jmp = DISAS_NORETURN;
}

DISAS_INSN(scc)
{
    DisasCompare c;
    int cond;
    TCGv tmp;

    cond = (insn >> 8) & 0xf;
    gen_cc_cond(&c, s, cond);

    tmp = tcg_temp_new();
    tcg_gen_setcond_i32(c.tcond, tmp, c.v1, c.v2);
    free_cond(&c);

    tcg_gen_neg_i32(tmp, tmp);
    DEST_EA(env, insn, OS_BYTE, tmp, NULL);
    tcg_temp_free(tmp);
}

DISAS_INSN(dbcc)
{
    TCGLabel *l1;
    TCGv reg;
    TCGv tmp;
    int16_t offset;
    uint32_t base;

    reg = DREG(insn, 0);
    base = s->pc;
    offset = (int16_t)read_im16(env, s);
    l1 = gen_new_label();
    gen_jmpcc(s, (insn >> 8) & 0xf, l1);

    tmp = tcg_temp_new();
    tcg_gen_ext16s_i32(tmp, reg);
    tcg_gen_addi_i32(tmp, tmp, -1);
    gen_partset_reg(OS_WORD, reg, tmp);
    tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, -1, l1);
    gen_jmp_tb(s, 1, base + offset, s->base.pc_next);
    gen_set_label(l1);
    gen_jmp_tb(s, 0, s->pc, s->base.pc_next);
}

DISAS_INSN(undef_mac)
{
    gen_exception(s, s->base.pc_next, EXCP_LINEA);
}

DISAS_INSN(undef_fpu)
{
    gen_exception(s, s->base.pc_next, EXCP_LINEF);
}

DISAS_INSN(undef)
{
    /*
     * ??? This is both instructions that are as yet unimplemented
     * for the 680x0 series, as well as those that are implemented
     * but actually illegal for CPU32 or pre-68020.
     */
    qemu_log_mask(LOG_UNIMP, "Illegal instruction: %04x @ %08x\n",
                  insn, s->base.pc_next);
    gen_exception(s, s->base.pc_next, EXCP_ILLEGAL);
}

DISAS_INSN(mulw)
{
    TCGv reg;
    TCGv tmp;
    TCGv src;
    int sign;

    sign = (insn & 0x100) != 0;
    reg = DREG(insn, 9);
    tmp = tcg_temp_new();
    if (sign)
        tcg_gen_ext16s_i32(tmp, reg);
    else
        tcg_gen_ext16u_i32(tmp, reg);
    SRC_EA(env, src, OS_WORD, sign, NULL);
    tcg_gen_mul_i32(tmp, tmp, src);
    tcg_gen_mov_i32(reg, tmp);
    gen_logic_cc(s, tmp, OS_LONG);
    tcg_temp_free(tmp);
}

DISAS_INSN(divw)
{
    int sign;
    TCGv src;
    TCGv destr;
    TCGv ilen;

    /* divX.w <EA>,Dn    32/16 -> 16r:16q */

    sign = (insn & 0x100) != 0;

    /* dest.l / src.w */

    SRC_EA(env, src, OS_WORD, sign, NULL);
    destr = tcg_constant_i32(REG(insn, 9));
    ilen = tcg_constant_i32(s->pc - s->base.pc_next);
    if (sign) {
        gen_helper_divsw(cpu_env, destr, src, ilen);
    } else {
        gen_helper_divuw(cpu_env, destr, src, ilen);
    }

    set_cc_op(s, CC_OP_FLAGS);
}

DISAS_INSN(divl)
{
    TCGv num, reg, den, ilen;
    int sign;
    uint16_t ext;

    ext = read_im16(env, s);

    sign = (ext & 0x0800) != 0;

    if (ext & 0x400) {
        if (!m68k_feature(s->env, M68K_FEATURE_QUAD_MULDIV)) {
            gen_exception(s, s->base.pc_next, EXCP_ILLEGAL);
            return;
        }

        /* divX.l <EA>, Dr:Dq    64/32 -> 32r:32q */

        SRC_EA(env, den, OS_LONG, 0, NULL);
        num = tcg_constant_i32(REG(ext, 12));
        reg = tcg_constant_i32(REG(ext, 0));
        ilen = tcg_constant_i32(s->pc - s->base.pc_next);
        if (sign) {
            gen_helper_divsll(cpu_env, num, reg, den, ilen);
        } else {
            gen_helper_divull(cpu_env, num, reg, den, ilen);
        }
        set_cc_op(s, CC_OP_FLAGS);
        return;
    }

    /* divX.l <EA>, Dq        32/32 -> 32q     */
    /* divXl.l <EA>, Dr:Dq    32/32 -> 32r:32q */

    SRC_EA(env, den, OS_LONG, 0, NULL);
    num = tcg_constant_i32(REG(ext, 12));
    reg = tcg_constant_i32(REG(ext, 0));
    ilen = tcg_constant_i32(s->pc - s->base.pc_next);
    if (sign) {
        gen_helper_divsl(cpu_env, num, reg, den, ilen);
    } else {
        gen_helper_divul(cpu_env, num, reg, den, ilen);
    }

    set_cc_op(s, CC_OP_FLAGS);
}

static void bcd_add(TCGv dest, TCGv src)
{
    TCGv t0, t1;

    /*
     * dest10 = dest10 + src10 + X
     *
     *        t1 = src
     *        t2 = t1 + 0x066
     *        t3 = t2 + dest + X
     *        t4 = t2 ^ dest
     *        t5 = t3 ^ t4
     *        t6 = ~t5 & 0x110
     *        t7 = (t6 >> 2) | (t6 >> 3)
     *        return t3 - t7
     */

    /*
     * t1 = (src + 0x066) + dest + X
     *    = result with some possible exceeding 0x6
     */

    t0 = tcg_const_i32(0x066);
    tcg_gen_add_i32(t0, t0, src);

    t1 = tcg_temp_new();
    tcg_gen_add_i32(t1, t0, dest);
    tcg_gen_add_i32(t1, t1, QREG_CC_X);

    /* we will remove exceeding 0x6 where there is no carry */

    /*
     * t0 = (src + 0x0066) ^ dest
     *    = t1 without carries
     */

    tcg_gen_xor_i32(t0, t0, dest);

    /*
     * extract the carries
     * t0 = t0 ^ t1
     *    = only the carries
     */

    tcg_gen_xor_i32(t0, t0, t1);

    /*
     * generate 0x1 where there is no carry
     * and for each 0x10, generate a 0x6
     */

    tcg_gen_shri_i32(t0, t0, 3);
    tcg_gen_not_i32(t0, t0);
    tcg_gen_andi_i32(t0, t0, 0x22);
    tcg_gen_add_i32(dest, t0, t0);
    tcg_gen_add_i32(dest, dest, t0);
    tcg_temp_free(t0);

    /*
     * remove the exceeding 0x6
     * for digits that have not generated a carry
     */

    tcg_gen_sub_i32(dest, t1, dest);
    tcg_temp_free(t1);
}

static void bcd_sub(TCGv dest, TCGv src)
{
    TCGv t0, t1, t2;

    /*
     *  dest10 = dest10 - src10 - X
     *         = bcd_add(dest + 1 - X, 0x199 - src)
     */

    /* t0 = 0x066 + (0x199 - src) */

    t0 = tcg_temp_new();
    tcg_gen_subfi_i32(t0, 0x1ff, src);

    /* t1 = t0 + dest + 1 - X*/

    t1 = tcg_temp_new();
    tcg_gen_add_i32(t1, t0, dest);
    tcg_gen_addi_i32(t1, t1, 1);
    tcg_gen_sub_i32(t1, t1, QREG_CC_X);

    /* t2 = t0 ^ dest */

    t2 = tcg_temp_new();
    tcg_gen_xor_i32(t2, t0, dest);

    /* t0 = t1 ^ t2 */

    tcg_gen_xor_i32(t0, t1, t2);

    /*
     * t2 = ~t0 & 0x110
     * t0 = (t2 >> 2) | (t2 >> 3)
     *
     * to fit on 8bit operands, changed in:
     *
     * t2 = ~(t0 >> 3) & 0x22
     * t0 = t2 + t2
     * t0 = t0 + t2
     */

    tcg_gen_shri_i32(t2, t0, 3);
    tcg_gen_not_i32(t2, t2);
    tcg_gen_andi_i32(t2, t2, 0x22);
    tcg_gen_add_i32(t0, t2, t2);
    tcg_gen_add_i32(t0, t0, t2);
    tcg_temp_free(t2);

    /* return t1 - t0 */

    tcg_gen_sub_i32(dest, t1, t0);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static void bcd_flags(TCGv val)
{
    tcg_gen_andi_i32(QREG_CC_C, val, 0x0ff);
    tcg_gen_or_i32(QREG_CC_Z, QREG_CC_Z, QREG_CC_C);

    tcg_gen_extract_i32(QREG_CC_C, val, 8, 1);

    tcg_gen_mov_i32(QREG_CC_X, QREG_CC_C);
}

DISAS_INSN(abcd_reg)
{
    TCGv src;
    TCGv dest;

    gen_flush_flags(s); /* !Z is sticky */

    src = gen_extend(s, DREG(insn, 0), OS_BYTE, 0);
    dest = gen_extend(s, DREG(insn, 9), OS_BYTE, 0);
    bcd_add(dest, src);
    gen_partset_reg(OS_BYTE, DREG(insn, 9), dest);

    bcd_flags(dest);
}

DISAS_INSN(abcd_mem)
{
    TCGv src, dest, addr;

    gen_flush_flags(s); /* !Z is sticky */

    /* Indirect pre-decrement load (mode 4) */

    src = gen_ea_mode(env, s, 4, REG(insn, 0), OS_BYTE,
                      NULL_QREG, NULL, EA_LOADU, IS_USER(s));
    dest = gen_ea_mode(env, s, 4, REG(insn, 9), OS_BYTE,
                       NULL_QREG, &addr, EA_LOADU, IS_USER(s));

    bcd_add(dest, src);

    gen_ea_mode(env, s, 4, REG(insn, 9), OS_BYTE, dest, &addr,
                EA_STORE, IS_USER(s));

    bcd_flags(dest);
}

DISAS_INSN(sbcd_reg)
{
    TCGv src, dest;

    gen_flush_flags(s); /* !Z is sticky */

    src = gen_extend(s, DREG(insn, 0), OS_BYTE, 0);
    dest = gen_extend(s, DREG(insn, 9), OS_BYTE, 0);

    bcd_sub(dest, src);

    gen_partset_reg(OS_BYTE, DREG(insn, 9), dest);

    bcd_flags(dest);
}

DISAS_INSN(sbcd_mem)
{
    TCGv src, dest, addr;

    gen_flush_flags(s); /* !Z is sticky */

    /* Indirect pre-decrement load (mode 4) */

    src = gen_ea_mode(env, s, 4, REG(insn, 0), OS_BYTE,
                      NULL_QREG, NULL, EA_LOADU, IS_USER(s));
    dest = gen_ea_mode(env, s, 4, REG(insn, 9), OS_BYTE,
                       NULL_QREG, &addr, EA_LOADU, IS_USER(s));

    bcd_sub(dest, src);

    gen_ea_mode(env, s, 4, REG(insn, 9), OS_BYTE, dest, &addr,
                EA_STORE, IS_USER(s));

    bcd_flags(dest);
}

DISAS_INSN(nbcd)
{
    TCGv src, dest;
    TCGv addr;

    gen_flush_flags(s); /* !Z is sticky */

    SRC_EA(env, src, OS_BYTE, 0, &addr);

    dest = tcg_const_i32(0);
    bcd_sub(dest, src);

    DEST_EA(env, insn, OS_BYTE, dest, &addr);

    bcd_flags(dest);

    tcg_temp_free(dest);
}

DISAS_INSN(addsub)
{
    TCGv reg;
    TCGv dest;
    TCGv src;
    TCGv tmp;
    TCGv addr;
    int add;
    int opsize;

    add = (insn & 0x4000) != 0;
    opsize = insn_opsize(insn);
    reg = gen_extend(s, DREG(insn, 9), opsize, 1);
    dest = tcg_temp_new();
    if (insn & 0x100) {
        SRC_EA(env, tmp, opsize, 1, &addr);
        src = reg;
    } else {
        tmp = reg;
        SRC_EA(env, src, opsize, 1, NULL);
    }
    if (add) {
        tcg_gen_add_i32(dest, tmp, src);
        tcg_gen_setcond_i32(TCG_COND_LTU, QREG_CC_X, dest, src);
        set_cc_op(s, CC_OP_ADDB + opsize);
    } else {
        tcg_gen_setcond_i32(TCG_COND_LTU, QREG_CC_X, tmp, src);
        tcg_gen_sub_i32(dest, tmp, src);
        set_cc_op(s, CC_OP_SUBB + opsize);
    }
    gen_update_cc_add(dest, src, opsize);
    if (insn & 0x100) {
        DEST_EA(env, insn, opsize, dest, &addr);
    } else {
        gen_partset_reg(opsize, DREG(insn, 9), dest);
    }
    tcg_temp_free(dest);
}

/* Reverse the order of the bits in REG.  */
DISAS_INSN(bitrev)
{
    TCGv reg;
    reg = DREG(insn, 0);
    gen_helper_bitrev(reg, reg);
}

DISAS_INSN(bitop_reg)
{
    int opsize;
    int op;
    TCGv src1;
    TCGv src2;
    TCGv tmp;
    TCGv addr;
    TCGv dest;

    if ((insn & 0x38) != 0)
        opsize = OS_BYTE;
    else
        opsize = OS_LONG;
    op = (insn >> 6) & 3;
    SRC_EA(env, src1, opsize, 0, op ? &addr: NULL);

    gen_flush_flags(s);
    src2 = tcg_temp_new();
    if (opsize == OS_BYTE)
        tcg_gen_andi_i32(src2, DREG(insn, 9), 7);
    else
        tcg_gen_andi_i32(src2, DREG(insn, 9), 31);

    tmp = tcg_const_i32(1);
    tcg_gen_shl_i32(tmp, tmp, src2);
    tcg_temp_free(src2);

    tcg_gen_and_i32(QREG_CC_Z, src1, tmp);

    dest = tcg_temp_new();
    switch (op) {
    case 1: /* bchg */
        tcg_gen_xor_i32(dest, src1, tmp);
        break;
    case 2: /* bclr */
        tcg_gen_andc_i32(dest, src1, tmp);
        break;
    case 3: /* bset */
        tcg_gen_or_i32(dest, src1, tmp);
        break;
    default: /* btst */
        break;
    }
    tcg_temp_free(tmp);
    if (op) {
        DEST_EA(env, insn, opsize, dest, &addr);
    }
    tcg_temp_free(dest);
}

DISAS_INSN(sats)
{
    TCGv reg;
    reg = DREG(insn, 0);
    gen_flush_flags(s);
    gen_helper_sats(reg, reg, QREG_CC_V);
    gen_logic_cc(s, reg, OS_LONG);
}

static void gen_push(DisasContext *s, TCGv val)
{
    TCGv tmp;

    tmp = tcg_temp_new();
    tcg_gen_subi_i32(tmp, QREG_SP, 4);
    gen_store(s, OS_LONG, tmp, val, IS_USER(s));
    tcg_gen_mov_i32(QREG_SP, tmp);
    tcg_temp_free(tmp);
}

static TCGv mreg(int reg)
{
    if (reg < 8) {
        /* Dx */
        return cpu_dregs[reg];
    }
    /* Ax */
    return cpu_aregs[reg & 7];
}

DISAS_INSN(movem)
{
    TCGv addr, incr, tmp, r[16];
    int is_load = (insn & 0x0400) != 0;
    int opsize = (insn & 0x40) != 0 ? OS_LONG : OS_WORD;
    uint16_t mask = read_im16(env, s);
    int mode = extract32(insn, 3, 3);
    int reg0 = REG(insn, 0);
    int i;

    tmp = cpu_aregs[reg0];

    switch (mode) {
    case 0: /* data register direct */
    case 1: /* addr register direct */
    do_addr_fault:
        gen_addr_fault(s);
        return;

    case 2: /* indirect */
        break;

    case 3: /* indirect post-increment */
        if (!is_load) {
            /* post-increment is not allowed */
            goto do_addr_fault;
        }
        break;

    case 4: /* indirect pre-decrement */
        if (is_load) {
            /* pre-decrement is not allowed */
            goto do_addr_fault;
        }
        /*
         * We want a bare copy of the address reg, without any pre-decrement
         * adjustment, as gen_lea would provide.
         */
        break;

    default:
        tmp = gen_lea_mode(env, s, mode, reg0, opsize);
        if (IS_NULL_QREG(tmp)) {
            goto do_addr_fault;
        }
        break;
    }

    addr = tcg_temp_new();
    tcg_gen_mov_i32(addr, tmp);
    incr = tcg_const_i32(opsize_bytes(opsize));

    if (is_load) {
        /* memory to register */
        for (i = 0; i < 16; i++) {
            if (mask & (1 << i)) {
                r[i] = gen_load(s, opsize, addr, 1, IS_USER(s));
                tcg_gen_add_i32(addr, addr, incr);
            }
        }
        for (i = 0; i < 16; i++) {
            if (mask & (1 << i)) {
                tcg_gen_mov_i32(mreg(i), r[i]);
                tcg_temp_free(r[i]);
            }
        }
        if (mode == 3) {
            /* post-increment: movem (An)+,X */
            tcg_gen_mov_i32(cpu_aregs[reg0], addr);
        }
    } else {
        /* register to memory */
        if (mode == 4) {
            /* pre-decrement: movem X,-(An) */
            for (i = 15; i >= 0; i--) {
                if ((mask << i) & 0x8000) {
                    tcg_gen_sub_i32(addr, addr, incr);
                    if (reg0 + 8 == i &&
                        m68k_feature(s->env, M68K_FEATURE_EXT_FULL)) {
                        /*
                         * M68020+: if the addressing register is the
                         * register moved to memory, the value written
                         * is the initial value decremented by the size of
                         * the operation, regardless of how many actual
                         * stores have been performed until this point.
                         * M68000/M68010: the value is the initial value.
                         */
                        tmp = tcg_temp_new();
                        tcg_gen_sub_i32(tmp, cpu_aregs[reg0], incr);
                        gen_store(s, opsize, addr, tmp, IS_USER(s));
                        tcg_temp_free(tmp);
                    } else {
                        gen_store(s, opsize, addr, mreg(i), IS_USER(s));
                    }
                }
            }
            tcg_gen_mov_i32(cpu_aregs[reg0], addr);
        } else {
            for (i = 0; i < 16; i++) {
                if (mask & (1 << i)) {
                    gen_store(s, opsize, addr, mreg(i), IS_USER(s));
                    tcg_gen_add_i32(addr, addr, incr);
                }
            }
        }
    }

    tcg_temp_free(incr);
    tcg_temp_free(addr);
}

DISAS_INSN(movep)
{
    uint8_t i;
    int16_t displ;
    TCGv reg;
    TCGv addr;
    TCGv abuf;
    TCGv dbuf;

    displ = read_im16(env, s);

    addr = AREG(insn, 0);
    reg = DREG(insn, 9);

    abuf = tcg_temp_new();
    tcg_gen_addi_i32(abuf, addr, displ);
    dbuf = tcg_temp_new();

    if (insn & 0x40) {
        i = 4;
    } else {
        i = 2;
    }

    if (insn & 0x80) {
        for ( ; i > 0 ; i--) {
            tcg_gen_shri_i32(dbuf, reg, (i - 1) * 8);
            tcg_gen_qemu_st8(dbuf, abuf, IS_USER(s));
            if (i > 1) {
                tcg_gen_addi_i32(abuf, abuf, 2);
            }
        }
    } else {
        for ( ; i > 0 ; i--) {
            tcg_gen_qemu_ld8u(dbuf, abuf, IS_USER(s));
            tcg_gen_deposit_i32(reg, reg, dbuf, (i - 1) * 8, 8);
            if (i > 1) {
                tcg_gen_addi_i32(abuf, abuf, 2);
            }
        }
    }
    tcg_temp_free(abuf);
    tcg_temp_free(dbuf);
}

DISAS_INSN(bitop_im)
{
    int opsize;
    int op;
    TCGv src1;
    uint32_t mask;
    int bitnum;
    TCGv tmp;
    TCGv addr;

    if ((insn & 0x38) != 0)
        opsize = OS_BYTE;
    else
        opsize = OS_LONG;
    op = (insn >> 6) & 3;

    bitnum = read_im16(env, s);
    if (m68k_feature(s->env, M68K_FEATURE_M68K)) {
        if (bitnum & 0xfe00) {
            disas_undef(env, s, insn);
            return;
        }
    } else {
        if (bitnum & 0xff00) {
            disas_undef(env, s, insn);
            return;
        }
    }

    SRC_EA(env, src1, opsize, 0, op ? &addr: NULL);

    gen_flush_flags(s);
    if (opsize == OS_BYTE)
        bitnum &= 7;
    else
        bitnum &= 31;
    mask = 1 << bitnum;

   tcg_gen_andi_i32(QREG_CC_Z, src1, mask);

    if (op) {
        tmp = tcg_temp_new();
        switch (op) {
        case 1: /* bchg */
            tcg_gen_xori_i32(tmp, src1, mask);
            break;
        case 2: /* bclr */
            tcg_gen_andi_i32(tmp, src1, ~mask);
            break;
        case 3: /* bset */
            tcg_gen_ori_i32(tmp, src1, mask);
            break;
        default: /* btst */
            break;
        }
        DEST_EA(env, insn, opsize, tmp, &addr);
        tcg_temp_free(tmp);
    }
}

static TCGv gen_get_ccr(DisasContext *s)
{
    TCGv dest;

    update_cc_op(s);
    dest = tcg_temp_new();
    gen_helper_get_ccr(dest, cpu_env);
    return dest;
}

static TCGv gen_get_sr(DisasContext *s)
{
    TCGv ccr;
    TCGv sr;

    ccr = gen_get_ccr(s);
    sr = tcg_temp_new();
    tcg_gen_andi_i32(sr, QREG_SR, 0xffe0);
    tcg_gen_or_i32(sr, sr, ccr);
    tcg_temp_free(ccr);
    return sr;
}

static void gen_set_sr_im(DisasContext *s, uint16_t val, int ccr_only)
{
    if (ccr_only) {
        tcg_gen_movi_i32(QREG_CC_C, val & CCF_C ? 1 : 0);
        tcg_gen_movi_i32(QREG_CC_V, val & CCF_V ? -1 : 0);
        tcg_gen_movi_i32(QREG_CC_Z, val & CCF_Z ? 0 : 1);
        tcg_gen_movi_i32(QREG_CC_N, val & CCF_N ? -1 : 0);
        tcg_gen_movi_i32(QREG_CC_X, val & CCF_X ? 1 : 0);
    } else {
        /* Must writeback before changing security state. */
        do_writebacks(s);
        gen_helper_set_sr(cpu_env, tcg_constant_i32(val));
    }
    set_cc_op(s, CC_OP_FLAGS);
}

static void gen_set_sr(DisasContext *s, TCGv val, int ccr_only)
{
    if (ccr_only) {
        gen_helper_set_ccr(cpu_env, val);
    } else {
        /* Must writeback before changing security state. */
        do_writebacks(s);
        gen_helper_set_sr(cpu_env, val);
    }
    set_cc_op(s, CC_OP_FLAGS);
}

static void gen_move_to_sr(CPUM68KState *env, DisasContext *s, uint16_t insn,
                           bool ccr_only)
{
    if ((insn & 0x3f) == 0x3c) {
        uint16_t val;
        val = read_im16(env, s);
        gen_set_sr_im(s, val, ccr_only);
    } else {
        TCGv src;
        SRC_EA(env, src, OS_WORD, 0, NULL);
        gen_set_sr(s, src, ccr_only);
    }
}

DISAS_INSN(arith_im)
{
    int op;
    TCGv im;
    TCGv src1;
    TCGv dest;
    TCGv addr;
    int opsize;
    bool with_SR = ((insn & 0x3f) == 0x3c);

    op = (insn >> 9) & 7;
    opsize = insn_opsize(insn);
    switch (opsize) {
    case OS_BYTE:
        im = tcg_const_i32((int8_t)read_im8(env, s));
        break;
    case OS_WORD:
        im = tcg_const_i32((int16_t)read_im16(env, s));
        break;
    case OS_LONG:
        im = tcg_const_i32(read_im32(env, s));
        break;
    default:
        g_assert_not_reached();
    }

    if (with_SR) {
        /* SR/CCR can only be used with andi/eori/ori */
        if (op == 2 || op == 3 || op == 6) {
            disas_undef(env, s, insn);
            return;
        }
        switch (opsize) {
        case OS_BYTE:
            src1 = gen_get_ccr(s);
            break;
        case OS_WORD:
            if (IS_USER(s)) {
                gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
                return;
            }
            src1 = gen_get_sr(s);
            break;
        default:
            /* OS_LONG; others already g_assert_not_reached.  */
            disas_undef(env, s, insn);
            return;
        }
    } else {
        SRC_EA(env, src1, opsize, 1, (op == 6) ? NULL : &addr);
    }
    dest = tcg_temp_new();
    switch (op) {
    case 0: /* ori */
        tcg_gen_or_i32(dest, src1, im);
        if (with_SR) {
            gen_set_sr(s, dest, opsize == OS_BYTE);
            gen_exit_tb(s);
        } else {
            DEST_EA(env, insn, opsize, dest, &addr);
            gen_logic_cc(s, dest, opsize);
        }
        break;
    case 1: /* andi */
        tcg_gen_and_i32(dest, src1, im);
        if (with_SR) {
            gen_set_sr(s, dest, opsize == OS_BYTE);
            gen_exit_tb(s);
        } else {
            DEST_EA(env, insn, opsize, dest, &addr);
            gen_logic_cc(s, dest, opsize);
        }
        break;
    case 2: /* subi */
        tcg_gen_setcond_i32(TCG_COND_LTU, QREG_CC_X, src1, im);
        tcg_gen_sub_i32(dest, src1, im);
        gen_update_cc_add(dest, im, opsize);
        set_cc_op(s, CC_OP_SUBB + opsize);
        DEST_EA(env, insn, opsize, dest, &addr);
        break;
    case 3: /* addi */
        tcg_gen_add_i32(dest, src1, im);
        gen_update_cc_add(dest, im, opsize);
        tcg_gen_setcond_i32(TCG_COND_LTU, QREG_CC_X, dest, im);
        set_cc_op(s, CC_OP_ADDB + opsize);
        DEST_EA(env, insn, opsize, dest, &addr);
        break;
    case 5: /* eori */
        tcg_gen_xor_i32(dest, src1, im);
        if (with_SR) {
            gen_set_sr(s, dest, opsize == OS_BYTE);
            gen_exit_tb(s);
        } else {
            DEST_EA(env, insn, opsize, dest, &addr);
            gen_logic_cc(s, dest, opsize);
        }
        break;
    case 6: /* cmpi */
        gen_update_cc_cmp(s, src1, im, opsize);
        break;
    default:
        abort();
    }
    tcg_temp_free(im);
    tcg_temp_free(dest);
}

DISAS_INSN(cas)
{
    int opsize;
    TCGv addr;
    uint16_t ext;
    TCGv load;
    TCGv cmp;
    MemOp opc;

    switch ((insn >> 9) & 3) {
    case 1:
        opsize = OS_BYTE;
        opc = MO_SB;
        break;
    case 2:
        opsize = OS_WORD;
        opc = MO_TESW;
        break;
    case 3:
        opsize = OS_LONG;
        opc = MO_TESL;
        break;
    default:
        g_assert_not_reached();
    }

    ext = read_im16(env, s);

    /* cas Dc,Du,<EA> */

    addr = gen_lea(env, s, insn, opsize);
    if (IS_NULL_QREG(addr)) {
        gen_addr_fault(s);
        return;
    }

    cmp = gen_extend(s, DREG(ext, 0), opsize, 1);

    /*
     * if  <EA> == Dc then
     *     <EA> = Du
     *     Dc = <EA> (because <EA> == Dc)
     * else
     *     Dc = <EA>
     */

    load = tcg_temp_new();
    tcg_gen_atomic_cmpxchg_i32(load, addr, cmp, DREG(ext, 6),
                               IS_USER(s), opc);
    /* update flags before setting cmp to load */
    gen_update_cc_cmp(s, load, cmp, opsize);
    gen_partset_reg(opsize, DREG(ext, 0), load);

    tcg_temp_free(load);

    switch (extract32(insn, 3, 3)) {
    case 3: /* Indirect postincrement.  */
        tcg_gen_addi_i32(AREG(insn, 0), addr, opsize_bytes(opsize));
        break;
    case 4: /* Indirect predecrememnt.  */
        tcg_gen_mov_i32(AREG(insn, 0), addr);
        break;
    }
}

DISAS_INSN(cas2w)
{
    uint16_t ext1, ext2;
    TCGv addr1, addr2;
    TCGv regs;

    /* cas2 Dc1:Dc2,Du1:Du2,(Rn1):(Rn2) */

    ext1 = read_im16(env, s);

    if (ext1 & 0x8000) {
        /* Address Register */
        addr1 = AREG(ext1, 12);
    } else {
        /* Data Register */
        addr1 = DREG(ext1, 12);
    }

    ext2 = read_im16(env, s);
    if (ext2 & 0x8000) {
        /* Address Register */
        addr2 = AREG(ext2, 12);
    } else {
        /* Data Register */
        addr2 = DREG(ext2, 12);
    }

    /*
     * if (R1) == Dc1 && (R2) == Dc2 then
     *     (R1) = Du1
     *     (R2) = Du2
     * else
     *     Dc1 = (R1)
     *     Dc2 = (R2)
     */

    regs = tcg_const_i32(REG(ext2, 6) |
                         (REG(ext1, 6) << 3) |
                         (REG(ext2, 0) << 6) |
                         (REG(ext1, 0) << 9));
    if (tb_cflags(s->base.tb) & CF_PARALLEL) {
        gen_helper_exit_atomic(cpu_env);
    } else {
        gen_helper_cas2w(cpu_env, regs, addr1, addr2);
    }
    tcg_temp_free(regs);

    /* Note that cas2w also assigned to env->cc_op.  */
    s->cc_op = CC_OP_CMPW;
    s->cc_op_synced = 1;
}

DISAS_INSN(cas2l)
{
    uint16_t ext1, ext2;
    TCGv addr1, addr2, regs;

    /* cas2 Dc1:Dc2,Du1:Du2,(Rn1):(Rn2) */

    ext1 = read_im16(env, s);

    if (ext1 & 0x8000) {
        /* Address Register */
        addr1 = AREG(ext1, 12);
    } else {
        /* Data Register */
        addr1 = DREG(ext1, 12);
    }

    ext2 = read_im16(env, s);
    if (ext2 & 0x8000) {
        /* Address Register */
        addr2 = AREG(ext2, 12);
    } else {
        /* Data Register */
        addr2 = DREG(ext2, 12);
    }

    /*
     * if (R1) == Dc1 && (R2) == Dc2 then
     *     (R1) = Du1
     *     (R2) = Du2
     * else
     *     Dc1 = (R1)
     *     Dc2 = (R2)
     */

    regs = tcg_const_i32(REG(ext2, 6) |
                         (REG(ext1, 6) << 3) |
                         (REG(ext2, 0) << 6) |
                         (REG(ext1, 0) << 9));
    if (tb_cflags(s->base.tb) & CF_PARALLEL) {
        gen_helper_cas2l_parallel(cpu_env, regs, addr1, addr2);
    } else {
        gen_helper_cas2l(cpu_env, regs, addr1, addr2);
    }
    tcg_temp_free(regs);

    /* Note that cas2l also assigned to env->cc_op.  */
    s->cc_op = CC_OP_CMPL;
    s->cc_op_synced = 1;
}

DISAS_INSN(byterev)
{
    TCGv reg;

    reg = DREG(insn, 0);
    tcg_gen_bswap32_i32(reg, reg);
}

DISAS_INSN(move)
{
    TCGv src;
    TCGv dest;
    int op;
    int opsize;

    switch (insn >> 12) {
    case 1: /* move.b */
        opsize = OS_BYTE;
        break;
    case 2: /* move.l */
        opsize = OS_LONG;
        break;
    case 3: /* move.w */
        opsize = OS_WORD;
        break;
    default:
        abort();
    }
    SRC_EA(env, src, opsize, 1, NULL);
    op = (insn >> 6) & 7;
    if (op == 1) {
        /* movea */
        /* The value will already have been sign extended.  */
        dest = AREG(insn, 9);
        tcg_gen_mov_i32(dest, src);
    } else {
        /* normal move */
        uint16_t dest_ea;
        dest_ea = ((insn >> 9) & 7) | (op << 3);
        DEST_EA(env, dest_ea, opsize, src, NULL);
        /* This will be correct because loads sign extend.  */
        gen_logic_cc(s, src, opsize);
    }
}

DISAS_INSN(negx)
{
    TCGv z;
    TCGv src;
    TCGv addr;
    int opsize;

    opsize = insn_opsize(insn);
    SRC_EA(env, src, opsize, 1, &addr);

    gen_flush_flags(s); /* compute old Z */

    /*
     * Perform subtract with borrow.
     * (X, N) =  -(src + X);
     */

    z = tcg_const_i32(0);
    tcg_gen_add2_i32(QREG_CC_N, QREG_CC_X, src, z, QREG_CC_X, z);
    tcg_gen_sub2_i32(QREG_CC_N, QREG_CC_X, z, z, QREG_CC_N, QREG_CC_X);
    tcg_temp_free(z);
    gen_ext(QREG_CC_N, QREG_CC_N, opsize, 1);

    tcg_gen_andi_i32(QREG_CC_X, QREG_CC_X, 1);

    /*
     * Compute signed-overflow for negation.  The normal formula for
     * subtraction is (res ^ src) & (src ^ dest), but with dest==0
     * this simplifies to res & src.
     */

    tcg_gen_and_i32(QREG_CC_V, QREG_CC_N, src);

    /* Copy the rest of the results into place.  */
    tcg_gen_or_i32(QREG_CC_Z, QREG_CC_Z, QREG_CC_N); /* !Z is sticky */
    tcg_gen_mov_i32(QREG_CC_C, QREG_CC_X);

    set_cc_op(s, CC_OP_FLAGS);

    /* result is in QREG_CC_N */

    DEST_EA(env, insn, opsize, QREG_CC_N, &addr);
}

DISAS_INSN(lea)
{
    TCGv reg;
    TCGv tmp;

    reg = AREG(insn, 9);
    tmp = gen_lea(env, s, insn, OS_LONG);
    if (IS_NULL_QREG(tmp)) {
        gen_addr_fault(s);
        return;
    }
    tcg_gen_mov_i32(reg, tmp);
}

DISAS_INSN(clr)
{
    int opsize;
    TCGv zero;

    zero = tcg_const_i32(0);

    opsize = insn_opsize(insn);
    DEST_EA(env, insn, opsize, zero, NULL);
    gen_logic_cc(s, zero, opsize);
    tcg_temp_free(zero);
}

DISAS_INSN(move_from_ccr)
{
    TCGv ccr;

    ccr = gen_get_ccr(s);
    DEST_EA(env, insn, OS_WORD, ccr, NULL);
}

DISAS_INSN(neg)
{
    TCGv src1;
    TCGv dest;
    TCGv addr;
    int opsize;

    opsize = insn_opsize(insn);
    SRC_EA(env, src1, opsize, 1, &addr);
    dest = tcg_temp_new();
    tcg_gen_neg_i32(dest, src1);
    set_cc_op(s, CC_OP_SUBB + opsize);
    gen_update_cc_add(dest, src1, opsize);
    tcg_gen_setcondi_i32(TCG_COND_NE, QREG_CC_X, dest, 0);
    DEST_EA(env, insn, opsize, dest, &addr);
    tcg_temp_free(dest);
}

DISAS_INSN(move_to_ccr)
{
    gen_move_to_sr(env, s, insn, true);
}

DISAS_INSN(not)
{
    TCGv src1;
    TCGv dest;
    TCGv addr;
    int opsize;

    opsize = insn_opsize(insn);
    SRC_EA(env, src1, opsize, 1, &addr);
    dest = tcg_temp_new();
    tcg_gen_not_i32(dest, src1);
    DEST_EA(env, insn, opsize, dest, &addr);
    gen_logic_cc(s, dest, opsize);
}

DISAS_INSN(swap)
{
    TCGv src1;
    TCGv src2;
    TCGv reg;

    src1 = tcg_temp_new();
    src2 = tcg_temp_new();
    reg = DREG(insn, 0);
    tcg_gen_shli_i32(src1, reg, 16);
    tcg_gen_shri_i32(src2, reg, 16);
    tcg_gen_or_i32(reg, src1, src2);
    tcg_temp_free(src2);
    tcg_temp_free(src1);
    gen_logic_cc(s, reg, OS_LONG);
}

DISAS_INSN(bkpt)
{
#if defined(CONFIG_SOFTMMU)
    gen_exception(s, s->base.pc_next, EXCP_ILLEGAL);
#else
    gen_exception(s, s->base.pc_next, EXCP_DEBUG);
#endif
}

DISAS_INSN(pea)
{
    TCGv tmp;

    tmp = gen_lea(env, s, insn, OS_LONG);
    if (IS_NULL_QREG(tmp)) {
        gen_addr_fault(s);
        return;
    }
    gen_push(s, tmp);
}

DISAS_INSN(ext)
{
    int op;
    TCGv reg;
    TCGv tmp;

    reg = DREG(insn, 0);
    op = (insn >> 6) & 7;
    tmp = tcg_temp_new();
    if (op == 3)
        tcg_gen_ext16s_i32(tmp, reg);
    else
        tcg_gen_ext8s_i32(tmp, reg);
    if (op == 2)
        gen_partset_reg(OS_WORD, reg, tmp);
    else
        tcg_gen_mov_i32(reg, tmp);
    gen_logic_cc(s, tmp, OS_LONG);
    tcg_temp_free(tmp);
}

DISAS_INSN(tst)
{
    int opsize;
    TCGv tmp;

    opsize = insn_opsize(insn);
    SRC_EA(env, tmp, opsize, 1, NULL);
    gen_logic_cc(s, tmp, opsize);
}

DISAS_INSN(pulse)
{
  /* Implemented as a NOP.  */
}

DISAS_INSN(illegal)
{
    gen_exception(s, s->base.pc_next, EXCP_ILLEGAL);
}

DISAS_INSN(tas)
{
    int mode = extract32(insn, 3, 3);
    int reg0 = REG(insn, 0);

    if (mode == 0) {
        /* data register direct */
        TCGv dest = cpu_dregs[reg0];
        gen_logic_cc(s, dest, OS_BYTE);
        tcg_gen_ori_tl(dest, dest, 0x80);
    } else {
        TCGv src1, addr;

        addr = gen_lea_mode(env, s, mode, reg0, OS_BYTE);
        if (IS_NULL_QREG(addr)) {
            gen_addr_fault(s);
            return;
        }
        src1 = tcg_temp_new();
        tcg_gen_atomic_fetch_or_tl(src1, addr, tcg_constant_tl(0x80),
                                   IS_USER(s), MO_SB);
        gen_logic_cc(s, src1, OS_BYTE);
        tcg_temp_free(src1);

        switch (mode) {
        case 3: /* Indirect postincrement.  */
            tcg_gen_addi_i32(AREG(insn, 0), addr, 1);
            break;
        case 4: /* Indirect predecrememnt.  */
            tcg_gen_mov_i32(AREG(insn, 0), addr);
            break;
        }
    }
}

DISAS_INSN(mull)
{
    uint16_t ext;
    TCGv src1;
    int sign;

    ext = read_im16(env, s);

    sign = ext & 0x800;

    if (ext & 0x400) {
        if (!m68k_feature(s->env, M68K_FEATURE_QUAD_MULDIV)) {
            gen_exception(s, s->base.pc_next, EXCP_ILLEGAL);
            return;
        }

        SRC_EA(env, src1, OS_LONG, 0, NULL);

        if (sign) {
            tcg_gen_muls2_i32(QREG_CC_Z, QREG_CC_N, src1, DREG(ext, 12));
        } else {
            tcg_gen_mulu2_i32(QREG_CC_Z, QREG_CC_N, src1, DREG(ext, 12));
        }
        /* if Dl == Dh, 68040 returns low word */
        tcg_gen_mov_i32(DREG(ext, 0), QREG_CC_N);
        tcg_gen_mov_i32(DREG(ext, 12), QREG_CC_Z);
        tcg_gen_or_i32(QREG_CC_Z, QREG_CC_Z, QREG_CC_N);

        tcg_gen_movi_i32(QREG_CC_V, 0);
        tcg_gen_movi_i32(QREG_CC_C, 0);

        set_cc_op(s, CC_OP_FLAGS);
        return;
    }
    SRC_EA(env, src1, OS_LONG, 0, NULL);
    if (m68k_feature(s->env, M68K_FEATURE_M68K)) {
        tcg_gen_movi_i32(QREG_CC_C, 0);
        if (sign) {
            tcg_gen_muls2_i32(QREG_CC_N, QREG_CC_V, src1, DREG(ext, 12));
            /* QREG_CC_V is -(QREG_CC_V != (QREG_CC_N >> 31)) */
            tcg_gen_sari_i32(QREG_CC_Z, QREG_CC_N, 31);
            tcg_gen_setcond_i32(TCG_COND_NE, QREG_CC_V, QREG_CC_V, QREG_CC_Z);
        } else {
            tcg_gen_mulu2_i32(QREG_CC_N, QREG_CC_V, src1, DREG(ext, 12));
            /* QREG_CC_V is -(QREG_CC_V != 0), use QREG_CC_C as 0 */
            tcg_gen_setcond_i32(TCG_COND_NE, QREG_CC_V, QREG_CC_V, QREG_CC_C);
        }
        tcg_gen_neg_i32(QREG_CC_V, QREG_CC_V);
        tcg_gen_mov_i32(DREG(ext, 12), QREG_CC_N);

        tcg_gen_mov_i32(QREG_CC_Z, QREG_CC_N);

        set_cc_op(s, CC_OP_FLAGS);
    } else {
        /*
         * The upper 32 bits of the product are discarded, so
         * muls.l and mulu.l are functionally equivalent.
         */
        tcg_gen_mul_i32(DREG(ext, 12), src1, DREG(ext, 12));
        gen_logic_cc(s, DREG(ext, 12), OS_LONG);
    }
}

static void gen_link(DisasContext *s, uint16_t insn, int32_t offset)
{
    TCGv reg;
    TCGv tmp;

    reg = AREG(insn, 0);
    tmp = tcg_temp_new();
    tcg_gen_subi_i32(tmp, QREG_SP, 4);
    gen_store(s, OS_LONG, tmp, reg, IS_USER(s));
    if ((insn & 7) != 7) {
        tcg_gen_mov_i32(reg, tmp);
    }
    tcg_gen_addi_i32(QREG_SP, tmp, offset);
    tcg_temp_free(tmp);
}

DISAS_INSN(link)
{
    int16_t offset;

    offset = read_im16(env, s);
    gen_link(s, insn, offset);
}

DISAS_INSN(linkl)
{
    int32_t offset;

    offset = read_im32(env, s);
    gen_link(s, insn, offset);
}

DISAS_INSN(unlk)
{
    TCGv src;
    TCGv reg;
    TCGv tmp;

    src = tcg_temp_new();
    reg = AREG(insn, 0);
    tcg_gen_mov_i32(src, reg);
    tmp = gen_load(s, OS_LONG, src, 0, IS_USER(s));
    tcg_gen_mov_i32(reg, tmp);
    tcg_gen_addi_i32(QREG_SP, src, 4);
    tcg_temp_free(src);
    tcg_temp_free(tmp);
}

#if defined(CONFIG_SOFTMMU)
DISAS_INSN(reset)
{
    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }

    gen_helper_reset(cpu_env);
}
#endif

DISAS_INSN(nop)
{
}

DISAS_INSN(rtd)
{
    TCGv tmp;
    int16_t offset = read_im16(env, s);

    tmp = gen_load(s, OS_LONG, QREG_SP, 0, IS_USER(s));
    tcg_gen_addi_i32(QREG_SP, QREG_SP, offset + 4);
    gen_jmp(s, tmp);
}

DISAS_INSN(rtr)
{
    TCGv tmp;
    TCGv ccr;
    TCGv sp;

    sp = tcg_temp_new();
    ccr = gen_load(s, OS_WORD, QREG_SP, 0, IS_USER(s));
    tcg_gen_addi_i32(sp, QREG_SP, 2);
    tmp = gen_load(s, OS_LONG, sp, 0, IS_USER(s));
    tcg_gen_addi_i32(QREG_SP, sp, 4);
    tcg_temp_free(sp);

    gen_set_sr(s, ccr, true);
    tcg_temp_free(ccr);

    gen_jmp(s, tmp);
}

DISAS_INSN(rts)
{
    TCGv tmp;

    tmp = gen_load(s, OS_LONG, QREG_SP, 0, IS_USER(s));
    tcg_gen_addi_i32(QREG_SP, QREG_SP, 4);
    gen_jmp(s, tmp);
}

DISAS_INSN(jump)
{
    TCGv tmp;

    /*
     * Load the target address first to ensure correct exception
     * behavior.
     */
    tmp = gen_lea(env, s, insn, OS_LONG);
    if (IS_NULL_QREG(tmp)) {
        gen_addr_fault(s);
        return;
    }
    if ((insn & 0x40) == 0) {
        /* jsr */
        gen_push(s, tcg_const_i32(s->pc));
    }
    gen_jmp(s, tmp);
}

DISAS_INSN(addsubq)
{
    TCGv src;
    TCGv dest;
    TCGv val;
    int imm;
    TCGv addr;
    int opsize;

    if ((insn & 070) == 010) {
        /* Operation on address register is always long.  */
        opsize = OS_LONG;
    } else {
        opsize = insn_opsize(insn);
    }
    SRC_EA(env, src, opsize, 1, &addr);
    imm = (insn >> 9) & 7;
    if (imm == 0) {
        imm = 8;
    }
    val = tcg_const_i32(imm);
    dest = tcg_temp_new();
    tcg_gen_mov_i32(dest, src);
    if ((insn & 0x38) == 0x08) {
        /*
         * Don't update condition codes if the destination is an
         * address register.
         */
        if (insn & 0x0100) {
            tcg_gen_sub_i32(dest, dest, val);
        } else {
            tcg_gen_add_i32(dest, dest, val);
        }
    } else {
        if (insn & 0x0100) {
            tcg_gen_setcond_i32(TCG_COND_LTU, QREG_CC_X, dest, val);
            tcg_gen_sub_i32(dest, dest, val);
            set_cc_op(s, CC_OP_SUBB + opsize);
        } else {
            tcg_gen_add_i32(dest, dest, val);
            tcg_gen_setcond_i32(TCG_COND_LTU, QREG_CC_X, dest, val);
            set_cc_op(s, CC_OP_ADDB + opsize);
        }
        gen_update_cc_add(dest, val, opsize);
    }
    tcg_temp_free(val);
    DEST_EA(env, insn, opsize, dest, &addr);
    tcg_temp_free(dest);
}

DISAS_INSN(branch)
{
    int32_t offset;
    uint32_t base;
    int op;

    base = s->pc;
    op = (insn >> 8) & 0xf;
    offset = (int8_t)insn;
    if (offset == 0) {
        offset = (int16_t)read_im16(env, s);
    } else if (offset == -1) {
        offset = read_im32(env, s);
    }
    if (op == 1) {
        /* bsr */
        gen_push(s, tcg_const_i32(s->pc));
    }
    if (op > 1) {
        /* Bcc */
        TCGLabel *l1 = gen_new_label();
        gen_jmpcc(s, ((insn >> 8) & 0xf) ^ 1, l1);
        gen_jmp_tb(s, 1, base + offset, s->base.pc_next);
        gen_set_label(l1);
        gen_jmp_tb(s, 0, s->pc, s->base.pc_next);
    } else {
        /* Unconditional branch.  */
        update_cc_op(s);
        gen_jmp_tb(s, 0, base + offset, s->base.pc_next);
    }
}

DISAS_INSN(moveq)
{
    tcg_gen_movi_i32(DREG(insn, 9), (int8_t)insn);
    gen_logic_cc(s, DREG(insn, 9), OS_LONG);
}

DISAS_INSN(mvzs)
{
    int opsize;
    TCGv src;
    TCGv reg;

    if (insn & 0x40)
        opsize = OS_WORD;
    else
        opsize = OS_BYTE;
    SRC_EA(env, src, opsize, (insn & 0x80) == 0, NULL);
    reg = DREG(insn, 9);
    tcg_gen_mov_i32(reg, src);
    gen_logic_cc(s, src, opsize);
}

DISAS_INSN(or)
{
    TCGv reg;
    TCGv dest;
    TCGv src;
    TCGv addr;
    int opsize;

    opsize = insn_opsize(insn);
    reg = gen_extend(s, DREG(insn, 9), opsize, 0);
    dest = tcg_temp_new();
    if (insn & 0x100) {
        SRC_EA(env, src, opsize, 0, &addr);
        tcg_gen_or_i32(dest, src, reg);
        DEST_EA(env, insn, opsize, dest, &addr);
    } else {
        SRC_EA(env, src, opsize, 0, NULL);
        tcg_gen_or_i32(dest, src, reg);
        gen_partset_reg(opsize, DREG(insn, 9), dest);
    }
    gen_logic_cc(s, dest, opsize);
    tcg_temp_free(dest);
}

DISAS_INSN(suba)
{
    TCGv src;
    TCGv reg;

    SRC_EA(env, src, (insn & 0x100) ? OS_LONG : OS_WORD, 1, NULL);
    reg = AREG(insn, 9);
    tcg_gen_sub_i32(reg, reg, src);
}

static inline void gen_subx(DisasContext *s, TCGv src, TCGv dest, int opsize)
{
    TCGv tmp;

    gen_flush_flags(s); /* compute old Z */

    /*
     * Perform subtract with borrow.
     * (X, N) = dest - (src + X);
     */

    tmp = tcg_const_i32(0);
    tcg_gen_add2_i32(QREG_CC_N, QREG_CC_X, src, tmp, QREG_CC_X, tmp);
    tcg_gen_sub2_i32(QREG_CC_N, QREG_CC_X, dest, tmp, QREG_CC_N, QREG_CC_X);
    gen_ext(QREG_CC_N, QREG_CC_N, opsize, 1);
    tcg_gen_andi_i32(QREG_CC_X, QREG_CC_X, 1);

    /* Compute signed-overflow for subtract.  */

    tcg_gen_xor_i32(QREG_CC_V, QREG_CC_N, dest);
    tcg_gen_xor_i32(tmp, dest, src);
    tcg_gen_and_i32(QREG_CC_V, QREG_CC_V, tmp);
    tcg_temp_free(tmp);

    /* Copy the rest of the results into place.  */
    tcg_gen_or_i32(QREG_CC_Z, QREG_CC_Z, QREG_CC_N); /* !Z is sticky */
    tcg_gen_mov_i32(QREG_CC_C, QREG_CC_X);

    set_cc_op(s, CC_OP_FLAGS);

    /* result is in QREG_CC_N */
}

DISAS_INSN(subx_reg)
{
    TCGv dest;
    TCGv src;
    int opsize;

    opsize = insn_opsize(insn);

    src = gen_extend(s, DREG(insn, 0), opsize, 1);
    dest = gen_extend(s, DREG(insn, 9), opsize, 1);

    gen_subx(s, src, dest, opsize);

    gen_partset_reg(opsize, DREG(insn, 9), QREG_CC_N);
}

DISAS_INSN(subx_mem)
{
    TCGv src;
    TCGv addr_src;
    TCGv dest;
    TCGv addr_dest;
    int opsize;

    opsize = insn_opsize(insn);

    addr_src = AREG(insn, 0);
    tcg_gen_subi_i32(addr_src, addr_src, opsize_bytes(opsize));
    src = gen_load(s, opsize, addr_src, 1, IS_USER(s));

    addr_dest = AREG(insn, 9);
    tcg_gen_subi_i32(addr_dest, addr_dest, opsize_bytes(opsize));
    dest = gen_load(s, opsize, addr_dest, 1, IS_USER(s));

    gen_subx(s, src, dest, opsize);

    gen_store(s, opsize, addr_dest, QREG_CC_N, IS_USER(s));

    tcg_temp_free(dest);
    tcg_temp_free(src);
}

DISAS_INSN(mov3q)
{
    TCGv src;
    int val;

    val = (insn >> 9) & 7;
    if (val == 0)
        val = -1;
    src = tcg_const_i32(val);
    gen_logic_cc(s, src, OS_LONG);
    DEST_EA(env, insn, OS_LONG, src, NULL);
    tcg_temp_free(src);
}

DISAS_INSN(cmp)
{
    TCGv src;
    TCGv reg;
    int opsize;

    opsize = insn_opsize(insn);
    SRC_EA(env, src, opsize, 1, NULL);
    reg = gen_extend(s, DREG(insn, 9), opsize, 1);
    gen_update_cc_cmp(s, reg, src, opsize);
}

DISAS_INSN(cmpa)
{
    int opsize;
    TCGv src;
    TCGv reg;

    if (insn & 0x100) {
        opsize = OS_LONG;
    } else {
        opsize = OS_WORD;
    }
    SRC_EA(env, src, opsize, 1, NULL);
    reg = AREG(insn, 9);
    gen_update_cc_cmp(s, reg, src, OS_LONG);
}

DISAS_INSN(cmpm)
{
    int opsize = insn_opsize(insn);
    TCGv src, dst;

    /* Post-increment load (mode 3) from Ay.  */
    src = gen_ea_mode(env, s, 3, REG(insn, 0), opsize,
                      NULL_QREG, NULL, EA_LOADS, IS_USER(s));
    /* Post-increment load (mode 3) from Ax.  */
    dst = gen_ea_mode(env, s, 3, REG(insn, 9), opsize,
                      NULL_QREG, NULL, EA_LOADS, IS_USER(s));

    gen_update_cc_cmp(s, dst, src, opsize);
}

DISAS_INSN(eor)
{
    TCGv src;
    TCGv dest;
    TCGv addr;
    int opsize;

    opsize = insn_opsize(insn);

    SRC_EA(env, src, opsize, 0, &addr);
    dest = tcg_temp_new();
    tcg_gen_xor_i32(dest, src, DREG(insn, 9));
    gen_logic_cc(s, dest, opsize);
    DEST_EA(env, insn, opsize, dest, &addr);
    tcg_temp_free(dest);
}

static void do_exg(TCGv reg1, TCGv reg2)
{
    TCGv temp = tcg_temp_new();
    tcg_gen_mov_i32(temp, reg1);
    tcg_gen_mov_i32(reg1, reg2);
    tcg_gen_mov_i32(reg2, temp);
    tcg_temp_free(temp);
}

DISAS_INSN(exg_dd)
{
    /* exchange Dx and Dy */
    do_exg(DREG(insn, 9), DREG(insn, 0));
}

DISAS_INSN(exg_aa)
{
    /* exchange Ax and Ay */
    do_exg(AREG(insn, 9), AREG(insn, 0));
}

DISAS_INSN(exg_da)
{
    /* exchange Dx and Ay */
    do_exg(DREG(insn, 9), AREG(insn, 0));
}

DISAS_INSN(and)
{
    TCGv src;
    TCGv reg;
    TCGv dest;
    TCGv addr;
    int opsize;

    dest = tcg_temp_new();

    opsize = insn_opsize(insn);
    reg = DREG(insn, 9);
    if (insn & 0x100) {
        SRC_EA(env, src, opsize, 0, &addr);
        tcg_gen_and_i32(dest, src, reg);
        DEST_EA(env, insn, opsize, dest, &addr);
    } else {
        SRC_EA(env, src, opsize, 0, NULL);
        tcg_gen_and_i32(dest, src, reg);
        gen_partset_reg(opsize, reg, dest);
    }
    gen_logic_cc(s, dest, opsize);
    tcg_temp_free(dest);
}

DISAS_INSN(adda)
{
    TCGv src;
    TCGv reg;

    SRC_EA(env, src, (insn & 0x100) ? OS_LONG : OS_WORD, 1, NULL);
    reg = AREG(insn, 9);
    tcg_gen_add_i32(reg, reg, src);
}

static inline void gen_addx(DisasContext *s, TCGv src, TCGv dest, int opsize)
{
    TCGv tmp;

    gen_flush_flags(s); /* compute old Z */

    /*
     * Perform addition with carry.
     * (X, N) = src + dest + X;
     */

    tmp = tcg_const_i32(0);
    tcg_gen_add2_i32(QREG_CC_N, QREG_CC_X, QREG_CC_X, tmp, dest, tmp);
    tcg_gen_add2_i32(QREG_CC_N, QREG_CC_X, QREG_CC_N, QREG_CC_X, src, tmp);
    gen_ext(QREG_CC_N, QREG_CC_N, opsize, 1);

    /* Compute signed-overflow for addition.  */

    tcg_gen_xor_i32(QREG_CC_V, QREG_CC_N, src);
    tcg_gen_xor_i32(tmp, dest, src);
    tcg_gen_andc_i32(QREG_CC_V, QREG_CC_V, tmp);
    tcg_temp_free(tmp);

    /* Copy the rest of the results into place.  */
    tcg_gen_or_i32(QREG_CC_Z, QREG_CC_Z, QREG_CC_N); /* !Z is sticky */
    tcg_gen_mov_i32(QREG_CC_C, QREG_CC_X);

    set_cc_op(s, CC_OP_FLAGS);

    /* result is in QREG_CC_N */
}

DISAS_INSN(addx_reg)
{
    TCGv dest;
    TCGv src;
    int opsize;

    opsize = insn_opsize(insn);

    dest = gen_extend(s, DREG(insn, 9), opsize, 1);
    src = gen_extend(s, DREG(insn, 0), opsize, 1);

    gen_addx(s, src, dest, opsize);

    gen_partset_reg(opsize, DREG(insn, 9), QREG_CC_N);
}

DISAS_INSN(addx_mem)
{
    TCGv src;
    TCGv addr_src;
    TCGv dest;
    TCGv addr_dest;
    int opsize;

    opsize = insn_opsize(insn);

    addr_src = AREG(insn, 0);
    tcg_gen_subi_i32(addr_src, addr_src, opsize_bytes(opsize));
    src = gen_load(s, opsize, addr_src, 1, IS_USER(s));

    addr_dest = AREG(insn, 9);
    tcg_gen_subi_i32(addr_dest, addr_dest, opsize_bytes(opsize));
    dest = gen_load(s, opsize, addr_dest, 1, IS_USER(s));

    gen_addx(s, src, dest, opsize);

    gen_store(s, opsize, addr_dest, QREG_CC_N, IS_USER(s));

    tcg_temp_free(dest);
    tcg_temp_free(src);
}

static inline void shift_im(DisasContext *s, uint16_t insn, int opsize)
{
    int count = (insn >> 9) & 7;
    int logical = insn & 8;
    int left = insn & 0x100;
    int bits = opsize_bytes(opsize) * 8;
    TCGv reg = gen_extend(s, DREG(insn, 0), opsize, !logical);

    if (count == 0) {
        count = 8;
    }

    tcg_gen_movi_i32(QREG_CC_V, 0);
    if (left) {
        tcg_gen_shri_i32(QREG_CC_C, reg, bits - count);
        tcg_gen_shli_i32(QREG_CC_N, reg, count);

        /*
         * Note that ColdFire always clears V (done above),
         * while M68000 sets if the most significant bit is changed at
         * any time during the shift operation.
         */
        if (!logical && m68k_feature(s->env, M68K_FEATURE_M68K)) {
            /* if shift count >= bits, V is (reg != 0) */
            if (count >= bits) {
                tcg_gen_setcond_i32(TCG_COND_NE, QREG_CC_V, reg, QREG_CC_V);
            } else {
                TCGv t0 = tcg_temp_new();
                tcg_gen_sari_i32(QREG_CC_V, reg, bits - 1);
                tcg_gen_sari_i32(t0, reg, bits - count - 1);
                tcg_gen_setcond_i32(TCG_COND_NE, QREG_CC_V, QREG_CC_V, t0);
                tcg_temp_free(t0);
            }
            tcg_gen_neg_i32(QREG_CC_V, QREG_CC_V);
        }
    } else {
        tcg_gen_shri_i32(QREG_CC_C, reg, count - 1);
        if (logical) {
            tcg_gen_shri_i32(QREG_CC_N, reg, count);
        } else {
            tcg_gen_sari_i32(QREG_CC_N, reg, count);
        }
    }

    gen_ext(QREG_CC_N, QREG_CC_N, opsize, 1);
    tcg_gen_andi_i32(QREG_CC_C, QREG_CC_C, 1);
    tcg_gen_mov_i32(QREG_CC_Z, QREG_CC_N);
    tcg_gen_mov_i32(QREG_CC_X, QREG_CC_C);

    gen_partset_reg(opsize, DREG(insn, 0), QREG_CC_N);
    set_cc_op(s, CC_OP_FLAGS);
}

static inline void shift_reg(DisasContext *s, uint16_t insn, int opsize)
{
    int logical = insn & 8;
    int left = insn & 0x100;
    int bits = opsize_bytes(opsize) * 8;
    TCGv reg = gen_extend(s, DREG(insn, 0), opsize, !logical);
    TCGv s32;
    TCGv_i64 t64, s64;

    t64 = tcg_temp_new_i64();
    s64 = tcg_temp_new_i64();
    s32 = tcg_temp_new();

    /*
     * Note that m68k truncates the shift count modulo 64, not 32.
     * In addition, a 64-bit shift makes it easy to find "the last
     * bit shifted out", for the carry flag.
     */
    tcg_gen_andi_i32(s32, DREG(insn, 9), 63);
    tcg_gen_extu_i32_i64(s64, s32);
    tcg_gen_extu_i32_i64(t64, reg);

    /* Optimistically set V=0.  Also used as a zero source below.  */
    tcg_gen_movi_i32(QREG_CC_V, 0);
    if (left) {
        tcg_gen_shl_i64(t64, t64, s64);

        if (opsize == OS_LONG) {
            tcg_gen_extr_i64_i32(QREG_CC_N, QREG_CC_C, t64);
            /* Note that C=0 if shift count is 0, and we get that for free.  */
        } else {
            TCGv zero = tcg_const_i32(0);
            tcg_gen_extrl_i64_i32(QREG_CC_N, t64);
            tcg_gen_shri_i32(QREG_CC_C, QREG_CC_N, bits);
            tcg_gen_movcond_i32(TCG_COND_EQ, QREG_CC_C,
                                s32, zero, zero, QREG_CC_C);
            tcg_temp_free(zero);
        }
        tcg_gen_andi_i32(QREG_CC_C, QREG_CC_C, 1);

        /* X = C, but only if the shift count was non-zero.  */
        tcg_gen_movcond_i32(TCG_COND_NE, QREG_CC_X, s32, QREG_CC_V,
                            QREG_CC_C, QREG_CC_X);

        /*
         * M68000 sets V if the most significant bit is changed at
         * any time during the shift operation.  Do this via creating
         * an extension of the sign bit, comparing, and discarding
         * the bits below the sign bit.  I.e.
         *     int64_t s = (intN_t)reg;
         *     int64_t t = (int64_t)(intN_t)reg << count;
         *     V = ((s ^ t) & (-1 << (bits - 1))) != 0
         */
        if (!logical && m68k_feature(s->env, M68K_FEATURE_M68K)) {
            TCGv_i64 tt = tcg_const_i64(32);
            /* if shift is greater than 32, use 32 */
            tcg_gen_movcond_i64(TCG_COND_GT, s64, s64, tt, tt, s64);
            tcg_temp_free_i64(tt);
            /* Sign extend the input to 64 bits; re-do the shift.  */
            tcg_gen_ext_i32_i64(t64, reg);
            tcg_gen_shl_i64(s64, t64, s64);
            /* Clear all bits that are unchanged.  */
            tcg_gen_xor_i64(t64, t64, s64);
            /* Ignore the bits below the sign bit.  */
            tcg_gen_andi_i64(t64, t64, -1ULL << (bits - 1));
            /* If any bits remain set, we have overflow.  */
            tcg_gen_setcondi_i64(TCG_COND_NE, t64, t64, 0);
            tcg_gen_extrl_i64_i32(QREG_CC_V, t64);
            tcg_gen_neg_i32(QREG_CC_V, QREG_CC_V);
        }
    } else {
        tcg_gen_shli_i64(t64, t64, 32);
        if (logical) {
            tcg_gen_shr_i64(t64, t64, s64);
        } else {
            tcg_gen_sar_i64(t64, t64, s64);
        }
        tcg_gen_extr_i64_i32(QREG_CC_C, QREG_CC_N, t64);

        /* Note that C=0 if shift count is 0, and we get that for free.  */
        tcg_gen_shri_i32(QREG_CC_C, QREG_CC_C, 31);

        /* X = C, but only if the shift count was non-zero.  */
        tcg_gen_movcond_i32(TCG_COND_NE, QREG_CC_X, s32, QREG_CC_V,
                            QREG_CC_C, QREG_CC_X);
    }
    gen_ext(QREG_CC_N, QREG_CC_N, opsize, 1);
    tcg_gen_mov_i32(QREG_CC_Z, QREG_CC_N);

    tcg_temp_free(s32);
    tcg_temp_free_i64(s64);
    tcg_temp_free_i64(t64);

    /* Write back the result.  */
    gen_partset_reg(opsize, DREG(insn, 0), QREG_CC_N);
    set_cc_op(s, CC_OP_FLAGS);
}

DISAS_INSN(shift8_im)
{
    shift_im(s, insn, OS_BYTE);
}

DISAS_INSN(shift16_im)
{
    shift_im(s, insn, OS_WORD);
}

DISAS_INSN(shift_im)
{
    shift_im(s, insn, OS_LONG);
}

DISAS_INSN(shift8_reg)
{
    shift_reg(s, insn, OS_BYTE);
}

DISAS_INSN(shift16_reg)
{
    shift_reg(s, insn, OS_WORD);
}

DISAS_INSN(shift_reg)
{
    shift_reg(s, insn, OS_LONG);
}

DISAS_INSN(shift_mem)
{
    int logical = insn & 8;
    int left = insn & 0x100;
    TCGv src;
    TCGv addr;

    SRC_EA(env, src, OS_WORD, !logical, &addr);
    tcg_gen_movi_i32(QREG_CC_V, 0);
    if (left) {
        tcg_gen_shri_i32(QREG_CC_C, src, 15);
        tcg_gen_shli_i32(QREG_CC_N, src, 1);

        /*
         * Note that ColdFire always clears V,
         * while M68000 sets if the most significant bit is changed at
         * any time during the shift operation
         */
        if (!logical && m68k_feature(s->env, M68K_FEATURE_M68K)) {
            src = gen_extend(s, src, OS_WORD, 1);
            tcg_gen_xor_i32(QREG_CC_V, QREG_CC_N, src);
        }
    } else {
        tcg_gen_mov_i32(QREG_CC_C, src);
        if (logical) {
            tcg_gen_shri_i32(QREG_CC_N, src, 1);
        } else {
            tcg_gen_sari_i32(QREG_CC_N, src, 1);
        }
    }

    gen_ext(QREG_CC_N, QREG_CC_N, OS_WORD, 1);
    tcg_gen_andi_i32(QREG_CC_C, QREG_CC_C, 1);
    tcg_gen_mov_i32(QREG_CC_Z, QREG_CC_N);
    tcg_gen_mov_i32(QREG_CC_X, QREG_CC_C);

    DEST_EA(env, insn, OS_WORD, QREG_CC_N, &addr);
    set_cc_op(s, CC_OP_FLAGS);
}

static void rotate(TCGv reg, TCGv shift, int left, int size)
{
    switch (size) {
    case 8:
        /* Replicate the 8-bit input so that a 32-bit rotate works.  */
        tcg_gen_ext8u_i32(reg, reg);
        tcg_gen_muli_i32(reg, reg, 0x01010101);
        goto do_long;
    case 16:
        /* Replicate the 16-bit input so that a 32-bit rotate works.  */
        tcg_gen_deposit_i32(reg, reg, reg, 16, 16);
        goto do_long;
    do_long:
    default:
        if (left) {
            tcg_gen_rotl_i32(reg, reg, shift);
        } else {
            tcg_gen_rotr_i32(reg, reg, shift);
        }
    }

    /* compute flags */

    switch (size) {
    case 8:
        tcg_gen_ext8s_i32(reg, reg);
        break;
    case 16:
        tcg_gen_ext16s_i32(reg, reg);
        break;
    default:
        break;
    }

    /* QREG_CC_X is not affected */

    tcg_gen_mov_i32(QREG_CC_N, reg);
    tcg_gen_mov_i32(QREG_CC_Z, reg);

    if (left) {
        tcg_gen_andi_i32(QREG_CC_C, reg, 1);
    } else {
        tcg_gen_shri_i32(QREG_CC_C, reg, 31);
    }

    tcg_gen_movi_i32(QREG_CC_V, 0); /* always cleared */
}

static void rotate_x_flags(TCGv reg, TCGv X, int size)
{
    switch (size) {
    case 8:
        tcg_gen_ext8s_i32(reg, reg);
        break;
    case 16:
        tcg_gen_ext16s_i32(reg, reg);
        break;
    default:
        break;
    }
    tcg_gen_mov_i32(QREG_CC_N, reg);
    tcg_gen_mov_i32(QREG_CC_Z, reg);
    tcg_gen_mov_i32(QREG_CC_X, X);
    tcg_gen_mov_i32(QREG_CC_C, X);
    tcg_gen_movi_i32(QREG_CC_V, 0);
}

/* Result of rotate_x() is valid if 0 <= shift <= size */
static TCGv rotate_x(TCGv reg, TCGv shift, int left, int size)
{
    TCGv X, shl, shr, shx, sz, zero;

    sz = tcg_const_i32(size);

    shr = tcg_temp_new();
    shl = tcg_temp_new();
    shx = tcg_temp_new();
    if (left) {
        tcg_gen_mov_i32(shl, shift);      /* shl = shift */
        tcg_gen_movi_i32(shr, size + 1);
        tcg_gen_sub_i32(shr, shr, shift); /* shr = size + 1 - shift */
        tcg_gen_subi_i32(shx, shift, 1);  /* shx = shift - 1 */
        /* shx = shx < 0 ? size : shx; */
        zero = tcg_const_i32(0);
        tcg_gen_movcond_i32(TCG_COND_LT, shx, shx, zero, sz, shx);
        tcg_temp_free(zero);
    } else {
        tcg_gen_mov_i32(shr, shift);      /* shr = shift */
        tcg_gen_movi_i32(shl, size + 1);
        tcg_gen_sub_i32(shl, shl, shift); /* shl = size + 1 - shift */
        tcg_gen_sub_i32(shx, sz, shift); /* shx = size - shift */
    }
    tcg_temp_free_i32(sz);

    /* reg = (reg << shl) | (reg >> shr) | (x << shx); */

    tcg_gen_shl_i32(shl, reg, shl);
    tcg_gen_shr_i32(shr, reg, shr);
    tcg_gen_or_i32(reg, shl, shr);
    tcg_temp_free(shl);
    tcg_temp_free(shr);
    tcg_gen_shl_i32(shx, QREG_CC_X, shx);
    tcg_gen_or_i32(reg, reg, shx);
    tcg_temp_free(shx);

    /* X = (reg >> size) & 1 */

    X = tcg_temp_new();
    tcg_gen_extract_i32(X, reg, size, 1);

    return X;
}

/* Result of rotate32_x() is valid if 0 <= shift < 33 */
static TCGv rotate32_x(TCGv reg, TCGv shift, int left)
{
    TCGv_i64 t0, shift64;
    TCGv X, lo, hi, zero;

    shift64 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(shift64, shift);

    t0 = tcg_temp_new_i64();

    X = tcg_temp_new();
    lo = tcg_temp_new();
    hi = tcg_temp_new();

    if (left) {
        /* create [reg:X:..] */

        tcg_gen_shli_i32(lo, QREG_CC_X, 31);
        tcg_gen_concat_i32_i64(t0, lo, reg);

        /* rotate */

        tcg_gen_rotl_i64(t0, t0, shift64);
        tcg_temp_free_i64(shift64);

        /* result is [reg:..:reg:X] */

        tcg_gen_extr_i64_i32(lo, hi, t0);
        tcg_gen_andi_i32(X, lo, 1);

        tcg_gen_shri_i32(lo, lo, 1);
    } else {
        /* create [..:X:reg] */

        tcg_gen_concat_i32_i64(t0, reg, QREG_CC_X);

        tcg_gen_rotr_i64(t0, t0, shift64);
        tcg_temp_free_i64(shift64);

        /* result is value: [X:reg:..:reg] */

        tcg_gen_extr_i64_i32(lo, hi, t0);

        /* extract X */

        tcg_gen_shri_i32(X, hi, 31);

        /* extract result */

        tcg_gen_shli_i32(hi, hi, 1);
    }
    tcg_temp_free_i64(t0);
    tcg_gen_or_i32(lo, lo, hi);
    tcg_temp_free(hi);

    /* if shift == 0, register and X are not affected */

    zero = tcg_const_i32(0);
    tcg_gen_movcond_i32(TCG_COND_EQ, X, shift, zero, QREG_CC_X, X);
    tcg_gen_movcond_i32(TCG_COND_EQ, reg, shift, zero, reg, lo);
    tcg_temp_free(zero);
    tcg_temp_free(lo);

    return X;
}

DISAS_INSN(rotate_im)
{
    TCGv shift;
    int tmp;
    int left = (insn & 0x100);

    tmp = (insn >> 9) & 7;
    if (tmp == 0) {
        tmp = 8;
    }

    shift = tcg_const_i32(tmp);
    if (insn & 8) {
        rotate(DREG(insn, 0), shift, left, 32);
    } else {
        TCGv X = rotate32_x(DREG(insn, 0), shift, left);
        rotate_x_flags(DREG(insn, 0), X, 32);
        tcg_temp_free(X);
    }
    tcg_temp_free(shift);

    set_cc_op(s, CC_OP_FLAGS);
}

DISAS_INSN(rotate8_im)
{
    int left = (insn & 0x100);
    TCGv reg;
    TCGv shift;
    int tmp;

    reg = gen_extend(s, DREG(insn, 0), OS_BYTE, 0);

    tmp = (insn >> 9) & 7;
    if (tmp == 0) {
        tmp = 8;
    }

    shift = tcg_const_i32(tmp);
    if (insn & 8) {
        rotate(reg, shift, left, 8);
    } else {
        TCGv X = rotate_x(reg, shift, left, 8);
        rotate_x_flags(reg, X, 8);
        tcg_temp_free(X);
    }
    tcg_temp_free(shift);
    gen_partset_reg(OS_BYTE, DREG(insn, 0), reg);
    set_cc_op(s, CC_OP_FLAGS);
}

DISAS_INSN(rotate16_im)
{
    int left = (insn & 0x100);
    TCGv reg;
    TCGv shift;
    int tmp;

    reg = gen_extend(s, DREG(insn, 0), OS_WORD, 0);
    tmp = (insn >> 9) & 7;
    if (tmp == 0) {
        tmp = 8;
    }

    shift = tcg_const_i32(tmp);
    if (insn & 8) {
        rotate(reg, shift, left, 16);
    } else {
        TCGv X = rotate_x(reg, shift, left, 16);
        rotate_x_flags(reg, X, 16);
        tcg_temp_free(X);
    }
    tcg_temp_free(shift);
    gen_partset_reg(OS_WORD, DREG(insn, 0), reg);
    set_cc_op(s, CC_OP_FLAGS);
}

DISAS_INSN(rotate_reg)
{
    TCGv reg;
    TCGv src;
    TCGv t0, t1;
    int left = (insn & 0x100);

    reg = DREG(insn, 0);
    src = DREG(insn, 9);
    /* shift in [0..63] */
    t0 = tcg_temp_new();
    tcg_gen_andi_i32(t0, src, 63);
    t1 = tcg_temp_new_i32();
    if (insn & 8) {
        tcg_gen_andi_i32(t1, src, 31);
        rotate(reg, t1, left, 32);
        /* if shift == 0, clear C */
        tcg_gen_movcond_i32(TCG_COND_EQ, QREG_CC_C,
                            t0, QREG_CC_V /* 0 */,
                            QREG_CC_V /* 0 */, QREG_CC_C);
    } else {
        TCGv X;
        /* modulo 33 */
        tcg_gen_movi_i32(t1, 33);
        tcg_gen_remu_i32(t1, t0, t1);
        X = rotate32_x(DREG(insn, 0), t1, left);
        rotate_x_flags(DREG(insn, 0), X, 32);
        tcg_temp_free(X);
    }
    tcg_temp_free(t1);
    tcg_temp_free(t0);
    set_cc_op(s, CC_OP_FLAGS);
}

DISAS_INSN(rotate8_reg)
{
    TCGv reg;
    TCGv src;
    TCGv t0, t1;
    int left = (insn & 0x100);

    reg = gen_extend(s, DREG(insn, 0), OS_BYTE, 0);
    src = DREG(insn, 9);
    /* shift in [0..63] */
    t0 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, src, 63);
    t1 = tcg_temp_new_i32();
    if (insn & 8) {
        tcg_gen_andi_i32(t1, src, 7);
        rotate(reg, t1, left, 8);
        /* if shift == 0, clear C */
        tcg_gen_movcond_i32(TCG_COND_EQ, QREG_CC_C,
                            t0, QREG_CC_V /* 0 */,
                            QREG_CC_V /* 0 */, QREG_CC_C);
    } else {
        TCGv X;
        /* modulo 9 */
        tcg_gen_movi_i32(t1, 9);
        tcg_gen_remu_i32(t1, t0, t1);
        X = rotate_x(reg, t1, left, 8);
        rotate_x_flags(reg, X, 8);
        tcg_temp_free(X);
    }
    tcg_temp_free(t1);
    tcg_temp_free(t0);
    gen_partset_reg(OS_BYTE, DREG(insn, 0), reg);
    set_cc_op(s, CC_OP_FLAGS);
}

DISAS_INSN(rotate16_reg)
{
    TCGv reg;
    TCGv src;
    TCGv t0, t1;
    int left = (insn & 0x100);

    reg = gen_extend(s, DREG(insn, 0), OS_WORD, 0);
    src = DREG(insn, 9);
    /* shift in [0..63] */
    t0 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, src, 63);
    t1 = tcg_temp_new_i32();
    if (insn & 8) {
        tcg_gen_andi_i32(t1, src, 15);
        rotate(reg, t1, left, 16);
        /* if shift == 0, clear C */
        tcg_gen_movcond_i32(TCG_COND_EQ, QREG_CC_C,
                            t0, QREG_CC_V /* 0 */,
                            QREG_CC_V /* 0 */, QREG_CC_C);
    } else {
        TCGv X;
        /* modulo 17 */
        tcg_gen_movi_i32(t1, 17);
        tcg_gen_remu_i32(t1, t0, t1);
        X = rotate_x(reg, t1, left, 16);
        rotate_x_flags(reg, X, 16);
        tcg_temp_free(X);
    }
    tcg_temp_free(t1);
    tcg_temp_free(t0);
    gen_partset_reg(OS_WORD, DREG(insn, 0), reg);
    set_cc_op(s, CC_OP_FLAGS);
}

DISAS_INSN(rotate_mem)
{
    TCGv src;
    TCGv addr;
    TCGv shift;
    int left = (insn & 0x100);

    SRC_EA(env, src, OS_WORD, 0, &addr);

    shift = tcg_const_i32(1);
    if (insn & 0x0200) {
        rotate(src, shift, left, 16);
    } else {
        TCGv X = rotate_x(src, shift, left, 16);
        rotate_x_flags(src, X, 16);
        tcg_temp_free(X);
    }
    tcg_temp_free(shift);
    DEST_EA(env, insn, OS_WORD, src, &addr);
    set_cc_op(s, CC_OP_FLAGS);
}

DISAS_INSN(bfext_reg)
{
    int ext = read_im16(env, s);
    int is_sign = insn & 0x200;
    TCGv src = DREG(insn, 0);
    TCGv dst = DREG(ext, 12);
    int len = ((extract32(ext, 0, 5) - 1) & 31) + 1;
    int ofs = extract32(ext, 6, 5);  /* big bit-endian */
    int pos = 32 - ofs - len;        /* little bit-endian */
    TCGv tmp = tcg_temp_new();
    TCGv shift;

    /*
     * In general, we're going to rotate the field so that it's at the
     * top of the word and then right-shift by the complement of the
     * width to extend the field.
     */
    if (ext & 0x20) {
        /* Variable width.  */
        if (ext & 0x800) {
            /* Variable offset.  */
            tcg_gen_andi_i32(tmp, DREG(ext, 6), 31);
            tcg_gen_rotl_i32(tmp, src, tmp);
        } else {
            tcg_gen_rotli_i32(tmp, src, ofs);
        }

        shift = tcg_temp_new();
        tcg_gen_neg_i32(shift, DREG(ext, 0));
        tcg_gen_andi_i32(shift, shift, 31);
        tcg_gen_sar_i32(QREG_CC_N, tmp, shift);
        if (is_sign) {
            tcg_gen_mov_i32(dst, QREG_CC_N);
        } else {
            tcg_gen_shr_i32(dst, tmp, shift);
        }
        tcg_temp_free(shift);
    } else {
        /* Immediate width.  */
        if (ext & 0x800) {
            /* Variable offset */
            tcg_gen_andi_i32(tmp, DREG(ext, 6), 31);
            tcg_gen_rotl_i32(tmp, src, tmp);
            src = tmp;
            pos = 32 - len;
        } else {
            /*
             * Immediate offset.  If the field doesn't wrap around the
             * end of the word, rely on (s)extract completely.
             */
            if (pos < 0) {
                tcg_gen_rotli_i32(tmp, src, ofs);
                src = tmp;
                pos = 32 - len;
            }
        }

        tcg_gen_sextract_i32(QREG_CC_N, src, pos, len);
        if (is_sign) {
            tcg_gen_mov_i32(dst, QREG_CC_N);
        } else {
            tcg_gen_extract_i32(dst, src, pos, len);
        }
    }

    tcg_temp_free(tmp);
    set_cc_op(s, CC_OP_LOGIC);
}

DISAS_INSN(bfext_mem)
{
    int ext = read_im16(env, s);
    int is_sign = insn & 0x200;
    TCGv dest = DREG(ext, 12);
    TCGv addr, len, ofs;

    addr = gen_lea(env, s, insn, OS_UNSIZED);
    if (IS_NULL_QREG(addr)) {
        gen_addr_fault(s);
        return;
    }

    if (ext & 0x20) {
        len = DREG(ext, 0);
    } else {
        len = tcg_const_i32(extract32(ext, 0, 5));
    }
    if (ext & 0x800) {
        ofs = DREG(ext, 6);
    } else {
        ofs = tcg_const_i32(extract32(ext, 6, 5));
    }

    if (is_sign) {
        gen_helper_bfexts_mem(dest, cpu_env, addr, ofs, len);
        tcg_gen_mov_i32(QREG_CC_N, dest);
    } else {
        TCGv_i64 tmp = tcg_temp_new_i64();
        gen_helper_bfextu_mem(tmp, cpu_env, addr, ofs, len);
        tcg_gen_extr_i64_i32(dest, QREG_CC_N, tmp);
        tcg_temp_free_i64(tmp);
    }
    set_cc_op(s, CC_OP_LOGIC);

    if (!(ext & 0x20)) {
        tcg_temp_free(len);
    }
    if (!(ext & 0x800)) {
        tcg_temp_free(ofs);
    }
}

DISAS_INSN(bfop_reg)
{
    int ext = read_im16(env, s);
    TCGv src = DREG(insn, 0);
    int len = ((extract32(ext, 0, 5) - 1) & 31) + 1;
    int ofs = extract32(ext, 6, 5);  /* big bit-endian */
    TCGv mask, tofs, tlen;

    tofs = NULL;
    tlen = NULL;
    if ((insn & 0x0f00) == 0x0d00) { /* bfffo */
        tofs = tcg_temp_new();
        tlen = tcg_temp_new();
    }

    if ((ext & 0x820) == 0) {
        /* Immediate width and offset.  */
        uint32_t maski = 0x7fffffffu >> (len - 1);
        if (ofs + len <= 32) {
            tcg_gen_shli_i32(QREG_CC_N, src, ofs);
        } else {
            tcg_gen_rotli_i32(QREG_CC_N, src, ofs);
        }
        tcg_gen_andi_i32(QREG_CC_N, QREG_CC_N, ~maski);
        mask = tcg_const_i32(ror32(maski, ofs));
        if (tofs) {
            tcg_gen_movi_i32(tofs, ofs);
            tcg_gen_movi_i32(tlen, len);
        }
    } else {
        TCGv tmp = tcg_temp_new();
        if (ext & 0x20) {
            /* Variable width */
            tcg_gen_subi_i32(tmp, DREG(ext, 0), 1);
            tcg_gen_andi_i32(tmp, tmp, 31);
            mask = tcg_const_i32(0x7fffffffu);
            tcg_gen_shr_i32(mask, mask, tmp);
            if (tlen) {
                tcg_gen_addi_i32(tlen, tmp, 1);
            }
        } else {
            /* Immediate width */
            mask = tcg_const_i32(0x7fffffffu >> (len - 1));
            if (tlen) {
                tcg_gen_movi_i32(tlen, len);
            }
        }
        if (ext & 0x800) {
            /* Variable offset */
            tcg_gen_andi_i32(tmp, DREG(ext, 6), 31);
            tcg_gen_rotl_i32(QREG_CC_N, src, tmp);
            tcg_gen_andc_i32(QREG_CC_N, QREG_CC_N, mask);
            tcg_gen_rotr_i32(mask, mask, tmp);
            if (tofs) {
                tcg_gen_mov_i32(tofs, tmp);
            }
        } else {
            /* Immediate offset (and variable width) */
            tcg_gen_rotli_i32(QREG_CC_N, src, ofs);
            tcg_gen_andc_i32(QREG_CC_N, QREG_CC_N, mask);
            tcg_gen_rotri_i32(mask, mask, ofs);
            if (tofs) {
                tcg_gen_movi_i32(tofs, ofs);
            }
        }
        tcg_temp_free(tmp);
    }
    set_cc_op(s, CC_OP_LOGIC);

    switch (insn & 0x0f00) {
    case 0x0a00: /* bfchg */
        tcg_gen_eqv_i32(src, src, mask);
        break;
    case 0x0c00: /* bfclr */
        tcg_gen_and_i32(src, src, mask);
        break;
    case 0x0d00: /* bfffo */
        gen_helper_bfffo_reg(DREG(ext, 12), QREG_CC_N, tofs, tlen);
        tcg_temp_free(tlen);
        tcg_temp_free(tofs);
        break;
    case 0x0e00: /* bfset */
        tcg_gen_orc_i32(src, src, mask);
        break;
    case 0x0800: /* bftst */
        /* flags already set; no other work to do.  */
        break;
    default:
        g_assert_not_reached();
    }
    tcg_temp_free(mask);
}

DISAS_INSN(bfop_mem)
{
    int ext = read_im16(env, s);
    TCGv addr, len, ofs;
    TCGv_i64 t64;

    addr = gen_lea(env, s, insn, OS_UNSIZED);
    if (IS_NULL_QREG(addr)) {
        gen_addr_fault(s);
        return;
    }

    if (ext & 0x20) {
        len = DREG(ext, 0);
    } else {
        len = tcg_const_i32(extract32(ext, 0, 5));
    }
    if (ext & 0x800) {
        ofs = DREG(ext, 6);
    } else {
        ofs = tcg_const_i32(extract32(ext, 6, 5));
    }

    switch (insn & 0x0f00) {
    case 0x0a00: /* bfchg */
        gen_helper_bfchg_mem(QREG_CC_N, cpu_env, addr, ofs, len);
        break;
    case 0x0c00: /* bfclr */
        gen_helper_bfclr_mem(QREG_CC_N, cpu_env, addr, ofs, len);
        break;
    case 0x0d00: /* bfffo */
        t64 = tcg_temp_new_i64();
        gen_helper_bfffo_mem(t64, cpu_env, addr, ofs, len);
        tcg_gen_extr_i64_i32(DREG(ext, 12), QREG_CC_N, t64);
        tcg_temp_free_i64(t64);
        break;
    case 0x0e00: /* bfset */
        gen_helper_bfset_mem(QREG_CC_N, cpu_env, addr, ofs, len);
        break;
    case 0x0800: /* bftst */
        gen_helper_bfexts_mem(QREG_CC_N, cpu_env, addr, ofs, len);
        break;
    default:
        g_assert_not_reached();
    }
    set_cc_op(s, CC_OP_LOGIC);

    if (!(ext & 0x20)) {
        tcg_temp_free(len);
    }
    if (!(ext & 0x800)) {
        tcg_temp_free(ofs);
    }
}

DISAS_INSN(bfins_reg)
{
    int ext = read_im16(env, s);
    TCGv dst = DREG(insn, 0);
    TCGv src = DREG(ext, 12);
    int len = ((extract32(ext, 0, 5) - 1) & 31) + 1;
    int ofs = extract32(ext, 6, 5);  /* big bit-endian */
    int pos = 32 - ofs - len;        /* little bit-endian */
    TCGv tmp;

    tmp = tcg_temp_new();

    if (ext & 0x20) {
        /* Variable width */
        tcg_gen_neg_i32(tmp, DREG(ext, 0));
        tcg_gen_andi_i32(tmp, tmp, 31);
        tcg_gen_shl_i32(QREG_CC_N, src, tmp);
    } else {
        /* Immediate width */
        tcg_gen_shli_i32(QREG_CC_N, src, 32 - len);
    }
    set_cc_op(s, CC_OP_LOGIC);

    /* Immediate width and offset */
    if ((ext & 0x820) == 0) {
        /* Check for suitability for deposit.  */
        if (pos >= 0) {
            tcg_gen_deposit_i32(dst, dst, src, pos, len);
        } else {
            uint32_t maski = -2U << (len - 1);
            uint32_t roti = (ofs + len) & 31;
            tcg_gen_andi_i32(tmp, src, ~maski);
            tcg_gen_rotri_i32(tmp, tmp, roti);
            tcg_gen_andi_i32(dst, dst, ror32(maski, roti));
            tcg_gen_or_i32(dst, dst, tmp);
        }
    } else {
        TCGv mask = tcg_temp_new();
        TCGv rot = tcg_temp_new();

        if (ext & 0x20) {
            /* Variable width */
            tcg_gen_subi_i32(rot, DREG(ext, 0), 1);
            tcg_gen_andi_i32(rot, rot, 31);
            tcg_gen_movi_i32(mask, -2);
            tcg_gen_shl_i32(mask, mask, rot);
            tcg_gen_mov_i32(rot, DREG(ext, 0));
            tcg_gen_andc_i32(tmp, src, mask);
        } else {
            /* Immediate width (variable offset) */
            uint32_t maski = -2U << (len - 1);
            tcg_gen_andi_i32(tmp, src, ~maski);
            tcg_gen_movi_i32(mask, maski);
            tcg_gen_movi_i32(rot, len & 31);
        }
        if (ext & 0x800) {
            /* Variable offset */
            tcg_gen_add_i32(rot, rot, DREG(ext, 6));
        } else {
            /* Immediate offset (variable width) */
            tcg_gen_addi_i32(rot, rot, ofs);
        }
        tcg_gen_andi_i32(rot, rot, 31);
        tcg_gen_rotr_i32(mask, mask, rot);
        tcg_gen_rotr_i32(tmp, tmp, rot);
        tcg_gen_and_i32(dst, dst, mask);
        tcg_gen_or_i32(dst, dst, tmp);

        tcg_temp_free(rot);
        tcg_temp_free(mask);
    }
    tcg_temp_free(tmp);
}

DISAS_INSN(bfins_mem)
{
    int ext = read_im16(env, s);
    TCGv src = DREG(ext, 12);
    TCGv addr, len, ofs;

    addr = gen_lea(env, s, insn, OS_UNSIZED);
    if (IS_NULL_QREG(addr)) {
        gen_addr_fault(s);
        return;
    }

    if (ext & 0x20) {
        len = DREG(ext, 0);
    } else {
        len = tcg_const_i32(extract32(ext, 0, 5));
    }
    if (ext & 0x800) {
        ofs = DREG(ext, 6);
    } else {
        ofs = tcg_const_i32(extract32(ext, 6, 5));
    }

    gen_helper_bfins_mem(QREG_CC_N, cpu_env, addr, src, ofs, len);
    set_cc_op(s, CC_OP_LOGIC);

    if (!(ext & 0x20)) {
        tcg_temp_free(len);
    }
    if (!(ext & 0x800)) {
        tcg_temp_free(ofs);
    }
}

DISAS_INSN(ff1)
{
    TCGv reg;
    reg = DREG(insn, 0);
    gen_logic_cc(s, reg, OS_LONG);
    gen_helper_ff1(reg, reg);
}

DISAS_INSN(chk)
{
    TCGv src, reg;
    int opsize;

    switch ((insn >> 7) & 3) {
    case 3:
        opsize = OS_WORD;
        break;
    case 2:
        if (m68k_feature(env, M68K_FEATURE_CHK2)) {
            opsize = OS_LONG;
            break;
        }
        /* fallthru */
    default:
        gen_exception(s, s->base.pc_next, EXCP_ILLEGAL);
        return;
    }
    SRC_EA(env, src, opsize, 1, NULL);
    reg = gen_extend(s, DREG(insn, 9), opsize, 1);

    gen_flush_flags(s);
    gen_helper_chk(cpu_env, reg, src);
}

DISAS_INSN(chk2)
{
    uint16_t ext;
    TCGv addr1, addr2, bound1, bound2, reg;
    int opsize;

    switch ((insn >> 9) & 3) {
    case 0:
        opsize = OS_BYTE;
        break;
    case 1:
        opsize = OS_WORD;
        break;
    case 2:
        opsize = OS_LONG;
        break;
    default:
        gen_exception(s, s->base.pc_next, EXCP_ILLEGAL);
        return;
    }

    ext = read_im16(env, s);
    if ((ext & 0x0800) == 0) {
        gen_exception(s, s->base.pc_next, EXCP_ILLEGAL);
        return;
    }

    addr1 = gen_lea(env, s, insn, OS_UNSIZED);
    addr2 = tcg_temp_new();
    tcg_gen_addi_i32(addr2, addr1, opsize_bytes(opsize));

    bound1 = gen_load(s, opsize, addr1, 1, IS_USER(s));
    tcg_temp_free(addr1);
    bound2 = gen_load(s, opsize, addr2, 1, IS_USER(s));
    tcg_temp_free(addr2);

    reg = tcg_temp_new();
    if (ext & 0x8000) {
        tcg_gen_mov_i32(reg, AREG(ext, 12));
    } else {
        gen_ext(reg, DREG(ext, 12), opsize, 1);
    }

    gen_flush_flags(s);
    gen_helper_chk2(cpu_env, reg, bound1, bound2);
    tcg_temp_free(reg);
    tcg_temp_free(bound1);
    tcg_temp_free(bound2);
}

static void m68k_copy_line(TCGv dst, TCGv src, int index)
{
    TCGv addr;
    TCGv_i64 t0, t1;

    addr = tcg_temp_new();

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();

    tcg_gen_andi_i32(addr, src, ~15);
    tcg_gen_qemu_ld64(t0, addr, index);
    tcg_gen_addi_i32(addr, addr, 8);
    tcg_gen_qemu_ld64(t1, addr, index);

    tcg_gen_andi_i32(addr, dst, ~15);
    tcg_gen_qemu_st64(t0, addr, index);
    tcg_gen_addi_i32(addr, addr, 8);
    tcg_gen_qemu_st64(t1, addr, index);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
    tcg_temp_free(addr);
}

DISAS_INSN(move16_reg)
{
    int index = IS_USER(s);
    TCGv tmp;
    uint16_t ext;

    ext = read_im16(env, s);
    if ((ext & (1 << 15)) == 0) {
        gen_exception(s, s->base.pc_next, EXCP_ILLEGAL);
    }

    m68k_copy_line(AREG(ext, 12), AREG(insn, 0), index);

    /* Ax can be Ay, so save Ay before incrementing Ax */
    tmp = tcg_temp_new();
    tcg_gen_mov_i32(tmp, AREG(ext, 12));
    tcg_gen_addi_i32(AREG(insn, 0), AREG(insn, 0), 16);
    tcg_gen_addi_i32(AREG(ext, 12), tmp, 16);
    tcg_temp_free(tmp);
}

DISAS_INSN(move16_mem)
{
    int index = IS_USER(s);
    TCGv reg, addr;

    reg = AREG(insn, 0);
    addr = tcg_const_i32(read_im32(env, s));

    if ((insn >> 3) & 1) {
        /* MOVE16 (xxx).L, (Ay) */
        m68k_copy_line(reg, addr, index);
    } else {
        /* MOVE16 (Ay), (xxx).L */
        m68k_copy_line(addr, reg, index);
    }

    tcg_temp_free(addr);

    if (((insn >> 3) & 2) == 0) {
        /* (Ay)+ */
        tcg_gen_addi_i32(reg, reg, 16);
    }
}

DISAS_INSN(strldsr)
{
    uint16_t ext;
    uint32_t addr;

    addr = s->pc - 2;
    ext = read_im16(env, s);
    if (ext != 0x46FC) {
        gen_exception(s, addr, EXCP_ILLEGAL);
        return;
    }
    ext = read_im16(env, s);
    if (IS_USER(s) || (ext & SR_S) == 0) {
        gen_exception(s, addr, EXCP_PRIVILEGE);
        return;
    }
    gen_push(s, gen_get_sr(s));
    gen_set_sr_im(s, ext, 0);
    gen_exit_tb(s);
}

DISAS_INSN(move_from_sr)
{
    TCGv sr;

    if (IS_USER(s) && m68k_feature(env, M68K_FEATURE_MOVEFROMSR_PRIV)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }
    sr = gen_get_sr(s);
    DEST_EA(env, insn, OS_WORD, sr, NULL);
}

#if defined(CONFIG_SOFTMMU)
DISAS_INSN(moves)
{
    int opsize;
    uint16_t ext;
    TCGv reg;
    TCGv addr;
    int extend;

    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }

    ext = read_im16(env, s);

    opsize = insn_opsize(insn);

    if (ext & 0x8000) {
        /* address register */
        reg = AREG(ext, 12);
        extend = 1;
    } else {
        /* data register */
        reg = DREG(ext, 12);
        extend = 0;
    }

    addr = gen_lea(env, s, insn, opsize);
    if (IS_NULL_QREG(addr)) {
        gen_addr_fault(s);
        return;
    }

    if (ext & 0x0800) {
        /* from reg to ea */
        gen_store(s, opsize, addr, reg, DFC_INDEX(s));
    } else {
        /* from ea to reg */
        TCGv tmp = gen_load(s, opsize, addr, 0, SFC_INDEX(s));
        if (extend) {
            gen_ext(reg, tmp, opsize, 1);
        } else {
            gen_partset_reg(opsize, reg, tmp);
        }
        tcg_temp_free(tmp);
    }
    switch (extract32(insn, 3, 3)) {
    case 3: /* Indirect postincrement.  */
        tcg_gen_addi_i32(AREG(insn, 0), addr,
                         REG(insn, 0) == 7 && opsize == OS_BYTE
                         ? 2
                         : opsize_bytes(opsize));
        break;
    case 4: /* Indirect predecrememnt.  */
        tcg_gen_mov_i32(AREG(insn, 0), addr);
        break;
    }
}

DISAS_INSN(move_to_sr)
{
    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }
    gen_move_to_sr(env, s, insn, false);
    gen_exit_tb(s);
}

DISAS_INSN(move_from_usp)
{
    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }
    tcg_gen_ld_i32(AREG(insn, 0), cpu_env,
                   offsetof(CPUM68KState, sp[M68K_USP]));
}

DISAS_INSN(move_to_usp)
{
    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }
    tcg_gen_st_i32(AREG(insn, 0), cpu_env,
                   offsetof(CPUM68KState, sp[M68K_USP]));
}

DISAS_INSN(halt)
{
    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }

    gen_exception(s, s->pc, EXCP_HALT_INSN);
}

DISAS_INSN(stop)
{
    uint16_t ext;

    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }

    ext = read_im16(env, s);

    gen_set_sr_im(s, ext, 0);
    tcg_gen_movi_i32(cpu_halted, 1);
    gen_exception(s, s->pc, EXCP_HLT);
}

DISAS_INSN(rte)
{
    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }
    gen_exception(s, s->base.pc_next, EXCP_RTE);
}

DISAS_INSN(cf_movec)
{
    uint16_t ext;
    TCGv reg;

    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }

    ext = read_im16(env, s);

    if (ext & 0x8000) {
        reg = AREG(ext, 12);
    } else {
        reg = DREG(ext, 12);
    }
    gen_helper_cf_movec_to(cpu_env, tcg_const_i32(ext & 0xfff), reg);
    gen_exit_tb(s);
}

DISAS_INSN(m68k_movec)
{
    uint16_t ext;
    TCGv reg;

    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }

    ext = read_im16(env, s);

    if (ext & 0x8000) {
        reg = AREG(ext, 12);
    } else {
        reg = DREG(ext, 12);
    }
    if (insn & 1) {
        gen_helper_m68k_movec_to(cpu_env, tcg_const_i32(ext & 0xfff), reg);
    } else {
        gen_helper_m68k_movec_from(reg, cpu_env, tcg_const_i32(ext & 0xfff));
    }
    gen_exit_tb(s);
}

DISAS_INSN(intouch)
{
    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }
    /* ICache fetch.  Implement as no-op.  */
}

DISAS_INSN(cpushl)
{
    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }
    /* Cache push/invalidate.  Implement as no-op.  */
}

DISAS_INSN(cpush)
{
    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }
    /* Cache push/invalidate.  Implement as no-op.  */
}

DISAS_INSN(cinv)
{
    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }
    /* Invalidate cache line.  Implement as no-op.  */
}

#if defined(CONFIG_SOFTMMU)
DISAS_INSN(pflush)
{
    TCGv opmode;

    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }

    opmode = tcg_const_i32((insn >> 3) & 3);
    gen_helper_pflush(cpu_env, AREG(insn, 0), opmode);
    tcg_temp_free(opmode);
}

DISAS_INSN(ptest)
{
    TCGv is_read;

    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }
    is_read = tcg_const_i32((insn >> 5) & 1);
    gen_helper_ptest(cpu_env, AREG(insn, 0), is_read);
    tcg_temp_free(is_read);
}
#endif

DISAS_INSN(wddata)
{
    gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
}

DISAS_INSN(wdebug)
{
    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }
    /* TODO: Implement wdebug.  */
    cpu_abort(env_cpu(env), "WDEBUG not implemented");
}
#endif

DISAS_INSN(trap)
{
    gen_exception(s, s->pc, EXCP_TRAP0 + (insn & 0xf));
}

static void do_trapcc(DisasContext *s, DisasCompare *c)
{
    if (c->tcond != TCG_COND_NEVER) {
        TCGLabel *over = NULL;

        update_cc_op(s);

        if (c->tcond != TCG_COND_ALWAYS) {
            /* Jump over if !c. */
            over = gen_new_label();
            tcg_gen_brcond_i32(tcg_invert_cond(c->tcond), c->v1, c->v2, over);
        }

        tcg_gen_movi_i32(QREG_PC, s->pc);
        gen_raise_exception_format2(s, EXCP_TRAPCC, s->base.pc_next);

        if (over != NULL) {
            gen_set_label(over);
            s->base.is_jmp = DISAS_NEXT;
        }
    }
    free_cond(c);
}

DISAS_INSN(trapcc)
{
    DisasCompare c;

    /* Consume and discard the immediate operand. */
    switch (extract32(insn, 0, 3)) {
    case 2: /* trapcc.w */
        (void)read_im16(env, s);
        break;
    case 3: /* trapcc.l */
        (void)read_im32(env, s);
        break;
    case 4: /* trapcc (no operand) */
        break;
    default:
        /* trapcc registered with only valid opmodes */
        g_assert_not_reached();
    }

    gen_cc_cond(&c, s, extract32(insn, 8, 4));
    do_trapcc(s, &c);
}

DISAS_INSN(trapv)
{
    DisasCompare c;

    gen_cc_cond(&c, s, 9); /* V set */
    do_trapcc(s, &c);
}

static void gen_load_fcr(DisasContext *s, TCGv res, int reg)
{
    switch (reg) {
    case M68K_FPIAR:
        tcg_gen_movi_i32(res, 0);
        break;
    case M68K_FPSR:
        tcg_gen_ld_i32(res, cpu_env, offsetof(CPUM68KState, fpsr));
        break;
    case M68K_FPCR:
        tcg_gen_ld_i32(res, cpu_env, offsetof(CPUM68KState, fpcr));
        break;
    }
}

static void gen_store_fcr(DisasContext *s, TCGv val, int reg)
{
    switch (reg) {
    case M68K_FPIAR:
        break;
    case M68K_FPSR:
        tcg_gen_st_i32(val, cpu_env, offsetof(CPUM68KState, fpsr));
        break;
    case M68K_FPCR:
        gen_helper_set_fpcr(cpu_env, val);
        break;
    }
}

static void gen_qemu_store_fcr(DisasContext *s, TCGv addr, int reg)
{
    int index = IS_USER(s);
    TCGv tmp;

    tmp = tcg_temp_new();
    gen_load_fcr(s, tmp, reg);
    tcg_gen_qemu_st32(tmp, addr, index);
    tcg_temp_free(tmp);
}

static void gen_qemu_load_fcr(DisasContext *s, TCGv addr, int reg)
{
    int index = IS_USER(s);
    TCGv tmp;

    tmp = tcg_temp_new();
    tcg_gen_qemu_ld32u(tmp, addr, index);
    gen_store_fcr(s, tmp, reg);
    tcg_temp_free(tmp);
}


static void gen_op_fmove_fcr(CPUM68KState *env, DisasContext *s,
                             uint32_t insn, uint32_t ext)
{
    int mask = (ext >> 10) & 7;
    int is_write = (ext >> 13) & 1;
    int mode = extract32(insn, 3, 3);
    int i;
    TCGv addr, tmp;

    switch (mode) {
    case 0: /* Dn */
        if (mask != M68K_FPIAR && mask != M68K_FPSR && mask != M68K_FPCR) {
            gen_exception(s, s->base.pc_next, EXCP_ILLEGAL);
            return;
        }
        if (is_write) {
            gen_load_fcr(s, DREG(insn, 0), mask);
        } else {
            gen_store_fcr(s, DREG(insn, 0), mask);
        }
        return;
    case 1: /* An, only with FPIAR */
        if (mask != M68K_FPIAR) {
            gen_exception(s, s->base.pc_next, EXCP_ILLEGAL);
            return;
        }
        if (is_write) {
            gen_load_fcr(s, AREG(insn, 0), mask);
        } else {
            gen_store_fcr(s, AREG(insn, 0), mask);
        }
        return;
    case 7: /* Immediate */
        if (REG(insn, 0) == 4) {
            if (is_write ||
                (mask != M68K_FPIAR && mask != M68K_FPSR &&
                 mask != M68K_FPCR)) {
                gen_exception(s, s->base.pc_next, EXCP_ILLEGAL);
                return;
            }
            tmp = tcg_const_i32(read_im32(env, s));
            gen_store_fcr(s, tmp, mask);
            tcg_temp_free(tmp);
            return;
        }
        break;
    default:
        break;
    }

    tmp = gen_lea(env, s, insn, OS_LONG);
    if (IS_NULL_QREG(tmp)) {
        gen_addr_fault(s);
        return;
    }

    addr = tcg_temp_new();
    tcg_gen_mov_i32(addr, tmp);

    /*
     * mask:
     *
     * 0b100 Floating-Point Control Register
     * 0b010 Floating-Point Status Register
     * 0b001 Floating-Point Instruction Address Register
     *
     */

    if (is_write && mode == 4) {
        for (i = 2; i >= 0; i--, mask >>= 1) {
            if (mask & 1) {
                gen_qemu_store_fcr(s, addr, 1 << i);
                if (mask != 1) {
                    tcg_gen_subi_i32(addr, addr, opsize_bytes(OS_LONG));
                }
            }
       }
       tcg_gen_mov_i32(AREG(insn, 0), addr);
    } else {
        for (i = 0; i < 3; i++, mask >>= 1) {
            if (mask & 1) {
                if (is_write) {
                    gen_qemu_store_fcr(s, addr, 1 << i);
                } else {
                    gen_qemu_load_fcr(s, addr, 1 << i);
                }
                if (mask != 1 || mode == 3) {
                    tcg_gen_addi_i32(addr, addr, opsize_bytes(OS_LONG));
                }
            }
        }
        if (mode == 3) {
            tcg_gen_mov_i32(AREG(insn, 0), addr);
        }
    }
    tcg_temp_free_i32(addr);
}

static void gen_op_fmovem(CPUM68KState *env, DisasContext *s,
                          uint32_t insn, uint32_t ext)
{
    int opsize;
    TCGv addr, tmp;
    int mode = (ext >> 11) & 0x3;
    int is_load = ((ext & 0x2000) == 0);

    if (m68k_feature(s->env, M68K_FEATURE_FPU)) {
        opsize = OS_EXTENDED;
    } else {
        opsize = OS_DOUBLE;  /* FIXME */
    }

    addr = gen_lea(env, s, insn, opsize);
    if (IS_NULL_QREG(addr)) {
        gen_addr_fault(s);
        return;
    }

    tmp = tcg_temp_new();
    if (mode & 0x1) {
        /* Dynamic register list */
        tcg_gen_ext8u_i32(tmp, DREG(ext, 4));
    } else {
        /* Static register list */
        tcg_gen_movi_i32(tmp, ext & 0xff);
    }

    if (!is_load && (mode & 2) == 0) {
        /*
         * predecrement addressing mode
         * only available to store register to memory
         */
        if (opsize == OS_EXTENDED) {
            gen_helper_fmovemx_st_predec(tmp, cpu_env, addr, tmp);
        } else {
            gen_helper_fmovemd_st_predec(tmp, cpu_env, addr, tmp);
        }
    } else {
        /* postincrement addressing mode */
        if (opsize == OS_EXTENDED) {
            if (is_load) {
                gen_helper_fmovemx_ld_postinc(tmp, cpu_env, addr, tmp);
            } else {
                gen_helper_fmovemx_st_postinc(tmp, cpu_env, addr, tmp);
            }
        } else {
            if (is_load) {
                gen_helper_fmovemd_ld_postinc(tmp, cpu_env, addr, tmp);
            } else {
                gen_helper_fmovemd_st_postinc(tmp, cpu_env, addr, tmp);
            }
        }
    }
    if ((insn & 070) == 030 || (insn & 070) == 040) {
        tcg_gen_mov_i32(AREG(insn, 0), tmp);
    }
    tcg_temp_free(tmp);
}

/*
 * ??? FP exceptions are not implemented.  Most exceptions are deferred until
 * immediately before the next FP instruction is executed.
 */
DISAS_INSN(fpu)
{
    uint16_t ext;
    int opmode;
    int opsize;
    TCGv_ptr cpu_src, cpu_dest;

    ext = read_im16(env, s);
    opmode = ext & 0x7f;
    switch ((ext >> 13) & 7) {
    case 0:
        break;
    case 1:
        goto undef;
    case 2:
        if (insn == 0xf200 && (ext & 0xfc00) == 0x5c00) {
            /* fmovecr */
            TCGv rom_offset = tcg_const_i32(opmode);
            cpu_dest = gen_fp_ptr(REG(ext, 7));
            gen_helper_fconst(cpu_env, cpu_dest, rom_offset);
            tcg_temp_free_ptr(cpu_dest);
            tcg_temp_free(rom_offset);
            return;
        }
        break;
    case 3: /* fmove out */
        cpu_src = gen_fp_ptr(REG(ext, 7));
        opsize = ext_opsize(ext, 10);
        if (gen_ea_fp(env, s, insn, opsize, cpu_src,
                      EA_STORE, IS_USER(s)) == -1) {
            gen_addr_fault(s);
        }
        gen_helper_ftst(cpu_env, cpu_src);
        tcg_temp_free_ptr(cpu_src);
        return;
    case 4: /* fmove to control register.  */
    case 5: /* fmove from control register.  */
        gen_op_fmove_fcr(env, s, insn, ext);
        return;
    case 6: /* fmovem */
    case 7:
        if ((ext & 0x1000) == 0 && !m68k_feature(s->env, M68K_FEATURE_FPU)) {
            goto undef;
        }
        gen_op_fmovem(env, s, insn, ext);
        return;
    }
    if (ext & (1 << 14)) {
        /* Source effective address.  */
        opsize = ext_opsize(ext, 10);
        cpu_src = gen_fp_result_ptr();
        if (gen_ea_fp(env, s, insn, opsize, cpu_src,
                      EA_LOADS, IS_USER(s)) == -1) {
            gen_addr_fault(s);
            return;
        }
    } else {
        /* Source register.  */
        opsize = OS_EXTENDED;
        cpu_src = gen_fp_ptr(REG(ext, 10));
    }
    cpu_dest = gen_fp_ptr(REG(ext, 7));
    switch (opmode) {
    case 0: /* fmove */
        gen_fp_move(cpu_dest, cpu_src);
        break;
    case 0x40: /* fsmove */
        gen_helper_fsround(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x44: /* fdmove */
        gen_helper_fdround(cpu_env, cpu_dest, cpu_src);
        break;
    case 1: /* fint */
        gen_helper_firound(cpu_env, cpu_dest, cpu_src);
        break;
    case 2: /* fsinh */
        gen_helper_fsinh(cpu_env, cpu_dest, cpu_src);
        break;
    case 3: /* fintrz */
        gen_helper_fitrunc(cpu_env, cpu_dest, cpu_src);
        break;
    case 4: /* fsqrt */
        gen_helper_fsqrt(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x41: /* fssqrt */
        gen_helper_fssqrt(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x45: /* fdsqrt */
        gen_helper_fdsqrt(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x06: /* flognp1 */
        gen_helper_flognp1(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x08: /* fetoxm1 */
        gen_helper_fetoxm1(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x09: /* ftanh */
        gen_helper_ftanh(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x0a: /* fatan */
        gen_helper_fatan(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x0c: /* fasin */
        gen_helper_fasin(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x0d: /* fatanh */
        gen_helper_fatanh(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x0e: /* fsin */
        gen_helper_fsin(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x0f: /* ftan */
        gen_helper_ftan(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x10: /* fetox */
        gen_helper_fetox(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x11: /* ftwotox */
        gen_helper_ftwotox(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x12: /* ftentox */
        gen_helper_ftentox(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x14: /* flogn */
        gen_helper_flogn(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x15: /* flog10 */
        gen_helper_flog10(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x16: /* flog2 */
        gen_helper_flog2(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x18: /* fabs */
        gen_helper_fabs(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x58: /* fsabs */
        gen_helper_fsabs(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x5c: /* fdabs */
        gen_helper_fdabs(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x19: /* fcosh */
        gen_helper_fcosh(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x1a: /* fneg */
        gen_helper_fneg(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x5a: /* fsneg */
        gen_helper_fsneg(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x5e: /* fdneg */
        gen_helper_fdneg(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x1c: /* facos */
        gen_helper_facos(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x1d: /* fcos */
        gen_helper_fcos(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x1e: /* fgetexp */
        gen_helper_fgetexp(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x1f: /* fgetman */
        gen_helper_fgetman(cpu_env, cpu_dest, cpu_src);
        break;
    case 0x20: /* fdiv */
        gen_helper_fdiv(cpu_env, cpu_dest, cpu_src, cpu_dest);
        break;
    case 0x60: /* fsdiv */
        gen_helper_fsdiv(cpu_env, cpu_dest, cpu_src, cpu_dest);
        break;
    case 0x64: /* fddiv */
        gen_helper_fddiv(cpu_env, cpu_dest, cpu_src, cpu_dest);
        break;
    case 0x21: /* fmod */
        gen_helper_fmod(cpu_env, cpu_dest, cpu_src, cpu_dest);
        break;
    case 0x22: /* fadd */
        gen_helper_fadd(cpu_env, cpu_dest, cpu_src, cpu_dest);
        break;
    case 0x62: /* fsadd */
        gen_helper_fsadd(cpu_env, cpu_dest, cpu_src, cpu_dest);
        break;
    case 0x66: /* fdadd */
        gen_helper_fdadd(cpu_env, cpu_dest, cpu_src, cpu_dest);
        break;
    case 0x23: /* fmul */
        gen_helper_fmul(cpu_env, cpu_dest, cpu_src, cpu_dest);
        break;
    case 0x63: /* fsmul */
        gen_helper_fsmul(cpu_env, cpu_dest, cpu_src, cpu_dest);
        break;
    case 0x67: /* fdmul */
        gen_helper_fdmul(cpu_env, cpu_dest, cpu_src, cpu_dest);
        break;
    case 0x24: /* fsgldiv */
        gen_helper_fsgldiv(cpu_env, cpu_dest, cpu_src, cpu_dest);
        break;
    case 0x25: /* frem */
        gen_helper_frem(cpu_env, cpu_dest, cpu_src, cpu_dest);
        break;
    case 0x26: /* fscale */
        gen_helper_fscale(cpu_env, cpu_dest, cpu_src, cpu_dest);
        break;
    case 0x27: /* fsglmul */
        gen_helper_fsglmul(cpu_env, cpu_dest, cpu_src, cpu_dest);
        break;
    case 0x28: /* fsub */
        gen_helper_fsub(cpu_env, cpu_dest, cpu_src, cpu_dest);
        break;
    case 0x68: /* fssub */
        gen_helper_fssub(cpu_env, cpu_dest, cpu_src, cpu_dest);
        break;
    case 0x6c: /* fdsub */
        gen_helper_fdsub(cpu_env, cpu_dest, cpu_src, cpu_dest);
        break;
    case 0x30: case 0x31: case 0x32:
    case 0x33: case 0x34: case 0x35:
    case 0x36: case 0x37: {
            TCGv_ptr cpu_dest2 = gen_fp_ptr(REG(ext, 0));
            gen_helper_fsincos(cpu_env, cpu_dest, cpu_dest2, cpu_src);
            tcg_temp_free_ptr(cpu_dest2);
        }
        break;
    case 0x38: /* fcmp */
        gen_helper_fcmp(cpu_env, cpu_src, cpu_dest);
        return;
    case 0x3a: /* ftst */
        gen_helper_ftst(cpu_env, cpu_src);
        return;
    default:
        goto undef;
    }
    tcg_temp_free_ptr(cpu_src);
    gen_helper_ftst(cpu_env, cpu_dest);
    tcg_temp_free_ptr(cpu_dest);
    return;
undef:
    /* FIXME: Is this right for offset addressing modes?  */
    s->pc -= 2;
    disas_undef_fpu(env, s, insn);
}

static void gen_fcc_cond(DisasCompare *c, DisasContext *s, int cond)
{
    TCGv fpsr;

    c->g1 = 1;
    c->v2 = tcg_const_i32(0);
    c->g2 = 0;
    /* TODO: Raise BSUN exception.  */
    fpsr = tcg_temp_new();
    gen_load_fcr(s, fpsr, M68K_FPSR);
    switch (cond) {
    case 0:  /* False */
    case 16: /* Signaling False */
        c->v1 = c->v2;
        c->tcond = TCG_COND_NEVER;
        break;
    case 1:  /* EQual Z */
    case 17: /* Signaling EQual Z */
        c->v1 = tcg_temp_new();
        c->g1 = 0;
        tcg_gen_andi_i32(c->v1, fpsr, FPSR_CC_Z);
        c->tcond = TCG_COND_NE;
        break;
    case 2:  /* Ordered Greater Than !(A || Z || N) */
    case 18: /* Greater Than !(A || Z || N) */
        c->v1 = tcg_temp_new();
        c->g1 = 0;
        tcg_gen_andi_i32(c->v1, fpsr,
                         FPSR_CC_A | FPSR_CC_Z | FPSR_CC_N);
        c->tcond = TCG_COND_EQ;
        break;
    case 3:  /* Ordered Greater than or Equal Z || !(A || N) */
    case 19: /* Greater than or Equal Z || !(A || N) */
        c->v1 = tcg_temp_new();
        c->g1 = 0;
        tcg_gen_andi_i32(c->v1, fpsr, FPSR_CC_A);
        tcg_gen_shli_i32(c->v1, c->v1, ctz32(FPSR_CC_N) - ctz32(FPSR_CC_A));
        tcg_gen_andi_i32(fpsr, fpsr, FPSR_CC_Z | FPSR_CC_N);
        tcg_gen_or_i32(c->v1, c->v1, fpsr);
        tcg_gen_xori_i32(c->v1, c->v1, FPSR_CC_N);
        c->tcond = TCG_COND_NE;
        break;
    case 4:  /* Ordered Less Than !(!N || A || Z); */
    case 20: /* Less Than !(!N || A || Z); */
        c->v1 = tcg_temp_new();
        c->g1 = 0;
        tcg_gen_xori_i32(c->v1, fpsr, FPSR_CC_N);
        tcg_gen_andi_i32(c->v1, c->v1, FPSR_CC_N | FPSR_CC_A | FPSR_CC_Z);
        c->tcond = TCG_COND_EQ;
        break;
    case 5:  /* Ordered Less than or Equal Z || (N && !A) */
    case 21: /* Less than or Equal Z || (N && !A) */
        c->v1 = tcg_temp_new();
        c->g1 = 0;
        tcg_gen_andi_i32(c->v1, fpsr, FPSR_CC_A);
        tcg_gen_shli_i32(c->v1, c->v1, ctz32(FPSR_CC_N) - ctz32(FPSR_CC_A));
        tcg_gen_andc_i32(c->v1, fpsr, c->v1);
        tcg_gen_andi_i32(c->v1, c->v1, FPSR_CC_Z | FPSR_CC_N);
        c->tcond = TCG_COND_NE;
        break;
    case 6:  /* Ordered Greater or Less than !(A || Z) */
    case 22: /* Greater or Less than !(A || Z) */
        c->v1 = tcg_temp_new();
        c->g1 = 0;
        tcg_gen_andi_i32(c->v1, fpsr, FPSR_CC_A | FPSR_CC_Z);
        c->tcond = TCG_COND_EQ;
        break;
    case 7:  /* Ordered !A */
    case 23: /* Greater, Less or Equal !A */
        c->v1 = tcg_temp_new();
        c->g1 = 0;
        tcg_gen_andi_i32(c->v1, fpsr, FPSR_CC_A);
        c->tcond = TCG_COND_EQ;
        break;
    case 8:  /* Unordered A */
    case 24: /* Not Greater, Less or Equal A */
        c->v1 = tcg_temp_new();
        c->g1 = 0;
        tcg_gen_andi_i32(c->v1, fpsr, FPSR_CC_A);
        c->tcond = TCG_COND_NE;
        break;
    case 9:  /* Unordered or Equal A || Z */
    case 25: /* Not Greater or Less then A || Z */
        c->v1 = tcg_temp_new();
        c->g1 = 0;
        tcg_gen_andi_i32(c->v1, fpsr, FPSR_CC_A | FPSR_CC_Z);
        c->tcond = TCG_COND_NE;
        break;
    case 10: /* Unordered or Greater Than A || !(N || Z)) */
    case 26: /* Not Less or Equal A || !(N || Z)) */
        c->v1 = tcg_temp_new();
        c->g1 = 0;
        tcg_gen_andi_i32(c->v1, fpsr, FPSR_CC_Z);
        tcg_gen_shli_i32(c->v1, c->v1, ctz32(FPSR_CC_N) - ctz32(FPSR_CC_Z));
        tcg_gen_andi_i32(fpsr, fpsr, FPSR_CC_A | FPSR_CC_N);
        tcg_gen_or_i32(c->v1, c->v1, fpsr);
        tcg_gen_xori_i32(c->v1, c->v1, FPSR_CC_N);
        c->tcond = TCG_COND_NE;
        break;
    case 11: /* Unordered or Greater or Equal A || Z || !N */
    case 27: /* Not Less Than A || Z || !N */
        c->v1 = tcg_temp_new();
        c->g1 = 0;
        tcg_gen_andi_i32(c->v1, fpsr, FPSR_CC_A | FPSR_CC_Z | FPSR_CC_N);
        tcg_gen_xori_i32(c->v1, c->v1, FPSR_CC_N);
        c->tcond = TCG_COND_NE;
        break;
    case 12: /* Unordered or Less Than A || (N && !Z) */
    case 28: /* Not Greater than or Equal A || (N && !Z) */
        c->v1 = tcg_temp_new();
        c->g1 = 0;
        tcg_gen_andi_i32(c->v1, fpsr, FPSR_CC_Z);
        tcg_gen_shli_i32(c->v1, c->v1, ctz32(FPSR_CC_N) - ctz32(FPSR_CC_Z));
        tcg_gen_andc_i32(c->v1, fpsr, c->v1);
        tcg_gen_andi_i32(c->v1, c->v1, FPSR_CC_A | FPSR_CC_N);
        c->tcond = TCG_COND_NE;
        break;
    case 13: /* Unordered or Less or Equal A || Z || N */
    case 29: /* Not Greater Than A || Z || N */
        c->v1 = tcg_temp_new();
        c->g1 = 0;
        tcg_gen_andi_i32(c->v1, fpsr, FPSR_CC_A | FPSR_CC_Z | FPSR_CC_N);
        c->tcond = TCG_COND_NE;
        break;
    case 14: /* Not Equal !Z */
    case 30: /* Signaling Not Equal !Z */
        c->v1 = tcg_temp_new();
        c->g1 = 0;
        tcg_gen_andi_i32(c->v1, fpsr, FPSR_CC_Z);
        c->tcond = TCG_COND_EQ;
        break;
    case 15: /* True */
    case 31: /* Signaling True */
        c->v1 = c->v2;
        c->tcond = TCG_COND_ALWAYS;
        break;
    }
    tcg_temp_free(fpsr);
}

static void gen_fjmpcc(DisasContext *s, int cond, TCGLabel *l1)
{
    DisasCompare c;

    gen_fcc_cond(&c, s, cond);
    update_cc_op(s);
    tcg_gen_brcond_i32(c.tcond, c.v1, c.v2, l1);
    free_cond(&c);
}

DISAS_INSN(fbcc)
{
    uint32_t offset;
    uint32_t base;
    TCGLabel *l1;

    base = s->pc;
    offset = (int16_t)read_im16(env, s);
    if (insn & (1 << 6)) {
        offset = (offset << 16) | read_im16(env, s);
    }

    l1 = gen_new_label();
    update_cc_op(s);
    gen_fjmpcc(s, insn & 0x3f, l1);
    gen_jmp_tb(s, 0, s->pc, s->base.pc_next);
    gen_set_label(l1);
    gen_jmp_tb(s, 1, base + offset, s->base.pc_next);
}

DISAS_INSN(fscc)
{
    DisasCompare c;
    int cond;
    TCGv tmp;
    uint16_t ext;

    ext = read_im16(env, s);
    cond = ext & 0x3f;
    gen_fcc_cond(&c, s, cond);

    tmp = tcg_temp_new();
    tcg_gen_setcond_i32(c.tcond, tmp, c.v1, c.v2);
    free_cond(&c);

    tcg_gen_neg_i32(tmp, tmp);
    DEST_EA(env, insn, OS_BYTE, tmp, NULL);
    tcg_temp_free(tmp);
}

DISAS_INSN(ftrapcc)
{
    DisasCompare c;
    uint16_t ext;
    int cond;

    ext = read_im16(env, s);
    cond = ext & 0x3f;

    /* Consume and discard the immediate operand. */
    switch (extract32(insn, 0, 3)) {
    case 2: /* ftrapcc.w */
        (void)read_im16(env, s);
        break;
    case 3: /* ftrapcc.l */
        (void)read_im32(env, s);
        break;
    case 4: /* ftrapcc (no operand) */
        break;
    default:
        /* ftrapcc registered with only valid opmodes */
        g_assert_not_reached();
    }

    gen_fcc_cond(&c, s, cond);
    do_trapcc(s, &c);
}

#if defined(CONFIG_SOFTMMU)
DISAS_INSN(frestore)
{
    TCGv addr;

    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }
    if (m68k_feature(s->env, M68K_FEATURE_M68040)) {
        SRC_EA(env, addr, OS_LONG, 0, NULL);
        /* FIXME: check the state frame */
    } else {
        disas_undef(env, s, insn);
    }
}

DISAS_INSN(fsave)
{
    if (IS_USER(s)) {
        gen_exception(s, s->base.pc_next, EXCP_PRIVILEGE);
        return;
    }

    if (m68k_feature(s->env, M68K_FEATURE_M68040)) {
        /* always write IDLE */
        TCGv idle = tcg_const_i32(0x41000000);
        DEST_EA(env, insn, OS_LONG, idle, NULL);
        tcg_temp_free(idle);
    } else {
        disas_undef(env, s, insn);
    }
}
#endif

static inline TCGv gen_mac_extract_word(DisasContext *s, TCGv val, int upper)
{
    TCGv tmp = tcg_temp_new();
    if (s->env->macsr & MACSR_FI) {
        if (upper)
            tcg_gen_andi_i32(tmp, val, 0xffff0000);
        else
            tcg_gen_shli_i32(tmp, val, 16);
    } else if (s->env->macsr & MACSR_SU) {
        if (upper)
            tcg_gen_sari_i32(tmp, val, 16);
        else
            tcg_gen_ext16s_i32(tmp, val);
    } else {
        if (upper)
            tcg_gen_shri_i32(tmp, val, 16);
        else
            tcg_gen_ext16u_i32(tmp, val);
    }
    return tmp;
}

static void gen_mac_clear_flags(void)
{
    tcg_gen_andi_i32(QREG_MACSR, QREG_MACSR,
                     ~(MACSR_V | MACSR_Z | MACSR_N | MACSR_EV));
}

DISAS_INSN(mac)
{
    TCGv rx;
    TCGv ry;
    uint16_t ext;
    int acc;
    TCGv tmp;
    TCGv addr;
    TCGv loadval;
    int dual;
    TCGv saved_flags;

    if (!s->done_mac) {
        s->mactmp = tcg_temp_new_i64();
        s->done_mac = 1;
    }

    ext = read_im16(env, s);

    acc = ((insn >> 7) & 1) | ((ext >> 3) & 2);
    dual = ((insn & 0x30) != 0 && (ext & 3) != 0);
    if (dual && !m68k_feature(s->env, M68K_FEATURE_CF_EMAC_B)) {
        disas_undef(env, s, insn);
        return;
    }
    if (insn & 0x30) {
        /* MAC with load.  */
        tmp = gen_lea(env, s, insn, OS_LONG);
        addr = tcg_temp_new();
        tcg_gen_and_i32(addr, tmp, QREG_MAC_MASK);
        /*
         * Load the value now to ensure correct exception behavior.
         * Perform writeback after reading the MAC inputs.
         */
        loadval = gen_load(s, OS_LONG, addr, 0, IS_USER(s));

        acc ^= 1;
        rx = (ext & 0x8000) ? AREG(ext, 12) : DREG(insn, 12);
        ry = (ext & 8) ? AREG(ext, 0) : DREG(ext, 0);
    } else {
        loadval = addr = NULL_QREG;
        rx = (insn & 0x40) ? AREG(insn, 9) : DREG(insn, 9);
        ry = (insn & 8) ? AREG(insn, 0) : DREG(insn, 0);
    }

    gen_mac_clear_flags();
#if 0
    l1 = -1;
    /* Disabled because conditional branches clobber temporary vars.  */
    if ((s->env->macsr & MACSR_OMC) != 0 && !dual) {
        /* Skip the multiply if we know we will ignore it.  */
        l1 = gen_new_label();
        tmp = tcg_temp_new();
        tcg_gen_andi_i32(tmp, QREG_MACSR, 1 << (acc + 8));
        gen_op_jmp_nz32(tmp, l1);
    }
#endif

    if ((ext & 0x0800) == 0) {
        /* Word.  */
        rx = gen_mac_extract_word(s, rx, (ext & 0x80) != 0);
        ry = gen_mac_extract_word(s, ry, (ext & 0x40) != 0);
    }
    if (s->env->macsr & MACSR_FI) {
        gen_helper_macmulf(s->mactmp, cpu_env, rx, ry);
    } else {
        if (s->env->macsr & MACSR_SU)
            gen_helper_macmuls(s->mactmp, cpu_env, rx, ry);
        else
            gen_helper_macmulu(s->mactmp, cpu_env, rx, ry);
        switch ((ext >> 9) & 3) {
        case 1:
            tcg_gen_shli_i64(s->mactmp, s->mactmp, 1);
            break;
        case 3:
            tcg_gen_shri_i64(s->mactmp, s->mactmp, 1);
            break;
        }
    }

    if (dual) {
        /* Save the overflow flag from the multiply.  */
        saved_flags = tcg_temp_new();
        tcg_gen_mov_i32(saved_flags, QREG_MACSR);
    } else {
        saved_flags = NULL_QREG;
    }

#if 0
    /* Disabled because conditional branches clobber temporary vars.  */
    if ((s->env->macsr & MACSR_OMC) != 0 && dual) {
        /* Skip the accumulate if the value is already saturated.  */
        l1 = gen_new_label();
        tmp = tcg_temp_new();
        gen_op_and32(tmp, QREG_MACSR, tcg_const_i32(MACSR_PAV0 << acc));
        gen_op_jmp_nz32(tmp, l1);
    }
#endif

    if (insn & 0x100)
        tcg_gen_sub_i64(MACREG(acc), MACREG(acc), s->mactmp);
    else
        tcg_gen_add_i64(MACREG(acc), MACREG(acc), s->mactmp);

    if (s->env->macsr & MACSR_FI)
        gen_helper_macsatf(cpu_env, tcg_const_i32(acc));
    else if (s->env->macsr & MACSR_SU)
        gen_helper_macsats(cpu_env, tcg_const_i32(acc));
    else
        gen_helper_macsatu(cpu_env, tcg_const_i32(acc));

#if 0
    /* Disabled because conditional branches clobber temporary vars.  */
    if (l1 != -1)
        gen_set_label(l1);
#endif

    if (dual) {
        /* Dual accumulate variant.  */
        acc = (ext >> 2) & 3;
        /* Restore the overflow flag from the multiplier.  */
        tcg_gen_mov_i32(QREG_MACSR, saved_flags);
#if 0
        /* Disabled because conditional branches clobber temporary vars.  */
        if ((s->env->macsr & MACSR_OMC) != 0) {
            /* Skip the accumulate if the value is already saturated.  */
            l1 = gen_new_label();
            tmp = tcg_temp_new();
            gen_op_and32(tmp, QREG_MACSR, tcg_const_i32(MACSR_PAV0 << acc));
            gen_op_jmp_nz32(tmp, l1);
        }
#endif
        if (ext & 2)
            tcg_gen_sub_i64(MACREG(acc), MACREG(acc), s->mactmp);
        else
            tcg_gen_add_i64(MACREG(acc), MACREG(acc), s->mactmp);
        if (s->env->macsr & MACSR_FI)
            gen_helper_macsatf(cpu_env, tcg_const_i32(acc));
        else if (s->env->macsr & MACSR_SU)
            gen_helper_macsats(cpu_env, tcg_const_i32(acc));
        else
            gen_helper_macsatu(cpu_env, tcg_const_i32(acc));
#if 0
        /* Disabled because conditional branches clobber temporary vars.  */
        if (l1 != -1)
            gen_set_label(l1);
#endif
    }
    gen_helper_mac_set_flags(cpu_env, tcg_const_i32(acc));

    if (insn & 0x30) {
        TCGv rw;
        rw = (insn & 0x40) ? AREG(insn, 9) : DREG(insn, 9);
        tcg_gen_mov_i32(rw, loadval);
        /*
         * FIXME: Should address writeback happen with the masked or
         * unmasked value?
         */
        switch ((insn >> 3) & 7) {
        case 3: /* Post-increment.  */
            tcg_gen_addi_i32(AREG(insn, 0), addr, 4);
            break;
        case 4: /* Pre-decrement.  */
            tcg_gen_mov_i32(AREG(insn, 0), addr);
        }
        tcg_temp_free(loadval);
    }
}

DISAS_INSN(from_mac)
{
    TCGv rx;
    TCGv_i64 acc;
    int accnum;

    rx = (insn & 8) ? AREG(insn, 0) : DREG(insn, 0);
    accnum = (insn >> 9) & 3;
    acc = MACREG(accnum);
    if (s->env->macsr & MACSR_FI) {
        gen_helper_get_macf(rx, cpu_env, acc);
    } else if ((s->env->macsr & MACSR_OMC) == 0) {
        tcg_gen_extrl_i64_i32(rx, acc);
    } else if (s->env->macsr & MACSR_SU) {
        gen_helper_get_macs(rx, acc);
    } else {
        gen_helper_get_macu(rx, acc);
    }
    if (insn & 0x40) {
        tcg_gen_movi_i64(acc, 0);
        tcg_gen_andi_i32(QREG_MACSR, QREG_MACSR, ~(MACSR_PAV0 << accnum));
    }
}

DISAS_INSN(move_mac)
{
    /* FIXME: This can be done without a helper.  */
    int src;
    TCGv dest;
    src = insn & 3;
    dest = tcg_const_i32((insn >> 9) & 3);
    gen_helper_mac_move(cpu_env, dest, tcg_const_i32(src));
    gen_mac_clear_flags();
    gen_helper_mac_set_flags(cpu_env, dest);
}

DISAS_INSN(from_macsr)
{
    TCGv reg;

    reg = (insn & 8) ? AREG(insn, 0) : DREG(insn, 0);
    tcg_gen_mov_i32(reg, QREG_MACSR);
}

DISAS_INSN(from_mask)
{
    TCGv reg;
    reg = (insn & 8) ? AREG(insn, 0) : DREG(insn, 0);
    tcg_gen_mov_i32(reg, QREG_MAC_MASK);
}

DISAS_INSN(from_mext)
{
    TCGv reg;
    TCGv acc;
    reg = (insn & 8) ? AREG(insn, 0) : DREG(insn, 0);
    acc = tcg_const_i32((insn & 0x400) ? 2 : 0);
    if (s->env->macsr & MACSR_FI)
        gen_helper_get_mac_extf(reg, cpu_env, acc);
    else
        gen_helper_get_mac_exti(reg, cpu_env, acc);
}

DISAS_INSN(macsr_to_ccr)
{
    TCGv tmp = tcg_temp_new();

    /* Note that X and C are always cleared. */
    tcg_gen_andi_i32(tmp, QREG_MACSR, CCF_N | CCF_Z | CCF_V);
    gen_helper_set_ccr(cpu_env, tmp);
    tcg_temp_free(tmp);
    set_cc_op(s, CC_OP_FLAGS);
}

DISAS_INSN(to_mac)
{
    TCGv_i64 acc;
    TCGv val;
    int accnum;
    accnum = (insn >> 9) & 3;
    acc = MACREG(accnum);
    SRC_EA(env, val, OS_LONG, 0, NULL);
    if (s->env->macsr & MACSR_FI) {
        tcg_gen_ext_i32_i64(acc, val);
        tcg_gen_shli_i64(acc, acc, 8);
    } else if (s->env->macsr & MACSR_SU) {
        tcg_gen_ext_i32_i64(acc, val);
    } else {
        tcg_gen_extu_i32_i64(acc, val);
    }
    tcg_gen_andi_i32(QREG_MACSR, QREG_MACSR, ~(MACSR_PAV0 << accnum));
    gen_mac_clear_flags();
    gen_helper_mac_set_flags(cpu_env, tcg_const_i32(accnum));
}

DISAS_INSN(to_macsr)
{
    TCGv val;
    SRC_EA(env, val, OS_LONG, 0, NULL);
    gen_helper_set_macsr(cpu_env, val);
    gen_exit_tb(s);
}

DISAS_INSN(to_mask)
{
    TCGv val;
    SRC_EA(env, val, OS_LONG, 0, NULL);
    tcg_gen_ori_i32(QREG_MAC_MASK, val, 0xffff0000);
}

DISAS_INSN(to_mext)
{
    TCGv val;
    TCGv acc;
    SRC_EA(env, val, OS_LONG, 0, NULL);
    acc = tcg_const_i32((insn & 0x400) ? 2 : 0);
    if (s->env->macsr & MACSR_FI)
        gen_helper_set_mac_extf(cpu_env, val, acc);
    else if (s->env->macsr & MACSR_SU)
        gen_helper_set_mac_exts(cpu_env, val, acc);
    else
        gen_helper_set_mac_extu(cpu_env, val, acc);
}

static disas_proc opcode_table[65536];

static void
register_opcode (disas_proc proc, uint16_t opcode, uint16_t mask)
{
  int i;
  int from;
  int to;

  /* Sanity check.  All set bits must be included in the mask.  */
  if (opcode & ~mask) {
      fprintf(stderr,
              "qemu internal error: bogus opcode definition %04x/%04x\n",
              opcode, mask);
      abort();
  }
  /*
   * This could probably be cleverer.  For now just optimize the case where
   * the top bits are known.
   */
  /* Find the first zero bit in the mask.  */
  i = 0x8000;
  while ((i & mask) != 0)
      i >>= 1;
  /* Iterate over all combinations of this and lower bits.  */
  if (i == 0)
      i = 1;
  else
      i <<= 1;
  from = opcode & ~(i - 1);
  to = from + i;
  for (i = from; i < to; i++) {
      if ((i & mask) == opcode)
          opcode_table[i] = proc;
  }
}

/*
 * Register m68k opcode handlers.  Order is important.
 * Later insn override earlier ones.
 */
void register_m68k_insns (CPUM68KState *env)
{
    /*
     * Build the opcode table only once to avoid
     * multithreading issues.
     */
    if (opcode_table[0] != NULL) {
        return;
    }

    /*
     * use BASE() for instruction available
     * for CF_ISA_A and M68000.
     */
#define BASE(name, opcode, mask) \
    register_opcode(disas_##name, 0x##opcode, 0x##mask)
#define INSN(name, opcode, mask, feature) do { \
    if (m68k_feature(env, M68K_FEATURE_##feature)) \
        BASE(name, opcode, mask); \
    } while(0)
    BASE(undef,     0000, 0000);
    INSN(arith_im,  0080, fff8, CF_ISA_A);
    INSN(arith_im,  0000, ff00, M68K);
    INSN(chk2,      00c0, f9c0, CHK2);
    INSN(bitrev,    00c0, fff8, CF_ISA_APLUSC);
    BASE(bitop_reg, 0100, f1c0);
    BASE(bitop_reg, 0140, f1c0);
    BASE(bitop_reg, 0180, f1c0);
    BASE(bitop_reg, 01c0, f1c0);
    INSN(movep,     0108, f138, MOVEP);
    INSN(arith_im,  0280, fff8, CF_ISA_A);
    INSN(arith_im,  0200, ff00, M68K);
    INSN(undef,     02c0, ffc0, M68K);
    INSN(byterev,   02c0, fff8, CF_ISA_APLUSC);
    INSN(arith_im,  0480, fff8, CF_ISA_A);
    INSN(arith_im,  0400, ff00, M68K);
    INSN(undef,     04c0, ffc0, M68K);
    INSN(arith_im,  0600, ff00, M68K);
    INSN(undef,     06c0, ffc0, M68K);
    INSN(ff1,       04c0, fff8, CF_ISA_APLUSC);
    INSN(arith_im,  0680, fff8, CF_ISA_A);
    INSN(arith_im,  0c00, ff38, CF_ISA_A);
    INSN(arith_im,  0c00, ff00, M68K);
    BASE(bitop_im,  0800, ffc0);
    BASE(bitop_im,  0840, ffc0);
    BASE(bitop_im,  0880, ffc0);
    BASE(bitop_im,  08c0, ffc0);
    INSN(arith_im,  0a80, fff8, CF_ISA_A);
    INSN(arith_im,  0a00, ff00, M68K);
#if defined(CONFIG_SOFTMMU)
    INSN(moves,     0e00, ff00, M68K);
#endif
    INSN(cas,       0ac0, ffc0, CAS);
    INSN(cas,       0cc0, ffc0, CAS);
    INSN(cas,       0ec0, ffc0, CAS);
    INSN(cas2w,     0cfc, ffff, CAS);
    INSN(cas2l,     0efc, ffff, CAS);
    BASE(move,      1000, f000);
    BASE(move,      2000, f000);
    BASE(move,      3000, f000);
    INSN(chk,       4000, f040, M68K);
    INSN(strldsr,   40e7, ffff, CF_ISA_APLUSC);
    INSN(negx,      4080, fff8, CF_ISA_A);
    INSN(negx,      4000, ff00, M68K);
    INSN(undef,     40c0, ffc0, M68K);
    INSN(move_from_sr, 40c0, fff8, CF_ISA_A);
    INSN(move_from_sr, 40c0, ffc0, M68K);
    BASE(lea,       41c0, f1c0);
    BASE(clr,       4200, ff00);
    BASE(undef,     42c0, ffc0);
    INSN(move_from_ccr, 42c0, fff8, CF_ISA_A);
    INSN(move_from_ccr, 42c0, ffc0, M68K);
    INSN(neg,       4480, fff8, CF_ISA_A);
    INSN(neg,       4400, ff00, M68K);
    INSN(undef,     44c0, ffc0, M68K);
    BASE(move_to_ccr, 44c0, ffc0);
    INSN(not,       4680, fff8, CF_ISA_A);
    INSN(not,       4600, ff00, M68K);
#if defined(CONFIG_SOFTMMU)
    BASE(move_to_sr, 46c0, ffc0);
#endif
    INSN(nbcd,      4800, ffc0, M68K);
    INSN(linkl,     4808, fff8, M68K);
    BASE(pea,       4840, ffc0);
    BASE(swap,      4840, fff8);
    INSN(bkpt,      4848, fff8, BKPT);
    INSN(movem,     48d0, fbf8, CF_ISA_A);
    INSN(movem,     48e8, fbf8, CF_ISA_A);
    INSN(movem,     4880, fb80, M68K);
    BASE(ext,       4880, fff8);
    BASE(ext,       48c0, fff8);
    BASE(ext,       49c0, fff8);
    BASE(tst,       4a00, ff00);
    INSN(tas,       4ac0, ffc0, CF_ISA_B);
    INSN(tas,       4ac0, ffc0, M68K);
#if defined(CONFIG_SOFTMMU)
    INSN(halt,      4ac8, ffff, CF_ISA_A);
    INSN(halt,      4ac8, ffff, M68K);
#endif
    INSN(pulse,     4acc, ffff, CF_ISA_A);
    BASE(illegal,   4afc, ffff);
    INSN(mull,      4c00, ffc0, CF_ISA_A);
    INSN(mull,      4c00, ffc0, LONG_MULDIV);
    INSN(divl,      4c40, ffc0, CF_ISA_A);
    INSN(divl,      4c40, ffc0, LONG_MULDIV);
    INSN(sats,      4c80, fff8, CF_ISA_B);
    BASE(trap,      4e40, fff0);
    BASE(link,      4e50, fff8);
    BASE(unlk,      4e58, fff8);
#if defined(CONFIG_SOFTMMU)
    INSN(move_to_usp, 4e60, fff8, USP);
    INSN(move_from_usp, 4e68, fff8, USP);
    INSN(reset,     4e70, ffff, M68K);
    BASE(stop,      4e72, ffff);
    BASE(rte,       4e73, ffff);
    INSN(cf_movec,  4e7b, ffff, CF_ISA_A);
    INSN(m68k_movec, 4e7a, fffe, MOVEC);
#endif
    BASE(nop,       4e71, ffff);
    INSN(rtd,       4e74, ffff, RTD);
    BASE(rts,       4e75, ffff);
    INSN(trapv,     4e76, ffff, M68K);
    INSN(rtr,       4e77, ffff, M68K);
    BASE(jump,      4e80, ffc0);
    BASE(jump,      4ec0, ffc0);
    INSN(addsubq,   5000, f080, M68K);
    BASE(addsubq,   5080, f0c0);
    INSN(scc,       50c0, f0f8, CF_ISA_A); /* Scc.B Dx   */
    INSN(scc,       50c0, f0c0, M68K);     /* Scc.B <EA> */
    INSN(dbcc,      50c8, f0f8, M68K);
    INSN(trapcc,    50fa, f0fe, TRAPCC);   /* opmode 010, 011 */
    INSN(trapcc,    50fc, f0ff, TRAPCC);   /* opmode 100 */
    INSN(trapcc,    51fa, fffe, CF_ISA_A); /* TPF (trapf) opmode 010, 011 */
    INSN(trapcc,    51fc, ffff, CF_ISA_A); /* TPF (trapf) opmode 100 */

    /* Branch instructions.  */
    BASE(branch,    6000, f000);
    /* Disable long branch instructions, then add back the ones we want.  */
    BASE(undef,     60ff, f0ff); /* All long branches.  */
    INSN(branch,    60ff, f0ff, CF_ISA_B);
    INSN(undef,     60ff, ffff, CF_ISA_B); /* bra.l */
    INSN(branch,    60ff, ffff, BRAL);
    INSN(branch,    60ff, f0ff, BCCL);

    BASE(moveq,     7000, f100);
    INSN(mvzs,      7100, f100, CF_ISA_B);
    BASE(or,        8000, f000);
    BASE(divw,      80c0, f0c0);
    INSN(sbcd_reg,  8100, f1f8, M68K);
    INSN(sbcd_mem,  8108, f1f8, M68K);
    BASE(addsub,    9000, f000);
    INSN(undef,     90c0, f0c0, CF_ISA_A);
    INSN(subx_reg,  9180, f1f8, CF_ISA_A);
    INSN(subx_reg,  9100, f138, M68K);
    INSN(subx_mem,  9108, f138, M68K);
    INSN(suba,      91c0, f1c0, CF_ISA_A);
    INSN(suba,      90c0, f0c0, M68K);

    BASE(undef_mac, a000, f000);
    INSN(mac,       a000, f100, CF_EMAC);
    INSN(from_mac,  a180, f9b0, CF_EMAC);
    INSN(move_mac,  a110, f9fc, CF_EMAC);
    INSN(from_macsr,a980, f9f0, CF_EMAC);
    INSN(from_mask, ad80, fff0, CF_EMAC);
    INSN(from_mext, ab80, fbf0, CF_EMAC);
    INSN(macsr_to_ccr, a9c0, ffff, CF_EMAC);
    INSN(to_mac,    a100, f9c0, CF_EMAC);
    INSN(to_macsr,  a900, ffc0, CF_EMAC);
    INSN(to_mext,   ab00, fbc0, CF_EMAC);
    INSN(to_mask,   ad00, ffc0, CF_EMAC);

    INSN(mov3q,     a140, f1c0, CF_ISA_B);
    INSN(cmp,       b000, f1c0, CF_ISA_B); /* cmp.b */
    INSN(cmp,       b040, f1c0, CF_ISA_B); /* cmp.w */
    INSN(cmpa,      b0c0, f1c0, CF_ISA_B); /* cmpa.w */
    INSN(cmp,       b080, f1c0, CF_ISA_A);
    INSN(cmpa,      b1c0, f1c0, CF_ISA_A);
    INSN(cmp,       b000, f100, M68K);
    INSN(eor,       b100, f100, M68K);
    INSN(cmpm,      b108, f138, M68K);
    INSN(cmpa,      b0c0, f0c0, M68K);
    INSN(eor,       b180, f1c0, CF_ISA_A);
    BASE(and,       c000, f000);
    INSN(exg_dd,    c140, f1f8, M68K);
    INSN(exg_aa,    c148, f1f8, M68K);
    INSN(exg_da,    c188, f1f8, M68K);
    BASE(mulw,      c0c0, f0c0);
    INSN(abcd_reg,  c100, f1f8, M68K);
    INSN(abcd_mem,  c108, f1f8, M68K);
    BASE(addsub,    d000, f000);
    INSN(undef,     d0c0, f0c0, CF_ISA_A);
    INSN(addx_reg,      d180, f1f8, CF_ISA_A);
    INSN(addx_reg,  d100, f138, M68K);
    INSN(addx_mem,  d108, f138, M68K);
    INSN(adda,      d1c0, f1c0, CF_ISA_A);
    INSN(adda,      d0c0, f0c0, M68K);
    INSN(shift_im,  e080, f0f0, CF_ISA_A);
    INSN(shift_reg, e0a0, f0f0, CF_ISA_A);
    INSN(shift8_im, e000, f0f0, M68K);
    INSN(shift16_im, e040, f0f0, M68K);
    INSN(shift_im,  e080, f0f0, M68K);
    INSN(shift8_reg, e020, f0f0, M68K);
    INSN(shift16_reg, e060, f0f0, M68K);
    INSN(shift_reg, e0a0, f0f0, M68K);
    INSN(shift_mem, e0c0, fcc0, M68K);
    INSN(rotate_im, e090, f0f0, M68K);
    INSN(rotate8_im, e010, f0f0, M68K);
    INSN(rotate16_im, e050, f0f0, M68K);
    INSN(rotate_reg, e0b0, f0f0, M68K);
    INSN(rotate8_reg, e030, f0f0, M68K);
    INSN(rotate16_reg, e070, f0f0, M68K);
    INSN(rotate_mem, e4c0, fcc0, M68K);
    INSN(bfext_mem, e9c0, fdc0, BITFIELD);  /* bfextu & bfexts */
    INSN(bfext_reg, e9c0, fdf8, BITFIELD);
    INSN(bfins_mem, efc0, ffc0, BITFIELD);
    INSN(bfins_reg, efc0, fff8, BITFIELD);
    INSN(bfop_mem, eac0, ffc0, BITFIELD);   /* bfchg */
    INSN(bfop_reg, eac0, fff8, BITFIELD);   /* bfchg */
    INSN(bfop_mem, ecc0, ffc0, BITFIELD);   /* bfclr */
    INSN(bfop_reg, ecc0, fff8, BITFIELD);   /* bfclr */
    INSN(bfop_mem, edc0, ffc0, BITFIELD);   /* bfffo */
    INSN(bfop_reg, edc0, fff8, BITFIELD);   /* bfffo */
    INSN(bfop_mem, eec0, ffc0, BITFIELD);   /* bfset */
    INSN(bfop_reg, eec0, fff8, BITFIELD);   /* bfset */
    INSN(bfop_mem, e8c0, ffc0, BITFIELD);   /* bftst */
    INSN(bfop_reg, e8c0, fff8, BITFIELD);   /* bftst */
    BASE(undef_fpu, f000, f000);
    INSN(fpu,       f200, ffc0, CF_FPU);
    INSN(fbcc,      f280, ffc0, CF_FPU);
    INSN(fpu,       f200, ffc0, FPU);
    INSN(fscc,      f240, ffc0, FPU);
    INSN(ftrapcc,   f27a, fffe, FPU);       /* opmode 010, 011 */
    INSN(ftrapcc,   f27c, ffff, FPU);       /* opmode 100 */
    INSN(fbcc,      f280, ff80, FPU);
#if defined(CONFIG_SOFTMMU)
    INSN(frestore,  f340, ffc0, CF_FPU);
    INSN(fsave,     f300, ffc0, CF_FPU);
    INSN(frestore,  f340, ffc0, FPU);
    INSN(fsave,     f300, ffc0, FPU);
    INSN(intouch,   f340, ffc0, CF_ISA_A);
    INSN(cpushl,    f428, ff38, CF_ISA_A);
    INSN(cpush,     f420, ff20, M68040);
    INSN(cinv,      f400, ff20, M68040);
    INSN(pflush,    f500, ffe0, M68040);
    INSN(ptest,     f548, ffd8, M68040);
    INSN(wddata,    fb00, ff00, CF_ISA_A);
    INSN(wdebug,    fbc0, ffc0, CF_ISA_A);
#endif
    INSN(move16_mem, f600, ffe0, M68040);
    INSN(move16_reg, f620, fff8, M68040);
#undef INSN
}

static void m68k_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUM68KState *env = cpu->env_ptr;

    dc->env = env;
    dc->pc = dc->base.pc_first;
    /* This value will always be filled in properly before m68k_tr_tb_stop. */
    dc->pc_prev = 0xdeadbeef;
    dc->cc_op = CC_OP_DYNAMIC;
    dc->cc_op_synced = 1;
    dc->done_mac = 0;
    dc->writeback_mask = 0;

    dc->ss_active = (M68K_SR_TRACE(env->sr) == M68K_SR_TRACE_ANY_INS);
    /* If architectural single step active, limit to 1 */
    if (dc->ss_active) {
        dc->base.max_insns = 1;
    }
}

static void m68k_tr_tb_start(DisasContextBase *dcbase, CPUState *cpu)
{
}

static void m68k_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    tcg_gen_insn_start(dc->base.pc_next, dc->cc_op);
}

static void m68k_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUM68KState *env = cpu->env_ptr;
    uint16_t insn = read_im16(env, dc);

    opcode_table[insn](env, dc, insn);
    do_writebacks(dc);

    dc->pc_prev = dc->base.pc_next;
    dc->base.pc_next = dc->pc;

    if (dc->base.is_jmp == DISAS_NEXT) {
        /*
         * Stop translation when the next insn might touch a new page.
         * This ensures that prefetch aborts at the right place.
         *
         * We cannot determine the size of the next insn without
         * completely decoding it.  However, the maximum insn size
         * is 32 bytes, so end if we do not have that much remaining.
         * This may produce several small TBs at the end of each page,
         * but they will all be linked with goto_tb.
         *
         * ??? ColdFire maximum is 4 bytes; MC68000's maximum is also
         * smaller than MC68020's.
         */
        target_ulong start_page_offset
            = dc->pc - (dc->base.pc_first & TARGET_PAGE_MASK);

        if (start_page_offset >= TARGET_PAGE_SIZE - 32) {
            dc->base.is_jmp = DISAS_TOO_MANY;
        }
    }
}

static void m68k_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    switch (dc->base.is_jmp) {
    case DISAS_NORETURN:
        break;
    case DISAS_TOO_MANY:
        update_cc_op(dc);
        gen_jmp_tb(dc, 0, dc->pc, dc->pc_prev);
        break;
    case DISAS_JUMP:
        /* We updated CC_OP and PC in gen_jmp/gen_jmp_im.  */
        if (dc->ss_active) {
            gen_raise_exception_format2(dc, EXCP_TRACE, dc->pc_prev);
        } else {
            tcg_gen_lookup_and_goto_ptr();
        }
        break;
    case DISAS_EXIT:
        /*
         * We updated CC_OP and PC in gen_exit_tb, but also modified
         * other state that may require returning to the main loop.
         */
        if (dc->ss_active) {
            gen_raise_exception_format2(dc, EXCP_TRACE, dc->pc_prev);
        } else {
            tcg_gen_exit_tb(NULL, 0);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void m68k_tr_disas_log(const DisasContextBase *dcbase,
                              CPUState *cpu, FILE *logfile)
{
    fprintf(logfile, "IN: %s\n", lookup_symbol(dcbase->pc_first));
    target_disas(logfile, cpu, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps m68k_tr_ops = {
    .init_disas_context = m68k_tr_init_disas_context,
    .tb_start           = m68k_tr_tb_start,
    .insn_start         = m68k_tr_insn_start,
    .translate_insn     = m68k_tr_translate_insn,
    .tb_stop            = m68k_tr_tb_stop,
    .disas_log          = m68k_tr_disas_log,
};

void gen_intermediate_code(CPUState *cpu, TranslationBlock *tb, int *max_insns,
                           target_ulong pc, void *host_pc)
{
    DisasContext dc;
    translator_loop(cpu, tb, max_insns, pc, host_pc, &m68k_tr_ops, &dc.base);
}

static double floatx80_to_double(CPUM68KState *env, uint16_t high, uint64_t low)
{
    floatx80 a = { .high = high, .low = low };
    union {
        float64 f64;
        double d;
    } u;

    u.f64 = floatx80_to_float64(a, &env->fp_status);
    return u.d;
}

void m68k_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    M68kCPU *cpu = M68K_CPU(cs);
    CPUM68KState *env = &cpu->env;
    int i;
    uint16_t sr;
    for (i = 0; i < 8; i++) {
        qemu_fprintf(f, "D%d = %08x   A%d = %08x   "
                     "F%d = %04x %016"PRIx64"  (%12g)\n",
                     i, env->dregs[i], i, env->aregs[i],
                     i, env->fregs[i].l.upper, env->fregs[i].l.lower,
                     floatx80_to_double(env, env->fregs[i].l.upper,
                                        env->fregs[i].l.lower));
    }
    qemu_fprintf(f, "PC = %08x   ", env->pc);
    sr = env->sr | cpu_m68k_get_ccr(env);
    qemu_fprintf(f, "SR = %04x T:%x I:%x %c%c %c%c%c%c%c\n",
                 sr, (sr & SR_T) >> SR_T_SHIFT, (sr & SR_I) >> SR_I_SHIFT,
                 (sr & SR_S) ? 'S' : 'U', (sr & SR_M) ? '%' : 'I',
                 (sr & CCF_X) ? 'X' : '-', (sr & CCF_N) ? 'N' : '-',
                 (sr & CCF_Z) ? 'Z' : '-', (sr & CCF_V) ? 'V' : '-',
                 (sr & CCF_C) ? 'C' : '-');
    qemu_fprintf(f, "FPSR = %08x %c%c%c%c ", env->fpsr,
                 (env->fpsr & FPSR_CC_A) ? 'A' : '-',
                 (env->fpsr & FPSR_CC_I) ? 'I' : '-',
                 (env->fpsr & FPSR_CC_Z) ? 'Z' : '-',
                 (env->fpsr & FPSR_CC_N) ? 'N' : '-');
    qemu_fprintf(f, "\n                                "
                 "FPCR =     %04x ", env->fpcr);
    switch (env->fpcr & FPCR_PREC_MASK) {
    case FPCR_PREC_X:
        qemu_fprintf(f, "X ");
        break;
    case FPCR_PREC_S:
        qemu_fprintf(f, "S ");
        break;
    case FPCR_PREC_D:
        qemu_fprintf(f, "D ");
        break;
    }
    switch (env->fpcr & FPCR_RND_MASK) {
    case FPCR_RND_N:
        qemu_fprintf(f, "RN ");
        break;
    case FPCR_RND_Z:
        qemu_fprintf(f, "RZ ");
        break;
    case FPCR_RND_M:
        qemu_fprintf(f, "RM ");
        break;
    case FPCR_RND_P:
        qemu_fprintf(f, "RP ");
        break;
    }
    qemu_fprintf(f, "\n");
#ifdef CONFIG_SOFTMMU
    qemu_fprintf(f, "%sA7(MSP) = %08x %sA7(USP) = %08x %sA7(ISP) = %08x\n",
                 env->current_sp == M68K_SSP ? "->" : "  ", env->sp[M68K_SSP],
                 env->current_sp == M68K_USP ? "->" : "  ", env->sp[M68K_USP],
                 env->current_sp == M68K_ISP ? "->" : "  ", env->sp[M68K_ISP]);
    qemu_fprintf(f, "VBR = 0x%08x\n", env->vbr);
    qemu_fprintf(f, "SFC = %x DFC %x\n", env->sfc, env->dfc);
    qemu_fprintf(f, "SSW %08x TCR %08x URP %08x SRP %08x\n",
                 env->mmu.ssw, env->mmu.tcr, env->mmu.urp, env->mmu.srp);
    qemu_fprintf(f, "DTTR0/1: %08x/%08x ITTR0/1: %08x/%08x\n",
                 env->mmu.ttr[M68K_DTTR0], env->mmu.ttr[M68K_DTTR1],
                 env->mmu.ttr[M68K_ITTR0], env->mmu.ttr[M68K_ITTR1]);
    qemu_fprintf(f, "MMUSR %08x, fault at %08x\n",
                 env->mmu.mmusr, env->mmu.ar);
#endif
}
