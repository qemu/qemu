/*
   SPARC translation

   Copyright (C) 2003 Thomas M. Ogrisegg <tom@fnord.at>
   Copyright (C) 2003 Fabrice Bellard

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
   TODO-list:

   NPC/PC static optimisations (use JUMP_TB when possible)
   FPU-Instructions
   Privileged instructions
   Coprocessor-Instructions
   Optimize synthetic instructions
   Optional alignment and privileged instruction check
*/

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"

#define DEBUG_DISAS

typedef struct DisasContext {
    uint8_t *pc;		/* NULL means dynamic value */
    uint8_t *npc;		/* NULL means dynamic value */
    int is_br;
    struct TranslationBlock *tb;
} DisasContext;

static uint16_t *gen_opc_ptr;
static uint32_t *gen_opparam_ptr;
extern FILE *logfile;
extern int loglevel;

enum {
#define DEF(s,n,copy_size) INDEX_op_ ## s,
#include "opc.h"
#undef DEF
    NB_OPS
};

#include "gen-op.h"

#define GET_FIELD(X, FROM, TO) \
  ((X) >> (31 - (TO)) & ((1 << ((TO) - (FROM) + 1)) - 1))

#define IS_IMM (insn & (1<<13))

static void disas_sparc_insn(DisasContext * dc);

static GenOpFunc *gen_op_movl_TN_reg[2][32] = {
    {
     gen_op_movl_g0_T0,
     gen_op_movl_g1_T0,
     gen_op_movl_g2_T0,
     gen_op_movl_g3_T0,
     gen_op_movl_g4_T0,
     gen_op_movl_g5_T0,
     gen_op_movl_g6_T0,
     gen_op_movl_g7_T0,
     gen_op_movl_o0_T0,
     gen_op_movl_o1_T0,
     gen_op_movl_o2_T0,
     gen_op_movl_o3_T0,
     gen_op_movl_o4_T0,
     gen_op_movl_o5_T0,
     gen_op_movl_o6_T0,
     gen_op_movl_o7_T0,
     gen_op_movl_l0_T0,
     gen_op_movl_l1_T0,
     gen_op_movl_l2_T0,
     gen_op_movl_l3_T0,
     gen_op_movl_l4_T0,
     gen_op_movl_l5_T0,
     gen_op_movl_l6_T0,
     gen_op_movl_l7_T0,
     gen_op_movl_i0_T0,
     gen_op_movl_i1_T0,
     gen_op_movl_i2_T0,
     gen_op_movl_i3_T0,
     gen_op_movl_i4_T0,
     gen_op_movl_i5_T0,
     gen_op_movl_i6_T0,
     gen_op_movl_i7_T0,
     },
    {
     gen_op_movl_g0_T1,
     gen_op_movl_g1_T1,
     gen_op_movl_g2_T1,
     gen_op_movl_g3_T1,
     gen_op_movl_g4_T1,
     gen_op_movl_g5_T1,
     gen_op_movl_g6_T1,
     gen_op_movl_g7_T1,
     gen_op_movl_o0_T1,
     gen_op_movl_o1_T1,
     gen_op_movl_o2_T1,
     gen_op_movl_o3_T1,
     gen_op_movl_o4_T1,
     gen_op_movl_o5_T1,
     gen_op_movl_o6_T1,
     gen_op_movl_o7_T1,
     gen_op_movl_l0_T1,
     gen_op_movl_l1_T1,
     gen_op_movl_l2_T1,
     gen_op_movl_l3_T1,
     gen_op_movl_l4_T1,
     gen_op_movl_l5_T1,
     gen_op_movl_l6_T1,
     gen_op_movl_l7_T1,
     gen_op_movl_i0_T1,
     gen_op_movl_i1_T1,
     gen_op_movl_i2_T1,
     gen_op_movl_i3_T1,
     gen_op_movl_i4_T1,
     gen_op_movl_i5_T1,
     gen_op_movl_i6_T1,
     gen_op_movl_i7_T1,
     }
};

