/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MACROS_H
#define MACROS_H

#include "cpu.h"
#include "hex_regs.h"
#include "reg_fields.h"

#define PCALIGN 4
#define PCALIGN_MASK (PCALIGN - 1)

#define GET_FIELD(FIELD, REGIN) \
    fEXTRACTU_BITS(REGIN, reg_field_info[FIELD].width, \
                   reg_field_info[FIELD].offset)

#ifdef QEMU_GENERATE
#define GET_USR_FIELD(FIELD, DST) \
    tcg_gen_extract_tl(DST, hex_gpr[HEX_REG_USR], \
                       reg_field_info[FIELD].offset, \
                       reg_field_info[FIELD].width)

#define SET_USR_FIELD_FUNC(X) \
    _Generic((X), int : gen_set_usr_fieldi, TCGv : gen_set_usr_field)
#define SET_USR_FIELD(FIELD, VAL) \
    SET_USR_FIELD_FUNC(VAL)(FIELD, VAL)
#else
#define GET_USR_FIELD(FIELD) \
    fEXTRACTU_BITS(env->gpr[HEX_REG_USR], reg_field_info[FIELD].width, \
                   reg_field_info[FIELD].offset)

#define SET_USR_FIELD(FIELD, VAL) \
    fINSERT_BITS(env->gpr[HEX_REG_USR], reg_field_info[FIELD].width, \
                 reg_field_info[FIELD].offset, (VAL))
#endif

#ifdef QEMU_GENERATE
#define DECL_REG(NAME, NUM, X, OFF) \
    TCGv NAME = tcg_temp_local_new(); \
    int NUM = REGNO(X) + OFF

#define DECL_REG_WRITABLE(NAME, NUM, X, OFF) \
    TCGv NAME = tcg_temp_local_new(); \
    int NUM = REGNO(X) + OFF; \
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        if (is_predicated && !is_preloaded(ctx, NUM)) { \
            tcg_gen_mov_tl(hex_new_value[NUM], hex_gpr[NUM]); \
        } \
    } while (0)
/*
 * For read-only temps, avoid allocating and freeing
 */
#define DECL_REG_READONLY(NAME, NUM, X, OFF) \
    TCGv NAME; \
    int NUM = REGNO(X) + OFF

#define DECL_RREG_d(NAME, NUM, X, OFF) \
    DECL_REG_WRITABLE(NAME, NUM, X, OFF)
#define DECL_RREG_e(NAME, NUM, X, OFF) \
    DECL_REG(NAME, NUM, X, OFF)
#define DECL_RREG_s(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_RREG_t(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_RREG_u(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_RREG_v(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_RREG_x(NAME, NUM, X, OFF) \
    DECL_REG_WRITABLE(NAME, NUM, X, OFF)
#define DECL_RREG_y(NAME, NUM, X, OFF) \
    DECL_REG_WRITABLE(NAME, NUM, X, OFF)

#define DECL_PREG_d(NAME, NUM, X, OFF) \
    DECL_REG(NAME, NUM, X, OFF)
#define DECL_PREG_e(NAME, NUM, X, OFF) \
    DECL_REG(NAME, NUM, X, OFF)
#define DECL_PREG_s(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_PREG_t(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_PREG_u(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_PREG_v(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_PREG_x(NAME, NUM, X, OFF) \
    DECL_REG(NAME, NUM, X, OFF)
#define DECL_PREG_y(NAME, NUM, X, OFF) \
    DECL_REG(NAME, NUM, X, OFF)

#define DECL_CREG_d(NAME, NUM, X, OFF) \
    DECL_REG(NAME, NUM, X, OFF)
#define DECL_CREG_s(NAME, NUM, X, OFF) \
    DECL_REG(NAME, NUM, X, OFF)

#define DECL_MREG_u(NAME, NUM, X, OFF) \
    DECL_REG(NAME, NUM, X, OFF)

#define DECL_NEW_NREG_s(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_NEW_NREG_t(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)

#define DECL_NEW_PREG_t(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_NEW_PREG_u(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)
#define DECL_NEW_PREG_v(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)

#define DECL_NEW_OREG_s(NAME, NUM, X, OFF) \
    DECL_REG_READONLY(NAME, NUM, X, OFF)

#define DECL_PAIR(NAME, NUM, X, OFF) \
    TCGv_i64 NAME = tcg_temp_local_new_i64(); \
    size1u_t NUM = REGNO(X) + OFF

#define DECL_PAIR_WRITABLE(NAME, NUM, X, OFF) \
    TCGv_i64 NAME = tcg_temp_local_new_i64(); \
    size1u_t NUM = REGNO(X) + OFF; \
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        if (is_predicated) { \
            if (!is_preloaded(ctx, NUM)) { \
                tcg_gen_mov_tl(hex_new_value[NUM], hex_gpr[NUM]); \
            } \
            if (!is_preloaded(ctx, NUM + 1)) { \
                tcg_gen_mov_tl(hex_new_value[NUM + 1], hex_gpr[NUM + 1]); \
            } \
        } \
    } while (0)

#define DECL_RREG_dd(NAME, NUM, X, OFF) \
    DECL_PAIR_WRITABLE(NAME, NUM, X, OFF)
#define DECL_RREG_ss(NAME, NUM, X, OFF) \
    DECL_PAIR(NAME, NUM, X, OFF)
#define DECL_RREG_tt(NAME, NUM, X, OFF) \
    DECL_PAIR(NAME, NUM, X, OFF)
#define DECL_RREG_xx(NAME, NUM, X, OFF) \
    DECL_PAIR_WRITABLE(NAME, NUM, X, OFF)
#define DECL_RREG_yy(NAME, NUM, X, OFF) \
    DECL_PAIR_WRITABLE(NAME, NUM, X, OFF)

#define DECL_CREG_dd(NAME, NUM, X, OFF) \
    DECL_PAIR_WRITABLE(NAME, NUM, X, OFF)
#define DECL_CREG_ss(NAME, NUM, X, OFF) \
    DECL_PAIR(NAME, NUM, X, OFF)

#define DECL_IMM(NAME, X) \
    TCGv NAME = tcg_const_tl(IMMNO(X))

#define DECL_EA \
    TCGv EA = tcg_temp_local_new()

#define LOG_REG_WRITE(RNUM, VAL)\
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        gen_log_reg_write(RNUM, VAL, insn->slot, is_predicated); \
        ctx_log_reg_write(ctx, (RNUM)); \
    } while (0)

#define LOG_PRED_WRITE(PNUM, VAL) \
    do { \
        gen_log_pred_write(PNUM, VAL); \
        ctx_log_pred_write(ctx, (PNUM)); \
    } while (0)

#define FREE_REG(NAME) \
    tcg_temp_free(NAME)
#define FREE_REG_READONLY(NAME) \
    /* Nothing */

#define FREE_RREG_d(NAME)            FREE_REG(NAME)
#define FREE_RREG_e(NAME)            FREE_REG(NAME)
#define FREE_RREG_s(NAME)            FREE_REG_READONLY(NAME)
#define FREE_RREG_t(NAME)            FREE_REG_READONLY(NAME)
#define FREE_RREG_u(NAME)            FREE_REG_READONLY(NAME)
#define FREE_RREG_v(NAME)            FREE_REG_READONLY(NAME)
#define FREE_RREG_x(NAME)            FREE_REG(NAME)
#define FREE_RREG_y(NAME)            FREE_REG(NAME)

#define FREE_PREG_d(NAME)            FREE_REG(NAME)
#define FREE_PREG_e(NAME)            FREE_REG(NAME)
#define FREE_PREG_s(NAME)            FREE_REG_READONLY(NAME)
#define FREE_PREG_t(NAME)            FREE_REG_READONLY(NAME)
#define FREE_PREG_u(NAME)            FREE_REG_READONLY(NAME)
#define FREE_PREG_v(NAME)            FREE_REG_READONLY(NAME)
#define FREE_PREG_x(NAME)            FREE_REG(NAME)

#define FREE_CREG_d(NAME)            FREE_REG(NAME)
#define FREE_CREG_s(NAME)            FREE_REG_READONLY(NAME)

#define FREE_MREG_u(NAME)            FREE_REG_READONLY(NAME)

#define FREE_NEW_NREG_s(NAME)        FREE_REG(NAME)
#define FREE_NEW_NREG_t(NAME)        FREE_REG(NAME)

#define FREE_NEW_PREG_t(NAME)        FREE_REG_READONLY(NAME)
#define FREE_NEW_PREG_u(NAME)        FREE_REG_READONLY(NAME)
#define FREE_NEW_PREG_v(NAME)        FREE_REG_READONLY(NAME)

#define FREE_NEW_OREG_s(NAME)        FREE_REG(NAME)

#define DECL_IMM(NAME, X) \
    TCGv NAME = tcg_const_tl(IMMNO(X))

#define DECL_EA \
    TCGv EA = tcg_temp_local_new()

#define FREE_REG_PAIR(NAME) \
    tcg_temp_free_i64(NAME)

#define FREE_RREG_dd(NAME)           FREE_REG_PAIR(NAME)
#define FREE_RREG_ss(NAME)           FREE_REG_PAIR(NAME)
#define FREE_RREG_tt(NAME)           FREE_REG_PAIR(NAME)
#define FREE_RREG_xx(NAME)           FREE_REG_PAIR(NAME)
#define FREE_RREG_yy(NAME)           FREE_REG_PAIR(NAME)

#define FREE_CREG_dd(NAME)           FREE_REG_PAIR(NAME)
#define FREE_CREG_ss(NAME)           FREE_REG_PAIR(NAME)

