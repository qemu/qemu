/*
 *  ARM translation
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

#include "cpu-arm.h"
#include "exec.h"
#include "disas.h"

/* internal defines */
typedef struct DisasContext {
    uint8_t *pc;
    int is_jmp;
    struct TranslationBlock *tb;
} DisasContext;

/* XXX: move that elsewhere */
static uint16_t *gen_opc_ptr;
static uint32_t *gen_opparam_ptr;
extern FILE *logfile;
extern int loglevel;

enum {
#define DEF(s, n, copy_size) INDEX_op_ ## s,
#include "opc-arm.h"
#undef DEF
    NB_OPS,
};

#include "gen-op-arm.h"

typedef void (GenOpFunc)(void);
typedef void (GenOpFunc1)(long);
typedef void (GenOpFunc2)(long, long);
typedef void (GenOpFunc3)(long, long, long);

static GenOpFunc2 *gen_test_cc[14] = {
    gen_op_test_eq,
    gen_op_test_ne,
    gen_op_test_cs,
    gen_op_test_cc,
    gen_op_test_mi,
    gen_op_test_pl,
    gen_op_test_vs,
    gen_op_test_vc,
    gen_op_test_hi,
    gen_op_test_ls,
    gen_op_test_ge,
    gen_op_test_lt,
    gen_op_test_gt,
    gen_op_test_le,
};

const uint8_t table_logic_cc[16] = {
    1, /* and */
    1, /* xor */
    0, /* sub */
    0, /* rsb */
    0, /* add */
    0, /* adc */
    0, /* sbc */
    0, /* rsc */
    1, /* andl */
    1, /* xorl */
    0, /* cmp */
    0, /* cmn */
    1, /* orr */
    1, /* mov */
    1, /* bic */
    1, /* mvn */
};
    
static GenOpFunc1 *gen_shift_T1_im[4] = {
    gen_op_shll_T1_im,
    gen_op_shrl_T1_im,
    gen_op_sarl_T1_im,
    gen_op_rorl_T1_im,
};

static GenOpFunc1 *gen_shift_T2_im[4] = {
    gen_op_shll_T2_im,
    gen_op_shrl_T2_im,
    gen_op_sarl_T2_im,
    gen_op_rorl_T2_im,
};

static GenOpFunc1 *gen_shift_T1_im_cc[4] = {
    gen_op_shll_T1_im_cc,
    gen_op_shrl_T1_im_cc,
    gen_op_sarl_T1_im_cc,
    gen_op_rorl_T1_im_cc,
};

static GenOpFunc *gen_shift_T1_T0[4] = {
    gen_op_shll_T1_T0,
    gen_op_shrl_T1_T0,
    gen_op_sarl_T1_T0,
    gen_op_rorl_T1_T0,
};

static GenOpFunc *gen_shift_T1_T0_cc[4] = {
    gen_op_shll_T1_T0_cc,
    gen_op_shrl_T1_T0_cc,
    gen_op_sarl_T1_T0_cc,
    gen_op_rorl_T1_T0_cc,
};

static GenOpFunc *gen_op_movl_TN_reg[3][16] = {
    {
        gen_op_movl_T0_r0,
        gen_op_movl_T0_r1,
        gen_op_movl_T0_r2,
        gen_op_movl_T0_r3,
        gen_op_movl_T0_r4,
        gen_op_movl_T0_r5,
        gen_op_movl_T0_r6,
        gen_op_movl_T0_r7,
        gen_op_movl_T0_r8,
        gen_op_movl_T0_r9,
        gen_op_movl_T0_r10,
        gen_op_movl_T0_r11,
        gen_op_movl_T0_r12,
        gen_op_movl_T0_r13,
        gen_op_movl_T0_r14,
        gen_op_movl_T0_r15,
    },
    {
        gen_op_movl_T1_r0,
        gen_op_movl_T1_r1,
        gen_op_movl_T1_r2,
        gen_op_movl_T1_r3,
        gen_op_movl_T1_r4,
        gen_op_movl_T1_r5,
        gen_op_movl_T1_r6,
        gen_op_movl_T1_r7,
        gen_op_movl_T1_r8,
        gen_op_movl_T1_r9,
        gen_op_movl_T1_r10,
        gen_op_movl_T1_r11,
        gen_op_movl_T1_r12,
        gen_op_movl_T1_r13,
        gen_op_movl_T1_r14,
        gen_op_movl_T1_r15,
    },
    {
        gen_op_movl_T2_r0,
        gen_op_movl_T2_r1,
        gen_op_movl_T2_r2,
        gen_op_movl_T2_r3,
        gen_op_movl_T2_r4,
        gen_op_movl_T2_r5,
        gen_op_movl_T2_r6,
        gen_op_movl_T2_r7,
        gen_op_movl_T2_r8,
        gen_op_movl_T2_r9,
        gen_op_movl_T2_r10,
        gen_op_movl_T2_r11,
        gen_op_movl_T2_r12,
        gen_op_movl_T2_r13,
        gen_op_movl_T2_r14,
        gen_op_movl_T2_r15,
    },
};

