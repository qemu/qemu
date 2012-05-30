/*
 *  PowerPC exception emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "cpu.h"
#include "dyngen-exec.h"
#include "helper.h"

#include "helper_regs.h"

//#define DEBUG_OP
//#define DEBUG_EXCEPTIONS

/*****************************************************************************/
/* Exceptions processing helpers */

void helper_raise_exception_err(uint32_t exception, uint32_t error_code)
{
#if 0
    printf("Raise exception %3x code : %d\n", exception, error_code);
#endif
    env->exception_index = exception;
    env->error_code = error_code;
    cpu_loop_exit(env);
}

void helper_raise_exception(uint32_t exception)
{
    helper_raise_exception_err(exception, 0);
}

#if !defined(CONFIG_USER_ONLY)
void helper_store_msr(target_ulong val)
{
    val = hreg_store_msr(env, val, 0);
    if (val != 0) {
        env->interrupt_request |= CPU_INTERRUPT_EXITTB;
        helper_raise_exception(val);
    }
}

static inline void do_rfi(target_ulong nip, target_ulong msr,
                          target_ulong msrm, int keep_msrh)
{
#if defined(TARGET_PPC64)
    if (msr & (1ULL << MSR_SF)) {
        nip = (uint64_t)nip;
        msr &= (uint64_t)msrm;
    } else {
        nip = (uint32_t)nip;
        msr = (uint32_t)(msr & msrm);
        if (keep_msrh) {
            msr |= env->msr & ~((uint64_t)0xFFFFFFFF);
        }
    }
#else
    nip = (uint32_t)nip;
    msr &= (uint32_t)msrm;
#endif
    /* XXX: beware: this is false if VLE is supported */
    env->nip = nip & ~((target_ulong)0x00000003);
    hreg_store_msr(env, msr, 1);
#if defined(DEBUG_OP)
    cpu_dump_rfi(env->nip, env->msr);
#endif
    /* No need to raise an exception here,
     * as rfi is always the last insn of a TB
     */
    env->interrupt_request |= CPU_INTERRUPT_EXITTB;
}

void helper_rfi(void)
{
    do_rfi(env->spr[SPR_SRR0], env->spr[SPR_SRR1],
           ~((target_ulong)0x783F0000), 1);
}

#if defined(TARGET_PPC64)
void helper_rfid(void)
{
    do_rfi(env->spr[SPR_SRR0], env->spr[SPR_SRR1],
           ~((target_ulong)0x783F0000), 0);
}

void helper_hrfid(void)
{
    do_rfi(env->spr[SPR_HSRR0], env->spr[SPR_HSRR1],
           ~((target_ulong)0x783F0000), 0);
}
#endif

/*****************************************************************************/
/* Embedded PowerPC specific helpers */
void helper_40x_rfci(void)
{
    do_rfi(env->spr[SPR_40x_SRR2], env->spr[SPR_40x_SRR3],
           ~((target_ulong)0xFFFF0000), 0);
}

void helper_rfci(void)
{
    do_rfi(env->spr[SPR_BOOKE_CSRR0], SPR_BOOKE_CSRR1,
           ~((target_ulong)0x3FFF0000), 0);
}

void helper_rfdi(void)
{
    do_rfi(env->spr[SPR_BOOKE_DSRR0], SPR_BOOKE_DSRR1,
           ~((target_ulong)0x3FFF0000), 0);
}

void helper_rfmci(void)
{
    do_rfi(env->spr[SPR_BOOKE_MCSRR0], SPR_BOOKE_MCSRR1,
           ~((target_ulong)0x3FFF0000), 0);
}
#endif

void helper_tw(target_ulong arg1, target_ulong arg2, uint32_t flags)
{
    if (!likely(!(((int32_t)arg1 < (int32_t)arg2 && (flags & 0x10)) ||
                  ((int32_t)arg1 > (int32_t)arg2 && (flags & 0x08)) ||
                  ((int32_t)arg1 == (int32_t)arg2 && (flags & 0x04)) ||
                  ((uint32_t)arg1 < (uint32_t)arg2 && (flags & 0x02)) ||
                  ((uint32_t)arg1 > (uint32_t)arg2 && (flags & 0x01))))) {
        helper_raise_exception_err(POWERPC_EXCP_PROGRAM, POWERPC_EXCP_TRAP);
    }
}

#if defined(TARGET_PPC64)
void helper_td(target_ulong arg1, target_ulong arg2, uint32_t flags)
{
    if (!likely(!(((int64_t)arg1 < (int64_t)arg2 && (flags & 0x10)) ||
                  ((int64_t)arg1 > (int64_t)arg2 && (flags & 0x08)) ||
                  ((int64_t)arg1 == (int64_t)arg2 && (flags & 0x04)) ||
                  ((uint64_t)arg1 < (uint64_t)arg2 && (flags & 0x02)) ||
                  ((uint64_t)arg1 > (uint64_t)arg2 && (flags & 0x01))))) {
        helper_raise_exception_err(POWERPC_EXCP_PROGRAM, POWERPC_EXCP_TRAP);
    }
}
#endif

#if !defined(CONFIG_USER_ONLY)
/*****************************************************************************/
/* PowerPC 601 specific instructions (POWER bridge) */

void helper_rfsvc(void)
{
    do_rfi(env->lr, env->ctr, 0x0000FFFF, 0);
}

/* Embedded.Processor Control */
static int dbell2irq(target_ulong rb)
{
    int msg = rb & DBELL_TYPE_MASK;
    int irq = -1;

    switch (msg) {
    case DBELL_TYPE_DBELL:
        irq = PPC_INTERRUPT_DOORBELL;
        break;
    case DBELL_TYPE_DBELL_CRIT:
        irq = PPC_INTERRUPT_CDOORBELL;
        break;
    case DBELL_TYPE_G_DBELL:
    case DBELL_TYPE_G_DBELL_CRIT:
    case DBELL_TYPE_G_DBELL_MC:
        /* XXX implement */
    default:
        break;
    }

    return irq;
}

void helper_msgclr(target_ulong rb)
{
    int irq = dbell2irq(rb);

    if (irq < 0) {
        return;
    }

    env->pending_interrupts &= ~(1 << irq);
}

void helper_msgsnd(target_ulong rb)
{
    int irq = dbell2irq(rb);
    int pir = rb & DBELL_PIRTAG_MASK;
    CPUPPCState *cenv;

    if (irq < 0) {
        return;
    }

    for (cenv = first_cpu; cenv != NULL; cenv = cenv->next_cpu) {
        if ((rb & DBELL_BRDCAST) || (cenv->spr[SPR_BOOKE_PIR] == pir)) {
            cenv->pending_interrupts |= 1 << irq;
            cpu_interrupt(cenv, CPU_INTERRUPT_HARD);
        }
    }
}
#endif
