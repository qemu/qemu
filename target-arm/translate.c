/*
 *  ARM translation
 * 
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2005 CodeSourcery, LLC
 *  Copyright (c) 2007 OpenedHand, Ltd.
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

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"

#define ENABLE_ARCH_5J  0
#define ENABLE_ARCH_6   1
#define ENABLE_ARCH_6T2 1

#define ARCH(x) if (!ENABLE_ARCH_##x) goto illegal_op;

/* internal defines */
typedef struct DisasContext {
    target_ulong pc;
    int is_jmp;
    /* Nonzero if this instruction has been conditionally skipped.  */
    int condjmp;
    /* The label that will be jumped to when the instruction is skipped.  */
    int condlabel;
    struct TranslationBlock *tb;
    int singlestep_enabled;
    int thumb;
    int is_mem;
#if !defined(CONFIG_USER_ONLY)
    int user;
#endif
} DisasContext;

#if defined(CONFIG_USER_ONLY)
#define IS_USER(s) 1
#else
#define IS_USER(s) (s->user)
#endif

#define DISAS_JUMP_NEXT 4

#ifdef USE_DIRECT_JUMP
#define TBPARAM(x)
#else
#define TBPARAM(x) (long)(x)
#endif

/* XXX: move that elsewhere */
static uint16_t *gen_opc_ptr;
static uint32_t *gen_opparam_ptr;
extern FILE *logfile;
extern int loglevel;

enum {
#define DEF(s, n, copy_size) INDEX_op_ ## s,
#include "opc.h"
#undef DEF
    NB_OPS,
};

#include "gen-op.h"