static GenOpFunc *gen_op_movl_reg_TN[3][32] = {
    {
     gen_op_movl_T0_g0,
     gen_op_movl_T0_g1,
     gen_op_movl_T0_g2,
     gen_op_movl_T0_g3,
     gen_op_movl_T0_g4,
     gen_op_movl_T0_g5,
     gen_op_movl_T0_g6,
     gen_op_movl_T0_g7,
     gen_op_movl_T0_o0,
     gen_op_movl_T0_o1,
     gen_op_movl_T0_o2,
     gen_op_movl_T0_o3,
     gen_op_movl_T0_o4,
     gen_op_movl_T0_o5,
     gen_op_movl_T0_o6,
     gen_op_movl_T0_o7,
     gen_op_movl_T0_l0,
     gen_op_movl_T0_l1,
     gen_op_movl_T0_l2,
     gen_op_movl_T0_l3,
     gen_op_movl_T0_l4,
     gen_op_movl_T0_l5,
     gen_op_movl_T0_l6,
     gen_op_movl_T0_l7,
     gen_op_movl_T0_i0,
     gen_op_movl_T0_i1,
     gen_op_movl_T0_i2,
     gen_op_movl_T0_i3,
     gen_op_movl_T0_i4,
     gen_op_movl_T0_i5,
     gen_op_movl_T0_i6,
     gen_op_movl_T0_i7,
     },
    {
     gen_op_movl_T1_g0,
     gen_op_movl_T1_g1,
     gen_op_movl_T1_g2,
     gen_op_movl_T1_g3,
     gen_op_movl_T1_g4,
     gen_op_movl_T1_g5,
     gen_op_movl_T1_g6,
     gen_op_movl_T1_g7,
     gen_op_movl_T1_o0,
     gen_op_movl_T1_o1,
     gen_op_movl_T1_o2,
     gen_op_movl_T1_o3,
     gen_op_movl_T1_o4,
     gen_op_movl_T1_o5,
     gen_op_movl_T1_o6,
     gen_op_movl_T1_o7,
     gen_op_movl_T1_l0,
     gen_op_movl_T1_l1,
     gen_op_movl_T1_l2,
     gen_op_movl_T1_l3,
     gen_op_movl_T1_l4,
     gen_op_movl_T1_l5,
     gen_op_movl_T1_l6,
     gen_op_movl_T1_l7,
     gen_op_movl_T1_i0,
     gen_op_movl_T1_i1,
     gen_op_movl_T1_i2,
     gen_op_movl_T1_i3,
     gen_op_movl_T1_i4,
     gen_op_movl_T1_i5,
     gen_op_movl_T1_i6,
     gen_op_movl_T1_i7,
     },
    {
     gen_op_movl_T2_g0,
     gen_op_movl_T2_g1,
     gen_op_movl_T2_g2,
     gen_op_movl_T2_g3,
     gen_op_movl_T2_g4,
     gen_op_movl_T2_g5,
     gen_op_movl_T2_g6,
     gen_op_movl_T2_g7,
     gen_op_movl_T2_o0,
     gen_op_movl_T2_o1,
     gen_op_movl_T2_o2,
     gen_op_movl_T2_o3,
     gen_op_movl_T2_o4,
     gen_op_movl_T2_o5,
     gen_op_movl_T2_o6,
     gen_op_movl_T2_o7,
     gen_op_movl_T2_l0,
     gen_op_movl_T2_l1,
     gen_op_movl_T2_l2,
     gen_op_movl_T2_l3,
     gen_op_movl_T2_l4,
     gen_op_movl_T2_l5,
     gen_op_movl_T2_l6,
     gen_op_movl_T2_l7,
     gen_op_movl_T2_i0,
     gen_op_movl_T2_i1,
     gen_op_movl_T2_i2,
     gen_op_movl_T2_i3,
     gen_op_movl_T2_i4,
     gen_op_movl_T2_i5,
     gen_op_movl_T2_i6,
     gen_op_movl_T2_i7,
     }
};

static GenOpFunc1 *gen_op_movl_TN_im[3] = {
    gen_op_movl_T0_im,
    gen_op_movl_T1_im,
    gen_op_movl_T2_im
};

static inline void gen_movl_imm_TN(int reg, int imm)
{
    gen_op_movl_TN_im[reg] (imm);
}

static inline void gen_movl_imm_T1(int val)
{
    gen_movl_imm_TN(1, val);
}

static inline void gen_movl_imm_T0(int val)
{
    gen_movl_imm_TN(0, val);
}

static inline void gen_movl_reg_TN(int reg, int t)
{
    if (reg)
	gen_op_movl_reg_TN[t][reg] ();
    else
	gen_movl_imm_TN(t, 0);
}

