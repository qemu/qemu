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

#include "config.h"
#include "cpu.h"
#include "exec-all.h"
#include "disas.h"
#include "m68k-qreg.h"

//#define DEBUG_DISPATCH 1

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

static uint16_t *gen_opc_ptr;
static uint32_t *gen_opparam_ptr;
extern FILE *logfile;
extern int loglevel;

enum {
#define DEF(s, n, copy_size) INDEX_op_ ## s,
#include "opc.h"
#undef DEF
    NB_OPS,
};

#include "gen-op.h"

#if defined(CONFIG_USER_ONLY)
#define gen_st(s, name, addr, val) gen_op_st##name##_raw(addr, val)
#define gen_ld(s, name, val, addr) gen_op_ld##name##_raw(val, addr)
#else
#define gen_st(s, name, addr, val) do { \
    if (IS_USER(s)) \
        gen_op_st##name##_user(addr, val); \
    else \
        gen_op_st##name##_kernel(addr, val); \
    } while (0)
#define gen_ld(s, name, val, addr) do { \
    if (IS_USER(s)) \
        gen_op_ld##name##_user(val, addr); \
    else \
        gen_op_ld##name##_kernel(val, addr); \
    } while (0)
#endif

#include "op-hacks.h"

#define OS_BYTE 0
#define OS_WORD 1
#define OS_LONG 2
#define OS_SINGLE 4
#define OS_DOUBLE 5

#define DREG(insn, pos) (((insn >> pos) & 7) + QREG_D0)
#define AREG(insn, pos) (((insn >> pos) & 7) + QREG_A0)
#define FREG(insn, pos) (((insn >> pos) & 7) + QREG_F0)

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

/* Generate a load from the specified address.  Narrow values are
   sign extended to full register width.  */
static inline int gen_load(DisasContext * s, int opsize, int addr, int sign)
{
    int tmp;
    s->is_mem = 1;
    switch(opsize) {
    case OS_BYTE:
        tmp = gen_new_qreg(QMODE_I32);
        if (sign)
            gen_ld(s, 8s32, tmp, addr);
        else
            gen_ld(s, 8u32, tmp, addr);
        break;
    case OS_WORD:
        tmp = gen_new_qreg(QMODE_I32);
        if (sign)
            gen_ld(s, 16s32, tmp, addr);
        else
            gen_ld(s, 16u32, tmp, addr);
        break;
    case OS_LONG:
        tmp = gen_new_qreg(QMODE_I32);
        gen_ld(s, 32, tmp, addr);
        break;
    case OS_SINGLE:
        tmp = gen_new_qreg(QMODE_F32);
        gen_ld(s, f32, tmp, addr);
        break;
    case OS_DOUBLE:
        tmp  = gen_new_qreg(QMODE_F64);
        gen_ld(s, f64, tmp, addr);
        break;
    default:
        qemu_assert(0, "bad load size");
    }
    gen_throws_exception = gen_last_qop;
    return tmp;
}

/* Generate a store.  */
static inline void gen_store(DisasContext *s, int opsize, int addr, int val)
{
    s->is_mem = 1;
    switch(opsize) {
    case OS_BYTE:
        gen_st(s, 8, addr, val);
        break;
    case OS_WORD:
        gen_st(s, 16, addr, val);
        break;
    case OS_LONG:
        gen_st(s, 32, addr, val);
        break;
    case OS_SINGLE:
        gen_st(s, f32, addr, val);
        break;
    case OS_DOUBLE:
        gen_st(s, f64, addr, val);
        break;
    default:
        qemu_assert(0, "bad store size");
    }
    gen_throws_exception = gen_last_qop;
}

/* Generate an unsigned load if VAL is 0 a signed load if val is -1,
   otherwise generate a store.  */