static GenOpFunc *gen_op_movl_reg_TN[2][16] = {
    {
        gen_op_movl_r0_T0,
        gen_op_movl_r1_T0,
        gen_op_movl_r2_T0,
        gen_op_movl_r3_T0,
        gen_op_movl_r4_T0,
        gen_op_movl_r5_T0,
        gen_op_movl_r6_T0,
        gen_op_movl_r7_T0,
        gen_op_movl_r8_T0,
        gen_op_movl_r9_T0,
        gen_op_movl_r10_T0,
        gen_op_movl_r11_T0,
        gen_op_movl_r12_T0,
        gen_op_movl_r13_T0,
        gen_op_movl_r14_T0,
        gen_op_movl_r15_T0,
    },
    {
        gen_op_movl_r0_T1,
        gen_op_movl_r1_T1,
        gen_op_movl_r2_T1,
        gen_op_movl_r3_T1,
        gen_op_movl_r4_T1,
        gen_op_movl_r5_T1,
        gen_op_movl_r6_T1,
        gen_op_movl_r7_T1,
        gen_op_movl_r8_T1,
        gen_op_movl_r9_T1,
        gen_op_movl_r10_T1,
        gen_op_movl_r11_T1,
        gen_op_movl_r12_T1,
        gen_op_movl_r13_T1,
        gen_op_movl_r14_T1,
        gen_op_movl_r15_T1,
    },
};

static GenOpFunc1 *gen_op_movl_TN_im[3] = {
    gen_op_movl_T0_im,
    gen_op_movl_T1_im,
    gen_op_movl_T2_im,
};

static inline void gen_movl_TN_reg(DisasContext *s, int reg, int t)
{
    int val;

    if (reg == 15) {
        /* normaly, since we updated PC, we need only to add 4 */
        val = (long)s->pc + 4;
        gen_op_movl_TN_im[t](val);
    } else {
        gen_op_movl_TN_reg[t][reg]();
    }
}

static inline void gen_movl_T0_reg(DisasContext *s, int reg)
{
    gen_movl_TN_reg(s, reg, 0);
}

static inline void gen_movl_T1_reg(DisasContext *s, int reg)
{
    gen_movl_TN_reg(s, reg, 1);
}

static inline void gen_movl_T2_reg(DisasContext *s, int reg)
{
    gen_movl_TN_reg(s, reg, 2);
}

static inline void gen_movl_reg_TN(DisasContext *s, int reg, int t)
{
    gen_op_movl_reg_TN[t][reg]();
    if (reg == 15) {
        s->is_jmp = DISAS_JUMP;
    }
}

static inline void gen_movl_reg_T0(DisasContext *s, int reg)
{
    gen_movl_reg_TN(s, reg, 0);
}

static inline void gen_movl_reg_T1(DisasContext *s, int reg)
{
    gen_movl_reg_TN(s, reg, 1);
}

static inline void gen_add_data_offset(DisasContext *s, unsigned int insn)
{
    int val, rm, shift;

    if (!(insn & (1 << 25))) {
        /* immediate */
        val = insn & 0xfff;
        if (!(insn & (1 << 23)))
            val = -val;
        gen_op_addl_T1_im(val);
    } else {
        /* shift/register */
        rm = (insn) & 0xf;
        shift = (insn >> 7) & 0x1f;
        gen_movl_T2_reg(s, rm);
        if (shift != 0) {
            gen_shift_T2_im[(insn >> 5) & 3](shift);
        }
        if (!(insn & (1 << 23)))
            gen_op_subl_T1_T2();
        else
            gen_op_addl_T1_T2();
    }
}

static inline void gen_add_datah_offset(DisasContext *s, unsigned int insn)
{
    int val, rm;
    
    if (insn & (1 << 22)) {
        /* immediate */
        val = (insn & 0xf) | ((insn >> 4) & 0xf0);
        if (!(insn & (1 << 23)))
            val = -val;
        gen_op_addl_T1_im(val);
    } else {
        /* register */
        rm = (insn) & 0xf;
        gen_movl_T2_reg(s, rm);
        if (!(insn & (1 << 23)))
            gen_op_subl_T1_T2();
        else
            gen_op_addl_T1_T2();
    }
}

