/*
 *  MIPS32 emulation for qemu: main translation routines.
 * 
 *  Copyright (c) 2004-2005 Jocelyn Mayer
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"

//#define MIPS_DEBUG_DISAS
//#define MIPS_SINGLE_STEP

#ifdef USE_DIRECT_JUMP
#define TBPARAM(x)
#else
#define TBPARAM(x) (long)(x)
#endif

enum {
#define DEF(s, n, copy_size) INDEX_op_ ## s,
#include "opc.h"
#undef DEF
    NB_OPS,
};

static uint16_t *gen_opc_ptr;
static uint32_t *gen_opparam_ptr;

#include "gen-op.h"

/* MIPS opcodes */
#define EXT_SPECIAL  0x100
#define EXT_SPECIAL2 0x200
#define EXT_REGIMM   0x300
#define EXT_CP0      0x400
#define EXT_CP1      0x500
#define EXT_CP2      0x600
#define EXT_CP3      0x700

enum {
    /* indirect opcode tables */
    OPC_SPECIAL  = 0x00,
    OPC_BREGIMM  = 0x01,
    OPC_CP0      = 0x10,
    OPC_CP1      = 0x11,
    OPC_CP2      = 0x12,
    OPC_CP3      = 0x13,
    OPC_SPECIAL2 = 0x1C,
    /* arithmetic with immediate */
    OPC_ADDI     = 0x08,
    OPC_ADDIU    = 0x09,
    OPC_SLTI     = 0x0A,
    OPC_SLTIU    = 0x0B,
    OPC_ANDI     = 0x0C,
    OPC_ORI      = 0x0D,
    OPC_XORI     = 0x0E,
    OPC_LUI      = 0x0F,
    /* Jump and branches */
    OPC_J        = 0x02,
    OPC_JAL      = 0x03,
    OPC_BEQ      = 0x04,  /* Unconditional if rs = rt = 0 (B) */
    OPC_BEQL     = 0x14,
    OPC_BNE      = 0x05,
    OPC_BNEL     = 0x15,
    OPC_BLEZ     = 0x06,
    OPC_BLEZL    = 0x16,
    OPC_BGTZ     = 0x07,
    OPC_BGTZL    = 0x17,
    OPC_JALX     = 0x1D,  /* MIPS 16 only */
    /* Load and stores */
    OPC_LB       = 0x20,
    OPC_LH       = 0x21,
    OPC_LWL      = 0x22,
    OPC_LW       = 0x23,
    OPC_LBU      = 0x24,
    OPC_LHU      = 0x25,
    OPC_LWR      = 0x26,
    OPC_SB       = 0x28,
    OPC_SH       = 0x29,
    OPC_SWL      = 0x2A,
    OPC_SW       = 0x2B,
    OPC_SWR      = 0x2E,
    OPC_LL       = 0x30,
    OPC_SC       = 0x38,
    /* Floating point load/store */
    OPC_LWC1     = 0x31,
    OPC_LWC2     = 0x32,
    OPC_LDC1     = 0x35,
    OPC_LDC2     = 0x36,
    OPC_SWC1     = 0x39,
    OPC_SWC2     = 0x3A,
    OPC_SDC1     = 0x3D,
    OPC_SDC2     = 0x3E,
    /* Cache and prefetch */
    OPC_CACHE    = 0x2F,
    OPC_PREF     = 0x33,
};

/* MIPS special opcodes */
enum {
    /* Shifts */
    OPC_SLL      = 0x00 | EXT_SPECIAL,
    /* NOP is SLL r0, r0, 0   */
    /* SSNOP is SLL r0, r0, 1 */
    OPC_SRL      = 0x02 | EXT_SPECIAL,
    OPC_SRA      = 0x03 | EXT_SPECIAL,
    OPC_SLLV     = 0x04 | EXT_SPECIAL,
    OPC_SRLV     = 0x06 | EXT_SPECIAL,
    OPC_SRAV     = 0x07 | EXT_SPECIAL,
    /* Multiplication / division */
    OPC_MULT     = 0x18 | EXT_SPECIAL,
    OPC_MULTU    = 0x19 | EXT_SPECIAL,
    OPC_DIV      = 0x1A | EXT_SPECIAL,
    OPC_DIVU     = 0x1B | EXT_SPECIAL,
    /* 2 registers arithmetic / logic */
    OPC_ADD      = 0x20 | EXT_SPECIAL,
    OPC_ADDU     = 0x21 | EXT_SPECIAL,
    OPC_SUB      = 0x22 | EXT_SPECIAL,
    OPC_SUBU     = 0x23 | EXT_SPECIAL,
    OPC_AND      = 0x24 | EXT_SPECIAL,
    OPC_OR       = 0x25 | EXT_SPECIAL,
    OPC_XOR      = 0x26 | EXT_SPECIAL,
    OPC_NOR      = 0x27 | EXT_SPECIAL,
    OPC_SLT      = 0x2A | EXT_SPECIAL,
    OPC_SLTU     = 0x2B | EXT_SPECIAL,
    /* Jumps */
    OPC_JR       = 0x08 | EXT_SPECIAL,
    OPC_JALR     = 0x09 | EXT_SPECIAL,
    /* Traps */
    OPC_TGE      = 0x30 | EXT_SPECIAL,
    OPC_TGEU     = 0x31 | EXT_SPECIAL,
    OPC_TLT      = 0x32 | EXT_SPECIAL,
    OPC_TLTU     = 0x33 | EXT_SPECIAL,
    OPC_TEQ      = 0x34 | EXT_SPECIAL,
    OPC_TNE      = 0x36 | EXT_SPECIAL,
    /* HI / LO registers load & stores */
    OPC_MFHI     = 0x10 | EXT_SPECIAL,
    OPC_MTHI     = 0x11 | EXT_SPECIAL,
    OPC_MFLO     = 0x12 | EXT_SPECIAL,
    OPC_MTLO     = 0x13 | EXT_SPECIAL,
    /* Conditional moves */
    OPC_MOVZ     = 0x0A | EXT_SPECIAL,
    OPC_MOVN     = 0x0B | EXT_SPECIAL,

    OPC_MOVCI    = 0x01 | EXT_SPECIAL,

    /* Special */
    OPC_PMON     = 0x05 | EXT_SPECIAL,
    OPC_SYSCALL  = 0x0C | EXT_SPECIAL,
    OPC_BREAK    = 0x0D | EXT_SPECIAL,
    OPC_SYNC     = 0x0F | EXT_SPECIAL,
};

enum {
    /* Mutiply & xxx operations */
    OPC_MADD     = 0x00 | EXT_SPECIAL2,
    OPC_MADDU    = 0x01 | EXT_SPECIAL2,
    OPC_MUL      = 0x02 | EXT_SPECIAL2,
    OPC_MSUB     = 0x04 | EXT_SPECIAL2,
    OPC_MSUBU    = 0x05 | EXT_SPECIAL2,
    /* Misc */
    OPC_CLZ      = 0x20 | EXT_SPECIAL2,
    OPC_CLO      = 0x21 | EXT_SPECIAL2,
    /* Special */
    OPC_SDBBP    = 0x3F | EXT_SPECIAL2,
};