#define FREE_IMM(NAME) \
    tcg_temp_free(NAME)

#define FREE_EA \
    tcg_temp_free(EA)
#else
#define LOG_REG_WRITE(RNUM, VAL)\
  log_reg_write(env, RNUM, VAL, slot)
#define LOG_PRED_WRITE(RNUM, VAL)\
    log_pred_write(env, RNUM, VAL)
#endif

#define SLOT_WRAP(CODE) \
    do { \
        TCGv slot = tcg_const_tl(insn->slot); \
        CODE; \
        tcg_temp_free(slot); \
    } while (0)

#define PART1_WRAP(CODE) \
    do { \
        TCGv part1 = tcg_const_tl(insn->part1); \
        CODE; \
        tcg_temp_free(part1); \
    } while (0)

#define MARK_LATE_PRED_WRITE(RNUM) /* Not modelled in qemu */

#define REGNO(NUM) (insn->regno[NUM])
#define IMMNO(NUM) (insn->immed[NUM])

#ifdef QEMU_GENERATE
#define READ_REG(dest, NUM) \
    gen_read_reg(dest, NUM)
#define READ_REG_READONLY(dest, NUM) \
    do { dest = hex_gpr[NUM]; } while (0)

#define READ_RREG_s(dest, NUM) \
    READ_REG_READONLY(dest, NUM)
#define READ_RREG_t(dest, NUM) \
    READ_REG_READONLY(dest, NUM)
#define READ_RREG_u(dest, NUM) \
    READ_REG_READONLY(dest, NUM)
#define READ_RREG_x(dest, NUM) \
    READ_REG(dest, NUM)
#define READ_RREG_y(dest, NUM) \
    READ_REG(dest, NUM)

#define READ_OREG_s(dest, NUM) \
    READ_REG_READONLY(dest, NUM)

#ifdef QEMU_GENERATE
#define READ_CREG_s(dest, NUM) \
    do { \
        if ((NUM) + HEX_REG_SA0 == HEX_REG_P3_0) { \
            gen_read_p3_0(dest); \
        } else { \
            READ_REG_READONLY(dest, ((NUM) + HEX_REG_SA0)); \
        } \
    } while (0)
#else
static inline int32_t read_p3_0(CPUHexagonState *env)
{
    int32_t control_reg = 0;
    int i;
    for (i = NUM_PREGS - 1; i >= 0; i--) {
        control_reg <<= 8;
        control_reg |= env->pred[i] & 0xff;
    }
    return control_reg;
}

#define READ_CREG_s(dest, NUM) \
    do { \
        if (NUM == HEX_REG_P3_0) { \
            dest = read_p3_0(env); \
        } else { \
            READ_REG_READONLY(dest, ((NUM) + HEX_REG_SA0)); \
        } \
    } while (0)
#endif
#define READ_MREG_u(dest, NUM) \
    do { \
        READ_REG_READONLY(dest, ((NUM) + HEX_REG_M0)); \
        dest = dest; \
    } while (0)
#else
#define READ_REG(NUM) \
    (env->gpr[(NUM)])
#define READ_MREG(NUM) \
    (env->gpr[NUM + REG_M])
#define READ_CSREG(NUM) \
    (env->gpr[NUM + HEX_REG_CS0])
#endif

#ifdef QEMU_GENERATE
#define READ_REG_PAIR(tmp, NUM) \
    tcg_gen_concat_i32_i64(tmp, hex_gpr[NUM], hex_gpr[(NUM) + 1])
#define READ_RREG_ss(tmp, NUM)          READ_REG_PAIR(tmp, NUM)
#define READ_RREG_tt(tmp, NUM)          READ_REG_PAIR(tmp, NUM)
#define READ_RREG_xx(tmp, NUM)          READ_REG_PAIR(tmp, NUM)
#define READ_RREG_yy(tmp, NUM)          READ_REG_PAIR(tmp, NUM)

#define READ_CREG_PAIR(tmp, i) \
    READ_REG_PAIR(tmp, ((i) + HEX_REG_SA0))
#define READ_CREG_ss(tmp, i)            READ_CREG_PAIR(tmp, i)
#endif

#ifdef QEMU_GENERATE
#define READ_PREG(dest, NUM)             gen_read_preg(dest, (NUM))
#define READ_PREG_READONLY(dest, NUM)    do { dest = hex_pred[NUM]; } while (0)

#define READ_PREG_s(dest, NUM)           READ_PREG_READONLY(dest, NUM)
#define READ_PREG_t(dest, NUM)           READ_PREG_READONLY(dest, NUM)
#define READ_PREG_u(dest, NUM)           READ_PREG_READONLY(dest, NUM)
#define READ_PREG_v(dest, NUM)           READ_PREG_READONLY(dest, NUM)
#define READ_PREG_x(dest, NUM)           READ_PREG(dest, NUM)

#define READ_NEW_PREG(pred, PNUM) \
    do { pred = hex_new_pred_value[PNUM]; } while (0)
#define READ_NEW_PREG_t(pred, PNUM)      READ_NEW_PREG(pred, PNUM)
#define READ_NEW_PREG_u(pred, PNUM)      READ_NEW_PREG(pred, PNUM)
#define READ_NEW_PREG_v(pred, PNUM)      READ_NEW_PREG(pred, PNUM)

#define READ_NEW_REG(tmp, i) \
    do { tmp = tcg_const_tl(i); } while (0)
#define READ_NEW_NREG_s(tmp, i)          READ_NEW_REG(tmp, i)
#define READ_NEW_NREG_t(tmp, i)          READ_NEW_REG(tmp, i)
#define READ_NEW_OREG_s(tmp, i)          READ_NEW_REG(tmp, i)
#else
#define READ_PREG(NUM)                (env->pred[NUM])
#endif


#define WRITE_RREG(NUM, VAL)             LOG_REG_WRITE(NUM, VAL)
#define WRITE_RREG_d(NUM, VAL)           LOG_REG_WRITE(NUM, VAL)
#define WRITE_RREG_e(NUM, VAL)           LOG_REG_WRITE(NUM, VAL)
#define WRITE_RREG_x(NUM, VAL)           LOG_REG_WRITE(NUM, VAL)
#define WRITE_RREG_y(NUM, VAL)           LOG_REG_WRITE(NUM, VAL)

#define WRITE_PREG(NUM, VAL)             LOG_PRED_WRITE(NUM, VAL)
#define WRITE_PREG_d(NUM, VAL)           LOG_PRED_WRITE(NUM, VAL)
#define WRITE_PREG_e(NUM, VAL)           LOG_PRED_WRITE(NUM, VAL)
#define WRITE_PREG_x(NUM, VAL)           LOG_PRED_WRITE(NUM, VAL)

#ifdef QEMU_GENERATE
#define WRITE_CREG(i, tmp) \
    do { \
        if (i + HEX_REG_SA0 == HEX_REG_P3_0) { \
            gen_write_p3_0(tmp); \
        } else { \
            WRITE_RREG((i) + HEX_REG_SA0, tmp); \
        } \
    } while (0)
#define WRITE_CREG_d(NUM, VAL)           WRITE_CREG(NUM, VAL)

#define WRITE_CREG_PAIR(i, tmp)          WRITE_REG_PAIR((i) + HEX_REG_SA0, tmp)
#define WRITE_CREG_dd(NUM, VAL)          WRITE_CREG_PAIR(NUM, VAL)

#define WRITE_REG_PAIR(NUM, VAL) \
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        gen_log_reg_write_pair(NUM, VAL, insn->slot, is_predicated); \
        ctx_log_reg_write(ctx, (NUM)); \
        ctx_log_reg_write(ctx, (NUM) + 1); \
    } while (0)

#define WRITE_RREG_dd(NUM, VAL)          WRITE_REG_PAIR(NUM, VAL)
#define WRITE_RREG_xx(NUM, VAL)          WRITE_REG_PAIR(NUM, VAL)
#define WRITE_RREG_yy(NUM, VAL)          WRITE_REG_PAIR(NUM, VAL)
#endif

#ifdef QEMU_GENERATE
#define MEM_LOAD1s(DST, VA)       tcg_gen_qemu_ld8s(DST, VA, ctx->mem_idx)
#define MEM_LOAD1u(DST, VA)       tcg_gen_qemu_ld8u(DST, VA, ctx->mem_idx)
#define MEM_LOAD2s(DST, VA)       tcg_gen_qemu_ld16s(DST, VA, ctx->mem_idx)
#define MEM_LOAD2u(DST, VA)       tcg_gen_qemu_ld16u(DST, VA, ctx->mem_idx)
#define MEM_LOAD4s(DST, VA)       tcg_gen_qemu_ld32s(DST, VA, ctx->mem_idx)
#define MEM_LOAD4u(DST, VA)       tcg_gen_qemu_ld32s(DST, VA, ctx->mem_idx)
#define MEM_LOAD8s(DST, VA)       tcg_gen_qemu_ld64(DST, VA, ctx->mem_idx)
#define MEM_LOAD8u(DST, VA)       tcg_gen_qemu_ld64(DST, VA, ctx->mem_idx)

#define MEM_STORE1_FUNC(X) \
    _Generic((X), int : gen_store1i, TCGv_i32 : gen_store1)
#define MEM_STORE1(VA, DATA, SLOT) \
    MEM_STORE1_FUNC(DATA)(cpu_env, VA, DATA, ctx, SLOT)

#define MEM_STORE2_FUNC(X) \
    _Generic((X), int : gen_store2i, TCGv_i32 : gen_store2)