static void disas_arm_insn(DisasContext *s)
{
    unsigned int cond, insn, val, op1, i, shift, rm, rs, rn, rd, sh;
    
    insn = ldl(s->pc);
    s->pc += 4;
    
    cond = insn >> 28;
    if (cond == 0xf)
        goto illegal_op;
    if (cond != 0xe) {
        /* if not always execute, we generate a conditional jump to
           next instruction */
        gen_test_cc[cond ^ 1]((long)s->tb, (long)s->pc);
        s->is_jmp = 1;
    }
    if ((insn & 0x0c000000) == 0 &&
        (insn & 0x00000090) != 0x90) {
        int set_cc, logic_cc, shiftop;
        
        op1 = (insn >> 21) & 0xf;
        set_cc = (insn >> 20) & 1;
        logic_cc = table_logic_cc[op1] & set_cc;

        /* data processing instruction */
        if (insn & (1 << 25)) {
            /* immediate operand */
            val = insn & 0xff;
            shift = ((insn >> 8) & 0xf) * 2;
            if (shift)
                val = (val >> shift) | (val << (32 - shift));
            gen_op_movl_T1_im(val);
            /* XXX: is CF modified ? */
        } else {
            /* register */
            rm = (insn) & 0xf;
            gen_movl_T1_reg(s, rm);
            shiftop = (insn >> 5) & 3;
            if (!(insn & (1 << 4))) {
                shift = (insn >> 7) & 0x1f;
                if (shift != 0) {
                    if (logic_cc) {
                        gen_shift_T1_im_cc[shiftop](shift);
                    } else {
                        gen_shift_T1_im[shiftop](shift);
                    }
                }
            } else {
                rs = (insn >> 16) & 0xf;
                gen_movl_T0_reg(s, rs);
                if (logic_cc) {
                    gen_shift_T1_T0_cc[shiftop]();
                } else {
                    gen_shift_T1_T0[shiftop]();
                }
            }
        }
        if (op1 != 0x0f && op1 != 0x0d) {
            rn = (insn >> 16) & 0xf;
            gen_movl_T0_reg(s, rn);
        }
        rd = (insn >> 12) & 0xf;
        switch(op1) {
        case 0x00:
            gen_op_andl_T0_T1();
            gen_movl_reg_T0(s, rd);
            break;
        case 0x01:
            gen_op_xorl_T0_T1();
            gen_movl_reg_T0(s, rd);
            break;
        case 0x02:
            if (set_cc)
                gen_op_subl_T0_T1_cc();
            else
                gen_op_subl_T0_T1();
            gen_movl_reg_T0(s, rd);
            break;
        case 0x03:
            if (set_cc)
                gen_op_rsbl_T0_T1_cc();
            else
                gen_op_rsbl_T0_T1();
            gen_movl_reg_T0(s, rd);
            break;
        case 0x04:
            if (set_cc)
                gen_op_addl_T0_T1_cc();
            else
                gen_op_addl_T0_T1();
            gen_movl_reg_T0(s, rd);
            break;
        case 0x05:
            if (set_cc)
                gen_op_adcl_T0_T1_cc();
            else
                gen_op_adcl_T0_T1();
            gen_movl_reg_T0(s, rd);
            break;
        case 0x06:
            if (set_cc)
                gen_op_sbcl_T0_T1_cc();
            else
                gen_op_sbcl_T0_T1();
            gen_movl_reg_T0(s, rd);
            break;
        case 0x07:
            if (set_cc)
                gen_op_rscl_T0_T1_cc();
            else
                gen_op_rscl_T0_T1();
            gen_movl_reg_T0(s, rd);
            break;
        case 0x08:
            if (set_cc) {
                gen_op_andl_T0_T1();
            }
            break;
        case 0x09:
            if (set_cc) {
                gen_op_xorl_T0_T1();
            }
            break;
        case 0x0a:
            if (set_cc) {
                gen_op_subl_T0_T1_cc();
            }
            break;
        case 0x0b:
            if (set_cc) {
                gen_op_addl_T0_T1_cc();
            }
            break;
        case 0x0c:
            gen_op_orl_T0_T1();
            gen_movl_reg_T0(s, rd);
            break;
        case 0x0d:
            gen_movl_reg_T1(s, rd);
            break;
        case 0x0e:
            gen_op_bicl_T0_T1();
            gen_movl_reg_T0(s, rd);
            break;
        default:
        case 0x0f:
            gen_op_notl_T1();
            gen_movl_reg_T1(s, rd);
            break;
        }
        if (logic_cc)
            gen_op_logic_cc();
    } else {
        /* other instructions */
        op1 = (insn >> 24) & 0xf;
        switch(op1) {
        case 0x0:
        case 0x1:
            sh = (insn >> 5) & 3;
            if (sh == 0) {
                if (op1 == 0x0) {
                    rd = (insn >> 16) & 0xf;
                    rn = (insn >> 12) & 0xf;
                    rs = (insn >> 8) & 0xf;
                    rm = (insn) & 0xf;
                    if (!(insn & (1 << 23))) {
                        /* 32 bit mul */
                        gen_movl_T0_reg(s, rs);
                        gen_movl_T1_reg(s, rm);
                        gen_op_mul_T0_T1();
                        if (insn & (1 << 21)) {
                            gen_movl_T1_reg(s, rn);
                            gen_op_addl_T0_T1();
                        }
                        if (insn & (1 << 20)) 
                            gen_op_logic_cc();
                        gen_movl_reg_T0(s, rd);
                    } else {
                        /* 64 bit mul */
                        gen_movl_T0_reg(s, rs);
                        gen_movl_T1_reg(s, rm);
                        if (insn & (1 << 22)) 
                            gen_op_mull_T0_T1();
                        else
                            gen_op_imull_T0_T1();
                        if (insn & (1 << 21)) 
                            gen_op_addq_T0_T1(rn, rd);
                        if (insn & (1 << 20)) 
                            gen_op_logicq_cc();
                        gen_movl_reg_T0(s, rn);
                        gen_movl_reg_T1(s, rd);
                    }
                } else {
                    /* SWP instruction */
                    rn = (insn >> 16) & 0xf;
                    rd = (insn >> 12) & 0xf;
                    rm = (insn) & 0xf;
                    
                    gen_movl_T0_reg(s, rm);
                    gen_movl_T1_reg(s, rn);
                    if (insn & (1 << 22)) {
                        gen_op_swpb_T0_T1();
                    } else {
                        gen_op_swpl_T0_T1();
                    }
                    gen_movl_reg_T0(s, rd);
                }
            } else {
                /* load/store half word */
                rn = (insn >> 16) & 0xf;
                rd = (insn >> 12) & 0xf;
                gen_movl_T1_reg(s, rn);
                if (insn & (1 << 25))
                    gen_add_datah_offset(s, insn);
                if (insn & (1 << 20)) {
                    /* load */
                    switch(sh) {
                    case 1:
                        gen_op_lduw_T0_T1();
                        break;
                    case 2:
                        gen_op_ldsb_T0_T1();
                        break;
                    default:
                    case 3:
                        gen_op_ldsw_T0_T1();
                        break;
                    }
                } else {
                    /* store */
                    gen_op_stw_T0_T1();
                }
                if (!(insn & (1 << 24)))
                    gen_add_datah_offset(s, insn);
                if (insn & (1 << 21))
                    gen_movl_reg_T1(s, rn);
            }
            break;
        case 0x4:
        case 0x5:
        case 0x6:
        case 0x7:
            /* load/store byte/word */
            rn = (insn >> 16) & 0xf;
            rd = (insn >> 12) & 0xf;
            gen_movl_T1_reg(s, rn);
            if (insn & (1 << 24))
                gen_add_data_offset(s, insn);
            if (insn & (1 << 20)) {
                /* load */
                if (insn & (1 << 22))
                    gen_op_ldub_T0_T1();
                else
                    gen_op_ldl_T0_T1();
                gen_movl_reg_T0(s, rd);
            } else {
                /* store */
                gen_movl_T0_reg(s, rd);
                if (insn & (1 << 22))
                    gen_op_stb_T0_T1();
                else
                    gen_op_stl_T0_T1();
            }
            if (!(insn & (1 << 24)))
                gen_add_data_offset(s, insn);
            if (insn & (1 << 21))
                gen_movl_reg_T1(s, rn);
            break;
        case 0x08:
        case 0x09:
            /* load/store multiple words */
            if (insn & (1 << 22))
                goto illegal_op; /* only usable in supervisor mode */
            rn = (insn >> 16) & 0xf;
            gen_movl_T1_reg(s, rn);
            val = 4;
            if (!(insn & (1 << 23)))
                val = -val;
            for(i=0;i<16;i++) {
                if (insn & (1 << i)) {
                    if (insn & (1 << 24))
                        gen_op_addl_T1_im(val);
                    if (insn & (1 << 20)) {
                        /* load */
                        gen_op_ldl_T0_T1();
                        gen_movl_reg_T0(s, i);
                    } else {
                        /* store */
                        gen_movl_T0_reg(s, i);
                        gen_op_stl_T0_T1();
                    }
                    if (!(insn & (1 << 24)))
                        gen_op_addl_T1_im(val);
                }
            }
            if (insn & (1 << 21))
                gen_movl_reg_T1(s, rn);
            break;
        case 0xa:
        case 0xb:
            {
                int offset;
                
                /* branch (and link) */
                val = (int)s->pc;
                if (insn & (1 << 24)) {
                    gen_op_movl_T0_im(val);
                    gen_op_movl_reg_TN[0][14]();
                }
                offset = (((int)insn << 8) >> 8);
                val += (offset << 2) + 4;
                gen_op_jmp((long)s->tb, val);
                s->is_jmp = DISAS_TB_JUMP;
            }
            break;
        case 0xf:
            /* swi */
            gen_op_movl_T0_im((long)s->pc);
            gen_op_movl_reg_TN[0][15]();
            gen_op_swi();
            s->is_jmp = DISAS_JUMP;
            break;
        default:
        illegal_op:
            gen_op_movl_T0_im((long)s->pc - 4);
            gen_op_movl_reg_TN[0][15]();
            gen_op_undef_insn();
            s->is_jmp = DISAS_JUMP;
            break;
        }
    }
}

