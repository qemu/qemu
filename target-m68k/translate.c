/*
 *  m68k translation
 *
 *  Copyright (c) 2005-2007 CodeSourcery
 *  Written by Paul Brook
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

#include "config.h"
#include "cpu.h"
#include "exec-all.h"
#include "disas.h"
#include "tcg-op.h"
#include "qemu-log.h"

#define GEN_HELPER 1
#include "helpers.h"

//#define DEBUG_DISPATCH 1

/* Fake floating point.  */
#define TCG_TYPE_F32 TCG_TYPE_I32
#define TCG_TYPE_F64 TCG_TYPE_I64
#define tcg_gen_mov_f64 tcg_gen_mov_i64
#define tcg_gen_qemu_ldf32 tcg_gen_qemu_ld32u
#define tcg_gen_qemu_ldf64 tcg_gen_qemu_ld64
#define tcg_gen_qemu_stf32 tcg_gen_qemu_st32
#define tcg_gen_qemu_stf64 tcg_gen_qemu_st64
#define gen_helper_pack_32_f32 tcg_gen_mov_i32
#define gen_helper_pack_f32_32 tcg_gen_mov_i32

#define DEFO32(name, offset) static TCGv QREG_##name;
#define DEFO64(name, offset) static TCGv QREG_##name;
#define DEFF64(name, offset) static TCGv QREG_##name;
#include "qregs.def"
#undef DEFO32
#undef DEFO64
#undef DEFF64

static TCGv cpu_env;

static char cpu_reg_names[3*8*3 + 5*4];
static TCGv cpu_dregs[8];
static TCGv cpu_aregs[8];
static TCGv cpu_fregs[8];
static TCGv cpu_macc[4];

#define DREG(insn, pos) cpu_dregs[((insn) >> (pos)) & 7]
#define AREG(insn, pos) cpu_aregs[((insn) >> (pos)) & 7]
#define FREG(insn, pos) cpu_fregs[((insn) >> (pos)) & 7]
#define MACREG(acc) cpu_macc[acc]
#define QREG_SP cpu_aregs[7]

static TCGv NULL_QREG;
#define IS_NULL_QREG(t) (GET_TCGV(t) == GET_TCGV(NULL_QREG))
/* Used to distinguish stores from bad addressing modes.  */
static TCGv store_dummy;

#include "gen-icount.h"

