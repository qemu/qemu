/*
 *  MIPS translation routines.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef TARGET_MIPS_TRANSLATE_H
#define TARGET_MIPS_TRANSLATE_H

#include "exec/translator.h"

#define MIPS_DEBUG_DISAS 0

typedef struct DisasContext {
    DisasContextBase base;
    target_ulong saved_pc;
    target_ulong page_start;
    uint32_t opcode;
    uint64_t insn_flags;
    int32_t CP0_Config1;
    int32_t CP0_Config2;
    int32_t CP0_Config3;
    int32_t CP0_Config5;
    /* Routine used to access memory */
    int mem_idx;
    MemOp default_tcg_memop_mask;
    uint32_t hflags, saved_hflags;
    target_ulong btarget;
    bool ulri;
    int kscrexist;
    bool rxi;
    int ie;
    bool bi;
    bool bp;
    uint64_t PAMask;
    bool mvh;
    bool eva;
    bool sc;
    int CP0_LLAddr_shift;
    bool ps;
    bool vp;
    bool cmgcr;
    bool mrp;
    bool nan2008;
    bool abs2008;
    bool saar;
    bool mi;
    int gi;
} DisasContext;

/* MIPS major opcodes */
#define MASK_OP_MAJOR(op)   (op & (0x3F << 26))

void generate_exception(DisasContext *ctx, int excp);
void generate_exception_err(DisasContext *ctx, int excp, int err);
void generate_exception_end(DisasContext *ctx, int excp);
void gen_reserved_instruction(DisasContext *ctx);

void check_insn(DisasContext *ctx, uint64_t flags);
#ifdef TARGET_MIPS64
void check_mips_64(DisasContext *ctx);
#endif

void gen_base_offset_addr(DisasContext *ctx, TCGv addr, int base, int offset);
void gen_move_low32(TCGv ret, TCGv_i64 arg);
void gen_move_high32(TCGv ret, TCGv_i64 arg);
void gen_load_gpr(TCGv t, int reg);
void gen_store_gpr(TCGv t, int reg);

void gen_op_addr_add(DisasContext *ctx, TCGv ret, TCGv arg0, TCGv arg1);

extern TCGv cpu_gpr[32], cpu_PC;
extern TCGv bcond;

#define LOG_DISAS(...)                                                        \
    do {                                                                      \
        if (MIPS_DEBUG_DISAS) {                                               \
            qemu_log_mask(CPU_LOG_TB_IN_ASM, ## __VA_ARGS__);                 \
        }                                                                     \
    } while (0)

#define MIPS_INVAL(op)                                                        \
    do {                                                                      \
        if (MIPS_DEBUG_DISAS) {                                               \
            qemu_log_mask(CPU_LOG_TB_IN_ASM,                                  \
                          TARGET_FMT_lx ": %08x Invalid %s %03x %03x %03x\n", \
                          ctx->base.pc_next, ctx->opcode, op,                 \
                          ctx->opcode >> 26, ctx->opcode & 0x3F,              \
                          ((ctx->opcode >> 16) & 0x1F));                      \
        }                                                                     \
    } while (0)

#endif