static GenOpFunc1 *gen_test_cc[14] = {
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

static GenOpFunc *gen_shift_T1_0[4] = {
    NULL,
    gen_op_shrl_T1_0,
    gen_op_sarl_T1_0,
    gen_op_rrxl_T1,
};

static GenOpFunc1 *gen_shift_T2_im[4] = {
    gen_op_shll_T2_im,
    gen_op_shrl_T2_im,
    gen_op_sarl_T2_im,
    gen_op_rorl_T2_im,
};

static GenOpFunc *gen_shift_T2_0[4] = {
    NULL,
    gen_op_shrl_T2_0,
    gen_op_sarl_T2_0,
    gen_op_rrxl_T2,
};

static GenOpFunc1 *gen_shift_T1_im_cc[4] = {
    gen_op_shll_T1_im_cc,
    gen_op_shrl_T1_im_cc,
    gen_op_sarl_T1_im_cc,
    gen_op_rorl_T1_im_cc,
};

static GenOpFunc *gen_shift_T1_0_cc[4] = {
    NULL,
    gen_op_shrl_T1_0_cc,
    gen_op_sarl_T1_0_cc,
    gen_op_rrxl_T1_cc,
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

static GenOpFunc1 *gen_shift_T0_im_thumb[3] = {
    gen_op_shll_T0_im_thumb,
    gen_op_shrl_T0_im_thumb,
    gen_op_sarl_T0_im_thumb,
};

static inline void gen_bx(DisasContext *s)
{
  s->is_jmp = DISAS_UPDATE;
  gen_op_bx_T0();
}


#if defined(CONFIG_USER_ONLY)
#define gen_ldst(name, s) gen_op_##name##_raw()
#else
#define gen_ldst(name, s) do { \
    s->is_mem = 1; \
    if (IS_USER(s)) \
        gen_op_##name##_user(); \
    else \
        gen_op_##name##_kernel(); \
    } while (0)
#endif

static inline void gen_movl_TN_reg(DisasContext *s, int reg, int t)
{
    int val;

    if (reg == 15) {
        /* normaly, since we updated PC, we need only to add one insn */
        if (s->thumb)
            val = (long)s->pc + 2;
        else
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

/* Force a TB lookup after an instruction that changes the CPU state.  */
static inline void gen_lookup_tb(DisasContext *s)
{
    gen_op_movl_T0_im(s->pc);
    gen_movl_reg_T0(s, 15);
    s->is_jmp = DISAS_UPDATE;
}

static inline void gen_add_data_offset(DisasContext *s, unsigned int insn)
{
    int val, rm, shift, shiftop;

    if (!(insn & (1 << 25))) {
        /* immediate */
        val = insn & 0xfff;
        if (!(insn & (1 << 23)))
            val = -val;
        if (val != 0)
            gen_op_addl_T1_im(val);
    } else {
        /* shift/register */
        rm = (insn) & 0xf;
        shift = (insn >> 7) & 0x1f;
        gen_movl_T2_reg(s, rm);
        shiftop = (insn >> 5) & 3;
        if (shift != 0) {
            gen_shift_T2_im[shiftop](shift);
        } else if (shiftop != 0) {
            gen_shift_T2_0[shiftop]();
        }
        if (!(insn & (1 << 23)))
            gen_op_subl_T1_T2();
        else
            gen_op_addl_T1_T2();
    }
}

static inline void gen_add_datah_offset(DisasContext *s, unsigned int insn,
                                        int extra)
{
    int val, rm;
    
    if (insn & (1 << 22)) {
        /* immediate */
        val = (insn & 0xf) | ((insn >> 4) & 0xf0);
        if (!(insn & (1 << 23)))
            val = -val;
        val += extra;
        if (val != 0)
            gen_op_addl_T1_im(val);
    } else {
        /* register */
        if (extra)
            gen_op_addl_T1_im(extra);
        rm = (insn) & 0xf;
        gen_movl_T2_reg(s, rm);
        if (!(insn & (1 << 23)))
            gen_op_subl_T1_T2();
        else
            gen_op_addl_T1_T2();
    }
}

#define VFP_OP(name)                      \
static inline void gen_vfp_##name(int dp) \
{                                         \
    if (dp)                               \
        gen_op_vfp_##name##d();           \
    else                                  \
        gen_op_vfp_##name##s();           \
}

VFP_OP(add)
VFP_OP(sub)
VFP_OP(mul)
VFP_OP(div)
VFP_OP(neg)
VFP_OP(abs)
VFP_OP(sqrt)
VFP_OP(cmp)
VFP_OP(cmpe)
VFP_OP(F1_ld0)
VFP_OP(uito)
VFP_OP(sito)
VFP_OP(toui)
VFP_OP(touiz)
VFP_OP(tosi)
VFP_OP(tosiz)

#undef VFP_OP

static inline void gen_vfp_ld(DisasContext *s, int dp)
{
    if (dp)
        gen_ldst(vfp_ldd, s);
    else
        gen_ldst(vfp_lds, s);
}

static inline void gen_vfp_st(DisasContext *s, int dp)
{
    if (dp)
        gen_ldst(vfp_std, s);
    else
        gen_ldst(vfp_sts, s);
}

static inline long
vfp_reg_offset (int dp, int reg)
{
    if (dp)
        return offsetof(CPUARMState, vfp.regs[reg]);
    else if (reg & 1) {
        return offsetof(CPUARMState, vfp.regs[reg >> 1])
          + offsetof(CPU_DoubleU, l.upper);
    } else {
        return offsetof(CPUARMState, vfp.regs[reg >> 1])
          + offsetof(CPU_DoubleU, l.lower);
    }
}
static inline void gen_mov_F0_vreg(int dp, int reg)
{
    if (dp)
        gen_op_vfp_getreg_F0d(vfp_reg_offset(dp, reg));
    else
        gen_op_vfp_getreg_F0s(vfp_reg_offset(dp, reg));
}

static inline void gen_mov_F1_vreg(int dp, int reg)
{
    if (dp)
        gen_op_vfp_getreg_F1d(vfp_reg_offset(dp, reg));
    else
        gen_op_vfp_getreg_F1s(vfp_reg_offset(dp, reg));
}

static inline void gen_mov_vreg_F0(int dp, int reg)
{
    if (dp)
        gen_op_vfp_setreg_F0d(vfp_reg_offset(dp, reg));
    else
        gen_op_vfp_setreg_F0s(vfp_reg_offset(dp, reg));
}

#define ARM_CP_RW_BIT	(1 << 20)

static inline int gen_iwmmxt_address(DisasContext *s, uint32_t insn)
{
    int rd;
    uint32_t offset;

    rd = (insn >> 16) & 0xf;
    gen_movl_T1_reg(s, rd);

    offset = (insn & 0xff) << ((insn >> 7) & 2);
    if (insn & (1 << 24)) {
        /* Pre indexed */
        if (insn & (1 << 23))
            gen_op_addl_T1_im(offset);
        else
            gen_op_addl_T1_im(-offset);

        if (insn & (1 << 21))
            gen_movl_reg_T1(s, rd);
    } else if (insn & (1 << 21)) {
        /* Post indexed */
        if (insn & (1 << 23))
            gen_op_movl_T0_im(offset);
        else
            gen_op_movl_T0_im(- offset);
        gen_op_addl_T0_T1();
        gen_movl_reg_T0(s, rd);
    } else if (!(insn & (1 << 23)))
        return 1;
    return 0;
}

static inline int gen_iwmmxt_shift(uint32_t insn, uint32_t mask)
{
    int rd = (insn >> 0) & 0xf;

    if (insn & (1 << 8))
        if (rd < ARM_IWMMXT_wCGR0 || rd > ARM_IWMMXT_wCGR3)
            return 1;
        else
            gen_op_iwmmxt_movl_T0_wCx(rd);
    else
        gen_op_iwmmxt_movl_T0_T1_wRn(rd);

    gen_op_movl_T1_im(mask);
    gen_op_andl_T0_T1();
    return 0;
}

/* Disassemble an iwMMXt instruction.  Returns nonzero if an error occured
   (ie. an undefined instruction).  */
static int disas_iwmmxt_insn(CPUState *env, DisasContext *s, uint32_t insn)
{
    int rd, wrd;
    int rdhi, rdlo, rd0, rd1, i;

    if ((insn & 0x0e000e00) == 0x0c000000) {
        if ((insn & 0x0fe00ff0) == 0x0c400000) {
            wrd = insn & 0xf;
            rdlo = (insn >> 12) & 0xf;
            rdhi = (insn >> 16) & 0xf;
            if (insn & ARM_CP_RW_BIT) {			/* TMRRC */
                gen_op_iwmmxt_movl_T0_T1_wRn(wrd);
                gen_movl_reg_T0(s, rdlo);
                gen_movl_reg_T1(s, rdhi);
            } else {					/* TMCRR */
                gen_movl_T0_reg(s, rdlo);
                gen_movl_T1_reg(s, rdhi);
                gen_op_iwmmxt_movl_wRn_T0_T1(wrd);
                gen_op_iwmmxt_set_mup();
            }
            return 0;
        }

        wrd = (insn >> 12) & 0xf;
        if (gen_iwmmxt_address(s, insn))
            return 1;
        if (insn & ARM_CP_RW_BIT) {
            if ((insn >> 28) == 0xf) {			/* WLDRW wCx */
                gen_ldst(ldl, s);
                gen_op_iwmmxt_movl_wCx_T0(wrd);
            } else {
                if (insn & (1 << 8))
                    if (insn & (1 << 22))		/* WLDRD */
                        gen_ldst(iwmmxt_ldq, s);
                    else				/* WLDRW wRd */
                        gen_ldst(iwmmxt_ldl, s);
                else
                    if (insn & (1 << 22))		/* WLDRH */
                        gen_ldst(iwmmxt_ldw, s);
                    else				/* WLDRB */
                        gen_ldst(iwmmxt_ldb, s);
                gen_op_iwmmxt_movq_wRn_M0(wrd);
            }
        } else {
            if ((insn >> 28) == 0xf) {			/* WSTRW wCx */
                gen_op_iwmmxt_movl_T0_wCx(wrd);
                gen_ldst(stl, s);
            } else {
                gen_op_iwmmxt_movq_M0_wRn(wrd);
                if (insn & (1 << 8))
                    if (insn & (1 << 22))		/* WSTRD */
                        gen_ldst(iwmmxt_stq, s);
                    else				/* WSTRW wRd */
                        gen_ldst(iwmmxt_stl, s);
                else
                    if (insn & (1 << 22))		/* WSTRH */
                        gen_ldst(iwmmxt_ldw, s);
                    else				/* WSTRB */
                        gen_ldst(iwmmxt_stb, s);
            }
        }
        return 0;
    }

    if ((insn & 0x0f000000) != 0x0e000000)
        return 1;

    switch (((insn >> 12) & 0xf00) | ((insn >> 4) & 0xff)) {
    case 0x000:						/* WOR */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 0) & 0xf;
        rd1 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        gen_op_iwmmxt_orq_M0_wRn(rd1);
        gen_op_iwmmxt_setpsr_nz();
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x011:						/* TMCR */
        if (insn & 0xf)
            return 1;
        rd = (insn >> 12) & 0xf;
        wrd = (insn >> 16) & 0xf;
        switch (wrd) {
        case ARM_IWMMXT_wCID:
        case ARM_IWMMXT_wCASF:
            break;
        case ARM_IWMMXT_wCon:
            gen_op_iwmmxt_set_cup();
            /* Fall through.  */
        case ARM_IWMMXT_wCSSF:
            gen_op_iwmmxt_movl_T0_wCx(wrd);
            gen_movl_T1_reg(s, rd);
            gen_op_bicl_T0_T1();
            gen_op_iwmmxt_movl_wCx_T0(wrd);
            break;
        case ARM_IWMMXT_wCGR0:
        case ARM_IWMMXT_wCGR1:
        case ARM_IWMMXT_wCGR2:
        case ARM_IWMMXT_wCGR3:
            gen_op_iwmmxt_set_cup();
            gen_movl_reg_T0(s, rd);
            gen_op_iwmmxt_movl_wCx_T0(wrd);
            break;
        default:
            return 1;
        }
        break;
    case 0x100:						/* WXOR */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 0) & 0xf;
        rd1 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        gen_op_iwmmxt_xorq_M0_wRn(rd1);
        gen_op_iwmmxt_setpsr_nz();
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x111:						/* TMRC */
        if (insn & 0xf)
            return 1;
        rd = (insn >> 12) & 0xf;
        wrd = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movl_T0_wCx(wrd);
        gen_movl_reg_T0(s, rd);
        break;
    case 0x300:						/* WANDN */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 0) & 0xf;
        rd1 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        gen_op_iwmmxt_negq_M0();
        gen_op_iwmmxt_andq_M0_wRn(rd1);
        gen_op_iwmmxt_setpsr_nz();
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x200:						/* WAND */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 0) & 0xf;
        rd1 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        gen_op_iwmmxt_andq_M0_wRn(rd1);
        gen_op_iwmmxt_setpsr_nz();
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x810: case 0xa10:				/* WMADD */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 0) & 0xf;
        rd1 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (insn & (1 << 21))
            gen_op_iwmmxt_maddsq_M0_wRn(rd1);
        else
            gen_op_iwmmxt_madduq_M0_wRn(rd1);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x10e: case 0x50e: case 0x90e: case 0xd0e:	/* WUNPCKIL */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            gen_op_iwmmxt_unpacklb_M0_wRn(rd1);
            break;
        case 1:
            gen_op_iwmmxt_unpacklw_M0_wRn(rd1);
            break;
        case 2:
            gen_op_iwmmxt_unpackll_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x10c: case 0x50c: case 0x90c: case 0xd0c:	/* WUNPCKIH */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            gen_op_iwmmxt_unpackhb_M0_wRn(rd1);
            break;
        case 1:
            gen_op_iwmmxt_unpackhw_M0_wRn(rd1);
            break;
        case 2:
            gen_op_iwmmxt_unpackhl_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x012: case 0x112: case 0x412: case 0x512:	/* WSAD */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (insn & (1 << 22))
            gen_op_iwmmxt_sadw_M0_wRn(rd1);
        else
            gen_op_iwmmxt_sadb_M0_wRn(rd1);
        if (!(insn & (1 << 20)))
            gen_op_iwmmxt_addl_M0_wRn(wrd);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x010: case 0x110: case 0x210: case 0x310:	/* WMUL */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (insn & (1 << 21))
            gen_op_iwmmxt_mulsw_M0_wRn(rd1, (insn & (1 << 20)) ? 16 : 0);
        else
            gen_op_iwmmxt_muluw_M0_wRn(rd1, (insn & (1 << 20)) ? 16 : 0);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x410: case 0x510: case 0x610: case 0x710:	/* WMAC */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (insn & (1 << 21))
            gen_op_iwmmxt_macsw_M0_wRn(rd1);
        else
            gen_op_iwmmxt_macuw_M0_wRn(rd1);
        if (!(insn & (1 << 20))) {
            if (insn & (1 << 21))
                gen_op_iwmmxt_addsq_M0_wRn(wrd);
            else
                gen_op_iwmmxt_adduq_M0_wRn(wrd);
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x006: case 0x406: case 0x806: case 0xc06:	/* WCMPEQ */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            gen_op_iwmmxt_cmpeqb_M0_wRn(rd1);
            break;
        case 1:
            gen_op_iwmmxt_cmpeqw_M0_wRn(rd1);
            break;
        case 2:
            gen_op_iwmmxt_cmpeql_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x800: case 0x900: case 0xc00: case 0xd00:	/* WAVG2 */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (insn & (1 << 22))
            gen_op_iwmmxt_avgw_M0_wRn(rd1, (insn >> 20) & 1);
        else
            gen_op_iwmmxt_avgb_M0_wRn(rd1, (insn >> 20) & 1);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x802: case 0x902: case 0xa02: case 0xb02:	/* WALIGNR */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        gen_op_iwmmxt_movl_T0_wCx(ARM_IWMMXT_wCGR0 + ((insn >> 20) & 3));
        gen_op_movl_T1_im(7);
        gen_op_andl_T0_T1();
        gen_op_iwmmxt_align_M0_T0_wRn(rd1);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x601: case 0x605: case 0x609: case 0x60d:	/* TINSR */
        rd = (insn >> 12) & 0xf;
        wrd = (insn >> 16) & 0xf;
        gen_movl_T0_reg(s, rd);
        gen_op_iwmmxt_movq_M0_wRn(wrd);
        switch ((insn >> 6) & 3) {
        case 0:
            gen_op_movl_T1_im(0xff);
            gen_op_iwmmxt_insr_M0_T0_T1((insn & 7) << 3);
            break;
        case 1:
            gen_op_movl_T1_im(0xffff);
            gen_op_iwmmxt_insr_M0_T0_T1((insn & 3) << 4);
            break;
        case 2:
            gen_op_movl_T1_im(0xffffffff);
            gen_op_iwmmxt_insr_M0_T0_T1((insn & 1) << 5);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x107: case 0x507: case 0x907: case 0xd07:	/* TEXTRM */
        rd = (insn >> 12) & 0xf;
        wrd = (insn >> 16) & 0xf;
        if (rd == 15)
            return 1;
        gen_op_iwmmxt_movq_M0_wRn(wrd);
        switch ((insn >> 22) & 3) {
        case 0:
            if (insn & 8)
                gen_op_iwmmxt_extrsb_T0_M0((insn & 7) << 3);
            else {
                gen_op_movl_T1_im(0xff);
                gen_op_iwmmxt_extru_T0_M0_T1((insn & 7) << 3);
            }
            break;
        case 1:
            if (insn & 8)
                gen_op_iwmmxt_extrsw_T0_M0((insn & 3) << 4);
            else {
                gen_op_movl_T1_im(0xffff);
                gen_op_iwmmxt_extru_T0_M0_T1((insn & 3) << 4);
            }
            break;
        case 2:
            gen_op_movl_T1_im(0xffffffff);
            gen_op_iwmmxt_extru_T0_M0_T1((insn & 1) << 5);
            break;
        case 3:
            return 1;
        }
        gen_op_movl_reg_TN[0][rd]();
        break;
    case 0x117: case 0x517: case 0x917: case 0xd17:	/* TEXTRC */
        if ((insn & 0x000ff008) != 0x0003f000)
            return 1;
        gen_op_iwmmxt_movl_T1_wCx(ARM_IWMMXT_wCASF);
        switch ((insn >> 22) & 3) {
        case 0:
            gen_op_shrl_T1_im(((insn & 7) << 2) + 0);
            break;
        case 1:
            gen_op_shrl_T1_im(((insn & 3) << 3) + 4);
            break;
        case 2:
            gen_op_shrl_T1_im(((insn & 1) << 4) + 12);
            break;
        case 3:
            return 1;
        }
        gen_op_shll_T1_im(28);
        gen_op_movl_T0_T1();
        gen_op_movl_cpsr_T0(0xf0000000);
        break;
    case 0x401: case 0x405: case 0x409: case 0x40d:	/* TBCST */
        rd = (insn >> 12) & 0xf;
        wrd = (insn >> 16) & 0xf;
        gen_movl_T0_reg(s, rd);
        switch ((insn >> 6) & 3) {
        case 0:
            gen_op_iwmmxt_bcstb_M0_T0();
            break;
        case 1:
            gen_op_iwmmxt_bcstw_M0_T0();
            break;
        case 2:
            gen_op_iwmmxt_bcstl_M0_T0();
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x113: case 0x513: case 0x913: case 0xd13:	/* TANDC */
        if ((insn & 0x000ff00f) != 0x0003f000)
            return 1;
        gen_op_iwmmxt_movl_T1_wCx(ARM_IWMMXT_wCASF);
        switch ((insn >> 22) & 3) {
        case 0:
            for (i = 0; i < 7; i ++) {
                gen_op_shll_T1_im(4);
                gen_op_andl_T0_T1();
            }
            break;
        case 1:
            for (i = 0; i < 3; i ++) {
                gen_op_shll_T1_im(8);
                gen_op_andl_T0_T1();
            }
            break;
        case 2:
            gen_op_shll_T1_im(16);
            gen_op_andl_T0_T1();
            break;
        case 3:
            return 1;
        }
        gen_op_movl_cpsr_T0(0xf0000000);
        break;
    case 0x01c: case 0x41c: case 0x81c: case 0xc1c:	/* WACC */
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            gen_op_iwmmxt_addcb_M0();
            break;
        case 1:
            gen_op_iwmmxt_addcw_M0();
            break;
        case 2:
            gen_op_iwmmxt_addcl_M0();
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x115: case 0x515: case 0x915: case 0xd15:	/* TORC */
        if ((insn & 0x000ff00f) != 0x0003f000)
            return 1;
        gen_op_iwmmxt_movl_T1_wCx(ARM_IWMMXT_wCASF);
        switch ((insn >> 22) & 3) {
        case 0:
            for (i = 0; i < 7; i ++) {
                gen_op_shll_T1_im(4);
                gen_op_orl_T0_T1();
            }
            break;
        case 1:
            for (i = 0; i < 3; i ++) {
                gen_op_shll_T1_im(8);
                gen_op_orl_T0_T1();
            }
            break;
        case 2:
            gen_op_shll_T1_im(16);
            gen_op_orl_T0_T1();
            break;
        case 3:
            return 1;
        }
        gen_op_movl_T1_im(0xf0000000);
        gen_op_andl_T0_T1();
        gen_op_movl_cpsr_T0(0xf0000000);
        break;
    case 0x103: case 0x503: case 0x903: case 0xd03:	/* TMOVMSK */
        rd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        if ((insn & 0xf) != 0)
            return 1;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            gen_op_iwmmxt_msbb_T0_M0();
            break;
        case 1:
            gen_op_iwmmxt_msbw_T0_M0();
            break;
        case 2:
            gen_op_iwmmxt_msbl_T0_M0();
            break;
        case 3:
            return 1;
        }
        gen_movl_reg_T0(s, rd);
        break;
    case 0x106: case 0x306: case 0x506: case 0x706:	/* WCMPGT */
    case 0x906: case 0xb06: case 0xd06: case 0xf06:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            if (insn & (1 << 21))
                gen_op_iwmmxt_cmpgtsb_M0_wRn(rd1);
            else
                gen_op_iwmmxt_cmpgtub_M0_wRn(rd1);
            break;
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_cmpgtsw_M0_wRn(rd1);
            else
                gen_op_iwmmxt_cmpgtuw_M0_wRn(rd1);
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_cmpgtsl_M0_wRn(rd1);
            else
                gen_op_iwmmxt_cmpgtul_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x00e: case 0x20e: case 0x40e: case 0x60e:	/* WUNPCKEL */
    case 0x80e: case 0xa0e: case 0xc0e: case 0xe0e:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpacklsb_M0();
            else
                gen_op_iwmmxt_unpacklub_M0();
            break;
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpacklsw_M0();
            else
                gen_op_iwmmxt_unpackluw_M0();
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpacklsl_M0();
            else
                gen_op_iwmmxt_unpacklul_M0();
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x00c: case 0x20c: case 0x40c: case 0x60c:	/* WUNPCKEH */
    case 0x80c: case 0xa0c: case 0xc0c: case 0xe0c:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpackhsb_M0();
            else
                gen_op_iwmmxt_unpackhub_M0();
            break;
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpackhsw_M0();
            else
                gen_op_iwmmxt_unpackhuw_M0();
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_unpackhsl_M0();
            else
                gen_op_iwmmxt_unpackhul_M0();
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x204: case 0x604: case 0xa04: case 0xe04:	/* WSRL */
    case 0x214: case 0x614: case 0xa14: case 0xe14:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (gen_iwmmxt_shift(insn, 0xff))
            return 1;
        switch ((insn >> 22) & 3) {
        case 0:
            return 1;
        case 1:
            gen_op_iwmmxt_srlw_M0_T0();
            break;
        case 2:
            gen_op_iwmmxt_srll_M0_T0();
            break;
        case 3:
            gen_op_iwmmxt_srlq_M0_T0();
            break;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x004: case 0x404: case 0x804: case 0xc04:	/* WSRA */
    case 0x014: case 0x414: case 0x814: case 0xc14:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (gen_iwmmxt_shift(insn, 0xff))
            return 1;
        switch ((insn >> 22) & 3) {
        case 0:
            return 1;
        case 1:
            gen_op_iwmmxt_sraw_M0_T0();
            break;
        case 2:
            gen_op_iwmmxt_sral_M0_T0();
            break;
        case 3:
            gen_op_iwmmxt_sraq_M0_T0();
            break;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x104: case 0x504: case 0x904: case 0xd04:	/* WSLL */
    case 0x114: case 0x514: case 0x914: case 0xd14:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (gen_iwmmxt_shift(insn, 0xff))
            return 1;
        switch ((insn >> 22) & 3) {
        case 0:
            return 1;
        case 1:
            gen_op_iwmmxt_sllw_M0_T0();
            break;
        case 2:
            gen_op_iwmmxt_slll_M0_T0();
            break;
        case 3:
            gen_op_iwmmxt_sllq_M0_T0();
            break;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x304: case 0x704: case 0xb04: case 0xf04:	/* WROR */
    case 0x314: case 0x714: case 0xb14: case 0xf14:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            return 1;
        case 1:
            if (gen_iwmmxt_shift(insn, 0xf))
                return 1;
            gen_op_iwmmxt_rorw_M0_T0();
            break;
        case 2:
            if (gen_iwmmxt_shift(insn, 0x1f))
                return 1;
            gen_op_iwmmxt_rorl_M0_T0();
            break;
        case 3:
            if (gen_iwmmxt_shift(insn, 0x3f))
                return 1;
            gen_op_iwmmxt_rorq_M0_T0();
            break;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x116: case 0x316: case 0x516: case 0x716:	/* WMIN */
    case 0x916: case 0xb16: case 0xd16: case 0xf16:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            if (insn & (1 << 21))
                gen_op_iwmmxt_minsb_M0_wRn(rd1);
            else
                gen_op_iwmmxt_minub_M0_wRn(rd1);
            break;
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_minsw_M0_wRn(rd1);
            else
                gen_op_iwmmxt_minuw_M0_wRn(rd1);
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_minsl_M0_wRn(rd1);
            else
                gen_op_iwmmxt_minul_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x016: case 0x216: case 0x416: case 0x616:	/* WMAX */
    case 0x816: case 0xa16: case 0xc16: case 0xe16:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 22) & 3) {
        case 0:
            if (insn & (1 << 21))
                gen_op_iwmmxt_maxsb_M0_wRn(rd1);
            else
                gen_op_iwmmxt_maxub_M0_wRn(rd1);
            break;
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_maxsw_M0_wRn(rd1);
            else
                gen_op_iwmmxt_maxuw_M0_wRn(rd1);
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_maxsl_M0_wRn(rd1);
            else
                gen_op_iwmmxt_maxul_M0_wRn(rd1);
            break;
        case 3:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x002: case 0x102: case 0x202: case 0x302:	/* WALIGNI */
    case 0x402: case 0x502: case 0x602: case 0x702:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        gen_op_movl_T0_im((insn >> 20) & 3);
        gen_op_iwmmxt_align_M0_T0_wRn(rd1);
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    case 0x01a: case 0x11a: case 0x21a: case 0x31a:	/* WSUB */
    case 0x41a: case 0x51a: case 0x61a: case 0x71a:
    case 0x81a: case 0x91a: case 0xa1a: case 0xb1a:
    case 0xc1a: case 0xd1a: case 0xe1a: case 0xf1a:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 20) & 0xf) {
        case 0x0:
            gen_op_iwmmxt_subnb_M0_wRn(rd1);
            break;
        case 0x1:
            gen_op_iwmmxt_subub_M0_wRn(rd1);
            break;
        case 0x3:
            gen_op_iwmmxt_subsb_M0_wRn(rd1);
            break;
        case 0x4:
            gen_op_iwmmxt_subnw_M0_wRn(rd1);
            break;
        case 0x5:
            gen_op_iwmmxt_subuw_M0_wRn(rd1);
            break;
        case 0x7:
            gen_op_iwmmxt_subsw_M0_wRn(rd1);
            break;
        case 0x8:
            gen_op_iwmmxt_subnl_M0_wRn(rd1);
            break;
        case 0x9:
            gen_op_iwmmxt_subul_M0_wRn(rd1);
            break;
        case 0xb:
            gen_op_iwmmxt_subsl_M0_wRn(rd1);
            break;
        default:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x01e: case 0x11e: case 0x21e: case 0x31e:	/* WSHUFH */
    case 0x41e: case 0x51e: case 0x61e: case 0x71e:
    case 0x81e: case 0x91e: case 0xa1e: case 0xb1e:
    case 0xc1e: case 0xd1e: case 0xe1e: case 0xf1e:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        gen_op_movl_T0_im(((insn >> 16) & 0xf0) | (insn & 0x0f));
        gen_op_iwmmxt_shufh_M0_T0();
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x018: case 0x118: case 0x218: case 0x318:	/* WADD */
    case 0x418: case 0x518: case 0x618: case 0x718:
    case 0x818: case 0x918: case 0xa18: case 0xb18:
    case 0xc18: case 0xd18: case 0xe18: case 0xf18:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        switch ((insn >> 20) & 0xf) {
        case 0x0:
            gen_op_iwmmxt_addnb_M0_wRn(rd1);
            break;
        case 0x1:
            gen_op_iwmmxt_addub_M0_wRn(rd1);
            break;
        case 0x3:
            gen_op_iwmmxt_addsb_M0_wRn(rd1);
            break;
        case 0x4:
            gen_op_iwmmxt_addnw_M0_wRn(rd1);
            break;
        case 0x5:
            gen_op_iwmmxt_adduw_M0_wRn(rd1);
            break;
        case 0x7:
            gen_op_iwmmxt_addsw_M0_wRn(rd1);
            break;
        case 0x8:
            gen_op_iwmmxt_addnl_M0_wRn(rd1);
            break;
        case 0x9:
            gen_op_iwmmxt_addul_M0_wRn(rd1);
            break;
        case 0xb:
            gen_op_iwmmxt_addsl_M0_wRn(rd1);
            break;
        default:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x008: case 0x108: case 0x208: case 0x308:	/* WPACK */
    case 0x408: case 0x508: case 0x608: case 0x708:
    case 0x808: case 0x908: case 0xa08: case 0xb08:
    case 0xc08: case 0xd08: case 0xe08: case 0xf08:
        wrd = (insn >> 12) & 0xf;
        rd0 = (insn >> 16) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        gen_op_iwmmxt_movq_M0_wRn(rd0);
        if (!(insn & (1 << 20)))
            return 1;
        switch ((insn >> 22) & 3) {
        case 0:
            return 1;
        case 1:
            if (insn & (1 << 21))
                gen_op_iwmmxt_packsw_M0_wRn(rd1);
            else
                gen_op_iwmmxt_packuw_M0_wRn(rd1);
            break;
        case 2:
            if (insn & (1 << 21))
                gen_op_iwmmxt_packsl_M0_wRn(rd1);
            else
                gen_op_iwmmxt_packul_M0_wRn(rd1);
            break;
        case 3:
            if (insn & (1 << 21))
                gen_op_iwmmxt_packsq_M0_wRn(rd1);
            else
                gen_op_iwmmxt_packuq_M0_wRn(rd1);
            break;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        gen_op_iwmmxt_set_cup();
        break;
    case 0x201: case 0x203: case 0x205: case 0x207:
    case 0x209: case 0x20b: case 0x20d: case 0x20f:
    case 0x211: case 0x213: case 0x215: case 0x217:
    case 0x219: case 0x21b: case 0x21d: case 0x21f:
        wrd = (insn >> 5) & 0xf;
        rd0 = (insn >> 12) & 0xf;
        rd1 = (insn >> 0) & 0xf;
        if (rd0 == 0xf || rd1 == 0xf)
            return 1;
        gen_op_iwmmxt_movq_M0_wRn(wrd);
        switch ((insn >> 16) & 0xf) {
        case 0x0:					/* TMIA */
            gen_op_movl_TN_reg[0][rd0]();
            gen_op_movl_TN_reg[1][rd1]();
            gen_op_iwmmxt_muladdsl_M0_T0_T1();
            break;
        case 0x8:					/* TMIAPH */
            gen_op_movl_TN_reg[0][rd0]();
            gen_op_movl_TN_reg[1][rd1]();
            gen_op_iwmmxt_muladdsw_M0_T0_T1();
            break;
        case 0xc: case 0xd: case 0xe: case 0xf:		/* TMIAxy */
            gen_op_movl_TN_reg[1][rd0]();
            if (insn & (1 << 16))
                gen_op_shrl_T1_im(16);
            gen_op_movl_T0_T1();
            gen_op_movl_TN_reg[1][rd1]();
            if (insn & (1 << 17))
                gen_op_shrl_T1_im(16);
            gen_op_iwmmxt_muladdswl_M0_T0_T1();
            break;
        default:
            return 1;
        }
        gen_op_iwmmxt_movq_wRn_M0(wrd);
        gen_op_iwmmxt_set_mup();
        break;
    default:
        return 1;
    }

    return 0;
}

/* Disassemble an XScale DSP instruction.  Returns nonzero if an error occured
   (ie. an undefined instruction).  */
static int disas_dsp_insn(CPUState *env, DisasContext *s, uint32_t insn)
{
    int acc, rd0, rd1, rdhi, rdlo;

    if ((insn & 0x0ff00f10) == 0x0e200010) {
        /* Multiply with Internal Accumulate Format */
        rd0 = (insn >> 12) & 0xf;
        rd1 = insn & 0xf;
        acc = (insn >> 5) & 7;

        if (acc != 0)
            return 1;

        switch ((insn >> 16) & 0xf) {
        case 0x0:					/* MIA */
            gen_op_movl_TN_reg[0][rd0]();
            gen_op_movl_TN_reg[1][rd1]();
            gen_op_iwmmxt_muladdsl_M0_T0_T1();
            break;
        case 0x8:					/* MIAPH */
            gen_op_movl_TN_reg[0][rd0]();
            gen_op_movl_TN_reg[1][rd1]();
            gen_op_iwmmxt_muladdsw_M0_T0_T1();
            break;
        case 0xc:					/* MIABB */
        case 0xd:					/* MIABT */
        case 0xe:					/* MIATB */
        case 0xf:					/* MIATT */
            gen_op_movl_TN_reg[1][rd0]();
            if (insn & (1 << 16))
                gen_op_shrl_T1_im(16);
            gen_op_movl_T0_T1();
            gen_op_movl_TN_reg[1][rd1]();
            if (insn & (1 << 17))
                gen_op_shrl_T1_im(16);
            gen_op_iwmmxt_muladdswl_M0_T0_T1();
            break;
        default:
            return 1;
        }

        gen_op_iwmmxt_movq_wRn_M0(acc);
        return 0;
    }

    if ((insn & 0x0fe00ff8) == 0x0c400000) {
        /* Internal Accumulator Access Format */
        rdhi = (insn >> 16) & 0xf;
        rdlo = (insn >> 12) & 0xf;
        acc = insn & 7;

        if (acc != 0)
            return 1;

        if (insn & ARM_CP_RW_BIT) {			/* MRA */
            gen_op_iwmmxt_movl_T0_T1_wRn(acc);
            gen_op_movl_reg_TN[0][rdlo]();
            gen_op_movl_T0_im((1 << (40 - 32)) - 1);
            gen_op_andl_T0_T1();
            gen_op_movl_reg_TN[0][rdhi]();
        } else {					/* MAR */
            gen_op_movl_TN_reg[0][rdlo]();
            gen_op_movl_TN_reg[1][rdhi]();
            gen_op_iwmmxt_movl_wRn_T0_T1(acc);
        }
        return 0;
    }

    return 1;
}

/* Disassemble system coprocessor instruction.  Return nonzero if
   instruction is not defined.  */
static int disas_cp_insn(CPUState *env, DisasContext *s, uint32_t insn)
{
    uint32_t rd = (insn >> 12) & 0xf;
    uint32_t cp = (insn >> 8) & 0xf;
    if (IS_USER(s)) {
        return 1;
    }

    if (insn & ARM_CP_RW_BIT) {
        if (!env->cp[cp].cp_read)
            return 1;
        gen_op_movl_T0_im((uint32_t) s->pc);
        gen_op_movl_reg_TN[0][15]();
        gen_op_movl_T0_cp(insn);
        gen_movl_reg_T0(s, rd);
    } else {
        if (!env->cp[cp].cp_write)
            return 1;
        gen_op_movl_T0_im((uint32_t) s->pc);
        gen_op_movl_reg_TN[0][15]();
        gen_movl_T0_reg(s, rd);
        gen_op_movl_cp_T0(insn);
    }
    return 0;
}

/* Disassemble system coprocessor (cp15) instruction.  Return nonzero if
   instruction is not defined.  */
static int disas_cp15_insn(CPUState *env, DisasContext *s, uint32_t insn)
{
    uint32_t rd;

    /* ??? Some cp15 registers are accessible from userspace.  */
    if (IS_USER(s)) {
        return 1;
    }
    if ((insn & 0x0fff0fff) == 0x0e070f90
        || (insn & 0x0fff0fff) == 0x0e070f58) {
        /* Wait for interrupt.  */
        gen_op_movl_T0_im((long)s->pc);
        gen_op_movl_reg_TN[0][15]();
        gen_op_wfi();
        s->is_jmp = DISAS_JUMP;
        return 0;
    }
    rd = (insn >> 12) & 0xf;
    if (insn & ARM_CP_RW_BIT) {
        gen_op_movl_T0_cp15(insn);
        /* If the destination register is r15 then sets condition codes.  */
        if (rd != 15)
            gen_movl_reg_T0(s, rd);
    } else {
        gen_movl_T0_reg(s, rd);
        gen_op_movl_cp15_T0(insn);
        /* Normally we would always end the TB here, but Linux
         * arch/arm/mach-pxa/sleep.S expects two instructions following
         * an MMU enable to execute from cache.  Imitate this behaviour.  */
        if (!arm_feature(env, ARM_FEATURE_XSCALE) ||
                (insn & 0x0fff0fff) != 0x0e010f10)
            gen_lookup_tb(s);
    }
    return 0;
}

/* Disassemble a VFP instruction.  Returns nonzero if an error occured
   (ie. an undefined instruction).  */
static int disas_vfp_insn(CPUState * env, DisasContext *s, uint32_t insn)
{
    uint32_t rd, rn, rm, op, i, n, offset, delta_d, delta_m, bank_mask;
    int dp, veclen;

    if (!arm_feature(env, ARM_FEATURE_VFP))
        return 1;

    if ((env->vfp.xregs[ARM_VFP_FPEXC] & (1 << 30)) == 0) {
        /* VFP disabled.  Only allow fmxr/fmrx to/from fpexc and fpsid.  */
        if ((insn & 0x0fe00fff) != 0x0ee00a10)
            return 1;
        rn = (insn >> 16) & 0xf;
        if (rn != 0 && rn != 8)
            return 1;
    }
    dp = ((insn & 0xf00) == 0xb00);
    switch ((insn >> 24) & 0xf) {
    case 0xe:
        if (insn & (1 << 4)) {
            /* single register transfer */
            if ((insn & 0x6f) != 0x00)
                return 1;
            rd = (insn >> 12) & 0xf;
            if (dp) {
                if (insn & 0x80)
                    return 1;
                rn = (insn >> 16) & 0xf;
                /* Get the existing value even for arm->vfp moves because
                   we only set half the register.  */
                gen_mov_F0_vreg(1, rn);
                gen_op_vfp_mrrd();
                if (insn & ARM_CP_RW_BIT) {
                    /* vfp->arm */
                    if (insn & (1 << 21))
                        gen_movl_reg_T1(s, rd);
                    else
                        gen_movl_reg_T0(s, rd);
                } else {
                    /* arm->vfp */
                    if (insn & (1 << 21))
                        gen_movl_T1_reg(s, rd);
                    else
                        gen_movl_T0_reg(s, rd);
                    gen_op_vfp_mdrr();
                    gen_mov_vreg_F0(dp, rn);
                }
            } else {
                rn = ((insn >> 15) & 0x1e) | ((insn >> 7) & 1);
                if (insn & ARM_CP_RW_BIT) {
                    /* vfp->arm */
                    if (insn & (1 << 21)) {
                        /* system register */
                        rn >>= 1;
                        switch (rn) {
                        case ARM_VFP_FPSID:
                        case ARM_VFP_FPEXC:
                        case ARM_VFP_FPINST:
                        case ARM_VFP_FPINST2:
                            gen_op_vfp_movl_T0_xreg(rn);
                            break;
                        case ARM_VFP_FPSCR:
			    if (rd == 15)
				gen_op_vfp_movl_T0_fpscr_flags();
			    else
				gen_op_vfp_movl_T0_fpscr();
                            break;
                        default:
                            return 1;
                        }
                    } else {
                        gen_mov_F0_vreg(0, rn);
                        gen_op_vfp_mrs();
                    }
                    if (rd == 15) {
                        /* Set the 4 flag bits in the CPSR.  */
                        gen_op_movl_cpsr_T0(0xf0000000);
                    } else
                        gen_movl_reg_T0(s, rd);
                } else {
                    /* arm->vfp */
                    gen_movl_T0_reg(s, rd);
                    if (insn & (1 << 21)) {
                        rn >>= 1;
                        /* system register */
                        switch (rn) {
                        case ARM_VFP_FPSID:
                            /* Writes are ignored.  */
                            break;
                        case ARM_VFP_FPSCR:
                            gen_op_vfp_movl_fpscr_T0();
                            gen_lookup_tb(s);
                            break;
                        case ARM_VFP_FPEXC:
                            gen_op_vfp_movl_xreg_T0(rn);
                            gen_lookup_tb(s);
                            break;
                        case ARM_VFP_FPINST:
                        case ARM_VFP_FPINST2:
                            gen_op_vfp_movl_xreg_T0(rn);
                            break;
                        default:
                            return 1;
                        }
                    } else {
                        gen_op_vfp_msr();
                        gen_mov_vreg_F0(0, rn);
                    }
                }
            }
        } else {
            /* data processing */
            /* The opcode is in bits 23, 21, 20 and 6.  */
            op = ((insn >> 20) & 8) | ((insn >> 19) & 6) | ((insn >> 6) & 1);
            if (dp) {
                if (op == 15) {
                    /* rn is opcode */
                    rn = ((insn >> 15) & 0x1e) | ((insn >> 7) & 1);
                } else {
                    /* rn is register number */
                    if (insn & (1 << 7))
                        return 1;
                    rn = (insn >> 16) & 0xf;
                }

                if (op == 15 && (rn == 15 || rn > 17)) {
                    /* Integer or single precision destination.  */
                    rd = ((insn >> 11) & 0x1e) | ((insn >> 22) & 1);
                } else {
                    if (insn & (1 << 22))
                        return 1;
                    rd = (insn >> 12) & 0xf;
                }

                if (op == 15 && (rn == 16 || rn == 17)) {
                    /* Integer source.  */
                    rm = ((insn << 1) & 0x1e) | ((insn >> 5) & 1);
                } else {
                    if (insn & (1 << 5))
                        return 1;
                    rm = insn & 0xf;
                }
            } else {
                rn = ((insn >> 15) & 0x1e) | ((insn >> 7) & 1);
                if (op == 15 && rn == 15) {
                    /* Double precision destination.  */
                    if (insn & (1 << 22))
                        return 1;
                    rd = (insn >> 12) & 0xf;
                } else
                    rd = ((insn >> 11) & 0x1e) | ((insn >> 22) & 1);
                rm = ((insn << 1) & 0x1e) | ((insn >> 5) & 1);
            }

            veclen = env->vfp.vec_len;
            if (op == 15 && rn > 3)
                veclen = 0;

            /* Shut up compiler warnings.  */
            delta_m = 0;
            delta_d = 0;
            bank_mask = 0;
            
            if (veclen > 0) {
                if (dp)
                    bank_mask = 0xc;
                else
                    bank_mask = 0x18;

                /* Figure out what type of vector operation this is.  */
                if ((rd & bank_mask) == 0) {
                    /* scalar */
                    veclen = 0;
                } else {
                    if (dp)
                        delta_d = (env->vfp.vec_stride >> 1) + 1;
                    else
                        delta_d = env->vfp.vec_stride + 1;

                    if ((rm & bank_mask) == 0) {
                        /* mixed scalar/vector */
                        delta_m = 0;
                    } else {
                        /* vector */
                        delta_m = delta_d;
                    }
                }
            }

            /* Load the initial operands.  */
            if (op == 15) {
                switch (rn) {
                case 16:
                case 17:
                    /* Integer source */
                    gen_mov_F0_vreg(0, rm);
                    break;
                case 8:
                case 9:
                    /* Compare */
                    gen_mov_F0_vreg(dp, rd);
                    gen_mov_F1_vreg(dp, rm);
                    break;
                case 10:
                case 11:
                    /* Compare with zero */
                    gen_mov_F0_vreg(dp, rd);
                    gen_vfp_F1_ld0(dp);
                    break;
                default:
                    /* One source operand.  */
                    gen_mov_F0_vreg(dp, rm);
                }
            } else {
                /* Two source operands.  */
                gen_mov_F0_vreg(dp, rn);
                gen_mov_F1_vreg(dp, rm);
            }

            for (;;) {
                /* Perform the calculation.  */
                switch (op) {
                case 0: /* mac: fd + (fn * fm) */
                    gen_vfp_mul(dp);
                    gen_mov_F1_vreg(dp, rd);
                    gen_vfp_add(dp);
                    break;
                case 1: /* nmac: fd - (fn * fm) */
                    gen_vfp_mul(dp);
                    gen_vfp_neg(dp);
                    gen_mov_F1_vreg(dp, rd);
                    gen_vfp_add(dp);
                    break;
                case 2: /* msc: -fd + (fn * fm) */
                    gen_vfp_mul(dp);
                    gen_mov_F1_vreg(dp, rd);
                    gen_vfp_sub(dp);
                    break;
                case 3: /* nmsc: -fd - (fn * fm)  */
                    gen_vfp_mul(dp);
                    gen_mov_F1_vreg(dp, rd);
                    gen_vfp_add(dp);
                    gen_vfp_neg(dp);
                    break;
                case 4: /* mul: fn * fm */
                    gen_vfp_mul(dp);
                    break;
                case 5: /* nmul: -(fn * fm) */
                    gen_vfp_mul(dp);
                    gen_vfp_neg(dp);
                    break;
                case 6: /* add: fn + fm */
                    gen_vfp_add(dp);
                    break;
                case 7: /* sub: fn - fm */
                    gen_vfp_sub(dp);
                    break;
                case 8: /* div: fn / fm */
                    gen_vfp_div(dp);
                    break;
                case 15: /* extension space */
                    switch (rn) {
                    case 0: /* cpy */
                        /* no-op */
                        break;
                    case 1: /* abs */
                        gen_vfp_abs(dp);
                        break;
                    case 2: /* neg */
                        gen_vfp_neg(dp);
                        break;
                    case 3: /* sqrt */
                        gen_vfp_sqrt(dp);
                        break;
                    case 8: /* cmp */
                        gen_vfp_cmp(dp);
                        break;
                    case 9: /* cmpe */
                        gen_vfp_cmpe(dp);
                        break;
                    case 10: /* cmpz */
                        gen_vfp_cmp(dp);
                        break;
                    case 11: /* cmpez */
                        gen_vfp_F1_ld0(dp);
                        gen_vfp_cmpe(dp);
                        break;
                    case 15: /* single<->double conversion */
                        if (dp)
                            gen_op_vfp_fcvtsd();
                        else
                            gen_op_vfp_fcvtds();
                        break;
                    case 16: /* fuito */
                        gen_vfp_uito(dp);
                        break;
                    case 17: /* fsito */
                        gen_vfp_sito(dp);
                        break;
                    case 24: /* ftoui */
                        gen_vfp_toui(dp);
                        break;
                    case 25: /* ftouiz */
                        gen_vfp_touiz(dp);
                        break;
                    case 26: /* ftosi */
                        gen_vfp_tosi(dp);
                        break;
                    case 27: /* ftosiz */
                        gen_vfp_tosiz(dp);
                        break;
                    default: /* undefined */
                        printf ("rn:%d\n", rn);
                        return 1;
                    }
                    break;
                default: /* undefined */
                    printf ("op:%d\n", op);
                    return 1;
                }

                /* Write back the result.  */
                if (op == 15 && (rn >= 8 && rn <= 11))
                    ; /* Comparison, do nothing.  */
                else if (op == 15 && rn > 17)
                    /* Integer result.  */
                    gen_mov_vreg_F0(0, rd);
                else if (op == 15 && rn == 15)
                    /* conversion */
                    gen_mov_vreg_F0(!dp, rd);
                else
                    gen_mov_vreg_F0(dp, rd);

                /* break out of the loop if we have finished  */
                if (veclen == 0)
                    break;

                if (op == 15 && delta_m == 0) {
                    /* single source one-many */
                    while (veclen--) {
                        rd = ((rd + delta_d) & (bank_mask - 1))
                             | (rd & bank_mask);
                        gen_mov_vreg_F0(dp, rd);
                    }
                    break;
                }
                /* Setup the next operands.  */
                veclen--;
                rd = ((rd + delta_d) & (bank_mask - 1))
                     | (rd & bank_mask);

                if (op == 15) {
                    /* One source operand.  */
                    rm = ((rm + delta_m) & (bank_mask - 1))
                         | (rm & bank_mask);
                    gen_mov_F0_vreg(dp, rm);
                } else {
                    /* Two source operands.  */
                    rn = ((rn + delta_d) & (bank_mask - 1))
                         | (rn & bank_mask);
                    gen_mov_F0_vreg(dp, rn);
                    if (delta_m) {
                        rm = ((rm + delta_m) & (bank_mask - 1))
                             | (rm & bank_mask);
                        gen_mov_F1_vreg(dp, rm);
                    }
                }
            }
        }
        break;
    case 0xc:
    case 0xd:
        if (dp && (insn & (1 << 22))) {
            /* two-register transfer */
            rn = (insn >> 16) & 0xf;
            rd = (insn >> 12) & 0xf;
            if (dp) {
                if (insn & (1 << 5))
                    return 1;
                rm = insn & 0xf;
            } else
                rm = ((insn << 1) & 0x1e) | ((insn >> 5) & 1);

            if (insn & ARM_CP_RW_BIT) {
                /* vfp->arm */
                if (dp) {
                    gen_mov_F0_vreg(1, rm);
                    gen_op_vfp_mrrd();
                    gen_movl_reg_T0(s, rd);
                    gen_movl_reg_T1(s, rn);
                } else {
                    gen_mov_F0_vreg(0, rm);
                    gen_op_vfp_mrs();
                    gen_movl_reg_T0(s, rn);
                    gen_mov_F0_vreg(0, rm + 1);
                    gen_op_vfp_mrs();
                    gen_movl_reg_T0(s, rd);
                }
            } else {
                /* arm->vfp */
                if (dp) {
                    gen_movl_T0_reg(s, rd);
                    gen_movl_T1_reg(s, rn);
                    gen_op_vfp_mdrr();
                    gen_mov_vreg_F0(1, rm);
                } else {
                    gen_movl_T0_reg(s, rn);
                    gen_op_vfp_msr();
                    gen_mov_vreg_F0(0, rm);
                    gen_movl_T0_reg(s, rd);
                    gen_op_vfp_msr();
                    gen_mov_vreg_F0(0, rm + 1);
                }
            }
        } else {
            /* Load/store */
            rn = (insn >> 16) & 0xf;
            if (dp)
                rd = (insn >> 12) & 0xf;
            else
                rd = ((insn >> 11) & 0x1e) | ((insn >> 22) & 1);
            gen_movl_T1_reg(s, rn);
            if ((insn & 0x01200000) == 0x01000000) {
                /* Single load/store */
                offset = (insn & 0xff) << 2;
                if ((insn & (1 << 23)) == 0)
                    offset = -offset;
                gen_op_addl_T1_im(offset);
                if (insn & (1 << 20)) {
                    gen_vfp_ld(s, dp);
                    gen_mov_vreg_F0(dp, rd);
                } else {
                    gen_mov_F0_vreg(dp, rd);
                    gen_vfp_st(s, dp);
                }
            } else {
                /* load/store multiple */
                if (dp)
                    n = (insn >> 1) & 0x7f;
                else
                    n = insn & 0xff;

                if (insn & (1 << 24)) /* pre-decrement */
                    gen_op_addl_T1_im(-((insn & 0xff) << 2));

                if (dp)
                    offset = 8;
                else
                    offset = 4;
                for (i = 0; i < n; i++) {
                    if (insn & ARM_CP_RW_BIT) {
                        /* load */
                        gen_vfp_ld(s, dp);
                        gen_mov_vreg_F0(dp, rd + i);
                    } else {
                        /* store */
                        gen_mov_F0_vreg(dp, rd + i);
                        gen_vfp_st(s, dp);
                    }
                    gen_op_addl_T1_im(offset);
                }
                if (insn & (1 << 21)) {
                    /* writeback */
                    if (insn & (1 << 24))
                        offset = -offset * n;
                    else if (dp && (insn & 1))
                        offset = 4;
                    else
                        offset = 0;

                    if (offset != 0)
                        gen_op_addl_T1_im(offset);
                    gen_movl_reg_T1(s, rn);
                }
            }
        }
        break;
    default:
        /* Should never happen.  */
        return 1;
    }
    return 0;
}

static inline void gen_goto_tb(DisasContext *s, int n, uint32_t dest)
{
    TranslationBlock *tb;

    tb = s->tb;
    if ((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK)) {
        if (n == 0)
            gen_op_goto_tb0(TBPARAM(tb));
        else
            gen_op_goto_tb1(TBPARAM(tb));
        gen_op_movl_T0_im(dest);
        gen_op_movl_r15_T0();
        gen_op_movl_T0_im((long)tb + n);
        gen_op_exit_tb();
    } else {
        gen_op_movl_T0_im(dest);
        gen_op_movl_r15_T0();
        gen_op_movl_T0_0();
        gen_op_exit_tb();
    }
}

static inline void gen_jmp (DisasContext *s, uint32_t dest)
{
    if (__builtin_expect(s->singlestep_enabled, 0)) {
        /* An indirect jump so that we still trigger the debug exception.  */
        if (s->thumb)
          dest |= 1;
        gen_op_movl_T0_im(dest);
        gen_bx(s);
    } else {
        gen_goto_tb(s, 0, dest);
        s->is_jmp = DISAS_TB_JUMP;
    }
}

static inline void gen_mulxy(int x, int y)
{
    if (x)
        gen_op_sarl_T0_im(16);
    else
        gen_op_sxth_T0();
    if (y)
        gen_op_sarl_T1_im(16);
    else
        gen_op_sxth_T1();
    gen_op_mul_T0_T1();
}

/* Return the mask of PSR bits set by a MSR instruction.  */
static uint32_t msr_mask(DisasContext *s, int flags, int spsr) {
    uint32_t mask;

    mask = 0;
    if (flags & (1 << 0))
        mask |= 0xff;
    if (flags & (1 << 1))
        mask |= 0xff00;
    if (flags & (1 << 2))
        mask |= 0xff0000;
    if (flags & (1 << 3))
        mask |= 0xff000000;
    /* Mask out undefined bits.  */
    mask &= 0xf90f03ff;
    /* Mask out state bits.  */
    if (!spsr)
        mask &= ~0x01000020;
    /* Mask out privileged bits.  */
    if (IS_USER(s))
        mask &= 0xf80f0200;
    return mask;
}

/* Returns nonzero if access to the PSR is not permitted.  */
static int gen_set_psr_T0(DisasContext *s, uint32_t mask, int spsr)
{
    if (spsr) {
        /* ??? This is also undefined in system mode.  */
        if (IS_USER(s))
            return 1;
        gen_op_movl_spsr_T0(mask);
    } else {
        gen_op_movl_cpsr_T0(mask);
    }
    gen_lookup_tb(s);
    return 0;
}

static void gen_exception_return(DisasContext *s)
{
    gen_op_movl_reg_TN[0][15]();
    gen_op_movl_T0_spsr();
    gen_op_movl_cpsr_T0(0xffffffff);
    s->is_jmp = DISAS_UPDATE;
}

static void disas_arm_insn(CPUState * env, DisasContext *s)
{
    unsigned int cond, insn, val, op1, i, shift, rm, rs, rn, rd, sh;
    
    insn = ldl_code(s->pc);
    s->pc += 4;
    
    cond = insn >> 28;
    if (cond == 0xf){
        /* Unconditional instructions.  */
        if ((insn & 0x0d70f000) == 0x0550f000)
            return; /* PLD */
        else if ((insn & 0x0e000000) == 0x0a000000) {
            /* branch link and change to thumb (blx <offset>) */
            int32_t offset;

            val = (uint32_t)s->pc;
            gen_op_movl_T0_im(val);
            gen_movl_reg_T0(s, 14);
            /* Sign-extend the 24-bit offset */
            offset = (((int32_t)insn) << 8) >> 8;
            /* offset * 4 + bit24 * 2 + (thumb bit) */
            val += (offset << 2) | ((insn >> 23) & 2) | 1;
            /* pipeline offset */
            val += 4;
            gen_op_movl_T0_im(val);
            gen_bx(s);
            return;
        } else if ((insn & 0x0fe00000) == 0x0c400000) {
            /* Coprocessor double register transfer.  */
        } else if ((insn & 0x0f000010) == 0x0e000010) {
            /* Additional coprocessor register transfer.  */
        } else if ((insn & 0x0ff10010) == 0x01000000) {
            /* cps (privileged) */
        } else if ((insn & 0x0ffffdff) == 0x01010000) {
            /* setend */
            if (insn & (1 << 9)) {
                /* BE8 mode not implemented.  */
                goto illegal_op;
            }
            return;
        }
        goto illegal_op;
    }
    if (cond != 0xe) {
        /* if not always execute, we generate a conditional jump to
           next instruction */
        s->condlabel = gen_new_label();
        gen_test_cc[cond ^ 1](s->condlabel);
        s->condjmp = 1;
        //gen_test_cc[cond ^ 1]((long)s->tb, (long)s->pc);
        //s->is_jmp = DISAS_JUMP_NEXT;
    }
    if ((insn & 0x0f900000) == 0x03000000) {
        if ((insn & 0x0fb0f000) != 0x0320f000)
            goto illegal_op;
        /* CPSR = immediate */
        val = insn & 0xff;
        shift = ((insn >> 8) & 0xf) * 2;
        if (shift)
            val = (val >> shift) | (val << (32 - shift));
        gen_op_movl_T0_im(val);
        i = ((insn & (1 << 22)) != 0);
        if (gen_set_psr_T0(s, msr_mask(s, (insn >> 16) & 0xf, i), i))
            goto illegal_op;
    } else if ((insn & 0x0f900000) == 0x01000000
               && (insn & 0x00000090) != 0x00000090) {
        /* miscellaneous instructions */
        op1 = (insn >> 21) & 3;
        sh = (insn >> 4) & 0xf;
        rm = insn & 0xf;
        switch (sh) {
        case 0x0: /* move program status register */
            if (op1 & 1) {
                /* PSR = reg */
                gen_movl_T0_reg(s, rm);
                i = ((op1 & 2) != 0);
                if (gen_set_psr_T0(s, msr_mask(s, (insn >> 16) & 0xf, i), i))
                    goto illegal_op;
            } else {
                /* reg = PSR */
                rd = (insn >> 12) & 0xf;
                if (op1 & 2) {
                    if (IS_USER(s))
                        goto illegal_op;
                    gen_op_movl_T0_spsr();
                } else {
                    gen_op_movl_T0_cpsr();
                }
                gen_movl_reg_T0(s, rd);
            }
            break;
        case 0x1:
            if (op1 == 1) {
                /* branch/exchange thumb (bx).  */
                gen_movl_T0_reg(s, rm);
                gen_bx(s);
            } else if (op1 == 3) {
                /* clz */
                rd = (insn >> 12) & 0xf;
                gen_movl_T0_reg(s, rm);
                gen_op_clz_T0();
                gen_movl_reg_T0(s, rd);
            } else {
                goto illegal_op;
            }
            break;
        case 0x2:
            if (op1 == 1) {
                ARCH(5J); /* bxj */
                /* Trivial implementation equivalent to bx.  */
                gen_movl_T0_reg(s, rm);
                gen_bx(s);
            } else {
                goto illegal_op;
            }
            break;
        case 0x3:
            if (op1 != 1)
              goto illegal_op;

            /* branch link/exchange thumb (blx) */
            val = (uint32_t)s->pc;
            gen_op_movl_T0_im(val);
            gen_movl_reg_T0(s, 14);
            gen_movl_T0_reg(s, rm);
            gen_bx(s);
            break;
        case 0x5: /* saturating add/subtract */
            rd = (insn >> 12) & 0xf;
            rn = (insn >> 16) & 0xf;
            gen_movl_T0_reg(s, rm);
            gen_movl_T1_reg(s, rn);
            if (op1 & 2)
                gen_op_double_T1_saturate();
            if (op1 & 1)
                gen_op_subl_T0_T1_saturate();
            else
                gen_op_addl_T0_T1_saturate();
            gen_movl_reg_T0(s, rd);
            break;
        case 7: /* bkpt */
            gen_op_movl_T0_im((long)s->pc - 4);
            gen_op_movl_reg_TN[0][15]();
            gen_op_bkpt();
            s->is_jmp = DISAS_JUMP;
            break;
        case 0x8: /* signed multiply */
        case 0xa:
        case 0xc:
        case 0xe:
            rs = (insn >> 8) & 0xf;
            rn = (insn >> 12) & 0xf;
            rd = (insn >> 16) & 0xf;
            if (op1 == 1) {
                /* (32 * 16) >> 16 */
                gen_movl_T0_reg(s, rm);
                gen_movl_T1_reg(s, rs);
                if (sh & 4)
                    gen_op_sarl_T1_im(16);
                else
                    gen_op_sxth_T1();
                gen_op_imulw_T0_T1();
                if ((sh & 2) == 0) {
                    gen_movl_T1_reg(s, rn);
                    gen_op_addl_T0_T1_setq();
                }
                gen_movl_reg_T0(s, rd);
            } else {
                /* 16 * 16 */
                gen_movl_T0_reg(s, rm);
                gen_movl_T1_reg(s, rs);
                gen_mulxy(sh & 2, sh & 4);
                if (op1 == 2) {
                    gen_op_signbit_T1_T0();
                    gen_op_addq_T0_T1(rn, rd);
                    gen_movl_reg_T0(s, rn);
                    gen_movl_reg_T1(s, rd);
                } else {
                    if (op1 == 0) {
                        gen_movl_T1_reg(s, rn);
                        gen_op_addl_T0_T1_setq();
                    }
                    gen_movl_reg_T0(s, rd);
                }
            }
            break;
        default:
            goto illegal_op;
        }
    } else if (((insn & 0x0e000000) == 0 &&
                (insn & 0x00000090) != 0x90) ||
               ((insn & 0x0e000000) == (1 << 25))) {
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
            if (logic_cc && shift)
                gen_op_mov_CF_T1();
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
                } else if (shiftop != 0) {
                    if (logic_cc) {
                        gen_shift_T1_0_cc[shiftop]();
                    } else {
                        gen_shift_T1_0[shiftop]();
                    }
                }
            } else {
                rs = (insn >> 8) & 0xf;
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
            if (logic_cc)
                gen_op_logic_T0_cc();
            break;
        case 0x01:
            gen_op_xorl_T0_T1();
            gen_movl_reg_T0(s, rd);
            if (logic_cc)
                gen_op_logic_T0_cc();
            break;
        case 0x02:
            if (set_cc && rd == 15) {
                /* SUBS r15, ... is used for exception return.  */
                if (IS_USER(s))
                    goto illegal_op;
                gen_op_subl_T0_T1_cc();
                gen_exception_return(s);
            } else {
                if (set_cc)
                    gen_op_subl_T0_T1_cc();
                else
                    gen_op_subl_T0_T1();
                gen_movl_reg_T0(s, rd);
            }
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
                gen_op_logic_T0_cc();
            }
            break;
        case 0x09:
            if (set_cc) {
                gen_op_xorl_T0_T1();
                gen_op_logic_T0_cc();
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
            if (logic_cc)
                gen_op_logic_T0_cc();
            break;
        case 0x0d:
            if (logic_cc && rd == 15) {
                /* MOVS r15, ... is used for exception return.  */
                if (IS_USER(s))
                    goto illegal_op;
                gen_op_movl_T0_T1();
                gen_exception_return(s);
            } else {
                gen_movl_reg_T1(s, rd);
                if (logic_cc)
                    gen_op_logic_T1_cc();
            }
            break;
        case 0x0e:
            gen_op_bicl_T0_T1();
            gen_movl_reg_T0(s, rd);
            if (logic_cc)
                gen_op_logic_T0_cc();
            break;
        default:
        case 0x0f:
            gen_op_notl_T1();
            gen_movl_reg_T1(s, rd);
            if (logic_cc)
                gen_op_logic_T1_cc();
            break;
        }
    } else {
        /* other instructions */
        op1 = (insn >> 24) & 0xf;
        switch(op1) {
        case 0x0:
        case 0x1:
            /* multiplies, extra load/stores */
            sh = (insn >> 5) & 3;
            if (sh == 0) {
                if (op1 == 0x0) {
                    rd = (insn >> 16) & 0xf;
                    rn = (insn >> 12) & 0xf;
                    rs = (insn >> 8) & 0xf;
                    rm = (insn) & 0xf;
                    if (((insn >> 22) & 3) == 0) {
                        /* 32 bit mul */
                        gen_movl_T0_reg(s, rs);
                        gen_movl_T1_reg(s, rm);
                        gen_op_mul_T0_T1();
                        if (insn & (1 << 21)) {
                            gen_movl_T1_reg(s, rn);
                            gen_op_addl_T0_T1();
                        }
                        if (insn & (1 << 20)) 
                            gen_op_logic_T0_cc();
                        gen_movl_reg_T0(s, rd);
                    } else {
                        /* 64 bit mul */
                        gen_movl_T0_reg(s, rs);
                        gen_movl_T1_reg(s, rm);
                        if (insn & (1 << 22)) 
                            gen_op_imull_T0_T1();
                        else
                            gen_op_mull_T0_T1();
                        if (insn & (1 << 21)) /* mult accumulate */
                            gen_op_addq_T0_T1(rn, rd);
                        if (!(insn & (1 << 23))) { /* double accumulate */
                            ARCH(6);
                            gen_op_addq_lo_T0_T1(rn);
                            gen_op_addq_lo_T0_T1(rd);
                        }
                        if (insn & (1 << 20)) 
                            gen_op_logicq_cc();
                        gen_movl_reg_T0(s, rn);
                        gen_movl_reg_T1(s, rd);
                    }
                } else {
                    rn = (insn >> 16) & 0xf;
                    rd = (insn >> 12) & 0xf;
                    if (insn & (1 << 23)) {
                        /* load/store exclusive */
                        goto illegal_op;
                    } else {
                        /* SWP instruction */
                        rm = (insn) & 0xf;
                        
                        gen_movl_T0_reg(s, rm);
                        gen_movl_T1_reg(s, rn);
                        if (insn & (1 << 22)) {
                            gen_ldst(swpb, s);
                        } else {
                            gen_ldst(swpl, s);
                        }
                        gen_movl_reg_T0(s, rd);
                    }
                }
            } else {
                int address_offset;
                int load;
                /* Misc load/store */
                rn = (insn >> 16) & 0xf;
                rd = (insn >> 12) & 0xf;
                gen_movl_T1_reg(s, rn);
                if (insn & (1 << 24))
                    gen_add_datah_offset(s, insn, 0);
                address_offset = 0;
                if (insn & (1 << 20)) {
                    /* load */
                    switch(sh) {
                    case 1:
                        gen_ldst(lduw, s);
                        break;
                    case 2:
                        gen_ldst(ldsb, s);
                        break;
                    default:
                    case 3:
                        gen_ldst(ldsw, s);
                        break;
                    }
                    load = 1;
                } else if (sh & 2) {
                    /* doubleword */
                    if (sh & 1) {
                        /* store */
                        gen_movl_T0_reg(s, rd);
                        gen_ldst(stl, s);
                        gen_op_addl_T1_im(4);
                        gen_movl_T0_reg(s, rd + 1);
                        gen_ldst(stl, s);
                        load = 0;
                    } else {
                        /* load */
                        gen_ldst(ldl, s);
                        gen_movl_reg_T0(s, rd);
                        gen_op_addl_T1_im(4);
                        gen_ldst(ldl, s);
                        rd++;
                        load = 1;
                    }
                    address_offset = -4;
                } else {
                    /* store */
                    gen_movl_T0_reg(s, rd);
                    gen_ldst(stw, s);
                    load = 0;
                }
                /* Perform base writeback before the loaded value to
                   ensure correct behavior with overlapping index registers.
                   ldrd with base writeback is is undefined if the
                   destination and index registers overlap.  */
                if (!(insn & (1 << 24))) {
                    gen_add_datah_offset(s, insn, address_offset);
                    gen_movl_reg_T1(s, rn);
                } else if (insn & (1 << 21)) {
                    if (address_offset)
                        gen_op_addl_T1_im(address_offset);
                    gen_movl_reg_T1(s, rn);
                }
                if (load) {
                    /* Complete the load.  */
                    gen_movl_reg_T0(s, rd);
                }
            }
            break;
        case 0x4:
        case 0x5:
        case 0x6:
        case 0x7:
            /* Check for undefined extension instructions
             * per the ARM Bible IE:
             * xxxx 0111 1111 xxxx  xxxx xxxx 1111 xxxx
             */
            sh = (0xf << 20) | (0xf << 4);
            if (op1 == 0x7 && ((insn & sh) == sh))
            {
                goto illegal_op;
            }
            /* load/store byte/word */
            rn = (insn >> 16) & 0xf;
            rd = (insn >> 12) & 0xf;
            gen_movl_T1_reg(s, rn);
            i = (IS_USER(s) || (insn & 0x01200000) == 0x00200000);
            if (insn & (1 << 24))
                gen_add_data_offset(s, insn);
            if (insn & (1 << 20)) {
                /* load */
                s->is_mem = 1;
#if defined(CONFIG_USER_ONLY)
                if (insn & (1 << 22))
                    gen_op_ldub_raw();
                else
                    gen_op_ldl_raw();
#else
                if (insn & (1 << 22)) {
                    if (i)
                        gen_op_ldub_user();
                    else
                        gen_op_ldub_kernel();
                } else {
                    if (i)
                        gen_op_ldl_user();
                    else
                        gen_op_ldl_kernel();
                }
#endif
            } else {
                /* store */
                gen_movl_T0_reg(s, rd);
#if defined(CONFIG_USER_ONLY)
                if (insn & (1 << 22))
                    gen_op_stb_raw();
                else
                    gen_op_stl_raw();
#else
                if (insn & (1 << 22)) {
                    if (i)
                        gen_op_stb_user();
                    else
                        gen_op_stb_kernel();
                } else {
                    if (i)
                        gen_op_stl_user();
                    else
                        gen_op_stl_kernel();
                }
#endif
            }
            if (!(insn & (1 << 24))) {
                gen_add_data_offset(s, insn);
                gen_movl_reg_T1(s, rn);
            } else if (insn & (1 << 21))
                gen_movl_reg_T1(s, rn); {
            }
            if (insn & (1 << 20)) {
                /* Complete the load.  */
                if (rd == 15)
                    gen_bx(s);
                else
                    gen_movl_reg_T0(s, rd);
            }
            break;
        case 0x08:
        case 0x09:
            {
                int j, n, user, loaded_base;
                /* load/store multiple words */
                /* XXX: store correct base if write back */
                user = 0;
                if (insn & (1 << 22)) {
                    if (IS_USER(s))
                        goto illegal_op; /* only usable in supervisor mode */

                    if ((insn & (1 << 15)) == 0)
                        user = 1;
                }
                rn = (insn >> 16) & 0xf;
                gen_movl_T1_reg(s, rn);
                
                /* compute total size */
                loaded_base = 0;
                n = 0;
                for(i=0;i<16;i++) {
                    if (insn & (1 << i))
                        n++;
                }
                /* XXX: test invalid n == 0 case ? */
                if (insn & (1 << 23)) {
                    if (insn & (1 << 24)) {
                        /* pre increment */
                        gen_op_addl_T1_im(4);
                    } else {
                        /* post increment */
                    }
                } else {
                    if (insn & (1 << 24)) {
                        /* pre decrement */
                        gen_op_addl_T1_im(-(n * 4));
                    } else {
                        /* post decrement */
                        if (n != 1)
                            gen_op_addl_T1_im(-((n - 1) * 4));
                    }
                }
                j = 0;
                for(i=0;i<16;i++) {
                    if (insn & (1 << i)) {
                        if (insn & (1 << 20)) {
                            /* load */
                            gen_ldst(ldl, s);
                            if (i == 15) {
                                gen_bx(s);
                            } else if (user) {
                                gen_op_movl_user_T0(i);
                            } else if (i == rn) {
                                gen_op_movl_T2_T0();
                                loaded_base = 1;
                            } else {
                                gen_movl_reg_T0(s, i);
                            }
                        } else {
                            /* store */
                            if (i == 15) {
                                /* special case: r15 = PC + 8 */
                                val = (long)s->pc + 4;
                                gen_op_movl_TN_im[0](val);
                            } else if (user) {
                                gen_op_movl_T0_user(i);
                            } else {
                                gen_movl_T0_reg(s, i);
                            }
                            gen_ldst(stl, s);
                        }
                        j++;
                        /* no need to add after the last transfer */
                        if (j != n)
                            gen_op_addl_T1_im(4);
                    }
                }
                if (insn & (1 << 21)) {
                    /* write back */
                    if (insn & (1 << 23)) {
                        if (insn & (1 << 24)) {
                            /* pre increment */
                        } else {
                            /* post increment */
                            gen_op_addl_T1_im(4);
                        }
                    } else {
                        if (insn & (1 << 24)) {
                            /* pre decrement */
                            if (n != 1)
                                gen_op_addl_T1_im(-((n - 1) * 4));
                        } else {
                            /* post decrement */
                            gen_op_addl_T1_im(-(n * 4));
                        }
                    }
                    gen_movl_reg_T1(s, rn);
                }
                if (loaded_base) {
                    gen_op_movl_T0_T2();
                    gen_movl_reg_T0(s, rn);
                }
                if ((insn & (1 << 22)) && !user) {
                    /* Restore CPSR from SPSR.  */
                    gen_op_movl_T0_spsr();
                    gen_op_movl_cpsr_T0(0xffffffff);
                    s->is_jmp = DISAS_UPDATE;
                }
            }
            break;
        case 0xa:
        case 0xb:
            {
                int32_t offset;
                
                /* branch (and link) */
                val = (int32_t)s->pc;
                if (insn & (1 << 24)) {
                    gen_op_movl_T0_im(val);
                    gen_op_movl_reg_TN[0][14]();
                }
                offset = (((int32_t)insn << 8) >> 8);
                val += (offset << 2) + 4;
                gen_jmp(s, val);
            }
            break;
        case 0xc:
        case 0xd:
        case 0xe:
            /* Coprocessor.  */
            op1 = (insn >> 8) & 0xf;
            if (arm_feature(env, ARM_FEATURE_XSCALE) &&
                    ((env->cp15.c15_cpar ^ 0x3fff) & (1 << op1)))
                goto illegal_op;
            switch (op1) {
            case 0 ... 1:
                if (arm_feature(env, ARM_FEATURE_IWMMXT)) {
                    if (disas_iwmmxt_insn(env, s, insn))
                        goto illegal_op;
                } else if (arm_feature(env, ARM_FEATURE_XSCALE)) {
                    if (disas_dsp_insn(env, s, insn))
                        goto illegal_op;
                } else
                    goto illegal_op;
                break;
            case 2 ... 9:
            case 12 ... 14:
                if (disas_cp_insn (env, s, insn))
                    goto illegal_op;
                break;
            case 10:
            case 11:
                if (disas_vfp_insn (env, s, insn))
                    goto illegal_op;
                break;
            case 15:
                if (disas_cp15_insn (env, s, insn))
                    goto illegal_op;
                break;
            default:
                /* unknown coprocessor.  */
                goto illegal_op;
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

static void disas_thumb_insn(DisasContext *s)
{
    uint32_t val, insn, op, rm, rn, rd, shift, cond;
    int32_t offset;
    int i;

    insn = lduw_code(s->pc);
    s->pc += 2;

    switch (insn >> 12) {
    case 0: case 1:
        rd = insn & 7;
        op = (insn >> 11) & 3;
        if (op == 3) {
            /* add/subtract */
            rn = (insn >> 3) & 7;
            gen_movl_T0_reg(s, rn);
            if (insn & (1 << 10)) {
                /* immediate */
                gen_op_movl_T1_im((insn >> 6) & 7);
            } else {
                /* reg */
                rm = (insn >> 6) & 7;
                gen_movl_T1_reg(s, rm);
            }
            if (insn & (1 << 9))
                gen_op_subl_T0_T1_cc();
            else
                gen_op_addl_T0_T1_cc();
            gen_movl_reg_T0(s, rd);
        } else {
            /* shift immediate */
            rm = (insn >> 3) & 7;
            shift = (insn >> 6) & 0x1f;
            gen_movl_T0_reg(s, rm);
            gen_shift_T0_im_thumb[op](shift);
            gen_movl_reg_T0(s, rd);
        }
        break;
    case 2: case 3:
        /* arithmetic large immediate */
        op = (insn >> 11) & 3;
        rd = (insn >> 8) & 0x7;
        if (op == 0) {
            gen_op_movl_T0_im(insn & 0xff);
        } else {
            gen_movl_T0_reg(s, rd);
            gen_op_movl_T1_im(insn & 0xff);
        }
        switch (op) {
        case 0: /* mov */
            gen_op_logic_T0_cc();
            break;
        case 1: /* cmp */
            gen_op_subl_T0_T1_cc();
            break;
        case 2: /* add */
            gen_op_addl_T0_T1_cc();
            break;
        case 3: /* sub */
            gen_op_subl_T0_T1_cc();
            break;
        }
        if (op != 1)
            gen_movl_reg_T0(s, rd);
        break;
    case 4:
        if (insn & (1 << 11)) {
            rd = (insn >> 8) & 7;
            /* load pc-relative.  Bit 1 of PC is ignored.  */
            val = s->pc + 2 + ((insn & 0xff) * 4);
            val &= ~(uint32_t)2;
            gen_op_movl_T1_im(val);
            gen_ldst(ldl, s);
            gen_movl_reg_T0(s, rd);
            break;
        }
        if (insn & (1 << 10)) {
            /* data processing extended or blx */
            rd = (insn & 7) | ((insn >> 4) & 8);
            rm = (insn >> 3) & 0xf;
            op = (insn >> 8) & 3;
            switch (op) {
            case 0: /* add */
                gen_movl_T0_reg(s, rd);
                gen_movl_T1_reg(s, rm);
                gen_op_addl_T0_T1();
                gen_movl_reg_T0(s, rd);
                break;
            case 1: /* cmp */
                gen_movl_T0_reg(s, rd);
                gen_movl_T1_reg(s, rm);
                gen_op_subl_T0_T1_cc();
                break;
            case 2: /* mov/cpy */
                gen_movl_T0_reg(s, rm);
                gen_movl_reg_T0(s, rd);
                break;
            case 3:/* branch [and link] exchange thumb register */
                if (insn & (1 << 7)) {
                    val = (uint32_t)s->pc | 1;
                    gen_op_movl_T1_im(val);
                    gen_movl_reg_T1(s, 14);
                }
                gen_movl_T0_reg(s, rm);
                gen_bx(s);
                break;
            }
            break;
        }

        /* data processing register */
        rd = insn & 7;
        rm = (insn >> 3) & 7;
        op = (insn >> 6) & 0xf;
        if (op == 2 || op == 3 || op == 4 || op == 7) {
            /* the shift/rotate ops want the operands backwards */
            val = rm;
            rm = rd;
            rd = val;
            val = 1;
        } else {
            val = 0;
        }

        if (op == 9) /* neg */
            gen_op_movl_T0_im(0);
        else if (op != 0xf) /* mvn doesn't read its first operand */
            gen_movl_T0_reg(s, rd);

        gen_movl_T1_reg(s, rm);
        switch (op) {
        case 0x0: /* and */
            gen_op_andl_T0_T1();
            gen_op_logic_T0_cc();
            break;
        case 0x1: /* eor */
            gen_op_xorl_T0_T1();
            gen_op_logic_T0_cc();
            break;
        case 0x2: /* lsl */
            gen_op_shll_T1_T0_cc();
            gen_op_logic_T1_cc();
            break;
        case 0x3: /* lsr */
            gen_op_shrl_T1_T0_cc();
            gen_op_logic_T1_cc();
            break;
        case 0x4: /* asr */
            gen_op_sarl_T1_T0_cc();
            gen_op_logic_T1_cc();
            break;
        case 0x5: /* adc */
            gen_op_adcl_T0_T1_cc();
            break;
        case 0x6: /* sbc */
            gen_op_sbcl_T0_T1_cc();
            break;
        case 0x7: /* ror */
            gen_op_rorl_T1_T0_cc();
            gen_op_logic_T1_cc();
            break;
        case 0x8: /* tst */
            gen_op_andl_T0_T1();
            gen_op_logic_T0_cc();
            rd = 16;
            break;
        case 0x9: /* neg */
            gen_op_subl_T0_T1_cc();
            break;
        case 0xa: /* cmp */
            gen_op_subl_T0_T1_cc();
            rd = 16;
            break;
        case 0xb: /* cmn */
            gen_op_addl_T0_T1_cc();
            rd = 16;
            break;
        case 0xc: /* orr */
            gen_op_orl_T0_T1();
            gen_op_logic_T0_cc();
            break;
        case 0xd: /* mul */
            gen_op_mull_T0_T1();
            gen_op_logic_T0_cc();
            break;
        case 0xe: /* bic */
            gen_op_bicl_T0_T1();
            gen_op_logic_T0_cc();
            break;
        case 0xf: /* mvn */
            gen_op_notl_T1();
            gen_op_logic_T1_cc();
            val = 1;
            rm = rd;
            break;
        }
        if (rd != 16) {
            if (val)
                gen_movl_reg_T1(s, rm);
            else
                gen_movl_reg_T0(s, rd);
        }
        break;

    case 5:
        /* load/store register offset.  */
        rd = insn & 7;
        rn = (insn >> 3) & 7;
        rm = (insn >> 6) & 7;
        op = (insn >> 9) & 7;
        gen_movl_T1_reg(s, rn);
        gen_movl_T2_reg(s, rm);
        gen_op_addl_T1_T2();

        if (op < 3) /* store */
            gen_movl_T0_reg(s, rd);

        switch (op) {
        case 0: /* str */
            gen_ldst(stl, s);
            break;
        case 1: /* strh */
            gen_ldst(stw, s);
            break;
        case 2: /* strb */
            gen_ldst(stb, s);
            break;
        case 3: /* ldrsb */
            gen_ldst(ldsb, s);
            break;
        case 4: /* ldr */
            gen_ldst(ldl, s);
            break;
        case 5: /* ldrh */
            gen_ldst(lduw, s);
            break;
        case 6: /* ldrb */
            gen_ldst(ldub, s);
            break;
        case 7: /* ldrsh */
            gen_ldst(ldsw, s);
            break;
        }
        if (op >= 3) /* load */
            gen_movl_reg_T0(s, rd);
        break;

    case 6:
        /* load/store word immediate offset */
        rd = insn & 7;
        rn = (insn >> 3) & 7;
        gen_movl_T1_reg(s, rn);
        val = (insn >> 4) & 0x7c;
        gen_op_movl_T2_im(val);
        gen_op_addl_T1_T2();

        if (insn & (1 << 11)) {
            /* load */
            gen_ldst(ldl, s);
            gen_movl_reg_T0(s, rd);
        } else {
            /* store */
            gen_movl_T0_reg(s, rd);
            gen_ldst(stl, s);
        }
        break;

    case 7:
        /* load/store byte immediate offset */
        rd = insn & 7;
        rn = (insn >> 3) & 7;
        gen_movl_T1_reg(s, rn);
        val = (insn >> 6) & 0x1f;
        gen_op_movl_T2_im(val);
        gen_op_addl_T1_T2();

        if (insn & (1 << 11)) {
            /* load */
            gen_ldst(ldub, s);
            gen_movl_reg_T0(s, rd);
        } else {
            /* store */
            gen_movl_T0_reg(s, rd);
            gen_ldst(stb, s);
        }
        break;

    case 8:
        /* load/store halfword immediate offset */
        rd = insn & 7;
        rn = (insn >> 3) & 7;
        gen_movl_T1_reg(s, rn);
        val = (insn >> 5) & 0x3e;
        gen_op_movl_T2_im(val);
        gen_op_addl_T1_T2();

        if (insn & (1 << 11)) {
            /* load */
            gen_ldst(lduw, s);
            gen_movl_reg_T0(s, rd);
        } else {
            /* store */
            gen_movl_T0_reg(s, rd);
            gen_ldst(stw, s);
        }
        break;

    case 9:
        /* load/store from stack */
        rd = (insn >> 8) & 7;
        gen_movl_T1_reg(s, 13);
        val = (insn & 0xff) * 4;
        gen_op_movl_T2_im(val);
        gen_op_addl_T1_T2();

        if (insn & (1 << 11)) {
            /* load */
            gen_ldst(ldl, s);
            gen_movl_reg_T0(s, rd);
        } else {
            /* store */
            gen_movl_T0_reg(s, rd);
            gen_ldst(stl, s);
        }
        break;

    case 10:
        /* add to high reg */
        rd = (insn >> 8) & 7;
        if (insn & (1 << 11)) {
            /* SP */
            gen_movl_T0_reg(s, 13);
        } else {
            /* PC. bit 1 is ignored.  */
            gen_op_movl_T0_im((s->pc + 2) & ~(uint32_t)2);
        }
        val = (insn & 0xff) * 4;
        gen_op_movl_T1_im(val);
        gen_op_addl_T0_T1();
        gen_movl_reg_T0(s, rd);
        break;

    case 11:
        /* misc */
        op = (insn >> 8) & 0xf;
        switch (op) {
        case 0:
            /* adjust stack pointer */
            gen_movl_T1_reg(s, 13);
            val = (insn & 0x7f) * 4;
            if (insn & (1 << 7))
              val = -(int32_t)val;
            gen_op_movl_T2_im(val);
            gen_op_addl_T1_T2();
            gen_movl_reg_T1(s, 13);
            break;

        case 4: case 5: case 0xc: case 0xd:
            /* push/pop */
            gen_movl_T1_reg(s, 13);
            if (insn & (1 << 8))
                offset = 4;
            else
                offset = 0;
            for (i = 0; i < 8; i++) {
                if (insn & (1 << i))
                    offset += 4;
            }
            if ((insn & (1 << 11)) == 0) {
                gen_op_movl_T2_im(-offset);
                gen_op_addl_T1_T2();
            }
            gen_op_movl_T2_im(4);
            for (i = 0; i < 8; i++) {
                if (insn & (1 << i)) {
                    if (insn & (1 << 11)) {
                        /* pop */
                        gen_ldst(ldl, s);
                        gen_movl_reg_T0(s, i);
                    } else {
                        /* push */
                        gen_movl_T0_reg(s, i);
                        gen_ldst(stl, s);
                    }
                    /* advance to the next address.  */
                    gen_op_addl_T1_T2();
                }
            }
            if (insn & (1 << 8)) {
                if (insn & (1 << 11)) {
                    /* pop pc */
                    gen_ldst(ldl, s);
                    /* don't set the pc until the rest of the instruction
                       has completed */
                } else {
                    /* push lr */
                    gen_movl_T0_reg(s, 14);
                    gen_ldst(stl, s);
                }
                gen_op_addl_T1_T2();
            }
            if ((insn & (1 << 11)) == 0) {
                gen_op_movl_T2_im(-offset);
                gen_op_addl_T1_T2();
            }
            /* write back the new stack pointer */
            gen_movl_reg_T1(s, 13);
            /* set the new PC value */
            if ((insn & 0x0900) == 0x0900)
                gen_bx(s);
            break;

        case 0xe: /* bkpt */
            gen_op_movl_T0_im((long)s->pc - 2);
            gen_op_movl_reg_TN[0][15]();
            gen_op_bkpt();
            s->is_jmp = DISAS_JUMP;
            break;

        default:
            goto undef;
        }
        break;

    case 12:
        /* load/store multiple */
        rn = (insn >> 8) & 0x7;
        gen_movl_T1_reg(s, rn);
        gen_op_movl_T2_im(4);
        for (i = 0; i < 8; i++) {
            if (insn & (1 << i)) {
                if (insn & (1 << 11)) {
                    /* load */
                    gen_ldst(ldl, s);
                    gen_movl_reg_T0(s, i);
                } else {
                    /* store */
                    gen_movl_T0_reg(s, i);
                    gen_ldst(stl, s);
                }
                /* advance to the next address */
                gen_op_addl_T1_T2();
            }
        }
        /* Base register writeback.  */
        if ((insn & (1 << rn)) == 0)
            gen_movl_reg_T1(s, rn);
        break;

    case 13:
        /* conditional branch or swi */
        cond = (insn >> 8) & 0xf;
        if (cond == 0xe)
            goto undef;

        if (cond == 0xf) {
            /* swi */
            gen_op_movl_T0_im((long)s->pc | 1);
            /* Don't set r15.  */
            gen_op_movl_reg_TN[0][15]();
            gen_op_swi();
            s->is_jmp = DISAS_JUMP;
            break;
        }
        /* generate a conditional jump to next instruction */
        s->condlabel = gen_new_label();
        gen_test_cc[cond ^ 1](s->condlabel);
        s->condjmp = 1;
        //gen_test_cc[cond ^ 1]((long)s->tb, (long)s->pc);
        //s->is_jmp = DISAS_JUMP_NEXT;
        gen_movl_T1_reg(s, 15);

        /* jump to the offset */
        val = (uint32_t)s->pc + 2;
        offset = ((int32_t)insn << 24) >> 24;
        val += offset << 1;
        gen_jmp(s, val);
        break;

    case 14:
        /* unconditional branch */
        if (insn & (1 << 11)) {
            /* Second half of blx.  */
            offset = ((insn & 0x7ff) << 1);
            gen_movl_T0_reg(s, 14);
            gen_op_movl_T1_im(offset);
            gen_op_addl_T0_T1();
            gen_op_movl_T1_im(0xfffffffc);
            gen_op_andl_T0_T1();

            val = (uint32_t)s->pc;
            gen_op_movl_T1_im(val | 1);
            gen_movl_reg_T1(s, 14);
            gen_bx(s);
            break;
        }
        val = (uint32_t)s->pc;
        offset = ((int32_t)insn << 21) >> 21;
        val += (offset << 1) + 2;
        gen_jmp(s, val);
        break;

    case 15:
        /* branch and link [and switch to arm] */
        if ((s->pc & ~TARGET_PAGE_MASK) == 0) {
            /* Instruction spans a page boundary.  Implement it as two
               16-bit instructions in case the second half causes an
               prefetch abort.  */
            offset = ((int32_t)insn << 21) >> 9;
            val = s->pc + 2 + offset;
            gen_op_movl_T0_im(val);
            gen_movl_reg_T0(s, 14);
            break;
        }
        if (insn & (1 << 11)) {
            /* Second half of bl.  */
            offset = ((insn & 0x7ff) << 1) | 1;
            gen_movl_T0_reg(s, 14);
            gen_op_movl_T1_im(offset);
            gen_op_addl_T0_T1();

            val = (uint32_t)s->pc;
            gen_op_movl_T1_im(val | 1);
            gen_movl_reg_T1(s, 14);
            gen_bx(s);
            break;
        }
        offset = ((int32_t)insn << 21) >> 10;
        insn = lduw_code(s->pc);
        offset |= insn & 0x7ff;

        val = (uint32_t)s->pc + 2;
        gen_op_movl_T1_im(val | 1);
        gen_movl_reg_T1(s, 14);
        
        val += offset << 1;
        if (insn & (1 << 12)) {
            /* bl */
            gen_jmp(s, val);
        } else {
            /* blx */
            val &= ~(uint32_t)2;
            gen_op_movl_T0_im(val);
            gen_bx(s);
        }
    }
    return;
undef:
    gen_op_movl_T0_im((long)s->pc - 2);
    gen_op_movl_reg_TN[0][15]();
    gen_op_undef_insn();
    s->is_jmp = DISAS_JUMP;
}

/* generate intermediate code in gen_opc_buf and gen_opparam_buf for
   basic block 'tb'. If search_pc is TRUE, also generate PC
   information for each intermediate instruction. */
static inline int gen_intermediate_code_internal(CPUState *env, 
                                                 TranslationBlock *tb, 
                                                 int search_pc)
{
    DisasContext dc1, *dc = &dc1;
    uint16_t *gen_opc_end;
    int j, lj;
    target_ulong pc_start;
    uint32_t next_page_start;
    
    /* generate intermediate code */
    pc_start = tb->pc;
       
    dc->tb = tb;

    gen_opc_ptr = gen_opc_buf;
    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;
    gen_opparam_ptr = gen_opparam_buf;

    dc->is_jmp = DISAS_NEXT;
    dc->pc = pc_start;
    dc->singlestep_enabled = env->singlestep_enabled;
    dc->condjmp = 0;
    dc->thumb = env->thumb;
    dc->is_mem = 0;
#if !defined(CONFIG_USER_ONLY)
    dc->user = (env->uncached_cpsr & 0x1f) == ARM_CPU_MODE_USR;
#endif
    next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
    nb_gen_labels = 0;
    lj = -1;
    do {
        if (env->nb_breakpoints > 0) {
            for(j = 0; j < env->nb_breakpoints; j++) {
                if (env->breakpoints[j] == dc->pc) {
                    gen_op_movl_T0_im((long)dc->pc);
                    gen_op_movl_reg_TN[0][15]();
                    gen_op_debug();
                    dc->is_jmp = DISAS_JUMP;
                    break;
                }
            }
        }
        if (search_pc) {
            j = gen_opc_ptr - gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j)
                    gen_opc_instr_start[lj++] = 0;
            }
            gen_opc_pc[lj] = dc->pc;
            gen_opc_instr_start[lj] = 1;
        }

        if (env->thumb)
          disas_thumb_insn(dc);
        else
          disas_arm_insn(env, dc);

        if (dc->condjmp && !dc->is_jmp) {
            gen_set_label(dc->condlabel);
            dc->condjmp = 0;
        }
        /* Terminate the TB on memory ops if watchpoints are present.  */
        /* FIXME: This should be replacd by the deterministic execution
         * IRQ raising bits.  */
        if (dc->is_mem && env->nb_watchpoints)
            break;

        /* Translation stops when a conditional branch is enoutered.
         * Otherwise the subsequent code could get translated several times.
         * Also stop translation when a page boundary is reached.  This
         * ensures prefech aborts occur at the right place.  */
    } while (!dc->is_jmp && gen_opc_ptr < gen_opc_end &&
             !env->singlestep_enabled &&
             dc->pc < next_page_start);
    /* At this stage dc->condjmp will only be set when the skipped
     * instruction was a conditional branch, and the PC has already been
     * written.  */
    if (__builtin_expect(env->singlestep_enabled, 0)) {
        /* Make sure the pc is updated, and raise a debug exception.  */
        if (dc->condjmp) {
            gen_op_debug();
            gen_set_label(dc->condlabel);
        }
        if (dc->condjmp || !dc->is_jmp) {
            gen_op_movl_T0_im((long)dc->pc);
            gen_op_movl_reg_TN[0][15]();
            dc->condjmp = 0;
        }
        gen_op_debug();
    } else {
        switch(dc->is_jmp) {
        case DISAS_NEXT:
            gen_goto_tb(dc, 1, dc->pc);
            break;
        default:
        case DISAS_JUMP:
        case DISAS_UPDATE:
            /* indicate that the hash table must be used to find the next TB */
            gen_op_movl_T0_0();
            gen_op_exit_tb();
            break;
        case DISAS_TB_JUMP:
            /* nothing more to generate */
            break;
        }
        if (dc->condjmp) {
            gen_set_label(dc->condlabel);
            gen_goto_tb(dc, 1, dc->pc);
            dc->condjmp = 0;
        }
    }
    *gen_opc_ptr = INDEX_op_end;

#ifdef DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "----------------\n");
        fprintf(logfile, "IN: %s\n", lookup_symbol(pc_start));
        target_disas(logfile, pc_start, dc->pc - pc_start, env->thumb);
        fprintf(logfile, "\n");
        if (loglevel & (CPU_LOG_TB_OP)) {
            fprintf(logfile, "OP:\n");
            dump_ops(gen_opc_buf, gen_opparam_buf);
            fprintf(logfile, "\n");
        }
    }
#endif
    if (search_pc) {
        j = gen_opc_ptr - gen_opc_buf;
        lj++;
        while (lj <= j)
            gen_opc_instr_start[lj++] = 0;
        tb->size = 0;
    } else {
        tb->size = dc->pc - pc_start;
    }
    return 0;
}

int gen_intermediate_code(CPUState *env, TranslationBlock *tb)
{
    return gen_intermediate_code_internal(env, tb, 0);
}

int gen_intermediate_code_pc(CPUState *env, TranslationBlock *tb)
{
    return gen_intermediate_code_internal(env, tb, 1);
}

static const char *cpu_mode_names[16] = {
  "usr", "fiq", "irq", "svc", "???", "???", "???", "abt",
  "???", "???", "???", "und", "???", "???", "???", "sys"
};
void cpu_dump_state(CPUState *env, FILE *f, 
                    int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                    int flags)
{
    int i;
    union {
        uint32_t i;
        float s;
    } s0, s1;
    CPU_DoubleU d;
    /* ??? This assumes float64 and double have the same layout.
       Oh well, it's only debug dumps.  */
    union {
        float64 f64;
        double d;
    } d0;
    uint32_t psr;

    for(i=0;i<16;i++) {
        cpu_fprintf(f, "R%02d=%08x", i, env->regs[i]);
        if ((i % 4) == 3)
            cpu_fprintf(f, "\n");
        else
            cpu_fprintf(f, " ");
    }
    psr = cpsr_read(env);
    cpu_fprintf(f, "PSR=%08x %c%c%c%c %c %s%d\n",
                psr,
                psr & (1 << 31) ? 'N' : '-',
                psr & (1 << 30) ? 'Z' : '-',
                psr & (1 << 29) ? 'C' : '-',
                psr & (1 << 28) ? 'V' : '-',
                psr & CPSR_T ? 'T' : 'A', 
                cpu_mode_names[psr & 0xf], (psr & 0x10) ? 32 : 26);

    for (i = 0; i < 16; i++) {
        d.d = env->vfp.regs[i];
        s0.i = d.l.lower;
        s1.i = d.l.upper;
        d0.f64 = d.d;
        cpu_fprintf(f, "s%02d=%08x(%8g) s%02d=%08x(%8g) d%02d=%08x%08x(%8g)\n",
                    i * 2, (int)s0.i, s0.s,
                    i * 2 + 1, (int)s1.i, s1.s,
                    i, (int)(uint32_t)d.l.upper, (int)(uint32_t)d.l.lower,
                    d0.d);
    }
    cpu_fprintf(f, "FPSCR: %08x\n", (int)env->vfp.xregs[ARM_VFP_FPSCR]);
}