void m68k_tcg_init(void)
{
    char *p;
    int i;

#define DEFO32(name,  offset) QREG_##name = tcg_global_mem_new(TCG_TYPE_I32, TCG_AREG0, offsetof(CPUState, offset), #name);
#define DEFO64(name,  offset) QREG_##name = tcg_global_mem_new(TCG_TYPE_I64, TCG_AREG0, offsetof(CPUState, offset), #name);
#define DEFF64(name,  offset) DEFO64(name, offset)
#include "qregs.def"
#undef DEFO32
#undef DEFO64
#undef DEFF64

    cpu_env = tcg_global_reg_new(TCG_TYPE_PTR, TCG_AREG0, "env");

    p = cpu_reg_names;
    for (i = 0; i < 8; i++) {
        sprintf(p, "D%d", i);
        cpu_dregs[i] = tcg_global_mem_new(TCG_TYPE_I32, TCG_AREG0,
                                          offsetof(CPUM68KState, dregs[i]), p);
        p += 3;
        sprintf(p, "A%d", i);
        cpu_aregs[i] = tcg_global_mem_new(TCG_TYPE_I32, TCG_AREG0,
                                          offsetof(CPUM68KState, aregs[i]), p);
        p += 3;
        sprintf(p, "F%d", i);
        cpu_fregs[i] = tcg_global_mem_new(TCG_TYPE_F64, TCG_AREG0,
                                          offsetof(CPUM68KState, fregs[i]), p);
        p += 3;
    }
    for (i = 0; i < 4; i++) {
        sprintf(p, "ACC%d", i);
        cpu_macc[i] = tcg_global_mem_new(TCG_TYPE_I64, TCG_AREG0,
                                         offsetof(CPUM68KState, macc[i]), p);
        p += 5;
    }

    NULL_QREG = tcg_global_mem_new(TCG_TYPE_I32, TCG_AREG0, -4, "NULL");
    store_dummy = tcg_global_mem_new(TCG_TYPE_I32, TCG_AREG0, -8, "NULL");

#define DEF_HELPER(name, ret, args) \
    tcg_register_helper(HELPER(name), #name);
#include "helpers.h"
}

static inline void qemu_assert(int cond, const char *msg)
{
    if (!cond) {
        fprintf (stderr, "badness: %s\n", msg);
        abort();
    }
}

/* internal defines */
typedef struct DisasContext {
    CPUM68KState *env;
    target_ulong insn_pc; /* Start of the current instruction.  */
    target_ulong pc;
    int is_jmp;
    int cc_op;
    int user;
    uint32_t fpcr;
    struct TranslationBlock *tb;
    int singlestep_enabled;
    int is_mem;
    TCGv mactmp;
} DisasContext;

#define DISAS_JUMP_NEXT 4

#if defined(CONFIG_USER_ONLY)
#define IS_USER(s) 1
#else
#define IS_USER(s) s->user
#endif

/* XXX: move that elsewhere */
/* ??? Fix exceptions.  */
static void *gen_throws_exception;
#define gen_last_qop NULL

#define OS_BYTE 0
#define OS_WORD 1
#define OS_LONG 2
#define OS_SINGLE 4
#define OS_DOUBLE 5

typedef void (*disas_proc)(DisasContext *, uint16_t);

#ifdef DEBUG_DISPATCH
#define DISAS_INSN(name) \
  static void real_disas_##name (DisasContext *s, uint16_t insn); \
  static void disas_##name (DisasContext *s, uint16_t insn) { \
    if (logfile) fprintf(logfile, "Dispatch " #name "\n"); \
    real_disas_##name(s, insn); } \
  static void real_disas_##name (DisasContext *s, uint16_t insn)
#else
#define DISAS_INSN(name) \
  static void disas_##name (DisasContext *s, uint16_t insn)
#endif

/* FIXME: Remove this.  */
#define gen_im32(val) tcg_const_i32(val)

#define QMODE_I32 TCG_TYPE_I32
#define QMODE_I64 TCG_TYPE_I64
#define QMODE_F32 TCG_TYPE_F32
#define QMODE_F64 TCG_TYPE_F64
static inline TCGv gen_new_qreg(int mode)
{
    return tcg_temp_new(mode);
}

/* Generate a load from the specified address.  Narrow values are
   sign extended to full register width.  */
static inline TCGv gen_load(DisasContext * s, int opsize, TCGv addr, int sign)
{
    TCGv tmp;
    int index = IS_USER(s);
    s->is_mem = 1;
    switch(opsize) {
    case OS_BYTE:
        tmp = gen_new_qreg(QMODE_I32);
        if (sign)
            tcg_gen_qemu_ld8s(tmp, addr, index);
        else
            tcg_gen_qemu_ld8u(tmp, addr, index);
        break;
    case OS_WORD:
        tmp = gen_new_qreg(QMODE_I32);
        if (sign)
            tcg_gen_qemu_ld16s(tmp, addr, index);
        else
            tcg_gen_qemu_ld16u(tmp, addr, index);
        break;
    case OS_LONG:
        tmp = gen_new_qreg(QMODE_I32);
        tcg_gen_qemu_ld32u(tmp, addr, index);
        break;
    case OS_SINGLE:
        tmp = gen_new_qreg(QMODE_F32);
        tcg_gen_qemu_ldf32(tmp, addr, index);
        break;
    case OS_DOUBLE:
        tmp  = gen_new_qreg(QMODE_F64);
        tcg_gen_qemu_ldf64(tmp, addr, index);
        break;
    default:
        qemu_assert(0, "bad load size");
    }
    gen_throws_exception = gen_last_qop;
    return tmp;
}

/* Generate a store.  */
static inline void gen_store(DisasContext *s, int opsize, TCGv addr, TCGv val)
{
    int index = IS_USER(s);
    s->is_mem = 1;
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
    case OS_SINGLE:
        tcg_gen_qemu_stf32(val, addr, index);
        break;
    case OS_DOUBLE:
        tcg_gen_qemu_stf64(val, addr, index);
        break;
    default:
        qemu_assert(0, "bad store size");
    }
    gen_throws_exception = gen_last_qop;
}

typedef enum {
    EA_STORE,
    EA_LOADU,
    EA_LOADS
} ea_what;

/* Generate an unsigned load if VAL is 0 a signed load if val is -1,
   otherwise generate a store.  */
static TCGv gen_ldst(DisasContext *s, int opsize, TCGv addr, TCGv val,
                     ea_what what)
{
    if (what == EA_STORE) {
        gen_store(s, opsize, addr, val);
        return store_dummy;
    } else {
        return gen_load(s, opsize, addr, what == EA_LOADS);
    }
}

/* Read a 32-bit immediate constant.  */
static inline uint32_t read_im32(DisasContext *s)
{
    uint32_t im;
    im = ((uint32_t)lduw_code(s->pc)) << 16;
    s->pc += 2;
    im |= lduw_code(s->pc);
    s->pc += 2;
    return im;
}

/* Calculate and address index.  */
static TCGv gen_addr_index(uint16_t ext, TCGv tmp)
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

/* Handle a base + index + displacement effective addresss.
   A NULL_QREG base means pc-relative.  */
static TCGv gen_lea_indexed(DisasContext *s, int opsize, TCGv base)
{
    uint32_t offset;
    uint16_t ext;
    TCGv add;
    TCGv tmp;
    uint32_t bd, od;

    offset = s->pc;
    ext = lduw_code(s->pc);
    s->pc += 2;

    if ((ext & 0x800) == 0 && !m68k_feature(s->env, M68K_FEATURE_WORD_INDEX))
        return NULL_QREG;

    if (ext & 0x100) {
        /* full extension word format */
        if (!m68k_feature(s->env, M68K_FEATURE_EXT_FULL))
            return NULL_QREG;

        if ((ext & 0x30) > 0x10) {
            /* base displacement */
            if ((ext & 0x30) == 0x20) {
                bd = (int16_t)lduw_code(s->pc);
                s->pc += 2;
            } else {
                bd = read_im32(s);
            }
        } else {
            bd = 0;
        }
        tmp = gen_new_qreg(QMODE_I32);
        if ((ext & 0x44) == 0) {
            /* pre-index */
            add = gen_addr_index(ext, tmp);
        } else {
            add = NULL_QREG;
        }
        if ((ext & 0x80) == 0) {
            /* base not suppressed */
            if (IS_NULL_QREG(base)) {
                base = gen_im32(offset + bd);
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
            add = gen_im32(bd);
        }
        if ((ext & 3) != 0) {
            /* memory indirect */
            base = gen_load(s, OS_LONG, add, 0);
            if ((ext & 0x44) == 4) {
                add = gen_addr_index(ext, tmp);
                tcg_gen_add_i32(tmp, add, base);
                add = tmp;
            } else {
                add = base;
            }
            if ((ext & 3) > 1) {
                /* outer displacement */
                if ((ext & 3) == 2) {
                    od = (int16_t)lduw_code(s->pc);
                    s->pc += 2;
                } else {
                    od = read_im32(s);
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
        tmp = gen_new_qreg(QMODE_I32);
        add = gen_addr_index(ext, tmp);
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

/* Update the CPU env CC_OP state.  */
static inline void gen_flush_cc_op(DisasContext *s)
{
    if (s->cc_op != CC_OP_DYNAMIC)
        tcg_gen_movi_i32(QREG_CC_OP, s->cc_op);
}

/* Evaluate all the CC flags.  */
static inline void gen_flush_flags(DisasContext *s)
{
    if (s->cc_op == CC_OP_FLAGS)
        return;
    gen_flush_cc_op(s);
    gen_helper_flush_flags(cpu_env, QREG_CC_OP);
    s->cc_op = CC_OP_FLAGS;
}

static void gen_logic_cc(DisasContext *s, TCGv val)
{
    tcg_gen_mov_i32(QREG_CC_DEST, val);
    s->cc_op = CC_OP_LOGIC;
}

static void gen_update_cc_add(TCGv dest, TCGv src)
{
    tcg_gen_mov_i32(QREG_CC_DEST, dest);
    tcg_gen_mov_i32(QREG_CC_SRC, src);
}

static inline int opsize_bytes(int opsize)
{
    switch (opsize) {
    case OS_BYTE: return 1;
    case OS_WORD: return 2;
    case OS_LONG: return 4;
    case OS_SINGLE: return 4;
    case OS_DOUBLE: return 8;
    default:
        qemu_assert(0, "bad operand size");
    }
}

/* Assign value to a register.  If the width is less than the register width
   only the low part of the register is set.  */
static void gen_partset_reg(int opsize, TCGv reg, TCGv val)
{
    TCGv tmp;
    switch (opsize) {
    case OS_BYTE:
        tcg_gen_andi_i32(reg, reg, 0xffffff00);
        tmp = gen_new_qreg(QMODE_I32);
        tcg_gen_ext8u_i32(tmp, val);
        tcg_gen_or_i32(reg, reg, tmp);
        break;
    case OS_WORD:
        tcg_gen_andi_i32(reg, reg, 0xffff0000);
        tmp = gen_new_qreg(QMODE_I32);
        tcg_gen_ext16u_i32(tmp, val);
        tcg_gen_or_i32(reg, reg, tmp);
        break;
    case OS_LONG:
        tcg_gen_mov_i32(reg, val);
        break;
    case OS_SINGLE:
        gen_helper_pack_32_f32(reg, val);
        break;
    default:
        qemu_assert(0, "Bad operand size");
        break;
    }
}

/* Sign or zero extend a value.  */
static inline TCGv gen_extend(TCGv val, int opsize, int sign)
{
    TCGv tmp;

    switch (opsize) {
    case OS_BYTE:
        tmp = gen_new_qreg(QMODE_I32);
        if (sign)
            tcg_gen_ext8s_i32(tmp, val);
        else
            tcg_gen_ext8u_i32(tmp, val);
        break;
    case OS_WORD:
        tmp = gen_new_qreg(QMODE_I32);
        if (sign)
            tcg_gen_ext16s_i32(tmp, val);
        else
            tcg_gen_ext16u_i32(tmp, val);
        break;
    case OS_LONG:
        tmp = val;
        break;
    case OS_SINGLE:
        tmp = gen_new_qreg(QMODE_F32);
        gen_helper_pack_f32_32(tmp, val);
        break;
    default:
        qemu_assert(0, "Bad operand size");
    }
    return tmp;
}

/* Generate code for an "effective address".  Does not adjust the base
   register for autoincrememnt addressing modes.  */
static TCGv gen_lea(DisasContext *s, uint16_t insn, int opsize)
{
    TCGv reg;
    TCGv tmp;
    uint16_t ext;
    uint32_t offset;

    switch ((insn >> 3) & 7) {
    case 0: /* Data register direct.  */
    case 1: /* Address register direct.  */
        return NULL_QREG;
    case 2: /* Indirect register */
    case 3: /* Indirect postincrement.  */
        return AREG(insn, 0);
    case 4: /* Indirect predecrememnt.  */
        reg = AREG(insn, 0);
        tmp = gen_new_qreg(QMODE_I32);
        tcg_gen_subi_i32(tmp, reg, opsize_bytes(opsize));
        return tmp;
    case 5: /* Indirect displacement.  */
        reg = AREG(insn, 0);
        tmp = gen_new_qreg(QMODE_I32);
        ext = lduw_code(s->pc);
        s->pc += 2;
        tcg_gen_addi_i32(tmp, reg, (int16_t)ext);
        return tmp;
    case 6: /* Indirect index + displacement.  */
        reg = AREG(insn, 0);
        return gen_lea_indexed(s, opsize, reg);
    case 7: /* Other */
        switch (insn & 7) {
        case 0: /* Absolute short.  */
            offset = ldsw_code(s->pc);
            s->pc += 2;
            return gen_im32(offset);
        case 1: /* Absolute long.  */
            offset = read_im32(s);
            return gen_im32(offset);
        case 2: /* pc displacement  */
            tmp = gen_new_qreg(QMODE_I32);
            offset = s->pc;
            offset += ldsw_code(s->pc);
            s->pc += 2;
            return gen_im32(offset);
        case 3: /* pc index+displacement.  */
            return gen_lea_indexed(s, opsize, NULL_QREG);
        case 4: /* Immediate.  */
        default:
            return NULL_QREG;
        }
    }
    /* Should never happen.  */
    return NULL_QREG;
}

/* Helper function for gen_ea. Reuse the computed address between the
   for read/write operands.  */
static inline TCGv gen_ea_once(DisasContext *s, uint16_t insn, int opsize,
                              TCGv val, TCGv *addrp, ea_what what)
{
    TCGv tmp;

    if (addrp && what == EA_STORE) {
        tmp = *addrp;
    } else {
        tmp = gen_lea(s, insn, opsize);
        if (IS_NULL_QREG(tmp))
            return tmp;
        if (addrp)
            *addrp = tmp;
    }
    return gen_ldst(s, opsize, tmp, val, what);
}

/* Generate code to load/store a value ito/from an EA.  If VAL > 0 this is
   a write otherwise it is a read (0 == sign extend, -1 == zero extend).
   ADDRP is non-null for readwrite operands.  */
static TCGv gen_ea(DisasContext *s, uint16_t insn, int opsize, TCGv val,
                   TCGv *addrp, ea_what what)
{
    TCGv reg;
    TCGv result;
    uint32_t offset;

    switch ((insn >> 3) & 7) {
    case 0: /* Data register direct.  */
        reg = DREG(insn, 0);
        if (what == EA_STORE) {
            gen_partset_reg(opsize, reg, val);
            return store_dummy;
        } else {
            return gen_extend(reg, opsize, what == EA_LOADS);
        }
    case 1: /* Address register direct.  */
        reg = AREG(insn, 0);
        if (what == EA_STORE) {
            tcg_gen_mov_i32(reg, val);
            return store_dummy;
        } else {
            return gen_extend(reg, opsize, what == EA_LOADS);
        }
    case 2: /* Indirect register */
        reg = AREG(insn, 0);
        return gen_ldst(s, opsize, reg, val, what);
    case 3: /* Indirect postincrement.  */
        reg = AREG(insn, 0);
        result = gen_ldst(s, opsize, reg, val, what);
        /* ??? This is not exception safe.  The instruction may still
           fault after this point.  */
        if (what == EA_STORE || !addrp)
            tcg_gen_addi_i32(reg, reg, opsize_bytes(opsize));
        return result;
    case 4: /* Indirect predecrememnt.  */
        {
            TCGv tmp;
            if (addrp && what == EA_STORE) {
                tmp = *addrp;
            } else {
                tmp = gen_lea(s, insn, opsize);
                if (IS_NULL_QREG(tmp))
                    return tmp;
                if (addrp)
                    *addrp = tmp;
            }
            result = gen_ldst(s, opsize, tmp, val, what);
            /* ??? This is not exception safe.  The instruction may still
               fault after this point.  */
            if (what == EA_STORE || !addrp) {
                reg = AREG(insn, 0);
                tcg_gen_mov_i32(reg, tmp);
            }
        }
        return result;
    case 5: /* Indirect displacement.  */
    case 6: /* Indirect index + displacement.  */
        return gen_ea_once(s, insn, opsize, val, addrp, what);
    case 7: /* Other */
        switch (insn & 7) {
        case 0: /* Absolute short.  */
        case 1: /* Absolute long.  */
        case 2: /* pc displacement  */
        case 3: /* pc index+displacement.  */
            return gen_ea_once(s, insn, opsize, val, addrp, what);
        case 4: /* Immediate.  */
            /* Sign extend values for consistency.  */
            switch (opsize) {
            case OS_BYTE:
                if (what == EA_LOADS)
                    offset = ldsb_code(s->pc + 1);
                else
                    offset = ldub_code(s->pc + 1);
                s->pc += 2;
                break;
            case OS_WORD:
                if (what == EA_LOADS)
                    offset = ldsw_code(s->pc);
                else
                    offset = lduw_code(s->pc);
                s->pc += 2;
                break;
            case OS_LONG:
                offset = read_im32(s);
                break;
            default:
                qemu_assert(0, "Bad immediate operand");
            }
            return tcg_const_i32(offset);
        default:
            return NULL_QREG;
        }
    }
    /* Should never happen.  */
    return NULL_QREG;
}

/* This generates a conditional branch, clobbering all temporaries.  */
static void gen_jmpcc(DisasContext *s, int cond, int l1)
{
    TCGv tmp;

    /* TODO: Optimize compare/branch pairs rather than always flushing
       flag state to CC_OP_FLAGS.  */
    gen_flush_flags(s);
    switch (cond) {
    case 0: /* T */
        tcg_gen_br(l1);
        break;
    case 1: /* F */
        break;
    case 2: /* HI (!C && !Z) */
        tmp = gen_new_qreg(QMODE_I32);
        tcg_gen_andi_i32(tmp, QREG_CC_DEST, CCF_C | CCF_Z);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, l1);
        break;
    case 3: /* LS (C || Z) */
        tmp = gen_new_qreg(QMODE_I32);
        tcg_gen_andi_i32(tmp, QREG_CC_DEST, CCF_C | CCF_Z);
        tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, l1);
        break;
    case 4: /* CC (!C) */
        tmp = gen_new_qreg(QMODE_I32);
        tcg_gen_andi_i32(tmp, QREG_CC_DEST, CCF_C);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, l1);
        break;
    case 5: /* CS (C) */
        tmp = gen_new_qreg(QMODE_I32);
        tcg_gen_andi_i32(tmp, QREG_CC_DEST, CCF_C);
        tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, l1);
        break;
    case 6: /* NE (!Z) */
        tmp = gen_new_qreg(QMODE_I32);
        tcg_gen_andi_i32(tmp, QREG_CC_DEST, CCF_Z);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, l1);
        break;
    case 7: /* EQ (Z) */
        tmp = gen_new_qreg(QMODE_I32);
        tcg_gen_andi_i32(tmp, QREG_CC_DEST, CCF_Z);
        tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, l1);
        break;
    case 8: /* VC (!V) */
        tmp = gen_new_qreg(QMODE_I32);
        tcg_gen_andi_i32(tmp, QREG_CC_DEST, CCF_V);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, l1);
        break;
    case 9: /* VS (V) */
        tmp = gen_new_qreg(QMODE_I32);
        tcg_gen_andi_i32(tmp, QREG_CC_DEST, CCF_V);
        tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, l1);
        break;
    case 10: /* PL (!N) */
        tmp = gen_new_qreg(QMODE_I32);
        tcg_gen_andi_i32(tmp, QREG_CC_DEST, CCF_N);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, l1);
        break;
    case 11: /* MI (N) */
        tmp = gen_new_qreg(QMODE_I32);
        tcg_gen_andi_i32(tmp, QREG_CC_DEST, CCF_N);
        tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, l1);
        break;
    case 12: /* GE (!(N ^ V)) */
        tmp = gen_new_qreg(QMODE_I32);
        assert(CCF_V == (CCF_N >> 2));
        tcg_gen_shri_i32(tmp, QREG_CC_DEST, 2);
        tcg_gen_xor_i32(tmp, tmp, QREG_CC_DEST);
        tcg_gen_andi_i32(tmp, tmp, CCF_V);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, l1);
        break;
    case 13: /* LT (N ^ V) */
        tmp = gen_new_qreg(QMODE_I32);
        assert(CCF_V == (CCF_N >> 2));
        tcg_gen_shri_i32(tmp, QREG_CC_DEST, 2);
        tcg_gen_xor_i32(tmp, tmp, QREG_CC_DEST);
        tcg_gen_andi_i32(tmp, tmp, CCF_V);
        tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, l1);
        break;
    case 14: /* GT (!(Z || (N ^ V))) */
        tmp = gen_new_qreg(QMODE_I32);
        assert(CCF_V == (CCF_N >> 2));
        tcg_gen_andi_i32(tmp, QREG_CC_DEST, CCF_N);
        tcg_gen_shri_i32(tmp, tmp, 2);
        tcg_gen_xor_i32(tmp, tmp, QREG_CC_DEST);
        tcg_gen_andi_i32(tmp, tmp, CCF_V | CCF_Z);
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, l1);
        break;
    case 15: /* LE (Z || (N ^ V)) */
        tmp = gen_new_qreg(QMODE_I32);
        assert(CCF_V == (CCF_N >> 2));
        tcg_gen_andi_i32(tmp, QREG_CC_DEST, CCF_N);
        tcg_gen_shri_i32(tmp, tmp, 2);
        tcg_gen_xor_i32(tmp, tmp, QREG_CC_DEST);
        tcg_gen_andi_i32(tmp, tmp, CCF_V | CCF_Z);
        tcg_gen_brcondi_i32(TCG_COND_NE, tmp, 0, l1);
        break;
    default:
        /* Should ever happen.  */
        abort();
    }
}