/* Branch REGIMM */
enum {
    OPC_BLTZ     = 0x00 | EXT_REGIMM,
    OPC_BLTZL    = 0x02 | EXT_REGIMM,
    OPC_BGEZ     = 0x01 | EXT_REGIMM,
    OPC_BGEZL    = 0x03 | EXT_REGIMM,
    OPC_BLTZAL   = 0x10 | EXT_REGIMM,
    OPC_BLTZALL  = 0x12 | EXT_REGIMM,
    OPC_BGEZAL   = 0x11 | EXT_REGIMM,
    OPC_BGEZALL  = 0x13 | EXT_REGIMM,
    OPC_TGEI     = 0x08 | EXT_REGIMM,
    OPC_TGEIU    = 0x09 | EXT_REGIMM,
    OPC_TLTI     = 0x0A | EXT_REGIMM,
    OPC_TLTIU    = 0x0B | EXT_REGIMM,
    OPC_TEQI     = 0x0C | EXT_REGIMM,
    OPC_TNEI     = 0x0E | EXT_REGIMM,
};

enum {
    /* Coprocessor 0 (MMU) */
    OPC_MFC0     = 0x00 | EXT_CP0,
    OPC_MTC0     = 0x04 | EXT_CP0,
    OPC_TLBR     = 0x01 | EXT_CP0,
    OPC_TLBWI    = 0x02 | EXT_CP0,
    OPC_TLBWR    = 0x06 | EXT_CP0,
    OPC_TLBP     = 0x08 | EXT_CP0,
    OPC_ERET     = 0x18 | EXT_CP0,
    OPC_DERET    = 0x1F | EXT_CP0,
    OPC_WAIT     = 0x20 | EXT_CP0,
};

const unsigned char *regnames[] =
    { "r0", "at", "v0", "v1", "a0", "a1", "a2", "a3",
      "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
      "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
      "t8", "t9", "k0", "k1", "gp", "sp", "s8", "ra", };

/* Warning: no function for r0 register (hard wired to zero) */
#define GEN32(func, NAME) \
static GenOpFunc *NAME ## _table [32] = {                                     \
NULL,       NAME ## 1, NAME ## 2, NAME ## 3,                                  \
NAME ## 4,  NAME ## 5, NAME ## 6, NAME ## 7,                                  \
NAME ## 8,  NAME ## 9, NAME ## 10, NAME ## 11,                                \
NAME ## 12, NAME ## 13, NAME ## 14, NAME ## 15,                               \
NAME ## 16, NAME ## 17, NAME ## 18, NAME ## 19,                               \
NAME ## 20, NAME ## 21, NAME ## 22, NAME ## 23,                               \
NAME ## 24, NAME ## 25, NAME ## 26, NAME ## 27,                               \
NAME ## 28, NAME ## 29, NAME ## 30, NAME ## 31,                               \
};                                                                            \
static inline void func(int n)                                                \
{                                                                             \
    NAME ## _table[n]();                                                      \
}

/* General purpose registers moves */
GEN32(gen_op_load_gpr_T0, gen_op_load_gpr_T0_gpr);
GEN32(gen_op_load_gpr_T1, gen_op_load_gpr_T1_gpr);
GEN32(gen_op_load_gpr_T2, gen_op_load_gpr_T2_gpr);

GEN32(gen_op_store_T0_gpr, gen_op_store_T0_gpr_gpr);
GEN32(gen_op_store_T1_gpr, gen_op_store_T1_gpr_gpr);

typedef struct DisasContext {
    struct TranslationBlock *tb;
    target_ulong pc, saved_pc;
    uint32_t opcode;
    /* Routine used to access memory */
    int mem_idx;
    uint32_t hflags, saved_hflags;
    uint32_t CP0_Status;
    int bstate;
    target_ulong btarget;
} DisasContext;

enum {
    BS_NONE     = 0, /* We go out of the TB without reaching a branch or an
                      * exception condition
                      */
    BS_STOP     = 1, /* We want to stop translation for any reason */
    BS_BRANCH   = 2, /* We reached a branch condition     */
    BS_EXCP     = 3, /* We reached an exception condition */
};

#if defined MIPS_DEBUG_DISAS
#define MIPS_DEBUG(fmt, args...)                                              \
do {                                                                          \
    if (loglevel & CPU_LOG_TB_IN_ASM) {                                       \
        fprintf(logfile, "%08x: %08x " fmt "\n",                              \
                ctx->pc, ctx->opcode , ##args);                               \
    }                                                                         \
} while (0)
#else
#define MIPS_DEBUG(fmt, args...) do { } while(0)
#endif

#define MIPS_INVAL(op)                                                        \
do {                                                                          \
    MIPS_DEBUG("Invalid %s %03x %03x %03x", op, ctx->opcode >> 26,            \
               ctx->opcode & 0x3F, ((ctx->opcode >> 16) & 0x1F));             \
} while (0)

#define GEN_LOAD_REG_TN(Tn, Rn)                                               \
do {                                                                          \
    if (Rn == 0) {                                                            \
        glue(gen_op_reset_, Tn)();                                            \
    } else {                                                                  \
        glue(gen_op_load_gpr_, Tn)(Rn);                                       \
    }                                                                         \
} while (0)

#define GEN_LOAD_IMM_TN(Tn, Imm)                                              \
do {                                                                          \
    if (Imm == 0) {                                                           \
        glue(gen_op_reset_, Tn)();                                            \
    } else {                                                                  \
        glue(gen_op_set_, Tn)(Imm);                                           \
    }                                                                         \
} while (0)

#define GEN_STORE_TN_REG(Rn, Tn)                                              \
do {                                                                          \
    if (Rn != 0) {                                                            \
        glue(glue(gen_op_store_, Tn),_gpr)(Rn);                               \
    }                                                                         \
} while (0)

static inline void save_cpu_state (DisasContext *ctx, int do_save_pc)
{
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
            fprintf(logfile, "hflags %08x saved %08x\n",
                    ctx->hflags, ctx->saved_hflags);
    }
#endif
    if (do_save_pc && ctx->pc != ctx->saved_pc) {
        gen_op_save_pc(ctx->pc);
        ctx->saved_pc = ctx->pc;
    }
    if (ctx->hflags != ctx->saved_hflags) {
        gen_op_save_state(ctx->hflags);
        ctx->saved_hflags = ctx->hflags;
        if (ctx->hflags & MIPS_HFLAG_BR) {
            gen_op_save_breg_target();
        } else if (ctx->hflags & MIPS_HFLAG_B) {
            gen_op_save_btarget(ctx->btarget);
        } else if (ctx->hflags & MIPS_HFLAG_BMASK) {
            gen_op_save_bcond();
            gen_op_save_btarget(ctx->btarget);
        }
    }
}

static inline void generate_exception_err (DisasContext *ctx, int excp, int err)
{
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM)
            fprintf(logfile, "%s: raise exception %d\n", __func__, excp);
#endif
    save_cpu_state(ctx, 1);
    if (err == 0)
        gen_op_raise_exception(excp);
    else
        gen_op_raise_exception_err(excp, err);
    ctx->bstate = BS_EXCP;
}

static inline void generate_exception (DisasContext *ctx, int excp)
{
    generate_exception_err (ctx, excp, 0);
}

#if defined(CONFIG_USER_ONLY)
#define op_ldst(name)        gen_op_##name##_raw()
#define OP_LD_TABLE(width)
#define OP_ST_TABLE(width)
#else
#define op_ldst(name)        (*gen_op_##name[ctx->mem_idx])()
#define OP_LD_TABLE(width)                                                    \
static GenOpFunc *gen_op_l##width[] = {                                       \
    &gen_op_l##width##_user,                                                  \
    &gen_op_l##width##_kernel,                                                \
}
#define OP_ST_TABLE(width)                                                    \
static GenOpFunc *gen_op_s##width[] = {                                       \
    &gen_op_s##width##_user,                                                  \
    &gen_op_s##width##_kernel,                                                \
}
#endif

