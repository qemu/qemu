/*
   SPARC micro operations

   Copyright (C) 2003 Thomas M. Ogrisegg <tom@fnord.at>

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

#include "exec.h"

 /*XXX*/
#define REGNAME g0
#define REG (env->gregs[0])
#include "op_template.h"
#define REGNAME g1
#define REG (env->gregs[1])
#include "op_template.h"
#define REGNAME g2
#define REG (env->gregs[2])
#include "op_template.h"
#define REGNAME g3
#define REG (env->gregs[3])
#include "op_template.h"
#define REGNAME g4
#define REG (env->gregs[4])
#include "op_template.h"
#define REGNAME g5
#define REG (env->gregs[5])
#include "op_template.h"
#define REGNAME g6
#define REG (env->gregs[6])
#include "op_template.h"
#define REGNAME g7
#define REG (env->gregs[7])
#include "op_template.h"
#define REGNAME i0
#define REG (env->regwptr[16])
#include "op_template.h"
#define REGNAME i1
#define REG (env->regwptr[17])
#include "op_template.h"
#define REGNAME i2
#define REG (env->regwptr[18])
#include "op_template.h"
#define REGNAME i3
#define REG (env->regwptr[19])
#include "op_template.h"
#define REGNAME i4
#define REG (env->regwptr[20])
#include "op_template.h"
#define REGNAME i5
#define REG (env->regwptr[21])
#include "op_template.h"
#define REGNAME i6
#define REG (env->regwptr[22])
#include "op_template.h"
#define REGNAME i7
#define REG (env->regwptr[23])
#include "op_template.h"
#define REGNAME l0
#define REG (env->regwptr[8])
#include "op_template.h"
#define REGNAME l1
#define REG (env->regwptr[9])
#include "op_template.h"
#define REGNAME l2
#define REG (env->regwptr[10])
#include "op_template.h"
#define REGNAME l3
#define REG (env->regwptr[11])
#include "op_template.h"
#define REGNAME l4
#define REG (env->regwptr[12])
#include "op_template.h"
#define REGNAME l5
#define REG (env->regwptr[13])
#include "op_template.h"
#define REGNAME l6
#define REG (env->regwptr[14])
#include "op_template.h"
#define REGNAME l7
#define REG (env->regwptr[15])
#include "op_template.h"
#define REGNAME o0
#define REG (env->regwptr[0])
#include "op_template.h"
#define REGNAME o1
#define REG (env->regwptr[1])
#include "op_template.h"
#define REGNAME o2
#define REG (env->regwptr[2])
#include "op_template.h"
#define REGNAME o3
#define REG (env->regwptr[3])
#include "op_template.h"
#define REGNAME o4
#define REG (env->regwptr[4])
#include "op_template.h"
#define REGNAME o5
#define REG (env->regwptr[5])
#include "op_template.h"
#define REGNAME o6
#define REG (env->regwptr[6])
#include "op_template.h"
#define REGNAME o7
#define REG (env->regwptr[7])
#include "op_template.h"