static inline void gen_movl_reg_T0(int reg)
{
    gen_movl_reg_TN(reg, 0);
}

static inline void gen_movl_reg_T1(int reg)
{
    gen_movl_reg_TN(reg, 1);
}

static inline void gen_movl_reg_T2(int reg)
{
    gen_movl_reg_TN(reg, 2);
}

static inline void gen_movl_TN_reg(int reg, int t)
{
    if (reg)
	gen_op_movl_TN_reg[t][reg] ();
}

static inline void gen_movl_T0_reg(int reg)
{
    gen_movl_TN_reg(reg, 0);
}

static inline void gen_movl_T1_reg(int reg)
{
    gen_movl_TN_reg(reg, 1);
}

static void gen_cond(int cond)
{
	switch (cond) {
        case 0x0:
            gen_op_movl_T2_0();
            break;
	case 0x1:
	    gen_op_eval_be();
	    break;
	case 0x2:
	    gen_op_eval_ble();
	    break;
	case 0x3:
	    gen_op_eval_bl();
	    break;
	case 0x4:
	    gen_op_eval_bleu();
	    break;
	case 0x5:
	    gen_op_eval_bcs();
	    break;
	case 0x6:
	    gen_op_eval_bneg();
	    break;
	case 0x7:
	    gen_op_eval_bvs();
	    break;
        case 0x8:
            gen_op_movl_T2_1();
            break;
	case 0x9:
	    gen_op_eval_bne();
	    break;
	case 0xa:
	    gen_op_eval_bg();
	    break;
	case 0xb:
	    gen_op_eval_bge();
	    break;
	case 0xc:
	    gen_op_eval_bgu();
	    break;
	case 0xd:
	    gen_op_eval_bcc();
	    break;
	case 0xe:
	    gen_op_eval_bpos();
	    break;
        default:
	case 0xf:
	    gen_op_eval_bvc();
	    break;
	}
}


static void do_branch(DisasContext * dc, uint32_t target, uint32_t insn)
{
    unsigned int cond = GET_FIELD(insn, 3, 6), a = (insn & (1 << 29));
    target += (uint32_t) dc->pc;
    if (cond == 0x0) {
	/* unconditional not taken */
	if (a) {
	    dc->pc = dc->npc + 4;
	    dc->npc = dc->pc + 4;
	} else {
	    dc->pc = dc->npc;
	    dc->npc = dc->pc + 4;
	}
    } else if (cond == 0x8) {
	/* unconditional taken */
	if (a) {
	    dc->pc = (uint8_t *) target;
	    dc->npc = dc->pc + 4;
	} else {
	    dc->pc = dc->npc;
	    dc->npc = (uint8_t *) target;
	}
    } else {
        gen_cond(cond);
	if (a) {
	    gen_op_generic_branch_a((uint32_t) target,
				    (uint32_t) (dc->npc));
            dc->is_br = 1;
            dc->pc = NULL;
            dc->npc = NULL;
	} else {
            dc->pc = dc->npc;
	    gen_op_generic_branch((uint32_t) target,
				  (uint32_t) (dc->npc + 4));
            dc->npc = NULL;
	}
    }
}

#define GET_FIELDs(x,a,b) sign_extend (GET_FIELD(x,a,b), (b) - (a) + 1)

static int sign_extend(int x, int len)
{
    len = 32 - len;
    return (x << len) >> len;
}

static inline void save_state(DisasContext * dc)
{
    gen_op_jmp_im((uint32_t)dc->pc);
    if (dc->npc != NULL)
        gen_op_movl_npc_im((long) dc->npc);
}