#ifdef TARGET_MIPS64
OP_LD_TABLE(d);
OP_LD_TABLE(dl);
OP_LD_TABLE(dr);
OP_ST_TABLE(d);
OP_ST_TABLE(dl);
OP_ST_TABLE(dr);
#endif
OP_LD_TABLE(w);
OP_LD_TABLE(wl);
OP_LD_TABLE(wr);
OP_ST_TABLE(w);
OP_ST_TABLE(wl);
OP_ST_TABLE(wr);
OP_LD_TABLE(h);
OP_LD_TABLE(hu);
OP_ST_TABLE(h);
OP_LD_TABLE(b);
OP_LD_TABLE(bu);
OP_ST_TABLE(b);
OP_LD_TABLE(l);
OP_ST_TABLE(c);

/* Load and store */
static void gen_ldst (DisasContext *ctx, uint16_t opc, int rt,
                      int base, int16_t offset)
{
    const unsigned char *opn = "unk";

    if (base == 0) {
        GEN_LOAD_IMM_TN(T0, offset);
    } else if (offset == 0) {
        gen_op_load_gpr_T0(base);
    } else {
        gen_op_load_gpr_T0(base);
        gen_op_set_T1(offset);
        gen_op_add();
    }
    /* Don't do NOP if destination is zero: we must perform the actual
     * memory access
     */
    switch (opc) {
#if defined(TARGET_MIPS64)
    case OPC_LD:
#if defined (MIPS_HAS_UNALIGNED_LS)
    case OPC_ULD:
#endif
        op_ldst(ld);
        GEN_STORE_TN_REG(rt, T0);
        opn = "ld";
        break;
    case OPC_SD:
#if defined (MIPS_HAS_UNALIGNED_LS)
    case OPC_USD:
#endif
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(sd);
        opn = "sd";
        break;
    case OPC_LDL:
        op_ldst(ldl);
        GEN_STORE_TN_REG(rt, T0);
        opn = "ldl";
        break;
    case OPC_SDL:
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(sdl);
        opn = "sdl";
        break;
    case OPC_LDR:
        op_ldst(ldr);
        GEN_STORE_TN_REG(rt, T0);
        opn = "ldr";
        break;
    case OPC_SDR:
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(sdr);
        opn = "sdr";
        break;
#endif
    case OPC_LW:
#if defined (MIPS_HAS_UNALIGNED_LS)
    case OPC_ULW:
#endif
        op_ldst(lw);
        GEN_STORE_TN_REG(rt, T0);
        opn = "lw";
        break;
    case OPC_SW:
#if defined (MIPS_HAS_UNALIGNED_LS)
    case OPC_USW:
#endif
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(sw);
        opn = "sw";
        break;
    case OPC_LH:
#if defined (MIPS_HAS_UNALIGNED_LS)
    case OPC_ULH:
#endif
        op_ldst(lh);
        GEN_STORE_TN_REG(rt, T0);
        opn = "lh";
        break;
    case OPC_SH:
#if defined (MIPS_HAS_UNALIGNED_LS)
    case OPC_USH:
#endif
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(sh);
        opn = "sh";
        break;
    case OPC_LHU:
#if defined (MIPS_HAS_UNALIGNED_LS)
    case OPC_ULHU:
#endif
        op_ldst(lhu);
        GEN_STORE_TN_REG(rt, T0);
        opn = "lhu";
        break;
    case OPC_LB:
        op_ldst(lb);
        GEN_STORE_TN_REG(rt, T0);
        opn = "lb";
        break;
    case OPC_SB:
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(sb);
        opn = "sb";
        break;
    case OPC_LBU:
        op_ldst(lbu);
        GEN_STORE_TN_REG(rt, T0);
        opn = "lbu";
        break;
    case OPC_LWL:
	GEN_LOAD_REG_TN(T1, rt);
        op_ldst(lwl);
        GEN_STORE_TN_REG(rt, T0);
        opn = "lwl";
        break;
    case OPC_SWL:
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(swl);
        opn = "swr";
        break;
    case OPC_LWR:
	GEN_LOAD_REG_TN(T1, rt);
        op_ldst(lwr);
        GEN_STORE_TN_REG(rt, T0);
        opn = "lwr";
        break;
    case OPC_SWR:
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(swr);
        opn = "swr";
        break;
    case OPC_LL:
        op_ldst(ll);
        GEN_STORE_TN_REG(rt, T0);
        opn = "ll";
        break;
    case OPC_SC:
        GEN_LOAD_REG_TN(T1, rt);
        op_ldst(sc);
        GEN_STORE_TN_REG(rt, T0);
        opn = "sc";
        break;
    default:
        MIPS_INVAL("load/store");
        generate_exception(ctx, EXCP_RI);
        return;
    }
    MIPS_DEBUG("%s %s, %d(%s)", opn, regnames[rt], offset, regnames[base]);
}

/* Arithmetic with immediate operand */
static void gen_arith_imm (DisasContext *ctx, uint16_t opc, int rt,
                           int rs, int16_t imm)
{
    uint32_t uimm;
    const unsigned char *opn = "unk";

    if (rt == 0 && opc != OPC_ADDI) {
        /* if no destination, treat it as a NOP 
         * For addi, we must generate the overflow exception when needed.
         */
        MIPS_DEBUG("NOP");
        return;
    }
    if (opc == OPC_ADDI || opc == OPC_ADDIU ||
        opc == OPC_SLTI || opc == OPC_SLTIU)
        uimm = (int32_t)imm; /* Sign extent to 32 bits */
    else
        uimm = (uint16_t)imm;
    if (opc != OPC_LUI) {
        GEN_LOAD_REG_TN(T0, rs);
        GEN_LOAD_IMM_TN(T1, uimm);
    } else {
        uimm = uimm << 16;
        GEN_LOAD_IMM_TN(T0, uimm);
    }
    switch (opc) {
    case OPC_ADDI:
        save_cpu_state(ctx, 1);
        gen_op_addo();
        opn = "addi";
        break;
    case OPC_ADDIU:
        gen_op_add();
        opn = "addiu";
        break;
    case OPC_SLTI:
        gen_op_lt();
        opn = "slti";
        break;
    case OPC_SLTIU:
        gen_op_ltu();
        opn = "sltiu";
        break;
    case OPC_ANDI:
        gen_op_and();
        opn = "andi";
        break;
    case OPC_ORI:
        gen_op_or();
        opn = "ori";
        break;
    case OPC_XORI:
        gen_op_xor();
        opn = "xori";
        break;
    case OPC_LUI:
        opn = "lui";
        break;
    case OPC_SLL:
        gen_op_sll();
        opn = "sll";
        break;
    case OPC_SRA:
        gen_op_sra();
        opn = "sra";
        break;
    case OPC_SRL:
        gen_op_srl();
        opn = "srl";
        break;
    default:
        MIPS_INVAL("imm arith");
        generate_exception(ctx, EXCP_RI);
        return;
    }
    GEN_STORE_TN_REG(rt, T0);
    MIPS_DEBUG("%s %s, %s, %x", opn, regnames[rt], regnames[rs], uimm);
}