DISAS_INSN(scc)
{
    int l1;
    int cond;
    TCGv reg;

    l1 = gen_new_label();
    cond = (insn >> 8) & 0xf;
    reg = DREG(insn, 0);
    tcg_gen_andi_i32(reg, reg, 0xffffff00);
    /* This is safe because we modify the reg directly, with no other values
       live.  */
    gen_jmpcc(s, cond ^ 1, l1);
    tcg_gen_ori_i32(reg, reg, 0xff);
    gen_set_label(l1);
}

/* Force a TB lookup after an instruction that changes the CPU state.  */
static void gen_lookup_tb(DisasContext *s)
{
    gen_flush_cc_op(s);
    tcg_gen_movi_i32(QREG_PC, s->pc);
    s->is_jmp = DISAS_UPDATE;
}

/* Generate a jump to an immediate address.  */
static void gen_jmp_im(DisasContext *s, uint32_t dest)
{
    gen_flush_cc_op(s);
    tcg_gen_movi_i32(QREG_PC, dest);
    s->is_jmp = DISAS_JUMP;
}

/* Generate a jump to the address in qreg DEST.  */
static void gen_jmp(DisasContext *s, TCGv dest)
{
    gen_flush_cc_op(s);
    tcg_gen_mov_i32(QREG_PC, dest);
    s->is_jmp = DISAS_JUMP;
}

static void gen_exception(DisasContext *s, uint32_t where, int nr)
{
    gen_flush_cc_op(s);
    gen_jmp_im(s, where);
    gen_helper_raise_exception(tcg_const_i32(nr));
}

static inline void gen_addr_fault(DisasContext *s)
{
    gen_exception(s, s->insn_pc, EXCP_ADDRESS);
}

#define SRC_EA(result, opsize, op_sign, addrp) do { \
    result = gen_ea(s, insn, opsize, NULL_QREG, addrp, op_sign ? EA_LOADS : EA_LOADU); \
    if (IS_NULL_QREG(result)) { \
        gen_addr_fault(s); \
        return; \
    } \
    } while (0)

#define DEST_EA(insn, opsize, val, addrp) do { \
    TCGv ea_result = gen_ea(s, insn, opsize, val, addrp, EA_STORE); \
    if (IS_NULL_QREG(ea_result)) { \
        gen_addr_fault(s); \
        return; \
    } \
    } while (0)