#define MEM_STORE2(VA, DATA, SLOT) \
    MEM_STORE2_FUNC(DATA)(cpu_env, VA, DATA, ctx, SLOT)

#define MEM_STORE4_FUNC(X) \
    _Generic((X), int : gen_store4i, TCGv_i32 : gen_store4)
#define MEM_STORE4(VA, DATA, SLOT) \
    MEM_STORE4_FUNC(DATA)(cpu_env, VA, DATA, ctx, SLOT)

#define MEM_STORE8_FUNC(X) \
    _Generic((X), int64_t : gen_store8i, TCGv_i64 : gen_store8)
#define MEM_STORE8(VA, DATA, SLOT) \
    MEM_STORE8_FUNC(DATA)(cpu_env, VA, DATA, ctx, SLOT)

#else
/*
 * These should never be executed, but they are needed so the helpers will
 * compile.  All the instructions with loads must be implemented under
 * QEMU_GENERATE.
 */
static inline uint8_t mem_load1(CPUHexagonState *env, target_ulong vaddr)
{
    printf("ERROR: mem_load1\n");
    g_assert_not_reached();
}

static inline uint16_t mem_load2(CPUHexagonState *env, target_ulong vadd)
{
    printf("ERROR: mem_load2\n");
    g_assert_not_reached();
}

static inline uint32_t mem_load4(CPUHexagonState *env, target_ulong vaddr)
{
    printf("ERROR: mem_load4\n");
    g_assert_not_reached();
}

static inline uint64_t mem_load8(CPUHexagonState *env, target_ulong vaddr)
{
    printf("ERROR: mem_load8\n");
    g_assert_not_reached();
}

static inline
uint32_t mem_load_locked4(CPUHexagonState *env, target_ulong vaddr)
{
    printf("ERROR: load_locked4\n");
    g_assert_not_reached();
    return 0;
}

static inline
uint64_t mem_load_locked8(CPUHexagonState *env, target_ulong vaddr)
{
    printf("ERROR: load_locked8\n");
    g_assert_not_reached();
    return 0;
}

static inline
uint8_t mem_store_conditional(CPUHexagonState *env,
                              target_ulong vaddr, uint32_t src, int size)
{
    printf("ERROR: store conditional\n");
    g_assert_not_reached();
    return 0;
}

#define MEM_LOAD1s(VA) ((size1s_t)mem_load1(env, VA))
#define MEM_LOAD1u(VA) ((size1u_t)mem_load1(env, VA))
#define MEM_LOAD2s(VA) ((size2s_t)mem_load2(env, VA))
#define MEM_LOAD2u(VA) ((size2u_t)mem_load2(env, VA))
#define MEM_LOAD4s(VA) ((size4s_t)mem_load4(env, VA))
#define MEM_LOAD4u(VA) ((size4u_t)mem_load4(env, VA))
#define MEM_LOAD8s(VA) ((size8s_t)mem_load8(env, VA))
#define MEM_LOAD8u(VA) ((size8u_t)mem_load8(env, VA))

#define MEM_STORE1(VA, DATA, SLOT) log_store32(env, VA, DATA, 1, SLOT)
#define MEM_STORE2(VA, DATA, SLOT) log_store32(env, VA, DATA, 2, SLOT)
#define MEM_STORE4(VA, DATA, SLOT) log_store32(env, VA, DATA, 4, SLOT)
#define MEM_STORE8(VA, DATA, SLOT) log_store64(env, VA, DATA, 8, SLOT)
#endif


#ifdef QEMU_GENERATE
static inline void gen_slot_cancelled_check(TCGv check, int slot_num)
{
    TCGv mask = tcg_const_tl(1 << slot_num);
    TCGv one = tcg_const_tl(1);
    TCGv zero = tcg_const_tl(0);

    tcg_gen_and_tl(mask, hex_slot_cancelled, mask);
    tcg_gen_movcond_tl(TCG_COND_NE, check, mask, zero, one, zero);

    tcg_temp_free(one);
    tcg_temp_free(zero);
    tcg_temp_free(mask);
}

static inline void gen_cancel(TCGv slot)
{
    TCGv one = tcg_const_tl(1);
    TCGv mask = tcg_temp_new();
    tcg_gen_shl_tl(mask, one, slot);
    tcg_gen_or_tl(hex_slot_cancelled, hex_slot_cancelled, mask);
    tcg_temp_free(one);
    tcg_temp_free(mask);
}

#define CANCEL gen_cancel(slot);
#else
#define CANCEL cancel_slot(env, slot)
#endif

#define STORE_ZERO
#define LOAD_CANCEL(EA) do { CANCEL; } while (0)

#ifdef QEMU_GENERATE
static inline void gen_pred_cancel(TCGv pred, int slot_num)
 {
    TCGv slot_mask = tcg_const_tl(1 << slot_num);
    TCGv tmp = tcg_temp_new();
    TCGv zero = tcg_const_tl(0);
    TCGv one = tcg_const_tl(1);
    tcg_gen_or_tl(slot_mask, hex_slot_cancelled, slot_mask);
    tcg_gen_andi_tl(tmp, pred, 1);
    tcg_gen_movcond_tl(TCG_COND_EQ, hex_slot_cancelled, tmp, zero,
                       slot_mask, hex_slot_cancelled);
    tcg_temp_free(slot_mask);
    tcg_temp_free(tmp);
    tcg_temp_free(zero);
    tcg_temp_free(one);
}
#define PRED_LOAD_CANCEL(PRED, EA) \
    gen_pred_cancel(PRED, insn->is_endloop ? 4 : insn->slot)

#define PRED_STORE_CANCEL(PRED, EA) \
    gen_pred_cancel(PRED, insn->is_endloop ? 4 : insn->slot)
#else
#define STORE_CANCEL(EA) { env->slot_cancelled |= (1 << slot); }
#endif

#define IS_CANCELLED(SLOT)
#define fMAX(A, B) (((A) > (B)) ? (A) : (B))

#ifdef QEMU_GENERATE
#define fMIN(DST, A, B) tcg_gen_movcond_i32(TCG_COND_GT, DST, A, B, A, B)
#else
#define fMIN(A, B) (((A) < (B)) ? (A) : (B))
#endif

#define fABS(A) (((A) < 0) ? (-(A)) : (A))
#define fINSERT_BITS(REG, WIDTH, OFFSET, INVAL) \
    do { \
        REG = ((REG) & ~(((fCONSTLL(1) << (WIDTH)) - 1) << (OFFSET))) | \
           (((INVAL) & ((fCONSTLL(1) << (WIDTH)) - 1)) << (OFFSET)); \
    } while (0)
#define fEXTRACTU_BITS(INREG, WIDTH, OFFSET) \
    (fZXTN(WIDTH, 32, (INREG >> OFFSET)))
#define fEXTRACTU_BIDIR(INREG, WIDTH, OFFSET) \
    (fZXTN(WIDTH, 32, fBIDIR_LSHIFTR((INREG), (OFFSET), 4_8)))
#define fEXTRACTU_RANGE(INREG, HIBIT, LOWBIT) \
    (fZXTN((HIBIT - LOWBIT + 1), 32, (INREG >> LOWBIT)))
#define fINSERT_RANGE(INREG, HIBIT, LOWBIT, INVAL) \
    do { \
        int offset = LOWBIT; \
        int width = HIBIT - LOWBIT + 1; \
        INREG &= ~(((fCONSTLL(1) << width) - 1) << offset); \
        INREG |= ((INVAL & ((fCONSTLL(1) << width) - 1)) << offset); \
    } while (0)

#ifdef QEMU_GENERATE
#define f8BITSOF(RES, VAL) gen_8bitsof(RES, VAL)
#define fLSBOLD(VAL) tcg_gen_andi_tl(LSB, (VAL), 1)
#else
#define f8BITSOF(VAL) ((VAL) ? 0xff : 0x00)
#define fLSBOLD(VAL)  ((VAL) & 1)
#endif

#ifdef QEMU_GENERATE
#define fLSBNEW(PVAL)   tcg_gen_mov_tl(LSB, (PVAL))
#define fLSBNEW0        fLSBNEW(0)
#define fLSBNEW1        fLSBNEW(1)
#else
#define fLSBNEW(PVAL)   (PVAL)
#define fLSBNEW0        new_pred_value(env, 0)
#define fLSBNEW1        new_pred_value(env, 1)
#endif

#ifdef QEMU_GENERATE
static inline void gen_logical_not(TCGv dest, TCGv src)
{
    TCGv one = tcg_const_tl(1);
    TCGv zero = tcg_const_tl(0);

    tcg_gen_movcond_tl(TCG_COND_NE, dest, src, zero, zero, one);

    tcg_temp_free(one);
    tcg_temp_free(zero);
}
#define fLSBOLDNOT(VAL) \
    do { \
        tcg_gen_andi_tl(LSB, (VAL), 1); \
        tcg_gen_xori_tl(LSB, LSB, 1); \
    } while (0)
#define fLSBNEWNOT(PNUM) \
    gen_logical_not(LSB, (PNUM))
#define fLSBNEW0NOT \
    do { \
        tcg_gen_mov_tl(LSB, hex_new_pred_value[0]); \
        gen_logical_not(LSB, LSB); \
    } while (0)
#define fLSBNEW1NOT \
    do { \
        tcg_gen_mov_tl(LSB, hex_new_pred_value[1]); \
        gen_logical_not(LSB, LSB); \
    } while (0)
#else
#define fLSBNEWNOT(PNUM) (!fLSBNEW(PNUM))
#define fLSBOLDNOT(VAL) (!fLSBOLD(VAL))
#define fLSBNEW0NOT (!fLSBNEW0)
#define fLSBNEW1NOT (!fLSBNEW1)
#endif