#define REGNAME f0
#define REG (env->fpr[0])
#include "fop_template.h"
#define REGNAME f1
#define REG (env->fpr[1])
#include "fop_template.h"
#define REGNAME f2
#define REG (env->fpr[2])
#include "fop_template.h"
#define REGNAME f3
#define REG (env->fpr[3])
#include "fop_template.h"
#define REGNAME f4
#define REG (env->fpr[4])
#include "fop_template.h"
#define REGNAME f5
#define REG (env->fpr[5])
#include "fop_template.h"
#define REGNAME f6
#define REG (env->fpr[6])
#include "fop_template.h"
#define REGNAME f7
#define REG (env->fpr[7])
#include "fop_template.h"
#define REGNAME f8
#define REG (env->fpr[8])
#include "fop_template.h"
#define REGNAME f9
#define REG (env->fpr[9])
#include "fop_template.h"
#define REGNAME f10
#define REG (env->fpr[10])
#include "fop_template.h"
#define REGNAME f11
#define REG (env->fpr[11])
#include "fop_template.h"
#define REGNAME f12
#define REG (env->fpr[12])
#include "fop_template.h"
#define REGNAME f13
#define REG (env->fpr[13])
#include "fop_template.h"
#define REGNAME f14
#define REG (env->fpr[14])
#include "fop_template.h"
#define REGNAME f15
#define REG (env->fpr[15])
#include "fop_template.h"
#define REGNAME f16
#define REG (env->fpr[16])
#include "fop_template.h"
#define REGNAME f17
#define REG (env->fpr[17])
#include "fop_template.h"
#define REGNAME f18
#define REG (env->fpr[18])
#include "fop_template.h"
#define REGNAME f19
#define REG (env->fpr[19])
#include "fop_template.h"
#define REGNAME f20
#define REG (env->fpr[20])
#include "fop_template.h"
#define REGNAME f21
#define REG (env->fpr[21])
#include "fop_template.h"
#define REGNAME f22
#define REG (env->fpr[22])
#include "fop_template.h"
#define REGNAME f23
#define REG (env->fpr[23])
#include "fop_template.h"
#define REGNAME f24
#define REG (env->fpr[24])
#include "fop_template.h"
#define REGNAME f25
#define REG (env->fpr[25])
#include "fop_template.h"
#define REGNAME f26
#define REG (env->fpr[26])
#include "fop_template.h"
#define REGNAME f27
#define REG (env->fpr[27])
#include "fop_template.h"
#define REGNAME f28
#define REG (env->fpr[28])
#include "fop_template.h"
#define REGNAME f29
#define REG (env->fpr[29])
#include "fop_template.h"
#define REGNAME f30
#define REG (env->fpr[30])
#include "fop_template.h"
#define REGNAME f31
#define REG (env->fpr[31])
#include "fop_template.h"

#define EIP (env->pc)

#define FLAG_SET(x) ((env->psr&x)?1:0)
#define FFLAG_SET(x) ((env->fsr&x)?1:0)

void OPPROTO op_movl_T0_0(void)
{
    T0 = 0;
}

void OPPROTO op_movl_T0_im(void)
{
    T0 = PARAM1;
}

void OPPROTO op_movl_T1_im(void)
{
    T1 = PARAM1;
}

void OPPROTO op_movl_T2_im(void)
{
    T2 = PARAM1;
}

void OPPROTO op_add_T1_T0(void)
{
    T0 += T1;
}

void OPPROTO op_add_T1_T0_cc(void)
{
    target_ulong src1;

    src1 = T0;
    T0 += T1;
    env->psr = 0;
    if (!T0)
	env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
	env->psr |= PSR_NEG;
    if (T0 < src1)
	env->psr |= PSR_CARRY;
    if (((src1 ^ T1 ^ -1) & (src1 ^ T0)) & (1 << 31))
	env->psr |= PSR_OVF;
    /* V9 xcc */
    FORCE_RET();
}

void OPPROTO op_addx_T1_T0(void)
{
    T0 += T1 + FLAG_SET(PSR_CARRY);
}

void OPPROTO op_addx_T1_T0_cc(void)
{
    target_ulong src1;

    src1 = T0;
    T0 += T1 + FLAG_SET(PSR_CARRY);
    env->psr = 0;
    if (!T0)
	env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
	env->psr |= PSR_NEG;
    if (T0 < src1)
	env->psr |= PSR_CARRY;
    if (((src1 ^ T1 ^ -1) & (src1 ^ T0)) & (1 << 31))
	env->psr |= PSR_OVF;
    /* V9 xcc */
    FORCE_RET();
}

void OPPROTO op_sub_T1_T0(void)
{
    T0 -= T1;
}

void OPPROTO op_sub_T1_T0_cc(void)
{
    target_ulong src1;

    src1 = T0;
    T0 -= T1;
    env->psr = 0;
    if (!T0)
	env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
	env->psr |= PSR_NEG;
    if (src1 < T1)
	env->psr |= PSR_CARRY;
    if (((src1 ^ T1) & (src1 ^ T0)) & (1 << 31))
	env->psr |= PSR_OVF;
    /* V9 xcc */
    FORCE_RET();
}