/* Generate a jump to an immediate address.  */
static void gen_jmp_tb(DisasContext *s, int n, uint32_t dest)
{
    TranslationBlock *tb;

    tb = s->tb;
    if (unlikely(s->singlestep_enabled)) {
        gen_exception(s, dest, EXCP_DEBUG);
    } else if ((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK) ||
               (s->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_i32(QREG_PC, dest);
        tcg_gen_exit_tb((long)tb + n);
    } else {
        gen_jmp_im(s, dest);
        tcg_gen_exit_tb(0);
    }
    s->is_jmp = DISAS_TB_JUMP;
}

DISAS_INSN(undef_mac)
{
    gen_exception(s, s->pc - 2, EXCP_LINEA);
}

DISAS_INSN(undef_fpu)
{
    gen_exception(s, s->pc - 2, EXCP_LINEF);
}

DISAS_INSN(undef)
{
    gen_exception(s, s->pc - 2, EXCP_UNSUPPORTED);
    cpu_abort(cpu_single_env, "Illegal instruction: %04x @ %08x",
              insn, s->pc - 2);
}

DISAS_INSN(mulw)
{
    TCGv reg;
    TCGv tmp;
    TCGv src;
    int sign;

    sign = (insn & 0x100) != 0;
    reg = DREG(insn, 9);
    tmp = gen_new_qreg(QMODE_I32);
    if (sign)
        tcg_gen_ext16s_i32(tmp, reg);
    else
        tcg_gen_ext16u_i32(tmp, reg);
    SRC_EA(src, OS_WORD, sign, NULL);
    tcg_gen_mul_i32(tmp, tmp, src);
    tcg_gen_mov_i32(reg, tmp);
    /* Unlike m68k, coldfire always clears the overflow bit.  */
    gen_logic_cc(s, tmp);
}

DISAS_INSN(divw)
{
    TCGv reg;
    TCGv tmp;
    TCGv src;
    int sign;

    sign = (insn & 0x100) != 0;
    reg = DREG(insn, 9);
    if (sign) {
        tcg_gen_ext16s_i32(QREG_DIV1, reg);
    } else {
        tcg_gen_ext16u_i32(QREG_DIV1, reg);
    }
    SRC_EA(src, OS_WORD, sign, NULL);
    tcg_gen_mov_i32(QREG_DIV2, src);
    if (sign) {
        gen_helper_divs(cpu_env, tcg_const_i32(1));
    } else {
        gen_helper_divu(cpu_env, tcg_const_i32(1));
    }

    tmp = gen_new_qreg(QMODE_I32);
    src = gen_new_qreg(QMODE_I32);
    tcg_gen_ext16u_i32(tmp, QREG_DIV1);
    tcg_gen_shli_i32(src, QREG_DIV2, 16);
    tcg_gen_or_i32(reg, tmp, src);
    s->cc_op = CC_OP_FLAGS;
}

DISAS_INSN(divl)
{
    TCGv num;
    TCGv den;
    TCGv reg;
    uint16_t ext;

    ext = lduw_code(s->pc);
    s->pc += 2;
    if (ext & 0x87f8) {
        gen_exception(s, s->pc - 4, EXCP_UNSUPPORTED);
        return;
    }
    num = DREG(ext, 12);
    reg = DREG(ext, 0);
    tcg_gen_mov_i32(QREG_DIV1, num);
    SRC_EA(den, OS_LONG, 0, NULL);
    tcg_gen_mov_i32(QREG_DIV2, den);
    if (ext & 0x0800) {
        gen_helper_divs(cpu_env, tcg_const_i32(0));
    } else {
        gen_helper_divu(cpu_env, tcg_const_i32(0));
    }
    if ((ext & 7) == ((ext >> 12) & 7)) {
        /* div */
        tcg_gen_mov_i32 (reg, QREG_DIV1);
    } else {
        /* rem */
        tcg_gen_mov_i32 (reg, QREG_DIV2);
    }
    s->cc_op = CC_OP_FLAGS;
}

DISAS_INSN(addsub)
{
    TCGv reg;
    TCGv dest;
    TCGv src;
    TCGv tmp;
    TCGv addr;
    int add;

    add = (insn & 0x4000) != 0;
    reg = DREG(insn, 9);
    dest = gen_new_qreg(QMODE_I32);
    if (insn & 0x100) {
        SRC_EA(tmp, OS_LONG, 0, &addr);
        src = reg;
    } else {
        tmp = reg;
        SRC_EA(src, OS_LONG, 0, NULL);
    }
    if (add) {
        tcg_gen_add_i32(dest, tmp, src);
        gen_helper_xflag_lt(QREG_CC_X, dest, src);
        s->cc_op = CC_OP_ADD;
    } else {
        gen_helper_xflag_lt(QREG_CC_X, tmp, src);
        tcg_gen_sub_i32(dest, tmp, src);
        s->cc_op = CC_OP_SUB;
    }
    gen_update_cc_add(dest, src);
    if (insn & 0x100) {
        DEST_EA(insn, OS_LONG, dest, &addr);
    } else {
        tcg_gen_mov_i32(reg, dest);
    }
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
    SRC_EA(src1, opsize, 0, op ? &addr: NULL);
    src2 = DREG(insn, 9);
    dest = gen_new_qreg(QMODE_I32);

    gen_flush_flags(s);
    tmp = gen_new_qreg(QMODE_I32);
    if (opsize == OS_BYTE)
        tcg_gen_andi_i32(tmp, src2, 7);
    else
        tcg_gen_andi_i32(tmp, src2, 31);
    src2 = tmp;
    tmp = gen_new_qreg(QMODE_I32);
    tcg_gen_shr_i32(tmp, src1, src2);
    tcg_gen_andi_i32(tmp, tmp, 1);
    tcg_gen_shli_i32(tmp, tmp, 2);
    /* Clear CCF_Z if bit set.  */
    tcg_gen_ori_i32(QREG_CC_DEST, QREG_CC_DEST, CCF_Z);
    tcg_gen_xor_i32(QREG_CC_DEST, QREG_CC_DEST, tmp);

    tcg_gen_shl_i32(tmp, tcg_const_i32(1), src2);
    switch (op) {
    case 1: /* bchg */
        tcg_gen_xor_i32(dest, src1, tmp);
        break;
    case 2: /* bclr */
        tcg_gen_not_i32(tmp, tmp);
        tcg_gen_and_i32(dest, src1, tmp);
        break;
    case 3: /* bset */
        tcg_gen_or_i32(dest, src1, tmp);
        break;
    default: /* btst */
        break;
    }
    if (op)
        DEST_EA(insn, opsize, dest, &addr);
}

DISAS_INSN(sats)
{
    TCGv reg;
    reg = DREG(insn, 0);
    gen_flush_flags(s);
    gen_helper_sats(reg, reg, QREG_CC_DEST);
    gen_logic_cc(s, reg);
}

static void gen_push(DisasContext *s, TCGv val)
{
    TCGv tmp;

    tmp = gen_new_qreg(QMODE_I32);
    tcg_gen_subi_i32(tmp, QREG_SP, 4);
    gen_store(s, OS_LONG, tmp, val);
    tcg_gen_mov_i32(QREG_SP, tmp);
}

DISAS_INSN(movem)
{
    TCGv addr;
    int i;
    uint16_t mask;
    TCGv reg;
    TCGv tmp;
    int is_load;

    mask = lduw_code(s->pc);
    s->pc += 2;
    tmp = gen_lea(s, insn, OS_LONG);
    if (IS_NULL_QREG(tmp)) {
        gen_addr_fault(s);
        return;
    }
    addr = gen_new_qreg(QMODE_I32);
    tcg_gen_mov_i32(addr, tmp);
    is_load = ((insn & 0x0400) != 0);
    for (i = 0; i < 16; i++, mask >>= 1) {
        if (mask & 1) {
            if (i < 8)
                reg = DREG(i, 0);
            else
                reg = AREG(i, 0);
            if (is_load) {
                tmp = gen_load(s, OS_LONG, addr, 0);
                tcg_gen_mov_i32(reg, tmp);
            } else {
                gen_store(s, OS_LONG, addr, reg);
            }
            if (mask != 1)
                tcg_gen_addi_i32(addr, addr, 4);
        }
    }
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

    bitnum = lduw_code(s->pc);
    s->pc += 2;
    if (bitnum & 0xff00) {
        disas_undef(s, insn);
        return;
    }

    SRC_EA(src1, opsize, 0, op ? &addr: NULL);

    gen_flush_flags(s);
    if (opsize == OS_BYTE)
        bitnum &= 7;
    else
        bitnum &= 31;
    mask = 1 << bitnum;

    tmp = gen_new_qreg(QMODE_I32);
    assert (CCF_Z == (1 << 2));
    if (bitnum > 2)
        tcg_gen_shri_i32(tmp, src1, bitnum - 2);
    else if (bitnum < 2)
        tcg_gen_shli_i32(tmp, src1, 2 - bitnum);
    else
        tcg_gen_mov_i32(tmp, src1);
    tcg_gen_andi_i32(tmp, tmp, CCF_Z);
    /* Clear CCF_Z if bit set.  */
    tcg_gen_ori_i32(QREG_CC_DEST, QREG_CC_DEST, CCF_Z);
    tcg_gen_xor_i32(QREG_CC_DEST, QREG_CC_DEST, tmp);
    if (op) {
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
        DEST_EA(insn, opsize, tmp, &addr);
    }
}

DISAS_INSN(arith_im)
{
    int op;
    uint32_t im;
    TCGv src1;
    TCGv dest;
    TCGv addr;

    op = (insn >> 9) & 7;
    SRC_EA(src1, OS_LONG, 0, (op == 6) ? NULL : &addr);
    im = read_im32(s);
    dest = gen_new_qreg(QMODE_I32);
    switch (op) {
    case 0: /* ori */
        tcg_gen_ori_i32(dest, src1, im);
        gen_logic_cc(s, dest);
        break;
    case 1: /* andi */
        tcg_gen_andi_i32(dest, src1, im);
        gen_logic_cc(s, dest);
        break;
    case 2: /* subi */
        tcg_gen_mov_i32(dest, src1);
        gen_helper_xflag_lt(QREG_CC_X, dest, gen_im32(im));
        tcg_gen_subi_i32(dest, dest, im);
        gen_update_cc_add(dest, gen_im32(im));
        s->cc_op = CC_OP_SUB;
        break;
    case 3: /* addi */
        tcg_gen_mov_i32(dest, src1);
        tcg_gen_addi_i32(dest, dest, im);
        gen_update_cc_add(dest, gen_im32(im));
        gen_helper_xflag_lt(QREG_CC_X, dest, gen_im32(im));
        s->cc_op = CC_OP_ADD;
        break;
    case 5: /* eori */
        tcg_gen_xori_i32(dest, src1, im);
        gen_logic_cc(s, dest);
        break;
    case 6: /* cmpi */
        tcg_gen_mov_i32(dest, src1);
        tcg_gen_subi_i32(dest, dest, im);
        gen_update_cc_add(dest, gen_im32(im));
        s->cc_op = CC_OP_SUB;
        break;
    default:
        abort();
    }
    if (op != 6) {
        DEST_EA(insn, OS_LONG, dest, &addr);
    }
}

DISAS_INSN(byterev)
{
    TCGv reg;

    reg = DREG(insn, 0);
    tcg_gen_bswap_i32(reg, reg);
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
    SRC_EA(src, opsize, 1, NULL);
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
        DEST_EA(dest_ea, opsize, src, NULL);
        /* This will be correct because loads sign extend.  */
        gen_logic_cc(s, src);
    }
}

DISAS_INSN(negx)
{
    TCGv reg;

    gen_flush_flags(s);
    reg = DREG(insn, 0);
    gen_helper_subx_cc(reg, cpu_env, tcg_const_i32(0), reg);
}

DISAS_INSN(lea)
{
    TCGv reg;
    TCGv tmp;

    reg = AREG(insn, 9);
    tmp = gen_lea(s, insn, OS_LONG);
    if (IS_NULL_QREG(tmp)) {
        gen_addr_fault(s);
        return;
    }
    tcg_gen_mov_i32(reg, tmp);
}

DISAS_INSN(clr)
{
    int opsize;

    switch ((insn >> 6) & 3) {
    case 0: /* clr.b */
        opsize = OS_BYTE;
        break;
    case 1: /* clr.w */
        opsize = OS_WORD;
        break;
    case 2: /* clr.l */
        opsize = OS_LONG;
        break;
    default:
        abort();
    }
    DEST_EA(insn, opsize, gen_im32(0), NULL);
    gen_logic_cc(s, gen_im32(0));
}

static TCGv gen_get_ccr(DisasContext *s)
{
    TCGv dest;

    gen_flush_flags(s);
    dest = gen_new_qreg(QMODE_I32);
    tcg_gen_shli_i32(dest, QREG_CC_X, 4);
    tcg_gen_or_i32(dest, dest, QREG_CC_DEST);
    return dest;
}

DISAS_INSN(move_from_ccr)
{
    TCGv reg;
    TCGv ccr;

    ccr = gen_get_ccr(s);
    reg = DREG(insn, 0);
    gen_partset_reg(OS_WORD, reg, ccr);
}

DISAS_INSN(neg)
{
    TCGv reg;
    TCGv src1;

    reg = DREG(insn, 0);
    src1 = gen_new_qreg(QMODE_I32);
    tcg_gen_mov_i32(src1, reg);
    tcg_gen_neg_i32(reg, src1);
    s->cc_op = CC_OP_SUB;
    gen_update_cc_add(reg, src1);
    gen_helper_xflag_lt(QREG_CC_X, tcg_const_i32(0), src1);
    s->cc_op = CC_OP_SUB;
}

static void gen_set_sr_im(DisasContext *s, uint16_t val, int ccr_only)
{
    tcg_gen_movi_i32(QREG_CC_DEST, val & 0xf);
    tcg_gen_movi_i32(QREG_CC_X, (val & 0x10) >> 4);
    if (!ccr_only) {
        gen_helper_set_sr(cpu_env, tcg_const_i32(val & 0xff00));
    }
}

static void gen_set_sr(DisasContext *s, uint16_t insn, int ccr_only)
{
    TCGv tmp;
    TCGv reg;

    s->cc_op = CC_OP_FLAGS;
    if ((insn & 0x38) == 0)
      {
        tmp = gen_new_qreg(QMODE_I32);
        reg = DREG(insn, 0);
        tcg_gen_andi_i32(QREG_CC_DEST, reg, 0xf);
        tcg_gen_shri_i32(tmp, reg, 4);
        tcg_gen_andi_i32(QREG_CC_X, tmp, 1);
        if (!ccr_only) {
            gen_helper_set_sr(cpu_env, reg);
        }
      }
    else if ((insn & 0x3f) == 0x3c)
      {
        uint16_t val;
        val = lduw_code(s->pc);
        s->pc += 2;
        gen_set_sr_im(s, val, ccr_only);
      }
    else
        disas_undef(s, insn);
}

DISAS_INSN(move_to_ccr)
{
    gen_set_sr(s, insn, 1);
}

DISAS_INSN(not)
{
    TCGv reg;

    reg = DREG(insn, 0);
    tcg_gen_not_i32(reg, reg);
    gen_logic_cc(s, reg);
}

DISAS_INSN(swap)
{
    TCGv src1;
    TCGv src2;
    TCGv reg;

    src1 = gen_new_qreg(QMODE_I32);
    src2 = gen_new_qreg(QMODE_I32);
    reg = DREG(insn, 0);
    tcg_gen_shli_i32(src1, reg, 16);
    tcg_gen_shri_i32(src2, reg, 16);
    tcg_gen_or_i32(reg, src1, src2);
    gen_logic_cc(s, reg);
}

DISAS_INSN(pea)
{
    TCGv tmp;

    tmp = gen_lea(s, insn, OS_LONG);
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
    tmp = gen_new_qreg(QMODE_I32);
    if (op == 3)
        tcg_gen_ext16s_i32(tmp, reg);
    else
        tcg_gen_ext8s_i32(tmp, reg);
    if (op == 2)
        gen_partset_reg(OS_WORD, reg, tmp);
    else
        tcg_gen_mov_i32(reg, tmp);
    gen_logic_cc(s, tmp);
}

