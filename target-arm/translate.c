/*
 *  ARM translation
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *  Copyright (c) 2005-2007 CodeSourcery
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"

#define ENABLE_ARCH_5J    0
#define ENABLE_ARCH_6     arm_feature(env, ARM_FEATURE_V6)
#define ENABLE_ARCH_6K   arm_feature(env, ARM_FEATURE_V6K)
#define ENABLE_ARCH_6T2   arm_feature(env, ARM_FEATURE_THUMB2)
#define ENABLE_ARCH_7     arm_feature(env, ARM_FEATURE_V7)

#define ARCH(x) if (!ENABLE_ARCH_##x) goto illegal_op;

/* internal defines */
typedef struct DisasContext {
    target_ulong pc;
    int is_jmp;
    /* Nonzero if this instruction has been conditionally skipped.  */
    int condjmp;
    /* The label that will be jumped to when the instruction is skipped.  */
    int condlabel;
    /* Thumb-2 condtional execution bits.  */
    int condexec_mask;
    int condexec_cond;
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

/* These instructions trap after executing, so defer them until after the
   conditional executions state has been updated.  */
#define DISAS_WFI 4
#define DISAS_SWI 5

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

#define PAS_OP(pfx) {  \
    gen_op_ ## pfx ## add16_T0_T1, \
    gen_op_ ## pfx ## addsubx_T0_T1, \
    gen_op_ ## pfx ## subaddx_T0_T1, \
    gen_op_ ## pfx ## sub16_T0_T1, \
    gen_op_ ## pfx ## add8_T0_T1, \
    NULL, \
    NULL, \
    gen_op_ ## pfx ## sub8_T0_T1 }

static GenOpFunc *gen_arm_parallel_addsub[8][8] = {
    {},
    PAS_OP(s),
    PAS_OP(q),
    PAS_OP(sh),
    {},
    PAS_OP(u),
    PAS_OP(uq),
    PAS_OP(uh),
};
#undef PAS_OP

/* For unknown reasons Arm and Thumb-2 use arbitrarily diffenet encodings.  */
#define PAS_OP(pfx) {  \
    gen_op_ ## pfx ## add8_T0_T1, \
    gen_op_ ## pfx ## add16_T0_T1, \
    gen_op_ ## pfx ## addsubx_T0_T1, \
    NULL, \
    gen_op_ ## pfx ## sub8_T0_T1, \
    gen_op_ ## pfx ## sub16_T0_T1, \
    gen_op_ ## pfx ## subaddx_T0_T1, \
    NULL }

static GenOpFunc *gen_thumb2_parallel_addsub[8][8] = {
    PAS_OP(s),
    PAS_OP(q),
    PAS_OP(sh),
    {},
    PAS_OP(u),
    PAS_OP(uq),
    PAS_OP(uh),
    {}
};
#undef PAS_OP

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