/* Arithmetic */
static void gen_arith (DisasContext *ctx, uint16_t opc,
                       int rd, int rs, int rt)
{
    const unsigned char *opn = "unk";

    if (rd == 0 && opc != OPC_ADD && opc != OPC_SUB) {
        /* if no destination, treat it as a NOP 
         * For add & sub, we must generate the overflow exception when needed.
         */
        MIPS_DEBUG("NOP");
        return;
    }
    GEN_LOAD_REG_TN(T0, rs);
    GEN_LOAD_REG_TN(T1, rt);
    switch (opc) {
    case OPC_ADD:
        save_cpu_state(ctx, 1);
        gen_op_addo();
        opn = "add";
        break;
    case OPC_ADDU:
        gen_op_add();
        opn = "addu";
        break;
    case OPC_SUB:
        save_cpu_state(ctx, 1);
        gen_op_subo();
        opn = "sub";
        break;
    case OPC_SUBU:
        gen_op_sub();
        opn = "subu";
        break;
    case OPC_SLT:
        gen_op_lt();
        opn = "slt";
        break;
    case OPC_SLTU:
        gen_op_ltu();
        opn = "sltu";
        break;
    case OPC_AND:
        gen_op_and();
        opn = "and";
        break;
    case OPC_NOR:
        gen_op_nor();
        opn = "nor";
        break;
    case OPC_OR:
        gen_op_or();
        opn = "or";
        break;
    case OPC_XOR:
        gen_op_xor();
        opn = "xor";
        break;
    case OPC_MUL:
        gen_op_mul();
        opn = "mul";
        break;
    case OPC_MOVN:
        gen_op_movn(rd);
        opn = "movn";
        goto print;
    case OPC_MOVZ:
        gen_op_movz(rd);
        opn = "movz";
        goto print;
    case OPC_SLLV:
        gen_op_sllv();
        opn = "sllv";
        break;
    case OPC_SRAV:
        gen_op_srav();
        opn = "srav";
        break;
    case OPC_SRLV:
        gen_op_srlv();
        opn = "srlv";
        break;
    default:
        MIPS_INVAL("arith");
        generate_exception(ctx, EXCP_RI);
        return;
    }
    GEN_STORE_TN_REG(rd, T0);
 print:
    MIPS_DEBUG("%s %s, %s, %s", opn, regnames[rd], regnames[rs], regnames[rt]);
}

/* Arithmetic on HI/LO registers */
static void gen_HILO (DisasContext *ctx, uint16_t opc, int reg)
{
    const unsigned char *opn = "unk";

    if (reg == 0 && (opc == OPC_MFHI || opc == OPC_MFLO)) {
        /* Treat as a NOP */
        MIPS_DEBUG("NOP");
        return;
    }
    switch (opc) {
    case OPC_MFHI:
        gen_op_load_HI();
        GEN_STORE_TN_REG(reg, T0);
        opn = "mfhi";
        break;
    case OPC_MFLO:
        gen_op_load_LO();
        GEN_STORE_TN_REG(reg, T0);
        opn = "mflo";
        break;
    case OPC_MTHI:
        GEN_LOAD_REG_TN(T0, reg);
        gen_op_store_HI();
        opn = "mthi";
        break;
    case OPC_MTLO:
        GEN_LOAD_REG_TN(T0, reg);
        gen_op_store_LO();
        opn = "mtlo";
        break;
    default:
        MIPS_INVAL("HILO");
        generate_exception(ctx, EXCP_RI);
        return;
    }
    MIPS_DEBUG("%s %s", opn, regnames[reg]);
}

static void gen_muldiv (DisasContext *ctx, uint16_t opc,
                        int rs, int rt)
{
    const unsigned char *opn = "unk";

    GEN_LOAD_REG_TN(T0, rs);
    GEN_LOAD_REG_TN(T1, rt);
    switch (opc) {
    case OPC_DIV:
        gen_op_div();
        opn = "div";
        break;
    case OPC_DIVU:
        gen_op_divu();
        opn = "divu";
        break;
    case OPC_MULT:
        gen_op_mult();
        opn = "mult";
        break;
    case OPC_MULTU:
        gen_op_multu();
        opn = "multu";
        break;
    case OPC_MADD:
        gen_op_madd();
        opn = "madd";
        break;
    case OPC_MADDU:
        gen_op_maddu();
        opn = "maddu";
        break;
    case OPC_MSUB:
        gen_op_msub();
        opn = "msub";
        break;
    case OPC_MSUBU:
        gen_op_msubu();
        opn = "msubu";
        break;
    default:
        MIPS_INVAL("mul/div");
        generate_exception(ctx, EXCP_RI);
        return;
    }
    MIPS_DEBUG("%s %s %s", opn, regnames[rs], regnames[rt]);
}

static void gen_cl (DisasContext *ctx, uint16_t opc,
                    int rd, int rs)
{
    const unsigned char *opn = "unk";
    if (rd == 0) {
        /* Treat as a NOP */
        MIPS_DEBUG("NOP");
        return;
    }
    GEN_LOAD_REG_TN(T0, rs);
    switch (opc) {
    case OPC_CLO:
        /* CLO */
        gen_op_clo();
        opn = "clo";
        break;
    case OPC_CLZ:
        /* CLZ */
        gen_op_clz();
        opn = "clz";
        break;
    default:
        MIPS_INVAL("CLx");
        generate_exception(ctx, EXCP_RI);
        return;
    }
    gen_op_store_T0_gpr(rd);
    MIPS_DEBUG("%s %s, %s", opn, regnames[rd], regnames[rs]);
}

/* Traps */
static void gen_trap (DisasContext *ctx, uint16_t opc,
                      int rs, int rt, int16_t imm)
{
    int cond;

    cond = 0;
    /* Load needed operands */
    switch (opc) {
    case OPC_TEQ:
    case OPC_TGE:
    case OPC_TGEU:
    case OPC_TLT:
    case OPC_TLTU:
    case OPC_TNE:
        /* Compare two registers */
        if (rs != rt) {
            GEN_LOAD_REG_TN(T0, rs);
            GEN_LOAD_REG_TN(T1, rt);
            cond = 1;
        }
    case OPC_TEQI:
    case OPC_TGEI:
    case OPC_TGEIU:
    case OPC_TLTI:
    case OPC_TLTIU:
    case OPC_TNEI:
        /* Compare register to immediate */
        if (rs != 0 || imm != 0) {
            GEN_LOAD_REG_TN(T0, rs);
            GEN_LOAD_IMM_TN(T1, (int32_t)imm);
            cond = 1;
        }
        break;
    }
    if (cond == 0) {
        switch (opc) {
        case OPC_TEQ:   /* rs == rs */
        case OPC_TEQI:  /* r0 == 0  */
        case OPC_TGE:   /* rs >= rs */
        case OPC_TGEI:  /* r0 >= 0  */
        case OPC_TGEU:  /* rs >= rs unsigned */
        case OPC_TGEIU: /* r0 >= 0  unsigned */
            /* Always trap */
            gen_op_set_T0(1);
            break;
        case OPC_TLT:   /* rs < rs           */
        case OPC_TLTI:  /* r0 < 0            */
        case OPC_TLTU:  /* rs < rs unsigned  */
        case OPC_TLTIU: /* r0 < 0  unsigned  */
        case OPC_TNE:   /* rs != rs          */
        case OPC_TNEI:  /* r0 != 0           */
            /* Never trap: treat as NOP */
            return;
        default:
            MIPS_INVAL("TRAP");
            generate_exception(ctx, EXCP_RI);
            return;
        }
    } else {
        switch (opc) {
        case OPC_TEQ:
        case OPC_TEQI:
            gen_op_eq();
            break;
        case OPC_TGE:
        case OPC_TGEI:
            gen_op_ge();
            break;
        case OPC_TGEU:
        case OPC_TGEIU:
            gen_op_geu();
            break;
        case OPC_TLT:
        case OPC_TLTI:
            gen_op_lt();
            break;
        case OPC_TLTU:
        case OPC_TLTIU:
            gen_op_ltu();
            break;
        case OPC_TNE:
        case OPC_TNEI:
            gen_op_ne();
            break;
        default:
            MIPS_INVAL("TRAP");
            generate_exception(ctx, EXCP_RI);
            return;
        }
    }
    save_cpu_state(ctx, 1);
    gen_op_trap();
    ctx->bstate = BS_STOP;
}