DISAS_INSN(tst)
{
    int opsize;
    TCGv tmp;

    switch ((insn >> 6) & 3) {
    case 0: /* tst.b */
        opsize = OS_BYTE;
        break;
    case 1: /* tst.w */
        opsize = OS_WORD;
        break;
    case 2: /* tst.l */
        opsize = OS_LONG;
        break;
    default:
        abort();
    }
    SRC_EA(tmp, opsize, 1, NULL);
    gen_logic_cc(s, tmp);
}

DISAS_INSN(pulse)
{
  /* Implemented as a NOP.  */
}

DISAS_INSN(illegal)
{
    gen_exception(s, s->pc - 2, EXCP_ILLEGAL);
}

/* ??? This should be atomic.  */
DISAS_INSN(tas)
{
    TCGv dest;
    TCGv src1;
    TCGv addr;

    dest = gen_new_qreg(QMODE_I32);
    SRC_EA(src1, OS_BYTE, 1, &addr);
    gen_logic_cc(s, src1);
    tcg_gen_ori_i32(dest, src1, 0x80);
    DEST_EA(insn, OS_BYTE, dest, &addr);
}

DISAS_INSN(mull)
{
    uint16_t ext;
    TCGv reg;
    TCGv src1;
    TCGv dest;

    /* The upper 32 bits of the product are discarded, so
       muls.l and mulu.l are functionally equivalent.  */
    ext = lduw_code(s->pc);
    s->pc += 2;
    if (ext & 0x87ff) {
        gen_exception(s, s->pc - 4, EXCP_UNSUPPORTED);
        return;
    }
    reg = DREG(ext, 12);
    SRC_EA(src1, OS_LONG, 0, NULL);
    dest = gen_new_qreg(QMODE_I32);
    tcg_gen_mul_i32(dest, src1, reg);
    tcg_gen_mov_i32(reg, dest);
    /* Unlike m68k, coldfire always clears the overflow bit.  */
    gen_logic_cc(s, dest);
}

DISAS_INSN(link)
{
    int16_t offset;
    TCGv reg;
    TCGv tmp;

    offset = ldsw_code(s->pc);
    s->pc += 2;
    reg = AREG(insn, 0);
    tmp = gen_new_qreg(QMODE_I32);
    tcg_gen_subi_i32(tmp, QREG_SP, 4);
    gen_store(s, OS_LONG, tmp, reg);
    if ((insn & 7) != 7)
        tcg_gen_mov_i32(reg, tmp);
    tcg_gen_addi_i32(QREG_SP, tmp, offset);
}

DISAS_INSN(unlk)
{
    TCGv src;
    TCGv reg;
    TCGv tmp;

    src = gen_new_qreg(QMODE_I32);
    reg = AREG(insn, 0);
    tcg_gen_mov_i32(src, reg);
    tmp = gen_load(s, OS_LONG, src, 0);
    tcg_gen_mov_i32(reg, tmp);
    tcg_gen_addi_i32(QREG_SP, src, 4);
}

DISAS_INSN(nop)
{
}

DISAS_INSN(rts)
{
    TCGv tmp;

    tmp = gen_load(s, OS_LONG, QREG_SP, 0);
    tcg_gen_addi_i32(QREG_SP, QREG_SP, 4);
    gen_jmp(s, tmp);
}

DISAS_INSN(jump)
{
    TCGv tmp;

    /* Load the target address first to ensure correct exception
       behavior.  */
    tmp = gen_lea(s, insn, OS_LONG);
    if (IS_NULL_QREG(tmp)) {
        gen_addr_fault(s);
        return;
    }
    if ((insn & 0x40) == 0) {
        /* jsr */
        gen_push(s, gen_im32(s->pc));
    }
    gen_jmp(s, tmp);
}

DISAS_INSN(addsubq)
{
    TCGv src1;
    TCGv src2;
    TCGv dest;
    int val;
    TCGv addr;

    SRC_EA(src1, OS_LONG, 0, &addr);
    val = (insn >> 9) & 7;
    if (val == 0)
        val = 8;
    dest = gen_new_qreg(QMODE_I32);
    tcg_gen_mov_i32(dest, src1);
    if ((insn & 0x38) == 0x08) {
        /* Don't update condition codes if the destination is an
           address register.  */
        if (insn & 0x0100) {
            tcg_gen_subi_i32(dest, dest, val);
        } else {
            tcg_gen_addi_i32(dest, dest, val);
        }
    } else {
        src2 = gen_im32(val);
        if (insn & 0x0100) {
            gen_helper_xflag_lt(QREG_CC_X, dest, src2);
            tcg_gen_subi_i32(dest, dest, val);
            s->cc_op = CC_OP_SUB;
        } else {
            tcg_gen_addi_i32(dest, dest, val);
            gen_helper_xflag_lt(QREG_CC_X, dest, src2);
            s->cc_op = CC_OP_ADD;
        }
        gen_update_cc_add(dest, src2);
    }
    DEST_EA(insn, OS_LONG, dest, &addr);
}

DISAS_INSN(tpf)
{
    switch (insn & 7) {
    case 2: /* One extension word.  */
        s->pc += 2;
        break;
    case 3: /* Two extension words.  */
        s->pc += 4;
        break;
    case 4: /* No extension words.  */
        break;
    default:
        disas_undef(s, insn);
    }
}

DISAS_INSN(branch)
{
    int32_t offset;
    uint32_t base;
    int op;
    int l1;

    base = s->pc;
    op = (insn >> 8) & 0xf;
    offset = (int8_t)insn;
    if (offset == 0) {
        offset = ldsw_code(s->pc);
        s->pc += 2;
    } else if (offset == -1) {
        offset = read_im32(s);
    }
    if (op == 1) {
        /* bsr */
        gen_push(s, gen_im32(s->pc));
    }
    gen_flush_cc_op(s);
    if (op > 1) {
        /* Bcc */
        l1 = gen_new_label();
        gen_jmpcc(s, ((insn >> 8) & 0xf) ^ 1, l1);
        gen_jmp_tb(s, 1, base + offset);
        gen_set_label(l1);
        gen_jmp_tb(s, 0, s->pc);
    } else {
        /* Unconditional branch.  */
        gen_jmp_tb(s, 0, base + offset);
    }
}

DISAS_INSN(moveq)
{
    uint32_t val;

    val = (int8_t)insn;
    tcg_gen_movi_i32(DREG(insn, 9), val);
    gen_logic_cc(s, tcg_const_i32(val));
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
    SRC_EA(src, opsize, (insn & 0x80) != 0, NULL);
    reg = DREG(insn, 9);
    tcg_gen_mov_i32(reg, src);
    gen_logic_cc(s, src);
}

DISAS_INSN(or)
{
    TCGv reg;
    TCGv dest;
    TCGv src;
    TCGv addr;

    reg = DREG(insn, 9);
    dest = gen_new_qreg(QMODE_I32);
    if (insn & 0x100) {
        SRC_EA(src, OS_LONG, 0, &addr);
        tcg_gen_or_i32(dest, src, reg);
        DEST_EA(insn, OS_LONG, dest, &addr);
    } else {
        SRC_EA(src, OS_LONG, 0, NULL);
        tcg_gen_or_i32(dest, src, reg);
        tcg_gen_mov_i32(reg, dest);
    }
    gen_logic_cc(s, dest);
}

DISAS_INSN(suba)
{
    TCGv src;
    TCGv reg;

    SRC_EA(src, OS_LONG, 0, NULL);
    reg = AREG(insn, 9);
    tcg_gen_sub_i32(reg, reg, src);
}

DISAS_INSN(subx)
{
    TCGv reg;
    TCGv src;

    gen_flush_flags(s);
    reg = DREG(insn, 9);
    src = DREG(insn, 0);
    gen_helper_subx_cc(reg, cpu_env, reg, src);
}

DISAS_INSN(mov3q)
{
    TCGv src;
    int val;

    val = (insn >> 9) & 7;
    if (val == 0)
        val = -1;
    src = gen_im32(val);
    gen_logic_cc(s, src);
    DEST_EA(insn, OS_LONG, src, NULL);
}

DISAS_INSN(cmp)
{
    int op;
    TCGv src;
    TCGv reg;
    TCGv dest;
    int opsize;

    op = (insn >> 6) & 3;
    switch (op) {
    case 0: /* cmp.b */
        opsize = OS_BYTE;
        s->cc_op = CC_OP_CMPB;
        break;
    case 1: /* cmp.w */
        opsize = OS_WORD;
        s->cc_op = CC_OP_CMPW;
        break;
    case 2: /* cmp.l */
        opsize = OS_LONG;
        s->cc_op = CC_OP_SUB;
        break;
    default:
        abort();
    }
    SRC_EA(src, opsize, 1, NULL);
    reg = DREG(insn, 9);
    dest = gen_new_qreg(QMODE_I32);
    tcg_gen_sub_i32(dest, reg, src);
    gen_update_cc_add(dest, src);
}

DISAS_INSN(cmpa)
{
    int opsize;
    TCGv src;
    TCGv reg;
    TCGv dest;

    if (insn & 0x100) {
        opsize = OS_LONG;
    } else {
        opsize = OS_WORD;
    }
    SRC_EA(src, opsize, 1, NULL);
    reg = AREG(insn, 9);
    dest = gen_new_qreg(QMODE_I32);
    tcg_gen_sub_i32(dest, reg, src);
    gen_update_cc_add(dest, src);
    s->cc_op = CC_OP_SUB;
}

DISAS_INSN(eor)
{
    TCGv src;
    TCGv reg;
    TCGv dest;
    TCGv addr;

    SRC_EA(src, OS_LONG, 0, &addr);
    reg = DREG(insn, 9);
    dest = gen_new_qreg(QMODE_I32);
    tcg_gen_xor_i32(dest, src, reg);
    gen_logic_cc(s, dest);
    DEST_EA(insn, OS_LONG, dest, &addr);
}

DISAS_INSN(and)
{
    TCGv src;
    TCGv reg;
    TCGv dest;
    TCGv addr;

    reg = DREG(insn, 9);
    dest = gen_new_qreg(QMODE_I32);
    if (insn & 0x100) {
        SRC_EA(src, OS_LONG, 0, &addr);
        tcg_gen_and_i32(dest, src, reg);
        DEST_EA(insn, OS_LONG, dest, &addr);
    } else {
        SRC_EA(src, OS_LONG, 0, NULL);
        tcg_gen_and_i32(dest, src, reg);
        tcg_gen_mov_i32(reg, dest);
    }
    gen_logic_cc(s, dest);
}

DISAS_INSN(adda)
{
    TCGv src;
    TCGv reg;

    SRC_EA(src, OS_LONG, 0, NULL);
    reg = AREG(insn, 9);
    tcg_gen_add_i32(reg, reg, src);
}

DISAS_INSN(addx)
{
    TCGv reg;
    TCGv src;

    gen_flush_flags(s);
    reg = DREG(insn, 9);
    src = DREG(insn, 0);
    gen_helper_addx_cc(reg, cpu_env, reg, src);
    s->cc_op = CC_OP_FLAGS;
}