void OPPROTO op_subx_T1_T0(void)
{
    T0 -= T1 + FLAG_SET(PSR_CARRY);
}

void OPPROTO op_subx_T1_T0_cc(void)
{
    target_ulong src1;

    src1 = T0;
    T0 -= T1 + FLAG_SET(PSR_CARRY);
    env->psr = 0;
    if (!T0)
	env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
	env->psr |= PSR_NEG;
    if (src1 < T1)
	env->psr |= PSR_CARRY;
    if (((src1 ^ T1) & (src1 ^ T0)) & (1 << 31))
	env->psr |= PSR_OVF;
    /* V9 xcc */
    FORCE_RET();
}

void OPPROTO op_and_T1_T0(void)
{
    T0 &= T1;
}

void OPPROTO op_or_T1_T0(void)
{
    T0 |= T1;
}

void OPPROTO op_xor_T1_T0(void)
{
    T0 ^= T1;
}

void OPPROTO op_andn_T1_T0(void)
{
    T0 &= ~T1;
}

void OPPROTO op_orn_T1_T0(void)
{
    T0 |= ~T1;
}

void OPPROTO op_xnor_T1_T0(void)
{
    T0 ^= ~T1;
}

void OPPROTO op_umul_T1_T0(void)
{
    uint64_t res;
    res = (uint64_t) T0 * (uint64_t) T1;
    T0 = res & 0xffffffff;
    env->y = res >> 32;
}

void OPPROTO op_smul_T1_T0(void)
{
    uint64_t res;
    res = (int64_t) ((int32_t) T0) * (int64_t) ((int32_t) T1);
    T0 = res & 0xffffffff;
    env->y = res >> 32;
}

void OPPROTO op_mulscc_T1_T0(void)
{
    unsigned int b1, N, V, b2;
    target_ulong src1;

    N = FLAG_SET(PSR_NEG);
    V = FLAG_SET(PSR_OVF);
    b1 = N ^ V;
    b2 = T0 & 1;
    T0 = (b1 << 31) | (T0 >> 1);
    if (!(env->y & 1))
        T1 = 0;
    /* do addition and update flags */
    src1 = T0;
    T0 += T1;
    env->psr = 0;
    if (!T0)
	env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
	env->psr |= PSR_NEG;
    if (T0 < src1)
	env->psr |= PSR_CARRY;
    if (((src1 ^ T1 ^ -1) & (src1 ^ T0)) & (1 << 31))
	env->psr |= PSR_OVF;
    env->y = (b2 << 31) | (env->y >> 1);
    FORCE_RET();
}

void OPPROTO op_udiv_T1_T0(void)
{
    uint64_t x0;
    uint32_t x1;

    x0 = T0 | ((uint64_t) (env->y) << 32);
    x1 = T1;
    x0 = x0 / x1;
    if (x0 > 0xffffffff) {
	T0 = 0xffffffff;
	T1 = 1;
    } else {
	T0 = x0;
	T1 = 0;
    }
    FORCE_RET();
}

void OPPROTO op_sdiv_T1_T0(void)
{
    int64_t x0;
    int32_t x1;

    x0 = T0 | ((int64_t) (env->y) << 32);
    x1 = T1;
    x0 = x0 / x1;
    if ((int32_t) x0 != x0) {
	T0 = x0 < 0? 0x80000000: 0x7fffffff;
	T1 = 1;
    } else {
	T0 = x0;
	T1 = 0;
    }
    FORCE_RET();
}

void OPPROTO op_div_cc(void)
{
    env->psr = 0;
    if (!T0)
	env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
	env->psr |= PSR_NEG;
    if (T1)
	env->psr |= PSR_OVF;
    /* V9 xcc */
    FORCE_RET();
}

void OPPROTO op_logic_T0_cc(void)
{
    env->psr = 0;
    if (!T0)
	env->psr |= PSR_ZERO;
    if ((int32_t) T0 < 0)
	env->psr |= PSR_NEG;
    /* V9 xcc */
    FORCE_RET();
}

