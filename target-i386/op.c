/*
 *  i386 micro operations
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

#define ASM_SOFTMMU
#include "exec.h"

/* we define the various pieces of code used by the JIT */

#define REG EAX
#define REGNAME _EAX
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG ECX
#define REGNAME _ECX
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG EDX
#define REGNAME _EDX
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG EBX
#define REGNAME _EBX
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG ESP
#define REGNAME _ESP
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG EBP
#define REGNAME _EBP
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG ESI
#define REGNAME _ESI
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG EDI
#define REGNAME _EDI
#include "opreg_template.h"
#undef REG
#undef REGNAME

#ifdef TARGET_X86_64

#define REG (env->regs[8])
#define REGNAME _R8
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[9])
#define REGNAME _R9
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[10])
#define REGNAME _R10
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[11])
#define REGNAME _R11
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[12])
#define REGNAME _R12
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[13])
#define REGNAME _R13
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[14])
#define REGNAME _R14
#include "opreg_template.h"
#undef REG
#undef REGNAME

#define REG (env->regs[15])
#define REGNAME _R15
#include "opreg_template.h"
#undef REG
#undef REGNAME

#endif

/* multiple size ops */

#define ldul ldl

#define SHIFT 0
#include "ops_template.h"
#undef SHIFT

#define SHIFT 1
#include "ops_template.h"
#undef SHIFT

#define SHIFT 2
#include "ops_template.h"
#undef SHIFT

#ifdef TARGET_X86_64

#define SHIFT 3
#include "ops_template.h"
#undef SHIFT

#endif

void OPPROTO op_movl_T0_env(void)
{
    T0 = *(uint32_t *)((char *)env + PARAM1);
}

void OPPROTO op_movl_env_T0(void)
{
    *(uint32_t *)((char *)env + PARAM1) = T0;
}

void OPPROTO op_movl_env_T1(void)
{
    *(uint32_t *)((char *)env + PARAM1) = T1;
}

void OPPROTO op_movtl_T0_env(void)
{
    T0 = *(target_ulong *)((char *)env + PARAM1);
}

void OPPROTO op_movtl_env_T0(void)
{
    *(target_ulong *)((char *)env + PARAM1) = T0;
}

void OPPROTO op_movtl_T1_env(void)
{
    T1 = *(target_ulong *)((char *)env + PARAM1);
}

void OPPROTO op_movtl_env_T1(void)
{
    *(target_ulong *)((char *)env + PARAM1) = T1;
}

/* flags handling */

void OPPROTO op_jmp_label(void)
{
    GOTO_LABEL_PARAM(1);
}

void OPPROTO op_jnz_T0_label(void)
{
    if (T0)
        GOTO_LABEL_PARAM(1);
    FORCE_RET();
}

/* slow set cases (compute x86 flags) */
void OPPROTO op_seto_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (eflags >> 11) & 1;
}

void OPPROTO op_setb_T0_cc(void)
{
    T0 = cc_table[CC_OP].compute_c();
}

void OPPROTO op_setz_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (eflags >> 6) & 1;
}

void OPPROTO op_setbe_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (eflags & (CC_Z | CC_C)) != 0;
}

void OPPROTO op_sets_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (eflags >> 7) & 1;
}

void OPPROTO op_setp_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (eflags >> 2) & 1;
}

void OPPROTO op_setl_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = ((eflags ^ (eflags >> 4)) >> 7) & 1;
}

void OPPROTO op_setle_T0_cc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    T0 = (((eflags ^ (eflags >> 4)) & 0x80) || (eflags & CC_Z)) != 0;
}

void OPPROTO op_xor_T0_1(void)
{
    T0 ^= 1;
}

/* XXX: clear VIF/VIP in all ops ? */

void OPPROTO op_movl_eflags_T0(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK));
}

void OPPROTO op_movw_eflags_T0(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK) & 0xffff);
}

void OPPROTO op_movl_eflags_T0_io(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK | IF_MASK));
}

void OPPROTO op_movw_eflags_T0_io(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK | IF_MASK) & 0xffff);
}

void OPPROTO op_movl_eflags_T0_cpl0(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK | IF_MASK | IOPL_MASK));
}

void OPPROTO op_movw_eflags_T0_cpl0(void)
{
    load_eflags(T0, (TF_MASK | AC_MASK | ID_MASK | NT_MASK | IF_MASK | IOPL_MASK) & 0xffff);
}

#if 0
/* vm86plus version */
void OPPROTO op_movw_eflags_T0_vm(void)
{
    int eflags;
    eflags = T0;
    CC_SRC = eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    DF = 1 - (2 * ((eflags >> 10) & 1));
    /* we also update some system flags as in user mode */
    env->eflags = (env->eflags & ~(FL_UPDATE_MASK16 | VIF_MASK)) |
        (eflags & FL_UPDATE_MASK16);
    if (eflags & IF_MASK) {
        env->eflags |= VIF_MASK;
        if (env->eflags & VIP_MASK) {
            EIP = PARAM1;
            raise_exception(EXCP0D_GPF);
        }
    }
    FORCE_RET();
}

void OPPROTO op_movl_eflags_T0_vm(void)
{
    int eflags;
    eflags = T0;
    CC_SRC = eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    DF = 1 - (2 * ((eflags >> 10) & 1));
    /* we also update some system flags as in user mode */
    env->eflags = (env->eflags & ~(FL_UPDATE_MASK32 | VIF_MASK)) |
        (eflags & FL_UPDATE_MASK32);
    if (eflags & IF_MASK) {
        env->eflags |= VIF_MASK;
        if (env->eflags & VIP_MASK) {
            EIP = PARAM1;
            raise_exception(EXCP0D_GPF);
        }
    }
    FORCE_RET();
}
#endif

/* XXX: compute only O flag */
void OPPROTO op_movb_eflags_T0(void)
{
    int of;
    of = cc_table[CC_OP].compute_all() & CC_O;
    CC_SRC = (T0 & (CC_S | CC_Z | CC_A | CC_P | CC_C)) | of;
}

void OPPROTO op_movl_T0_eflags(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags |= (DF & DF_MASK);
    eflags |= env->eflags & ~(VM_MASK | RF_MASK);
    T0 = eflags;
}

/* vm86plus version */
#if 0
void OPPROTO op_movl_T0_eflags_vm(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags |= (DF & DF_MASK);
    eflags |= env->eflags & ~(VM_MASK | RF_MASK | IF_MASK);
    if (env->eflags & VIF_MASK)
        eflags |= IF_MASK;
    T0 = eflags;
}
#endif

void OPPROTO op_clc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags &= ~CC_C;
    CC_SRC = eflags;
}

void OPPROTO op_stc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags |= CC_C;
    CC_SRC = eflags;
}

void OPPROTO op_cmc(void)
{
    int eflags;
    eflags = cc_table[CC_OP].compute_all();
    eflags ^= CC_C;
    CC_SRC = eflags;
}

void OPPROTO op_salc(void)
{
    int cf;
    cf = cc_table[CC_OP].compute_c();
    EAX = (EAX & ~0xff) | ((-cf) & 0xff);
}