static inline void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    TranslationBlock *tb;
    tb = ctx->tb;
    if ((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK)) {
        if (n == 0)
            gen_op_goto_tb0(TBPARAM(tb));
        else
            gen_op_goto_tb1(TBPARAM(tb));
        gen_op_save_pc(dest);
        gen_op_set_T0((long)tb + n);
        gen_op_exit_tb();
    } else {
        gen_op_save_pc(dest);
        gen_op_set_T0(0);
        gen_op_exit_tb();
    }
}

/* Branches (before delay slot) */
static void gen_compute_branch (DisasContext *ctx, uint16_t opc,
                                int rs, int rt, int32_t offset)
{
    target_ulong btarget;
    int blink, bcond;

    btarget = -1;
    blink = 0;
    bcond = 0;
    /* Load needed operands */
    switch (opc) {
    case OPC_BEQ:
    case OPC_BEQL:
    case OPC_BNE:
    case OPC_BNEL:
        /* Compare two registers */
        if (rs != rt) {
            GEN_LOAD_REG_TN(T0, rs);
            GEN_LOAD_REG_TN(T1, rt);
            bcond = 1;
        }
        btarget = ctx->pc + 4 + offset;
        break;
    case OPC_BGEZ:
    case OPC_BGEZAL:
    case OPC_BGEZALL:
    case OPC_BGEZL:
    case OPC_BGTZ:
    case OPC_BGTZL:
    case OPC_BLEZ:
    case OPC_BLEZL:
    case OPC_BLTZ:
    case OPC_BLTZAL:
    case OPC_BLTZALL:
    case OPC_BLTZL:
        /* Compare to zero */
        if (rs != 0) {
            gen_op_load_gpr_T0(rs);
            bcond = 1;
        }
        btarget = ctx->pc + 4 + offset;
        break;
    case OPC_J:
    case OPC_JAL:
        /* Jump to immediate */
        btarget = ((ctx->pc + 4) & 0xF0000000) | offset;
        break;
    case OPC_JR:
    case OPC_JALR:
        /* Jump to register */
        if (offset != 0) {
            /* Only hint = 0 is valid */
            generate_exception(ctx, EXCP_RI);
            return;
        }
        GEN_LOAD_REG_TN(T2, rs);
        break;
    default:
        MIPS_INVAL("branch/jump");
        generate_exception(ctx, EXCP_RI);
        return;
    }
    if (bcond == 0) {
        /* No condition to be computed */
        switch (opc) {
        case OPC_BEQ:     /* rx == rx        */
        case OPC_BEQL:    /* rx == rx likely */
        case OPC_BGEZ:    /* 0 >= 0          */
        case OPC_BGEZL:   /* 0 >= 0 likely   */
        case OPC_BLEZ:    /* 0 <= 0          */
        case OPC_BLEZL:   /* 0 <= 0 likely   */
            /* Always take */
            ctx->hflags |= MIPS_HFLAG_B;
            MIPS_DEBUG("balways");
            break;
        case OPC_BGEZAL:  /* 0 >= 0          */
        case OPC_BGEZALL: /* 0 >= 0 likely   */
            /* Always take and link */
            blink = 31;
            ctx->hflags |= MIPS_HFLAG_B;
            MIPS_DEBUG("balways and link");
            break;
        case OPC_BNE:     /* rx != rx        */
        case OPC_BGTZ:    /* 0 > 0           */
        case OPC_BLTZ:    /* 0 < 0           */
            /* Treated as NOP */
            MIPS_DEBUG("bnever (NOP)");
            return;
        case OPC_BLTZAL:  /* 0 < 0           */
            gen_op_set_T0(ctx->pc + 8);
            gen_op_store_T0_gpr(31);
            return;
        case OPC_BLTZALL: /* 0 < 0 likely */
            gen_op_set_T0(ctx->pc + 8);
            gen_op_store_T0_gpr(31);
            gen_goto_tb(ctx, 0, ctx->pc + 4);
            return;
        case OPC_BNEL:    /* rx != rx likely */
        case OPC_BGTZL:   /* 0 > 0 likely */
        case OPC_BLTZL:   /* 0 < 0 likely */
            /* Skip the instruction in the delay slot */
            MIPS_DEBUG("bnever and skip");
            gen_goto_tb(ctx, 0, ctx->pc + 4);
            return;
        case OPC_J:
            ctx->hflags |= MIPS_HFLAG_B;
            MIPS_DEBUG("j %08x", btarget);
            break;
        case OPC_JAL:
            blink = 31;
            ctx->hflags |= MIPS_HFLAG_B;
            MIPS_DEBUG("jal %08x", btarget);
            break;
        case OPC_JR:
            ctx->hflags |= MIPS_HFLAG_BR;
            MIPS_DEBUG("jr %s", regnames[rs]);
            break;
        case OPC_JALR:
            blink = rt;
            ctx->hflags |= MIPS_HFLAG_BR;
            MIPS_DEBUG("jalr %s, %s", regnames[rt], regnames[rs]);
            break;
        default:
            MIPS_INVAL("branch/jump");
            generate_exception(ctx, EXCP_RI);
            return;
        }
    } else {
        switch (opc) {
        case OPC_BEQ:
            gen_op_eq();
            MIPS_DEBUG("beq %s, %s, %08x",
                       regnames[rs], regnames[rt], btarget);
            goto not_likely;
        case OPC_BEQL:
            gen_op_eq();
            MIPS_DEBUG("beql %s, %s, %08x",
                       regnames[rs], regnames[rt], btarget);
            goto likely;
        case OPC_BNE:
            gen_op_ne();
            MIPS_DEBUG("bne %s, %s, %08x",
                       regnames[rs], regnames[rt], btarget);
            goto not_likely;
        case OPC_BNEL:
            gen_op_ne();
            MIPS_DEBUG("bnel %s, %s, %08x",
                       regnames[rs], regnames[rt], btarget);
            goto likely;
        case OPC_BGEZ:
            gen_op_gez();
            MIPS_DEBUG("bgez %s, %08x", regnames[rs], btarget);
            goto not_likely;
        case OPC_BGEZL:
            gen_op_gez();
            MIPS_DEBUG("bgezl %s, %08x", regnames[rs], btarget);
            goto likely;
        case OPC_BGEZAL:
            gen_op_gez();
            MIPS_DEBUG("bgezal %s, %08x", regnames[rs], btarget);
            blink = 31;
            goto not_likely;
        case OPC_BGEZALL:
            gen_op_gez();
            blink = 31;
            MIPS_DEBUG("bgezall %s, %08x", regnames[rs], btarget);
            goto likely;
        case OPC_BGTZ:
            gen_op_gtz();
            MIPS_DEBUG("bgtz %s, %08x", regnames[rs], btarget);
            goto not_likely;
        case OPC_BGTZL:
            gen_op_gtz();
            MIPS_DEBUG("bgtzl %s, %08x", regnames[rs], btarget);
            goto likely;
        case OPC_BLEZ:
            gen_op_lez();
            MIPS_DEBUG("blez %s, %08x", regnames[rs], btarget);
            goto not_likely;
        case OPC_BLEZL:
            gen_op_lez();
            MIPS_DEBUG("blezl %s, %08x", regnames[rs], btarget);
            goto likely;
        case OPC_BLTZ:
            gen_op_ltz();
            MIPS_DEBUG("bltz %s, %08x", regnames[rs], btarget);
            goto not_likely;
        case OPC_BLTZL:
            gen_op_ltz();
            MIPS_DEBUG("bltzl %s, %08x", regnames[rs], btarget);
            goto likely;
        case OPC_BLTZAL:
            gen_op_ltz();
            blink = 31;
            MIPS_DEBUG("bltzal %s, %08x", regnames[rs], btarget);
        not_likely:
            ctx->hflags |= MIPS_HFLAG_BC;
            break;
        case OPC_BLTZALL:
            gen_op_ltz();
            blink = 31;
            MIPS_DEBUG("bltzall %s, %08x", regnames[rs], btarget);
        likely:
            ctx->hflags |= MIPS_HFLAG_BL;
            break;
        }
        gen_op_set_bcond();
    }
    MIPS_DEBUG("enter ds: link %d cond %02x target %08x",
               blink, ctx->hflags, btarget);
    ctx->btarget = btarget;
    if (blink > 0) {
        gen_op_set_T0(ctx->pc + 8);
        gen_op_store_T0_gpr(blink);
    }
    return;
}