static void disas_sparc_insn(DisasContext * dc)
{
    unsigned int insn, opc, rs1, rs2, rd;

    insn = ldl_code(dc->pc);
    opc = GET_FIELD(insn, 0, 1);

    rd = GET_FIELD(insn, 2, 6);
    switch (opc) {
    case 0:			/* branches/sethi */
	{
	    unsigned int xop = GET_FIELD(insn, 7, 9);
	    int target;
	    target = GET_FIELD(insn, 10, 31);
	    switch (xop) {
	    case 0x0:
	    case 0x1:		/* UNIMPL */
                goto illegal_insn;
	    case 0x2:		/* BN+x */
		{
		    target <<= 2;
		    target = sign_extend(target, 22);
		    do_branch(dc, target, insn);
		    goto jmp_insn;
		}
	    case 0x3:		/* FBN+x */
		break;
	    case 0x4:		/* SETHI */
		gen_movl_imm_T0(target << 10);
		gen_movl_T0_reg(rd);
		break;
	    case 0x5:		/*CBN+x */
		break;
	    }
	    break;
	}
    case 1:
	/*CALL*/ {
	    unsigned int target = GET_FIELDs(insn, 2, 31) << 2;

	    gen_op_movl_T0_im((long) (dc->pc));
	    gen_movl_T0_reg(15);
	    target = (long) dc->pc + target;
	    dc->pc = dc->npc;
	    dc->npc = (uint8_t *) target;
	}
	goto jmp_insn;
    case 2:			/* FPU & Logical Operations */
	{
	    unsigned int xop = GET_FIELD(insn, 7, 12);
	    if (xop == 0x3a) {	/* generate trap */
                int cond;
                rs1 = GET_FIELD(insn, 13, 17);
                gen_movl_reg_T0(rs1);
		if (IS_IMM) {
                    gen_movl_imm_T1(GET_FIELD(insn, 25, 31));
                } else {
                    rs2 = GET_FIELD(insn, 27, 31);
                    gen_movl_reg_T1(rs2);
                }
                gen_op_add_T1_T0();
                save_state(dc);
                cond = GET_FIELD(insn, 3, 6);
                if (cond == 0x8) {
                    gen_op_trap_T0();
                    dc->is_br = 1;
                    goto jmp_insn;
                } else {
                    gen_op_trapcc_T0();
                }
            } else if (xop == 0x28) {
                rs1 = GET_FIELD(insn, 13, 17);
                switch(rs1) {
                case 0: /* rdy */
                    gen_op_rdy();
                    gen_movl_T0_reg(rd);
                    break;
                default:
                    goto illegal_insn;
                }
	    } else if (xop == 0x34 || xop == 0x35) {	/* FPU Operations */
                goto illegal_insn;
	    } else {
                rs1 = GET_FIELD(insn, 13, 17);
                gen_movl_reg_T0(rs1);
                if (IS_IMM) {	/* immediate */
                    rs2 = GET_FIELDs(insn, 19, 31);
                    gen_movl_imm_T1(rs2);
                } else {		/* register */
                    rs2 = GET_FIELD(insn, 27, 31);
                    gen_movl_reg_T1(rs2);
                }
                if (xop < 0x20) {
                    switch (xop & ~0x10) {
                    case 0x0:
                        if (xop & 0x10)
                            gen_op_add_T1_T0_cc();
                        else
                            gen_op_add_T1_T0();
                        break;
                    case 0x1:
                        gen_op_and_T1_T0();
                        if (xop & 0x10)
                            gen_op_logic_T0_cc();
                        break;
                    case 0x2:
                        gen_op_or_T1_T0();
                        if (xop & 0x10)
                            gen_op_logic_T0_cc();
                        break;
                    case 0x3:
                        gen_op_xor_T1_T0();
                        if (xop & 0x10)
                            gen_op_logic_T0_cc();
                        break;
                    case 0x4:
                        if (xop & 0x10)
                            gen_op_sub_T1_T0_cc();
                        else
                            gen_op_sub_T1_T0();
                        break;
                    case 0x5:
                        gen_op_andn_T1_T0();
                        if (xop & 0x10)
                            gen_op_logic_T0_cc();
                        break;
                    case 0x6:
                        gen_op_orn_T1_T0();
                        if (xop & 0x10)
                            gen_op_logic_T0_cc();
                        break;
                    case 0x7:
                        gen_op_xnor_T1_T0();
                        if (xop & 0x10)
                            gen_op_logic_T0_cc();
                        break;
                    case 0x8:
                        gen_op_addx_T1_T0();
                        if (xop & 0x10)
                            gen_op_set_flags();
                        break;
                    case 0xa:
                        gen_op_umul_T1_T0();
                        if (xop & 0x10)
                            gen_op_logic_T0_cc();
                        break;
                    case 0xb:
                        gen_op_smul_T1_T0();
                        if (xop & 0x10)
                            gen_op_logic_T0_cc();
                        break;
                    case 0xc:
                        gen_op_subx_T1_T0();
                        if (xop & 0x10)
                            gen_op_set_flags();
                        break;
                    case 0xe:
                        gen_op_udiv_T1_T0();
                        if (xop & 0x10)
                            gen_op_div_cc();
                        break;
                    case 0xf:
                        gen_op_sdiv_T1_T0();
                        if (xop & 0x10)
                            gen_op_div_cc();
                        break;
                    default:
                        goto illegal_insn;
                    }
                    gen_movl_T0_reg(rd);
                } else {
                    switch (xop) {
                    case 0x24: /* mulscc */
                        gen_op_mulscc_T1_T0();
                        gen_movl_T0_reg(rd);
                        break;
                    case 0x25:	/* SLL */
                        gen_op_sll();
                        gen_movl_T0_reg(rd);
                        break;
                    case 0x26:
                        gen_op_srl();
                        gen_movl_T0_reg(rd);
                        break;
                    case 0x27:
                        gen_op_sra();
                        gen_movl_T0_reg(rd);
                        break;
                    case 0x30:
                        {
                            gen_op_xor_T1_T0();
                            switch(rd) {
                            case 0:
                                gen_op_wry();
                                break;
                            default:
                                goto illegal_insn;
                            }
                        }
                        break;
                    case 0x38:	/* jmpl */
                        {
                            gen_op_add_T1_T0();
                            gen_op_movl_npc_T0();
                            if (rd != 0) {
                                gen_op_movl_T0_im((long) (dc->pc));
                                gen_movl_T0_reg(rd);
                            }
                            dc->pc = dc->npc;
                            dc->npc = NULL;
                        }
                        goto jmp_insn;
                    case 0x3b: /* flush */
                        /* nothing to do */
                        break;
                    case 0x3c:	/* save */
                        save_state(dc);
                        gen_op_add_T1_T0();
                        gen_op_save();
                        gen_movl_T0_reg(rd);
                        break;
                    case 0x3d:	/* restore */
                        save_state(dc);
                        gen_op_add_T1_T0();
                        gen_op_restore();
                        gen_movl_T0_reg(rd);
                        break;
                    default:
                        goto illegal_insn;
                    }
                }
            }
	    break;
	}
    case 3:			/* load/store instructions */
	{
	    unsigned int xop = GET_FIELD(insn, 7, 12);
	    rs1 = GET_FIELD(insn, 13, 17);
	    gen_movl_reg_T0(rs1);
	    if (IS_IMM) {	/* immediate */
		rs2 = GET_FIELDs(insn, 19, 31);
		gen_movl_imm_T1(rs2);
	    } else {		/* register */
		rs2 = GET_FIELD(insn, 27, 31);
		gen_movl_reg_T1(rs2);
	    }
	    gen_op_add_T1_T0();
	    if (xop < 4 || xop > 7) {
		switch (xop) {
		case 0x0:	/* load word */
		    gen_op_ld();
		    break;
		case 0x1:	/* load unsigned byte */
		    gen_op_ldub();
		    break;
		case 0x2:	/* load unsigned halfword */
		    gen_op_lduh();
		    break;
		case 0x3:	/* load double word */
		    gen_op_ldd();
		    gen_movl_T0_reg(rd + 1);
		    break;
		case 0x9:	/* load signed byte */
		    gen_op_ldsb();
		    break;
		case 0xa:	/* load signed halfword */
		    gen_op_ldsh();
		    break;
		case 0xd:	/* ldstub -- XXX: should be atomically */
		    gen_op_ldstub();
		    break;
		case 0x0f:	/* swap register with memory. Also atomically */
		    gen_op_swap();
		    break;
		}
		gen_movl_T1_reg(rd);
	    } else if (xop < 8) {
		gen_movl_reg_T1(rd);
		switch (xop) {
		case 0x4:
		    gen_op_st();
		    break;
		case 0x5:
		    gen_op_stb();
		    break;
		case 0x6:
		    gen_op_sth();
		    break;
		case 0x7:
		    gen_movl_reg_T2(rd + 1);
		    gen_op_std();
		    break;
		}
	    }
	}
    }
    /* default case for non jump instructions */
    if (dc->npc != NULL) {
	dc->pc = dc->npc;
	dc->npc = dc->npc + 4;
    } else {
	dc->pc = NULL;
	gen_op_next_insn();
    }
  jmp_insn:;
    return;
 illegal_insn:
    gen_op_jmp_im((uint32_t)dc->pc);
    if (dc->npc != NULL)
        gen_op_movl_npc_im((long) dc->npc);
    gen_op_exception(TT_ILL_INSN);
    dc->is_br = 1;
}