void OPPROTO op_sll(void)
{
    T0 <<= T1;
}

void OPPROTO op_srl(void)
{
    T0 >>= T1;
}

void OPPROTO op_sra(void)
{
    T0 = ((int32_t) T0) >> T1;
}

/* Load and store */
#define MEMSUFFIX _raw
#include "op_mem.h"
#if !defined(CONFIG_USER_ONLY)
#define MEMSUFFIX _user
#include "op_mem.h"

#define MEMSUFFIX _kernel
#include "op_mem.h"
#endif

void OPPROTO op_ldfsr(void)
{
    env->fsr = *((uint32_t *) &FT0);
    helper_ldfsr();
}

void OPPROTO op_stfsr(void)
{
    *((uint32_t *) &FT0) = env->fsr;
}

void OPPROTO op_wry(void)
{
    env->y = T0;
}

void OPPROTO op_rdy(void)
{
    T0 = env->y;
}

void OPPROTO op_rdwim(void)
{
    T0 = env->wim;
}

void OPPROTO op_wrwim(void)
{
    env->wim = T0;
    FORCE_RET();
}

void OPPROTO op_rdpsr(void)
{
    do_rdpsr();
}

void OPPROTO op_wrpsr(void)
{
    do_wrpsr();
    FORCE_RET();
}

void OPPROTO op_rdtbr(void)
{
    T0 = env->tbr;
}

void OPPROTO op_wrtbr(void)
{
    env->tbr = T0;
    FORCE_RET();
}

void OPPROTO op_rett(void)
{
    helper_rett();
    FORCE_RET();
}

/* XXX: use another pointer for %iN registers to avoid slow wrapping
   handling ? */
void OPPROTO op_save(void)
{
    uint32_t cwp;
    cwp = (env->cwp - 1) & (NWINDOWS - 1); 
    if (env->wim & (1 << cwp)) {
        raise_exception(TT_WIN_OVF);
    }
    set_cwp(cwp);
    FORCE_RET();
}

void OPPROTO op_restore(void)
{
    uint32_t cwp;
    cwp = (env->cwp + 1) & (NWINDOWS - 1); 
    if (env->wim & (1 << cwp)) {
        raise_exception(TT_WIN_UNF);
    }
    set_cwp(cwp);
    FORCE_RET();
}

void OPPROTO op_exception(void)
{
    env->exception_index = PARAM1;
    cpu_loop_exit();
}

void OPPROTO op_trap_T0(void)
{
    env->exception_index = TT_TRAP + (T0 & 0x7f);
    cpu_loop_exit();
}

void OPPROTO op_trapcc_T0(void)
{
    if (T2) {
        env->exception_index = TT_TRAP + (T0 & 0x7f);
        cpu_loop_exit();
    }
    FORCE_RET();
}

void OPPROTO op_trap_ifnofpu(void)
{
    if (!env->psref) {
        env->exception_index = TT_NFPU_INSN;
        cpu_loop_exit();
    }
    FORCE_RET();
}

void OPPROTO op_fpexception_im(void)
{
    env->exception_index = TT_FP_EXCP;
    env->fsr &= ~FSR_FTT_MASK;
    env->fsr |= PARAM1;
    cpu_loop_exit();
    FORCE_RET();
}

void OPPROTO op_debug(void)
{
    helper_debug();
}

void OPPROTO op_exit_tb(void)
{
    EXIT_TB();
}

void OPPROTO op_eval_be(void)
{
    T2 = FLAG_SET(PSR_ZERO);
}

void OPPROTO op_eval_ble(void)
{
    target_ulong Z = FLAG_SET(PSR_ZERO), N = FLAG_SET(PSR_NEG), V = FLAG_SET(PSR_OVF);
    
    T2 = Z | (N ^ V);
}

void OPPROTO op_eval_bl(void)
{
    target_ulong N = FLAG_SET(PSR_NEG), V = FLAG_SET(PSR_OVF);

    T2 = N ^ V;
}

