#ifndef GEN_ICOUNT_H
#define GEN_ICOUNT_H

#include "exec/exec-all.h"

/* Helpers for instruction counting code generation.  */

static TCGOp *icount_start_insn;

static inline void gen_io_start(void)
{
    tcg_gen_st_i32(tcg_constant_i32(1), cpu_env,
                   offsetof(ArchCPU, parent_obj.can_do_io) -
                   offsetof(ArchCPU, env));
}

static inline void gen_tb_start(const TranslationBlock *tb)
{
    TCGv_i32 count = tcg_temp_new_i32();

    tcg_gen_ld_i32(count, cpu_env,
                   offsetof(ArchCPU, neg.icount_decr.u32) -
                   offsetof(ArchCPU, env));

    if (tb_cflags(tb) & CF_USE_ICOUNT) {
        /*
         * We emit a sub with a dummy immediate argument. Keep the insn index
         * of the sub so that we later (when we know the actual insn count)
         * can update the argument with the actual insn count.
         */
        tcg_gen_sub_i32(count, count, tcg_constant_i32(0));
        icount_start_insn = tcg_last_op();
    }

    /*
     * Emit the check against icount_decr.u32 to see if we should exit
     * unless we suppress the check with CF_NOIRQ. If we are using
     * icount and have suppressed interruption the higher level code
     * should have ensured we don't run more instructions than the
     * budget.
     */
    if (tb_cflags(tb) & CF_NOIRQ) {
        tcg_ctx->exitreq_label = NULL;
    } else {
        tcg_ctx->exitreq_label = gen_new_label();
        tcg_gen_brcondi_i32(TCG_COND_LT, count, 0, tcg_ctx->exitreq_label);
    }

    if (tb_cflags(tb) & CF_USE_ICOUNT) {
        tcg_gen_st16_i32(count, cpu_env,
                         offsetof(ArchCPU, neg.icount_decr.u16.low) -
                         offsetof(ArchCPU, env));
        /*
         * cpu->can_do_io is cleared automatically here at the beginning of
         * each translation block.  The cost is minimal and only paid for
         * -icount, plus it would be very easy to forget doing it in the
         * translator. Doing it here means we don't need a gen_io_end() to
         * go with gen_io_start().
         */
        tcg_gen_st_i32(tcg_constant_i32(0), cpu_env,
                       offsetof(ArchCPU, parent_obj.can_do_io) -
                       offsetof(ArchCPU, env));
    }
}

static inline void gen_tb_end(const TranslationBlock *tb, int num_insns)
{
    if (tb_cflags(tb) & CF_USE_ICOUNT) {
        /*
         * Update the num_insn immediate parameter now that we know
         * the actual insn count.
         */
        tcg_set_insn_param(icount_start_insn, 2,
                           tcgv_i32_arg(tcg_constant_i32(num_insns)));
    }

    if (tcg_ctx->exitreq_label) {
        gen_set_label(tcg_ctx->exitreq_label);
        tcg_gen_exit_tb(tb, TB_EXIT_REQUESTED);
    }
}

#endif
