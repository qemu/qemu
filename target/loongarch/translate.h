/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch translation routines.
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#ifndef TARGET_LOONGARCH_TRANSLATE_H
#define TARGET_LOONGARCH_TRANSLATE_H

#include "exec/translator.h"

#define TRANS(NAME, AVAIL, FUNC, ...) \
    static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
    { return avail_##AVAIL(ctx) && FUNC(ctx, a, __VA_ARGS__); }

#define TRANS64(NAME, AVAIL, FUNC, ...) \
    static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
    { return avail_64(ctx) && avail_##AVAIL(ctx) && FUNC(ctx, a, __VA_ARGS__); }

#define avail_ALL(C)   true
#define avail_64(C)    (FIELD_EX32((C)->cpucfg1, CPUCFG1, ARCH) == \
                        CPUCFG1_ARCH_LA64)
#define avail_FP(C)     (FIELD_EX32((C)->cpucfg2, CPUCFG2, FP))
#define avail_FP_SP(C)  (FIELD_EX32((C)->cpucfg2, CPUCFG2, FP_SP))
#define avail_FP_DP(C)  (FIELD_EX32((C)->cpucfg2, CPUCFG2, FP_DP))
#define avail_LSPW(C)   (FIELD_EX32((C)->cpucfg2, CPUCFG2, LSPW))
#define avail_LAM(C)    (FIELD_EX32((C)->cpucfg2, CPUCFG2, LAM))
#define avail_LAM_BH(C) (FIELD_EX32((C)->cpucfg2, CPUCFG2, LAM_BH))
#define avail_LAMCAS(C) (FIELD_EX32((C)->cpucfg2, CPUCFG2, LAMCAS))
#define avail_LSX(C)    (FIELD_EX32((C)->cpucfg2, CPUCFG2, LSX))
#define avail_LASX(C)   (FIELD_EX32((C)->cpucfg2, CPUCFG2, LASX))
#define avail_SCQ(C)    (FIELD_EX32((C)->cpucfg2, CPUCFG2, SCQ))
#define avail_IOCSR(C)  (FIELD_EX32((C)->cpucfg1, CPUCFG1, IOCSR))
#define avail_CRC(C)    (FIELD_EX32((C)->cpucfg1, CPUCFG1, CRC))

#define avail_FRECIPE(C) (FIELD_EX32((C)->cpucfg2, CPUCFG2, FRECIPE))
#define avail_FRECIPE_FP_SP(C) (avail_FRECIPE(C) && avail_FP_SP(C))
#define avail_FRECIPE_FP_DP(C) (avail_FRECIPE(C) && avail_FP_DP(C))
#define avail_FRECIPE_LSX(C)   (avail_FRECIPE(C) && avail_LSX(C))
#define avail_FRECIPE_LASX(C)   (avail_FRECIPE(C) && avail_LASX(C))

#define avail_LLACQ_SCREL(C)    (FIELD_EX32((C)->cpucfg2, CPUCFG2, LLACQ_SCREL))
#define avail_LLACQ_SCREL_64(C) (avail_64(C) && avail_LLACQ_SCREL(C))

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
    target_ulong page_start;
    uint32_t opcode;
    uint16_t mem_idx;
    uint16_t plv;
    int vl;   /* Vector length */
    TCGv zero;
    bool la64; /* LoongArch64 mode */
    bool va32; /* 32-bit virtual address */
    uint32_t cpucfg1;
    uint32_t cpucfg2;
} DisasContext;

void generate_exception(DisasContext *ctx, int excp);

extern TCGv cpu_gpr[32], cpu_pc;
extern TCGv_i32 cpu_fscr0;
extern TCGv_i64 cpu_fpr[32];

#endif