/* CP0 (MMU and control) */
static void gen_cp0 (DisasContext *ctx, uint16_t opc, int rt, int rd)
{
    const unsigned char *opn = "unk";

    if (!(ctx->CP0_Status & (1 << CP0St_CU0)) &&
        (ctx->hflags & MIPS_HFLAG_UM) &&
        !(ctx->hflags & MIPS_HFLAG_ERL) &&
        !(ctx->hflags & MIPS_HFLAG_EXL)) {
        if (loglevel & CPU_LOG_TB_IN_ASM) {
            fprintf(logfile, "CP0 is not usable\n");
        }
        generate_exception_err (ctx, EXCP_CpU, 0);
        return;
    }
    switch (opc) {
    case OPC_MFC0:
        if (rt == 0) {
            /* Treat as NOP */
            return;
        }
        gen_op_mfc0(rd, ctx->opcode & 0x7);
        gen_op_store_T0_gpr(rt);
        opn = "mfc0";
        break;
    case OPC_MTC0:
        /* If we get an exception, we want to restart at next instruction */
        ctx->pc += 4;
        save_cpu_state(ctx, 1);
        ctx->pc -= 4;
        GEN_LOAD_REG_TN(T0, rt);
        gen_op_mtc0(rd, ctx->opcode & 0x7);
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        opn = "mtc0";
        break;
#if defined(MIPS_USES_R4K_TLB)
    case OPC_TLBWI:
        gen_op_tlbwi();
        opn = "tlbwi";
        break;
    case OPC_TLBWR:
        gen_op_tlbwr();
        opn = "tlbwr";
        break;
    case OPC_TLBP:
        gen_op_tlbp();
        opn = "tlbp";
        break;
    case OPC_TLBR:
        gen_op_tlbr();
        opn = "tlbr";
        break;
#endif
    case OPC_ERET:
        opn = "eret";
        save_cpu_state(ctx, 0);
        gen_op_eret();
        ctx->bstate = BS_EXCP;
        break;
    case OPC_DERET:
        opn = "deret";
        if (!(ctx->hflags & MIPS_HFLAG_DM)) {
            generate_exception(ctx, EXCP_RI);
        } else {
            save_cpu_state(ctx, 0);
            gen_op_deret();
            ctx->bstate = BS_EXCP;
        }
        break;
    case OPC_WAIT:
        opn = "wait";
        /* If we get an exception, we want to restart at next instruction */
        ctx->pc += 4;
        save_cpu_state(ctx, 1);
        ctx->pc -= 4;
        gen_op_wait();
        ctx->bstate = BS_EXCP;
        break;
    default:
        if (loglevel & CPU_LOG_TB_IN_ASM) {
            fprintf(logfile, "Invalid CP0 opcode: %08x %03x %03x %03x\n",
                    ctx->opcode, ctx->opcode >> 26, ctx->opcode & 0x3F,
                    ((ctx->opcode >> 16) & 0x1F));
        }
        generate_exception(ctx, EXCP_RI);
        return;
    }
    MIPS_DEBUG("%s %s %d", opn, regnames[rt], rd);
}

/* Coprocessor 1 (FPU) */

/* ISA extensions */
/* MIPS16 extension to MIPS32 */
/* SmartMIPS extension to MIPS32 */

#ifdef TARGET_MIPS64
static void gen_arith64 (DisasContext *ctx, uint16_t opc)
{
    if (func == 0x02 && rd == 0) {
        /* NOP */
        return;
    }
    if (rs == 0 || rt == 0) {
        gen_op_reset_T0();
        gen_op_save64();
    } else {
        gen_op_load_gpr_T0(rs);
        gen_op_load_gpr_T1(rt);
        gen_op_save64();
        if (func & 0x01)
            gen_op_mul64u();
        else
            gen_op_mul64s();
    }
    if (func & 0x02)
        gen_op_add64();
    else
        gen_op_sub64();
}

/* Coprocessor 3 (FPU) */

/* MDMX extension to MIPS64 */
/* MIPS-3D extension to MIPS64 */

#endif

static void gen_blikely(DisasContext *ctx)
{
    int l1;
    l1 = gen_new_label();
    gen_op_jnz_T2(l1);
    gen_op_save_state(ctx->hflags & ~MIPS_HFLAG_BMASK);
    gen_goto_tb(ctx, 1, ctx->pc + 4);
    gen_set_label(l1);
}

