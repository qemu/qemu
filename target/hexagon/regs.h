/* Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved. */


/*
 * There are 32 general user regs and up to 32 user control regs.
 */


#ifndef _REGS_H
#define _REGS_H

#include "arch_types.h"

#define DEF_SUBSYS_REG(NAME, DESC, OFFSET)

#define NUM_GEN_REGS 32
#define NUM_PREGS 4
/* user + guest + per-thread supervisor + A regs */
#define NUM_PER_THREAD_CR (32 + 32 + 16 + 48)
#define TOTAL_PER_THREAD_REGS 64
#define NUM_GLOBAL_REGS (128 + 32) /* + A regs */

enum regs_enum {
#define DEF_REG_MUTABILITY(REG, MASK)
#define DEF_GLOBAL_REG_MUTABILITY(REG, MASK)
#define DEF_REG_FIELD(TAG, NAME, START, WIDTH, DESCRIPTION)
#define DEF_GLOBAL_REG(TAG, NAME, SYMBOL, NUM, OFFSET)
#define DEF_MMAP_REG(TAG, NAME, SYMBOL, NUM, OFFSET)
#define DEF_REG(TAG, NAME, SYMBOL, NUM, OFFSET) \
    TAG = OFFSET,
#include "regs.def"
#undef DEF_REG_MUTABILITY
#undef DEF_GLOBAL_REG_MUTABILITY
#undef DEF_REG_FIELD
#undef DEF_GLOBAL_REG
#undef DEF_MMAP_REG
#undef DEF_REG
};

typedef struct {
    const char *name;
    int offset;
    int width;
    const char *description;
} reg_field_t;

extern reg_field_t reg_field_info[];


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

enum global_regs_enum {
#define DEF_REG_MUTABILITY(REG, MASK)
#define DEF_GLOBAL_REG_MUTABILITY(REG, MASK)
#define DEF_REG_FIELD(TAG, NAME, START, WIDTH, DESCRIPTION)
#define DEF_REG(TAG, NAME, SYMBOL, NUM, OFFSET)
#define DEF_MMAP_REG(TAG, NAME, SYMBOL, NUM, OFFSET)
#define DEF_GLOBAL_REG(TAG, NAME, SYMBOL, NUM, OFFSET) \
    TAG = OFFSET,
#include "regs.def"
    END_GLOBAL_REGS
};

#undef DEF_REG
#undef DEF_MMAP_REG
#undef DEF_GLOBAL_REG
#undef DEF_REG_FIELD
#undef DEF_REG_MUTABILITY
#undef DEF_GLOBAL_REG_MUTABILITY


enum reg_fields_enum {
#define DEF_REG_MUTABILITY(REG, MASK)
#define DEF_GLOBAL_REG_MUTABILITY(REG, MASK)
#define DEF_REG(TAG, NAME, SYMBOL, NUM, OFFSET)
#define DEF_GLOBAL_REG(TAG, NAME, SYMBOL, NUM, OFFSET)
#define DEF_MMAP_REG(TAG, NAME, SYMBOL, NUM, OFFSET)
#define DEF_REG_FIELD(TAG, NAME, START, WIDTH, DESCRIPTION) \
    TAG,
#include "regs.def"
    NUM_REG_FIELDS
};

#undef DEF_REG
#undef DEF_GLOBAL_REG
#undef DEF_MMAP_REG
#undef DEF_REG_FIELD
#undef DEF_REG_MUTABILITY
#undef DEF_GLOBAL_REG_MUTABILITY

#ifdef QEMU_GENERATE
#define DECL_RREG(TYPE, NAME, NUM, X, OFF) \
    TYPE NAME = tcg_temp_local_new(); \
    int NUM = REGNO(X) + OFF
#define DECL_RREG_WRITABLE(TYPE, NAME, NUM, X, OFF) \
    TYPE NAME = tcg_temp_local_new(); \
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
#define DECL_RREG_READONLY(TYPE, NAME, NUM, X, OFF) \
    TYPE NAME; \
    int NUM = REGNO(X) + OFF

#define DECL_RREG_d(TYPE, NAME, NUM, X, OFF) \
    DECL_RREG_WRITABLE(TYPE, NAME, NUM, X, OFF)
#define DECL_RREG_e(TYPE, NAME, NUM, X, OFF) \
    DECL_RREG(TYPE, NAME, NUM, X, OFF)
#define DECL_RREG_s(TYPE, NAME, NUM, X, OFF) \
    DECL_RREG_READONLY(TYPE, NAME, NUM, X, OFF)
#define DECL_RREG_t(TYPE, NAME, NUM, X, OFF) \
    DECL_RREG_READONLY(TYPE, NAME, NUM, X, OFF)
#define DECL_RREG_u(TYPE, NAME, NUM, X, OFF) \
    DECL_RREG_READONLY(TYPE, NAME, NUM, X, OFF)
#define DECL_RREG_v(TYPE, NAME, NUM, X, OFF) \
    DECL_RREG_READONLY(TYPE, NAME, NUM, X, OFF)