/* TODO: This could be implemented without helper functions.  */
DISAS_INSN(shift_im)
{
    TCGv reg;
    int tmp;
    TCGv shift;

    reg = DREG(insn, 0);
    tmp = (insn >> 9) & 7;
    if (tmp == 0)
        tmp = 8;
    shift = gen_im32(tmp);
    /* No need to flush flags becuse we know we will set C flag.  */
    if (insn & 0x100) {
        gen_helper_shl_cc(reg, cpu_env, reg, shift);
    } else {
        if (insn & 8) {
            gen_helper_shr_cc(reg, cpu_env, reg, shift);
        } else {
            gen_helper_sar_cc(reg, cpu_env, reg, shift);
        }
    }
    s->cc_op = CC_OP_SHIFT;
}

DISAS_INSN(shift_reg)
{
    TCGv reg;
    TCGv shift;

    reg = DREG(insn, 0);
    shift = DREG(insn, 9);
    /* Shift by zero leaves C flag unmodified.   */
    gen_flush_flags(s);
    if (insn & 0x100) {
        gen_helper_shl_cc(reg, cpu_env, reg, shift);
    } else {
        if (insn & 8) {
            gen_helper_shr_cc(reg, cpu_env, reg, shift);
        } else {
            gen_helper_sar_cc(reg, cpu_env, reg, shift);
        }
    }
    s->cc_op = CC_OP_SHIFT;
}

DISAS_INSN(ff1)
{
    TCGv reg;
    reg = DREG(insn, 0);
    gen_logic_cc(s, reg);
    gen_helper_ff1(reg, reg);
}

static TCGv gen_get_sr(DisasContext *s)
{
    TCGv ccr;
    TCGv sr;

    ccr = gen_get_ccr(s);
    sr = gen_new_qreg(QMODE_I32);
    tcg_gen_andi_i32(sr, QREG_SR, 0xffe0);
    tcg_gen_or_i32(sr, sr, ccr);
    return sr;
}

DISAS_INSN(strldsr)
{
    uint16_t ext;
    uint32_t addr;

    addr = s->pc - 2;
    ext = lduw_code(s->pc);
    s->pc += 2;
    if (ext != 0x46FC) {
        gen_exception(s, addr, EXCP_UNSUPPORTED);
        return;
    }
    ext = lduw_code(s->pc);
    s->pc += 2;
    if (IS_USER(s) || (ext & SR_S) == 0) {
        gen_exception(s, addr, EXCP_PRIVILEGE);
        return;
    }
    gen_push(s, gen_get_sr(s));
    gen_set_sr_im(s, ext, 0);
}

DISAS_INSN(move_from_sr)
{
    TCGv reg;
    TCGv sr;

    if (IS_USER(s)) {
        gen_exception(s, s->pc - 2, EXCP_PRIVILEGE);
        return;
    }
    sr = gen_get_sr(s);
    reg = DREG(insn, 0);
    gen_partset_reg(OS_WORD, reg, sr);
}

DISAS_INSN(move_to_sr)
{
    if (IS_USER(s)) {
        gen_exception(s, s->pc - 2, EXCP_PRIVILEGE);
        return;
    }
    gen_set_sr(s, insn, 0);
    gen_lookup_tb(s);
}

DISAS_INSN(move_from_usp)
{
    if (IS_USER(s)) {
        gen_exception(s, s->pc - 2, EXCP_PRIVILEGE);
        return;
    }
    /* TODO: Implement USP.  */
    gen_exception(s, s->pc - 2, EXCP_ILLEGAL);
}

DISAS_INSN(move_to_usp)
{
    if (IS_USER(s)) {
        gen_exception(s, s->pc - 2, EXCP_PRIVILEGE);
        return;
    }
    /* TODO: Implement USP.  */
    gen_exception(s, s->pc - 2, EXCP_ILLEGAL);
}

DISAS_INSN(halt)
{
    gen_exception(s, s->pc, EXCP_HALT_INSN);
}

DISAS_INSN(stop)
{
    uint16_t ext;

    if (IS_USER(s)) {
        gen_exception(s, s->pc - 2, EXCP_PRIVILEGE);
        return;
    }

    ext = lduw_code(s->pc);
    s->pc += 2;

    gen_set_sr_im(s, ext, 0);
    tcg_gen_movi_i32(QREG_HALTED, 1);
    gen_exception(s, s->pc, EXCP_HLT);
}

DISAS_INSN(rte)
{
    if (IS_USER(s)) {
        gen_exception(s, s->pc - 2, EXCP_PRIVILEGE);
        return;
    }
    gen_exception(s, s->pc - 2, EXCP_RTE);
}

DISAS_INSN(movec)
{
    uint16_t ext;
    TCGv reg;

    if (IS_USER(s)) {
        gen_exception(s, s->pc - 2, EXCP_PRIVILEGE);
        return;
    }

    ext = lduw_code(s->pc);
    s->pc += 2;

    if (ext & 0x8000) {
        reg = AREG(ext, 12);
    } else {
        reg = DREG(ext, 12);
    }
    gen_helper_movec(cpu_env, tcg_const_i32(ext & 0xfff), reg);
    gen_lookup_tb(s);
}

DISAS_INSN(intouch)
{
    if (IS_USER(s)) {
        gen_exception(s, s->pc - 2, EXCP_PRIVILEGE);
        return;
    }
    /* ICache fetch.  Implement as no-op.  */
}

DISAS_INSN(cpushl)
{
    if (IS_USER(s)) {
        gen_exception(s, s->pc - 2, EXCP_PRIVILEGE);
        return;
    }
    /* Cache push/invalidate.  Implement as no-op.  */
}

DISAS_INSN(wddata)
{
    gen_exception(s, s->pc - 2, EXCP_PRIVILEGE);
}

DISAS_INSN(wdebug)
{
    if (IS_USER(s)) {
        gen_exception(s, s->pc - 2, EXCP_PRIVILEGE);
        return;
    }
    /* TODO: Implement wdebug.  */
    qemu_assert(0, "WDEBUG not implemented");
}

DISAS_INSN(trap)
{
    gen_exception(s, s->pc - 2, EXCP_TRAP0 + (insn & 0xf));
}

/* ??? FP exceptions are not implemented.  Most exceptions are deferred until
   immediately before the next FP instruction is executed.  */
DISAS_INSN(fpu)
{
    uint16_t ext;
    int opmode;
    TCGv src;
    TCGv dest;
    TCGv res;
    int round;
    int opsize;

    ext = lduw_code(s->pc);
    s->pc += 2;
    opmode = ext & 0x7f;
    switch ((ext >> 13) & 7) {
    case 0: case 2:
        break;
    case 1:
        goto undef;
    case 3: /* fmove out */
        src = FREG(ext, 7);
        /* fmove */
        /* ??? TODO: Proper behavior on overflow.  */
        switch ((ext >> 10) & 7) {
        case 0:
            opsize = OS_LONG;
            res = gen_new_qreg(QMODE_I32);
            gen_helper_f64_to_i32(res, cpu_env, src);
            break;
        case 1:
            opsize = OS_SINGLE;
            res = gen_new_qreg(QMODE_F32);
            gen_helper_f64_to_f32(res, cpu_env, src);
            break;
        case 4:
            opsize = OS_WORD;
            res = gen_new_qreg(QMODE_I32);
            gen_helper_f64_to_i32(res, cpu_env, src);
            break;
        case 5:
            opsize = OS_DOUBLE;
            res = src;
            break;
        case 6:
            opsize = OS_BYTE;
            res = gen_new_qreg(QMODE_I32);
            gen_helper_f64_to_i32(res, cpu_env, src);
            break;
        default:
            goto undef;
        }
        DEST_EA(insn, opsize, res, NULL);
        return;
    case 4: /* fmove to control register.  */
        switch ((ext >> 10) & 7) {
        case 4: /* FPCR */
            /* Not implemented.  Ignore writes.  */
            break;
        case 1: /* FPIAR */
        case 2: /* FPSR */
        default:
            cpu_abort(NULL, "Unimplemented: fmove to control %d",
                      (ext >> 10) & 7);
        }
        break;
    case 5: /* fmove from control register.  */
        switch ((ext >> 10) & 7) {
        case 4: /* FPCR */
            /* Not implemented.  Always return zero.  */
            res = gen_im32(0);
            break;
        case 1: /* FPIAR */
        case 2: /* FPSR */
        default:
            cpu_abort(NULL, "Unimplemented: fmove from control %d",
                      (ext >> 10) & 7);
            goto undef;
        }
        DEST_EA(insn, OS_LONG, res, NULL);
        break;
    case 6: /* fmovem */
    case 7:
        {
            TCGv addr;
            uint16_t mask;
            int i;
            if ((ext & 0x1f00) != 0x1000 || (ext & 0xff) == 0)
                goto undef;
            src = gen_lea(s, insn, OS_LONG);
            if (IS_NULL_QREG(src)) {
                gen_addr_fault(s);
                return;
            }
            addr = gen_new_qreg(QMODE_I32);
            tcg_gen_mov_i32(addr, src);
            mask = 0x80;
            for (i = 0; i < 8; i++) {
                if (ext & mask) {
                    s->is_mem = 1;
                    dest = FREG(i, 0);
                    if (ext & (1 << 13)) {
                        /* store */
                        tcg_gen_qemu_stf64(dest, addr, IS_USER(s));
                    } else {
                        /* load */
                        tcg_gen_qemu_ldf64(dest, addr, IS_USER(s));
                    }
                    if (ext & (mask - 1))
                        tcg_gen_addi_i32(addr, addr, 8);
                }
                mask >>= 1;
            }
        }
        return;
    }
    if (ext & (1 << 14)) {
        TCGv tmp;

        /* Source effective address.  */
        switch ((ext >> 10) & 7) {
        case 0: opsize = OS_LONG; break;
        case 1: opsize = OS_SINGLE; break;
        case 4: opsize = OS_WORD; break;
        case 5: opsize = OS_DOUBLE; break;
        case 6: opsize = OS_BYTE; break;
        default:
            goto undef;
        }
        SRC_EA(tmp, opsize, 1, NULL);
        if (opsize == OS_DOUBLE) {
            src = tmp;
        } else {
            src = gen_new_qreg(QMODE_F64);
            switch (opsize) {
            case OS_LONG:
            case OS_WORD:
            case OS_BYTE:
                gen_helper_i32_to_f64(src, cpu_env, tmp);
                break;
            case OS_SINGLE:
                gen_helper_f32_to_f64(src, cpu_env, tmp);
                break;
            }
        }
    } else {
        /* Source register.  */
        src = FREG(ext, 10);
    }
    dest = FREG(ext, 7);
    res = gen_new_qreg(QMODE_F64);
    if (opmode != 0x3a)
        tcg_gen_mov_f64(res, dest);
    round = 1;
    switch (opmode) {
    case 0: case 0x40: case 0x44: /* fmove */
        tcg_gen_mov_f64(res, src);
        break;
    case 1: /* fint */
        gen_helper_iround_f64(res, cpu_env, src);
        round = 0;
        break;
    case 3: /* fintrz */
        gen_helper_itrunc_f64(res, cpu_env, src);
        round = 0;
        break;
    case 4: case 0x41: case 0x45: /* fsqrt */
        gen_helper_sqrt_f64(res, cpu_env, src);
        break;
    case 0x18: case 0x58: case 0x5c: /* fabs */
        gen_helper_abs_f64(res, src);
        break;
    case 0x1a: case 0x5a: case 0x5e: /* fneg */
        gen_helper_chs_f64(res, src);
        break;
    case 0x20: case 0x60: case 0x64: /* fdiv */
        gen_helper_div_f64(res, cpu_env, res, src);
        break;
    case 0x22: case 0x62: case 0x66: /* fadd */
        gen_helper_add_f64(res, cpu_env, res, src);
        break;
    case 0x23: case 0x63: case 0x67: /* fmul */
        gen_helper_mul_f64(res, cpu_env, res, src);
        break;
    case 0x28: case 0x68: case 0x6c: /* fsub */
        gen_helper_sub_f64(res, cpu_env, res, src);
        break;
    case 0x38: /* fcmp */
        gen_helper_sub_cmp_f64(res, cpu_env, res, src);
        dest = NULL_QREG;
        round = 0;
        break;
    case 0x3a: /* ftst */
        tcg_gen_mov_f64(res, src);
        dest = NULL_QREG;
        round = 0;
        break;
    default:
        goto undef;
    }
    if (round) {
        if (opmode & 0x40) {
            if ((opmode & 0x4) != 0)
                round = 0;
        } else if ((s->fpcr & M68K_FPCR_PREC) == 0) {
            round = 0;
        }
    }
    if (round) {
        TCGv tmp;

        tmp = gen_new_qreg(QMODE_F32);
        gen_helper_f64_to_f32(tmp, cpu_env, res);
        gen_helper_f32_to_f64(res, cpu_env, tmp);
    }
    tcg_gen_mov_f64(QREG_FP_RESULT, res);
    if (!IS_NULL_QREG(dest)) {
        tcg_gen_mov_f64(dest, res);
    }
    return;
undef:
    s->pc -= 2;
    disas_undef_fpu(s, insn);
}