#define fNEWREG(RNUM) HELPER(new_value)(env, RNUM)

#ifdef QEMU_GENERATE
#define fNEWREG_ST(RNUM) gen_newreg_st(NEWREG_ST, cpu_env, RNUM)
#else
#define fNEWREG_ST(RNUM) HELPER(new_value)(env, RNUM)
#endif

#define fMEMZNEW(RNUM) (RNUM == 0)
#define fMEMNZNEW(RNUM) (RNUM != 0)
#define fVSATUVALN(N, VAL) \
    ({ \
        (((int)(VAL)) < 0) ? 0 : ((1LL << (N)) - 1); \
    })
#define fSATUVALN(N, VAL) \
    ({ \
        fSET_OVERFLOW(); \
        ((VAL) < 0) ? 0 : ((1LL << (N)) - 1); \
    })
#define fSATVALN(N, VAL) \
    ({ \
        fSET_OVERFLOW(); \
        ((VAL) < 0) ? (-(1LL << ((N) - 1))) : ((1LL << ((N) - 1)) - 1); \
    })
#define fVSATVALN(N, VAL) \
    ({ \
        ((VAL) < 0) ? (-(1LL << ((N) - 1))) : ((1LL << ((N) - 1)) - 1); \
    })
#define fZXTN(N, M, VAL) ((VAL) & ((1LL << (N)) - 1))
#define fSXTN(N, M, VAL) \
    ((fZXTN(N, M, VAL) ^ (1LL << ((N) - 1))) - (1LL << ((N) - 1)))
#define fSATN(N, VAL) \
    ((fSXTN(N, 64, VAL) == (VAL)) ? (VAL) : fSATVALN(N, VAL))
#define fVSATN(N, VAL) \
    ((fSXTN(N, 64, VAL) == (VAL)) ? (VAL) : fVSATVALN(N, VAL))
#define fADDSAT64(DST, A, B) \
    do { \
        size8u_t __a = fCAST8u(A); \
        size8u_t __b = fCAST8u(B); \
        size8u_t __sum = __a + __b; \
        size8u_t __xor = __a ^ __b; \
        const size8u_t __mask = 0x8000000000000000ULL; \
        if (__xor & __mask) { \
            DST = __sum; \
        } \
        else if ((__a ^ __sum) & __mask) { \
            if (__sum & __mask) { \
                DST = 0x7FFFFFFFFFFFFFFFLL; \
                fSET_OVERFLOW(); \
            } else { \
                DST = 0x8000000000000000LL; \
                fSET_OVERFLOW(); \
            } \
        } else { \
            DST = __sum; \
        } \
    } while (0)
#define fVSATUN(N, VAL) \
    ((fZXTN(N, 64, VAL) == (VAL)) ? (VAL) : fVSATUVALN(N, VAL))
#define fSATUN(N, VAL) \
    ((fZXTN(N, 64, VAL) == (VAL)) ? (VAL) : fSATUVALN(N, VAL))
#define fSATH(VAL) (fSATN(16, VAL))
#define fSATUH(VAL) (fSATUN(16, VAL))
#define fVSATH(VAL) (fVSATN(16, VAL))
#define fVSATUH(VAL) (fVSATUN(16, VAL))
#define fSATUB(VAL) (fSATUN(8, VAL))
#define fSATB(VAL) (fSATN(8, VAL))
#define fVSATUB(VAL) (fVSATUN(8, VAL))
#define fVSATB(VAL) (fVSATN(8, VAL))
#define fIMMEXT(IMM) (IMM = IMM)
#define fMUST_IMMEXT(IMM) fIMMEXT(IMM)

#ifdef QEMU_GENERATE
#define fPCALIGN(IMM) tcg_gen_andi_tl(IMM, IMM, ~PCALIGN_MASK)
#else
#define fPCALIGN(IMM) IMM = (IMM & ~PCALIGN_MASK)
#endif

#define fGET_EXTENSION (insn->extension)

#ifdef QEMU_GENERATE
static inline TCGv gen_read_ireg(TCGv tmp, TCGv val, int shift)
{
    /*
     *  #define fREAD_IREG(VAL) \
     *      (fSXTN(11,64,(((VAL) & 0xf0000000)>>21) | ((VAL>>17)&0x7f) ))
     */
    tcg_gen_sari_tl(tmp, val, 17);
    tcg_gen_andi_tl(tmp, tmp, 0x7f);
    tcg_gen_shli_tl(tmp, tmp, shift);
    return tmp;
}
#define fREAD_IREG(VAL, SHIFT) gen_read_ireg(ireg, (VAL), (SHIFT))
#define fREAD_R0() (READ_REG(tmp, 0))
#define fREAD_LR() (READ_REG(tmp, HEX_REG_LR))
#define fREAD_SSR() (READ_REG(tmp, HEX_REG_SSR))
#else
#define fREAD_IREG(VAL) \
    (fSXTN(11, 64, (((VAL) & 0xf0000000) >> 21) | ((VAL >> 17) & 0x7f)))
#define fREAD_R0() (READ_REG(0))
#define fREAD_LR() (READ_REG(HEX_REG_LR))
#define fREAD_SSR() (READ_REG(HEX_REG_SSR))
#endif

#define fWRITE_R0(A) WRITE_RREG(0, A)
#define fWRITE_LR(A) WRITE_RREG(HEX_REG_LR, A)
#define fWRITE_FP(A) WRITE_RREG(HEX_REG_FP, A)
#define fWRITE_SP(A) WRITE_RREG(HEX_REG_SP, A)
#define fWRITE_GOSP(A) WRITE_RREG(HEX_REG_GOSP, A)
#define fWRITE_GP(A) WRITE_RREG(HEX_REG_GP, A)

#ifdef QEMU_GENERATE
#define fREAD_SP() (READ_REG(tmp, HEX_REG_SP))
#define fREAD_GOSP() (READ_REG(tmp, HEX_REG_GOSP))
#define fREAD_GELR() (READ_REG(tmp, HEX_REG_GELR))
#define fREAD_GEVB() (READ_REG(tmp, HEX_REG_GEVB))
#define fREAD_CSREG(N) (READ_REG(tmp, HEX_REG_CS0 + N))
#define fREAD_LC0 (READ_REG(tmp, HEX_REG_LC0))
#define fREAD_LC1 (READ_REG(tmp, HEX_REG_LC1))
#define fREAD_SA0 (READ_REG(tmp, HEX_REG_SA0))
#define fREAD_SA1 (READ_REG(tmp, HEX_REG_SA1))
#define fREAD_FP() (READ_REG(tmp, HEX_REG_FP))
#define fREAD_GP() (READ_REG(tmp, HEX_REG_GP))
#define fREAD_PC() (READ_REG(tmp, HEX_REG_PC))
#else
#define fREAD_SP() (READ_REG(HEX_REG_SP))
#define fREAD_GOSP() (READ_REG(HEX_REG_GOSP))
#define fREAD_GELR() (READ_REG(HEX_REG_GELR))
#define fREAD_GEVB() (READ_REG(HEX_REG_GEVB))
#define fREAD_CSREG(N) (READ_REG(HEX_REG_CS0 + N))
#define fREAD_LC0 (READ_REG(HEX_REG_LC0))
#define fREAD_LC1 (READ_REG(HEX_REG_LC1))
#define fREAD_SA0 (READ_REG(HEX_REG_SA0))
#define fREAD_SA1 (READ_REG(HEX_REG_SA1))
#define fREAD_FP() (READ_REG(HEX_REG_FP))
#define fREAD_GP() (READ_REG(HEX_REG_GP))
#define fREAD_PC() (READ_REG(HEX_REG_PC))
#endif

#define fREAD_NPC() (env->next_PC & (0xfffffffe))

#ifdef QEMU_GENERATE
#define fREAD_P0() (READ_PREG(tmp, 0))
#define fREAD_P3() (READ_PREG(tmp, 3))
#define fNOATTRIB_READ_P3() (READ_PREG(tmp, 3))
#else
#define fREAD_P0() (READ_PREG(0))
#define fREAD_P3() (READ_PREG(3))
#define fNOATTRIB_READ_P3() (READ_PREG(3))
#endif

#define fCHECK_PCALIGN(A)
#define fCUREXT() GET_SSR_FIELD(SSR_XA)
#define fCUREXT_WRAP(EXT_NO)

#ifdef QEMU_GENERATE
#define fWRITE_NPC(A) gen_write_new_pc(A)
#else
#define fWRITE_NPC(A) write_new_pc(env, A)
#endif

#define fLOOPSTATS(A)
#define fCOF_CALLBACK(LOC, TYPE)
#define fBRANCH(LOC, TYPE) \
    do { \
        fWRITE_NPC(LOC); \
        fCOF_CALLBACK(LOC, TYPE); \
    } while (0)
#define fJUMPR(REGNO, TARGET, TYPE) fBRANCH(TARGET, COF_TYPE_JUMPR)
#define fHINTJR(TARGET) { }
#define fBP_RAS_CALL(A)
#define fCALL(A) \
    do { \
        fWRITE_LR(fREAD_NPC()); \
        fBRANCH(A, COF_TYPE_CALL); \
    } while (0)
#define fCALLR(A) \
    do { \
        fWRITE_LR(fREAD_NPC()); \
        fBRANCH(A, COF_TYPE_CALLR); \
    } while (0)