static inline int gen_intermediate_code_internal(TranslationBlock * tb,
						 int spc)
{
    uint8_t *pc_start, *last_pc;
    uint16_t *gen_opc_end;
    DisasContext dc1, *dc = &dc1;

    memset(dc, 0, sizeof(DisasContext));
    if (spc) {
	printf("SearchPC not yet supported\n");
	exit(0);
    }
    dc->tb = tb;
    pc_start = (uint8_t *) tb->pc;
    dc->pc = pc_start;
    dc->npc = (uint8_t *) tb->cs_base;

    gen_opc_ptr = gen_opc_buf;
    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;
    gen_opparam_ptr = gen_opparam_buf;

    do {
	last_pc = dc->pc;
	disas_sparc_insn(dc);
	if (dc->is_br)
	    break;
	/* if the next PC is different, we abort now */
	if (dc->pc != (last_pc + 4))
	    break;
    } while ((gen_opc_ptr < gen_opc_end) &&
	     (dc->pc - pc_start) < (TARGET_PAGE_SIZE - 32));
    if (dc->pc != NULL)
	gen_op_jmp_im((long) dc->pc);
    if (dc->npc != NULL)
	gen_op_movl_npc_im((long) dc->npc);
    gen_op_movl_T0_0();
    gen_op_exit_tb();

    *gen_opc_ptr = INDEX_op_end;
#ifdef DEBUG_DISAS
    if (loglevel) {
	fprintf(logfile, "--------------\n");
	fprintf(logfile, "IN: %s\n", lookup_symbol(pc_start));
	disas(logfile, pc_start, last_pc + 4 - pc_start, 0, 0);
	fprintf(logfile, "\n");
	fprintf(logfile, "OP:\n");
	dump_ops(gen_opc_buf, gen_opparam_buf);
	fprintf(logfile, "\n");
    }
#endif

    return 0;
}

