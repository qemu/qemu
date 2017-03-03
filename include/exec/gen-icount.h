#ifndef GEN_ICOUNT_H
#define GEN_ICOUNT_H

#include "qemu/timer.h"

/* Helpers for instruction counting code generation.  */

static int icount_start_insn_idx;
static TCGLabel *exitreq_label;

static inline void gen_tb_start(TranslationBlock *tb)
{
    TCGv_i32 count, imm;

    exitreq_label = gen_new_label();
    if (tb->cflags & CF_USE_ICOUNT) {
        count = tcg_temp_local_new_i32();
    } else {
        count = tcg_temp_new_i32();
    }

    tcg_gen_ld_i32(count, cpu_env,
                   -ENV_OFFSET + offsetof(CPUState, icount_decr.u32));

    if (tb->cflags & CF_USE_ICOUNT) {
        imm = tcg_temp_new_i32();
        /* We emit a movi with a dummy immediate argument. Keep the insn index
         * of the movi so that we later (when we know the actual insn count)
         * can update the immediate argument with the actual insn count.  */
        icount_start_insn_idx = tcg_op_buf_count();
        tcg_gen_movi_i32(imm, 0xdeadbeef);

        tcg_gen_sub_i32(count, count, imm);
        tcg_temp_free_i32(imm);
    }

    tcg_gen_brcondi_i32(TCG_COND_LT, count, 0, exitreq_label);

    if (tb->cflags & CF_USE_ICOUNT) {
        tcg_gen_st16_i32(count, cpu_env,
                         -ENV_OFFSET + offsetof(CPUState, icount_decr.u16.low));
    }

    tcg_temp_free_i32(count);
}

static void gen_tb_end(TranslationBlock *tb, int num_insns)
{
    if (tb->cflags & CF_USE_ICOUNT) {
        /* Update the num_insn immediate parameter now that we know
         * the actual insn count.  */
        tcg_set_insn_param(icount_start_insn_idx, 1, num_insns);
    }

    gen_set_label(exitreq_label);
    tcg_gen_exit_tb((uintptr_t)tb + TB_EXIT_REQUESTED);

    /* Terminate the linked list.  */
    tcg_ctx.gen_op_buf[tcg_ctx.gen_op_buf[0].prev].next = 0;
}

static inline void gen_io_start(void)
{
    TCGv_i32 tmp = tcg_const_i32(1);
    tcg_gen_st_i32(tmp, cpu_env, -ENV_OFFSET + offsetof(CPUState, can_do_io));
    tcg_temp_free_i32(tmp);
}

static inline void gen_io_end(void)
{
    TCGv_i32 tmp = tcg_const_i32(0);
    tcg_gen_st_i32(tmp, cpu_env, -ENV_OFFSET + offsetof(CPUState, can_do_io));
    tcg_temp_free_i32(tmp);
}

#endif
