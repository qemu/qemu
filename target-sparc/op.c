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

void OPPROTO op_add_T1_T0 (void)
{
	T0 += T1;
}

void OPPROTO op_and_T1_T0 (void)
{
	T0 &= T1;
}

void OPPROTO op_or_T1_T0 (void)
{
	T0 |= T1;
}

void OPPROTO op_xor_T1_T0 (void)
{
	T0 ^= T1;
}

void OPPROTO op_sub_T1_T0 (void)
{
	T0 -= T1;
}

void OPPROTO op_andn_T1_T0 (void)
{
	T0 &= ~T1;
}

void OPPROTO op_orn_T1_T0 (void)
{
	T0 |= ~T1;
}

void OPPROTO op_xnor_T1_T0 (void)
{
	T0 ^= ~T1;
}

void OPPROTO op_addx_T1_T0 (void)
{
	T0 += T1+((env->psr & PSR_CARRY)?1:0);
}

void OPPROTO op_umul_T1_T0 (void)
{
	unsigned long long res = T0*T1;
	T0 = res & 0xffffffff;
	env->y = res >> 32;
}

void OPPROTO op_smul_T1_T0 (void)
{
	long long res = T0*T1;
	T0 = res & 0xffffffff;
	env->y = res >> 32;
}

void OPPROTO op_udiv_T1_T0 (void)
{
	unsigned long long x0 = T0 * env->y;
	unsigned int x1 = T1;
	T0 = x0 / x1;
}

void OPPROTO op_sdiv_T1_T0 (void)
{
	long long x0 = T0 * env->y;
	int x1 = T1;
	T0 = x0 / x1;
}

void OPPROTO op_subx_T1_T0 (void)
{
	T0 -= T1+((env->psr & PSR_CARRY)?1:0);
}

void OPPROTO op_set_flags (void)
{
	env->psr = 0;
	if (!T0) env->psr |= PSR_ZERO;
	if ((unsigned int) T0 < (unsigned int) T1) env->psr |= PSR_CARRY;
	if ((int) T0 < (int) T1) env->psr |= PSR_OVF;
	if ((int) T0 < 0) env->psr |= PSR_NEG;
}

void OPPROTO op_sll (void)
{
	T0 <<= T1;
}

void OPPROTO op_srl (void)
{
	T0 >>= T1;
}

void OPPROTO op_sra (void)
{
	int x = T0 >> T1;
	T0 = x;
}

void OPPROTO op_st (void)
{
	stl ((void *) T0, T1);
}

void OPPROTO op_stb (void)
{
	stb ((void *) T0, T1);
}

void OPPROTO op_sth (void)
{
	stw ((void *) T0, T1);
}

void OPPROTO op_ld (void)
{
	T1 = ldl ((void *) T0);
}

void OPPROTO op_ldub (void)
{
	T1 = ldub ((void *) T0);
}

void OPPROTO op_lduh (void)
{
	T1 = lduw ((void *) T0);
}

void OPPROTO op_ldsb (void)
{
	T1 = ldsb ((void *) T0);
}

void OPPROTO op_ldsh (void)
{
	T1 = ldsw ((void *) T0);
}

void OPPROTO op_ldstub (void)
{
	T1 = ldub ((void *) T0);
	stb ((void *) T0, 0xff); /* XXX: Should be Atomically */
}

void OPPROTO op_swap (void)
{
	unsigned int tmp = ldl ((void *) T0);
	stl ((void *) T0, T1);   /* XXX: Should be Atomically */
	T1 = tmp;
}

void OPPROTO op_ldd (void)
{
	T1 = ldl ((void *) T0);
	T0 = ldl ((void *) T0+4);
}

void OPPROTO op_wry (void)
{
	env->y = T0^T1;
}

void OPPROTO op_rdy (void)
{
	T0 = env->y;
}

#define regwptr (env->regwptr)

void OPPROTO op_save (void)
{
	regwptr -= 16;
}

void OPPROTO op_restore (void)
{
	regwptr += 16;
}

void OPPROTO op_trap (void)
{
	env->exception_index = PARAM1;
	cpu_loop_exit ();
}

void OPPROTO op_exit_tb (void)
{
	EXIT_TB ();
}

void OPPROTO op_eval_be (void)
{
	T0 = (env->psr & PSR_ZERO);
}

#define FLAG_SET(x) (env->psr&x)?1:0
#define GET_FLAGS unsigned int Z = FLAG_SET(PSR_ZERO), N = FLAG_SET(PSR_NEG), V = FLAG_SET(PSR_OVF), C = FLAG_SET(PSR_CARRY)

void OPPROTO op_eval_ble (void)
{
	GET_FLAGS;
	T0 = Z | (N^V);
}

void OPPROTO op_eval_bl (void)
{
	GET_FLAGS;
	T0 = N^V;
}

void OPPROTO op_eval_bleu (void)
{
	GET_FLAGS;
	T0 = C|Z;
}

void OPPROTO op_eval_bcs (void)
{
	T0 = (env->psr & PSR_CARRY);
}

void OPPROTO op_eval_bvs (void)
{
	T0 = (env->psr & PSR_OVF);
}

void OPPROTO op_eval_bneg (void)
{
	T0 = (env->psr & PSR_NEG);
}

void OPPROTO op_eval_bne (void)
{
	T0 = !(env->psr & PSR_ZERO);
}

void OPPROTO op_eval_bg (void)
{
	GET_FLAGS;
	T0 = !(Z | (N^V));
}

/*XXX: This seems to be documented wrong in the SPARC V8 Manual
  The manual states: !(N^V)
  but I assume Z | !(N^V) to be correct */
void OPPROTO op_eval_bge (void)
{
	GET_FLAGS;
	T0 = Z | !(N^V);
}

void OPPROTO op_eval_bgu (void)
{
	GET_FLAGS;
	T0 = !(C | Z);
}

void OPPROTO op_eval_bcc (void)
{
	T0 = !(env->psr & PSR_CARRY);
}

void OPPROTO op_eval_bpos (void)
{
	T0 = !(env->psr & PSR_NEG);
}

void OPPROTO op_eval_bvc (void)
{
	T0 = !(env->psr & PSR_OVF);
}

void OPPROTO op_jmp_im (void)
{
	env->pc = PARAM1;
}

void OPPROTO op_call (void)
{
	regwptr[7] = PARAM1-4;
	env->pc = PARAM1+PARAM2;
}

void OPPROTO op_jmpl (void)
{
	env->npc = T0;
}

void OPPROTO op_generic_jmp_1 (void)
{
	T1 = PARAM1;
	env->pc = PARAM1+PARAM2;
}

void OPPROTO op_generic_jmp_2 (void)
{
	T1 = PARAM1;
	env->pc = env->npc;
}

unsigned long old_T0;

void OPPROTO op_save_T0 (void)
{
	old_T0 = T0;
}

void OPPROTO op_restore_T0 (void)
{
	T0 = old_T0;
}

void OPPROTO op_generic_branch (void)
{
	if (T0)
		JUMP_TB (op_generic_branch, PARAM1, 0, PARAM2);
	else
		JUMP_TB (op_generic_branch, PARAM1, 1, PARAM3);
	FORCE_RET ();
}

void OPPROTO op_generic_branch_a (void)
{
	if (T0)
		env->npc = PARAM3;
	else
		JUMP_TB (op_generic_branch_a, PARAM1, 0, PARAM2);
	FORCE_RET ();
}