#define DECL_RREG_x(TYPE, NAME, NUM, X, OFF) \
    DECL_RREG_WRITABLE(TYPE, NAME, NUM, X, OFF)
#define DECL_RREG_y(TYPE, NAME, NUM, X, OFF) \
    DECL_RREG_WRITABLE(TYPE, NAME, NUM, X, OFF)

#define DECL_PREG(TYPE, NAME, NUM, X, OFF) \
    TYPE NAME = tcg_temp_local_new(); \
    int NUM = REGNO(X) + OFF
/*
 * For read-only temps, avoid allocating and freeing
 */
#define DECL_PREG_READONLY(TYPE, NAME, NUM, X, OFF) \
    TYPE NAME; \
    int NUM = REGNO(X) + OFF

#define DECL_PREG_d(TYPE, NAME, NUM, X, OFF) \
    DECL_PREG(TYPE, NAME, NUM, X, OFF)
#define DECL_PREG_e(TYPE, NAME, NUM, X, OFF) \
    DECL_PREG(TYPE, NAME, NUM, X, OFF)
#define DECL_PREG_s(TYPE, NAME, NUM, X, OFF) \
    DECL_PREG_READONLY(TYPE, NAME, NUM, X, OFF)
#define DECL_PREG_t(TYPE, NAME, NUM, X, OFF) \
    DECL_PREG_READONLY(TYPE, NAME, NUM, X, OFF)
#define DECL_PREG_u(TYPE, NAME, NUM, X, OFF) \
    DECL_PREG_READONLY(TYPE, NAME, NUM, X, OFF)
#define DECL_PREG_v(TYPE, NAME, NUM, X, OFF) \
    DECL_PREG_READONLY(TYPE, NAME, NUM, X, OFF)
#define DECL_PREG_x(TYPE, NAME, NUM, X, OFF) \
    DECL_PREG(TYPE, NAME, NUM, X, OFF)
#define DECL_PREG_y(TYPE, NAME, NUM, X, OFF) \
    DECL_PREG(TYPE, NAME, NUM, X, OFF)

#define DECL_CREG(TYPE, NAME, NUM, X, OFF) \
    TYPE NAME = tcg_temp_local_new(); \
    int NUM = REGNO(X) + OFF

#define DECL_CREG_d(TYPE, NAME, NUM, X, OFF) \
    DECL_CREG(TYPE, NAME, NUM, X, OFF)
#define DECL_CREG_s(TYPE, NAME, NUM, X, OFF) \
    DECL_CREG(TYPE, NAME, NUM, X, OFF)

#define DECL_MREG(TYPE, NAME, NUM, X, OFF) \
    TYPE NAME = tcg_temp_local_new(); \
    int NUM = REGNO(X) + OFF
/*
 * For read-only temps, avoid allocating and freeing
 */
#define DECL_MREG_READONLY(TYPE, NAME, NUM, X, OFF) \
    TYPE NAME; \
    int NUM = REGNO(X) + OFF

#define DECL_MREG_u(TYPE, NAME, NUM, X, OFF) \
    DECL_MREG(TYPE, NAME, NUM, X, OFF)

#define DECL_NEW_NREG(TYPE, NAME, NUM, X, OFF) \
    TYPE NAME; \
    int NUM = REGNO(X) + OFF

#define DECL_NEW_PREG(TYPE, NAME, NUM, X, OFF) \
    TYPE NAME; \
    int NUM = REGNO(X) + OFF

#define DECL_PAIR(TYPE, NAME, NUM, X, OFF) \
    TYPE NAME = tcg_temp_local_new_i64(); \
    size1u_t NUM = REGNO(X) + OFF
#define DECL_PAIR_WRITABLE(TYPE, NAME, NUM, X, OFF) \
    TYPE NAME = tcg_temp_local_new_i64(); \
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

#define DECL_PAIR_dd(TYPE, NAME, NUM, X, OFF) \
    DECL_PAIR_WRITABLE(TYPE, NAME, NUM, X, OFF)
#define DECL_PAIR_ss(TYPE, NAME, NUM, X, OFF) \
    DECL_PAIR(TYPE, NAME, NUM, X, OFF)
#define DECL_PAIR_tt(TYPE, NAME, NUM, X, OFF) \
    DECL_PAIR(TYPE, NAME, NUM, X, OFF)
#define DECL_PAIR_xx(TYPE, NAME, NUM, X, OFF) \
    DECL_PAIR_WRITABLE(TYPE, NAME, NUM, X, OFF)
#define DECL_PAIR_yy(TYPE, NAME, NUM, X, OFF) \
    DECL_PAIR_WRITABLE(TYPE, NAME, NUM, X, OFF)

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

#define FREE_REG_d(NAME)            FREE_REG(NAME)
#define FREE_REG_e(NAME)            FREE_REG(NAME)
#define FREE_REG_s(NAME)            FREE_REG_READONLY(NAME)
#define FREE_REG_t(NAME)            FREE_REG_READONLY(NAME)
#define FREE_REG_u(NAME)            FREE_REG_READONLY(NAME)
#define FREE_REG_v(NAME)            FREE_REG_READONLY(NAME)
#define FREE_REG_x(NAME)            FREE_REG(NAME)
#define FREE_REG_y(NAME)            FREE_REG(NAME)