#define fWRITE_LOOP_REGS0(START, COUNT) \
    do { \
        WRITE_RREG(HEX_REG_LC0, COUNT);  \
        WRITE_RREG(HEX_REG_SA0, START); \
    } while (0)
#define fWRITE_LOOP_REGS1(START, COUNT) \
    do { \
        WRITE_RREG(HEX_REG_LC1, COUNT);  \
        WRITE_RREG(HEX_REG_SA1, START);\
    } while (0)
#define fWRITE_LC0(VAL) WRITE_RREG(HEX_REG_LC0, VAL)
#define fWRITE_LC1(VAL) WRITE_RREG(HEX_REG_LC1, VAL)

#ifdef QEMU_GENERATE
#define fCARRY_FROM_ADD(A, B, C) gen_carry_from_add64(tmp_i64, A, B, C)
#else
#define fCARRY_FROM_ADD(A, B, C) carry_from_add64(A, B, C)
#endif

#define fSETCV_ADD(A, B, CARRY) \
    do { \
        SET_USR_FIELD(USR_C, gen_carry_add((A), (B), ((A) + (B)))); \
        SET_USR_FIELD(USR_V, gen_overflow_add((A), (B), ((A) + (B)))); \
    } while (0)
#define fSETCV_SUB(A, B, CARRY) \
    do { \
        SET_USR_FIELD(USR_C, gen_carry_add((A), (B), ((A) - (B)))); \
        SET_USR_FIELD(USR_V, gen_overflow_add((A), (B), ((A) - (B)))); \
    } while (0)
#define fSET_OVERFLOW() SET_USR_FIELD(USR_OVF, 1)
#define fSET_LPCFG(VAL) SET_USR_FIELD(USR_LPCFG, (VAL))
#define fGET_LPCFG (GET_USR_FIELD(USR_LPCFG))
#define fWRITE_P0(VAL) WRITE_PREG(0, VAL)
#define fWRITE_P1(VAL) WRITE_PREG(1, VAL)
#define fWRITE_P2(VAL) WRITE_PREG(2, VAL)
#define fWRITE_P3(VAL) WRITE_PREG(3, VAL)
#define fWRITE_P3_LATE(VAL) WRITE_PREG(3, VAL)
#define fPART1(WORK) if (part1) { WORK; return; }
#define fCAST4u(A) ((size4u_t)(A))
#define fCAST4s(A) ((size4s_t)(A))
#define fCAST8u(A) ((size8u_t)(A))
#define fCAST8s(A) ((size8s_t)(A))
#define fCAST2_2s(A) ((size2s_t)(A))
#define fCAST2_2u(A) ((size2u_t)(A))
#define fCAST4_4s(A) ((size4s_t)(A))
#define fCAST4_4u(A) ((size4u_t)(A))
#define fCAST4_8s(A) ((size8s_t)((size4s_t)(A)))
#define fCAST4_8u(A) ((size8u_t)((size4u_t)(A)))
#define fCAST8_8s(A) ((size8s_t)(A))
#define fCAST8_8u(A) ((size8u_t)(A))
#define fCAST2_8s(A) ((size8s_t)((size2s_t)(A)))
#define fCAST2_8u(A) ((size8u_t)((size2u_t)(A)))
#define fZE8_16(A) ((size2s_t)((size1u_t)(A)))
#define fSE8_16(A) ((size2s_t)((size1s_t)(A)))
#define fSE16_32(A) ((size4s_t)((size2s_t)(A)))
#define fZE16_32(A) ((size4u_t)((size2u_t)(A)))
#define fSE32_64(A) ((size8s_t)((size4s_t)(A)))
#define fZE32_64(A) ((size8u_t)((size4u_t)(A)))
#define fSE8_32(A) ((size4s_t)((size1s_t)(A)))
#define fZE8_32(A) ((size4s_t)((size1u_t)(A)))
#define fMPY8UU(A, B) (int)(fZE8_16(A) * fZE8_16(B))
#define fMPY8US(A, B) (int)(fZE8_16(A) * fSE8_16(B))
#define fMPY8SU(A, B) (int)(fSE8_16(A) * fZE8_16(B))
#define fMPY8SS(A, B) (int)((short)(A) * (short)(B))
#define fMPY16SS(A, B) fSE32_64(fSE16_32(A) * fSE16_32(B))
#define fMPY16UU(A, B) fZE32_64(fZE16_32(A) * fZE16_32(B))
#define fMPY16SU(A, B) fSE32_64(fSE16_32(A) * fZE16_32(B))
#define fMPY16US(A, B) fMPY16SU(B, A)
#define fMPY32SS(A, B) (fSE32_64(A) * fSE32_64(B))
#define fMPY32UU(A, B) (fZE32_64(A) * fZE32_64(B))
#define fMPY32SU(A, B) (fSE32_64(A) * fZE32_64(B))
#define fMPY3216SS(A, B) (fSE32_64(A) * fSXTN(16, 64, B))
#define fMPY3216SU(A, B) (fSE32_64(A) * fZXTN(16, 64, B))
#define fROUND(A) (A + 0x8000)
#define fCLIP(DST, SRC, U) \
    do { \
        size4s_t maxv = (1 << U) - 1; \
        size4s_t minv = -(1 << U); \
        DST = fMIN(maxv, fMAX(SRC, minv)); \
    } while (0)
#define fCRND(A) ((((A) & 0x3) == 0x3) ? ((A) + 1) : ((A)))
#define fRNDN(A, N) ((((N) == 0) ? (A) : (((fSE32_64(A)) + (1 << ((N) - 1))))))
#define fCRNDN(A, N) (conv_round(A, N))
#define fCRNDN64(A, N) (conv_round64(A, N))
#define fADD128(A, B) (add128(A, B))
#define fSUB128(A, B) (sub128(A, B))
#define fSHIFTR128(A, B) (shiftr128(A, B))
#define fSHIFTL128(A, B) (shiftl128(A, B))
#define fAND128(A, B) (and128(A, B))
#define fCAST8S_16S(A) (cast8s_to_16s(A))
#define fCAST16S_8S(A) (cast16s_to_8s(A))
#define fCAST16S_4S(A) (cast16s_to_4s(A))

#ifdef QEMU_GENERATE
#define fEA_RI_FUNC(X) \
    _Generic((X), int : tcg_gen_addi_tl, TCGv_i32 : tcg_gen_add_tl)
#define fEA_RI(REG, IMM) fEA_RI_FUNC(IMM)(EA, REG, IMM)
#define fEA_RRs(REG, REG2, SCALE) \
    do { \
        TCGv __tmp = tcg_temp_new(); \
        tcg_gen_shl_tl(__tmp, REG2, SCALE); \
        tcg_gen_add_tl(EA, REG, __tmp); \
        tcg_temp_free(__tmp); \
    } while (0)
#define fEA_IRs(IMM, REG, SCALE) \
    do { \
        tcg_gen_shl_tl(tmp, REG, SCALE); \
        tcg_gen_add_tl(EA, IMM, tmp); \
    } while (0)
#else
#define fEA_RI(REG, IMM) \
    do { \
        EA = REG + IMM; \
        fDOCHKPAGECROSS(REG, EA); \
    } while (0)
#define fEA_RRs(REG, REG2, SCALE) \
    do { \
        EA = REG + (REG2 << SCALE); \
        fDOCHKPAGECROSS(REG, EA); \
    } while (0)
#define fEA_IRs(IMM, REG, SCALE) \
    do { \
        EA = IMM + (REG << SCALE); \
        fDOCHKPAGECROSS(IMM, EA); \
    } while (0)
#endif

#ifdef QEMU_GENERATE
#define fEA_IMM(IMM) tcg_gen_mov_tl(EA, IMM)
#define fEA_REG(REG) tcg_gen_mov_tl(EA, REG)
static inline void gen_fbrev(TCGv result, TCGv src)
{
    TCGv result_hi = tcg_temp_new();
    TCGv result_lo = tcg_temp_new();
    TCGv tmp1 = tcg_temp_new();
    TCGv tmp2 = tcg_temp_new();
    int i;

    /*
     *  Bit reverse the low 16 bits of the address
     */
    tcg_gen_andi_tl(result_hi, src, 0xffff0000);
    tcg_gen_movi_tl(result_lo, 0);
    tcg_gen_mov_tl(tmp1, src);
    for (i = 0; i < 16; i++) {
        /*
         * result_lo = (result_lo << 1) | (tmp1 & 1);
         * tmp1 >>= 1;
         */
        tcg_gen_shli_tl(result_lo, result_lo, 1);
        tcg_gen_andi_tl(tmp2, tmp1, 1);
        tcg_gen_or_tl(result_lo, result_lo, tmp2);
        tcg_gen_sari_tl(tmp1, tmp1, 1);
    }
    tcg_gen_or_tl(result, result_hi, result_lo);

    tcg_temp_free(result_hi);
    tcg_temp_free(result_lo);
    tcg_temp_free(tmp1);
    tcg_temp_free(tmp2);
}
static inline void gen_fcircadd(TCGv reg, TCGv incr, TCGv M, TCGv start_addr)
{
    TCGv length = tcg_temp_new();
    TCGv new_ptr = tcg_temp_new();
    TCGv end_addr = tcg_temp_new();
    TCGv tmp = tcg_temp_new();

    tcg_gen_andi_tl(length, M, 0x1ffff);
    tcg_gen_add_tl(new_ptr, reg, incr);
    tcg_gen_add_tl(end_addr, start_addr, length);

    tcg_gen_sub_tl(tmp, new_ptr, length);
    tcg_gen_movcond_tl(TCG_COND_GE, new_ptr, new_ptr, end_addr, tmp, new_ptr);
    tcg_gen_add_tl(tmp, new_ptr, length);
    tcg_gen_movcond_tl(TCG_COND_LT, new_ptr, new_ptr, start_addr, tmp, new_ptr);

    tcg_gen_mov_tl(reg, new_ptr);

    tcg_temp_free(length);
    tcg_temp_free(new_ptr);
    tcg_temp_free(end_addr);
    tcg_temp_free(tmp);
}