static GenOpFunc1 *gen_shift_T0_im_thumb_cc[3] = {
    gen_op_shll_T0_im_thumb_cc,
    gen_op_shrl_T0_im_thumb_cc,
    gen_op_sarl_T0_im_thumb_cc,
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

#define VFP_OP1(name)                               \
static inline void gen_vfp_##name(int dp, int arg)  \
{                                                   \
    if (dp)                                         \
        gen_op_vfp_##name##d(arg);                  \
    else                                            \
        gen_op_vfp_##name##s(arg);                  \
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
VFP_OP1(tosh)
VFP_OP1(tosl)
VFP_OP1(touh)
VFP_OP1(toul)
VFP_OP1(shto)
VFP_OP1(slto)
VFP_OP1(uhto)
VFP_OP1(ulto)

#undef VFP_OP

static inline void gen_vfp_fconst(int dp, uint32_t val)
{
    if (dp)
        gen_op_vfp_fconstd(val);
    else
        gen_op_vfp_fconsts(val);
}

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

/* Return the offset of a 32-bit piece of a NEON register.
   zero is the least significant end of the register.  */
static inline long
neon_reg_offset (int reg, int n)
{
    int sreg;
    sreg = reg * 2 + n;
    return vfp_reg_offset(0, sreg);
}

#define NEON_GET_REG(T, reg, n) gen_op_neon_getreg_##T(neon_reg_offset(reg, n))
#define NEON_SET_REG(T, reg, n) gen_op_neon_setreg_##T(neon_reg_offset(reg, n))

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

static int cp15_user_ok(uint32_t insn)
{
    int cpn = (insn >> 16) & 0xf;
    int cpm = insn & 0xf;
    int op = ((insn >> 5) & 7) | ((insn >> 18) & 0x38);

    if (cpn == 13 && cpm == 0) {
        /* TLS register.  */
        if (op == 2 || (op == 3 && (insn & ARM_CP_RW_BIT)))
            return 1;
    }
    if (cpn == 7) {
        /* ISB, DSB, DMB.  */
        if ((cpm == 5 && op == 4)
                || (cpm == 10 && (op == 4 || op == 5)))
            return 1;
    }
    return 0;
}

/* Disassemble system coprocessor (cp15) instruction.  Return nonzero if
   instruction is not defined.  */
static int disas_cp15_insn(CPUState *env, DisasContext *s, uint32_t insn)
{
    uint32_t rd;

    /* M profile cores use memory mapped registers instead of cp15.  */
    if (arm_feature(env, ARM_FEATURE_M))
	return 1;

    if ((insn & (1 << 25)) == 0) {
        if (insn & (1 << 20)) {
            /* mrrc */
            return 1;
        }
        /* mcrr.  Used for block cache operations, so implement as no-op.  */
        return 0;
    }
    if ((insn & (1 << 4)) == 0) {
        /* cdp */
        return 1;
    }
    if (IS_USER(s) && !cp15_user_ok(insn)) {
        return 1;
    }
    if ((insn & 0x0fff0fff) == 0x0e070f90
        || (insn & 0x0fff0fff) == 0x0e070f58) {
        /* Wait for interrupt.  */
        gen_op_movl_T0_im((long)s->pc);
        gen_op_movl_reg_TN[0][15]();
        s->is_jmp = DISAS_WFI;
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

#define VFP_REG_SHR(x, n) (((n) > 0) ? (x) >> (n) : (x) << -(n))
#define VFP_SREG(insn, bigbit, smallbit) \
  ((VFP_REG_SHR(insn, bigbit - 1) & 0x1e) | (((insn) >> (smallbit)) & 1))
#define VFP_DREG(reg, insn, bigbit, smallbit) do { \
    if (arm_feature(env, ARM_FEATURE_VFP3)) { \
        reg = (((insn) >> (bigbit)) & 0x0f) \
              | (((insn) >> ((smallbit) - 4)) & 0x10); \
    } else { \
        if (insn & (1 << (smallbit))) \
            return 1; \
        reg = ((insn) >> (bigbit)) & 0x0f; \
    }} while (0)

#define VFP_SREG_D(insn) VFP_SREG(insn, 12, 22)
#define VFP_DREG_D(reg, insn) VFP_DREG(reg, insn, 12, 22)
#define VFP_SREG_N(insn) VFP_SREG(insn, 16,  7)
#define VFP_DREG_N(reg, insn) VFP_DREG(reg, insn, 16,  7)
#define VFP_SREG_M(insn) VFP_SREG(insn,  0,  5)
#define VFP_DREG_M(reg, insn) VFP_DREG(reg, insn,  0,  5)

static inline int
vfp_enabled(CPUState * env)
{
    return ((env->vfp.xregs[ARM_VFP_FPEXC] & (1 << 30)) != 0);
}

/* Disassemble a VFP instruction.  Returns nonzero if an error occured
   (ie. an undefined instruction).  */
static int disas_vfp_insn(CPUState * env, DisasContext *s, uint32_t insn)
{
    uint32_t rd, rn, rm, op, i, n, offset, delta_d, delta_m, bank_mask;
    int dp, veclen;

    if (!arm_feature(env, ARM_FEATURE_VFP))
        return 1;

    if (!vfp_enabled(env)) {
        /* VFP disabled.  Only allow fmxr/fmrx to/from some control regs.  */
        if ((insn & 0x0fe00fff) != 0x0ee00a10)
            return 1;
        rn = (insn >> 16) & 0xf;
        if (rn != ARM_VFP_FPSID && rn != ARM_VFP_FPEXC
            && rn != ARM_VFP_MVFR1 && rn != ARM_VFP_MVFR0)
            return 1;
    }
    dp = ((insn & 0xf00) == 0xb00);
    switch ((insn >> 24) & 0xf) {
    case 0xe:
        if (insn & (1 << 4)) {
            /* single register transfer */
            rd = (insn >> 12) & 0xf;
            if (dp) {
                int size;
                int pass;

                VFP_DREG_N(rn, insn);
                if (insn & 0xf)
                    return 1;
                if (insn & 0x00c00060
                    && !arm_feature(env, ARM_FEATURE_NEON))
                    return 1;

                pass = (insn >> 21) & 1;
                if (insn & (1 << 22)) {
                    size = 0;
                    offset = ((insn >> 5) & 3) * 8;
                } else if (insn & (1 << 5)) {
                    size = 1;
                    offset = (insn & (1 << 6)) ? 16 : 0;
                } else {
                    size = 2;
                    offset = 0;
                }
                if (insn & ARM_CP_RW_BIT) {
                    /* vfp->arm */
                    switch (size) {
                    case 0:
                        NEON_GET_REG(T1, rn, pass);
                        if (offset)
                            gen_op_shrl_T1_im(offset);
                        if (insn & (1 << 23))
                            gen_op_uxtb_T1();
                        else
                            gen_op_sxtb_T1();
                        break;
                    case 1:
                        NEON_GET_REG(T1, rn, pass);
                        if (insn & (1 << 23)) {
                            if (offset) {
                                gen_op_shrl_T1_im(16);
                            } else {
                                gen_op_uxth_T1();
                            }
                        } else {
                            if (offset) {
                                gen_op_sarl_T1_im(16);
                            } else {
                                gen_op_sxth_T1();
                            }
                        }
                        break;
                    case 2:
                        NEON_GET_REG(T1, rn, pass);
                        break;
                    }
                    gen_movl_reg_T1(s, rd);
                } else {
                    /* arm->vfp */
                    gen_movl_T0_reg(s, rd);
                    if (insn & (1 << 23)) {
                        /* VDUP */
                        if (size == 0) {
                            gen_op_neon_dup_u8(0);
                        } else if (size == 1) {
                            gen_op_neon_dup_low16();
                        }
                        NEON_SET_REG(T0, rn, 0);
                        NEON_SET_REG(T0, rn, 1);
                    } else {
                        /* VMOV */
                        switch (size) {
                        case 0:
                            NEON_GET_REG(T2, rn, pass);
                            gen_op_movl_T1_im(0xff);
                            gen_op_andl_T0_T1();
                            gen_op_neon_insert_elt(offset, ~(0xff << offset));
                            NEON_SET_REG(T2, rn, pass);
                            break;
                        case 1:
                            NEON_GET_REG(T2, rn, pass);
                            gen_op_movl_T1_im(0xffff);
                            gen_op_andl_T0_T1();
                            bank_mask = offset ? 0xffff : 0xffff0000;
                            gen_op_neon_insert_elt(offset, bank_mask);
                            NEON_SET_REG(T2, rn, pass);
                            break;
                        case 2:
                            NEON_SET_REG(T0, rn, pass);
                            break;
                        }
                    }
                }
            } else { /* !dp */
                if ((insn & 0x6f) != 0x00)
                    return 1;
                rn = VFP_SREG_N(insn);
                if (insn & ARM_CP_RW_BIT) {
                    /* vfp->arm */
                    if (insn & (1 << 21)) {
                        /* system register */
                        rn >>= 1;

                        switch (rn) {
                        case ARM_VFP_FPSID:
                            /* VFP2 allows access for FSID from userspace.
                               VFP3 restricts all id registers to privileged
                               accesses.  */
                            if (IS_USER(s)
                                && arm_feature(env, ARM_FEATURE_VFP3))
                                return 1;
                            gen_op_vfp_movl_T0_xreg(rn);
                            break;
                        case ARM_VFP_FPEXC:
                            if (IS_USER(s))
                                return 1;
                            gen_op_vfp_movl_T0_xreg(rn);
                            break;
                        case ARM_VFP_FPINST:
                        case ARM_VFP_FPINST2:
                            /* Not present in VFP3.  */
                            if (IS_USER(s)
                                || arm_feature(env, ARM_FEATURE_VFP3))
                                return 1;
                            gen_op_vfp_movl_T0_xreg(rn);
                            break;
                        case ARM_VFP_FPSCR:
			    if (rd == 15)
				gen_op_vfp_movl_T0_fpscr_flags();
			    else
				gen_op_vfp_movl_T0_fpscr();
                            break;
                        case ARM_VFP_MVFR0:
                        case ARM_VFP_MVFR1:
                            if (IS_USER(s)
                                || !arm_feature(env, ARM_FEATURE_VFP3))
                                return 1;
                            gen_op_vfp_movl_T0_xreg(rn);
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
                        case ARM_VFP_MVFR0:
                        case ARM_VFP_MVFR1:
                            /* Writes are ignored.  */
                            break;
                        case ARM_VFP_FPSCR:
                            gen_op_vfp_movl_fpscr_T0();
                            gen_lookup_tb(s);
                            break;
                        case ARM_VFP_FPEXC:
                            if (IS_USER(s))
                                return 1;
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
                    VFP_DREG_N(rn, insn);
                }

                if (op == 15 && (rn == 15 || rn > 17)) {
                    /* Integer or single precision destination.  */
                    rd = VFP_SREG_D(insn);
                } else {
                    VFP_DREG_D(rd, insn);
                }

                if (op == 15 && (rn == 16 || rn == 17)) {
                    /* Integer source.  */
                    rm = ((insn << 1) & 0x1e) | ((insn >> 5) & 1);
                } else {
                    VFP_DREG_M(rm, insn);
                }
            } else {
                rn = VFP_SREG_N(insn);
                if (op == 15 && rn == 15) {
                    /* Double precision destination.  */
                    VFP_DREG_D(rd, insn);
                } else {
                    rd = VFP_SREG_D(insn);
                }
                rm = VFP_SREG_M(insn);
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
                case 20:
                case 21:
                case 22:
                case 23:
                    /* Source and destination the same.  */
                    gen_mov_F0_vreg(dp, rd);
                    break;
                default:
                    /* One source operand.  */
                    gen_mov_F0_vreg(dp, rm);
                    break;
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
                case 14: /* fconst */
                    if (!arm_feature(env, ARM_FEATURE_VFP3))
                      return 1;

                    n = (insn << 12) & 0x80000000;
                    i = ((insn >> 12) & 0x70) | (insn & 0xf);
                    if (dp) {
                        if (i & 0x40)
                            i |= 0x3f80;
                        else
                            i |= 0x4000;
                        n |= i << 16;
                    } else {
                        if (i & 0x40)
                            i |= 0x780;
                        else
                            i |= 0x800;
                        n |= i << 19;
                    }
                    gen_vfp_fconst(dp, n);
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
                    case 20: /* fshto */
                        if (!arm_feature(env, ARM_FEATURE_VFP3))
                          return 1;
                        gen_vfp_shto(dp, rm);
                        break;
                    case 21: /* fslto */
                        if (!arm_feature(env, ARM_FEATURE_VFP3))
                          return 1;
                        gen_vfp_slto(dp, rm);
                        break;
                    case 22: /* fuhto */
                        if (!arm_feature(env, ARM_FEATURE_VFP3))
                          return 1;
                        gen_vfp_uhto(dp, rm);
                        break;
                    case 23: /* fulto */
                        if (!arm_feature(env, ARM_FEATURE_VFP3))
                          return 1;
                        gen_vfp_ulto(dp, rm);
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
                    case 28: /* ftosh */
                        if (!arm_feature(env, ARM_FEATURE_VFP3))
                          return 1;
                        gen_vfp_tosh(dp, rm);
                        break;
                    case 29: /* ftosl */
                        if (!arm_feature(env, ARM_FEATURE_VFP3))
                          return 1;
                        gen_vfp_tosl(dp, rm);
                        break;
                    case 30: /* ftouh */
                        if (!arm_feature(env, ARM_FEATURE_VFP3))
                          return 1;
                        gen_vfp_touh(dp, rm);
                        break;
                    case 31: /* ftoul */
                        if (!arm_feature(env, ARM_FEATURE_VFP3))
                          return 1;
                        gen_vfp_toul(dp, rm);
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
        if (dp && (insn & 0x03e00000) == 0x00400000) {
            /* two-register transfer */
            rn = (insn >> 16) & 0xf;
            rd = (insn >> 12) & 0xf;
            if (dp) {
                VFP_DREG_M(rm, insn);
            } else {
                rm = VFP_SREG_M(insn);
            }

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
                VFP_DREG_D(rd, insn);
            else
                rd = VFP_SREG_D(insn);
            if (s->thumb && rn == 15) {
                gen_op_movl_T1_im(s->pc & ~2);
            } else {
                gen_movl_T1_reg(s, rn);
            }
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
static uint32_t msr_mask(CPUState *env, DisasContext *s, int flags, int spsr) {
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
    mask &= ~CPSR_RESERVED;
    if (!arm_feature(env, ARM_FEATURE_V6))
        mask &= ~(CPSR_E | CPSR_GE);
    if (!arm_feature(env, ARM_FEATURE_THUMB2))
        mask &= ~CPSR_IT;
    /* Mask out execution state bits.  */
    if (!spsr)
        mask &= ~CPSR_EXEC;
    /* Mask out privileged bits.  */
    if (IS_USER(s))
        mask &= CPSR_USER;
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

/* Generate an old-style exception return.  */
static void gen_exception_return(DisasContext *s)
{
    gen_op_movl_reg_TN[0][15]();
    gen_op_movl_T0_spsr();
    gen_op_movl_cpsr_T0(0xffffffff);
    s->is_jmp = DISAS_UPDATE;
}

/* Generate a v6 exception return.  */
static void gen_rfe(DisasContext *s)
{
    gen_op_movl_cpsr_T0(0xffffffff);
    gen_op_movl_T0_T2();
    gen_op_movl_reg_TN[0][15]();
    s->is_jmp = DISAS_UPDATE;
}

static inline void
gen_set_condexec (DisasContext *s)
{
    if (s->condexec_mask) {
        gen_op_set_condexec((s->condexec_cond << 4) | (s->condexec_mask >> 1));
    }
}

static void gen_nop_hint(DisasContext *s, int val)
{
    switch (val) {
    case 3: /* wfi */
        gen_op_movl_T0_im((long)s->pc);
        gen_op_movl_reg_TN[0][15]();
        s->is_jmp = DISAS_WFI;
        break;
    case 2: /* wfe */
    case 4: /* sev */
        /* TODO: Implement SEV and WFE.  May help SMP performance.  */
    default: /* nop */
        break;
    }
}

/* Neon shift by constant.  The actual ops are the same as used for variable
   shifts.  [OP][U][SIZE]  */
static GenOpFunc *gen_neon_shift_im[8][2][4] = {
    { /* 0 */ /* VSHR */
      {
        gen_op_neon_shl_u8,
        gen_op_neon_shl_u16,
        gen_op_neon_shl_u32,
        gen_op_neon_shl_u64
      }, {
        gen_op_neon_shl_s8,
        gen_op_neon_shl_s16,
        gen_op_neon_shl_s32,
        gen_op_neon_shl_s64
      }
    }, { /* 1 */ /* VSRA */
      {
        gen_op_neon_shl_u8,
        gen_op_neon_shl_u16,
        gen_op_neon_shl_u32,
        gen_op_neon_shl_u64
      }, {
        gen_op_neon_shl_s8,
        gen_op_neon_shl_s16,
        gen_op_neon_shl_s32,
        gen_op_neon_shl_s64
      }
    }, { /* 2 */ /* VRSHR */
      {
        gen_op_neon_rshl_u8,
        gen_op_neon_rshl_u16,
        gen_op_neon_rshl_u32,
        gen_op_neon_rshl_u64
      }, {
        gen_op_neon_rshl_s8,
        gen_op_neon_rshl_s16,
        gen_op_neon_rshl_s32,
        gen_op_neon_rshl_s64
      }
    }, { /* 3 */ /* VRSRA */
      {
        gen_op_neon_rshl_u8,
        gen_op_neon_rshl_u16,
        gen_op_neon_rshl_u32,
        gen_op_neon_rshl_u64
      }, {
        gen_op_neon_rshl_s8,
        gen_op_neon_rshl_s16,
        gen_op_neon_rshl_s32,
        gen_op_neon_rshl_s64
      }
    }, { /* 4 */
      {
        NULL, NULL, NULL, NULL
      }, { /* VSRI */
        gen_op_neon_shl_u8,
        gen_op_neon_shl_u16,
        gen_op_neon_shl_u32,
        gen_op_neon_shl_u64,
      }
    }, { /* 5 */
      { /* VSHL */
        gen_op_neon_shl_u8,
        gen_op_neon_shl_u16,
        gen_op_neon_shl_u32,
        gen_op_neon_shl_u64,
      }, { /* VSLI */
        gen_op_neon_shl_u8,
        gen_op_neon_shl_u16,
        gen_op_neon_shl_u32,
        gen_op_neon_shl_u64,
      }
    }, { /* 6 */ /* VQSHL */
      {
        gen_op_neon_qshl_u8,
        gen_op_neon_qshl_u16,
        gen_op_neon_qshl_u32,
        gen_op_neon_qshl_u64
      }, {
        gen_op_neon_qshl_s8,
        gen_op_neon_qshl_s16,
        gen_op_neon_qshl_s32,
        gen_op_neon_qshl_s64
      }
    }, { /* 7 */ /* VQSHLU */
      {
        gen_op_neon_qshl_u8,
        gen_op_neon_qshl_u16,
        gen_op_neon_qshl_u32,
        gen_op_neon_qshl_u64
      }, {
        gen_op_neon_qshl_u8,
        gen_op_neon_qshl_u16,
        gen_op_neon_qshl_u32,
        gen_op_neon_qshl_u64
      }
    }
};

/* [R][U][size - 1] */
static GenOpFunc *gen_neon_shift_im_narrow[2][2][3] = {
    {
      {
        gen_op_neon_shl_u16,
        gen_op_neon_shl_u32,
        gen_op_neon_shl_u64
      }, {
        gen_op_neon_shl_s16,
        gen_op_neon_shl_s32,
        gen_op_neon_shl_s64
      }
    }, {
      {
        gen_op_neon_rshl_u16,
        gen_op_neon_rshl_u32,
        gen_op_neon_rshl_u64
      }, {
        gen_op_neon_rshl_s16,
        gen_op_neon_rshl_s32,
        gen_op_neon_rshl_s64
      }
    }
};

static inline void
gen_op_neon_narrow_u32 ()
{
    /* No-op.  */
}

static GenOpFunc *gen_neon_narrow[3] = {
    gen_op_neon_narrow_u8,
    gen_op_neon_narrow_u16,
    gen_op_neon_narrow_u32
};

static GenOpFunc *gen_neon_narrow_satu[3] = {
    gen_op_neon_narrow_sat_u8,
    gen_op_neon_narrow_sat_u16,
    gen_op_neon_narrow_sat_u32
};

static GenOpFunc *gen_neon_narrow_sats[3] = {
    gen_op_neon_narrow_sat_s8,
    gen_op_neon_narrow_sat_s16,
    gen_op_neon_narrow_sat_s32
};

static inline int gen_neon_add(int size)
{
    switch (size) {
    case 0: gen_op_neon_add_u8(); break;
    case 1: gen_op_neon_add_u16(); break;
    case 2: gen_op_addl_T0_T1(); break;
    default: return 1;
    }
    return 0;
}

/* 32-bit pairwise ops end up the same as the elementsise versions.  */
#define gen_op_neon_pmax_s32  gen_op_neon_max_s32
#define gen_op_neon_pmax_u32  gen_op_neon_max_u32
#define gen_op_neon_pmin_s32  gen_op_neon_min_s32
#define gen_op_neon_pmin_u32  gen_op_neon_min_u32

#define GEN_NEON_INTEGER_OP(name) do { \
    switch ((size << 1) | u) { \
    case 0: gen_op_neon_##name##_s8(); break; \
    case 1: gen_op_neon_##name##_u8(); break; \
    case 2: gen_op_neon_##name##_s16(); break; \
    case 3: gen_op_neon_##name##_u16(); break; \
    case 4: gen_op_neon_##name##_s32(); break; \
    case 5: gen_op_neon_##name##_u32(); break; \
    default: return 1; \
    }} while (0)

static inline void
gen_neon_movl_scratch_T0(int scratch)
{
  uint32_t offset;

  offset = offsetof(CPUARMState, vfp.scratch[scratch]);
  gen_op_neon_setreg_T0(offset);
}

static inline void
gen_neon_movl_scratch_T1(int scratch)
{
  uint32_t offset;

  offset = offsetof(CPUARMState, vfp.scratch[scratch]);
  gen_op_neon_setreg_T1(offset);
}

static inline void
gen_neon_movl_T0_scratch(int scratch)
{
  uint32_t offset;

  offset = offsetof(CPUARMState, vfp.scratch[scratch]);
  gen_op_neon_getreg_T0(offset);
}

static inline void
gen_neon_movl_T1_scratch(int scratch)
{
  uint32_t offset;

  offset = offsetof(CPUARMState, vfp.scratch[scratch]);
  gen_op_neon_getreg_T1(offset);
}

static inline void gen_op_neon_widen_u32(void)
{
    gen_op_movl_T1_im(0);
}

static inline void gen_neon_get_scalar(int size, int reg)
{
    if (size == 1) {
        NEON_GET_REG(T0, reg >> 1, reg & 1);
    } else {
        NEON_GET_REG(T0, reg >> 2, (reg >> 1) & 1);
        if (reg & 1)
            gen_op_neon_dup_low16();
        else
            gen_op_neon_dup_high16();
    }
}

static void gen_neon_unzip(int reg, int q, int tmp, int size)
{
    int n;

    for (n = 0; n < q + 1; n += 2) {
        NEON_GET_REG(T0, reg, n);
        NEON_GET_REG(T0, reg, n + n);
        switch (size) {
        case 0: gen_op_neon_unzip_u8(); break;
        case 1: gen_op_neon_zip_u16(); break; /* zip and unzip are the same.  */
        case 2: /* no-op */; break;
        default: abort();
        }
        gen_neon_movl_scratch_T0(tmp + n);
        gen_neon_movl_scratch_T1(tmp + n + 1);
    }
}

static struct {
    int nregs;
    int interleave;
    int spacing;
} neon_ls_element_type[11] = {
    {4, 4, 1},
    {4, 4, 2},
    {4, 1, 1},
    {4, 2, 1},
    {3, 3, 1},
    {3, 3, 2},
    {3, 1, 1},
    {1, 1, 1},
    {2, 2, 1},
    {2, 2, 2},
    {2, 1, 1}
};

/* Translate a NEON load/store element instruction.  Return nonzero if the
   instruction is invalid.  */
static int disas_neon_ls_insn(CPUState * env, DisasContext *s, uint32_t insn)
{
    int rd, rn, rm;
    int op;
    int nregs;
    int interleave;
    int stride;
    int size;
    int reg;
    int pass;
    int load;
    int shift;
    uint32_t mask;
    int n;

    if (!vfp_enabled(env))
      return 1;
    VFP_DREG_D(rd, insn);
    rn = (insn >> 16) & 0xf;
    rm = insn & 0xf;
    load = (insn & (1 << 21)) != 0;
    if ((insn & (1 << 23)) == 0) {
        /* Load store all elements.  */
        op = (insn >> 8) & 0xf;
        size = (insn >> 6) & 3;
        if (op > 10 || size == 3)
            return 1;
        nregs = neon_ls_element_type[op].nregs;
        interleave = neon_ls_element_type[op].interleave;
        gen_movl_T1_reg(s, rn);
        stride = (1 << size) * interleave;
        for (reg = 0; reg < nregs; reg++) {
            if (interleave > 2 || (interleave == 2 && nregs == 2)) {
                gen_movl_T1_reg(s, rn);
                gen_op_addl_T1_im((1 << size) * reg);
            } else if (interleave == 2 && nregs == 4 && reg == 2) {
                gen_movl_T1_reg(s, rn);
                gen_op_addl_T1_im(1 << size);
            }
            for (pass = 0; pass < 2; pass++) {
                if (size == 2) {
                    if (load) {
                        gen_ldst(ldl, s);
                        NEON_SET_REG(T0, rd, pass);
                    } else {
                        NEON_GET_REG(T0, rd, pass);
                        gen_ldst(stl, s);
                    }
                    gen_op_addl_T1_im(stride);
                } else if (size == 1) {
                    if (load) {
                        gen_ldst(lduw, s);
                        gen_op_addl_T1_im(stride);
                        gen_op_movl_T2_T0();
                        gen_ldst(lduw, s);
                        gen_op_addl_T1_im(stride);
                        gen_op_neon_insert_elt(16, 0xffff);
                        NEON_SET_REG(T2, rd, pass);
                    } else {
                        NEON_GET_REG(T2, rd, pass);
                        gen_op_movl_T0_T2();
                        gen_ldst(stw, s);
                        gen_op_addl_T1_im(stride);
                        gen_op_neon_extract_elt(16, 0xffff0000);
                        gen_ldst(stw, s);
                        gen_op_addl_T1_im(stride);
                    }
                } else /* size == 0 */ {
                    if (load) {
                        mask = 0xff;
                        for (n = 0; n < 4; n++) {
                            gen_ldst(ldub, s);
                            gen_op_addl_T1_im(stride);
                            if (n == 0) {
                                gen_op_movl_T2_T0();
                            } else {
                                gen_op_neon_insert_elt(n * 8, ~mask);
                            }
                            mask <<= 8;
                        }
                        NEON_SET_REG(T2, rd, pass);
                    } else {
                        NEON_GET_REG(T2, rd, pass);
                        mask = 0xff;
                        for (n = 0; n < 4; n++) {
                            if (n == 0) {
                                gen_op_movl_T0_T2();
                            } else {
                                gen_op_neon_extract_elt(n * 8, mask);
                            }
                            gen_ldst(stb, s);
                            gen_op_addl_T1_im(stride);
                            mask <<= 8;
                        }
                    }
                }
            }
            rd += neon_ls_element_type[op].spacing;
        }
        stride = nregs * 8;
    } else {
        size = (insn >> 10) & 3;
        if (size == 3) {
            /* Load single element to all lanes.  */
            if (!load)
                return 1;
            size = (insn >> 6) & 3;
            nregs = ((insn >> 8) & 3) + 1;
            stride = (insn & (1 << 5)) ? 2 : 1;
            gen_movl_T1_reg(s, rn);
            for (reg = 0; reg < nregs; reg++) {
                switch (size) {
                case 0:
                    gen_ldst(ldub, s);
                    gen_op_neon_dup_u8(0);
                    break;
                case 1:
                    gen_ldst(lduw, s);
                    gen_op_neon_dup_low16();
                    break;
                case 2:
                    gen_ldst(ldl, s);
                    break;
                case 3:
                    return 1;
                }
                gen_op_addl_T1_im(1 << size);
                NEON_SET_REG(T0, rd, 0);
                NEON_SET_REG(T0, rd, 1);
                rd += stride;
            }
            stride = (1 << size) * nregs;
        } else {
            /* Single element.  */
            pass = (insn >> 7) & 1;
            switch (size) {
            case 0:
                shift = ((insn >> 5) & 3) * 8;
                mask = 0xff << shift;
                stride = 1;
                break;
            case 1:
                shift = ((insn >> 6) & 1) * 16;
                mask = shift ? 0xffff0000 : 0xffff;
                stride = (insn & (1 << 5)) ? 2 : 1;
                break;
            case 2:
                shift = 0;
                mask = 0xffffffff;
                stride = (insn & (1 << 6)) ? 2 : 1;
                break;
            default:
                abort();
            }
            nregs = ((insn >> 8) & 3) + 1;
            gen_movl_T1_reg(s, rn);
            for (reg = 0; reg < nregs; reg++) {
                if (load) {
                    if (size != 2) {
                        NEON_GET_REG(T2, rd, pass);
                    }
                    switch (size) {
                    case 0:
                        gen_ldst(ldub, s);
                        break;
                    case 1:
                        gen_ldst(lduw, s);
                        break;
                    case 2:
                        gen_ldst(ldl, s);
                        NEON_SET_REG(T0, rd, pass);
                        break;
                    }
                    if (size != 2) {
                        gen_op_neon_insert_elt(shift, ~mask);
                        NEON_SET_REG(T0, rd, pass);
                    }
                } else { /* Store */
                    if (size == 2) {
                        NEON_GET_REG(T0, rd, pass);
                    } else {
                        NEON_GET_REG(T2, rd, pass);
                        gen_op_neon_extract_elt(shift, mask);
                    }
                    switch (size) {
                    case 0:
                        gen_ldst(stb, s);
                        break;
                    case 1:
                        gen_ldst(stw, s);
                        break;
                    case 2:
                        gen_ldst(stl, s);
                        break;
                    }
                }
                rd += stride;
                gen_op_addl_T1_im(1 << size);
            }
            stride = nregs * (1 << size);
        }
    }
    if (rm != 15) {
        gen_movl_T1_reg(s, rn);
        if (rm == 13) {
            gen_op_addl_T1_im(stride);
        } else {
            gen_movl_T2_reg(s, rm);
            gen_op_addl_T1_T2();
        }
        gen_movl_reg_T1(s, rn);
    }
    return 0;
}

/* Translate a NEON data processing instruction.  Return nonzero if the
   instruction is invalid.
   In general we process vectors in 32-bit chunks.  This means we can reuse
   some of the scalar ops, and hopefully the code generated for 32-bit
   hosts won't be too awful.  The downside is that the few 64-bit operations
   (mainly shifts) get complicated.  */

static int disas_neon_data_insn(CPUState * env, DisasContext *s, uint32_t insn)
{
    int op;
    int q;
    int rd, rn, rm;
    int size;
    int shift;
    int pass;
    int count;
    int pairwise;
    int u;
    int n;
    uint32_t imm;

    if (!vfp_enabled(env))
      return 1;
    q = (insn & (1 << 6)) != 0;
    u = (insn >> 24) & 1;
    VFP_DREG_D(rd, insn);
    VFP_DREG_N(rn, insn);
    VFP_DREG_M(rm, insn);
    size = (insn >> 20) & 3;
    if ((insn & (1 << 23)) == 0) {
        /* Three register same length.  */
        op = ((insn >> 7) & 0x1e) | ((insn >> 4) & 1);
        if (size == 3 && (op == 1 || op == 5 || op == 16)) {
            for (pass = 0; pass < (q ? 2 : 1); pass++) {
                NEON_GET_REG(T0, rm, pass * 2);
                NEON_GET_REG(T1, rm, pass * 2 + 1);
                gen_neon_movl_scratch_T0(0);
                gen_neon_movl_scratch_T1(1);
                NEON_GET_REG(T0, rn, pass * 2);
                NEON_GET_REG(T1, rn, pass * 2 + 1);
                switch (op) {
                case 1: /* VQADD */
                    if (u) {
                        gen_op_neon_addl_saturate_u64();
                    } else {
                        gen_op_neon_addl_saturate_s64();
                    }
                    break;
                case 5: /* VQSUB */
                    if (u) {
                        gen_op_neon_subl_saturate_u64();
                    } else {
                        gen_op_neon_subl_saturate_s64();
                    }
                    break;
                case 16:
                    if (u) {
                        gen_op_neon_subl_u64();
                    } else {
                        gen_op_neon_addl_u64();
                    }
                    break;
                default:
                    abort();
                }
                NEON_SET_REG(T0, rd, pass * 2);
                NEON_SET_REG(T1, rd, pass * 2 + 1);
            }
            return 0;
        }
        switch (op) {
        case 8: /* VSHL */
        case 9: /* VQSHL */
        case 10: /* VRSHL */
        case 11: /* VQSHL */
            /* Shift operations have Rn and Rm reversed.  */
            {
                int tmp;
                tmp = rn;
                rn = rm;
                rm = tmp;
                pairwise = 0;
            }
            break;
        case 20: /* VPMAX */
        case 21: /* VPMIN */
        case 23: /* VPADD */
            pairwise = 1;
            break;
        case 26: /* VPADD (float) */
            pairwise = (u && size < 2);
            break;
        case 30: /* VPMIN/VPMAX (float) */
            pairwise = u;
            break;
        default:
            pairwise = 0;
            break;
        }
        for (pass = 0; pass < (q ? 4 : 2); pass++) {

        if (pairwise) {
            /* Pairwise.  */
            if (q)
                n = (pass & 1) * 2;
            else
                n = 0;
            if (pass < q + 1) {
                NEON_GET_REG(T0, rn, n);
                NEON_GET_REG(T1, rn, n + 1);
            } else {
                NEON_GET_REG(T0, rm, n);
                NEON_GET_REG(T1, rm, n + 1);
            }
        } else {
            /* Elementwise.  */
            NEON_GET_REG(T0, rn, pass);
            NEON_GET_REG(T1, rm, pass);
        }
        switch (op) {
        case 0: /* VHADD */
            GEN_NEON_INTEGER_OP(hadd);
            break;
        case 1: /* VQADD */
            switch (size << 1| u) {
            case 0: gen_op_neon_qadd_s8(); break;
            case 1: gen_op_neon_qadd_u8(); break;
            case 2: gen_op_neon_qadd_s16(); break;
            case 3: gen_op_neon_qadd_u16(); break;
            case 4: gen_op_addl_T0_T1_saturate(); break;
            case 5: gen_op_addl_T0_T1_usaturate(); break;
            default: abort();
            }
            break;
        case 2: /* VRHADD */
            GEN_NEON_INTEGER_OP(rhadd);
            break;
        case 3: /* Logic ops.  */
            switch ((u << 2) | size) {
            case 0: /* VAND */
                gen_op_andl_T0_T1();
                break;
            case 1: /* BIC */
                gen_op_bicl_T0_T1();
                break;
            case 2: /* VORR */
                gen_op_orl_T0_T1();
                break;
            case 3: /* VORN */
                gen_op_notl_T1();
                gen_op_orl_T0_T1();
                break;
            case 4: /* VEOR */
                gen_op_xorl_T0_T1();
                break;
            case 5: /* VBSL */
                NEON_GET_REG(T2, rd, pass);
                gen_op_neon_bsl();
                break;
            case 6: /* VBIT */
                NEON_GET_REG(T2, rd, pass);
                gen_op_neon_bit();
                break;
            case 7: /* VBIF */
                NEON_GET_REG(T2, rd, pass);
                gen_op_neon_bif();
                break;
            }
            break;
        case 4: /* VHSUB */
            GEN_NEON_INTEGER_OP(hsub);
            break;
        case 5: /* VQSUB */
            switch ((size << 1) | u) {
            case 0: gen_op_neon_qsub_s8(); break;
            case 1: gen_op_neon_qsub_u8(); break;
            case 2: gen_op_neon_qsub_s16(); break;
            case 3: gen_op_neon_qsub_u16(); break;
            case 4: gen_op_subl_T0_T1_saturate(); break;
            case 5: gen_op_subl_T0_T1_usaturate(); break;
            default: abort();
            }
            break;
        case 6: /* VCGT */
            GEN_NEON_INTEGER_OP(cgt);
            break;
        case 7: /* VCGE */
            GEN_NEON_INTEGER_OP(cge);
            break;
        case 8: /* VSHL */
            switch ((size << 1) | u) {
            case 0: gen_op_neon_shl_s8(); break;
            case 1: gen_op_neon_shl_u8(); break;
            case 2: gen_op_neon_shl_s16(); break;
            case 3: gen_op_neon_shl_u16(); break;
            case 4: gen_op_neon_shl_s32(); break;
            case 5: gen_op_neon_shl_u32(); break;
#if 0
            /* ??? Implementing these is tricky because the vector ops work
               on 32-bit pieces.  */
            case 6: gen_op_neon_shl_s64(); break;
            case 7: gen_op_neon_shl_u64(); break;
#else
            case 6: case 7: cpu_abort(env, "VSHL.64 not implemented");
#endif
            }
            break;
        case 9: /* VQSHL */
            switch ((size << 1) | u) {
            case 0: gen_op_neon_qshl_s8(); break;
            case 1: gen_op_neon_qshl_u8(); break;
            case 2: gen_op_neon_qshl_s16(); break;
            case 3: gen_op_neon_qshl_u16(); break;
            case 4: gen_op_neon_qshl_s32(); break;
            case 5: gen_op_neon_qshl_u32(); break;
#if 0
            /* ??? Implementing these is tricky because the vector ops work
               on 32-bit pieces.  */
            case 6: gen_op_neon_qshl_s64(); break;
            case 7: gen_op_neon_qshl_u64(); break;
#else
            case 6: case 7: cpu_abort(env, "VQSHL.64 not implemented");
#endif
            }
            break;
        case 10: /* VRSHL */
            switch ((size << 1) | u) {
            case 0: gen_op_neon_rshl_s8(); break;
            case 1: gen_op_neon_rshl_u8(); break;
            case 2: gen_op_neon_rshl_s16(); break;
            case 3: gen_op_neon_rshl_u16(); break;
            case 4: gen_op_neon_rshl_s32(); break;
            case 5: gen_op_neon_rshl_u32(); break;
#if 0
            /* ??? Implementing these is tricky because the vector ops work
               on 32-bit pieces.  */
            case 6: gen_op_neon_rshl_s64(); break;
            case 7: gen_op_neon_rshl_u64(); break;
#else
            case 6: case 7: cpu_abort(env, "VRSHL.64 not implemented");
#endif
            }
            break;
        case 11: /* VQRSHL */
            switch ((size << 1) | u) {
            case 0: gen_op_neon_qrshl_s8(); break;
            case 1: gen_op_neon_qrshl_u8(); break;
            case 2: gen_op_neon_qrshl_s16(); break;
            case 3: gen_op_neon_qrshl_u16(); break;
            case 4: gen_op_neon_qrshl_s32(); break;
            case 5: gen_op_neon_qrshl_u32(); break;
#if 0
            /* ??? Implementing these is tricky because the vector ops work
               on 32-bit pieces.  */
            case 6: gen_op_neon_qrshl_s64(); break;
            case 7: gen_op_neon_qrshl_u64(); break;
#else
            case 6: case 7: cpu_abort(env, "VQRSHL.64 not implemented");
#endif
            }
            break;
        case 12: /* VMAX */
            GEN_NEON_INTEGER_OP(max);
            break;
        case 13: /* VMIN */
            GEN_NEON_INTEGER_OP(min);
            break;
        case 14: /* VABD */
            GEN_NEON_INTEGER_OP(abd);
            break;
        case 15: /* VABA */
            GEN_NEON_INTEGER_OP(abd);
            NEON_GET_REG(T1, rd, pass);
            gen_neon_add(size);
            break;
        case 16:
            if (!u) { /* VADD */
                if (gen_neon_add(size))
                    return 1;
            } else { /* VSUB */
                switch (size) {
                case 0: gen_op_neon_sub_u8(); break;
                case 1: gen_op_neon_sub_u16(); break;
                case 2: gen_op_subl_T0_T1(); break;
                default: return 1;
                }
            }
            break;
        case 17:
            if (!u) { /* VTST */
                switch (size) {
                case 0: gen_op_neon_tst_u8(); break;
                case 1: gen_op_neon_tst_u16(); break;
                case 2: gen_op_neon_tst_u32(); break;
                default: return 1;
                }
            } else { /* VCEQ */
                switch (size) {
                case 0: gen_op_neon_ceq_u8(); break;
                case 1: gen_op_neon_ceq_u16(); break;
                case 2: gen_op_neon_ceq_u32(); break;
                default: return 1;
                }
            }
            break;
        case 18: /* Multiply.  */
            switch (size) {
            case 0: gen_op_neon_mul_u8(); break;
            case 1: gen_op_neon_mul_u16(); break;
            case 2: gen_op_mul_T0_T1(); break;
            default: return 1;
            }
            NEON_GET_REG(T1, rd, pass);
            if (u) { /* VMLS */
                switch (size) {
                case 0: gen_op_neon_rsb_u8(); break;
                case 1: gen_op_neon_rsb_u16(); break;
                case 2: gen_op_rsbl_T0_T1(); break;
                default: return 1;
                }
            } else { /* VMLA */
                gen_neon_add(size);
            }
            break;
        case 19: /* VMUL */
            if (u) { /* polynomial */
                gen_op_neon_mul_p8();
            } else { /* Integer */
                switch (size) {
                case 0: gen_op_neon_mul_u8(); break;
                case 1: gen_op_neon_mul_u16(); break;
                case 2: gen_op_mul_T0_T1(); break;
                default: return 1;
                }
            }
            break;
        case 20: /* VPMAX */
            GEN_NEON_INTEGER_OP(pmax);
            break;
        case 21: /* VPMIN */
            GEN_NEON_INTEGER_OP(pmin);
            break;
        case 22: /* Hultiply high.  */
            if (!u) { /* VQDMULH */
                switch (size) {
                case 1: gen_op_neon_qdmulh_s16(); break;
                case 2: gen_op_neon_qdmulh_s32(); break;
                default: return 1;
                }
            } else { /* VQRDHMUL */
                switch (size) {
                case 1: gen_op_neon_qrdmulh_s16(); break;
                case 2: gen_op_neon_qrdmulh_s32(); break;
                default: return 1;
                }
            }
            break;
        case 23: /* VPADD */
            if (u)
                return 1;
            switch (size) {
            case 0: gen_op_neon_padd_u8(); break;
            case 1: gen_op_neon_padd_u16(); break;
            case 2: gen_op_addl_T0_T1(); break;
            default: return 1;
            }
            break;
        case 26: /* Floating point arithnetic.  */
            switch ((u << 2) | size) {
            case 0: /* VADD */
                gen_op_neon_add_f32();
                break;
            case 2: /* VSUB */
                gen_op_neon_sub_f32();
                break;
            case 4: /* VPADD */
                gen_op_neon_add_f32();
                break;
            case 6: /* VABD */
                gen_op_neon_abd_f32();
                break;
            default:
                return 1;
            }
            break;
        case 27: /* Float multiply.  */
            gen_op_neon_mul_f32();
            if (!u) {
                NEON_GET_REG(T1, rd, pass);
                if (size == 0) {
                    gen_op_neon_add_f32();
                } else {
                    gen_op_neon_rsb_f32();
                }
            }
            break;
        case 28: /* Float compare.  */
            if (!u) {
                gen_op_neon_ceq_f32();
            } else {
                if (size == 0)
                    gen_op_neon_cge_f32();
                else
                    gen_op_neon_cgt_f32();
            }
            break;
        case 29: /* Float compare absolute.  */
            if (!u)
                return 1;
            if (size == 0)
                gen_op_neon_acge_f32();
            else
                gen_op_neon_acgt_f32();
            break;
        case 30: /* Float min/max.  */
            if (size == 0)
                gen_op_neon_max_f32();
            else
                gen_op_neon_min_f32();
            break;
        case 31:
            if (size == 0)
                gen_op_neon_recps_f32();
            else
                gen_op_neon_rsqrts_f32();
            break;
        default:
            abort();
        }
        /* Save the result.  For elementwise operations we can put it
           straight into the destination register.  For pairwise operations
           we have to be careful to avoid clobbering the source operands.  */
        if (pairwise && rd == rm) {
            gen_neon_movl_scratch_T0(pass);
        } else {
            NEON_SET_REG(T0, rd, pass);
        }

        } /* for pass */
        if (pairwise && rd == rm) {
            for (pass = 0; pass < (q ? 4 : 2); pass++) {
                gen_neon_movl_T0_scratch(pass);
                NEON_SET_REG(T0, rd, pass);
            }
        }
    } else if (insn & (1 << 4)) {
        if ((insn & 0x00380080) != 0) {
            /* Two registers and shift.  */
            op = (insn >> 8) & 0xf;
            if (insn & (1 << 7)) {
                /* 64-bit shift.   */
                size = 3;
            } else {
                size = 2;
                while ((insn & (1 << (size + 19))) == 0)
                    size--;
            }
            shift = (insn >> 16) & ((1 << (3 + size)) - 1);
            /* To avoid excessive dumplication of ops we implement shift
               by immediate using the variable shift operations.  */
            if (op < 8) {
                /* Shift by immediate:
                   VSHR, VSRA, VRSHR, VRSRA, VSRI, VSHL, VQSHL, VQSHLU.  */
                /* Right shifts are encoded as N - shift, where N is the
                   element size in bits.  */
                if (op <= 4)
                    shift = shift - (1 << (size + 3));
                else
                    shift++;
                if (size == 3) {
                    count = q + 1;
                } else {
                    count = q ? 4: 2;
                }
                switch (size) {
                case 0:
                    imm = (uint8_t) shift;
                    imm |= imm << 8;
                    imm |= imm << 16;
                    break;
                case 1:
                    imm = (uint16_t) shift;
                    imm |= imm << 16;
                    break;
                case 2:
                case 3:
                    imm = shift;
                    break;
                default:
                    abort();
                }

                for (pass = 0; pass < count; pass++) {
                    if (size < 3) {
                        /* Operands in T0 and T1.  */
                        gen_op_movl_T1_im(imm);
                        NEON_GET_REG(T0, rm, pass);
                    } else {
                        /* Operands in {T0, T1} and env->vfp.scratch.  */
                        gen_op_movl_T0_im(imm);
                        gen_neon_movl_scratch_T0(0);
                        gen_op_movl_T0_im((int32_t)imm >> 31);
                        gen_neon_movl_scratch_T0(1);
                        NEON_GET_REG(T0, rm, pass * 2);
                        NEON_GET_REG(T1, rm, pass * 2 + 1);
                    }

                    if (gen_neon_shift_im[op][u][size] == NULL)
                        return 1;
                    gen_neon_shift_im[op][u][size]();

                    if (op == 1 || op == 3) {
                        /* Accumulate.  */
                        if (size == 3) {
                            gen_neon_movl_scratch_T0(0);
                            gen_neon_movl_scratch_T1(1);
                            NEON_GET_REG(T0, rd, pass * 2);
                            NEON_GET_REG(T1, rd, pass * 2 + 1);
                            gen_op_neon_addl_u64();
                        } else {
                            NEON_GET_REG(T1, rd, pass);
                            gen_neon_add(size);
                        }
                    } else if (op == 4 || (op == 5 && u)) {
                        /* Insert */
                        if (size == 3) {
                            cpu_abort(env, "VS[LR]I.64 not implemented");
                        }
                        switch (size) {
                        case 0:
                            if (op == 4)
                                imm = 0xff >> -shift;
                            else
                                imm = (uint8_t)(0xff << shift);
                            imm |= imm << 8;
                            imm |= imm << 16;
                            break;
                        case 1:
                            if (op == 4)
                                imm = 0xffff >> -shift;
                            else
                                imm = (uint16_t)(0xffff << shift);
                            imm |= imm << 16;
                            break;
                        case 2:
                            if (op == 4)
                                imm = 0xffffffffu >> -shift;
                            else
                                imm = 0xffffffffu << shift;
                            break;
                        default:
                            abort();
                        }
                        NEON_GET_REG(T1, rd, pass);
                        gen_op_movl_T2_im(imm);
                        gen_op_neon_bsl();
                    }
                    if (size == 3) {
                        NEON_SET_REG(T0, rd, pass * 2);
                        NEON_SET_REG(T1, rd, pass * 2 + 1);
                    } else {
                        NEON_SET_REG(T0, rd, pass);
                    }
                } /* for pass */
            } else if (op < 10) {
                /* Shift by immedaiate and narrow:
                   VSHRN, VRSHRN, VQSHRN, VQRSHRN.  */
                shift = shift - (1 << (size + 3));
                size++;
                if (size == 3) {
                    count = q + 1;
                } else {
                    count = q ? 4: 2;
                }
                switch (size) {
                case 1:
                    imm = (uint16_t) shift;
                    imm |= imm << 16;
                    break;
                case 2:
                case 3:
                    imm = shift;
                    break;
                default:
                    abort();
                }

                /* Processing MSB first means we need to do less shuffling at
                   the end.  */
                for (pass =  count - 1; pass >= 0; pass--) {
                    /* Avoid clobbering the second operand before it has been
                       written.  */
                    n = pass;
                    if (rd == rm)
                        n ^= (count - 1);
                    else
                        n = pass;

                    if (size < 3) {
                        /* Operands in T0 and T1.  */
                        gen_op_movl_T1_im(imm);
                        NEON_GET_REG(T0, rm, n);
                    } else {
                        /* Operands in {T0, T1} and env->vfp.scratch.  */
                        gen_op_movl_T0_im(imm);
                        gen_neon_movl_scratch_T0(0);
                        gen_op_movl_T0_im((int32_t)imm >> 31);
                        gen_neon_movl_scratch_T0(1);
                        NEON_GET_REG(T0, rm, n * 2);
                        NEON_GET_REG(T0, rm, n * 2 + 1);
                    }

                    gen_neon_shift_im_narrow[q][u][size - 1]();

                    if (size < 3 && (pass & 1) == 0) {
                        gen_neon_movl_scratch_T0(0);
                    } else {
                        uint32_t offset;

                        if (size < 3)
                            gen_neon_movl_T1_scratch(0);

                        if (op == 8 && !u) {
                            gen_neon_narrow[size - 1]();
                        } else {
                            if (op == 8)
                                gen_neon_narrow_sats[size - 2]();
                            else
                                gen_neon_narrow_satu[size - 1]();
                        }
                        if (size == 3)
                            offset = neon_reg_offset(rd, n);
                        else
                            offset = neon_reg_offset(rd, n >> 1);
                        gen_op_neon_setreg_T0(offset);
                    }
                } /* for pass */
            } else if (op == 10) {
                /* VSHLL */
                if (q)
                    return 1;
                for (pass = 0; pass < 2; pass++) {
                    /* Avoid clobbering the input operand.  */
                    if (rd == rm)
                        n = 1 - pass;
                    else
                        n = pass;

                    NEON_GET_REG(T0, rm, n);
                    GEN_NEON_INTEGER_OP(widen);
                    if (shift != 0) {
                        /* The shift is less than the width of the source
                           type, so in some cases we can just
                           shift the whole register.  */
                        if (size == 1 || (size == 0 && u)) {
                            gen_op_shll_T0_im(shift);
                            gen_op_shll_T1_im(shift);
                        } else {
                            switch (size) {
                            case 0: gen_op_neon_shll_u16(shift); break;
                            case 2: gen_op_neon_shll_u64(shift); break;
                            default: abort();
                            }
                        }
                    }
                    NEON_SET_REG(T0, rd, n * 2);
                    NEON_SET_REG(T1, rd, n * 2 + 1);
                }
            } else if (op == 15 || op == 16) {
                /* VCVT fixed-point.  */
                for (pass = 0; pass < (q ? 4 : 2); pass++) {
                    gen_op_vfp_getreg_F0s(neon_reg_offset(rm, pass));
                    if (op & 1) {
                        if (u)
                            gen_op_vfp_ultos(shift);
                        else
                            gen_op_vfp_sltos(shift);
                    } else {
                        if (u)
                            gen_op_vfp_touls(shift);
                        else
                            gen_op_vfp_tosls(shift);
                    }
                    gen_op_vfp_setreg_F0s(neon_reg_offset(rd, pass));
                }
            } else {
                return 1;
            }
        } else { /* (insn & 0x00380080) == 0 */
            int invert;

            op = (insn >> 8) & 0xf;
            /* One register and immediate.  */
            imm = (u << 7) | ((insn >> 12) & 0x70) | (insn & 0xf);
            invert = (insn & (1 << 5)) != 0;
            switch (op) {
            case 0: case 1:
                /* no-op */
                break;
            case 2: case 3:
                imm <<= 8;
                break;
            case 4: case 5:
                imm <<= 16;
                break;
            case 6: case 7:
                imm <<= 24;
                break;
            case 8: case 9:
                imm |= imm << 16;
                break;
            case 10: case 11:
                imm = (imm << 8) | (imm << 24);
                break;
            case 12:
                imm = (imm < 8) | 0xff;
                break;
            case 13:
                imm = (imm << 16) | 0xffff;
                break;
            case 14:
                imm |= (imm << 8) | (imm << 16) | (imm << 24);
                if (invert)
                    imm = ~imm;
                break;
            case 15:
                imm = ((imm & 0x80) << 24) | ((imm & 0x3f) << 19)
                      | ((imm & 0x40) ? (0x1f << 25) : (1 << 30));
                break;
            }
            if (invert)
                imm = ~imm;

            if (op != 14 || !invert)
                gen_op_movl_T1_im(imm);

            for (pass = 0; pass < (q ? 4 : 2); pass++) {
                if (op & 1 && op < 12) {
                    NEON_GET_REG(T0, rd, pass);
                    if (invert) {
                        /* The immediate value has already been inverted, so
                           BIC becomes AND.  */
                        gen_op_andl_T0_T1();
                    } else {
                        gen_op_orl_T0_T1();
                    }
                    NEON_SET_REG(T0, rd, pass);
                } else {
                    if (op == 14 && invert) {
                        uint32_t tmp;
                        tmp = 0;
                        for (n = 0; n < 4; n++) {
                            if (imm & (1 << (n + (pass & 1) * 4)))
                                tmp |= 0xff << (n * 8);
                        }
                        gen_op_movl_T1_im(tmp);
                    }
                    /* VMOV, VMVN.  */
                    NEON_SET_REG(T1, rd, pass);
                }
            }
        }
    } else { /* (insn & 0x00800010 == 0x00800010) */
        if (size != 3) {
            op = (insn >> 8) & 0xf;
            if ((insn & (1 << 6)) == 0) {
                /* Three registers of different lengths.  */
                int src1_wide;
                int src2_wide;
                int prewiden;
                /* prewiden, src1_wide, src2_wide */
                static const int neon_3reg_wide[16][3] = {
                    {1, 0, 0}, /* VADDL */
                    {1, 1, 0}, /* VADDW */
                    {1, 0, 0}, /* VSUBL */
                    {1, 1, 0}, /* VSUBW */
                    {0, 1, 1}, /* VADDHN */
                    {0, 0, 0}, /* VABAL */
                    {0, 1, 1}, /* VSUBHN */
                    {0, 0, 0}, /* VABDL */
                    {0, 0, 0}, /* VMLAL */
                    {0, 0, 0}, /* VQDMLAL */
                    {0, 0, 0}, /* VMLSL */
                    {0, 0, 0}, /* VQDMLSL */
                    {0, 0, 0}, /* Integer VMULL */
                    {0, 0, 0}, /* VQDMULL */
                    {0, 0, 0}  /* Polynomial VMULL */
                };

                prewiden = neon_3reg_wide[op][0];
                src1_wide = neon_3reg_wide[op][1];
                src2_wide = neon_3reg_wide[op][2];

                /* Avoid overlapping operands.  Wide source operands are
                   always aligned so will never overlap with wide
                   destinations in problematic ways.  */
                if (rd == rm) {
                    NEON_GET_REG(T2, rm, 1);
                } else if (rd == rn) {
                    NEON_GET_REG(T2, rn, 1);
                }
                for (pass = 0; pass < 2; pass++) {
                    /* Load the second operand into env->vfp.scratch.
                       Also widen narrow operands.  */
                    if (pass == 1 && rd == rm) {
                        if (prewiden) {
                            gen_op_movl_T0_T2();
                        } else {
                            gen_op_movl_T1_T2();
                        }
                    } else {
                        if (src2_wide) {
                            NEON_GET_REG(T0, rm, pass * 2);
                            NEON_GET_REG(T1, rm, pass * 2 + 1);
                        } else {
                            if (prewiden) {
                                NEON_GET_REG(T0, rm, pass);
                            } else {
                                NEON_GET_REG(T1, rm, pass);
                            }
                        }
                    }
                    if (prewiden && !src2_wide) {
                        GEN_NEON_INTEGER_OP(widen);
                    }
                    if (prewiden || src2_wide) {
                        gen_neon_movl_scratch_T0(0);
                        gen_neon_movl_scratch_T1(1);
                    }

                    /* Load the first operand.  */
                    if (pass == 1 && rd == rn) {
                        gen_op_movl_T0_T2();
                    } else {
                        if (src1_wide) {
                            NEON_GET_REG(T0, rn, pass * 2);
                            NEON_GET_REG(T1, rn, pass * 2 + 1);
                        } else {
                            NEON_GET_REG(T0, rn, pass);
                        }
                    }
                    if (prewiden && !src1_wide) {
                        GEN_NEON_INTEGER_OP(widen);
                    }
                    switch (op) {
                    case 0: case 1: case 4: /* VADDL, VADDW, VADDHN, VRADDHN */
                        switch (size) {
                        case 0: gen_op_neon_addl_u16(); break;
                        case 1: gen_op_neon_addl_u32(); break;
                        case 2: gen_op_neon_addl_u64(); break;
                        default: abort();
                        }
                        break;
                    case 2: case 3: case 6: /* VSUBL, VSUBW, VSUBHL, VRSUBHL */
                        switch (size) {
                        case 0: gen_op_neon_subl_u16(); break;
                        case 1: gen_op_neon_subl_u32(); break;
                        case 2: gen_op_neon_subl_u64(); break;
                        default: abort();
                        }
                        break;
                    case 5: case 7: /* VABAL, VABDL */
                        switch ((size << 1) | u) {
                        case 0: gen_op_neon_abdl_s16(); break;
                        case 1: gen_op_neon_abdl_u16(); break;
                        case 2: gen_op_neon_abdl_s32(); break;
                        case 3: gen_op_neon_abdl_u32(); break;
                        case 4: gen_op_neon_abdl_s64(); break;
                        case 5: gen_op_neon_abdl_u64(); break;
                        default: abort();
                        }
                        break;
                    case 8: case 9: case 10: case 11: case 12: case 13:
                        /* VMLAL, VQDMLAL, VMLSL, VQDMLSL, VMULL, VQDMULL */
                        switch ((size << 1) | u) {
                        case 0: gen_op_neon_mull_s8(); break;
                        case 1: gen_op_neon_mull_u8(); break;
                        case 2: gen_op_neon_mull_s16(); break;
                        case 3: gen_op_neon_mull_u16(); break;
                        case 4: gen_op_imull_T0_T1(); break;
                        case 5: gen_op_mull_T0_T1(); break;
                        default: abort();
                        }
                        break;
                    case 14: /* Polynomial VMULL */
                        cpu_abort(env, "Polynomial VMULL not implemented");

                    default: /* 15 is RESERVED.  */
                        return 1;
                    }
                    if (op == 5 || op == 13 || (op >= 8 && op <= 11)) {
                        /* Accumulate.  */
                        if (op == 10 || op == 11) {
                            switch (size) {
                            case 0: gen_op_neon_negl_u16(); break;
                            case 1: gen_op_neon_negl_u32(); break;
                            case 2: gen_op_neon_negl_u64(); break;
                            default: abort();
                            }
                        }

                        gen_neon_movl_scratch_T0(0);
                        gen_neon_movl_scratch_T1(1);

                        if (op != 13) {
                            NEON_GET_REG(T0, rd, pass * 2);
                            NEON_GET_REG(T1, rd, pass * 2 + 1);
                        }

                        switch (op) {
                        case 5: case 8: case 10: /* VABAL, VMLAL, VMLSL */
                            switch (size) {
                            case 0: gen_op_neon_addl_u16(); break;
                            case 1: gen_op_neon_addl_u32(); break;
                            case 2: gen_op_neon_addl_u64(); break;
                            default: abort();
                            }
                            break;
                        case 9: case 11: /* VQDMLAL, VQDMLSL */
                            switch (size) {
                            case 1: gen_op_neon_addl_saturate_s32(); break;
                            case 2: gen_op_neon_addl_saturate_s64(); break;
                            default: abort();
                            }
                            /* Fall through.  */
                        case 13: /* VQDMULL */
                            switch (size) {
                            case 1: gen_op_neon_addl_saturate_s32(); break;
                            case 2: gen_op_neon_addl_saturate_s64(); break;
                            default: abort();
                            }
                            break;
                        default:
                            abort();
                        }
                        NEON_SET_REG(T0, rd, pass * 2);
                        NEON_SET_REG(T1, rd, pass * 2 + 1);
                    } else if (op == 4 || op == 6) {
                        /* Narrowing operation.  */
                        if (u) {
                            switch (size) {
                            case 0: gen_op_neon_narrow_high_u8(); break;
                            case 1: gen_op_neon_narrow_high_u16(); break;
                            case 2: gen_op_movl_T0_T1(); break;
                            default: abort();
                            }
                        } else {
                            switch (size) {
                            case 0: gen_op_neon_narrow_high_round_u8(); break;
                            case 1: gen_op_neon_narrow_high_round_u16(); break;
                            case 2: gen_op_neon_narrow_high_round_u32(); break;
                            default: abort();
                            }
                        }
                        NEON_SET_REG(T0, rd, pass);
                    } else {
                        /* Write back the result.  */
                        NEON_SET_REG(T0, rd, pass * 2);
                        NEON_SET_REG(T1, rd, pass * 2 + 1);
                    }
                }
            } else {
                /* Two registers and a scalar.  */
                switch (op) {
                case 0: /* Integer VMLA scalar */
                case 1: /* Float VMLA scalar */
                case 4: /* Integer VMLS scalar */
                case 5: /* Floating point VMLS scalar */
                case 8: /* Integer VMUL scalar */
                case 9: /* Floating point VMUL scalar */
                case 12: /* VQDMULH scalar */
                case 13: /* VQRDMULH scalar */
                    gen_neon_get_scalar(size, rm);
                    gen_op_movl_T2_T0();
                    for (pass = 0; pass < (u ? 4 : 2); pass++) {
                        if (pass != 0)
                            gen_op_movl_T0_T2();
                        NEON_GET_REG(T1, rn, pass);
                        if (op == 12) {
                            if (size == 1) {
                                gen_op_neon_qdmulh_s16();
                            } else {
                                gen_op_neon_qdmulh_s32();
                            }
                        } else if (op == 13) {
                            if (size == 1) {
                                gen_op_neon_qrdmulh_s16();
                            } else {
                                gen_op_neon_qrdmulh_s32();
                            }
                        } else if (op & 1) {
                            gen_op_neon_mul_f32();
                        } else {
                            switch (size) {
                            case 0: gen_op_neon_mul_u8(); break;
                            case 1: gen_op_neon_mul_u16(); break;
                            case 2: gen_op_mul_T0_T1(); break;
                            default: return 1;
                            }
                        }
                        if (op < 8) {
                            /* Accumulate.  */
                            NEON_GET_REG(T1, rd, pass);
                            switch (op) {
                            case 0:
                                gen_neon_add(size);
                                break;
                            case 1:
                                gen_op_neon_add_f32();
                                break;
                            case 4:
                                switch (size) {
                                case 0: gen_op_neon_rsb_u8(); break;
                                case 1: gen_op_neon_rsb_u16(); break;
                                case 2: gen_op_rsbl_T0_T1(); break;
                                default: return 1;
                                }
                                break;
                            case 5:
                                gen_op_neon_rsb_f32();
                                break;
                            default:
                                abort();
                            }
                        }
                        NEON_SET_REG(T0, rd, pass);
                    }
                    break;
                case 2: /* VMLAL sclar */
                case 3: /* VQDMLAL scalar */
                case 6: /* VMLSL scalar */
                case 7: /* VQDMLSL scalar */
                case 10: /* VMULL scalar */
                case 11: /* VQDMULL scalar */
                    if (rd == rn) {
                        /* Save overlapping operands before they are
                           clobbered.  */
                        NEON_GET_REG(T0, rn, 1);
                        gen_neon_movl_scratch_T0(2);
                    }
                    gen_neon_get_scalar(size, rm);
                    gen_op_movl_T2_T0();
                    for (pass = 0; pass < 2; pass++) {
                        if (pass != 0) {
                            gen_op_movl_T0_T2();
                        }
                        if (pass != 0 && rd == rn) {
                            gen_neon_movl_T1_scratch(2);
                        } else {
                            NEON_GET_REG(T1, rn, pass);
                        }
                        switch ((size << 1) | u) {
                        case 0: gen_op_neon_mull_s8(); break;
                        case 1: gen_op_neon_mull_u8(); break;
                        case 2: gen_op_neon_mull_s16(); break;
                        case 3: gen_op_neon_mull_u16(); break;
                        case 4: gen_op_imull_T0_T1(); break;
                        case 5: gen_op_mull_T0_T1(); break;
                        default: abort();
                        }
                        if (op == 6 || op == 7) {
                            switch (size) {
                            case 0: gen_op_neon_negl_u16(); break;
                            case 1: gen_op_neon_negl_u32(); break;
                            case 2: gen_op_neon_negl_u64(); break;
                            default: abort();
                            }
                        }
                        gen_neon_movl_scratch_T0(0);
                        gen_neon_movl_scratch_T1(1);
                        NEON_GET_REG(T0, rd, pass * 2);
                        NEON_GET_REG(T1, rd, pass * 2 + 1);
                        switch (op) {
                        case 2: case 6:
                            switch (size) {
                            case 0: gen_op_neon_addl_u16(); break;
                            case 1: gen_op_neon_addl_u32(); break;
                            case 2: gen_op_neon_addl_u64(); break;
                            default: abort();
                            }
                            break;
                        case 3: case 7:
                            switch (size) {
                            case 1:
                                gen_op_neon_addl_saturate_s32();
                                gen_op_neon_addl_saturate_s32();
                                break;
                            case 2:
                                gen_op_neon_addl_saturate_s64();
                                gen_op_neon_addl_saturate_s64();
                                break;
                            default: abort();
                            }
                            break;
                        case 10:
                            /* no-op */
                            break;
                        case 11:
                            switch (size) {
                            case 1: gen_op_neon_addl_saturate_s32(); break;
                            case 2: gen_op_neon_addl_saturate_s64(); break;
                            default: abort();
                            }
                            break;
                        default:
                            abort();
                        }
                        NEON_SET_REG(T0, rd, pass * 2);
                        NEON_SET_REG(T1, rd, pass * 2 + 1);
                    }
                    break;
                default: /* 14 and 15 are RESERVED */
                    return 1;
                }
            }
        } else { /* size == 3 */
            if (!u) {
                /* Extract.  */
                int reg;
                imm = (insn >> 8) & 0xf;
                reg = rn;
                count = q ? 4 : 2;
                n = imm >> 2;
                NEON_GET_REG(T0, reg, n);
                for (pass = 0; pass < count; pass++) {
                    n++;
                    if (n > count) {
                        reg = rm;
                        n -= count;
                    }
                    if (imm & 3) {
                        NEON_GET_REG(T1, reg, n);
                        gen_op_neon_extract((insn << 3) & 0x1f);
                    }
                    /* ??? This is broken if rd and rm overlap */
                    NEON_SET_REG(T0, rd, pass);
                    if (imm & 3) {
                        gen_op_movl_T0_T1();
                    } else {
                        NEON_GET_REG(T0, reg, n);
                    }
                }
            } else if ((insn & (1 << 11)) == 0) {
                /* Two register misc.  */
                op = ((insn >> 12) & 0x30) | ((insn >> 7) & 0xf);
                size = (insn >> 18) & 3;
                switch (op) {
                case 0: /* VREV64 */
                    if (size == 3)
                        return 1;
                    for (pass = 0; pass < (q ? 2 : 1); pass++) {
                        NEON_GET_REG(T0, rm, pass * 2);
                        NEON_GET_REG(T1, rm, pass * 2 + 1);
                        switch (size) {
                        case 0: gen_op_rev_T0(); break;
                        case 1: gen_op_revh_T0(); break;
                        case 2: /* no-op */ break;
                        default: abort();
                        }
                        NEON_SET_REG(T0, rd, pass * 2 + 1);
                        if (size == 2) {
                            NEON_SET_REG(T1, rd, pass * 2);
                        } else {
                            gen_op_movl_T0_T1();
                            switch (size) {
                            case 0: gen_op_rev_T0(); break;
                            case 1: gen_op_revh_T0(); break;
                            default: abort();
                            }
                            NEON_SET_REG(T0, rd, pass * 2);
                        }
                    }
                    break;
                case 4: case 5: /* VPADDL */
                case 12: case 13: /* VPADAL */
                    if (size < 2)
                        goto elementwise;
                    if (size == 3)
                        return 1;
                    for (pass = 0; pass < (q ? 2 : 1); pass++) {
                        NEON_GET_REG(T0, rm, pass * 2);
                        NEON_GET_REG(T1, rm, pass * 2 + 1);
                        if (op & 1)
                            gen_op_neon_paddl_u32();
                        else
                            gen_op_neon_paddl_s32();
                        if (op >= 12) {
                            /* Accumulate.  */
                            gen_neon_movl_scratch_T0(0);
                            gen_neon_movl_scratch_T1(1);

                            NEON_GET_REG(T0, rd, pass * 2);
                            NEON_GET_REG(T1, rd, pass * 2 + 1);
                            gen_op_neon_addl_u64();
                        }
                        NEON_SET_REG(T0, rd, pass * 2);
                        NEON_SET_REG(T1, rd, pass * 2 + 1);
                    }
                    break;
                case 33: /* VTRN */
                    if (size == 2) {
                        for (n = 0; n < (q ? 4 : 2); n += 2) {
                            NEON_GET_REG(T0, rm, n);
                            NEON_GET_REG(T1, rd, n + 1);
                            NEON_SET_REG(T1, rm, n);
                            NEON_SET_REG(T0, rd, n + 1);
                        }
                    } else {
                        goto elementwise;
                    }
                    break;
                case 34: /* VUZP */
                    /* Reg  Before       After
                       Rd   A3 A2 A1 A0  B2 B0 A2 A0
                       Rm   B3 B2 B1 B0  B3 B1 A3 A1
                     */
                    if (size == 3)
                        return 1;
                    gen_neon_unzip(rd, q, 0, size);
                    gen_neon_unzip(rm, q, 4, size);
                    if (q) {
                        static int unzip_order_q[8] =
                            {0, 2, 4, 6, 1, 3, 5, 7};
                        for (n = 0; n < 8; n++) {
                            int reg = (n < 4) ? rd : rm;
                            gen_neon_movl_T0_scratch(unzip_order_q[n]);
                            NEON_SET_REG(T0, reg, n % 4);
                        }
                    } else {
                        static int unzip_order[4] =
                            {0, 4, 1, 5};
                        for (n = 0; n < 4; n++) {
                            int reg = (n < 2) ? rd : rm;
                            gen_neon_movl_T0_scratch(unzip_order[n]);
                            NEON_SET_REG(T0, reg, n % 2);
                        }
                    }
                    break;
                case 35: /* VZIP */
                    /* Reg  Before       After
                       Rd   A3 A2 A1 A0  B1 A1 B0 A0
                       Rm   B3 B2 B1 B0  B3 A3 B2 A2
                     */
                    if (size == 3)
                        return 1;
                    count = (q ? 4 : 2);
                    for (n = 0; n < count; n++) {
                        NEON_GET_REG(T0, rd, n);
                        NEON_GET_REG(T1, rd, n);
                        switch (size) {
                        case 0: gen_op_neon_zip_u8(); break;
                        case 1: gen_op_neon_zip_u16(); break;
                        case 2: /* no-op */; break;
                        default: abort();
                        }
                        gen_neon_movl_scratch_T0(n * 2);
                        gen_neon_movl_scratch_T1(n * 2 + 1);
                    }
                    for (n = 0; n < count * 2; n++) {
                        int reg = (n < count) ? rd : rm;
                        gen_neon_movl_T0_scratch(n);
                        NEON_SET_REG(T0, reg, n % count);
                    }
                    break;
                case 36: case 37: /* VMOVN, VQMOVUN, VQMOVN */
                    for (pass = 0; pass < 2; pass++) {
                        if (rd == rm + 1) {
                            n = 1 - pass;
                        } else {
                            n = pass;
                        }
                        NEON_GET_REG(T0, rm, n * 2);
                        NEON_GET_REG(T1, rm, n * 2 + 1);
                        if (op == 36 && q == 0) {
                            switch (size) {
                            case 0: gen_op_neon_narrow_u8(); break;
                            case 1: gen_op_neon_narrow_u16(); break;
                            case 2: /* no-op */ break;
                            default: return 1;
                            }
                        } else if (q) {
                            switch (size) {
                            case 0: gen_op_neon_narrow_sat_u8(); break;
                            case 1: gen_op_neon_narrow_sat_u16(); break;
                            case 2: gen_op_neon_narrow_sat_u32(); break;
                            default: return 1;
                            }
                        } else {
                            switch (size) {
                            case 0: gen_op_neon_narrow_sat_s8(); break;
                            case 1: gen_op_neon_narrow_sat_s16(); break;
                            case 2: gen_op_neon_narrow_sat_s32(); break;
                            default: return 1;
                            }
                        }
                        NEON_SET_REG(T0, rd, n);
                    }
                    break;
                case 38: /* VSHLL */
                    if (q)
                        return 1;
                    if (rm == rd) {
                        NEON_GET_REG(T2, rm, 1);
                    }
                    for (pass = 0; pass < 2; pass++) {
                        if (pass == 1 && rm == rd) {
                            gen_op_movl_T0_T2();
                        } else {
                            NEON_GET_REG(T0, rm, pass);
                        }
                        switch (size) {
                        case 0: gen_op_neon_widen_high_u8(); break;
                        case 1: gen_op_neon_widen_high_u16(); break;
                        case 2:
                            gen_op_movl_T1_T0();
                            gen_op_movl_T0_im(0);
                            break;
                        default: return 1;
                        }
                        NEON_SET_REG(T0, rd, pass * 2);
                        NEON_SET_REG(T1, rd, pass * 2 + 1);
                    }
                    break;
                default:
                elementwise:
                    for (pass = 0; pass < (q ? 4 : 2); pass++) {
                        if (op == 30 || op == 31 || op >= 58) {
                            gen_op_vfp_getreg_F0s(neon_reg_offset(rm, pass));
                        } else {
                            NEON_GET_REG(T0, rm, pass);
                        }
                        switch (op) {
                        case 1: /* VREV32 */
                            switch (size) {
                            case 0: gen_op_rev_T0(); break;
                            case 1: gen_op_revh_T0(); break;
                            default: return 1;
                            }
                            break;
                        case 2: /* VREV16 */
                            if (size != 0)
                                return 1;
                            gen_op_rev16_T0();
                            break;
                        case 4: case 5: /* VPADDL */
                        case 12: case 13: /* VPADAL */
                            switch ((size << 1) | (op & 1)) {
                            case 0: gen_op_neon_paddl_s8(); break;
                            case 1: gen_op_neon_paddl_u8(); break;
                            case 2: gen_op_neon_paddl_s16(); break;
                            case 3: gen_op_neon_paddl_u16(); break;
                            default: abort();
                            }
                            if (op >= 12) {
                                /* Accumulate */
                                NEON_GET_REG(T1, rd, pass);
                                switch (size) {
                                case 0: gen_op_neon_add_u16(); break;
                                case 1: gen_op_addl_T0_T1(); break;
                                default: abort();
                                }
                            }
                            break;
                        case 8: /* CLS */
                            switch (size) {
                            case 0: gen_op_neon_cls_s8(); break;
                            case 1: gen_op_neon_cls_s16(); break;
                            case 2: gen_op_neon_cls_s32(); break;
                            default: return 1;
                            }
                            break;
                        case 9: /* CLZ */
                            switch (size) {
                            case 0: gen_op_neon_clz_u8(); break;
                            case 1: gen_op_neon_clz_u16(); break;
                            case 2: gen_op_clz_T0(); break;
                            default: return 1;
                            }
                            break;
                        case 10: /* CNT */
                            if (size != 0)
                                return 1;
                            gen_op_neon_cnt_u8();
                            break;
                        case 11: /* VNOT */
                            if (size != 0)
                                return 1;
                            gen_op_notl_T0();
                            break;
                        case 14: /* VQABS */
                            switch (size) {
                            case 0: gen_op_neon_qabs_s8(); break;
                            case 1: gen_op_neon_qabs_s16(); break;
                            case 2: gen_op_neon_qabs_s32(); break;
                            default: return 1;
                            }
                            break;
                        case 15: /* VQNEG */
                            switch (size) {
                            case 0: gen_op_neon_qneg_s8(); break;
                            case 1: gen_op_neon_qneg_s16(); break;
                            case 2: gen_op_neon_qneg_s32(); break;
                            default: return 1;
                            }
                            break;
                        case 16: case 19: /* VCGT #0, VCLE #0 */
                            gen_op_movl_T1_im(0);
                            switch(size) {
                            case 0: gen_op_neon_cgt_s8(); break;
                            case 1: gen_op_neon_cgt_s16(); break;
                            case 2: gen_op_neon_cgt_s32(); break;
                            default: return 1;
                            }
                            if (op == 19)
                                gen_op_notl_T0();
                            break;
                        case 17: case 20: /* VCGE #0, VCLT #0 */
                            gen_op_movl_T1_im(0);
                            switch(size) {
                            case 0: gen_op_neon_cge_s8(); break;
                            case 1: gen_op_neon_cge_s16(); break;
                            case 2: gen_op_neon_cge_s32(); break;
                            default: return 1;
                            }
                            if (op == 20)
                                gen_op_notl_T0();
                            break;
                        case 18: /* VCEQ #0 */
                            gen_op_movl_T1_im(0);
                            switch(size) {
                            case 0: gen_op_neon_ceq_u8(); break;
                            case 1: gen_op_neon_ceq_u16(); break;
                            case 2: gen_op_neon_ceq_u32(); break;
                            default: return 1;
                            }
                            break;
                        case 22: /* VABS */
                            switch(size) {
                            case 0: gen_op_neon_abs_s8(); break;
                            case 1: gen_op_neon_abs_s16(); break;
                            case 2: gen_op_neon_abs_s32(); break;
                            default: return 1;
                            }
                            break;
                        case 23: /* VNEG */
                            gen_op_movl_T1_im(0);
                            switch(size) {
                            case 0: gen_op_neon_rsb_u8(); break;
                            case 1: gen_op_neon_rsb_u16(); break;
                            case 2: gen_op_rsbl_T0_T1(); break;
                            default: return 1;
                            }
                            break;
                        case 24: case 27: /* Float VCGT #0, Float VCLE #0 */
                            gen_op_movl_T1_im(0);
                            gen_op_neon_cgt_f32();
                            if (op == 27)
                                gen_op_notl_T0();
                            break;
                        case 25: case 28: /* Float VCGE #0, Float VCLT #0 */
                            gen_op_movl_T1_im(0);
                            gen_op_neon_cge_f32();
                            if (op == 28)
                                gen_op_notl_T0();
                            break;
                        case 26: /* Float VCEQ #0 */
                            gen_op_movl_T1_im(0);
                            gen_op_neon_ceq_f32();
                            break;
                        case 30: /* Float VABS */
                            gen_op_vfp_abss();
                            break;
                        case 31: /* Float VNEG */
                            gen_op_vfp_negs();
                            break;
                        case 32: /* VSWP */
                            NEON_GET_REG(T1, rd, pass);
                            NEON_SET_REG(T1, rm, pass);
                            break;
                        case 33: /* VTRN */
                            NEON_GET_REG(T1, rd, pass);
                            switch (size) {
                            case 0: gen_op_neon_trn_u8(); break;
                            case 1: gen_op_neon_trn_u16(); break;
                            case 2: abort();
                            default: return 1;
                            }
                            NEON_SET_REG(T1, rm, pass);
                            break;
                        case 56: /* Integer VRECPE */
                            gen_op_neon_recpe_u32();
                            break;
                        case 57: /* Integer VRSQRTE */
                            gen_op_neon_rsqrte_u32();
                            break;
                        case 58: /* Float VRECPE */
                            gen_op_neon_recpe_f32();
                            break;
                        case 59: /* Float VRSQRTE */
                            gen_op_neon_rsqrte_f32();
                            break;
                        case 60: /* VCVT.F32.S32 */
                            gen_op_vfp_tosizs();
                            break;
                        case 61: /* VCVT.F32.U32 */
                            gen_op_vfp_touizs();
                            break;
                        case 62: /* VCVT.S32.F32 */
                            gen_op_vfp_sitos();
                            break;
                        case 63: /* VCVT.U32.F32 */
                            gen_op_vfp_uitos();
                            break;
                        default:
                            /* Reserved: 21, 29, 39-56 */
                            return 1;
                        }
                        if (op == 30 || op == 31 || op >= 58) {
                            gen_op_vfp_setreg_F0s(neon_reg_offset(rm, pass));
                        } else {
                            NEON_SET_REG(T0, rd, pass);
                        }
                    }
                    break;
                }
            } else if ((insn & (1 << 10)) == 0) {
                /* VTBL, VTBX.  */
                n = (insn >> 5) & 0x18;
                NEON_GET_REG(T1, rm, 0);
                if (insn & (1 << 6)) {
                    NEON_GET_REG(T0, rd, 0);
                } else {
                    gen_op_movl_T0_im(0);
                }
                gen_op_neon_tbl(rn, n);
                gen_op_movl_T2_T0();
                NEON_GET_REG(T1, rm, 1);
                if (insn & (1 << 6)) {
                    NEON_GET_REG(T0, rd, 0);
                } else {
                    gen_op_movl_T0_im(0);
                }
                gen_op_neon_tbl(rn, n);
                NEON_SET_REG(T2, rd, 0);
                NEON_SET_REG(T0, rd, 1);
            } else if ((insn & 0x380) == 0) {
                /* VDUP */
                if (insn & (1 << 19)) {
                    NEON_SET_REG(T0, rm, 1);
                } else {
                    NEON_SET_REG(T0, rm, 0);
                }
                if (insn & (1 << 16)) {
                    gen_op_neon_dup_u8(((insn >> 17) & 3) * 8);
                } else if (insn & (1 << 17)) {
                    if ((insn >> 18) & 1)
                        gen_op_neon_dup_high16();
                    else
                        gen_op_neon_dup_low16();
                }
                for (pass = 0; pass < (q ? 4 : 2); pass++) {
                    NEON_SET_REG(T0, rd, pass);
                }
            } else {
                return 1;
            }
        }
    }
    return 0;
}

static int disas_coproc_insn(CPUState * env, DisasContext *s, uint32_t insn)
{
    int cpnum;

    cpnum = (insn >> 8) & 0xf;
    if (arm_feature(env, ARM_FEATURE_XSCALE)
	    && ((env->cp15.c15_cpar ^ 0x3fff) & (1 << cpnum)))
	return 1;

    switch (cpnum) {
      case 0:
      case 1:
	if (arm_feature(env, ARM_FEATURE_IWMMXT)) {
	    return disas_iwmmxt_insn(env, s, insn);
	} else if (arm_feature(env, ARM_FEATURE_XSCALE)) {
	    return disas_dsp_insn(env, s, insn);
	}
	return 1;
    case 10:
    case 11:
	return disas_vfp_insn (env, s, insn);
    case 15:
	return disas_cp15_insn (env, s, insn);
    default:
	/* Unknown coprocessor.  See if the board has hooked it.  */
	return disas_cp_insn (env, s, insn);
    }
}

static void disas_arm_insn(CPUState * env, DisasContext *s)
{
    unsigned int cond, insn, val, op1, i, shift, rm, rs, rn, rd, sh;

    insn = ldl_code(s->pc);
    s->pc += 4;

    /* M variants do not implement ARM mode.  */
    if (IS_M(env))
        goto illegal_op;
    cond = insn >> 28;
    if (cond == 0xf){
        /* Unconditional instructions.  */
        if (((insn >> 25) & 7) == 1) {
            /* NEON Data processing.  */
            if (!arm_feature(env, ARM_FEATURE_NEON))
                goto illegal_op;

            if (disas_neon_data_insn(env, s, insn))
                goto illegal_op;
            return;
        }
        if ((insn & 0x0f100000) == 0x04000000) {
            /* NEON load/store.  */
            if (!arm_feature(env, ARM_FEATURE_NEON))
                goto illegal_op;

            if (disas_neon_ls_insn(env, s, insn))
                goto illegal_op;
            return;
        }
        if ((insn & 0x0d70f000) == 0x0550f000)
            return; /* PLD */
        else if ((insn & 0x0ffffdff) == 0x01010000) {
            ARCH(6);
            /* setend */
            if (insn & (1 << 9)) {
                /* BE8 mode not implemented.  */
                goto illegal_op;
            }
            return;
        } else if ((insn & 0x0fffff00) == 0x057ff000) {
            switch ((insn >> 4) & 0xf) {
            case 1: /* clrex */
                ARCH(6K);
                gen_op_clrex();
                return;
            case 4: /* dsb */
            case 5: /* dmb */
            case 6: /* isb */
                ARCH(7);
                /* We don't emulate caches so these are a no-op.  */
                return;
            default:
                goto illegal_op;
            }
        } else if ((insn & 0x0e5fffe0) == 0x084d0500) {
            /* srs */
            uint32_t offset;
            if (IS_USER(s))
                goto illegal_op;
            ARCH(6);
            op1 = (insn & 0x1f);
            if (op1 == (env->uncached_cpsr & CPSR_M)) {
                gen_movl_T1_reg(s, 13);
            } else {
                gen_op_movl_T1_r13_banked(op1);
            }
            i = (insn >> 23) & 3;
            switch (i) {
            case 0: offset = -4; break; /* DA */
            case 1: offset = -8; break; /* DB */
            case 2: offset = 0; break; /* IA */
            case 3: offset = 4; break; /* IB */
            default: abort();
            }
            if (offset)
                gen_op_addl_T1_im(offset);
            gen_movl_T0_reg(s, 14);
            gen_ldst(stl, s);
            gen_op_movl_T0_cpsr();
            gen_op_addl_T1_im(4);
            gen_ldst(stl, s);
            if (insn & (1 << 21)) {
                /* Base writeback.  */
                switch (i) {
                case 0: offset = -8; break;
                case 1: offset = -4; break;
                case 2: offset = 4; break;
                case 3: offset = 0; break;
                default: abort();
                }
                if (offset)
                    gen_op_addl_T1_im(offset);
                if (op1 == (env->uncached_cpsr & CPSR_M)) {
                    gen_movl_reg_T1(s, 13);
                } else {
                    gen_op_movl_r13_T1_banked(op1);
                }
            }
        } else if ((insn & 0x0e5fffe0) == 0x081d0a00) {
            /* rfe */
            uint32_t offset;
            if (IS_USER(s))
                goto illegal_op;
            ARCH(6);
            rn = (insn >> 16) & 0xf;
            gen_movl_T1_reg(s, rn);
            i = (insn >> 23) & 3;
            switch (i) {
            case 0: offset = 0; break; /* DA */
            case 1: offset = -4; break; /* DB */
            case 2: offset = 4; break; /* IA */
            case 3: offset = 8; break; /* IB */
            default: abort();
            }
            if (offset)
                gen_op_addl_T1_im(offset);
            /* Load CPSR into T2 and PC into T0.  */
            gen_ldst(ldl, s);
            gen_op_movl_T2_T0();
            gen_op_addl_T1_im(-4);
            gen_ldst(ldl, s);
            if (insn & (1 << 21)) {
                /* Base writeback.  */
                switch (i) {
                case 0: offset = -4; break;
                case 1: offset = 0; break;
                case 2: offset = 8; break;
                case 3: offset = 4; break;
                default: abort();
                }
                if (offset)
                    gen_op_addl_T1_im(offset);
                gen_movl_reg_T1(s, rn);
            }
            gen_rfe(s);
        } else if ((insn & 0x0e000000) == 0x0a000000) {
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
        } else if ((insn & 0x0e000f00) == 0x0c000100) {
            if (arm_feature(env, ARM_FEATURE_IWMMXT)) {
                /* iWMMXt register transfer.  */
                if (env->cp15.c15_cpar & (1 << 1))
                    if (!disas_iwmmxt_insn(env, s, insn))
                        return;
            }
        } else if ((insn & 0x0fe00000) == 0x0c400000) {
            /* Coprocessor double register transfer.  */
        } else if ((insn & 0x0f000010) == 0x0e000010) {
            /* Additional coprocessor register transfer.  */
        } else if ((insn & 0x0ff10010) == 0x01000000) {
            uint32_t mask;
            uint32_t val;
            /* cps (privileged) */
            if (IS_USER(s))
                return;
            mask = val = 0;
            if (insn & (1 << 19)) {
                if (insn & (1 << 8))
                    mask |= CPSR_A;
                if (insn & (1 << 7))
                    mask |= CPSR_I;
                if (insn & (1 << 6))
                    mask |= CPSR_F;
                if (insn & (1 << 18))
                    val |= mask;
            }
            if (insn & (1 << 14)) {
                mask |= CPSR_M;
                val |= (insn & 0x1f);
            }
            if (mask) {
                gen_op_movl_T0_im(val);
                gen_set_psr_T0(s, mask, 0);
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
    }
    if ((insn & 0x0f900000) == 0x03000000) {
        if ((insn & (1 << 21)) == 0) {
            ARCH(6T2);
            rd = (insn >> 12) & 0xf;
            val = ((insn >> 4) & 0xf000) | (insn & 0xfff);
            if ((insn & (1 << 22)) == 0) {
                /* MOVW */
                gen_op_movl_T0_im(val);
            } else {
                /* MOVT */
                gen_movl_T0_reg(s, rd);
                gen_op_movl_T1_im(0xffff);
                gen_op_andl_T0_T1();
                gen_op_movl_T1_im(val << 16);
                gen_op_orl_T0_T1();
            }
            gen_movl_reg_T0(s, rd);
        } else {
            if (((insn >> 12) & 0xf) != 0xf)
                goto illegal_op;
            if (((insn >> 16) & 0xf) == 0) {
                gen_nop_hint(s, insn & 0xff);
            } else {
                /* CPSR = immediate */
                val = insn & 0xff;
                shift = ((insn >> 8) & 0xf) * 2;
                if (shift)
                    val = (val >> shift) | (val << (32 - shift));
                gen_op_movl_T0_im(val);
                i = ((insn & (1 << 22)) != 0);
                if (gen_set_psr_T0(s, msr_mask(env, s, (insn >> 16) & 0xf, i), i))
                    goto illegal_op;
            }
        }
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
                if (gen_set_psr_T0(s, msr_mask(env, s, (insn >> 16) & 0xf, i), i))
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
            gen_op_movl_T1_im(val);
            gen_movl_T0_reg(s, rm);
            gen_movl_reg_T1(s, 14);
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
            gen_set_condexec(s);
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
                    op1 = (insn >> 20) & 0xf;
                    switch (op1) {
                    case 0: case 1: case 2: case 3: case 6:
                        /* 32 bit mul */
                        gen_movl_T0_reg(s, rs);
                        gen_movl_T1_reg(s, rm);
                        gen_op_mul_T0_T1();
                        if (insn & (1 << 22)) {
                            /* Subtract (mls) */
                            ARCH(6T2);
                            gen_movl_T1_reg(s, rn);
                            gen_op_rsbl_T0_T1();
                        } else if (insn & (1 << 21)) {
                            /* Add */
                            gen_movl_T1_reg(s, rn);
                            gen_op_addl_T0_T1();
                        }
                        if (insn & (1 << 20))
                            gen_op_logic_T0_cc();
                        gen_movl_reg_T0(s, rd);
                        break;
                    default:
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
                        break;
                    }
                } else {
                    rn = (insn >> 16) & 0xf;
                    rd = (insn >> 12) & 0xf;
                    if (insn & (1 << 23)) {
                        /* load/store exclusive */
                        gen_movl_T1_reg(s, rn);
                        if (insn & (1 << 20)) {
                            gen_ldst(ldlex, s);
                        } else {
                            rm = insn & 0xf;
                            gen_movl_T0_reg(s, rm);
                            gen_ldst(stlex, s);
                        }
                        gen_movl_reg_T0(s, rd);
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
            goto do_ldst;
        case 0x6:
        case 0x7:
            if (insn & (1 << 4)) {
                ARCH(6);
                /* Armv6 Media instructions.  */
                rm = insn & 0xf;
                rn = (insn >> 16) & 0xf;
                rd = (insn >> 12) & 0xf;
                rs = (insn >> 8) & 0xf;
                switch ((insn >> 23) & 3) {
                case 0: /* Parallel add/subtract.  */
                    op1 = (insn >> 20) & 7;
                    gen_movl_T0_reg(s, rn);
                    gen_movl_T1_reg(s, rm);
                    sh = (insn >> 5) & 7;
                    if ((op1 & 3) == 0 || sh == 5 || sh == 6)
                        goto illegal_op;
                    gen_arm_parallel_addsub[op1][sh]();
                    gen_movl_reg_T0(s, rd);
                    break;
                case 1:
                    if ((insn & 0x00700020) == 0) {
                        /* Hafword pack.  */
                        gen_movl_T0_reg(s, rn);
                        gen_movl_T1_reg(s, rm);
                        shift = (insn >> 7) & 0x1f;
                        if (shift)
                            gen_op_shll_T1_im(shift);
                        if (insn & (1 << 6))
                            gen_op_pkhtb_T0_T1();
                        else
                            gen_op_pkhbt_T0_T1();
                        gen_movl_reg_T0(s, rd);
                    } else if ((insn & 0x00200020) == 0x00200000) {
                        /* [us]sat */
                        gen_movl_T1_reg(s, rm);
                        shift = (insn >> 7) & 0x1f;
                        if (insn & (1 << 6)) {
                            if (shift == 0)
                                shift = 31;
                            gen_op_sarl_T1_im(shift);
                        } else {
                            gen_op_shll_T1_im(shift);
                        }
                        sh = (insn >> 16) & 0x1f;
                        if (sh != 0) {
                            if (insn & (1 << 22))
                                gen_op_usat_T1(sh);
                            else
                                gen_op_ssat_T1(sh);
                        }
                        gen_movl_T1_reg(s, rd);
                    } else if ((insn & 0x00300fe0) == 0x00200f20) {
                        /* [us]sat16 */
                        gen_movl_T1_reg(s, rm);
                        sh = (insn >> 16) & 0x1f;
                        if (sh != 0) {
                            if (insn & (1 << 22))
                                gen_op_usat16_T1(sh);
                            else
                                gen_op_ssat16_T1(sh);
                        }
                        gen_movl_T1_reg(s, rd);
                    } else if ((insn & 0x00700fe0) == 0x00000fa0) {
                        /* Select bytes.  */
                        gen_movl_T0_reg(s, rn);
                        gen_movl_T1_reg(s, rm);
                        gen_op_sel_T0_T1();
                        gen_movl_reg_T0(s, rd);
                    } else if ((insn & 0x000003e0) == 0x00000060) {
                        gen_movl_T1_reg(s, rm);
                        shift = (insn >> 10) & 3;
                        /* ??? In many cases it's not neccessary to do a
                           rotate, a shift is sufficient.  */
                        if (shift != 0)
                            gen_op_rorl_T1_im(shift * 8);
                        op1 = (insn >> 20) & 7;
                        switch (op1) {
                        case 0: gen_op_sxtb16_T1(); break;
                        case 2: gen_op_sxtb_T1();   break;
                        case 3: gen_op_sxth_T1();   break;
                        case 4: gen_op_uxtb16_T1(); break;
                        case 6: gen_op_uxtb_T1();   break;
                        case 7: gen_op_uxth_T1();   break;
                        default: goto illegal_op;
                        }
                        if (rn != 15) {
                            gen_movl_T2_reg(s, rn);
                            if ((op1 & 3) == 0) {
                                gen_op_add16_T1_T2();
                            } else {
                                gen_op_addl_T1_T2();
                            }
                        }
                        gen_movl_reg_T1(s, rd);
                    } else if ((insn & 0x003f0f60) == 0x003f0f20) {
                        /* rev */
                        gen_movl_T0_reg(s, rm);
                        if (insn & (1 << 22)) {
                            if (insn & (1 << 7)) {
                                gen_op_revsh_T0();
                            } else {
                                ARCH(6T2);
                                gen_op_rbit_T0();
                            }
                        } else {
                            if (insn & (1 << 7))
                                gen_op_rev16_T0();
                            else
                                gen_op_rev_T0();
                        }
                        gen_movl_reg_T0(s, rd);
                    } else {
                        goto illegal_op;
                    }
                    break;
                case 2: /* Multiplies (Type 3).  */
                    gen_movl_T0_reg(s, rm);
                    gen_movl_T1_reg(s, rs);
                    if (insn & (1 << 20)) {
                        /* Signed multiply most significant [accumulate].  */
                        gen_op_imull_T0_T1();
                        if (insn & (1 << 5))
                            gen_op_roundqd_T0_T1();
                        else
                            gen_op_movl_T0_T1();
                        if (rn != 15) {
                            gen_movl_T1_reg(s, rn);
                            if (insn & (1 << 6)) {
                                gen_op_addl_T0_T1();
                            } else {
                                gen_op_rsbl_T0_T1();
                            }
                        }
                        gen_movl_reg_T0(s, rd);
                    } else {
                        if (insn & (1 << 5))
                            gen_op_swap_half_T1();
                        gen_op_mul_dual_T0_T1();
                        if (insn & (1 << 22)) {
                            if (insn & (1 << 6)) {
                                /* smlald */
                                gen_op_addq_T0_T1_dual(rn, rd);
                            } else {
                                /* smlsld */
                                gen_op_subq_T0_T1_dual(rn, rd);
                            }
                        } else {
                            /* This addition cannot overflow.  */
                            if (insn & (1 << 6)) {
                                /* sm[ul]sd */
                                gen_op_subl_T0_T1();
                            } else {
                                /* sm[ul]ad */
                                gen_op_addl_T0_T1();
                            }
                            if (rn != 15)
                              {
                                gen_movl_T1_reg(s, rn);
                                gen_op_addl_T0_T1_setq();
                              }
                            gen_movl_reg_T0(s, rd);
                        }
                    }
                    break;
                case 3:
                    op1 = ((insn >> 17) & 0x38) | ((insn >> 5) & 7);
                    switch (op1) {
                    case 0: /* Unsigned sum of absolute differences.  */
                            goto illegal_op;
                        gen_movl_T0_reg(s, rm);
                        gen_movl_T1_reg(s, rs);
                        gen_op_usad8_T0_T1();
                        if (rn != 15) {
                            gen_movl_T1_reg(s, rn);
                            gen_op_addl_T0_T1();
                        }
                        gen_movl_reg_T0(s, rd);
                        break;
                    case 0x20: case 0x24: case 0x28: case 0x2c:
                        /* Bitfield insert/clear.  */
                        ARCH(6T2);
                        shift = (insn >> 7) & 0x1f;
                        i = (insn >> 16) & 0x1f;
                        i = i + 1 - shift;
                        if (rm == 15) {
                            gen_op_movl_T1_im(0);
                        } else {
                            gen_movl_T1_reg(s, rm);
                        }
                        if (i != 32) {
                            gen_movl_T0_reg(s, rd);
                            gen_op_bfi_T1_T0(shift, ((1u << i) - 1) << shift);
                        }
                        gen_movl_reg_T1(s, rd);
                        break;
                    case 0x12: case 0x16: case 0x1a: case 0x1e: /* sbfx */
                    case 0x32: case 0x36: case 0x3a: case 0x3e: /* ubfx */
                        gen_movl_T1_reg(s, rm);
                        shift = (insn >> 7) & 0x1f;
                        i = ((insn >> 16) & 0x1f) + 1;
                        if (shift + i > 32)
                            goto illegal_op;
                        if (i < 32) {
                            if (op1 & 0x20) {
                                gen_op_ubfx_T1(shift, (1u << i) - 1);
                            } else {
                                gen_op_sbfx_T1(shift, i);
                            }
                        }
                        gen_movl_reg_T1(s, rd);
                        break;
                    default:
                        goto illegal_op;
                    }
                    break;
                }
                break;
            }
        do_ldst:
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
            if (disas_coproc_insn(env, s, insn))
                goto illegal_op;
            break;
        case 0xf:
            /* swi */
            gen_op_movl_T0_im((long)s->pc);
            gen_op_movl_reg_TN[0][15]();
            s->is_jmp = DISAS_SWI;
            break;
        default:
        illegal_op:
            gen_set_condexec(s);
            gen_op_movl_T0_im((long)s->pc - 4);
            gen_op_movl_reg_TN[0][15]();
            gen_op_undef_insn();
            s->is_jmp = DISAS_JUMP;
            break;
        }
    }
}

/* Return true if this is a Thumb-2 logical op.  */
static int
thumb2_logic_op(int op)
{
    return (op < 8);
}

/* Generate code for a Thumb-2 data processing operation.  If CONDS is nonzero
   then set condition code flags based on the result of the operation.
   If SHIFTER_OUT is nonzero then set the carry flag for logical operations
   to the high bit of T1.
   Returns zero if the opcode is valid.  */

static int
gen_thumb2_data_op(DisasContext *s, int op, int conds, uint32_t shifter_out)
{
    int logic_cc;

    logic_cc = 0;
    switch (op) {
    case 0: /* and */
        gen_op_andl_T0_T1();
        logic_cc = conds;
        break;
    case 1: /* bic */
        gen_op_bicl_T0_T1();
        logic_cc = conds;
        break;
    case 2: /* orr */
        gen_op_orl_T0_T1();
        logic_cc = conds;
        break;
    case 3: /* orn */
        gen_op_notl_T1();
        gen_op_orl_T0_T1();
        logic_cc = conds;
        break;
    case 4: /* eor */
        gen_op_xorl_T0_T1();
        logic_cc = conds;
        break;
    case 8: /* add */
        if (conds)
            gen_op_addl_T0_T1_cc();
        else
            gen_op_addl_T0_T1();
        break;
    case 10: /* adc */
        if (conds)
            gen_op_adcl_T0_T1_cc();
        else
            gen_op_adcl_T0_T1();
        break;
    case 11: /* sbc */
        if (conds)
            gen_op_sbcl_T0_T1_cc();
        else
            gen_op_sbcl_T0_T1();
        break;
    case 13: /* sub */
        if (conds)
            gen_op_subl_T0_T1_cc();
        else
            gen_op_subl_T0_T1();
        break;
    case 14: /* rsb */
        if (conds)
            gen_op_rsbl_T0_T1_cc();
        else
            gen_op_rsbl_T0_T1();
        break;
    default: /* 5, 6, 7, 9, 12, 15. */
        return 1;
    }
    if (logic_cc) {
        gen_op_logic_T0_cc();
        if (shifter_out)
            gen_op_mov_CF_T1();
    }
    return 0;
}

/* Translate a 32-bit thumb instruction.  Returns nonzero if the instruction
   is not legal.  */
static int disas_thumb2_insn(CPUState *env, DisasContext *s, uint16_t insn_hw1)
{
    uint32_t insn, imm, shift, offset, addr;
    uint32_t rd, rn, rm, rs;
    int op;
    int shiftop;
    int conds;
    int logic_cc;

    if (!(arm_feature(env, ARM_FEATURE_THUMB2)
          || arm_feature (env, ARM_FEATURE_M))) {
        /* Thumb-1 cores may need to tread bl and blx as a pair of
           16-bit instructions to get correct prefetch abort behavior.  */
        insn = insn_hw1;
        if ((insn & (1 << 12)) == 0) {
            /* Second half of blx.  */
            offset = ((insn & 0x7ff) << 1);
            gen_movl_T0_reg(s, 14);
            gen_op_movl_T1_im(offset);
            gen_op_addl_T0_T1();
            gen_op_movl_T1_im(0xfffffffc);
            gen_op_andl_T0_T1();

            addr = (uint32_t)s->pc;
            gen_op_movl_T1_im(addr | 1);
            gen_movl_reg_T1(s, 14);
            gen_bx(s);
            return 0;
        }
        if (insn & (1 << 11)) {
            /* Second half of bl.  */
            offset = ((insn & 0x7ff) << 1) | 1;
            gen_movl_T0_reg(s, 14);
            gen_op_movl_T1_im(offset);
            gen_op_addl_T0_T1();

            addr = (uint32_t)s->pc;
            gen_op_movl_T1_im(addr | 1);
            gen_movl_reg_T1(s, 14);
            gen_bx(s);
            return 0;
        }
        if ((s->pc & ~TARGET_PAGE_MASK) == 0) {
            /* Instruction spans a page boundary.  Implement it as two
               16-bit instructions in case the second half causes an
               prefetch abort.  */
            offset = ((int32_t)insn << 21) >> 9;
            addr = s->pc + 2 + offset;
            gen_op_movl_T0_im(addr);
            gen_movl_reg_T0(s, 14);
            return 0;
        }
        /* Fall through to 32-bit decode.  */
    }

    insn = lduw_code(s->pc);
    s->pc += 2;
    insn |= (uint32_t)insn_hw1 << 16;

    if ((insn & 0xf800e800) != 0xf000e800) {
        ARCH(6T2);
    }

    rn = (insn >> 16) & 0xf;
    rs = (insn >> 12) & 0xf;
    rd = (insn >> 8) & 0xf;
    rm = insn & 0xf;
    switch ((insn >> 25) & 0xf) {
    case 0: case 1: case 2: case 3:
        /* 16-bit instructions.  Should never happen.  */
        abort();
    case 4:
        if (insn & (1 << 22)) {
            /* Other load/store, table branch.  */
            if (insn & 0x01200000) {
                /* Load/store doubleword.  */
                if (rn == 15) {
                    gen_op_movl_T1_im(s->pc & ~3);
                } else {
                    gen_movl_T1_reg(s, rn);
                }
                offset = (insn & 0xff) * 4;
                if ((insn & (1 << 23)) == 0)
                    offset = -offset;
                if (insn & (1 << 24)) {
                    gen_op_addl_T1_im(offset);
                    offset = 0;
                }
                if (insn & (1 << 20)) {
                    /* ldrd */
                    gen_ldst(ldl, s);
                    gen_movl_reg_T0(s, rs);
                    gen_op_addl_T1_im(4);
                    gen_ldst(ldl, s);
                    gen_movl_reg_T0(s, rd);
                } else {
                    /* strd */
                    gen_movl_T0_reg(s, rs);
                    gen_ldst(stl, s);
                    gen_op_addl_T1_im(4);
                    gen_movl_T0_reg(s, rd);
                    gen_ldst(stl, s);
                }
                if (insn & (1 << 21)) {
                    /* Base writeback.  */
                    if (rn == 15)
                        goto illegal_op;
                    gen_op_addl_T1_im(offset - 4);
                    gen_movl_reg_T1(s, rn);
                }
            } else if ((insn & (1 << 23)) == 0) {
                /* Load/store exclusive word.  */
                gen_movl_T0_reg(s, rd);
                gen_movl_T1_reg(s, rn);
                if (insn & (1 << 20)) {
                    gen_ldst(ldlex, s);
                } else {
                    gen_ldst(stlex, s);
                }
                gen_movl_reg_T0(s, rd);
            } else if ((insn & (1 << 6)) == 0) {
                /* Table Branch.  */
                if (rn == 15) {
                    gen_op_movl_T1_im(s->pc);
                } else {
                    gen_movl_T1_reg(s, rn);
                }
                gen_movl_T2_reg(s, rm);
                gen_op_addl_T1_T2();
                if (insn & (1 << 4)) {
                    /* tbh */
                    gen_op_addl_T1_T2();
                    gen_ldst(lduw, s);
                } else { /* tbb */
                    gen_ldst(ldub, s);
                }
                gen_op_jmp_T0_im(s->pc);
                s->is_jmp = DISAS_JUMP;
            } else {
                /* Load/store exclusive byte/halfword/doubleword.  */
                op = (insn >> 4) & 0x3;
                gen_movl_T1_reg(s, rn);
                if (insn & (1 << 20)) {
                    switch (op) {
                    case 0:
                        gen_ldst(ldbex, s);
                        break;
                    case 1:
                        gen_ldst(ldwex, s);
                        break;
                    case 3:
                        gen_ldst(ldqex, s);
                        gen_movl_reg_T1(s, rd);
                        break;
                    default:
                        goto illegal_op;
                    }
                    gen_movl_reg_T0(s, rs);
                } else {
                    gen_movl_T0_reg(s, rs);
                    switch (op) {
                    case 0:
                        gen_ldst(stbex, s);
                        break;
                    case 1:
                        gen_ldst(stwex, s);
                        break;
                    case 3:
                        gen_movl_T2_reg(s, rd);
                        gen_ldst(stqex, s);
                        break;
                    default:
                        goto illegal_op;
                    }
                    gen_movl_reg_T0(s, rm);
                }
            }
        } else {
            /* Load/store multiple, RFE, SRS.  */
            if (((insn >> 23) & 1) == ((insn >> 24) & 1)) {
                /* Not available in user mode.  */
                if (!IS_USER(s))
                    goto illegal_op;
                if (insn & (1 << 20)) {
                    /* rfe */
                    gen_movl_T1_reg(s, rn);
                    if (insn & (1 << 24)) {
                        gen_op_addl_T1_im(4);
                    } else {
                        gen_op_addl_T1_im(-4);
                    }
                    /* Load CPSR into T2 and PC into T0.  */
                    gen_ldst(ldl, s);
                    gen_op_movl_T2_T0();
                    gen_op_addl_T1_im(-4);
                    gen_ldst(ldl, s);
                    if (insn & (1 << 21)) {
                        /* Base writeback.  */
                        if (insn & (1 << 24))
                            gen_op_addl_T1_im(8);
                        gen_movl_reg_T1(s, rn);
                    }
                    gen_rfe(s);
                } else {
                    /* srs */
                    op = (insn & 0x1f);
                    if (op == (env->uncached_cpsr & CPSR_M)) {
                        gen_movl_T1_reg(s, 13);
                    } else {
                        gen_op_movl_T1_r13_banked(op);
                    }
                    if ((insn & (1 << 24)) == 0) {
                        gen_op_addl_T1_im(-8);
                    }
                    gen_movl_T0_reg(s, 14);
                    gen_ldst(stl, s);
                    gen_op_movl_T0_cpsr();
                    gen_op_addl_T1_im(4);
                    gen_ldst(stl, s);
                    if (insn & (1 << 21)) {
                        if ((insn & (1 << 24)) == 0) {
                            gen_op_addl_T1_im(-4);
                        } else {
                            gen_op_addl_T1_im(4);
                        }
                        if (op == (env->uncached_cpsr & CPSR_M)) {
                            gen_movl_reg_T1(s, 13);
                        } else {
                            gen_op_movl_r13_T1_banked(op);
                        }
                    }
                }
            } else {
                int i;
                /* Load/store multiple.  */
                gen_movl_T1_reg(s, rn);
                offset = 0;
                for (i = 0; i < 16; i++) {
                    if (insn & (1 << i))
                        offset += 4;
                }
                if (insn & (1 << 24)) {
                    gen_op_addl_T1_im(-offset);
                }

                for (i = 0; i < 16; i++) {
                    if ((insn & (1 << i)) == 0)
                        continue;
                    if (insn & (1 << 20)) {
                        /* Load.  */
                        gen_ldst(ldl, s);
                        if (i == 15) {
                            gen_bx(s);
                        } else {
                            gen_movl_reg_T0(s, i);
                        }
                    } else {
                        /* Store.  */
                        gen_movl_T0_reg(s, i);
                        gen_ldst(stl, s);
                    }
                    gen_op_addl_T1_im(4);
                }
                if (insn & (1 << 21)) {
                    /* Base register writeback.  */
                    if (insn & (1 << 24)) {
                        gen_op_addl_T1_im(-offset);
                    }
                    /* Fault if writeback register is in register list.  */
                    if (insn & (1 << rn))
                        goto illegal_op;
                    gen_movl_reg_T1(s, rn);
                }
            }
        }
        break;
    case 5: /* Data processing register constant shift.  */
        if (rn == 15)
            gen_op_movl_T0_im(0);
        else
            gen_movl_T0_reg(s, rn);
        gen_movl_T1_reg(s, rm);
        op = (insn >> 21) & 0xf;
        shiftop = (insn >> 4) & 3;
        shift = ((insn >> 6) & 3) | ((insn >> 10) & 0x1c);
        conds = (insn & (1 << 20)) != 0;
        logic_cc = (conds && thumb2_logic_op(op));
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
        if (gen_thumb2_data_op(s, op, conds, 0))
            goto illegal_op;
        if (rd != 15)
            gen_movl_reg_T0(s, rd);
        break;
    case 13: /* Misc data processing.  */
        op = ((insn >> 22) & 6) | ((insn >> 7) & 1);
        if (op < 4 && (insn & 0xf000) != 0xf000)
            goto illegal_op;
        switch (op) {
        case 0: /* Register controlled shift.  */
            gen_movl_T0_reg(s, rm);
            gen_movl_T1_reg(s, rn);
            if ((insn & 0x70) != 0)
                goto illegal_op;
            op = (insn >> 21) & 3;
            if (insn & (1 << 20)) {
                gen_shift_T1_T0_cc[op]();
                gen_op_logic_T1_cc();
            } else {
                gen_shift_T1_T0[op]();
            }
            gen_movl_reg_T1(s, rd);
            break;
        case 1: /* Sign/zero extend.  */
            gen_movl_T1_reg(s, rm);
            shift = (insn >> 4) & 3;
            /* ??? In many cases it's not neccessary to do a
               rotate, a shift is sufficient.  */
            if (shift != 0)
                gen_op_rorl_T1_im(shift * 8);
            op = (insn >> 20) & 7;
            switch (op) {
            case 0: gen_op_sxth_T1();   break;
            case 1: gen_op_uxth_T1();   break;
            case 2: gen_op_sxtb16_T1(); break;
            case 3: gen_op_uxtb16_T1(); break;
            case 4: gen_op_sxtb_T1();   break;
            case 5: gen_op_uxtb_T1();   break;
            default: goto illegal_op;
            }
            if (rn != 15) {
                gen_movl_T2_reg(s, rn);
                if ((op >> 1) == 1) {
                    gen_op_add16_T1_T2();
                } else {
                    gen_op_addl_T1_T2();
                }
            }
            gen_movl_reg_T1(s, rd);
            break;
        case 2: /* SIMD add/subtract.  */
            op = (insn >> 20) & 7;
            shift = (insn >> 4) & 7;
            if ((op & 3) == 3 || (shift & 3) == 3)
                goto illegal_op;
            gen_movl_T0_reg(s, rn);
            gen_movl_T1_reg(s, rm);
            gen_thumb2_parallel_addsub[op][shift]();
            gen_movl_reg_T0(s, rd);
            break;
        case 3: /* Other data processing.  */
            op = ((insn >> 17) & 0x38) | ((insn >> 4) & 7);
            if (op < 4) {
                /* Saturating add/subtract.  */
                gen_movl_T0_reg(s, rm);
                gen_movl_T1_reg(s, rn);
                if (op & 2)
                    gen_op_double_T1_saturate();
                if (op & 1)
                    gen_op_subl_T0_T1_saturate();
                else
                    gen_op_addl_T0_T1_saturate();
            } else {
                gen_movl_T0_reg(s, rn);
                switch (op) {
                case 0x0a: /* rbit */
                    gen_op_rbit_T0();
                    break;
                case 0x08: /* rev */
                    gen_op_rev_T0();
                    break;
                case 0x09: /* rev16 */
                    gen_op_rev16_T0();
                    break;
                case 0x0b: /* revsh */
                    gen_op_revsh_T0();
                    break;
                case 0x10: /* sel */
                    gen_movl_T1_reg(s, rm);
                    gen_op_sel_T0_T1();
                    break;
                case 0x18: /* clz */
                    gen_op_clz_T0();
                    break;
                default:
                    goto illegal_op;
                }
            }
            gen_movl_reg_T0(s, rd);
            break;
        case 4: case 5: /* 32-bit multiply.  Sum of absolute differences.  */
            op = (insn >> 4) & 0xf;
            gen_movl_T0_reg(s, rn);
            gen_movl_T1_reg(s, rm);
            switch ((insn >> 20) & 7) {
            case 0: /* 32 x 32 -> 32 */
                gen_op_mul_T0_T1();
                if (rs != 15) {
                    gen_movl_T1_reg(s, rs);
                    if (op)
                        gen_op_rsbl_T0_T1();
                    else
                        gen_op_addl_T0_T1();
                }
                gen_movl_reg_T0(s, rd);
                break;
            case 1: /* 16 x 16 -> 32 */
                gen_mulxy(op & 2, op & 1);
                if (rs != 15) {
                    gen_movl_T1_reg(s, rs);
                    gen_op_addl_T0_T1_setq();
                }
                gen_movl_reg_T0(s, rd);
                break;
            case 2: /* Dual multiply add.  */
            case 4: /* Dual multiply subtract.  */
                if (op)
                    gen_op_swap_half_T1();
                gen_op_mul_dual_T0_T1();
                /* This addition cannot overflow.  */
                if (insn & (1 << 22)) {
                    gen_op_subl_T0_T1();
                } else {
                    gen_op_addl_T0_T1();
                }
                if (rs != 15)
                  {
                    gen_movl_T1_reg(s, rs);
                    gen_op_addl_T0_T1_setq();
                  }
                gen_movl_reg_T0(s, rd);
                break;
            case 3: /* 32 * 16 -> 32msb */
                if (op)
                    gen_op_sarl_T1_im(16);
                else
                    gen_op_sxth_T1();
                gen_op_imulw_T0_T1();
                if (rs != 15)
                  {
                    gen_movl_T1_reg(s, rs);
                    gen_op_addl_T0_T1_setq();
                  }
                gen_movl_reg_T0(s, rd);
                break;
            case 5: case 6: /* 32 * 32 -> 32msb */
                gen_op_imull_T0_T1();
                if (insn & (1 << 5))
                    gen_op_roundqd_T0_T1();
                else
                    gen_op_movl_T0_T1();
                if (rs != 15) {
                    gen_movl_T1_reg(s, rs);
                    if (insn & (1 << 21)) {
                        gen_op_addl_T0_T1();
                    } else {
                        gen_op_rsbl_T0_T1();
                    }
                }
                gen_movl_reg_T0(s, rd);
                break;
            case 7: /* Unsigned sum of absolute differences.  */
                gen_op_usad8_T0_T1();
                if (rs != 15) {
                    gen_movl_T1_reg(s, rs);
                    gen_op_addl_T0_T1();
                }
                gen_movl_reg_T0(s, rd);
                break;
            }
            break;
        case 6: case 7: /* 64-bit multiply, Divide.  */
            op = ((insn >> 4) & 0xf) | ((insn >> 16) & 0x70);
            gen_movl_T0_reg(s, rn);
            gen_movl_T1_reg(s, rm);
            if ((op & 0x50) == 0x10) {
                /* sdiv, udiv */
                if (!arm_feature(env, ARM_FEATURE_DIV))
                    goto illegal_op;
                if (op & 0x20)
                    gen_op_udivl_T0_T1();
                else
                    gen_op_sdivl_T0_T1();
                gen_movl_reg_T0(s, rd);
            } else if ((op & 0xe) == 0xc) {
                /* Dual multiply accumulate long.  */
                if (op & 1)
                    gen_op_swap_half_T1();
                gen_op_mul_dual_T0_T1();
                if (op & 0x10) {
                    gen_op_subl_T0_T1();
                } else {
                    gen_op_addl_T0_T1();
                }
                gen_op_signbit_T1_T0();
                gen_op_addq_T0_T1(rs, rd);
                gen_movl_reg_T0(s, rs);
                gen_movl_reg_T1(s, rd);
            } else {
                if (op & 0x20) {
                    /* Unsigned 64-bit multiply  */
                    gen_op_mull_T0_T1();
                } else {
                    if (op & 8) {
                        /* smlalxy */
                        gen_mulxy(op & 2, op & 1);
                        gen_op_signbit_T1_T0();
                    } else {
                        /* Signed 64-bit multiply  */
                        gen_op_imull_T0_T1();
                    }
                }
                if (op & 4) {
                    /* umaal */
                    gen_op_addq_lo_T0_T1(rs);
                    gen_op_addq_lo_T0_T1(rd);
                } else if (op & 0x40) {
                    /* 64-bit accumulate.  */
                    gen_op_addq_T0_T1(rs, rd);
                }
                gen_movl_reg_T0(s, rs);
                gen_movl_reg_T1(s, rd);
            }
            break;
        }
        break;
    case 6: case 7: case 14: case 15:
        /* Coprocessor.  */
        if (((insn >> 24) & 3) == 3) {
            /* Translate into the equivalent ARM encoding.  */
            insn = (insn & 0xe2ffffff) | ((insn & (1 << 28)) >> 4);
            if (disas_neon_data_insn(env, s, insn))
                goto illegal_op;
        } else {
            if (insn & (1 << 28))
                goto illegal_op;
            if (disas_coproc_insn (env, s, insn))
                goto illegal_op;
        }
        break;
    case 8: case 9: case 10: case 11:
        if (insn & (1 << 15)) {
            /* Branches, misc control.  */
            if (insn & 0x5000) {
                /* Unconditional branch.  */
                /* signextend(hw1[10:0]) -> offset[:12].  */
                offset = ((int32_t)insn << 5) >> 9 & ~(int32_t)0xfff;
                /* hw1[10:0] -> offset[11:1].  */
                offset |= (insn & 0x7ff) << 1;
                /* (~hw2[13, 11] ^ offset[24]) -> offset[23,22]
                   offset[24:22] already have the same value because of the
                   sign extension above.  */
                offset ^= ((~insn) & (1 << 13)) << 10;
                offset ^= ((~insn) & (1 << 11)) << 11;

                addr = s->pc;
                if (insn & (1 << 14)) {
                    /* Branch and link.  */
                    gen_op_movl_T1_im(addr | 1);
                    gen_movl_reg_T1(s, 14);
                }

                addr += offset;
                if (insn & (1 << 12)) {
                    /* b/bl */
                    gen_jmp(s, addr);
                } else {
                    /* blx */
                    addr &= ~(uint32_t)2;
                    gen_op_movl_T0_im(addr);
                    gen_bx(s);
                }
            } else if (((insn >> 23) & 7) == 7) {
                /* Misc control */
                if (insn & (1 << 13))
                    goto illegal_op;

                if (insn & (1 << 26)) {
                    /* Secure monitor call (v6Z) */
                    goto illegal_op; /* not implemented.  */
                } else {
                    op = (insn >> 20) & 7;
                    switch (op) {
                    case 0: /* msr cpsr.  */
                        if (IS_M(env)) {
                            gen_op_v7m_msr_T0(insn & 0xff);
                            gen_movl_reg_T0(s, rn);
                            gen_lookup_tb(s);
                            break;
                        }
                        /* fall through */
                    case 1: /* msr spsr.  */
                        if (IS_M(env))
                            goto illegal_op;
                        gen_movl_T0_reg(s, rn);
                        if (gen_set_psr_T0(s,
                              msr_mask(env, s, (insn >> 8) & 0xf, op == 1),
                              op == 1))
                            goto illegal_op;
                        break;
                    case 2: /* cps, nop-hint.  */
                        if (((insn >> 8) & 7) == 0) {
                            gen_nop_hint(s, insn & 0xff);
                        }
                        /* Implemented as NOP in user mode.  */
                        if (IS_USER(s))
                            break;
                        offset = 0;
                        imm = 0;
                        if (insn & (1 << 10)) {
                            if (insn & (1 << 7))
                                offset |= CPSR_A;
                            if (insn & (1 << 6))
                                offset |= CPSR_I;
                            if (insn & (1 << 5))
                                offset |= CPSR_F;
                            if (insn & (1 << 9))
                                imm = CPSR_A | CPSR_I | CPSR_F;
                        }
                        if (insn & (1 << 8)) {
                            offset |= 0x1f;
                            imm |= (insn & 0x1f);
                        }
                        if (offset) {
                            gen_op_movl_T0_im(imm);
                            gen_set_psr_T0(s, offset, 0);
                        }
                        break;
                    case 3: /* Special control operations.  */
                        op = (insn >> 4) & 0xf;
                        switch (op) {
                        case 2: /* clrex */
                            gen_op_clrex();
                            break;
                        case 4: /* dsb */
                        case 5: /* dmb */
                        case 6: /* isb */
                            /* These execute as NOPs.  */
                            ARCH(7);
                            break;
                        default:
                            goto illegal_op;
                        }
                        break;
                    case 4: /* bxj */
                        /* Trivial implementation equivalent to bx.  */
                        gen_movl_T0_reg(s, rn);
                        gen_bx(s);
                        break;
                    case 5: /* Exception return.  */
                        /* Unpredictable in user mode.  */
                        goto illegal_op;
                    case 6: /* mrs cpsr.  */
                        if (IS_M(env)) {
                            gen_op_v7m_mrs_T0(insn & 0xff);
                        } else {
                            gen_op_movl_T0_cpsr();
                        }
                        gen_movl_reg_T0(s, rd);
                        break;
                    case 7: /* mrs spsr.  */
                        /* Not accessible in user mode.  */
                        if (IS_USER(s) || IS_M(env))
                            goto illegal_op;
                        gen_op_movl_T0_spsr();
                        gen_movl_reg_T0(s, rd);
                        break;
                    }
                }
            } else {
                /* Conditional branch.  */
                op = (insn >> 22) & 0xf;
                /* Generate a conditional jump to next instruction.  */
                s->condlabel = gen_new_label();
                gen_test_cc[op ^ 1](s->condlabel);
                s->condjmp = 1;

                /* offset[11:1] = insn[10:0] */
                offset = (insn & 0x7ff) << 1;
                /* offset[17:12] = insn[21:16].  */
                offset |= (insn & 0x003f0000) >> 4;
                /* offset[31:20] = insn[26].  */
                offset |= ((int32_t)((insn << 5) & 0x80000000)) >> 11;
                /* offset[18] = insn[13].  */
                offset |= (insn & (1 << 13)) << 5;
                /* offset[19] = insn[11].  */
                offset |= (insn & (1 << 11)) << 8;

                /* jump to the offset */
                addr = s->pc + offset;
                gen_jmp(s, addr);
            }
        } else {
            /* Data processing immediate.  */
            if (insn & (1 << 25)) {
                if (insn & (1 << 24)) {
                    if (insn & (1 << 20))
                        goto illegal_op;
                    /* Bitfield/Saturate.  */
                    op = (insn >> 21) & 7;
                    imm = insn & 0x1f;
                    shift = ((insn >> 6) & 3) | ((insn >> 10) & 0x1c);
                    if (rn == 15)
                        gen_op_movl_T1_im(0);
                    else
                        gen_movl_T1_reg(s, rn);
                    switch (op) {
                    case 2: /* Signed bitfield extract.  */
                        imm++;
                        if (shift + imm > 32)
                            goto illegal_op;
                        if (imm < 32)
                            gen_op_sbfx_T1(shift, imm);
                        break;
                    case 6: /* Unsigned bitfield extract.  */
                        imm++;
                        if (shift + imm > 32)
                            goto illegal_op;
                        if (imm < 32)
                            gen_op_ubfx_T1(shift, (1u << imm) - 1);
                        break;
                    case 3: /* Bitfield insert/clear.  */
                        if (imm < shift)
                            goto illegal_op;
                        imm = imm + 1 - shift;
                        if (imm != 32) {
                            gen_movl_T0_reg(s, rd);
                            gen_op_bfi_T1_T0(shift, ((1u << imm) - 1) << shift);
                        }
                        break;
                    case 7:
                        goto illegal_op;
                    default: /* Saturate.  */
                        gen_movl_T1_reg(s, rn);
                        if (shift) {
                            if (op & 1)
                                gen_op_sarl_T1_im(shift);
                            else
                                gen_op_shll_T1_im(shift);
                        }
                        if (op & 4) {
                            /* Unsigned.  */
                            gen_op_ssat_T1(imm);
                            if ((op & 1) && shift == 0)
                                gen_op_usat16_T1(imm);
                            else
                                gen_op_usat_T1(imm);
                        } else {
                            /* Signed.  */
                            gen_op_ssat_T1(imm);
                            if ((op & 1) && shift == 0)
                                gen_op_ssat16_T1(imm);
                            else
                                gen_op_ssat_T1(imm);
                        }
                        break;
                    }
                    gen_movl_reg_T1(s, rd);
                } else {
                    imm = ((insn & 0x04000000) >> 15)
                          | ((insn & 0x7000) >> 4) | (insn & 0xff);
                    if (insn & (1 << 22)) {
                        /* 16-bit immediate.  */
                        imm |= (insn >> 4) & 0xf000;
                        if (insn & (1 << 23)) {
                            /* movt */
                            gen_movl_T0_reg(s, rd);
                            gen_op_movtop_T0_im(imm << 16);
                        } else {
                            /* movw */
                            gen_op_movl_T0_im(imm);
                        }
                    } else {
                        /* Add/sub 12-bit immediate.  */
                        if (rn == 15) {
                            addr = s->pc & ~(uint32_t)3;
                            if (insn & (1 << 23))
                                addr -= imm;
                            else
                                addr += imm;
                            gen_op_movl_T0_im(addr);
                        } else {
                            gen_movl_T0_reg(s, rn);
                            gen_op_movl_T1_im(imm);
                            if (insn & (1 << 23))
                                gen_op_subl_T0_T1();
                            else
                                gen_op_addl_T0_T1();
                        }
                    }
                    gen_movl_reg_T0(s, rd);
                }
            } else {
                int shifter_out = 0;
                /* modified 12-bit immediate.  */
                shift = ((insn & 0x04000000) >> 23) | ((insn & 0x7000) >> 12);
                imm = (insn & 0xff);
                switch (shift) {
                case 0: /* XY */
                    /* Nothing to do.  */
                    break;
                case 1: /* 00XY00XY */
                    imm |= imm << 16;
                    break;
                case 2: /* XY00XY00 */
                    imm |= imm << 16;
                    imm <<= 8;
                    break;
                case 3: /* XYXYXYXY */
                    imm |= imm << 16;
                    imm |= imm << 8;
                    break;
                default: /* Rotated constant.  */
                    shift = (shift << 1) | (imm >> 7);
                    imm |= 0x80;
                    imm = imm << (32 - shift);
                    shifter_out = 1;
                    break;
                }
                gen_op_movl_T1_im(imm);
                rn = (insn >> 16) & 0xf;
                if (rn == 15)
                    gen_op_movl_T0_im(0);
                else
                    gen_movl_T0_reg(s, rn);
                op = (insn >> 21) & 0xf;
                if (gen_thumb2_data_op(s, op, (insn & (1 << 20)) != 0,
                                       shifter_out))
                    goto illegal_op;
                rd = (insn >> 8) & 0xf;
                if (rd != 15) {
                    gen_movl_reg_T0(s, rd);
                }
            }
        }
        break;
    case 12: /* Load/store single data item.  */
        {
        int postinc = 0;
        int writeback = 0;
        if ((insn & 0x01100000) == 0x01000000) {
            if (disas_neon_ls_insn(env, s, insn))
                goto illegal_op;
            break;
        }
        if (rn == 15) {
            /* PC relative.  */
            /* s->pc has already been incremented by 4.  */
            imm = s->pc & 0xfffffffc;
            if (insn & (1 << 23))
                imm += insn & 0xfff;
            else
                imm -= insn & 0xfff;
            gen_op_movl_T1_im(imm);
        } else {
            gen_movl_T1_reg(s, rn);
            if (insn & (1 << 23)) {
                /* Positive offset.  */
                imm = insn & 0xfff;
                gen_op_addl_T1_im(imm);
            } else {
                op = (insn >> 8) & 7;
                imm = insn & 0xff;
                switch (op) {
                case 0: case 8: /* Shifted Register.  */
                    shift = (insn >> 4) & 0xf;
                    if (shift > 3)
                        goto illegal_op;
                    gen_movl_T2_reg(s, rm);
                    if (shift)
                        gen_op_shll_T2_im(shift);
                    gen_op_addl_T1_T2();
                    break;
                case 4: /* Negative offset.  */
                    gen_op_addl_T1_im(-imm);
                    break;
                case 6: /* User privilege.  */
                    gen_op_addl_T1_im(imm);
                    break;
                case 1: /* Post-decrement.  */
                    imm = -imm;
                    /* Fall through.  */
                case 3: /* Post-increment.  */
                    gen_op_movl_T2_im(imm);
                    postinc = 1;
                    writeback = 1;
                    break;
                case 5: /* Pre-decrement.  */
                    imm = -imm;
                    /* Fall through.  */
                case 7: /* Pre-increment.  */
                    gen_op_addl_T1_im(imm);
                    writeback = 1;
                    break;
                default:
                    goto illegal_op;
                }
            }
        }
        op = ((insn >> 21) & 3) | ((insn >> 22) & 4);
        if (insn & (1 << 20)) {
            /* Load.  */
            if (rs == 15 && op != 2) {
                if (op & 2)
                    goto illegal_op;
                /* Memory hint.  Implemented as NOP.  */
            } else {
                switch (op) {
                case 0: gen_ldst(ldub, s); break;
                case 4: gen_ldst(ldsb, s); break;
                case 1: gen_ldst(lduw, s); break;
                case 5: gen_ldst(ldsw, s); break;
                case 2: gen_ldst(ldl, s); break;
                default: goto illegal_op;
                }
                if (rs == 15) {
                    gen_bx(s);
                } else {
                    gen_movl_reg_T0(s, rs);
                }
            }
        } else {
            /* Store.  */
            if (rs == 15)
                goto illegal_op;
            gen_movl_T0_reg(s, rs);
            switch (op) {
            case 0: gen_ldst(stb, s); break;
            case 1: gen_ldst(stw, s); break;
            case 2: gen_ldst(stl, s); break;
            default: goto illegal_op;
            }
        }
        if (postinc)
            gen_op_addl_T1_im(imm);
        if (writeback)
            gen_movl_reg_T1(s, rn);
        }
        break;
    default:
        goto illegal_op;
    }
    return 0;
illegal_op:
    return 1;
}

static void disas_thumb_insn(CPUState *env, DisasContext *s)
{
    uint32_t val, insn, op, rm, rn, rd, shift, cond;
    int32_t offset;
    int i;

    if (s->condexec_mask) {
        cond = s->condexec_cond;
        s->condlabel = gen_new_label();
        gen_test_cc[cond ^ 1](s->condlabel);
        s->condjmp = 1;
    }

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
            if (insn & (1 << 9)) {
                if (s->condexec_mask)
                    gen_op_subl_T0_T1();
                else
                    gen_op_subl_T0_T1_cc();
            } else {
                if (s->condexec_mask)
                    gen_op_addl_T0_T1();
                else
                    gen_op_addl_T0_T1_cc();
            }
            gen_movl_reg_T0(s, rd);
        } else {
            /* shift immediate */
            rm = (insn >> 3) & 7;
            shift = (insn >> 6) & 0x1f;
            gen_movl_T0_reg(s, rm);
            if (s->condexec_mask)
                gen_shift_T0_im_thumb[op](shift);
            else
                gen_shift_T0_im_thumb_cc[op](shift);
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
            if (!s->condexec_mask)
                gen_op_logic_T0_cc();
            break;
        case 1: /* cmp */
            gen_op_subl_T0_T1_cc();
            break;
        case 2: /* add */
            if (s->condexec_mask)
                gen_op_addl_T0_T1();
            else
                gen_op_addl_T0_T1_cc();
            break;
        case 3: /* sub */
            if (s->condexec_mask)
                gen_op_subl_T0_T1();
            else
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
            if (!s->condexec_mask)
                gen_op_logic_T0_cc();
            break;
        case 0x1: /* eor */
            gen_op_xorl_T0_T1();
            if (!s->condexec_mask)
                gen_op_logic_T0_cc();
            break;
        case 0x2: /* lsl */
            if (s->condexec_mask) {
                gen_op_shll_T1_T0();
            } else {
                gen_op_shll_T1_T0_cc();
                gen_op_logic_T1_cc();
            }
            break;
        case 0x3: /* lsr */
            if (s->condexec_mask) {
                gen_op_shrl_T1_T0();
            } else {
                gen_op_shrl_T1_T0_cc();
                gen_op_logic_T1_cc();
            }
            break;
        case 0x4: /* asr */
            if (s->condexec_mask) {
                gen_op_sarl_T1_T0();
            } else {
                gen_op_sarl_T1_T0_cc();
                gen_op_logic_T1_cc();
            }
            break;
        case 0x5: /* adc */
            if (s->condexec_mask)
                gen_op_adcl_T0_T1();
            else
                gen_op_adcl_T0_T1_cc();
            break;
        case 0x6: /* sbc */
            if (s->condexec_mask)
                gen_op_sbcl_T0_T1();
            else
                gen_op_sbcl_T0_T1_cc();
            break;
        case 0x7: /* ror */
            if (s->condexec_mask) {
                gen_op_rorl_T1_T0();
            } else {
                gen_op_rorl_T1_T0_cc();
                gen_op_logic_T1_cc();
            }
            break;
        case 0x8: /* tst */
            gen_op_andl_T0_T1();
            gen_op_logic_T0_cc();
            rd = 16;
            break;
        case 0x9: /* neg */
            if (s->condexec_mask)
                gen_op_subl_T0_T1();
            else
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
            if (!s->condexec_mask)
                gen_op_logic_T0_cc();
            break;
        case 0xd: /* mul */
            gen_op_mull_T0_T1();
            if (!s->condexec_mask)
                gen_op_logic_T0_cc();
            break;
        case 0xe: /* bic */
            gen_op_bicl_T0_T1();
            if (!s->condexec_mask)
                gen_op_logic_T0_cc();
            break;
        case 0xf: /* mvn */
            gen_op_notl_T1();
            if (!s->condexec_mask)
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

        case 2: /* sign/zero extend.  */
            ARCH(6);
            rd = insn & 7;
            rm = (insn >> 3) & 7;
            gen_movl_T1_reg(s, rm);
            switch ((insn >> 6) & 3) {
            case 0: gen_op_sxth_T1(); break;
            case 1: gen_op_sxtb_T1(); break;
            case 2: gen_op_uxth_T1(); break;
            case 3: gen_op_uxtb_T1(); break;
            }
            gen_movl_reg_T1(s, rd);
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

        case 1: case 3: case 9: case 11: /* czb */
            rm = insn & 7;
            gen_movl_T0_reg(s, rm);
            s->condlabel = gen_new_label();
            s->condjmp = 1;
            if (insn & (1 << 11))
                gen_op_testn_T0(s->condlabel);
            else
                gen_op_test_T0(s->condlabel);

            offset = ((insn & 0xf8) >> 2) | (insn & 0x200) >> 3;
            val = (uint32_t)s->pc + 2;
            val += offset;
            gen_jmp(s, val);
            break;

        case 15: /* IT, nop-hint.  */
            if ((insn & 0xf) == 0) {
                gen_nop_hint(s, (insn >> 4) & 0xf);
                break;
            }
            /* If Then.  */
            s->condexec_cond = (insn >> 4) & 0xe;
            s->condexec_mask = insn & 0x1f;
            /* No actual code generated for this insn, just setup state.  */
            break;

        case 0xe: /* bkpt */
            gen_set_condexec(s);
            gen_op_movl_T0_im((long)s->pc - 2);
            gen_op_movl_reg_TN[0][15]();
            gen_op_bkpt();
            s->is_jmp = DISAS_JUMP;
            break;

        case 0xa: /* rev */
            ARCH(6);
            rn = (insn >> 3) & 0x7;
            rd = insn & 0x7;
            gen_movl_T0_reg(s, rn);
            switch ((insn >> 6) & 3) {
            case 0: gen_op_rev_T0(); break;
            case 1: gen_op_rev16_T0(); break;
            case 3: gen_op_revsh_T0(); break;
            default: goto illegal_op;
            }
            gen_movl_reg_T0(s, rd);
            break;

        case 6: /* cps */
            ARCH(6);
            if (IS_USER(s))
                break;
            if (IS_M(env)) {
                val = (insn & (1 << 4)) != 0;
                gen_op_movl_T0_im(val);
                /* PRIMASK */
                if (insn & 1)
                    gen_op_v7m_msr_T0(16);
                /* FAULTMASK */
                if (insn & 2)
                    gen_op_v7m_msr_T0(17);

                gen_lookup_tb(s);
            } else {
                if (insn & (1 << 4))
                    shift = CPSR_A | CPSR_I | CPSR_F;
                else
                    shift = 0;

                val = ((insn & 7) << 6) & shift;
                gen_op_movl_T0_im(val);
                gen_set_psr_T0(s, shift, 0);
            }
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
            gen_set_condexec(s);
            gen_op_movl_T0_im((long)s->pc | 1);
            /* Don't set r15.  */
            gen_op_movl_reg_TN[0][15]();
            s->is_jmp = DISAS_SWI;
            break;
        }
        /* generate a conditional jump to next instruction */
        s->condlabel = gen_new_label();
        gen_test_cc[cond ^ 1](s->condlabel);
        s->condjmp = 1;
        gen_movl_T1_reg(s, 15);

        /* jump to the offset */
        val = (uint32_t)s->pc + 2;
        offset = ((int32_t)insn << 24) >> 24;
        val += offset << 1;
        gen_jmp(s, val);
        break;

    case 14:
        if (insn & (1 << 11)) {
            if (disas_thumb2_insn(env, s, insn))
              goto undef32;
            break;
        }
        /* unconditional branch */
        val = (uint32_t)s->pc;
        offset = ((int32_t)insn << 21) >> 21;
        val += (offset << 1) + 2;
        gen_jmp(s, val);
        break;

    case 15:
        if (disas_thumb2_insn(env, s, insn))
          goto undef32;
        break;
    }
    return;
undef32:
    gen_set_condexec(s);
    gen_op_movl_T0_im((long)s->pc - 4);
    gen_op_movl_reg_TN[0][15]();
    gen_op_undef_insn();
    s->is_jmp = DISAS_JUMP;
    return;
illegal_op:
undef:
    gen_set_condexec(s);
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
    dc->condexec_mask = (env->condexec_bits & 0xf) << 1;
    dc->condexec_cond = env->condexec_bits >> 4;
    dc->is_mem = 0;
#if !defined(CONFIG_USER_ONLY)
    if (IS_M(env)) {
        dc->user = ((env->v7m.exception == 0) && (env->v7m.control & 1));
    } else {
        dc->user = (env->uncached_cpsr & 0x1f) == ARM_CPU_MODE_USR;
    }
#endif
    next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
    nb_gen_labels = 0;
    lj = -1;
    /* Reset the conditional execution bits immediately. This avoids
       complications trying to do it at the end of the block.  */
    if (env->condexec_bits)
      gen_op_set_condexec(0);
    do {
#ifndef CONFIG_USER_ONLY
        if (dc->pc >= 0xfffffff0 && IS_M(env)) {
            /* We always get here via a jump, so know we are not in a
               conditional execution block.  */
            gen_op_exception_exit();
        }
#endif

        if (env->nb_breakpoints > 0) {
            for(j = 0; j < env->nb_breakpoints; j++) {
                if (env->breakpoints[j] == dc->pc) {
                    gen_set_condexec(dc);
                    gen_op_movl_T0_im((long)dc->pc);
                    gen_op_movl_reg_TN[0][15]();
                    gen_op_debug();
                    dc->is_jmp = DISAS_JUMP;
                    /* Advance PC so that clearing the breakpoint will
                       invalidate this TB.  */
                    dc->pc += 2;
                    goto done_generating;
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

        if (env->thumb) {
            disas_thumb_insn(env, dc);
            if (dc->condexec_mask) {
                dc->condexec_cond = (dc->condexec_cond & 0xe)
                                   | ((dc->condexec_mask >> 4) & 1);
                dc->condexec_mask = (dc->condexec_mask << 1) & 0x1f;
                if (dc->condexec_mask == 0) {
                    dc->condexec_cond = 0;
                }
            }
        } else {
            disas_arm_insn(env, dc);
        }

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
       instruction was a conditional branch or trap, and the PC has
       already been written.  */
    if (__builtin_expect(env->singlestep_enabled, 0)) {
        /* Make sure the pc is updated, and raise a debug exception.  */
        if (dc->condjmp) {
            gen_set_condexec(dc);
            if (dc->is_jmp == DISAS_SWI) {
                gen_op_swi();
            } else {
                gen_op_debug();
            }
            gen_set_label(dc->condlabel);
        }
        if (dc->condjmp || !dc->is_jmp) {
            gen_op_movl_T0_im((long)dc->pc);
            gen_op_movl_reg_TN[0][15]();
            dc->condjmp = 0;
        }
        gen_set_condexec(dc);
        if (dc->is_jmp == DISAS_SWI && !dc->condjmp) {
            gen_op_swi();
        } else {
            /* FIXME: Single stepping a WFI insn will not halt
               the CPU.  */
            gen_op_debug();
        }
    } else {
        /* While branches must always occur at the end of an IT block,
           there are a few other things that can cause us to terminate
           the TB in the middel of an IT block:
            - Exception generating instructions (bkpt, swi, undefined).
            - Page boundaries.
            - Hardware watchpoints.
           Hardware breakpoints have already been handled and skip this code.
         */
        gen_set_condexec(dc);
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
        case DISAS_WFI:
            gen_op_wfi();
            break;
        case DISAS_SWI:
            gen_op_swi();
            break;
        }
        if (dc->condjmp) {
            gen_set_label(dc->condlabel);
            gen_set_condexec(dc);
            gen_goto_tb(dc, 1, dc->pc);
            dc->condjmp = 0;
        }
    }
done_generating:
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