static int gen_ldst(DisasContext *s, int opsize, int addr, int val)
{
    if (val > 0) {
        gen_store(s, opsize, addr, val);
        return 0;
    } else {
        return gen_load(s, opsize, addr, val != 0);
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
static int gen_addr_index(uint16_t ext, int tmp)
{
    int add;
    int scale;

    add = (ext & 0x8000) ? AREG(ext, 12) : DREG(ext, 12);
    if ((ext & 0x800) == 0) {
        gen_op_ext16s32(tmp, add);
        add = tmp;
    }
    scale = (ext >> 9) & 3;
    if (scale != 0) {
        gen_op_shl32(tmp, add, gen_im32(scale));
        add = tmp;
    }
    return add;
}

/* Handle a base + index + displacement effective addresss.  A base of
   -1 means pc-relative.  */
static int gen_lea_indexed(DisasContext *s, int opsize, int base)
{
    uint32_t offset;
    uint16_t ext;
    int add;
    int tmp;
    uint32_t bd, od;

    offset = s->pc;
    ext = lduw_code(s->pc);
    s->pc += 2;

    if ((ext & 0x800) == 0 && !m68k_feature(s->env, M68K_FEATURE_WORD_INDEX))
        return -1;

    if (ext & 0x100) {
        /* full extension word format */
        if (!m68k_feature(s->env, M68K_FEATURE_EXT_FULL))
            return -1;

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
            add = QREG_NULL;
        }
        if ((ext & 0x80) == 0) {
            /* base not suppressed */
            if (base == -1) {
                base = gen_im32(offset + bd);
                bd = 0;
            }
            if (add) {
                gen_op_add32(tmp, add, base);
                add = tmp;
            } else {
                add = base;
            }
        }
        if (add) {
            if (bd != 0) {
                gen_op_add32(tmp, add, gen_im32(bd));
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
                gen_op_add32(tmp, add, base);
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
                gen_op_add32(tmp, add, gen_im32(od));
                add = tmp;
            }
        }
    } else {
        /* brief extension word format */
        tmp = gen_new_qreg(QMODE_I32);
        add = gen_addr_index(ext, tmp);
        if (base != -1) {
            gen_op_add32(tmp, add, base);
            if ((int8_t)ext)
                gen_op_add32(tmp, tmp, gen_im32((int8_t)ext));
        } else {
            gen_op_add32(tmp, add, gen_im32(offset + (int8_t)ext));
        }
        add = tmp;
    }
    return add;
}

/* Update the CPU env CC_OP state.  */
static inline void gen_flush_cc_op(DisasContext *s)
{
    if (s->cc_op != CC_OP_DYNAMIC)
        gen_op_mov32(QREG_CC_OP, gen_im32(s->cc_op));
}

/* Evaluate all the CC flags.  */
static inline void gen_flush_flags(DisasContext *s)
{
    if (s->cc_op == CC_OP_FLAGS)
        return;
    gen_flush_cc_op(s);
    gen_op_flush_flags();
    s->cc_op = CC_OP_FLAGS;
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
static void gen_partset_reg(int opsize, int reg, int val)
{
    int tmp;
    switch (opsize) {
    case OS_BYTE:
        gen_op_and32(reg, reg, gen_im32(0xffffff00));
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_and32(tmp, val, gen_im32(0xff));
        gen_op_or32(reg, reg, tmp);
        break;
    case OS_WORD:
        gen_op_and32(reg, reg, gen_im32(0xffff0000));
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_and32(tmp, val, gen_im32(0xffff));
        gen_op_or32(reg, reg, tmp);
        break;
    case OS_LONG:
        gen_op_mov32(reg, val);
        break;
    case OS_SINGLE:
        gen_op_pack_32_f32(reg, val);
        break;
    default:
        qemu_assert(0, "Bad operand size");
        break;
    }
}

/* Sign or zero extend a value.  */
static inline int gen_extend(int val, int opsize, int sign)
{
    int tmp;

    switch (opsize) {
    case OS_BYTE:
        tmp = gen_new_qreg(QMODE_I32);
        if (sign)
            gen_op_ext8s32(tmp, val);
        else
            gen_op_ext8u32(tmp, val);
        break;
    case OS_WORD:
        tmp = gen_new_qreg(QMODE_I32);
        if (sign)
            gen_op_ext16s32(tmp, val);
        else
            gen_op_ext16u32(tmp, val);
        break;
    case OS_LONG:
        tmp = val;
        break;
    case OS_SINGLE:
        tmp = gen_new_qreg(QMODE_F32);
        gen_op_pack_f32_32(tmp, val);
        break;
    default:
        qemu_assert(0, "Bad operand size");
    }
    return tmp;
}

/* Generate code for an "effective address".  Does not adjust the base
   register for autoincrememnt addressing modes.  */
static int gen_lea(DisasContext *s, uint16_t insn, int opsize)
{
    int reg;
    int tmp;
    uint16_t ext;
    uint32_t offset;

    reg = insn & 7;
    switch ((insn >> 3) & 7) {
    case 0: /* Data register direct.  */
    case 1: /* Address register direct.  */
        return -1;
    case 2: /* Indirect register */
    case 3: /* Indirect postincrement.  */
        reg += QREG_A0;
        return reg;
    case 4: /* Indirect predecrememnt.  */
        reg += QREG_A0;
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_sub32(tmp, reg, gen_im32(opsize_bytes(opsize)));
        return tmp;
    case 5: /* Indirect displacement.  */
        reg += QREG_A0;
        tmp = gen_new_qreg(QMODE_I32);
        ext = lduw_code(s->pc);
        s->pc += 2;
        gen_op_add32(tmp, reg, gen_im32((int16_t)ext));
        return tmp;
    case 6: /* Indirect index + displacement.  */
        reg += QREG_A0;
        return gen_lea_indexed(s, opsize, reg);
    case 7: /* Other */
        switch (reg) {
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
            return gen_lea_indexed(s, opsize, -1);
        case 4: /* Immediate.  */
        default:
            return -1;
        }
    }
    /* Should never happen.  */
    return -1;
}

/* Helper function for gen_ea. Reuse the computed address between the
   for read/write operands.  */
static inline int gen_ea_once(DisasContext *s, uint16_t insn, int opsize,
                              int val, int *addrp)
{
    int tmp;

    if (addrp && val > 0) {
        tmp = *addrp;
    } else {
        tmp = gen_lea(s, insn, opsize);
        if (tmp == -1)
            return -1;
        if (addrp)
            *addrp = tmp;
    }
    return gen_ldst(s, opsize, tmp, val);
}

/* Generate code to load/store a value ito/from an EA.  If VAL > 0 this is
   a write otherwise it is a read (0 == sign extend, -1 == zero extend).
   ADDRP is non-null for readwrite operands.  */
static int gen_ea(DisasContext *s, uint16_t insn, int opsize, int val,
                  int *addrp)
{
    int reg;
    int result;
    uint32_t offset;

    reg = insn & 7;
    switch ((insn >> 3) & 7) {
    case 0: /* Data register direct.  */
        reg += QREG_D0;
        if (val > 0) {
            gen_partset_reg(opsize, reg, val);
            return 0;
        } else {
            return gen_extend(reg, opsize, val);
        }
    case 1: /* Address register direct.  */
        reg += QREG_A0;
        if (val > 0) {
            gen_op_mov32(reg, val);
            return 0;
        } else {
            return gen_extend(reg, opsize, val);
        }
    case 2: /* Indirect register */
        reg += QREG_A0;
        return gen_ldst(s, opsize, reg, val);
    case 3: /* Indirect postincrement.  */
        reg += QREG_A0;
        result = gen_ldst(s, opsize, reg, val);
        /* ??? This is not exception safe.  The instruction may still
           fault after this point.  */
        if (val > 0 || !addrp)
            gen_op_add32(reg, reg, gen_im32(opsize_bytes(opsize)));
        return result;
    case 4: /* Indirect predecrememnt.  */
        {
            int tmp;
            if (addrp && val > 0) {
                tmp = *addrp;
            } else {
                tmp = gen_lea(s, insn, opsize);
                if (tmp == -1)
                    return -1;
                if (addrp)
                    *addrp = tmp;
            }
            result = gen_ldst(s, opsize, tmp, val);
            /* ??? This is not exception safe.  The instruction may still
               fault after this point.  */
            if (val > 0 || !addrp) {
                reg += QREG_A0;
                gen_op_mov32(reg, tmp);
            }
        }
        return result;
    case 5: /* Indirect displacement.  */
    case 6: /* Indirect index + displacement.  */
        return gen_ea_once(s, insn, opsize, val, addrp);
    case 7: /* Other */
        switch (reg) {
        case 0: /* Absolute short.  */
        case 1: /* Absolute long.  */
        case 2: /* pc displacement  */
        case 3: /* pc index+displacement.  */
            return gen_ea_once(s, insn, opsize, val, addrp);
        case 4: /* Immediate.  */
            /* Sign extend values for consistency.  */
            switch (opsize) {
            case OS_BYTE:
                if (val)
                    offset = ldsb_code(s->pc + 1);
                else
                    offset = ldub_code(s->pc + 1);
                s->pc += 2;
                break;
            case OS_WORD:
                if (val)
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
            return gen_im32(offset);
        default:
            return -1;
        }
    }
    /* Should never happen.  */
    return -1;
}

static void gen_logic_cc(DisasContext *s, int val)
{
    gen_op_logic_cc(val);
    s->cc_op = CC_OP_LOGIC;
}

static void gen_jmpcc(DisasContext *s, int cond, int l1)
{
    int tmp;

    gen_flush_flags(s);
    switch (cond) {
    case 0: /* T */
        gen_op_jmp(l1);
        break;
    case 1: /* F */
        break;
    case 2: /* HI (!C && !Z) */
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_and32(tmp, QREG_CC_DEST, gen_im32(CCF_C | CCF_Z));
        gen_op_jmp_z32(tmp, l1);
        break;
    case 3: /* LS (C || Z) */
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_and32(tmp, QREG_CC_DEST, gen_im32(CCF_C | CCF_Z));
        gen_op_jmp_nz32(tmp, l1);
        break;
    case 4: /* CC (!C) */
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_and32(tmp, QREG_CC_DEST, gen_im32(CCF_C));
        gen_op_jmp_z32(tmp, l1);
        break;
    case 5: /* CS (C) */
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_and32(tmp, QREG_CC_DEST, gen_im32(CCF_C));
        gen_op_jmp_nz32(tmp, l1);
        break;
    case 6: /* NE (!Z) */
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_and32(tmp, QREG_CC_DEST, gen_im32(CCF_Z));
        gen_op_jmp_z32(tmp, l1);
        break;
    case 7: /* EQ (Z) */
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_and32(tmp, QREG_CC_DEST, gen_im32(CCF_Z));
        gen_op_jmp_nz32(tmp, l1);
        break;
    case 8: /* VC (!V) */
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_and32(tmp, QREG_CC_DEST, gen_im32(CCF_V));
        gen_op_jmp_z32(tmp, l1);
        break;
    case 9: /* VS (V) */
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_and32(tmp, QREG_CC_DEST, gen_im32(CCF_V));
        gen_op_jmp_nz32(tmp, l1);
        break;
    case 10: /* PL (!N) */
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_and32(tmp, QREG_CC_DEST, gen_im32(CCF_N));
        gen_op_jmp_z32(tmp, l1);
        break;
    case 11: /* MI (N) */
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_and32(tmp, QREG_CC_DEST, gen_im32(CCF_N));
        gen_op_jmp_nz32(tmp, l1);
        break;
    case 12: /* GE (!(N ^ V)) */
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_shr32(tmp, QREG_CC_DEST, gen_im32(2));
        gen_op_xor32(tmp, tmp, QREG_CC_DEST);
        gen_op_and32(tmp, tmp, gen_im32(CCF_V));
        gen_op_jmp_z32(tmp, l1);
        break;
    case 13: /* LT (N ^ V) */
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_shr32(tmp, QREG_CC_DEST, gen_im32(2));
        gen_op_xor32(tmp, tmp, QREG_CC_DEST);
        gen_op_and32(tmp, tmp, gen_im32(CCF_V));
        gen_op_jmp_nz32(tmp, l1);
        break;
    case 14: /* GT (!(Z || (N ^ V))) */
        {
            int l2;
            l2 = gen_new_label();
            tmp = gen_new_qreg(QMODE_I32);
            gen_op_and32(tmp, QREG_CC_DEST, gen_im32(CCF_Z));
            gen_op_jmp_nz32(tmp, l2);
            tmp = gen_new_qreg(QMODE_I32);
            gen_op_shr32(tmp, QREG_CC_DEST, gen_im32(2));
            gen_op_xor32(tmp, tmp, QREG_CC_DEST);
            gen_op_and32(tmp, tmp, gen_im32(CCF_V));
            gen_op_jmp_nz32(tmp, l2);
            gen_op_jmp(l1);
            gen_set_label(l2);
        }
        break;
    case 15: /* LE (Z || (N ^ V)) */
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_and32(tmp, QREG_CC_DEST, gen_im32(CCF_Z));
        gen_op_jmp_nz32(tmp, l1);
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_shr32(tmp, QREG_CC_DEST, gen_im32(2));
        gen_op_xor32(tmp, tmp, QREG_CC_DEST);
        gen_op_and32(tmp, tmp, gen_im32(CCF_V));
        gen_op_jmp_nz32(tmp, l1);
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
    int reg;

    l1 = gen_new_label();
    cond = (insn >> 8) & 0xf;
    reg = DREG(insn, 0);
    gen_op_and32(reg, reg, gen_im32(0xffffff00));
    gen_jmpcc(s, cond ^ 1, l1);
    gen_op_or32(reg, reg, gen_im32(0xff));
    gen_set_label(l1);
}

/* Force a TB lookup after an instruction that changes the CPU state.  */
static void gen_lookup_tb(DisasContext *s)
{
    gen_flush_cc_op(s);
    gen_op_mov32(QREG_PC, gen_im32(s->pc));
    s->is_jmp = DISAS_UPDATE;
}

/* Generate a jump to to the address in qreg DEST.  */
static void gen_jmp(DisasContext *s, int dest)
{
    gen_flush_cc_op(s);
    gen_op_mov32(QREG_PC, dest);
    s->is_jmp = DISAS_JUMP;
}

static void gen_exception(DisasContext *s, uint32_t where, int nr)
{
    gen_flush_cc_op(s);
    gen_jmp(s, gen_im32(where));
    gen_op_raise_exception(nr);
}

static inline void gen_addr_fault(DisasContext *s)
{
    gen_exception(s, s->insn_pc, EXCP_ADDRESS);
}

#define SRC_EA(result, opsize, val, addrp) do { \
    result = gen_ea(s, insn, opsize, val, addrp); \
    if (result == -1) { \
        gen_addr_fault(s); \
        return; \
    } \
    } while (0)

#define DEST_EA(insn, opsize, val, addrp) do { \
    int ea_result = gen_ea(s, insn, opsize, val, addrp); \
    if (ea_result == -1) { \
        gen_addr_fault(s); \
        return; \
    } \
    } while (0)

/* Generate a jump to an immediate address.  */
static void gen_jmp_tb(DisasContext *s, int n, uint32_t dest)
{
    TranslationBlock *tb;

    tb = s->tb;
    if (__builtin_expect (s->singlestep_enabled, 0)) {
        gen_exception(s, dest, EXCP_DEBUG);
    } else if ((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK) ||
               (s->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK)) {
        gen_op_goto_tb(0, n, (long)tb);
        gen_op_mov32(QREG_PC, gen_im32(dest));
        gen_op_mov32(QREG_T0, gen_im32((long)tb + n));
        gen_op_exit_tb();
    } else {
        gen_jmp(s, gen_im32(dest));
        gen_op_mov32(QREG_T0, gen_im32(0));
        gen_op_exit_tb();
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
    int reg;
    int tmp;
    int src;
    int sign;

    sign = (insn & 0x100) != 0;
    reg = DREG(insn, 9);
    tmp = gen_new_qreg(QMODE_I32);
    if (sign)
        gen_op_ext16s32(tmp, reg);
    else
        gen_op_ext16u32(tmp, reg);
    SRC_EA(src, OS_WORD, sign ? -1 : 0, NULL);
    gen_op_mul32(tmp, tmp, src);
    gen_op_mov32(reg, tmp);
    /* Unlike m68k, coldfire always clears the overflow bit.  */
    gen_logic_cc(s, tmp);
}

DISAS_INSN(divw)
{
    int reg;
    int tmp;
    int src;
    int sign;

    sign = (insn & 0x100) != 0;
    reg = DREG(insn, 9);
    if (sign) {
        gen_op_ext16s32(QREG_DIV1, reg);
    } else {
        gen_op_ext16u32(QREG_DIV1, reg);
    }
    SRC_EA(src, OS_WORD, sign ? -1 : 0, NULL);
    gen_op_mov32(QREG_DIV2, src);
    if (sign) {
        gen_op_divs(1);
    } else {
        gen_op_divu(1);
    }

    tmp = gen_new_qreg(QMODE_I32);
    src = gen_new_qreg(QMODE_I32);
    gen_op_ext16u32(tmp, QREG_DIV1);
    gen_op_shl32(src, QREG_DIV2, gen_im32(16));
    gen_op_or32(reg, tmp, src);
    gen_op_flags_set();
    s->cc_op = CC_OP_FLAGS;
}

DISAS_INSN(divl)
{
    int num;
    int den;
    int reg;
    uint16_t ext;

    ext = lduw_code(s->pc);
    s->pc += 2;
    if (ext & 0x87f8) {
        gen_exception(s, s->pc - 4, EXCP_UNSUPPORTED);
        return;
    }
    num = DREG(ext, 12);
    reg = DREG(ext, 0);
    gen_op_mov32(QREG_DIV1, num);
    SRC_EA(den, OS_LONG, 0, NULL);
    gen_op_mov32(QREG_DIV2, den);
    if (ext & 0x0800) {
        gen_op_divs(2);
    } else {
        gen_op_divu(2);
    }
    if (num == reg) {
        /* div */
        gen_op_mov32 (reg, QREG_DIV1);
    } else {
        /* rem */
        gen_op_mov32 (reg, QREG_DIV2);
    }
    gen_op_flags_set();
    s->cc_op = CC_OP_FLAGS;
}

DISAS_INSN(addsub)
{
    int reg;
    int dest;
    int src;
    int tmp;
    int addr;
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
        gen_op_add32(dest, tmp, src);
        gen_op_update_xflag_lt(dest, src);
        s->cc_op = CC_OP_ADD;
    } else {
        gen_op_update_xflag_lt(tmp, src);
        gen_op_sub32(dest, tmp, src);
        s->cc_op = CC_OP_SUB;
    }
    gen_op_update_cc_add(dest, src);
    if (insn & 0x100) {
        DEST_EA(insn, OS_LONG, dest, &addr);
    } else {
        gen_op_mov32(reg, dest);
    }
}


/* Reverse the order of the bits in REG.  */
DISAS_INSN(bitrev)
{
    int val;
    int tmp1;
    int tmp2;
    int reg;

    val = gen_new_qreg(QMODE_I32);
    tmp1 = gen_new_qreg(QMODE_I32);
    tmp2 = gen_new_qreg(QMODE_I32);
    reg = DREG(insn, 0);
    gen_op_mov32(val, reg);
    /* Reverse bits within each nibble.  */
    gen_op_shl32(tmp1, val, gen_im32(3));
    gen_op_and32(tmp1, tmp1, gen_im32(0x88888888));
    gen_op_shl32(tmp2, val, gen_im32(1));
    gen_op_and32(tmp2, tmp2, gen_im32(0x44444444));
    gen_op_or32(tmp1, tmp1, tmp2);
    gen_op_shr32(tmp2, val, gen_im32(1));
    gen_op_and32(tmp2, tmp2, gen_im32(0x22222222));
    gen_op_or32(tmp1, tmp1, tmp2);
    gen_op_shr32(tmp2, val, gen_im32(3));
    gen_op_and32(tmp2, tmp2, gen_im32(0x11111111));
    gen_op_or32(tmp1, tmp1, tmp2);
    /* Reverse nibbles withing bytes.  */
    gen_op_shl32(val, tmp1, gen_im32(4));
    gen_op_and32(val, val, gen_im32(0xf0f0f0f0));
    gen_op_shr32(tmp2, tmp1, gen_im32(4));
    gen_op_and32(tmp2, tmp2, gen_im32(0x0f0f0f0f));
    gen_op_or32(val, val, tmp2);
    /* Reverse bytes.  */
    gen_op_bswap32(reg, val);
    gen_op_mov32(reg, val);
}

DISAS_INSN(bitop_reg)
{
    int opsize;
    int op;
    int src1;
    int src2;
    int tmp;
    int addr;
    int dest;

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
        gen_op_and32(tmp, src2, gen_im32(7));
    else
        gen_op_and32(tmp, src2, gen_im32(31));
    src2 = tmp;
    tmp = gen_new_qreg(QMODE_I32);
    gen_op_shl32(tmp, gen_im32(1), src2);

    gen_op_btest(src1, tmp);
    switch (op) {
    case 1: /* bchg */
        gen_op_xor32(dest, src1, tmp);
        break;
    case 2: /* bclr */
        gen_op_not32(tmp, tmp);
        gen_op_and32(dest, src1, tmp);
        break;
    case 3: /* bset */
        gen_op_or32(dest, src1, tmp);
        break;
    default: /* btst */
        break;
    }
    if (op)
        DEST_EA(insn, opsize, dest, &addr);
}

DISAS_INSN(sats)
{
    int reg;
    int tmp;
    int l1;

    reg = DREG(insn, 0);
    tmp = gen_new_qreg(QMODE_I32);
    gen_flush_flags(s);
    gen_op_and32(tmp, QREG_CC_DEST, gen_im32(CCF_V));
    l1 = gen_new_label();
    gen_op_jmp_z32(tmp, l1);
    tmp = gen_new_qreg(QMODE_I32);
    gen_op_shr32(tmp, reg, gen_im32(31));
    gen_op_xor32(tmp, tmp, gen_im32(0x80000000));
    gen_op_mov32(reg, tmp);
    gen_set_label(l1);
    gen_logic_cc(s, tmp);
}

static void gen_push(DisasContext *s, int val)
{
    int tmp;

    tmp = gen_new_qreg(QMODE_I32);
    gen_op_sub32(tmp, QREG_SP, gen_im32(4));
    gen_store(s, OS_LONG, tmp, val);
    gen_op_mov32(QREG_SP, tmp);
}

DISAS_INSN(movem)
{
    int addr;
    int i;
    uint16_t mask;
    int reg;
    int tmp;
    int is_load;

    mask = lduw_code(s->pc);
    s->pc += 2;
    tmp = gen_lea(s, insn, OS_LONG);
    if (tmp == -1) {
        gen_addr_fault(s);
        return;
    }
    addr = gen_new_qreg(QMODE_I32);
    gen_op_mov32(addr, tmp);
    is_load = ((insn & 0x0400) != 0);
    for (i = 0; i < 16; i++, mask >>= 1) {
        if (mask & 1) {
            if (i < 8)
                reg = DREG(i, 0);
            else
                reg = AREG(i, 0);
            if (is_load) {
                tmp = gen_load(s, OS_LONG, addr, 0);
                gen_op_mov32(reg, tmp);
            } else {
                gen_store(s, OS_LONG, addr, reg);
            }
            if (mask != 1)
                gen_op_add32(addr, addr, gen_im32(4));
        }
    }
}

DISAS_INSN(bitop_im)
{
    int opsize;
    int op;
    int src1;
    uint32_t mask;
    int bitnum;
    int tmp;
    int addr;
    int dest;

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
    tmp = gen_new_qreg(QMODE_I32);
    if (opsize == OS_BYTE)
        bitnum &= 7;
    else
        bitnum &= 31;
    mask = 1 << bitnum;

    gen_op_btest(src1, gen_im32(mask));
    if (op)
        dest = gen_new_qreg(QMODE_I32);
    else
        dest = -1;

    switch (op) {
    case 1: /* bchg */
        gen_op_xor32(dest, src1, gen_im32(mask));
        break;
    case 2: /* bclr */
        gen_op_and32(dest, src1, gen_im32(~mask));
        break;
    case 3: /* bset */
        gen_op_or32(dest, src1, gen_im32(mask));
        break;
    default: /* btst */
        break;
    }
    if (op)
        DEST_EA(insn, opsize, dest, &addr);
}

DISAS_INSN(arith_im)
{
    int op;
    int src1;
    int dest;
    int src2;
    int addr;

    op = (insn >> 9) & 7;
    SRC_EA(src1, OS_LONG, 0, (op == 6) ? NULL : &addr);
    src2 = gen_im32(read_im32(s));
    dest = gen_new_qreg(QMODE_I32);
    switch (op) {
    case 0: /* ori */
        gen_op_or32(dest, src1, src2);
        gen_logic_cc(s, dest);
        break;
    case 1: /* andi */
        gen_op_and32(dest, src1, src2);
        gen_logic_cc(s, dest);
        break;
    case 2: /* subi */
        gen_op_mov32(dest, src1);
        gen_op_update_xflag_lt(dest, src2);
        gen_op_sub32(dest, dest, src2);
        gen_op_update_cc_add(dest, src2);
        s->cc_op = CC_OP_SUB;
        break;
    case 3: /* addi */
        gen_op_mov32(dest, src1);
        gen_op_add32(dest, dest, src2);
        gen_op_update_cc_add(dest, src2);
        gen_op_update_xflag_lt(dest, src2);
        s->cc_op = CC_OP_ADD;
        break;
    case 5: /* eori */
        gen_op_xor32(dest, src1, src2);
        gen_logic_cc(s, dest);
        break;
    case 6: /* cmpi */
        gen_op_mov32(dest, src1);
        gen_op_sub32(dest, dest, src2);
        gen_op_update_cc_add(dest, src2);
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
    int reg;

    reg = DREG(insn, 0);
    gen_op_bswap32(reg, reg);
}

DISAS_INSN(move)
{
    int src;
    int dest;
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
    SRC_EA(src, opsize, -1, NULL);
    op = (insn >> 6) & 7;
    if (op == 1) {
        /* movea */
        /* The value will already have been sign extended.  */
        dest = AREG(insn, 9);
        gen_op_mov32(dest, src);
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
    int reg;
    int dest;
    int tmp;

    gen_flush_flags(s);
    reg = DREG(insn, 0);
    dest = gen_new_qreg(QMODE_I32);
    gen_op_mov32 (dest, gen_im32(0));
    gen_op_subx_cc(dest, reg);
    /* !Z is sticky.  */
    tmp = gen_new_qreg(QMODE_I32);
    gen_op_mov32 (tmp, QREG_CC_DEST);
    gen_op_update_cc_add(dest, reg);
    gen_op_mov32(reg, dest);
    s->cc_op = CC_OP_DYNAMIC;
    gen_flush_flags(s);
    gen_op_or32(tmp, tmp, gen_im32(~CCF_Z));
    gen_op_and32(QREG_CC_DEST, QREG_CC_DEST, tmp);
    s->cc_op = CC_OP_FLAGS;
}

DISAS_INSN(lea)
{
    int reg;
    int tmp;

    reg = AREG(insn, 9);
    tmp = gen_lea(s, insn, OS_LONG);
    if (tmp == -1) {
        gen_addr_fault(s);
        return;
    }
    gen_op_mov32(reg, tmp);
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

static int gen_get_ccr(DisasContext *s)
{
    int dest;

    gen_flush_flags(s);
    dest = gen_new_qreg(QMODE_I32);
    gen_op_get_xflag(dest);
    gen_op_shl32(dest, dest, gen_im32(4));
    gen_op_or32(dest, dest, QREG_CC_DEST);
    return dest;
}

DISAS_INSN(move_from_ccr)
{
    int reg;
    int ccr;

    ccr = gen_get_ccr(s);
    reg = DREG(insn, 0);
    gen_partset_reg(OS_WORD, reg, ccr);
}

DISAS_INSN(neg)
{
    int reg;
    int src1;

    reg = DREG(insn, 0);
    src1 = gen_new_qreg(QMODE_I32);
    gen_op_mov32(src1, reg);
    gen_op_neg32(reg, src1);
    s->cc_op = CC_OP_SUB;
    gen_op_update_cc_add(reg, src1);
    gen_op_update_xflag_lt(gen_im32(0), src1);
    s->cc_op = CC_OP_SUB;
}

static void gen_set_sr_im(DisasContext *s, uint16_t val, int ccr_only)
{
    gen_op_logic_cc(gen_im32(val & 0xf));
    gen_op_update_xflag_tst(gen_im32((val & 0x10) >> 4));
    if (!ccr_only) {
        gen_op_set_sr(gen_im32(val & 0xff00));
    }
}

static void gen_set_sr(DisasContext *s, uint16_t insn, int ccr_only)
{
    int src1;
    int reg;

    s->cc_op = CC_OP_FLAGS;
    if ((insn & 0x38) == 0)
      {
        src1 = gen_new_qreg(QMODE_I32);
        reg = DREG(insn, 0);
        gen_op_and32(src1, reg, gen_im32(0xf));
        gen_op_logic_cc(src1);
        gen_op_shr32(src1, reg, gen_im32(4));
        gen_op_and32(src1, src1, gen_im32(1));
        gen_op_update_xflag_tst(src1);
        if (!ccr_only) {
            gen_op_set_sr(reg);
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
    int reg;

    reg = DREG(insn, 0);
    gen_op_not32(reg, reg);
    gen_logic_cc(s, reg);
}

DISAS_INSN(swap)
{
    int dest;
    int src1;
    int src2;
    int reg;

    dest = gen_new_qreg(QMODE_I32);
    src1 = gen_new_qreg(QMODE_I32);
    src2 = gen_new_qreg(QMODE_I32);
    reg = DREG(insn, 0);
    gen_op_shl32(src1, reg, gen_im32(16));
    gen_op_shr32(src2, reg, gen_im32(16));
    gen_op_or32(dest, src1, src2);
    gen_op_mov32(reg, dest);
    gen_logic_cc(s, dest);
}

DISAS_INSN(pea)
{
    int tmp;

    tmp = gen_lea(s, insn, OS_LONG);
    if (tmp == -1) {
        gen_addr_fault(s);
        return;
    }
    gen_push(s, tmp);
}

DISAS_INSN(ext)
{
    int reg;
    int op;
    int tmp;

    reg = DREG(insn, 0);
    op = (insn >> 6) & 7;
    tmp = gen_new_qreg(QMODE_I32);
    if (op == 3)
        gen_op_ext16s32(tmp, reg);
    else
        gen_op_ext8s32(tmp, reg);
    if (op == 2)
        gen_partset_reg(OS_WORD, reg, tmp);
    else
      gen_op_mov32(reg, tmp);
    gen_logic_cc(s, tmp);
}

DISAS_INSN(tst)
{
    int opsize;
    int tmp;

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
    SRC_EA(tmp, opsize, -1, NULL);
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
    int dest;
    int src1;
    int addr;

    dest = gen_new_qreg(QMODE_I32);
    SRC_EA(src1, OS_BYTE, -1, &addr);
    gen_logic_cc(s, src1);
    gen_op_or32(dest, src1, gen_im32(0x80));
    DEST_EA(insn, OS_BYTE, dest, &addr);
}

DISAS_INSN(mull)
{
    uint16_t ext;
    int reg;
    int src1;
    int dest;

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
    gen_op_mul32(dest, src1, reg);
    gen_op_mov32(reg, dest);
    /* Unlike m68k, coldfire always clears the overflow bit.  */
    gen_logic_cc(s, dest);
}

DISAS_INSN(link)
{
    int16_t offset;
    int reg;
    int tmp;

    offset = ldsw_code(s->pc);
    s->pc += 2;
    reg = AREG(insn, 0);
    tmp = gen_new_qreg(QMODE_I32);
    gen_op_sub32(tmp, QREG_SP, gen_im32(4));
    gen_store(s, OS_LONG, tmp, reg);
    if (reg != QREG_SP)
        gen_op_mov32(reg, tmp);
    gen_op_add32(QREG_SP, tmp, gen_im32(offset));
}

DISAS_INSN(unlk)
{
    int src;
    int reg;
    int tmp;

    src = gen_new_qreg(QMODE_I32);
    reg = AREG(insn, 0);
    gen_op_mov32(src, reg);
    tmp = gen_load(s, OS_LONG, src, 0);
    gen_op_mov32(reg, tmp);
    gen_op_add32(QREG_SP, src, gen_im32(4));
}

DISAS_INSN(nop)
{
}

DISAS_INSN(rts)
{
    int tmp;

    tmp = gen_load(s, OS_LONG, QREG_SP, 0);
    gen_op_add32(QREG_SP, QREG_SP, gen_im32(4));
    gen_jmp(s, tmp);
}

DISAS_INSN(jump)
{
    int tmp;

    /* Load the target address first to ensure correct exception
       behavior.  */
    tmp = gen_lea(s, insn, OS_LONG);
    if (tmp == -1) {
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
    int src1;
    int src2;
    int dest;
    int val;
    int addr;

    SRC_EA(src1, OS_LONG, 0, &addr);
    val = (insn >> 9) & 7;
    if (val == 0)
        val = 8;
    src2 = gen_im32(val);
    dest = gen_new_qreg(QMODE_I32);
    gen_op_mov32(dest, src1);
    if ((insn & 0x38) == 0x08) {
        /* Don't update condition codes if the destination is an
           address register.  */
        if (insn & 0x0100) {
            gen_op_sub32(dest, dest, src2);
        } else {
            gen_op_add32(dest, dest, src2);
        }
    } else {
        if (insn & 0x0100) {
            gen_op_update_xflag_lt(dest, src2);
            gen_op_sub32(dest, dest, src2);
            s->cc_op = CC_OP_SUB;
        } else {
            gen_op_add32(dest, dest, src2);
            gen_op_update_xflag_lt(dest, src2);
            s->cc_op = CC_OP_ADD;
        }
        gen_op_update_cc_add(dest, src2);
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
    int tmp;

    tmp = gen_im32((int8_t)insn);
    gen_op_mov32(DREG(insn, 9), tmp);
    gen_logic_cc(s, tmp);
}

DISAS_INSN(mvzs)
{
    int opsize;
    int src;
    int reg;

    if (insn & 0x40)
        opsize = OS_WORD;
    else
        opsize = OS_BYTE;
    SRC_EA(src, opsize, (insn & 0x80) ? 0 : -1, NULL);
    reg = DREG(insn, 9);
    gen_op_mov32(reg, src);
    gen_logic_cc(s, src);
}

DISAS_INSN(or)
{
    int reg;
    int dest;
    int src;
    int addr;

    reg = DREG(insn, 9);
    dest = gen_new_qreg(QMODE_I32);
    if (insn & 0x100) {
        SRC_EA(src, OS_LONG, 0, &addr);
        gen_op_or32(dest, src, reg);
        DEST_EA(insn, OS_LONG, dest, &addr);
    } else {
        SRC_EA(src, OS_LONG, 0, NULL);
        gen_op_or32(dest, src, reg);
        gen_op_mov32(reg, dest);
    }
    gen_logic_cc(s, dest);
}

DISAS_INSN(suba)
{
    int src;
    int reg;

    SRC_EA(src, OS_LONG, 0, NULL);
    reg = AREG(insn, 9);
    gen_op_sub32(reg, reg, src);
}

DISAS_INSN(subx)
{
    int reg;
    int src;
    int dest;
    int tmp;

    gen_flush_flags(s);
    reg = DREG(insn, 9);
    src = DREG(insn, 0);
    dest = gen_new_qreg(QMODE_I32);
    gen_op_mov32 (dest, reg);
    gen_op_subx_cc(dest, src);
    /* !Z is sticky.  */
    tmp = gen_new_qreg(QMODE_I32);
    gen_op_mov32 (tmp, QREG_CC_DEST);
    gen_op_update_cc_add(dest, src);
    gen_op_mov32(reg, dest);
    s->cc_op = CC_OP_DYNAMIC;
    gen_flush_flags(s);
    gen_op_or32(tmp, tmp, gen_im32(~CCF_Z));
    gen_op_and32(QREG_CC_DEST, QREG_CC_DEST, tmp);
    s->cc_op = CC_OP_FLAGS;
}

DISAS_INSN(mov3q)
{
    int src;
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
    int src;
    int reg;
    int dest;
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
    SRC_EA(src, opsize, -1, NULL);
    reg = DREG(insn, 9);
    dest = gen_new_qreg(QMODE_I32);
    gen_op_sub32(dest, reg, src);
    gen_op_update_cc_add(dest, src);
}

DISAS_INSN(cmpa)
{
    int opsize;
    int src;
    int reg;
    int dest;

    if (insn & 0x100) {
        opsize = OS_LONG;
    } else {
        opsize = OS_WORD;
    }
    SRC_EA(src, opsize, -1, NULL);
    reg = AREG(insn, 9);
    dest = gen_new_qreg(QMODE_I32);
    gen_op_sub32(dest, reg, src);
    gen_op_update_cc_add(dest, src);
    s->cc_op = CC_OP_SUB;
}

DISAS_INSN(eor)
{
    int src;
    int reg;
    int dest;
    int addr;

    SRC_EA(src, OS_LONG, 0, &addr);
    reg = DREG(insn, 9);
    dest = gen_new_qreg(QMODE_I32);
    gen_op_xor32(dest, src, reg);
    gen_logic_cc(s, dest);
    DEST_EA(insn, OS_LONG, dest, &addr);
}

DISAS_INSN(and)
{
    int src;
    int reg;
    int dest;
    int addr;

    reg = DREG(insn, 9);
    dest = gen_new_qreg(QMODE_I32);
    if (insn & 0x100) {
        SRC_EA(src, OS_LONG, 0, &addr);
        gen_op_and32(dest, src, reg);
        DEST_EA(insn, OS_LONG, dest, &addr);
    } else {
        SRC_EA(src, OS_LONG, 0, NULL);
        gen_op_and32(dest, src, reg);
        gen_op_mov32(reg, dest);
    }
    gen_logic_cc(s, dest);
}

DISAS_INSN(adda)
{
    int src;
    int reg;

    SRC_EA(src, OS_LONG, 0, NULL);
    reg = AREG(insn, 9);
    gen_op_add32(reg, reg, src);
}

DISAS_INSN(addx)
{
    int reg;
    int src;
    int dest;
    int tmp;

    gen_flush_flags(s);
    reg = DREG(insn, 9);
    src = DREG(insn, 0);
    dest = gen_new_qreg(QMODE_I32);
    gen_op_mov32 (dest, reg);
    gen_op_addx_cc(dest, src);
    /* !Z is sticky.  */
    tmp = gen_new_qreg(QMODE_I32);
    gen_op_mov32 (tmp, QREG_CC_DEST);
    gen_op_update_cc_add(dest, src);
    gen_op_mov32(reg, dest);
    s->cc_op = CC_OP_DYNAMIC;
    gen_flush_flags(s);
    gen_op_or32(tmp, tmp, gen_im32(~CCF_Z));
    gen_op_and32(QREG_CC_DEST, QREG_CC_DEST, tmp);
    s->cc_op = CC_OP_FLAGS;
}

DISAS_INSN(shift_im)
{
    int reg;
    int tmp;

    reg = DREG(insn, 0);
    tmp = (insn >> 9) & 7;
    if (tmp == 0)
      tmp = 8;
    if (insn & 0x100) {
        gen_op_shl_im_cc(reg, tmp);
        s->cc_op = CC_OP_SHL;
    } else {
        if (insn & 8) {
            gen_op_shr_im_cc(reg, tmp);
            s->cc_op = CC_OP_SHR;
        } else {
            gen_op_sar_im_cc(reg, tmp);
            s->cc_op = CC_OP_SAR;
        }
    }
}

DISAS_INSN(shift_reg)
{
    int reg;
    int src;
    int tmp;

    reg = DREG(insn, 0);
    src = DREG(insn, 9);
    tmp = gen_new_qreg(QMODE_I32);
    gen_op_and32(tmp, src, gen_im32(63));
    if (insn & 0x100) {
        gen_op_shl_cc(reg, tmp);
        s->cc_op = CC_OP_SHL;
    } else {
        if (insn & 8) {
            gen_op_shr_cc(reg, tmp);
            s->cc_op = CC_OP_SHR;
        } else {
            gen_op_sar_cc(reg, tmp);
            s->cc_op = CC_OP_SAR;
        }
    }
}

DISAS_INSN(ff1)
{
    int reg;
    reg = DREG(insn, 0);
    gen_logic_cc(s, reg);
    gen_op_ff1(reg, reg);
}

static int gen_get_sr(DisasContext *s)
{
    int ccr;
    int sr;

    ccr = gen_get_ccr(s);
    sr = gen_new_qreg(QMODE_I32);
    gen_op_and32(sr, QREG_SR, gen_im32(0xffe0));
    gen_op_or32(sr, sr, ccr);
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
    int reg;
    int sr;

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
    gen_jmp(s, gen_im32(s->pc));
    gen_op_halt();
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
    gen_jmp(s, gen_im32(s->pc));
    gen_op_stop();
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
    int reg;

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
    gen_op_movec(gen_im32(ext & 0xfff), reg);
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
    int src;
    int dest;
    int res;
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
            gen_op_f64_to_i32(res, src);
            break;
        case 1:
            opsize = OS_SINGLE;
            res = gen_new_qreg(QMODE_F32);
            gen_op_f64_to_f32(res, src);
            break;
        case 4:
            opsize = OS_WORD;
            res = gen_new_qreg(QMODE_I32);
            gen_op_f64_to_i32(res, src);
            break;
        case 5:
            opsize = OS_DOUBLE;
            res = src;
            break;
        case 6:
            opsize = OS_BYTE;
            res = gen_new_qreg(QMODE_I32);
            gen_op_f64_to_i32(res, src);
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
        int addr;
        uint16_t mask;
        if ((ext & 0x1f00) != 0x1000 || (ext & 0xff) == 0)
            goto undef;
        src = gen_lea(s, insn, OS_LONG);
        if (src == -1) {
            gen_addr_fault(s);
            return;
        }
        addr = gen_new_qreg(QMODE_I32);
        gen_op_mov32(addr, src);
        mask = 0x80;
        dest = QREG_F0;
        while (mask) {
            if (ext & mask) {
                s->is_mem = 1;
                if (ext & (1 << 13)) {
                    /* store */
                    gen_st(s, f64, addr, dest);
                } else {
                    /* load */
                    gen_ld(s, f64, dest, addr);
                }
                if (ext & (mask - 1))
                    gen_op_add32(addr, addr, gen_im32(8));
            }
            mask >>= 1;
            dest++;
        }
        }
        return;
    }
    if (ext & (1 << 14)) {
        int tmp;

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
        SRC_EA(tmp, opsize, -1, NULL);
        if (opsize == OS_DOUBLE) {
            src = tmp;
        } else {
            src = gen_new_qreg(QMODE_F64);
            switch (opsize) {
            case OS_LONG:
            case OS_WORD:
            case OS_BYTE:
                gen_op_i32_to_f64(src, tmp);
                break;
            case OS_SINGLE:
                gen_op_f32_to_f64(src, tmp);
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
        gen_op_movf64(res, dest);
    round = 1;
    switch (opmode) {
    case 0: case 0x40: case 0x44: /* fmove */
        gen_op_movf64(res, src);
        break;
    case 1: /* fint */
        gen_op_iround_f64(res, src);
        round = 0;
        break;
    case 3: /* fintrz */
        gen_op_itrunc_f64(res, src);
        round = 0;
        break;
    case 4: case 0x41: case 0x45: /* fsqrt */
        gen_op_sqrtf64(res, src);
        break;
    case 0x18: case 0x58: case 0x5c: /* fabs */
        gen_op_absf64(res, src);
        break;
    case 0x1a: case 0x5a: case 0x5e: /* fneg */
        gen_op_chsf64(res, src);
        break;
    case 0x20: case 0x60: case 0x64: /* fdiv */
        gen_op_divf64(res, res, src);
        break;
    case 0x22: case 0x62: case 0x66: /* fadd */
        gen_op_addf64(res, res, src);
        break;
    case 0x23: case 0x63: case 0x67: /* fmul */
        gen_op_mulf64(res, res, src);
        break;
    case 0x28: case 0x68: case 0x6c: /* fsub */
        gen_op_subf64(res, res, src);
        break;
    case 0x38: /* fcmp */
        gen_op_sub_cmpf64(res, res, src);
        dest = 0;
        round = 0;
        break;
    case 0x3a: /* ftst */
        gen_op_movf64(res, src);
        dest = 0;
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
        int tmp;

        tmp = gen_new_qreg(QMODE_F32);
        gen_op_f64_to_f32(tmp, res);
        gen_op_f32_to_f64(res, tmp);
    }
    gen_op_fp_result(res);
    if (dest) {
        gen_op_movf64(dest, res);
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
    int flag;
    int zero;
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
    zero = gen_new_qreg(QMODE_F64);
    gen_op_zerof64(zero);
    gen_op_compare_quietf64(flag, QREG_FP_RESULT, zero);
    /* Jump to l1 if condition is true.  */
    switch (insn & 0xf) {
    case 0: /* f */
        break;
    case 1: /* eq (=0) */
        gen_op_jmp_z32(flag, l1);
        break;
    case 2: /* ogt (=1) */
        gen_op_sub32(flag, flag, gen_im32(1));
        gen_op_jmp_z32(flag, l1);
        break;
    case 3: /* oge (=0 or =1) */
        gen_op_jmp_z32(flag, l1);
        gen_op_sub32(flag, flag, gen_im32(1));
        gen_op_jmp_z32(flag, l1);
        break;
    case 4: /* olt (=-1) */
        gen_op_jmp_s32(flag, l1);
        break;
    case 5: /* ole (=-1 or =0) */
        gen_op_jmp_s32(flag, l1);
        gen_op_jmp_z32(flag, l1);
        break;
    case 6: /* ogl (=-1 or =1) */
        gen_op_jmp_s32(flag, l1);
        gen_op_sub32(flag, flag, gen_im32(1));
        gen_op_jmp_z32(flag, l1);
        break;
    case 7: /* or (=2) */
        gen_op_sub32(flag, flag, gen_im32(2));
        gen_op_jmp_z32(flag, l1);
        break;
    case 8: /* un (<2) */
        gen_op_sub32(flag, flag, gen_im32(2));
        gen_op_jmp_s32(flag, l1);
        break;
    case 9: /* ueq (=0 or =2) */
        gen_op_jmp_z32(flag, l1);
        gen_op_sub32(flag, flag, gen_im32(2));
        gen_op_jmp_z32(flag, l1);
        break;
    case 10: /* ugt (>0) */
        /* ??? Add jmp_gtu.  */
        gen_op_sub32(flag, flag, gen_im32(1));
        gen_op_jmp_ns32(flag, l1);
        break;
    case 11: /* uge (>=0) */
        gen_op_jmp_ns32(flag, l1);
        break;
    case 12: /* ult (=-1 or =2) */
        gen_op_jmp_s32(flag, l1);
        gen_op_sub32(flag, flag, gen_im32(2));
        gen_op_jmp_z32(flag, l1);
        break;
    case 13: /* ule (!=1) */
        gen_op_sub32(flag, flag, gen_im32(1));
        gen_op_jmp_nz32(flag, l1);
        break;
    case 14: /* ne (!=0) */
        gen_op_jmp_nz32(flag, l1);
        break;
    case 15: /* t */
        gen_op_mov32(flag, gen_im32(1));
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

static inline int gen_mac_extract_word(DisasContext *s, int val, int upper)
{
    int tmp = gen_new_qreg(QMODE_I32);
    if (s->env->macsr & MACSR_FI) {
        if (upper)
            gen_op_and32(tmp, val, gen_im32(0xffff0000));
        else
            gen_op_shl32(tmp, val, gen_im32(16));
    } else if (s->env->macsr & MACSR_SU) {
        if (upper)
            gen_op_sar32(tmp, val, gen_im32(16));
        else
            gen_op_ext16s32(tmp, val);
    } else {
        if (upper)
            gen_op_shr32(tmp, val, gen_im32(16));
        else
            gen_op_ext16u32(tmp, val);
    }
    return tmp;
}

DISAS_INSN(mac)
{
    int rx;
    int ry;
    uint16_t ext;
    int acc;
    int l1;
    int tmp;
    int addr;
    int loadval;
    int dual;
    int saved_flags = -1;

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
        gen_op_and32(addr, tmp, QREG_MAC_MASK);
        /* Load the value now to ensure correct exception behavior.
           Perform writeback after reading the MAC inputs.  */
        loadval = gen_load(s, OS_LONG, addr, 0);

        acc ^= 1;
        rx = (ext & 0x8000) ? AREG(ext, 12) : DREG(insn, 12);
        ry = (ext & 8) ? AREG(ext, 0) : DREG(ext, 0);
    } else {
        loadval = addr = -1;
        rx = (insn & 0x40) ? AREG(insn, 9) : DREG(insn, 9);
        ry = (insn & 8) ? AREG(insn, 0) : DREG(insn, 0);
    }

    gen_op_mac_clear_flags();
    l1 = -1;
    if ((s->env->macsr & MACSR_OMC) != 0 && !dual) {
        /* Skip the multiply if we know we will ignore it.  */
        l1 = gen_new_label();
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_and32(tmp, QREG_MACSR, gen_im32(1 << (acc + 8)));
        gen_op_jmp_nz32(tmp, l1);
    }

    if ((ext & 0x0800) == 0) {
        /* Word.  */
        rx = gen_mac_extract_word(s, rx, (ext & 0x80) != 0);
        ry = gen_mac_extract_word(s, ry, (ext & 0x40) != 0);
    }
    if (s->env->macsr & MACSR_FI) {
        gen_op_macmulf(rx, ry);
    } else {
        if (s->env->macsr & MACSR_SU)
            gen_op_macmuls(rx, ry);
        else
            gen_op_macmulu(rx, ry);
        switch ((ext >> 9) & 3) {
        case 1:
            gen_op_macshl();
            break;
        case 3:
            gen_op_macshr();
            break;
        }
    }

    if (dual) {
        /* Save the overflow flag from the multiply.  */
        saved_flags = gen_new_qreg(QMODE_I32);
        gen_op_mov32(saved_flags, QREG_MACSR);
    }

    if ((s->env->macsr & MACSR_OMC) != 0 && dual) {
        /* Skip the accumulate if the value is already saturated.  */
        l1 = gen_new_label();
        tmp = gen_new_qreg(QMODE_I32);
        gen_op_and32(tmp, QREG_MACSR, gen_im32(MACSR_PAV0 << acc));
        gen_op_jmp_nz32(tmp, l1);
    }

    if (insn & 0x100)
        gen_op_macsub(acc);
    else
        gen_op_macadd(acc);

    if (s->env->macsr & MACSR_FI)
        gen_op_macsatf(acc);
    else if (s->env->macsr & MACSR_SU)
        gen_op_macsats(acc);
    else
        gen_op_macsatu(acc);

    if (l1 != -1)
        gen_set_label(l1);

    if (dual) {
        /* Dual accumulate variant.  */
        acc = (ext >> 2) & 3;
        /* Restore the overflow flag from the multiplier.  */
        gen_op_mov32(QREG_MACSR, saved_flags);
        if ((s->env->macsr & MACSR_OMC) != 0) {
            /* Skip the accumulate if the value is already saturated.  */
            l1 = gen_new_label();
            tmp = gen_new_qreg(QMODE_I32);
            gen_op_and32(tmp, QREG_MACSR, gen_im32(MACSR_PAV0 << acc));
            gen_op_jmp_nz32(tmp, l1);
        }
        if (ext & 2)
            gen_op_macsub(acc);
        else
            gen_op_macadd(acc);
        if (s->env->macsr & MACSR_FI)
            gen_op_macsatf(acc);
        else if (s->env->macsr & MACSR_SU)
            gen_op_macsats(acc);
        else
            gen_op_macsatu(acc);
        if (l1 != -1)
            gen_set_label(l1);
    }
    gen_op_mac_set_flags(acc);

    if (insn & 0x30) {
        int rw;
        rw = (insn & 0x40) ? AREG(insn, 9) : DREG(insn, 9);
        gen_op_mov32(rw, loadval);
        /* FIXME: Should address writeback happen with the masked or
           unmasked value?  */
        switch ((insn >> 3) & 7) {
        case 3: /* Post-increment.  */
            gen_op_add32(AREG(insn, 0), addr, gen_im32(4));
            break;
        case 4: /* Pre-decrement.  */
            gen_op_mov32(AREG(insn, 0), addr);
        }
    }
}

DISAS_INSN(from_mac)
{
    int rx;
    int acc;

    rx = (insn & 8) ? AREG(insn, 0) : DREG(insn, 0);
    acc = (insn >> 9) & 3;
    if (s->env->macsr & MACSR_FI) {
        gen_op_get_macf(rx, acc);
    } else if ((s->env->macsr & MACSR_OMC) == 0) {
        gen_op_get_maci(rx, acc);
    } else if (s->env->macsr & MACSR_SU) {
        gen_op_get_macs(rx, acc);
    } else {
        gen_op_get_macu(rx, acc);
    }
    if (insn & 0x40)
        gen_op_clear_mac(acc);
}

DISAS_INSN(move_mac)
{
    int src;
    int dest;
    src = insn & 3;
    dest = (insn >> 9) & 3;
    gen_op_move_mac(dest, src);
    gen_op_mac_clear_flags();
    gen_op_mac_set_flags(dest);
}

DISAS_INSN(from_macsr)
{
    int reg;

    reg = (insn & 8) ? AREG(insn, 0) : DREG(insn, 0);
    gen_op_mov32(reg, QREG_MACSR);
}

DISAS_INSN(from_mask)
{
    int reg;
    reg = (insn & 8) ? AREG(insn, 0) : DREG(insn, 0);
    gen_op_mov32(reg, QREG_MAC_MASK);
}

DISAS_INSN(from_mext)
{
    int reg;
    int acc;
    reg = (insn & 8) ? AREG(insn, 0) : DREG(insn, 0);
    acc = (insn & 0x400) ? 2 : 0;
    if (s->env->macsr & MACSR_FI)
        gen_op_get_mac_extf(reg, acc);
    else
        gen_op_get_mac_exti(reg, acc);
}

DISAS_INSN(macsr_to_ccr)
{
    gen_op_mov32(QREG_CC_X, gen_im32(0));
    gen_op_and32(QREG_CC_DEST, QREG_MACSR, gen_im32(0xf));
    s->cc_op = CC_OP_FLAGS;
}

DISAS_INSN(to_mac)
{
    int acc;
    int val;
    acc = (insn >>9) & 3;
    SRC_EA(val, OS_LONG, 0, NULL);
    if (s->env->macsr & MACSR_FI) {
        gen_op_set_macf(val, acc);
    } else if (s->env->macsr & MACSR_SU) {
        gen_op_set_macs(val, acc);
    } else {
        gen_op_set_macu(val, acc);
    }
    gen_op_mac_clear_flags();
    gen_op_mac_set_flags(acc);
}

DISAS_INSN(to_macsr)
{
    int val;
    SRC_EA(val, OS_LONG, 0, NULL);
    gen_op_set_macsr(val);
    gen_lookup_tb(s);
}

DISAS_INSN(to_mask)
{
    int val;
    SRC_EA(val, OS_LONG, 0, NULL);
    gen_op_or32(QREG_MAC_MASK, val, gen_im32(0xffff0000));
}

DISAS_INSN(to_mext)
{
    int val;
    int acc;
    SRC_EA(val, OS_LONG, 0, NULL);
    acc = (insn & 0x400) ? 2 : 0;
    if (s->env->macsr & MACSR_FI)
        gen_op_set_mac_extf(val, acc);
    else if (s->env->macsr & MACSR_SU)
        gen_op_set_mac_exts(val, acc);
    else
        gen_op_set_mac_extu(val, acc);
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

#if 0
/* Save the result of a floating point operation.  */
static void expand_op_fp_result(qOP *qop)
{
    gen_op_movf64(QREG_FP_RESULT, qop->args[0]);
}

/* Dummy op to indicate that the flags have been set.  */
static void expand_op_flags_set(qOP *qop)
{
}

/* Convert the confition codes into CC_OP_FLAGS format.  */
static void expand_op_flush_flags(qOP *qop)
{
    int cc_opreg;

    if (qop->args[0] == CC_OP_DYNAMIC)
        cc_opreg = QREG_CC_OP;
    else
        cc_opreg = gen_im32(qop->args[0]);
    gen_op_helper32(QREG_NULL, cc_opreg, HELPER_flush_flags);
}

/* Set CC_DEST after a logical or direct flag setting operation.  */
static void expand_op_logic_cc(qOP *qop)
{
    gen_op_mov32(QREG_CC_DEST, qop->args[0]);
}

/* Set CC_SRC and CC_DEST after an arithmetic operation.  */
static void expand_op_update_cc_add(qOP *qop)
{
    gen_op_mov32(QREG_CC_DEST, qop->args[0]);
    gen_op_mov32(QREG_CC_SRC, qop->args[1]);
}

/* Update the X flag.  */
static void expand_op_update_xflag(qOP *qop)
{
    int arg0;
    int arg1;

    arg0 = qop->args[0];
    arg1 = qop->args[1];
    if (arg1 == QREG_NULL) {
        /* CC_X = arg0.  */
        gen_op_mov32(QREG_CC_X, arg0);
    } else {
        /* CC_X = arg0 < (unsigned)arg1.  */
        gen_op_set_ltu32(QREG_CC_X, arg0, arg1);
    }
}

/* Set arg0 to the contents of the X flag.  */
static void expand_op_get_xflag(qOP *qop)
{
    gen_op_mov32(qop->args[0], QREG_CC_X);
}

/* Expand a shift by immediate.  The ISA only allows shifts by 1-8, so we
   already know the shift is within range.  */
static inline void expand_shift_im(qOP *qop, int right, int arith)
{
    int val;
    int reg;
    int tmp;
    int im;

    reg = qop->args[0];
    im = qop->args[1];
    tmp = gen_im32(im);
    val = gen_new_qreg(QMODE_I32);
    gen_op_mov32(val, reg);
    gen_op_mov32(QREG_CC_DEST, val);
    gen_op_mov32(QREG_CC_SRC, tmp);
    if (right) {
        if (arith) {
            gen_op_sar32(reg, val, tmp);
        } else {
            gen_op_shr32(reg, val, tmp);
        }
        if (im == 1)
            tmp = QREG_NULL;
        else
            tmp = gen_im32(im - 1);
    } else {
        gen_op_shl32(reg, val, tmp);
        tmp = gen_im32(32 - im);
    }
    if (tmp != QREG_NULL)
        gen_op_shr32(val, val, tmp);
    gen_op_and32(QREG_CC_X, val, gen_im32(1));
}

static void expand_op_shl_im_cc(qOP *qop)
{
    expand_shift_im(qop, 0, 0);
}

static void expand_op_shr_im_cc(qOP *qop)
{
    expand_shift_im(qop, 1, 0);
}

static void expand_op_sar_im_cc(qOP *qop)
{
    expand_shift_im(qop, 1, 1);
}

/* Expand a shift by register.  */
/* ??? This gives incorrect answers for shifts by 0 or >= 32 */
static inline void expand_shift_reg(qOP *qop, int right, int arith)
{
    int val;
    int reg;
    int shift;
    int tmp;

    reg = qop->args[0];
    shift = qop->args[1];
    val = gen_new_qreg(QMODE_I32);
    gen_op_mov32(val, reg);
    gen_op_mov32(QREG_CC_DEST, val);
    gen_op_mov32(QREG_CC_SRC, shift);
    tmp = gen_new_qreg(QMODE_I32);
    if (right) {
        if (arith) {
            gen_op_sar32(reg, val, shift);
        } else {
            gen_op_shr32(reg, val, shift);
        }
        gen_op_sub32(tmp, shift, gen_im32(1));
    } else {
        gen_op_shl32(reg, val, shift);
        gen_op_sub32(tmp, gen_im32(31), shift);
    }
    gen_op_shl32(val, val, tmp);
    gen_op_and32(QREG_CC_X, val, gen_im32(1));
}

static void expand_op_shl_cc(qOP *qop)
{
    expand_shift_reg(qop, 0, 0);
}

static void expand_op_shr_cc(qOP *qop)
{
    expand_shift_reg(qop, 1, 0);
}

static void expand_op_sar_cc(qOP *qop)
{
    expand_shift_reg(qop, 1, 1);
}

/* Set the Z flag to (arg0 & arg1) == 0.  */
static void expand_op_btest(qOP *qop)
{
    int tmp;
    int l1;

    l1 = gen_new_label();
    tmp = gen_new_qreg(QMODE_I32);
    gen_op_and32(tmp, qop->args[0], qop->args[1]);
    gen_op_and32(QREG_CC_DEST, QREG_CC_DEST, gen_im32(~(uint32_t)CCF_Z));
    gen_op_jmp_nz32(tmp, l1);
    gen_op_or32(QREG_CC_DEST, QREG_CC_DEST, gen_im32(CCF_Z));
    gen_op_label(l1);
}

/* arg0 += arg1 + CC_X */
static void expand_op_addx_cc(qOP *qop)
{
    int arg0 = qop->args[0];
    int arg1 = qop->args[1];
    int l1, l2;

    gen_op_add32 (arg0, arg0, arg1);
    l1 = gen_new_label();
    l2 = gen_new_label();
    gen_op_jmp_z32(QREG_CC_X, l1);
    gen_op_add32(arg0, arg0, gen_im32(1));
    gen_op_mov32(QREG_CC_OP, gen_im32(CC_OP_ADDX));
    gen_op_set_leu32(QREG_CC_X, arg0, arg1);
    gen_op_jmp(l2);
    gen_set_label(l1);
    gen_op_mov32(QREG_CC_OP, gen_im32(CC_OP_ADD));
    gen_op_set_ltu32(QREG_CC_X, arg0, arg1);
    gen_set_label(l2);
}

/* arg0 -= arg1 + CC_X */
static void expand_op_subx_cc(qOP *qop)
{
    int arg0 = qop->args[0];
    int arg1 = qop->args[1];
    int l1, l2;

    l1 = gen_new_label();
    l2 = gen_new_label();
    gen_op_jmp_z32(QREG_CC_X, l1);
    gen_op_set_leu32(QREG_CC_X, arg0, arg1);
    gen_op_sub32(arg0, arg0, gen_im32(1));
    gen_op_mov32(QREG_CC_OP, gen_im32(CC_OP_SUBX));
    gen_op_jmp(l2);
    gen_set_label(l1);
    gen_op_set_ltu32(QREG_CC_X, arg0, arg1);
    gen_op_mov32(QREG_CC_OP, gen_im32(CC_OP_SUB));
    gen_set_label(l2);
    gen_op_sub32 (arg0, arg0, arg1);
}

/* Expand target specific ops to generic qops.  */
static void expand_target_qops(void)
{
    qOP *qop;
    qOP *next;
    int c;

    /* Copy the list of qops, expanding target specific ops as we go.  */
    qop = gen_first_qop;
    gen_first_qop = NULL;
    gen_last_qop = NULL;
    for (; qop; qop = next) {
        c = qop->opcode;
        next = qop->next;
        if (c < FIRST_TARGET_OP) {
            qop->prev = gen_last_qop;
            qop->next = NULL;
            if (gen_last_qop)
                gen_last_qop->next = qop;
            else
                gen_first_qop = qop;
            gen_last_qop = qop;
            continue;
        }
        switch (c) {
#define DEF(name, nargs, barrier) \
        case INDEX_op_##name: \
            expand_op_##name(qop); \
            break;
#include "qop-target.def"
#undef DEF
        default:
            cpu_abort(NULL, "Unexpanded target qop");
        }
    }
}

/* ??? Implement this.  */
static void
optimize_flags(void)
{
}
#endif

/* generate intermediate code for basic block 'tb'.  */
static inline int
gen_intermediate_code_internal(CPUState *env, TranslationBlock *tb,
                               int search_pc)
{
    DisasContext dc1, *dc = &dc1;
    uint16_t *gen_opc_end;
    int j, lj;
    target_ulong pc_start;
    int pc_offset;
    int last_cc_op;

    /* generate intermediate code */
    pc_start = tb->pc;

    dc->tb = tb;

    gen_opc_ptr = gen_opc_buf;
    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;
    gen_opparam_ptr = gen_opparam_buf;

    dc->env = env;
    dc->is_jmp = DISAS_NEXT;
    dc->pc = pc_start;
    dc->cc_op = CC_OP_DYNAMIC;
    dc->singlestep_enabled = env->singlestep_enabled;
    dc->fpcr = env->fpcr;
    dc->user = (env->sr & SR_S) == 0;
    dc->is_mem = 0;
    nb_gen_labels = 0;
    lj = -1;
    do {
        free_qreg = 0;
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
        }
        last_cc_op = dc->cc_op;
        dc->insn_pc = dc->pc;
	disas_m68k_insn(env, dc);

        /* Terminate the TB on memory ops if watchpoints are present.  */
        /* FIXME: This should be replacd by the deterministic execution
         * IRQ raising bits.  */
        if (dc->is_mem && env->nb_watchpoints)
            break;
    } while (!dc->is_jmp && gen_opc_ptr < gen_opc_end &&
             !env->singlestep_enabled &&
             (pc_offset) < (TARGET_PAGE_SIZE - 32));

    if (__builtin_expect(env->singlestep_enabled, 0)) {
        /* Make sure the pc is updated, and raise a debug exception.  */
        if (!dc->is_jmp) {
            gen_flush_cc_op(dc);
            gen_op_mov32(QREG_PC, gen_im32((long)dc->pc));
        }
        gen_op_raise_exception(EXCP_DEBUG);
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
            gen_op_mov32(QREG_T0, gen_im32(0));
            gen_op_exit_tb();
            break;
        case DISAS_TB_JUMP:
            /* nothing more to generate */
            break;
        }
    }
    *gen_opc_ptr = INDEX_op_end;

#ifdef DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "----------------\n");
        fprintf(logfile, "IN: %s\n", lookup_symbol(pc_start));
        target_disas(logfile, pc_start, dc->pc - pc_start, 0);
        fprintf(logfile, "\n");
        if (loglevel & (CPU_LOG_TB_OP)) {
            fprintf(logfile, "OP:\n");
            dump_ops(gen_opc_buf, gen_opparam_buf);
            fprintf(logfile, "\n");
        }
    }
#endif
    if (search_pc) {
        j = gen_opc_ptr - gen_opc_buf;
        lj++;
        while (lj <= j)
            gen_opc_instr_start[lj++] = 0;
    } else {
        tb->size = dc->pc - pc_start;
    }

    //optimize_flags();
    //expand_target_qops();
    return 0;
}

int gen_intermediate_code(CPUState *env, TranslationBlock *tb)
{
    return gen_intermediate_code_internal(env, tb, 0);
}

int gen_intermediate_code_pc(CPUState *env, TranslationBlock *tb)
{
    return gen_intermediate_code_internal(env, tb, 1);
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

