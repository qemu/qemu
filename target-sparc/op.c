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
#define EIP (env->pc)

#define FLAG_SET(x) (env->psr&x)?1:0

void OPPROTO op_movl_T0_0(void)
{
    T0 = 0;
}

void OPPROTO op_movl_T0_1(void)
{
    T0 = 1;
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

void OPPROTO op_addl_T1_im(void)
{
    T1 += PARAM1;
}

void OPPROTO op_addl_T1_T2(void)
{
    T1 += T2;
}

void OPPROTO op_subl_T1_T2(void)
{
    T1 -= T2;
}

void OPPROTO op_add_T1_T0(void)
{
    T0 += T1;
}

void OPPROTO op_add_T1_T0_cc(void)
{
    unsigned int src1;
    src1 = T0;
    T0 += T1;
    env->psr = 0;
    if (!T0)
	env->psr |= PSR_ZERO;
    if ((int) T0 < 0)
	env->psr |= PSR_NEG;
    if (T0 < src1)
	env->psr |= PSR_CARRY;
    if (((src1 ^ T1 ^ -1) & (src1 ^ T0)) & (1 << 31))
	env->psr |= PSR_OVF;
    FORCE_RET();
}

void OPPROTO op_sub_T1_T0(void)
{
    T0 -= T1;
}

void OPPROTO op_sub_T1_T0_cc(void)
{
    unsigned int src1;

    src1 = T0;
    T0 -= T1;
    env->psr = 0;
    if (!T0)
	env->psr |= PSR_ZERO;
    if ((int) T0 < 0)
	env->psr |= PSR_NEG;
    if (src1 < T1)
	env->psr |= PSR_CARRY;
    if (((src1 ^ T1) & (src1 ^ T0)) & (1 << 31))
	env->psr |= PSR_OVF;
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

void OPPROTO op_addx_T1_T0(void)
{
    T0 += T1 + ((env->psr & PSR_CARRY) ? 1 : 0);
}

void OPPROTO op_umul_T1_T0(void)
{
    uint64_t res;
    res = (uint64_t) T0 *(uint64_t) T1;
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
    unsigned int b1, C, V, b2, src1;
    C = FLAG_SET(PSR_CARRY);
    V = FLAG_SET(PSR_OVF);
    b1 = C ^ V;
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
    if ((int) T0 < 0)
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

    x0 = T0 | ((uint64_t) (env->y) << 32);
    x1 = T1;
    x0 = x0 / x1;
    if ((int32_t) x0 != x0) {
	T0 = x0 >> 63;
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
    if ((int) T0 < 0)
	env->psr |= PSR_NEG;
    if (T1)
	env->psr |= PSR_OVF;
    FORCE_RET();
}

void OPPROTO op_subx_T1_T0(void)
{
    T0 -= T1 + ((env->psr & PSR_CARRY) ? 1 : 0);
}

void OPPROTO op_logic_T0_cc(void)
{
    env->psr = 0;
    if (!T0)
	env->psr |= PSR_ZERO;
    if ((int) T0 < 0)
	env->psr |= PSR_NEG;
    FORCE_RET();
}

void OPPROTO op_set_flags(void)
{
    env->psr = 0;
    if (!T0)
	env->psr |= PSR_ZERO;
    if ((unsigned int) T0 < (unsigned int) T1)
	env->psr |= PSR_CARRY;
    if ((int) T0 < (int) T1)
	env->psr |= PSR_OVF;
    if ((int) T0 < 0)
	env->psr |= PSR_NEG;
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

void OPPROTO op_st(void)
{
    stl((void *) T0, T1);
}

void OPPROTO op_stb(void)
{
    stb((void *) T0, T1);
}

void OPPROTO op_sth(void)
{
    stw((void *) T0, T1);
}

void OPPROTO op_std(void)
{
    stl((void *) T0, T1);
    stl((void *) (T0 + 4), T2);
}

void OPPROTO op_ld(void)
{
    T1 = ldl((void *) T0);
}

void OPPROTO op_ldub(void)
{
    T1 = ldub((void *) T0);
}

void OPPROTO op_lduh(void)
{
    T1 = lduw((void *) T0);
}

void OPPROTO op_ldsb(void)
{
    T1 = ldsb((void *) T0);
}

void OPPROTO op_ldsh(void)
{
    T1 = ldsw((void *) T0);
}

void OPPROTO op_ldstub(void)
{
    T1 = ldub((void *) T0);
    stb((void *) T0, 0xff);	/* XXX: Should be Atomically */
}

void OPPROTO op_swap(void)
{
    unsigned int tmp = ldl((void *) T0);
    stl((void *) T0, T1);	/* XXX: Should be Atomically */
    T1 = tmp;
}

void OPPROTO op_ldd(void)
{
    T1 = ldl((void *) T0);
    T0 = ldl((void *) (T0 + 4));
}

void OPPROTO op_wry(void)
{
    env->y = T0;
}

void OPPROTO op_rdy(void)
{
    T0 = env->y;
}

void raise_exception(int tt)
{
    env->exception_index = tt;
    cpu_loop_exit();
}   

void memcpy32(uint32_t *dst, const uint32_t *src)
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    dst[4] = src[4];
    dst[5] = src[5];
    dst[6] = src[6];
    dst[7] = src[7];
}

static inline void set_cwp(int new_cwp)
{
    /* put the modified wrap registers at their proper location */
    if (env->cwp == (NWINDOWS - 1))
        memcpy32(env->regbase, env->regbase + NWINDOWS * 16);
    env->cwp = new_cwp;
    /* put the wrap registers at their temporary location */
    if (new_cwp == (NWINDOWS - 1))
        memcpy32(env->regbase + NWINDOWS * 16, env->regbase);
    env->regwptr = env->regbase + (new_cwp * 16);
}

/* XXX: use another pointer for %iN registers to avoid slow wrapping
   handling ? */
void OPPROTO op_save(void)
{
    int cwp;
    cwp = (env->cwp - 1) & (NWINDOWS - 1); 
    if (env->wim & (1 << cwp)) {
        raise_exception(TT_WIN_OVF);
    }
    set_cwp(cwp);
    FORCE_RET();
}

void OPPROTO op_restore(void)
{
    int cwp;
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

void OPPROTO op_exit_tb(void)
{
    EXIT_TB();
}

void OPPROTO op_eval_be(void)
{
    T2 = (env->psr & PSR_ZERO);
}

void OPPROTO op_eval_ble(void)
{
    unsigned int Z = FLAG_SET(PSR_ZERO), N = FLAG_SET(PSR_NEG), V = FLAG_SET(PSR_OVF);
    
    T2 = Z | (N ^ V);
}

void OPPROTO op_eval_bl(void)
{
    unsigned int N = FLAG_SET(PSR_NEG), V = FLAG_SET(PSR_OVF);

    T2 = N ^ V;
}

void OPPROTO op_eval_bleu(void)
{
    unsigned int Z = FLAG_SET(PSR_ZERO), C = FLAG_SET(PSR_CARRY);

    T2 = C | Z;
}

void OPPROTO op_eval_bcs(void)
{
    T2 = (env->psr & PSR_CARRY);
}

void OPPROTO op_eval_bvs(void)
{
    T2 = (env->psr & PSR_OVF);
}

void OPPROTO op_eval_bneg(void)
{
    T2 = (env->psr & PSR_NEG);
}

void OPPROTO op_eval_bne(void)
{
    T2 = !(env->psr & PSR_ZERO);
}

void OPPROTO op_eval_bg(void)
{
    unsigned int Z = FLAG_SET(PSR_ZERO), N = FLAG_SET(PSR_NEG), V = FLAG_SET(PSR_OVF);

    T2 = !(Z | (N ^ V));
}

void OPPROTO op_eval_bge(void)
{
    unsigned int N = FLAG_SET(PSR_NEG), V = FLAG_SET(PSR_OVF);

    T2 = !(N ^ V);
}

void OPPROTO op_eval_bgu(void)
{
    unsigned int Z = FLAG_SET(PSR_ZERO), C = FLAG_SET(PSR_CARRY);

    T2 = !(C | Z);
}

void OPPROTO op_eval_bcc(void)
{
    T2 = !(env->psr & PSR_CARRY);
}

void OPPROTO op_eval_bpos(void)
{
    T2 = !(env->psr & PSR_NEG);
}

void OPPROTO op_eval_bvc(void)
{
    T2 = !(env->psr & PSR_OVF);
}

void OPPROTO op_movl_T2_0(void)
{
    T2 = 0;
}

void OPPROTO op_movl_T2_1(void)
{
    T2 = 1;
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
        JUMP_TB(op_generic_branch_a, PARAM1, 0, PARAM3);
    } else {
	env->npc = PARAM3 + 8; /* XXX: optimize */
        JUMP_TB(op_generic_branch_a, PARAM1, 1, PARAM3 + 4);
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