DISAS_INSN(fbcc)
{
    uint32_t offset;
    uint32_t addr;
    TCGv flag;
    int l1;

    addr = s->pc;
    offset = ldsw_code(s->pc);
    s->pc += 2;
    if (insn & (1 << 6)) {
        offset = (offset << 16) | lduw_code(s->pc);
        s->pc += 2;
    }

    l1 = gen_new_label();
    /* TODO: Raise BSUN exception.  */
    flag = gen_new_qreg(QMODE_I32);
    gen_helper_compare_f64(flag, cpu_env, QREG_FP_RESULT);
    /* Jump to l1 if condition is true.  */
    switch (insn & 0xf) {
    case 0: /* f */
        break;
    case 1: /* eq (=0) */
        tcg_gen_brcond_i32(TCG_COND_EQ, flag, tcg_const_i32(0), l1);
        break;
    case 2: /* ogt (=1) */
        tcg_gen_brcond_i32(TCG_COND_EQ, flag, tcg_const_i32(1), l1);
        break;
    case 3: /* oge (=0 or =1) */
        tcg_gen_brcond_i32(TCG_COND_LEU, flag, tcg_const_i32(1), l1);
        break;
    case 4: /* olt (=-1) */
        tcg_gen_brcond_i32(TCG_COND_LT, flag, tcg_const_i32(0), l1);
        break;
    case 5: /* ole (=-1 or =0) */
        tcg_gen_brcond_i32(TCG_COND_LE, flag, tcg_const_i32(0), l1);
        break;
    case 6: /* ogl (=-1 or =1) */
        tcg_gen_andi_i32(flag, flag, 1);
        tcg_gen_brcond_i32(TCG_COND_NE, flag, tcg_const_i32(0), l1);
        break;
    case 7: /* or (=2) */
        tcg_gen_brcond_i32(TCG_COND_EQ, flag, tcg_const_i32(2), l1);
        break;
    case 8: /* un (<2) */
        tcg_gen_brcond_i32(TCG_COND_LT, flag, tcg_const_i32(2), l1);
        break;
    case 9: /* ueq (=0 or =2) */
        tcg_gen_andi_i32(flag, flag, 1);
        tcg_gen_brcond_i32(TCG_COND_EQ, flag, tcg_const_i32(0), l1);
        break;
    case 10: /* ugt (>0) */
        tcg_gen_brcond_i32(TCG_COND_GT, flag, tcg_const_i32(0), l1);
        break;
    case 11: /* uge (>=0) */
        tcg_gen_brcond_i32(TCG_COND_GE, flag, tcg_const_i32(0), l1);
        break;
    case 12: /* ult (=-1 or =2) */
        tcg_gen_brcond_i32(TCG_COND_GEU, flag, tcg_const_i32(2), l1);
        break;
    case 13: /* ule (!=1) */
        tcg_gen_brcond_i32(TCG_COND_NE, flag, tcg_const_i32(1), l1);
        break;
    case 14: /* ne (!=0) */
        tcg_gen_brcond_i32(TCG_COND_NE, flag, tcg_const_i32(0), l1);
        break;
    case 15: /* t */
        tcg_gen_br(l1);
        break;
    }
    gen_jmp_tb(s, 0, s->pc);
    gen_set_label(l1);
    gen_jmp_tb(s, 1, addr + offset);
}

DISAS_INSN(frestore)
{
    /* TODO: Implement frestore.  */
    qemu_assert(0, "FRESTORE not implemented");
}

DISAS_INSN(fsave)
{
    /* TODO: Implement fsave.  */
    qemu_assert(0, "FSAVE not implemented");
}

static inline TCGv gen_mac_extract_word(DisasContext *s, TCGv val, int upper)
{
    TCGv tmp = gen_new_qreg(QMODE_I32);
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

    if (IS_NULL_QREG(s->mactmp))
        s->mactmp = tcg_temp_new(TCG_TYPE_I64);

    ext = lduw_code(s->pc);
    s->pc += 2;

    acc = ((insn >> 7) & 1) | ((ext >> 3) & 2);
    dual = ((insn & 0x30) != 0 && (ext & 3) != 0);
    if (dual && !m68k_feature(s->env, M68K_FEATURE_CF_EMAC_B)) {
        disas_undef(s, insn);
        return;
    }
    if (insn & 0x30) {
        /* MAC with load.  */
        tmp = gen_lea(s, insn, OS_LONG);
        addr = gen_new_qreg(QMODE_I32);
        tcg_gen_and_i32(addr, tmp, QREG_MAC_MASK);
        /* Load the value now to ensure correct exception behavior.
           Perform writeback after reading the MAC inputs.  */
        loadval = gen_load(s, OS_LONG, addr, 0);

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
        tmp = gen_new_qreg(QMODE_I32);
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
        saved_flags = gen_new_qreg(QMODE_I32);
        tcg_gen_mov_i32(saved_flags, QREG_MACSR);
    } else {
        saved_flags = NULL_QREG;
    }

#if 0
    /* Disabled because conditional branches clobber temporary vars.  */
    if ((s->env->macsr & MACSR_OMC) != 0 && dual) {
        /* Skip the accumulate if the value is already saturated.  */
        l1 = gen_new_label();
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_and32(tmp, QREG_MACSR, gen_im32(MACSR_PAV0 << acc));
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
            tmp = gen_new_qreg(QMODE_I32);
            gen_op_and32(tmp, QREG_MACSR, gen_im32(MACSR_PAV0 << acc));
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
        /* FIXME: Should address writeback happen with the masked or
           unmasked value?  */
        switch ((insn >> 3) & 7) {
        case 3: /* Post-increment.  */
            tcg_gen_addi_i32(AREG(insn, 0), addr, 4);
            break;
        case 4: /* Pre-decrement.  */
            tcg_gen_mov_i32(AREG(insn, 0), addr);
        }
    }
}