/* generate intermediate code in gen_opc_buf and gen_opparam_buf for
   basic block 'tb'. If search_pc is TRUE, also generate PC
   information for each intermediate instruction. */
static inline int gen_intermediate_code_internal(TranslationBlock *tb, int search_pc)
{
    DisasContext dc1, *dc = &dc1;
    uint16_t *gen_opc_end;
    int j, lj;
    uint8_t *pc_start;
    
    /* generate intermediate code */
    pc_start = (uint8_t *)tb->pc;
       
    dc->tb = tb;

    gen_opc_ptr = gen_opc_buf;
    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;
    gen_opparam_ptr = gen_opparam_buf;

    dc->is_jmp = DISAS_NEXT;
    dc->pc = pc_start;
    lj = -1;
    do {
        if (search_pc) {
            j = gen_opc_ptr - gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j)
                    gen_opc_instr_start[lj++] = 0;
                gen_opc_pc[lj] = (uint32_t)dc->pc;
                gen_opc_instr_start[lj] = 1;
            }
        }
        disas_arm_insn(dc);
    } while (!dc->is_jmp && gen_opc_ptr < gen_opc_end && 
             (dc->pc - pc_start) < (TARGET_PAGE_SIZE - 32));
    /* we must store the eflags state if it is not already done */
    if (dc->is_jmp != DISAS_TB_JUMP && 
        dc->is_jmp != DISAS_JUMP) {
        gen_op_movl_T0_im((long)dc->pc - 4);
        gen_op_movl_reg_TN[0][15]();
    }
    if (dc->is_jmp != DISAS_TB_JUMP) {
        /* indicate that the hash table must be used to find the next TB */
        gen_op_movl_T0_0();
    }
    *gen_opc_ptr = INDEX_op_end;

