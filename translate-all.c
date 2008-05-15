/*
 *  Host code generation
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "config.h"

#define NO_CPU_IO_DEFS
#include "cpu.h"
#include "exec-all.h"
#include "disas.h"
#include "tcg.h"

/* code generation context */
TCGContext tcg_ctx;

uint16_t gen_opc_buf[OPC_BUF_SIZE];
TCGArg gen_opparam_buf[OPPARAM_BUF_SIZE];

target_ulong gen_opc_pc[OPC_BUF_SIZE];
uint8_t gen_opc_instr_start[OPC_BUF_SIZE];
#if defined(TARGET_I386)
uint8_t gen_opc_cc_op[OPC_BUF_SIZE];
#elif defined(TARGET_SPARC)
target_ulong gen_opc_npc[OPC_BUF_SIZE];
target_ulong gen_opc_jump_pc[2];
#elif defined(TARGET_MIPS) || defined(TARGET_SH4)
uint32_t gen_opc_hflags[OPC_BUF_SIZE];
#endif

#ifdef CONFIG_PROFILER
int64_t dyngen_tb_count1;
int64_t dyngen_tb_count;
int64_t dyngen_op_count;
int64_t dyngen_old_op_count;
int64_t dyngen_tcg_del_op_count;
int dyngen_op_count_max;
int64_t dyngen_code_in_len;
int64_t dyngen_code_out_len;
int64_t dyngen_interm_time;
int64_t dyngen_code_time;
int64_t dyngen_restore_count;
int64_t dyngen_restore_time;
#endif

/* XXX: suppress that */
unsigned long code_gen_max_block_size(void)
{
    static unsigned long max;

    if (max == 0) {
        max = TCG_MAX_OP_SIZE;
#define DEF(s, n, copy_size) max = copy_size > max? copy_size : max;
#include "tcg-opc.h"
#undef DEF
        max *= OPC_MAX_SIZE;
    }

    return max;
}

void cpu_gen_init(void)
{
    tcg_context_init(&tcg_ctx); 
    tcg_set_frame(&tcg_ctx, TCG_AREG0, offsetof(CPUState, temp_buf),
                  CPU_TEMP_BUF_NLONGS * sizeof(long));
}

/* return non zero if the very first instruction is invalid so that
   the virtual CPU can trigger an exception.

   '*gen_code_size_ptr' contains the size of the generated code (host
   code).
*/
int cpu_gen_code(CPUState *env, TranslationBlock *tb, int *gen_code_size_ptr)
{
    TCGContext *s = &tcg_ctx;
    uint8_t *gen_code_buf;
    int gen_code_size;
#ifdef CONFIG_PROFILER
    int64_t ti;
#endif

#ifdef CONFIG_PROFILER
    dyngen_tb_count1++; /* includes aborted translations because of
                           exceptions */
    ti = profile_getclock();
#endif
    tcg_func_start(s);

    if (gen_intermediate_code(env, tb) < 0)
        return -1;
    
    /* generate machine code */
    gen_code_buf = tb->tc_ptr;
    tb->tb_next_offset[0] = 0xffff;
    tb->tb_next_offset[1] = 0xffff;
    s->tb_next_offset = tb->tb_next_offset;
#ifdef USE_DIRECT_JUMP
    s->tb_jmp_offset = tb->tb_jmp_offset;
    s->tb_next = NULL;
    /* the following two entries are optional (only used for string ops) */
    /* XXX: not used ? */
    tb->tb_jmp_offset[2] = 0xffff;
    tb->tb_jmp_offset[3] = 0xffff;
#else
    s->tb_jmp_offset = NULL;
    s->tb_next = tb->tb_next;
#endif

#ifdef CONFIG_PROFILER
    dyngen_tb_count++;
    dyngen_interm_time += profile_getclock() - ti;
    dyngen_code_time -= profile_getclock();
#endif
    gen_code_size = dyngen_code(s, gen_code_buf);
    *gen_code_size_ptr = gen_code_size;
#ifdef CONFIG_PROFILER
    dyngen_code_time += profile_getclock();
    dyngen_code_in_len += tb->size;
    dyngen_code_out_len += gen_code_size;
#endif

#ifdef DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_OUT_ASM) {
        fprintf(logfile, "OUT: [size=%d]\n", *gen_code_size_ptr);
        disas(logfile, tb->tc_ptr, *gen_code_size_ptr);
        fprintf(logfile, "\n");
        fflush(logfile);
    }
#endif
    return 0;
}

/* The cpu state corresponding to 'searched_pc' is restored.
 */
int cpu_restore_state(TranslationBlock *tb,
                      CPUState *env, unsigned long searched_pc,
                      void *puc)
{
    TCGContext *s = &tcg_ctx;
    int j;
    unsigned long tc_ptr;
#ifdef CONFIG_PROFILER
    int64_t ti;
#endif

#ifdef CONFIG_PROFILER
    ti = profile_getclock();
#endif
    tcg_func_start(s);

    if (gen_intermediate_code_pc(env, tb) < 0)
        return -1;

    /* find opc index corresponding to search_pc */
    tc_ptr = (unsigned long)tb->tc_ptr;
    if (searched_pc < tc_ptr)
        return -1;

    s->tb_next_offset = tb->tb_next_offset;
#ifdef USE_DIRECT_JUMP
    s->tb_jmp_offset = tb->tb_jmp_offset;
    s->tb_next = NULL;
#else
    s->tb_jmp_offset = NULL;
    s->tb_next = tb->tb_next;
#endif
    j = dyngen_code_search_pc(s, (uint8_t *)tc_ptr, searched_pc - tc_ptr);
    if (j < 0)
        return -1;
    /* now find start of instruction before */
    while (gen_opc_instr_start[j] == 0)
        j--;

    gen_pc_load(env, tb, searched_pc, j, puc);

#ifdef CONFIG_PROFILER
    dyngen_restore_time += profile_getclock() - ti;
    dyngen_restore_count++;
#endif
    return 0;
}