#define DECL_NEW(TYPE, NAME, NUM, X, OFF) \
    TYPE NAME = tcg_temp_local_new(); \
    int NUM = REGNO(X) + OFF

#define DECL_IMM(NAME, X) \
    TCGv NAME = tcg_const_tl(IMMNO(X))

#define DECL_EA \
    TCGv EA = tcg_temp_local_new()

#define FREE_REG(NAME) \
    tcg_temp_free(NAME)

#define FREE_NEW_NREG(NAME) \
    tcg_temp_free(NAME)

#define FREE_NEW_PREG(NAME) \
    /* Nothing */

#define FREE_REG_PAIR(NAME) \
    tcg_temp_free_i64(NAME)

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
        TCGv slot = tcg_const_tl(insn->is_endloop ? 4 : insn->slot); \
        CODE; \
        tcg_temp_free(slot); \
    } while (0)

#define PART1_WRAP(CODE) \
    do { \
        TCGv part1 = tcg_const_tl(insn->part1); \
        CODE; \
        tcg_temp_free(part1); \
    } while (0)

/*
 * FIXME - Writing more than one late pred in the same packet should
 *         raise an exception
 */
#define MARK_LATE_PRED_WRITE(RNUM) {}

#define REGNO(NUM) (insn->regno[NUM])
#define IMMNO(NUM) (insn->immed[NUM])

#ifdef QEMU_GENERATE
#define READ_RREG(dest, NUM) \
    gen_read_rreg(dest, NUM)
#define READ_RREG_READONLY(dest, NUM) \
    do { dest = hex_gpr[NUM]; } while (0)

#define READ_RREG_s(dest, NUM) \
    READ_RREG_READONLY(dest, NUM)
#define READ_RREG_t(dest, NUM) \
    READ_RREG_READONLY(dest, NUM)
#define READ_RREG_u(dest, NUM) \
    READ_RREG_READONLY(dest, NUM)
#define READ_RREG_x(dest, NUM) \
    READ_RREG(dest, NUM)
#define READ_RREG_y(dest, NUM) \
    READ_RREG(dest, NUM)

#ifdef QEMU_GENERATE
#define READ_CREG_s(dest, NUM) \
    do { \
        if ((NUM) + HEX_REG_SA0 == HEX_REG_P3_0) { \
            gen_read_p3_0(dest); \
        } else { \
            READ_RREG_READONLY(dest, ((NUM) + HEX_REG_SA0)); \
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
            READ_RREG_READONLY(dest, ((NUM) + HEX_REG_SA0)); \
        } \
    } while (0)
#endif
/* HACK since circular addressing not implemented */
#define READ_MREG_u(dest, NUM) \
    do { \
        READ_RREG_READONLY(dest, ((NUM) + HEX_REG_M0)); \
        dest = dest; \
    } while (0)
#else
#define READ_RREG(NUM) \
    (env->gpr[(NUM)])
#define READ_MREG(NUM) \
    (env->gpr[NUM + REG_M])
#define READ_CSREG(NUM) \
    (env->gpr[NUM + REG_CSA])
#endif

#ifdef QEMU_GENERATE
#define READ_RREG_PAIR(tmp, NUM) \
    tcg_gen_concat_i32_i64(tmp, hex_gpr[NUM], hex_gpr[(NUM) + 1])
#define READ_CREG_PAIR(tmp, i) \
    READ_RREG_PAIR(tmp, ((i) + HEX_REG_SA0))
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

#define READ_NEW_NREG(tmp, i)    (tmp = tcg_const_tl(i))
#else
#define READ_PREG(NUM)                (env->pred[NUM])
#endif

#define WRITE_RREG(NUM, VAL) LOG_REG_WRITE(NUM, VAL)
#define WRITE_PREG(NUM, VAL) LOG_PRED_WRITE(NUM, VAL)

#ifdef QEMU_GENERATE
#define WRITE_CREG(i, tmp) \
    do { \
        if (i + HEX_REG_SA0 == HEX_REG_P3_0) { \
            gen_write_p3_0(tmp); \
        } else { \
            WRITE_RREG((i) + HEX_REG_SA0, tmp); \
        } \
    } while (0)
#define WRITE_CREG_PAIR(i, tmp) WRITE_RREG_PAIR((i) + HEX_REG_SA0, tmp)

#define WRITE_RREG_PAIR(NUM, VAL) \
    do { \
        int is_predicated = GET_ATTRIB(insn->opcode, A_CONDEXEC); \
        gen_log_reg_write_pair(NUM, VAL, insn->slot, is_predicated); \
        ctx_log_reg_write(ctx, (NUM)); \
        ctx_log_reg_write(ctx, (NUM) + 1); \
    } while (0)
#endif

#endif