int gen_intermediate_code(CPUSPARCState * env, TranslationBlock * tb)
{
    return gen_intermediate_code_internal(tb, 0);
}

int gen_intermediate_code_pc(CPUSPARCState * env, TranslationBlock * tb)
{
    return gen_intermediate_code_internal(tb, 1);
}

CPUSPARCState *cpu_sparc_init(void)
{
    CPUSPARCState *env;

    cpu_exec_init();

    if (!(env = malloc(sizeof(CPUSPARCState))))
	return (NULL);
    memset(env, 0, sizeof(*env));
    env->cwp = 0;
    env->wim = 1;
    env->regwptr = env->regbase + (env->cwp * 16);
    env->user_mode_only = 1;
    return (env);
}

#define GET_FLAG(a,b) ((env->psr & a)?b:'-')

void cpu_sparc_dump_state(CPUSPARCState * env, FILE * f, int flags)
{
    int i, x;

    fprintf(f, "pc: 0x%08x  npc: 0x%08x\n", (int) env->pc, (int) env->npc);
    fprintf(f, "General Registers:\n");
    for (i = 0; i < 4; i++)
	fprintf(f, "%%g%c: 0x%08x\t", i + '0', env->gregs[i]);
    fprintf(f, "\n");
    for (; i < 8; i++)
	fprintf(f, "%%g%c: 0x%08x\t", i + '0', env->gregs[i]);
    fprintf(f, "\nCurrent Register Window:\n");
    for (x = 0; x < 3; x++) {
	for (i = 0; i < 4; i++)
	    fprintf(f, "%%%c%d: 0x%08x\t",
		    (x == 0 ? 'o' : (x == 1 ? 'l' : 'i')), i,
		    env->regwptr[i + x * 8]);
	fprintf(f, "\n");
	for (; i < 8; i++)
	    fprintf(f, "%%%c%d: 0x%08x\t",
		    (x == 0 ? 'o' : x == 1 ? 'l' : 'i'), i,
		    env->regwptr[i + x * 8]);
	fprintf(f, "\n");
    }
    fprintf(f, "psr: 0x%08x -> %c%c%c%c wim: 0x%08x\n", env->psr | env->cwp,
	    GET_FLAG(PSR_ZERO, 'Z'), GET_FLAG(PSR_OVF, 'V'),
	    GET_FLAG(PSR_NEG, 'N'), GET_FLAG(PSR_CARRY, 'C'),
            env->wim);
}
