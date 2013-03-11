#ifndef GEN_ICOUNT_H
#define GEN_ICOUNT_H 1

#include "qemu/timer.h"

/* Helpers for instruction counting code generation.  */

static TCGArg *icount_arg;
static int icount_label;
static int exitreq_label;

static inline void gen_tb_start(void)
{
    TCGv_i32 count;
    TCGv_i32 flag;

    exitreq_label = gen_new_label();
    flag = tcg_temp_new_i32();
    tcg_gen_ld_i32(flag, cpu_env,
                   offsetof(CPUState, tcg_exit_req) - ENV_OFFSET);
    tcg_gen_brcondi_i32(TCG_COND_NE, flag, 0, exitreq_label);
    tcg_temp_free_i32(flag);

    if (!use_icount)
        return;

    icount_label = gen_new_label();
    count = tcg_temp_local_new_i32();
    tcg_gen_ld_i32(count, cpu_env, offsetof(CPUArchState, icount_decr.u32));
    /* This is a horrid hack to allow fixing up the value later.  */
    icount_arg = tcg_ctx.gen_opparam_ptr + 1;
    tcg_gen_subi_i32(count, count, 0xdeadbeef);

    tcg_gen_brcondi_i32(TCG_COND_LT, count, 0, icount_label);
    tcg_gen_st16_i32(count, cpu_env, offsetof(CPUArchState, icount_decr.u16.low));
    tcg_temp_free_i32(count);
}

static void gen_tb_end(TranslationBlock *tb, int num_insns)
{
    gen_set_label(exitreq_label);
    tcg_gen_exit_tb((tcg_target_long)tb + TB_EXIT_REQUESTED);

    if (use_icount) {
        *icount_arg = num_insns;
        gen_set_label(icount_label);
        tcg_gen_exit_tb((tcg_target_long)tb + TB_EXIT_ICOUNT_EXPIRED);
    }
}

static inline void gen_io_start(void)
{
    TCGv_i32 tmp = tcg_const_i32(1);
    tcg_gen_st_i32(tmp, cpu_env, offsetof(CPUArchState, can_do_io));
    tcg_temp_free_i32(tmp);
}

static inline void gen_io_end(void)
{
    TCGv_i32 tmp = tcg_const_i32(0);
    tcg_gen_st_i32(tmp, cpu_env, offsetof(CPUArchState, can_do_io));
    tcg_temp_free_i32(tmp);
}

#endif