void OPPROTO op_eval_bleu(void)
{
    target_ulong Z = FLAG_SET(PSR_ZERO), C = FLAG_SET(PSR_CARRY);

    T2 = C | Z;
}

void OPPROTO op_eval_bcs(void)
{
    T2 = FLAG_SET(PSR_CARRY);
}

void OPPROTO op_eval_bvs(void)
{
    T2 = FLAG_SET(PSR_OVF);
}

void OPPROTO op_eval_bneg(void)
{
    T2 = FLAG_SET(PSR_NEG);
}

void OPPROTO op_eval_bne(void)
{
    T2 = !FLAG_SET(PSR_ZERO);
}

void OPPROTO op_eval_bg(void)
{
    target_ulong Z = FLAG_SET(PSR_ZERO), N = FLAG_SET(PSR_NEG), V = FLAG_SET(PSR_OVF);

    T2 = !(Z | (N ^ V));
}

void OPPROTO op_eval_bge(void)
{
    target_ulong N = FLAG_SET(PSR_NEG), V = FLAG_SET(PSR_OVF);

    T2 = !(N ^ V);
}

void OPPROTO op_eval_bgu(void)
{
    target_ulong Z = FLAG_SET(PSR_ZERO), C = FLAG_SET(PSR_CARRY);

    T2 = !(C | Z);
}

void OPPROTO op_eval_bcc(void)
{
    T2 = !FLAG_SET(PSR_CARRY);
}

void OPPROTO op_eval_bpos(void)
{
    T2 = !FLAG_SET(PSR_NEG);
}

void OPPROTO op_eval_bvc(void)
{
    T2 = !FLAG_SET(PSR_OVF);
}

/* FCC1:FCC0: 0 =, 1 <, 2 >, 3 u */

void OPPROTO op_eval_fbne(void)
{
// !0
    T2 = (env->fsr & (FSR_FCC1 | FSR_FCC0)); /* L or G or U */
}

void OPPROTO op_eval_fblg(void)
{
// 1 or 2
    T2 = FFLAG_SET(FSR_FCC0) ^ FFLAG_SET(FSR_FCC1);
}

void OPPROTO op_eval_fbul(void)
{
// 1 or 3
    T2 = FFLAG_SET(FSR_FCC0);
}

void OPPROTO op_eval_fbl(void)
{
// 1
    T2 = FFLAG_SET(FSR_FCC0) & !FFLAG_SET(FSR_FCC1);
}

void OPPROTO op_eval_fbug(void)
{
// 2 or 3
    T2 = FFLAG_SET(FSR_FCC1);
}

void OPPROTO op_eval_fbg(void)
{
// 2
    T2 = !FFLAG_SET(FSR_FCC0) & FFLAG_SET(FSR_FCC1);
}

void OPPROTO op_eval_fbu(void)
{
// 3
    T2 = FFLAG_SET(FSR_FCC0) & FFLAG_SET(FSR_FCC1);
}

void OPPROTO op_eval_fbe(void)
{
// 0
    T2 = !FFLAG_SET(FSR_FCC0) & !FFLAG_SET(FSR_FCC1);
}

void OPPROTO op_eval_fbue(void)
{
// 0 or 3
    T2 = !(FFLAG_SET(FSR_FCC1) ^ FFLAG_SET(FSR_FCC0));
}

void OPPROTO op_eval_fbge(void)
{
// 0 or 2
    T2 = !FFLAG_SET(FSR_FCC0);
}

void OPPROTO op_eval_fbuge(void)
{
// !1
    T2 = !(FFLAG_SET(FSR_FCC0) & !FFLAG_SET(FSR_FCC1));
}

void OPPROTO op_eval_fble(void)
{
// 0 or 1
    T2 = !FFLAG_SET(FSR_FCC1);
}

void OPPROTO op_eval_fbule(void)
{
// !2
    T2 = !(!FFLAG_SET(FSR_FCC0) & FFLAG_SET(FSR_FCC1));
}