static void decode_opc (DisasContext *ctx)
{
    int32_t offset;
    int rs, rt, rd, sa;
    uint16_t op, op1;
    int16_t imm;

    if ((ctx->hflags & MIPS_HFLAG_BMASK) == MIPS_HFLAG_BL) {
        /* Handle blikely not taken case */
        MIPS_DEBUG("blikely condition (%08x)", ctx->pc + 4);
        gen_blikely(ctx);
    }
    op = ctx->opcode >> 26;
    rs = ((ctx->opcode >> 21) & 0x1F);
    rt = ((ctx->opcode >> 16) & 0x1F);
    rd = ((ctx->opcode >> 11) & 0x1F);
    sa = ((ctx->opcode >> 6) & 0x1F);
    imm = (int16_t)ctx->opcode;
    switch (op) {
    case 0x00:          /* Special opcode */
        op1 = ctx->opcode & 0x3F;
        switch (op1) {
        case 0x00:          /* Arithmetic with immediate */
        case 0x02 ... 0x03:
            gen_arith_imm(ctx, op1 | EXT_SPECIAL, rd, rt, sa);
            break;
        case 0x04:          /* Arithmetic */
        case 0x06 ... 0x07:
        case 0x0A ... 0x0B:
        case 0x20 ... 0x27:
        case 0x2A ... 0x2B:
            gen_arith(ctx, op1 | EXT_SPECIAL, rd, rs, rt);
            break;
        case 0x18 ... 0x1B: /* MULT / DIV */
            gen_muldiv(ctx, op1 | EXT_SPECIAL, rs, rt);
            break;
        case 0x08 ... 0x09: /* Jumps */
            gen_compute_branch(ctx, op1 | EXT_SPECIAL, rs, rd, sa);
            return;
        case 0x30 ... 0x34: /* Traps */
        case 0x36:
            gen_trap(ctx, op1 | EXT_SPECIAL, rs, rt, -1);
            break;
        case 0x10:          /* Move from HI/LO */
        case 0x12:
            gen_HILO(ctx, op1 | EXT_SPECIAL, rd);
            break;
        case 0x11:
        case 0x13:          /* Move to HI/LO */
            gen_HILO(ctx, op1 | EXT_SPECIAL, rs);
            break;
        case 0x0C:          /* SYSCALL */
            generate_exception(ctx, EXCP_SYSCALL);
            break;
        case 0x0D:          /* BREAK */
            generate_exception(ctx, EXCP_BREAK);
            break;
        case 0x0F:          /* SYNC */
            /* Treat as a noop */
            break;
        case 0x05:          /* Pmon entry point */
            gen_op_pmon((ctx->opcode >> 6) & 0x1F);
            break;

        case 0x01:          /* MOVCI */
#if defined (MIPS_HAS_MOVCI)
            /* XXX */
#else
            /* Not implemented */
            generate_exception_err (ctx, EXCP_CpU, 1);
#endif
            break;

#if defined (TARGET_MIPS64)
        case 0x14: /* MIPS64 specific opcodes */
        case 0x16:
        case 0x17:
        case 0x1C ... 0x1F:
        case 0x2C ... 0x2F:
        case 0x37:
        case 0x39 ... 0x3B:
        case 0x3E ... 0x3F:
#endif
        default:            /* Invalid */
            MIPS_INVAL("special");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
    case 0x1C:          /* Special2 opcode */
        op1 = ctx->opcode & 0x3F;
        switch (op1) {
#if defined (MIPS_USES_R4K_EXT)
        /* Those instructions are not part of MIPS32 core */
        case 0x00 ... 0x01: /* Multiply and add/sub */
        case 0x04 ... 0x05:
            gen_muldiv(ctx, op1 | EXT_SPECIAL2, rs, rt);
            break;
        case 0x02:          /* MUL */
            gen_arith(ctx, op1 | EXT_SPECIAL2, rd, rs, rt);
            break;
        case 0x20 ... 0x21: /* CLO / CLZ */
            gen_cl(ctx, op1 | EXT_SPECIAL2, rd, rs);
            break;
#endif
        case 0x3F:          /* SDBBP */
            /* XXX: not clear which exception should be raised
             *      when in debug mode...
             */
            if (!(ctx->hflags & MIPS_HFLAG_DM)) {
                generate_exception(ctx, EXCP_DBp);
            } else {
                generate_exception(ctx, EXCP_DBp);
            }
            /* Treat as a noop */
            break;
        default:            /* Invalid */
            MIPS_INVAL("special2");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
    case 0x01:          /* B REGIMM opcode */
        op1 = ((ctx->opcode >> 16) & 0x1F);
        switch (op1) {
        case 0x00 ... 0x03: /* REGIMM branches */
        case 0x10 ... 0x13:
            gen_compute_branch(ctx, op1 | EXT_REGIMM, rs, -1, imm << 2);
            return;
        case 0x08 ... 0x0C: /* Traps */
        case 0x0E:
            gen_trap(ctx, op1 | EXT_REGIMM, rs, -1, imm);
            break;
        default:            /* Invalid */
            MIPS_INVAL("REGIMM");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
    case 0x10:          /* CP0 opcode */
        op1 = ((ctx->opcode >> 21) & 0x1F);
        switch (op1) {
        case 0x00:
        case 0x04:
            gen_cp0(ctx, op1 | EXT_CP0, rt, rd);
            break;
        default:
            gen_cp0(ctx, (ctx->opcode & 0x3F) | EXT_CP0, rt, rd);
            break;
        }
        break;
    case 0x08 ... 0x0F: /* Arithmetic with immediate opcode */
        gen_arith_imm(ctx, op, rt, rs, imm);
        break;
    case 0x02 ... 0x03: /* Jump */
        offset = (int32_t)(ctx->opcode & 0x03FFFFFF) << 2;
        gen_compute_branch(ctx, op, rs, rt, offset);
        return;
    case 0x04 ... 0x07: /* Branch */
    case 0x14 ... 0x17:
        gen_compute_branch(ctx, op, rs, rt, imm << 2);
        return;
    case 0x20 ... 0x26: /* Load and stores */
    case 0x28 ... 0x2E:
    case 0x30:
    case 0x38:
        gen_ldst(ctx, op, rt, rs, imm);
        break;
    case 0x2F:          /* Cache operation */
        /* Treat as a noop */
        break;
    case 0x33:          /* Prefetch */
        /* Treat as a noop */
        break;
    case 0x3F: /* HACK */
        break;

    /* Floating point.  */
    case 0x31: /* LWC1 */
    case 0x35: /* LDC1 */
    case 0x39: /* SWC1 */
    case 0x3D: /* SDC1 */
    case 0x11:          /* CP1 opcode */
#if defined(MIPS_USES_FPU)
        /* XXX: not correct */
#else
        generate_exception_err(ctx, EXCP_CpU, 1);
#endif
        break;

    /* COP2.  */
    case 0x32: /* LWC2 */
    case 0x36: /* LDC2 */
    case 0x3A: /* SWC2 */
    case 0x3E: /* SDC2 */
    case 0x12:          /* CP2 opcode */
        /* Not implemented */
        generate_exception_err(ctx, EXCP_CpU, 2);
        break;

    case 0x13:          /* CP3 opcode */
        /* Not implemented */
        generate_exception_err(ctx, EXCP_CpU, 3);
        break;

#if defined (TARGET_MIPS64)
    case 0x18 ... 0x1B:
    case 0x27:
    case 0x34:
    case 0x37:
        /* MIPS64 opcodes */
#endif
#if defined (MIPS_HAS_JALX)
    case 0x1D:
        /* JALX: not implemented */
#endif
    case 0x1E:
        /* ASE specific */
    default:            /* Invalid */
        MIPS_INVAL("");
        generate_exception(ctx, EXCP_RI);
        break;
    }
    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        int hflags = ctx->hflags;
        /* Branches completion */
        ctx->hflags &= ~MIPS_HFLAG_BMASK;
        ctx->bstate = BS_BRANCH;
        save_cpu_state(ctx, 0);
        switch (hflags & MIPS_HFLAG_BMASK) {
        case MIPS_HFLAG_B:
            /* unconditional branch */
            MIPS_DEBUG("unconditional branch");
            gen_goto_tb(ctx, 0, ctx->btarget);
            break;
        case MIPS_HFLAG_BL:
            /* blikely taken case */
            MIPS_DEBUG("blikely branch taken");
            gen_goto_tb(ctx, 0, ctx->btarget);
            break;
        case MIPS_HFLAG_BC:
            /* Conditional branch */
            MIPS_DEBUG("conditional branch");
            {
              int l1;
              l1 = gen_new_label();
              gen_op_jnz_T2(l1);
              gen_goto_tb(ctx, 1, ctx->pc + 4);
              gen_set_label(l1);
              gen_goto_tb(ctx, 0, ctx->btarget);
            }
            break;
        case MIPS_HFLAG_BR:
            /* unconditional branch to register */
            MIPS_DEBUG("branch to register");
            gen_op_breg();
            break;
        default:
            MIPS_DEBUG("unknown branch");
            break;
        }
    }
}

int gen_intermediate_code_internal (CPUState *env, TranslationBlock *tb,
                                    int search_pc)
{
    DisasContext ctx, *ctxp = &ctx;
    target_ulong pc_start;
    uint16_t *gen_opc_end;
    int j, lj = -1;

    if (search_pc && loglevel)
	fprintf (logfile, "search pc %d\n", search_pc);

    pc_start = tb->pc;
    gen_opc_ptr = gen_opc_buf;
    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;
    gen_opparam_ptr = gen_opparam_buf;
    nb_gen_labels = 0;
    ctx.pc = pc_start;
    ctx.saved_pc = -1;
    ctx.tb = tb;
    ctx.bstate = BS_NONE;
    /* Restore delay slot state from the tb context.  */
    ctx.hflags = tb->flags;
    ctx.saved_hflags = ctx.hflags;
    if (ctx.hflags & MIPS_HFLAG_BR) {
        gen_op_restore_breg_target();
    } else if (ctx.hflags & MIPS_HFLAG_B) {
        ctx.btarget = env->btarget;
    } else if (ctx.hflags & MIPS_HFLAG_BMASK) {
        /* If we are in the delay slot of a conditional branch,
         * restore the branch condition from env->bcond to T2
         */
        ctx.btarget = env->btarget;
        gen_op_restore_bcond();
    }
#if defined(CONFIG_USER_ONLY)
    ctx.mem_idx = 0;
#else
    ctx.mem_idx = !((ctx.hflags & MIPS_HFLAG_MODE) == MIPS_HFLAG_UM);
#endif
    ctx.CP0_Status = env->CP0_Status;
#ifdef DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_CPU) {
        fprintf(logfile, "------------------------------------------------\n");
        /* FIXME: This may print out stale hflags from env... */
        cpu_dump_state(env, logfile, fprintf, 0);
    }
#endif
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM)
        fprintf(logfile, "\ntb %p super %d cond %04x\n",
                tb, ctx.mem_idx, ctx.hflags);
#endif
    while (ctx.bstate == BS_NONE && gen_opc_ptr < gen_opc_end) {
        if (env->nb_breakpoints > 0) {
            for(j = 0; j < env->nb_breakpoints; j++) {
                if (env->breakpoints[j] == ctx.pc) {
                    save_cpu_state(ctxp, 1);
                    ctx.bstate = BS_BRANCH;
                    gen_op_debug();
                    goto done_generating;
                }
            }
        }

        if (search_pc) {
            j = gen_opc_ptr - gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j)
                    gen_opc_instr_start[lj++] = 0;
            }
            gen_opc_pc[lj] = ctx.pc;
            gen_opc_hflags[lj] = ctx.hflags & MIPS_HFLAG_BMASK;
            gen_opc_instr_start[lj] = 1;
        }
        ctx.opcode = ldl_code(ctx.pc);
        decode_opc(&ctx);
        ctx.pc += 4;

        if (env->singlestep_enabled)
            break;

        if ((ctx.pc & (TARGET_PAGE_SIZE - 1)) == 0)
            break;