#define fEA_BREVR(REG) gen_fbrev(EA, REG)
#define fEA_GPI(IMM) tcg_gen_add_tl(EA, fREAD_GP(), IMM)
#define fPM_I(REG, IMM) { tcg_gen_add_tl(REG, REG, IMM); }
#define fPM_M(REG, MVAL) { tcg_gen_add_tl(REG, REG, MVAL); }
#else
#define fEA_IMM(IMM) EA = IMM
#define fEA_REG(REG) EA = REG
#define fEA_GPI(IMM) \
    do { \
        EA = fREAD_GP() + IMM; \
        fGP_DOCHKPAGECROSS(fREAD_GP(), EA); \
    } while (0)
#define fPM_I(REG, IMM) \
    do { \
        REG = REG + IMM; \
    } while (0)
#define fPM_M(REG, MVAL) \
    do { \
        REG = REG + MVAL; \
    } while (0)
#define fEA_BREVR(REG) \
    do { \
        EA = fbrev(REG); \
    } while (0)
#endif
#define fPM_CIRI(REG, IMM, MVAL) \
    do { \
        fcirc_add(REG, siV, MuV); \
    } while (0)
#define fPM_CIRR(REG, VAL, MVAL) \
    do { \
        fcirc_add(REG, VAL, MuV); \
    } while (0)
#define fMODCIRCU(N, P) ((N) & ((1 << (P)) - 1))
#define fSCALE(N, A) (((size8s_t)(A)) << N)
#define fVSATW(A) fVSATN(32, ((long long)A))
#define fSATW(A) fSATN(32, ((long long)A))
#define fVSAT(A) fVSATN(32, (A))
#define fSAT(A) fSATN(32, (A))
#define fSAT_ORIG_SHL(A, ORIG_REG) \
    ((((size4s_t)((fSAT(A)) ^ ((size4s_t)(ORIG_REG)))) < 0) \
        ? fSATVALN(32, ((size4s_t)(ORIG_REG))) \
        : ((((ORIG_REG) > 0) && ((A) == 0)) ? fSATVALN(32, (ORIG_REG)) \
                                            : fSAT(A)))