#ifdef DEBUG_DISAS
    if (loglevel) {
        fprintf(logfile, "----------------\n");
        fprintf(logfile, "IN: %s\n", lookup_symbol(pc_start));
	disas(logfile, pc_start, dc->pc - pc_start, 0, 0);
        fprintf(logfile, "\n");

        fprintf(logfile, "OP:\n");
        dump_ops(gen_opc_buf, gen_opparam_buf);
        fprintf(logfile, "\n");
    }
#endif
    if (!search_pc)
        tb->size = dc->pc - pc_start;
    return 0;
}

int gen_intermediate_code(TranslationBlock *tb)
{
    return gen_intermediate_code_internal(tb, 0);
}

int gen_intermediate_code_pc(TranslationBlock *tb)
{
    return gen_intermediate_code_internal(tb, 1);
}

CPUARMState *cpu_arm_init(void)
{
    CPUARMState *env;

    cpu_exec_init();

    env = malloc(sizeof(CPUARMState));
    if (!env)
        return NULL;
    memset(env, 0, sizeof(CPUARMState));
    return env;
}

void cpu_arm_close(CPUARMState *env)
{
    free(env);
}

void cpu_arm_dump_state(CPUARMState *env, FILE *f, int flags)
{
    int i;

    for(i=0;i<16;i++) {
        fprintf(f, "R%02d=%08x", i, env->regs[i]);
        if ((i % 4) == 3)
            fprintf(f, "\n");
        else
            fprintf(f, " ");
    }
    fprintf(f, "CPSR=%08x", env->cpsr);
}