#if defined (MIPS_SINGLE_STEP)
        break;
#endif
    }
    if (env->singlestep_enabled) {
        save_cpu_state(ctxp, ctx.bstate == BS_NONE);
        gen_op_debug();
        goto done_generating;
    }
    else if (ctx.bstate != BS_BRANCH && ctx.bstate != BS_EXCP) {
        save_cpu_state(ctxp, 0);
        gen_goto_tb(&ctx, 0, ctx.pc);
    }
    gen_op_reset_T0();
    /* Generate the return instruction */
    gen_op_exit_tb();
done_generating:
    *gen_opc_ptr = INDEX_op_end;
    if (search_pc) {
        j = gen_opc_ptr - gen_opc_buf;
        lj++;
        while (lj <= j)
            gen_opc_instr_start[lj++] = 0;
        tb->size = 0;
    } else {
        tb->size = ctx.pc - pc_start;
    }
#ifdef DEBUG_DISAS
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM)
        fprintf(logfile, "\n");
#endif
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "IN: %s\n", lookup_symbol(pc_start));
	target_disas(logfile, pc_start, ctx.pc - pc_start, 0);
        fprintf(logfile, "\n");
    }
    if (loglevel & CPU_LOG_TB_OP) {
        fprintf(logfile, "OP:\n");
        dump_ops(gen_opc_buf, gen_opparam_buf);
        fprintf(logfile, "\n");
    }
    if (loglevel & CPU_LOG_TB_CPU) {
        fprintf(logfile, "---------------- %d %08x\n", ctx.bstate, ctx.hflags);
    }
#endif
    
    return 0;
}

int gen_intermediate_code (CPUState *env, struct TranslationBlock *tb)
{
    return gen_intermediate_code_internal(env, tb, 0);
}

int gen_intermediate_code_pc (CPUState *env, struct TranslationBlock *tb)
{
    return gen_intermediate_code_internal(env, tb, 1);
}

void cpu_dump_state (CPUState *env, FILE *f, 
                     int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                     int flags)
{
    uint32_t c0_status;
    int i;
    
    cpu_fprintf(f, "pc=0x%08x HI=0x%08x LO=0x%08x ds %04x %08x %d\n",
                env->PC, env->HI, env->LO, env->hflags, env->btarget, env->bcond);
    for (i = 0; i < 32; i++) {
        if ((i & 3) == 0)
            cpu_fprintf(f, "GPR%02d:", i);
        cpu_fprintf(f, " %s %08x", regnames[i], env->gpr[i]);
        if ((i & 3) == 3)
            cpu_fprintf(f, "\n");
    }

    c0_status = env->CP0_Status;
    if (env->hflags & MIPS_HFLAG_UM)
        c0_status |= (1 << CP0St_UM);
    if (env->hflags & MIPS_HFLAG_ERL)
        c0_status |= (1 << CP0St_ERL);
    if (env->hflags & MIPS_HFLAG_EXL)
        c0_status |= (1 << CP0St_EXL);

    cpu_fprintf(f, "CP0 Status  0x%08x Cause   0x%08x EPC    0x%08x\n",
                c0_status, env->CP0_Cause, env->CP0_EPC);
    cpu_fprintf(f, "    Config0 0x%08x Config1 0x%08x LLAddr 0x%08x\n",
                env->CP0_Config0, env->CP0_Config1, env->CP0_LLAddr);
}

CPUMIPSState *cpu_mips_init (void)
{
    CPUMIPSState *env;

    env = qemu_mallocz(sizeof(CPUMIPSState));
    if (!env)
        return NULL;
    cpu_exec_init(env);
    tlb_flush(env, 1);
    /* Minimal init */
    env->PC = 0xBFC00000;
#if defined (MIPS_USES_R4K_TLB)
    env->CP0_random = MIPS_TLB_NB - 1;
#endif
    env->CP0_Wired = 0;
    env->CP0_Config0 = MIPS_CONFIG0;
#if defined (MIPS_CONFIG1)
        env->CP0_Config1 = MIPS_CONFIG1;
#endif
#if defined (MIPS_CONFIG2)
        env->CP0_Config2 = MIPS_CONFIG2;
#endif
#if defined (MIPS_CONFIG3)
        env->CP0_Config3 = MIPS_CONFIG3;
#endif
    env->CP0_Status = (1 << CP0St_CU0) | (1 << CP0St_BEV);
    env->CP0_WatchLo = 0;
    env->hflags = MIPS_HFLAG_ERL;
    /* Count register increments in debug mode, EJTAG version 1 */
    env->CP0_Debug = (1 << CP0DB_CNT) | (0x1 << CP0DB_VER);
    env->CP0_PRid = MIPS_CPU;
    env->exception_index = EXCP_NONE;
#if defined(CONFIG_USER_ONLY)
    env->hflags |= MIPS_HFLAG_UM;
#endif
    return env;
}