void OPPROTO op_eval_fbo(void)
{
// !3
    T2 = !(FFLAG_SET(FSR_FCC0) & FFLAG_SET(FSR_FCC1));
}

void OPPROTO op_jmp_im(void)
{
    env->pc = PARAM1;
}

void OPPROTO op_movl_npc_im(void)
{
    env->npc = PARAM1;
}

void OPPROTO op_movl_npc_T0(void)
{
    env->npc = T0;
}

void OPPROTO op_next_insn(void)
{
    env->pc = env->npc;
    env->npc = env->npc + 4;
}

void OPPROTO op_branch(void)
{
    env->npc = PARAM3; /* XXX: optimize */
    JUMP_TB(op_branch, PARAM1, 0, PARAM2);
}

void OPPROTO op_branch2(void)
{
    if (T2) {
        env->npc = PARAM2 + 4; 
        JUMP_TB(op_branch2, PARAM1, 0, PARAM2);
    } else {
        env->npc = PARAM3 + 4; 
        JUMP_TB(op_branch2, PARAM1, 1, PARAM3);
    }
    FORCE_RET();
}

void OPPROTO op_branch_a(void)
{
    if (T2) {
	env->npc = PARAM2; /* XXX: optimize */
        JUMP_TB(op_branch_a, PARAM1, 0, PARAM3);
    } else {
	env->npc = PARAM3 + 8; /* XXX: optimize */
        JUMP_TB(op_branch_a, PARAM1, 1, PARAM3 + 4);
    }
    FORCE_RET();
}

void OPPROTO op_generic_branch(void)
{
    if (T2) {
	env->npc = PARAM1;
    } else {
	env->npc = PARAM2;
    }
    FORCE_RET();
}

void OPPROTO op_flush_T0(void)
{
    helper_flush(T0);
}

void OPPROTO op_fnegs(void)
{
    FT0 = -FT1;
}

void OPPROTO op_fabss(void)
{
    do_fabss();
}

void OPPROTO op_fsqrts(void)
{
    do_fsqrts();
}

void OPPROTO op_fsqrtd(void)
{
    do_fsqrtd();
}

void OPPROTO op_fmuls(void)
{
    FT0 *= FT1;
}

void OPPROTO op_fmuld(void)
{
    DT0 *= DT1;
}

void OPPROTO op_fsmuld(void)
{
    DT0 = FT0 * FT1;
}

void OPPROTO op_fadds(void)
{
    FT0 += FT1;
}

void OPPROTO op_faddd(void)
{
    DT0 += DT1;
}

void OPPROTO op_fsubs(void)
{
    FT0 -= FT1;
}

void OPPROTO op_fsubd(void)
{
    DT0 -= DT1;
}

void OPPROTO op_fdivs(void)
{
    FT0 /= FT1;
}

void OPPROTO op_fdivd(void)
{
    DT0 /= DT1;
}

void OPPROTO op_fcmps(void)
{
    do_fcmps();
}

void OPPROTO op_fcmpd(void)
{
    do_fcmpd();
}

#ifdef USE_INT_TO_FLOAT_HELPERS
void OPPROTO op_fitos(void)
{
    do_fitos();
}

void OPPROTO op_fitod(void)
{
    do_fitod();
}
#else
void OPPROTO op_fitos(void)
{
    FT0 = (float) *((int32_t *)&FT1);
}

void OPPROTO op_fitod(void)
{
    DT0 = (double) *((int32_t *)&FT1);
}
#endif

void OPPROTO op_fdtos(void)
{
    FT0 = (float) DT1;
}

void OPPROTO op_fstod(void)
{
    DT0 = (double) FT1;
}

void OPPROTO op_fstoi(void)
{
    *((int32_t *)&FT0) = (int32_t) FT1;
}

void OPPROTO op_fdtoi(void)
{
    *((int32_t *)&FT0) = (int32_t) DT1;
}

void OPPROTO op_ld_asi()
{
    helper_ld_asi(PARAM1, PARAM2, PARAM3);
}

void OPPROTO op_st_asi()
{
    helper_st_asi(PARAM1, PARAM2, PARAM3);
}

