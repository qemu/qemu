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

#define NO_CPU_IO_DEFS
#include "cpu.h"
#include "exec-all.h"
#include "disas.h"

extern int dyngen_code(uint8_t *gen_code_buf,
                       uint16_t *label_offsets, uint16_t *jmp_offsets,
                       const uint16_t *opc_buf, const uint32_t *opparam_buf, const long *gen_labels);

enum {
#define DEF(s, n, copy_size) INDEX_op_ ## s,
#include "opc.h"
#undef DEF
    NB_OPS,
};

uint16_t gen_opc_buf[OPC_BUF_SIZE];
uint32_t gen_opparam_buf[OPPARAM_BUF_SIZE];
long gen_labels[OPC_BUF_SIZE];
int nb_gen_labels;

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

int code_copy_enabled = 1;

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

static const unsigned short opc_copy_size[] = {
#define DEF(s, n, copy_size) copy_size,
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

/* compute label info */
static void dyngen_labels(long *gen_labels, int nb_gen_labels,
                          uint8_t *gen_code_buf, const uint16_t *opc_buf)
{
    uint8_t *gen_code_ptr;
    int c, i;
    unsigned long gen_code_addr[OPC_BUF_SIZE];

    if (nb_gen_labels == 0)
        return;
    /* compute the address of each op code */

    gen_code_ptr = gen_code_buf;
    i = 0;
    for(;;) {
        c = opc_buf[i];
        gen_code_addr[i] =(unsigned long)gen_code_ptr;
        if (c == INDEX_op_end)
            break;
        gen_code_ptr += opc_copy_size[c];
        i++;
    }

    /* compute the address of each label */
    for(i = 0; i < nb_gen_labels; i++) {
        gen_labels[i] = gen_code_addr[gen_labels[i]];
    }
}

unsigned long code_gen_max_block_size(void)
{
    static unsigned long max;

    if (max == 0) {
#define DEF(s, n, copy_size) max = copy_size > max? copy_size : max;
#include "opc.h"
#undef DEF
        max *= OPC_MAX_SIZE;
    }

    return max;
}

/* return non zero if the very first instruction is invalid so that
   the virtual CPU can trigger an exception.

   '*gen_code_size_ptr' contains the size of the generated code (host
   code).
*/
int cpu_gen_code(CPUState *env, TranslationBlock *tb, int *gen_code_size_ptr)
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
    dyngen_labels(gen_labels, nb_gen_labels, gen_code_buf, gen_opc_buf);
    
    gen_code_size = dyngen_code(gen_code_buf, tb->tb_next_offset,
#ifdef USE_DIRECT_JUMP
                                tb->tb_jmp_offset,
#else
                                NULL,
#endif
                                gen_opc_buf, gen_opparam_buf, gen_labels);
    *gen_code_size_ptr = gen_code_size;
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
        if (loglevel & CPU_LOG_TB_OP) {
            int i;
            fprintf(logfile, "RESTORE:\n");
            for(i=0;i<=j; i++) {
                if (gen_opc_instr_start[i]) {
                    fprintf(logfile, "0x%04x: " TARGET_FMT_lx "\n", i, gen_opc_pc[i]);
                }
            }
            fprintf(logfile, "spc=0x%08lx j=0x%x eip=" TARGET_FMT_lx " cs_base=%x\n",
                    searched_pc, j, gen_opc_pc[j] - tb->cs_base,
                    (uint32_t)tb->cs_base);
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
    {
        target_ulong npc;
        env->pc = gen_opc_pc[j];
        npc = gen_opc_npc[j];
        if (npc == 1) {
            /* dynamic NPC: already stored */
        } else if (npc == 2) {
            target_ulong t2 = (target_ulong)(unsigned long)puc;
            /* jump PC: use T2 and the jump targets of the translation */
            if (t2)
                env->npc = gen_opc_jump_pc[0];
            else
                env->npc = gen_opc_jump_pc[1];
        } else {
            env->npc = npc;
        }
    }
#elif defined(TARGET_PPC)
    {
        int type;
        /* for PPC, we need to look at the micro operation to get the
           access type */
        env->nip = gen_opc_pc[j];
        switch(c) {
#if defined(CONFIG_USER_ONLY)
#define CASE3(op)\
        case INDEX_op_ ## op ## _raw
#else
#define CASE3(op)\
        case INDEX_op_ ## op ## _user:\
        case INDEX_op_ ## op ## _kernel:\
        case INDEX_op_ ## op ## _hypv
#endif

        CASE3(stfd):
        CASE3(stfs):
        CASE3(lfd):
        CASE3(lfs):
            type = ACCESS_FLOAT;
            break;
        CASE3(lwarx):
            type = ACCESS_RES;
            break;
        CASE3(stwcx):
            type = ACCESS_RES;
            break;
        CASE3(eciwx):
        CASE3(ecowx):
            type = ACCESS_EXT;
            break;
        default:
            type = ACCESS_INT;
            break;
        }
        env->access_type = type;
    }
#elif defined(TARGET_M68K)
    env->pc = gen_opc_pc[j];
#elif defined(TARGET_MIPS)
    env->PC[env->current_tc] = gen_opc_pc[j];
    env->hflags &= ~MIPS_HFLAG_BMASK;
    env->hflags |= gen_opc_hflags[j];
#elif defined(TARGET_ALPHA)
    env->pc = gen_opc_pc[j];
#elif defined(TARGET_SH4)
    env->pc = gen_opc_pc[j];
    env->flags = gen_opc_hflags[j];
#endif
    return 0;
}
