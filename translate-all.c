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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "config.h"

#define IN_OP_I386
#include "cpu.h"
#include "exec-all.h"
#include "disas.h"

enum {
#define DEF(s, n, copy_size) INDEX_op_ ## s,
#include "opc.h"
#undef DEF
    NB_OPS,
};

#include "dyngen.h"
#include "op.h"

uint16_t gen_opc_buf[OPC_BUF_SIZE];
uint32_t gen_opparam_buf[OPPARAM_BUF_SIZE];
uint32_t gen_opc_pc[OPC_BUF_SIZE];
uint8_t gen_opc_instr_start[OPC_BUF_SIZE];
#if defined(TARGET_I386)
uint8_t gen_opc_cc_op[OPC_BUF_SIZE];
#endif

#ifdef DEBUG_DISAS
static const char *op_str[] = {
#define DEF(s, n, copy_size) #s,
#include "opc.h"
#undef DEF
};

static uint8_t op_nb_args[] = {
#define DEF(s, n, copy_size) n,
#include "opc.h"
#undef DEF
};

void dump_ops(const uint16_t *opc_buf, const uint32_t *opparam_buf)
{
    const uint16_t *opc_ptr;
    const uint32_t *opparam_ptr;
    int c, n, i;

    opc_ptr = opc_buf;
    opparam_ptr = opparam_buf;
    for(;;) {
        c = *opc_ptr++;
        n = op_nb_args[c];
        fprintf(logfile, "0x%04x: %s", 
                (int)(opc_ptr - opc_buf - 1), op_str[c]);
        for(i = 0; i < n; i++) {
            fprintf(logfile, " 0x%x", opparam_ptr[i]);
        }
        fprintf(logfile, "\n");
        if (c == INDEX_op_end)
            break;
        opparam_ptr += n;
    }
}

#endif

/* return non zero if the very first instruction is invalid so that
   the virtual CPU can trigger an exception. 

   '*gen_code_size_ptr' contains the size of the generated code (host
   code).
*/
int cpu_gen_code(CPUState *env, TranslationBlock *tb,
                 int max_code_size, int *gen_code_size_ptr)
{
    uint8_t *gen_code_buf;
    int gen_code_size;

    if (gen_intermediate_code(env, tb) < 0)
        return -1;

    /* generate machine code */
    tb->tb_next_offset[0] = 0xffff;
    tb->tb_next_offset[1] = 0xffff;
    gen_code_buf = tb->tc_ptr;
#ifdef USE_DIRECT_JUMP
    /* the following two entries are optional (only used for string ops) */
    tb->tb_jmp_offset[2] = 0xffff;
    tb->tb_jmp_offset[3] = 0xffff;
#endif
    gen_code_size = dyngen_code(gen_code_buf, tb->tb_next_offset,
#ifdef USE_DIRECT_JUMP
                                tb->tb_jmp_offset,
#else
                                NULL,
#endif
                                gen_opc_buf, gen_opparam_buf);
    *gen_code_size_ptr = gen_code_size;
#ifdef DEBUG_DISAS
    if (loglevel) {
        fprintf(logfile, "OUT: [size=%d]\n", *gen_code_size_ptr);
        disas(logfile, gen_code_buf, *gen_code_size_ptr, 1, 0);
        fprintf(logfile, "\n");
        fflush(logfile);
    }
#endif
    return 0;
}

static const unsigned short opc_copy_size[] = {
#define DEF(s, n, copy_size) copy_size,
#include "opc.h"
#undef DEF
};

/* The cpu state corresponding to 'searched_pc' is restored. 
 */
int cpu_restore_state(TranslationBlock *tb, 
                      CPUState *env, unsigned long searched_pc)
{
    int j, c;
    unsigned long tc_ptr;
    uint16_t *opc_ptr;

    if (gen_intermediate_code_pc(env, tb) < 0)
        return -1;
    
    /* find opc index corresponding to search_pc */
    tc_ptr = (unsigned long)tb->tc_ptr;
    if (searched_pc < tc_ptr)
        return -1;
    j = 0;
    opc_ptr = gen_opc_buf;
    for(;;) {
        c = *opc_ptr;
        if (c == INDEX_op_end)
            return -1;
        tc_ptr += opc_copy_size[c];
        if (searched_pc < tc_ptr)
            break;
        opc_ptr++;
    }
    j = opc_ptr - gen_opc_buf;
    /* now find start of instruction before */
    while (gen_opc_instr_start[j] == 0)
        j--;
#if defined(TARGET_I386)
    {
        int cc_op;
#ifdef DEBUG_DISAS
        if (loglevel) {
            int i;
            fprintf(logfile, "RESTORE:\n");
            for(i=0;i<=j; i++) {
                if (gen_opc_instr_start[i]) {
                    fprintf(logfile, "0x%04x: 0x%08x\n", i, gen_opc_pc[i]);
                }
            }
            fprintf(logfile, "spc=0x%08lx j=0x%x eip=0x%lx cs_base=%lx\n", 
                    searched_pc, j, gen_opc_pc[j] - tb->cs_base, tb->cs_base);
        }
#endif
        env->eip = gen_opc_pc[j] - tb->cs_base;
        cc_op = gen_opc_cc_op[j];
        if (cc_op != CC_OP_DYNAMIC)
            env->cc_op = cc_op;
    }
#elif defined(TARGET_ARM)
    env->regs[15] = gen_opc_pc[j];
#elif defined(TARGET_SPARC)
    env->pc = gen_opc_pc[j];
#elif defined(TARGET_PPC)
    env->nip = gen_opc_pc[j];
#endif
    return 0;
}