#define fPASS(A) A
#define fRND(A) (((A) + 1) >> 1)
#define fBIDIR_SHIFTL(SRC, SHAMT, REGSTYPE) \
    (((SHAMT) < 0) ? ((fCAST##REGSTYPE(SRC) >> ((-(SHAMT)) - 1)) >> 1) \
                   : (fCAST##REGSTYPE(SRC) << (SHAMT)))
#define fBIDIR_ASHIFTL(SRC, SHAMT, REGSTYPE) \
    fBIDIR_SHIFTL(SRC, SHAMT, REGSTYPE##s)
#define fBIDIR_LSHIFTL(SRC, SHAMT, REGSTYPE) \
    fBIDIR_SHIFTL(SRC, SHAMT, REGSTYPE##u)
#define fBIDIR_ASHIFTL_SAT(SRC, SHAMT, REGSTYPE) \
    (((SHAMT) < 0) ? ((fCAST##REGSTYPE##s(SRC) >> ((-(SHAMT)) - 1)) >> 1) \
                   : fSAT_ORIG_SHL(fCAST##REGSTYPE##s(SRC) << (SHAMT), (SRC)))
#define fBIDIR_SHIFTR(SRC, SHAMT, REGSTYPE) \
    (((SHAMT) < 0) ? ((fCAST##REGSTYPE(SRC) << ((-(SHAMT)) - 1)) << 1) \
                   : (fCAST##REGSTYPE(SRC) >> (SHAMT)))
#define fBIDIR_ASHIFTR(SRC, SHAMT, REGSTYPE) \
    fBIDIR_SHIFTR(SRC, SHAMT, REGSTYPE##s)
#define fBIDIR_LSHIFTR(SRC, SHAMT, REGSTYPE) \
    fBIDIR_SHIFTR(SRC, SHAMT, REGSTYPE##u)
#define fBIDIR_ASHIFTR_SAT(SRC, SHAMT, REGSTYPE) \
    (((SHAMT) < 0) ? fSAT_ORIG_SHL((fCAST##REGSTYPE##s(SRC) \
                        << ((-(SHAMT)) - 1)) << 1, (SRC)) \
                   : (fCAST##REGSTYPE##s(SRC) >> (SHAMT)))
#ifdef QEMU_GENERATE
#define fASHIFTR(DST, SRC, SHAMT, REGSTYPE) \
    gen_ashiftr_##REGSTYPE##s(DST, SRC, SHAMT)
#define fLSHIFTR(DST, SRC, SHAMT, REGSTYPE) \
    gen_lshiftr_##REGSTYPE##u(DST, SRC, SHAMT)
#else
#define fASHIFTR(SRC, SHAMT, REGSTYPE) (fCAST##REGSTYPE##s(SRC) >> (SHAMT))
#define fLSHIFTR(SRC, SHAMT, REGSTYPE) \
    (((SHAMT) >= 64) ? 0 : (fCAST##REGSTYPE##u(SRC) >> (SHAMT)))
#endif
#define fROTL(SRC, SHAMT, REGSTYPE) \
    (((SHAMT) == 0) ? (SRC) : ((fCAST##REGSTYPE##u(SRC) << (SHAMT)) | \
                              ((fCAST##REGSTYPE##u(SRC) >> \
                                 ((sizeof(SRC) * 8) - (SHAMT))))))
#define fROTR(SRC, SHAMT, REGSTYPE) \
    (((SHAMT) == 0) ? (SRC) : ((fCAST##REGSTYPE##u(SRC) >> (SHAMT)) | \
                              ((fCAST##REGSTYPE##u(SRC) << \
                                 ((sizeof(SRC) * 8) - (SHAMT))))))
#ifdef QEMU_GENERATE
#define fASHIFTL(DST, SRC, SHAMT, REGSTYPE) \
    gen_ashiftl_##REGSTYPE##s(DST, SRC, SHAMT)
#else
#define fASHIFTL(SRC, SHAMT, REGSTYPE) \
    (((SHAMT) >= 64) ? 0 : (fCAST##REGSTYPE##s(SRC) << (SHAMT)))
#endif
#define fFLOAT(A) \
    ({ union { float f; size4u_t i; } _fipun; _fipun.i = (A); _fipun.f; })
#define fUNFLOAT(A) \
    ({ union { float f; size4u_t i; } _fipun; \
     _fipun.f = (A); isnan(_fipun.f) ? 0xFFFFFFFFU : _fipun.i; })
#define fSFNANVAL() 0xffffffff
#define fSFINFVAL(A) (((A) & 0x80000000) | 0x7f800000)
#define fSFONEVAL(A) (((A) & 0x80000000) | fUNFLOAT(1.0))
#define fCHECKSFNAN(DST, A) \
    do { \
        if (isnan(fFLOAT(A))) { \
            if ((fGETBIT(22, A)) == 0) { \
                fRAISEFLAGS(FE_INVALID); \
            } \
            DST = fSFNANVAL(); \
        } \
    } while (0)
#define fCHECKSFNAN3(DST, A, B, C) \
    do { \
        fCHECKSFNAN(DST, A); \
        fCHECKSFNAN(DST, B); \
        fCHECKSFNAN(DST, C); \
    } while (0)
#define fSF_BIAS() 127
#define fSF_MANTBITS() 23
#define fSF_RECIP_LOOKUP(IDX) arch_recip_lookup(IDX)
#define fSF_INVSQRT_LOOKUP(IDX) arch_invsqrt_lookup(IDX)
#define fSF_MUL_POW2(A, B) \
    (fUNFLOAT(fFLOAT(A) * fFLOAT((fSF_BIAS() + (B)) << fSF_MANTBITS())))
#define fSF_GETEXP(A) (((A) >> fSF_MANTBITS()) & 0xff)
#define fSF_MAXEXP() (254)
#define fSF_RECIP_COMMON(N, D, O, A) arch_sf_recip_common(&N, &D, &O, &A)
#define fSF_INVSQRT_COMMON(N, O, A) arch_sf_invsqrt_common(&N, &O, &A)
#define fFMAFX(A, B, C, ADJ) internal_fmafx(A, B, C, fSXTN(8, 64, ADJ))
#define fFMAF(A, B, C) internal_fmafx(A, B, C, 0)
#define fSFMPY(A, B) internal_mpyf(A, B)
#define fMAKESF(SIGN, EXP, MANT) \
    ((((SIGN) & 1) << 31) | \
     (((EXP) & 0xff) << fSF_MANTBITS()) | \
     ((MANT) & ((1 << fSF_MANTBITS()) - 1)))
#define fDOUBLE(A) \
    ({ union { double f; size8u_t i; } _fipun; _fipun.i = (A); _fipun.f; })
#define fUNDOUBLE(A) \
    ({ union { double f; size8u_t i; } _fipun; \
     _fipun.f = (A); \
     isnan(_fipun.f) ? 0xFFFFFFFFFFFFFFFFULL : _fipun.i; })
#define fDFNANVAL() 0xffffffffffffffffULL
#define fDFINFVAL(A) (((A) & 0x8000000000000000ULL) | 0x7ff0000000000000ULL)
#define fDFONEVAL(A) (((A) & 0x8000000000000000ULL) | fUNDOUBLE(1.0))
#define fCHECKDFNAN(DST, A) \
    do { \
        if (isnan(fDOUBLE(A))) { \
            if ((fGETBIT(51, A)) == 0) { \
                fRAISEFLAGS(FE_INVALID); \
            } \
            DST = fDFNANVAL(); \
        } \
    } while (0)
#define fCHECKDFNAN3(DST, A, B, C) \
    do { \
        fCHECKDFNAN(DST, A); \
        fCHECKDFNAN(DST, B); \
        fCHECKDFNAN(DST, C); \
    } while (0)
#define fDF_BIAS() 1023
#define fDF_ISNORMAL(X) (fpclassify(fDOUBLE(X)) == FP_NORMAL)
#define fDF_ISDENORM(X) (fpclassify(fDOUBLE(X)) == FP_SUBNORMAL)
#define fDF_ISBIG(X) (fDF_GETEXP(X) >= 512)
#define fDF_MANTBITS() 52
#define fDF_RECIP_LOOKUP(IDX) (size8u_t)(arch_recip_lookup(IDX))
#define fDF_INVSQRT_LOOKUP(IDX) (size8u_t)(arch_invsqrt_lookup(IDX))
#define fDF_MUL_POW2(A, B) \
    (fUNDOUBLE(fDOUBLE(A) * fDOUBLE((0ULL + fDF_BIAS() + (B)) << \
     fDF_MANTBITS())))
#define fDF_GETEXP(A) (((A) >> fDF_MANTBITS()) & 0x7ff)
#define fDF_MAXEXP() (2046)
#define fDF_RECIP_COMMON(N, D, O, A) arch_df_recip_common(&N, &D, &O, &A)
#define fDF_INVSQRT_COMMON(N, O, A) arch_df_invsqrt_common(&N, &O, &A)
#define fFMA(A, B, C) internal_fma(A, B, C)
#define fDFMPY(A, B) internal_mpy(A, B)
#define fDF_MPY_HH(A, B, ACC) internal_mpyhh(A, B, ACC)
#define fFMAX(A, B, C, ADJ) internal_fmax(A, B, C, fSXTN(8, 64, ADJ) * 2)
#define fMAKEDF(SIGN, EXP, MANT) \
    ((((SIGN) & 1ULL) << 63) | \
     (((EXP) & 0x7ffULL) << fDF_MANTBITS()) | \
     ((MANT) & ((1ULL << fDF_MANTBITS()) - 1)))

#ifdef QEMU_GENERATE
/* These will be needed if we write any FP instructions with TCG */
#define fFPOP_START() g_assert_not_reached();
#define fFPOP_END()   g_assert_not_reached();
#else
#define fFPOP_START() arch_fpop_start(env)
#define fFPOP_END() arch_fpop_end(env)
#endif

#define fFPSETROUND_NEAREST() fesetround(FE_TONEAREST)
#define fFPSETROUND_CHOP() fesetround(FE_TOWARDZERO)
#define fFPCANCELFLAGS() feclearexcept(FE_ALL_EXCEPT)
#define fISINFPROD(A, B) \
    ((isinf(A) && isinf(B)) || \
     (isinf(A) && isfinite(B) && ((B) != 0.0)) || \
     (isinf(B) && isfinite(A) && ((A) != 0.0)))
#define fISZEROPROD(A, B) \
    ((((A) == 0.0) && isfinite(B)) || (((B) == 0.0) && isfinite(A)))
#define fRAISEFLAGS(A) arch_raise_fpflag(A)
#define fDF_MAX(A, B) \
    (((A) == (B)) ? fDOUBLE(fUNDOUBLE(A) & fUNDOUBLE(B)) : fmax(A, B))
#define fDF_MIN(A, B) \
    (((A) == (B)) ? fDOUBLE(fUNDOUBLE(A) | fUNDOUBLE(B)) : fmin(A, B))
#define fSF_MAX(A, B) \
    (((A) == (B)) ? fFLOAT(fUNFLOAT(A) & fUNFLOAT(B)) : fmaxf(A, B))
#define fSF_MIN(A, B) \
    (((A) == (B)) ? fFLOAT(fUNFLOAT(A) | fUNFLOAT(B)) : fminf(A, B))
#define fMMU(ADDR) ADDR

#ifdef QEMU_GENERATE
#define fcirc_add(REG, INCR, MV) \
    gen_fcircadd(REG, INCR, MV, fREAD_CSREG(MuN))
#else
#define fcirc_add(REG, INCR, IMMED)  /* Not possible in helpers */
#endif

#define fbrev(REG) (fbrevaddr(REG))

#ifdef QEMU_GENERATE
#define fLOAD(NUM, SIZE, SIGN, EA, DST) MEM_LOAD##SIZE##SIGN(DST, EA)
#else
#define fLOAD(NUM, SIZE, SIGN, EA, DST) \
    DST = (size##SIZE##SIGN##_t)MEM_LOAD##SIZE##SIGN(EA)
#endif

#define fMEMOP(NUM, SIZE, SIGN, EA, FNTYPE, VALUE)

#ifdef QEMU_GENERATE
#define fGET_FRAMEKEY() READ_REG(tmp, HEX_REG_FRAMEKEY)
static inline TCGv_i64 gen_frame_scramble(TCGv_i64 result)
{
    /* ((LR << 32) | FP) ^ (FRAMEKEY << 32)) */
    TCGv_i64 LR_i64 = tcg_temp_new_i64();
    TCGv_i64 FP_i64 = tcg_temp_new_i64();
    TCGv_i64 FRAMEKEY_i64 = tcg_temp_new_i64();

    tcg_gen_extu_i32_i64(LR_i64, hex_gpr[HEX_REG_LR]);
    tcg_gen_extu_i32_i64(FP_i64, hex_gpr[HEX_REG_FP]);
    tcg_gen_extu_i32_i64(FRAMEKEY_i64, hex_gpr[HEX_REG_FRAMEKEY]);

    tcg_gen_shli_i64(LR_i64, LR_i64, 32);
    tcg_gen_shli_i64(FRAMEKEY_i64, FRAMEKEY_i64, 32);
    tcg_gen_or_i64(result, LR_i64, FP_i64);
    tcg_gen_xor_i64(result, result, FRAMEKEY_i64);

    tcg_temp_free_i64(LR_i64);
    tcg_temp_free_i64(FP_i64);
    tcg_temp_free_i64(FRAMEKEY_i64);
    return result;
}
#define fFRAME_SCRAMBLE(VAL) gen_frame_scramble(scramble_tmp)
static inline TCGv_i64 gen_frame_unscramble(TCGv_i64 frame)
{
    TCGv_i64 FRAMEKEY_i64 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(FRAMEKEY_i64, hex_gpr[HEX_REG_FRAMEKEY]);
    tcg_gen_shli_i64(FRAMEKEY_i64, FRAMEKEY_i64, 32);
    tcg_gen_xor_i64(frame, frame, FRAMEKEY_i64);
    tcg_temp_free_i64(FRAMEKEY_i64);
    return frame;
}

#define fFRAME_UNSCRAMBLE(VAL) gen_frame_unscramble(VAL)
#else
#define fGET_FRAMEKEY() READ_REG(HEX_REG_FRAMEKEY)
#define fFRAME_SCRAMBLE(VAL) ((VAL) ^ (fCAST8u(fGET_FRAMEKEY()) << 32))
#define fFRAME_UNSCRAMBLE(VAL) fFRAME_SCRAMBLE(VAL)
#endif

#ifdef CONFIG_USER_ONLY
#define fFRAMECHECK(ADDR, EA) do { } while (0) /* Not modelled in linux-user */
#else
/* System mode not implemented yet */
#define fFRAMECHECK(ADDR, EA)  g_assert_not_reached();
#endif

#ifdef QEMU_GENERATE
#define fLOAD_LOCKED(NUM, SIZE, SIGN, EA, DST) \
    gen_load_locked##SIZE##SIGN(DST, EA, ctx->mem_idx);
#else
#define fLOAD_LOCKED(NUM, SIZE, SIGN, EA, DST) \
    DST = (size##SIZE##SIGN##_t)mem_load_locked##SIZE(env, EA);
#endif

#define fLOAD_PHYS(NUM, SIZE, SIGN, SRC1, SRC2, DST)

#ifdef QEMU_GENERATE
#define fSTORE(NUM, SIZE, EA, SRC) MEM_STORE##SIZE(EA, SRC, insn->slot)
#else
#define fSTORE(NUM, SIZE, EA, SRC) MEM_STORE##SIZE(EA, SRC, slot)
#endif

#ifdef QEMU_GENERATE
#define fSTORE_LOCKED(NUM, SIZE, EA, SRC, PRED) \
    gen_store_conditional##SIZE(env, ctx, PdN, PRED, EA, SRC);
#else
#define fSTORE_LOCKED(NUM, SIZE, EA, SRC, PRED) \
    PRED = (mem_store_conditional(env, EA, SRC, SIZE) ? 0xff : 0);
#endif

#define fVTCM_MEMCPY(DST, SRC, SIZE)
#define fPERMUTEH(SRC0, SRC1, CTRL) fpermuteh((SRC0), (SRC1), CTRL)
#define fPERMUTEB(SRC0, SRC1, CTRL) fpermuteb((SRC0), (SRC1), CTRL)

#ifdef QEMU_GENERATE
#define GETBYTE_FUNC(X) \
    _Generic((X), TCGv_i32 : gen_get_byte, TCGv_i64 : gen_get_byte_i64)
#define fGETBYTE(N, SRC) GETBYTE_FUNC(SRC)(BYTE, N, SRC, true)
#define fGETUBYTE(N, SRC) GETBYTE_FUNC(SRC)(BYTE, N, SRC, false)
#else
#define fGETBYTE(N, SRC) ((size1s_t)((SRC >> ((N) * 8)) & 0xff))
#define fGETUBYTE(N, SRC) ((size1u_t)((SRC >> ((N) * 8)) & 0xff))
#endif

#ifdef QEMU_GENERATE
#define SETBYTE_FUNC(X) \
    _Generic((X), TCGv_i32 : gen_set_byte, TCGv_i64 : gen_set_byte_i64)
#define fSETBYTE(N, DST, VAL) SETBYTE_FUNC(DST)(N, DST, VAL)

#define fGETHALF(N, SRC)  gen_get_half(HALF, N, SRC, true)
#define fGETUHALF(N, SRC) gen_get_half(HALF, N, SRC, false)

#define SETHALF_FUNC(X) \
    _Generic((X), TCGv_i32 : gen_set_half, TCGv_i64 : gen_set_half_i64)
#define fSETHALF(N, DST, VAL) SETHALF_FUNC(DST)(N, DST, VAL)
#define fSETHALFw(N, DST, VAL) gen_set_half(N, DST, VAL)
#define fSETHALFd(N, DST, VAL) gen_set_half_i64(N, DST, VAL)
#else
#define fSETBYTE(N, DST, VAL) \
    do { \
        DST = (DST & ~(0x0ffLL << ((N) * 8))) | \
        (((size8u_t)((VAL) & 0x0ffLL)) << ((N) * 8)); \
    } while (0)
#define fGETHALF(N, SRC) ((size2s_t)((SRC >> ((N) * 16)) & 0xffff))
#define fGETUHALF(N, SRC) ((size2u_t)((SRC >> ((N) * 16)) & 0xffff))
#define fSETHALF(N, DST, VAL) \
    do { \
        DST = (DST & ~(0x0ffffLL << ((N) * 16))) | \
        (((size8u_t)((VAL) & 0x0ffff)) << ((N) * 16)); \
    } while (0)
#define fSETHALFw fSETHALF
#define fSETHALFd fSETHALF
#endif

#ifdef QEMU_GENERATE
#define GETWORD_FUNC(X) \
    _Generic((X), TCGv_i32 : gen_get_word, TCGv_i64 : gen_get_word_i64)
#define fGETWORD(N, SRC)  GETWORD_FUNC(WORD)(WORD, N, SRC, true)
#define fGETUWORD(N, SRC) GETWORD_FUNC(WORD)(WORD, N, SRC, false)
#else
#define fGETWORD(N, SRC) \
    ((size8s_t)((size4s_t)((SRC >> ((N) * 32)) & 0x0ffffffffLL)))
#define fGETUWORD(N, SRC) \
    ((size8u_t)((size4u_t)((SRC >> ((N) * 32)) & 0x0ffffffffLL)))
#endif

#define fSETWORD(N, DST, VAL) \
    do { \
        DST = (DST & ~(0x0ffffffffLL << ((N) * 32))) | \
              (((VAL) & 0x0ffffffffLL) << ((N) * 32)); \
    } while (0)
#define fACC()
#define fEXTENSION_AUDIO(A) A

#ifdef QEMU_GENERATE
#define fSETBIT(N, DST, VAL) gen_set_bit((N), (DST), (VAL));
#else
#define fSETBIT(N, DST, VAL) \
    do { \
        DST = (DST & ~(1ULL << (N))) | (((size8u_t)(VAL)) << (N)); \
    } while (0)
#endif

#define fGETBIT(N, SRC) (((SRC) >> N) & 1)
#define fSETBITS(HI, LO, DST, VAL) \
    do { \
        int j; \
        for (j = LO; j <= HI; j++) { \
            fSETBIT(j, DST, VAL); \
        } \
    } while (0)
#define fCOUNTONES_2(VAL) count_ones_2(VAL)
#define fCOUNTONES_4(VAL) count_ones_4(VAL)
#define fCOUNTONES_8(VAL) count_ones_8(VAL)
#define fBREV_8(VAL) reverse_bits_8(VAL)
#define fBREV_4(VAL) reverse_bits_4(VAL)
#define fBREV_2(VAL) reverse_bits_2(VAL)
#define fBREV_1(VAL) reverse_bits_1(VAL)
#define fCL1_8(VAL) count_leading_ones_8(VAL)
#define fCL1_4(VAL) count_leading_ones_4(VAL)
#define fCL1_2(VAL) count_leading_ones_2(VAL)
#define fCL1_1(VAL) count_leading_ones_1(VAL)
#define fINTERLEAVE(ODD, EVEN) interleave(ODD, EVEN)
#define fDEINTERLEAVE(MIXED) deinterleave(MIXED)
#define fNORM16(VAL) \
    ((VAL == 0) ? (31) : (fMAX(fCL1_2(VAL), fCL1_2(~VAL)) - 1))
#define fHIDE(A) A
#define fCONSTLL(A) A##LL
#define fCONSTULL(A) A##ULL
#define fECHO(A) (A)

#define fDO_TRACE(SREG)
#define fBREAK()
#define fGP_DOCHKPAGECROSS(BASE, SUM)
#define fDOCHKPAGECROSS(BASE, SUM)
#define fPAUSE(IMM)
#define fTRAP(TRAPTYPE, IMM) helper_raise_exception(env, HEX_EXCP_TRAP0)

#define fALIGN_REG_FIELD_VALUE(FIELD, VAL) \
    ((VAL) << reg_field_info[FIELD].offset)
#define fGET_REG_FIELD_MASK(FIELD) \
    (((1 << reg_field_info[FIELD].width) - 1) << reg_field_info[FIELD].offset)
#define fLOG_REG_FIELD(REG, FIELD, VAL)
#define fWRITE_GLOBAL_REG_FIELD(REG, FIELD, VAL)
#define fLOG_GLOBAL_REG_FIELD(REG, FIELD, VAL)
#define fREAD_REG_FIELD(REG, FIELD) \
    fEXTRACTU_BITS(env->gpr[HEX_REG_##REG], \
                   reg_field_info[FIELD].width, \
                   reg_field_info[FIELD].offset)
#define fREAD_GLOBAL_REG_FIELD(REG, FIELD)
#define fGET_FIELD(VAL, FIELD)
#define fSET_FIELD(VAL, FIELD, NEWVAL)
#define fPOW2_HELP_ROUNDUP(VAL) \
    ((VAL) | \
     ((VAL) >> 1) | \
     ((VAL) >> 2) | \
     ((VAL) >> 4) | \
     ((VAL) >> 8) | \
     ((VAL) >> 16))
#define fPOW2_ROUNDUP(VAL) (fPOW2_HELP_ROUNDUP((VAL) - 1) + 1)
#define fBARRIER()
#define fSYNCH()
#define fISYNC()
#define fICFETCH(REG)
#define fDCFETCH(REG) do { REG = REG; } while (0) /* Nothing to do in qemu */
#define fICINVIDX(REG)
#define fICINVA(REG) do { REG = REG; } while (0) /* Nothing to do in qemu */
#define fICKILL()
#define fDCKILL()
#define fL2KILL()
#define fL2UNLOCK()
#define fL2CLEAN()
#define fL2CLEANINV()
#define fL2CLEANPA(REG)
#define fL2CLEANINVPA(REG)
#define fL2CLEANINVIDX(REG)
#define fL2CLEANIDX(REG)
#define fL2INVIDX(REG)
#define fL2FETCH(ADDR, HEIGHT, WIDTH, STRIDE, FLAGS)
#define fL2TAGR(INDEX, DST, DSTREG)
#define fL2LOCKA(VA, DST, PREGDST)
#define fL2UNLOCKA(VA)
#define fL2TAGW(INDEX, PART2)
#define fDCCLEANIDX(REG)
#define fDCCLEANA(REG) do { REG = REG; } while (0) /* Nothing to do in qemu */
#define fDCCLEANINVIDX(REG)
#define fDCCLEANINVA(REG) \
    do { REG = REG; } while (0) /* Nothing to do in qemu */

#ifdef QEMU_GENERATE
#define fDCZEROA(REG) tcg_gen_mov_tl(hex_dczero_addr, (REG))
#else
#define fDCZEROA(REG) do { REG = REG; g_assert_not_reached(); } while (0)
#endif

#define fDCINVIDX(REG)
#define fDCINVA(REG) do { REG = REG; } while (0) /* Nothing to do in qemu */
#define fBRANCH_SPECULATED_RIGHT(JC, SD, DOTNEWVAL) \
    (((JC) ^ (SD) ^ (DOTNEWVAL & 1)) & 0x1)
#define fBRANCH_SPECULATE_STALL(DOTNEWVAL, JUMP_COND, SPEC_DIR, HINTBITNUM, \
                                STRBITNUM) /* Nothing */

#define IV1DEAD()
#define fVIRTINSN_SPSWAP(IMM, REG)
#define fVIRTINSN_GETIE(IMM, REG) { REG = 0xdeafbeef; }
#define fVIRTINSN_SETIE(IMM, REG)
#define fVIRTINSN_RTE(IMM, REG)
#define fTRAP1_VIRTINSN(IMM) \
    (((IMM) == 1) || ((IMM) == 3) || ((IMM) == 4) || ((IMM) == 6))
#define fNOP_EXECUTED
#define fPREDUSE_TIMING()

#endif