DISAS_INSN(from_mac)
{
    TCGv rx;
    TCGv acc;
    int accnum;

    rx = (insn & 8) ? AREG(insn, 0) : DREG(insn, 0);
    accnum = (insn >> 9) & 3;
    acc = MACREG(accnum);
    if (s->env->macsr & MACSR_FI) {
        gen_helper_get_macf(cpu_env, rx, acc);
    } else if ((s->env->macsr & MACSR_OMC) == 0) {
        tcg_gen_trunc_i64_i32(rx, acc);
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
    tcg_gen_movi_i32(QREG_CC_X, 0);
    tcg_gen_andi_i32(QREG_CC_DEST, QREG_MACSR, 0xf);
    s->cc_op = CC_OP_FLAGS;
}

DISAS_INSN(to_mac)
{
    TCGv acc;
    TCGv val;
    int accnum;
    accnum = (insn >> 9) & 3;
    acc = MACREG(accnum);
    SRC_EA(val, OS_LONG, 0, NULL);
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
    SRC_EA(val, OS_LONG, 0, NULL);
    gen_helper_set_macsr(cpu_env, val);
    gen_lookup_tb(s);
}

DISAS_INSN(to_mask)
{
    TCGv val;
    SRC_EA(val, OS_LONG, 0, NULL);
    tcg_gen_ori_i32(QREG_MAC_MASK, val, 0xffff0000);
}

DISAS_INSN(to_mext)
{
    TCGv val;
    TCGv acc;
    SRC_EA(val, OS_LONG, 0, NULL);
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
  /* This could probably be cleverer.  For now just optimize the case where
     the top bits are known.  */
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

/* Register m68k opcode handlers.  Order is important.
   Later insn override earlier ones.  */
void register_m68k_insns (CPUM68KState *env)
{
#define INSN(name, opcode, mask, feature) do { \
    if (m68k_feature(env, M68K_FEATURE_##feature)) \
        register_opcode(disas_##name, 0x##opcode, 0x##mask); \
    } while(0)
    INSN(undef,     0000, 0000, CF_ISA_A);
    INSN(arith_im,  0080, fff8, CF_ISA_A);
    INSN(bitrev,    00c0, fff8, CF_ISA_APLUSC);
    INSN(bitop_reg, 0100, f1c0, CF_ISA_A);
    INSN(bitop_reg, 0140, f1c0, CF_ISA_A);
    INSN(bitop_reg, 0180, f1c0, CF_ISA_A);
    INSN(bitop_reg, 01c0, f1c0, CF_ISA_A);
    INSN(arith_im,  0280, fff8, CF_ISA_A);
    INSN(byterev,   02c0, fff8, CF_ISA_APLUSC);
    INSN(arith_im,  0480, fff8, CF_ISA_A);
    INSN(ff1,       04c0, fff8, CF_ISA_APLUSC);
    INSN(arith_im,  0680, fff8, CF_ISA_A);
    INSN(bitop_im,  0800, ffc0, CF_ISA_A);
    INSN(bitop_im,  0840, ffc0, CF_ISA_A);
    INSN(bitop_im,  0880, ffc0, CF_ISA_A);
    INSN(bitop_im,  08c0, ffc0, CF_ISA_A);
    INSN(arith_im,  0a80, fff8, CF_ISA_A);
    INSN(arith_im,  0c00, ff38, CF_ISA_A);
    INSN(move,      1000, f000, CF_ISA_A);
    INSN(move,      2000, f000, CF_ISA_A);
    INSN(move,      3000, f000, CF_ISA_A);
    INSN(strldsr,   40e7, ffff, CF_ISA_APLUSC);
    INSN(negx,      4080, fff8, CF_ISA_A);
    INSN(move_from_sr, 40c0, fff8, CF_ISA_A);
    INSN(lea,       41c0, f1c0, CF_ISA_A);
    INSN(clr,       4200, ff00, CF_ISA_A);
    INSN(undef,     42c0, ffc0, CF_ISA_A);
    INSN(move_from_ccr, 42c0, fff8, CF_ISA_A);
    INSN(neg,       4480, fff8, CF_ISA_A);
    INSN(move_to_ccr, 44c0, ffc0, CF_ISA_A);
    INSN(not,       4680, fff8, CF_ISA_A);
    INSN(move_to_sr, 46c0, ffc0, CF_ISA_A);
    INSN(pea,       4840, ffc0, CF_ISA_A);
    INSN(swap,      4840, fff8, CF_ISA_A);
    INSN(movem,     48c0, fbc0, CF_ISA_A);
    INSN(ext,       4880, fff8, CF_ISA_A);
    INSN(ext,       48c0, fff8, CF_ISA_A);
    INSN(ext,       49c0, fff8, CF_ISA_A);
    INSN(tst,       4a00, ff00, CF_ISA_A);
    INSN(tas,       4ac0, ffc0, CF_ISA_B);
    INSN(halt,      4ac8, ffff, CF_ISA_A);
    INSN(pulse,     4acc, ffff, CF_ISA_A);
    INSN(illegal,   4afc, ffff, CF_ISA_A);
    INSN(mull,      4c00, ffc0, CF_ISA_A);
    INSN(divl,      4c40, ffc0, CF_ISA_A);
    INSN(sats,      4c80, fff8, CF_ISA_B);
    INSN(trap,      4e40, fff0, CF_ISA_A);
    INSN(link,      4e50, fff8, CF_ISA_A);
    INSN(unlk,      4e58, fff8, CF_ISA_A);
    INSN(move_to_usp, 4e60, fff8, USP);
    INSN(move_from_usp, 4e68, fff8, USP);
    INSN(nop,       4e71, ffff, CF_ISA_A);
    INSN(stop,      4e72, ffff, CF_ISA_A);
    INSN(rte,       4e73, ffff, CF_ISA_A);
    INSN(rts,       4e75, ffff, CF_ISA_A);
    INSN(movec,     4e7b, ffff, CF_ISA_A);
    INSN(jump,      4e80, ffc0, CF_ISA_A);
    INSN(jump,      4ec0, ffc0, CF_ISA_A);
    INSN(addsubq,   5180, f1c0, CF_ISA_A);
    INSN(scc,       50c0, f0f8, CF_ISA_A);
    INSN(addsubq,   5080, f1c0, CF_ISA_A);
    INSN(tpf,       51f8, fff8, CF_ISA_A);

    /* Branch instructions.  */
    INSN(branch,    6000, f000, CF_ISA_A);
    /* Disable long branch instructions, then add back the ones we want.  */
    INSN(undef,     60ff, f0ff, CF_ISA_A); /* All long branches.  */
    INSN(branch,    60ff, f0ff, CF_ISA_B);
    INSN(undef,     60ff, ffff, CF_ISA_B); /* bra.l */
    INSN(branch,    60ff, ffff, BRAL);

    INSN(moveq,     7000, f100, CF_ISA_A);
    INSN(mvzs,      7100, f100, CF_ISA_B);
    INSN(or,        8000, f000, CF_ISA_A);
    INSN(divw,      80c0, f0c0, CF_ISA_A);
    INSN(addsub,    9000, f000, CF_ISA_A);
    INSN(subx,      9180, f1f8, CF_ISA_A);
    INSN(suba,      91c0, f1c0, CF_ISA_A);

    INSN(undef_mac, a000, f000, CF_ISA_A);
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
    INSN(eor,       b180, f1c0, CF_ISA_A);
    INSN(and,       c000, f000, CF_ISA_A);
    INSN(mulw,      c0c0, f0c0, CF_ISA_A);
    INSN(addsub,    d000, f000, CF_ISA_A);
    INSN(addx,      d180, f1f8, CF_ISA_A);
    INSN(adda,      d1c0, f1c0, CF_ISA_A);
    INSN(shift_im,  e080, f0f0, CF_ISA_A);
    INSN(shift_reg, e0a0, f0f0, CF_ISA_A);
    INSN(undef_fpu, f000, f000, CF_ISA_A);
    INSN(fpu,       f200, ffc0, CF_FPU);
    INSN(fbcc,      f280, ffc0, CF_FPU);
    INSN(frestore,  f340, ffc0, CF_FPU);
    INSN(fsave,     f340, ffc0, CF_FPU);
    INSN(intouch,   f340, ffc0, CF_ISA_A);
    INSN(cpushl,    f428, ff38, CF_ISA_A);
    INSN(wddata,    fb00, ff00, CF_ISA_A);
    INSN(wdebug,    fbc0, ffc0, CF_ISA_A);
#undef INSN
}

/* ??? Some of this implementation is not exception safe.  We should always
   write back the result to memory before setting the condition codes.  */
static void disas_m68k_insn(CPUState * env, DisasContext *s)
{
    uint16_t insn;

    insn = lduw_code(s->pc);
    s->pc += 2;

    opcode_table[insn](s, insn);
}

/* generate intermediate code for basic block 'tb'.  */
static inline void
gen_intermediate_code_internal(CPUState *env, TranslationBlock *tb,
                               int search_pc)
{
    DisasContext dc1, *dc = &dc1;
    uint16_t *gen_opc_end;
    int j, lj;
    target_ulong pc_start;
    int pc_offset;
    int last_cc_op;
    int num_insns;
    int max_insns;

    /* generate intermediate code */
    pc_start = tb->pc;

    dc->tb = tb;

    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;

    dc->env = env;
    dc->is_jmp = DISAS_NEXT;
    dc->pc = pc_start;
    dc->cc_op = CC_OP_DYNAMIC;
    dc->singlestep_enabled = env->singlestep_enabled;
    dc->fpcr = env->fpcr;
    dc->user = (env->sr & SR_S) == 0;
    dc->is_mem = 0;
    dc->mactmp = NULL_QREG;
    lj = -1;
    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0)
        max_insns = CF_COUNT_MASK;

    gen_icount_start();
    do {
        pc_offset = dc->pc - pc_start;
        gen_throws_exception = NULL;
        if (env->nb_breakpoints > 0) {
            for(j = 0; j < env->nb_breakpoints; j++) {
                if (env->breakpoints[j] == dc->pc) {
                    gen_exception(dc, dc->pc, EXCP_DEBUG);
                    dc->is_jmp = DISAS_JUMP;
                    break;
                }
            }
            if (dc->is_jmp)
                break;
        }
        if (search_pc) {
            j = gen_opc_ptr - gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j)
                    gen_opc_instr_start[lj++] = 0;
            }
            gen_opc_pc[lj] = dc->pc;
            gen_opc_instr_start[lj] = 1;
            gen_opc_icount[lj] = num_insns;
        }
        if (num_insns + 1 == max_insns && (tb->cflags & CF_LAST_IO))
            gen_io_start();
        last_cc_op = dc->cc_op;
        dc->insn_pc = dc->pc;
	disas_m68k_insn(env, dc);
        num_insns++;

        /* Terminate the TB on memory ops if watchpoints are present.  */
        /* FIXME: This should be replaced by the deterministic execution
         * IRQ raising bits.  */
        if (dc->is_mem && env->nb_watchpoints)
            break;
    } while (!dc->is_jmp && gen_opc_ptr < gen_opc_end &&
             !env->singlestep_enabled &&
             (pc_offset) < (TARGET_PAGE_SIZE - 32) &&
             num_insns < max_insns);

    if (tb->cflags & CF_LAST_IO)
        gen_io_end();
    if (unlikely(env->singlestep_enabled)) {
        /* Make sure the pc is updated, and raise a debug exception.  */
        if (!dc->is_jmp) {
            gen_flush_cc_op(dc);
            tcg_gen_movi_i32(QREG_PC, dc->pc);
        }
        gen_helper_raise_exception(tcg_const_i32(EXCP_DEBUG));
    } else {
        switch(dc->is_jmp) {
        case DISAS_NEXT:
            gen_flush_cc_op(dc);
            gen_jmp_tb(dc, 0, dc->pc);
            break;
        default:
        case DISAS_JUMP:
        case DISAS_UPDATE:
            gen_flush_cc_op(dc);
            /* indicate that the hash table must be used to find the next TB */
            tcg_gen_exit_tb(0);
            break;
        case DISAS_TB_JUMP:
            /* nothing more to generate */
            break;
        }
    }
    gen_icount_end(tb, num_insns);
    *gen_opc_ptr = INDEX_op_end;

#ifdef DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "----------------\n");
        fprintf(logfile, "IN: %s\n", lookup_symbol(pc_start));
        target_disas(logfile, pc_start, dc->pc - pc_start, 0);
        fprintf(logfile, "\n");
    }
#endif
    if (search_pc) {
        j = gen_opc_ptr - gen_opc_buf;
        lj++;
        while (lj <= j)
            gen_opc_instr_start[lj++] = 0;
    } else {
        tb->size = dc->pc - pc_start;
        tb->icount = num_insns;
    }

    //optimize_flags();
    //expand_target_qops();
}

void gen_intermediate_code(CPUState *env, TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 0);
}

void gen_intermediate_code_pc(CPUState *env, TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 1);
}

void cpu_dump_state(CPUState *env, FILE *f,
                    int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                    int flags)
{
    int i;
    uint16_t sr;
    CPU_DoubleU u;
    for (i = 0; i < 8; i++)
      {
        u.d = env->fregs[i];
        cpu_fprintf (f, "D%d = %08x   A%d = %08x   F%d = %08x%08x (%12g)\n",
                     i, env->dregs[i], i, env->aregs[i],
                     i, u.l.upper, u.l.lower, *(double *)&u.d);
      }
    cpu_fprintf (f, "PC = %08x   ", env->pc);
    sr = env->sr;
    cpu_fprintf (f, "SR = %04x %c%c%c%c%c ", sr, (sr & 0x10) ? 'X' : '-',
                 (sr & CCF_N) ? 'N' : '-', (sr & CCF_Z) ? 'Z' : '-',
                 (sr & CCF_V) ? 'V' : '-', (sr & CCF_C) ? 'C' : '-');
    cpu_fprintf (f, "FPRESULT = %12g\n", *(double *)&env->fp_result);
}

void gen_pc_load(CPUState *env, TranslationBlock *tb,
                unsigned long searched_pc, int pc_pos, void *puc)
{
    env->pc = gen_opc_pc[pc_pos];
}
