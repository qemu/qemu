/*
 *  HPPA interrupt helper routines
 *
 *  Copyright (c) 2017 Richard Henderson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "hw/core/cpu.h"

#ifndef CONFIG_USER_ONLY
static void eval_interrupt(HPPACPU *cpu)
{
    CPUState *cs = CPU(cpu);
    if (cpu->env.cr[CR_EIRR] & cpu->env.cr[CR_EIEM]) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

/* Each CPU has a word mapped into the GSC bus.  Anything on the GSC bus
 * can write to this word to raise an external interrupt on the target CPU.
 * This includes the system controler (DINO) for regular devices, or
 * another CPU for SMP interprocessor interrupts.
 */
static uint64_t io_eir_read(void *opaque, hwaddr addr, unsigned size)
{
    HPPACPU *cpu = opaque;

    /* ??? What does a read of this register over the GSC bus do?  */
    return cpu->env.cr[CR_EIRR];
}

static void io_eir_write(void *opaque, hwaddr addr,
                         uint64_t data, unsigned size)
{
    HPPACPU *cpu = opaque;
    int le_bit = ~data & (TARGET_REGISTER_BITS - 1);

    cpu->env.cr[CR_EIRR] |= (target_ureg)1 << le_bit;
    eval_interrupt(cpu);
}

const MemoryRegionOps hppa_io_eir_ops = {
    .read = io_eir_read,
    .write = io_eir_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

void hppa_cpu_alarm_timer(void *opaque)
{
    /* Raise interrupt 0.  */
    io_eir_write(opaque, 0, 0, 4);
}

void HELPER(write_eirr)(CPUHPPAState *env, target_ureg val)
{
    env->cr[CR_EIRR] &= ~val;
    qemu_mutex_lock_iothread();
    eval_interrupt(env_archcpu(env));
    qemu_mutex_unlock_iothread();
}

void HELPER(write_eiem)(CPUHPPAState *env, target_ureg val)
{
    env->cr[CR_EIEM] = val;
    qemu_mutex_lock_iothread();
    eval_interrupt(env_archcpu(env));
    qemu_mutex_unlock_iothread();
}
#endif /* !CONFIG_USER_ONLY */

void hppa_cpu_do_interrupt(CPUState *cs)
{
    HPPACPU *cpu = HPPA_CPU(cs);
    CPUHPPAState *env = &cpu->env;
    int i = cs->exception_index;
    target_ureg iaoq_f = env->iaoq_f;
    target_ureg iaoq_b = env->iaoq_b;
    uint64_t iasq_f = env->iasq_f;
    uint64_t iasq_b = env->iasq_b;

#ifndef CONFIG_USER_ONLY
    target_ureg old_psw;

    /* As documented in pa2.0 -- interruption handling.  */
    /* step 1 */
    env->cr[CR_IPSW] = old_psw = cpu_hppa_get_psw(env);

    /* step 2 -- note PSW_W == 0 for !HPPA64.  */
    cpu_hppa_put_psw(env, PSW_W | (i == EXCP_HPMC ? PSW_M : 0));

    /* step 3 */
    env->cr[CR_IIASQ] = iasq_f >> 32;
    env->cr_back[0] = iasq_b >> 32;
    env->cr[CR_IIAOQ] = iaoq_f;
    env->cr_back[1] = iaoq_b;

    if (old_psw & PSW_Q) {
        /* step 5 */
        /* ISR and IOR will be set elsewhere.  */
        switch (i) {
        case EXCP_ILL:
        case EXCP_BREAK:
        case EXCP_PRIV_REG:
        case EXCP_PRIV_OPR:
            /* IIR set via translate.c.  */
            break;

        case EXCP_OVERFLOW:
        case EXCP_COND:
        case EXCP_ASSIST:
        case EXCP_DTLB_MISS:
        case EXCP_NA_ITLB_MISS:
        case EXCP_NA_DTLB_MISS:
        case EXCP_DMAR:
        case EXCP_DMPI:
        case EXCP_UNALIGN:
        case EXCP_DMP:
        case EXCP_DMB:
        case EXCP_TLB_DIRTY:
        case EXCP_PAGE_REF:
        case EXCP_ASSIST_EMU:
            {
                /* Avoid reading directly from the virtual address, lest we
                   raise another exception from some sort of TLB issue.  */
                /* ??? An alternate fool-proof method would be to store the
                   instruction data into the unwind info.  That's probably
                   a bit too much in the way of extra storage required.  */
                vaddr vaddr;
                hwaddr paddr;

                paddr = vaddr = iaoq_f & -4;
                if (old_psw & PSW_C) {
                    int prot, t;

                    vaddr = hppa_form_gva_psw(old_psw, iasq_f, vaddr);
                    t = hppa_get_physical_address(env, vaddr, MMU_KERNEL_IDX,
                                                  0, &paddr, &prot);
                    if (t >= 0) {
                        /* We can't re-load the instruction.  */
                        env->cr[CR_IIR] = 0;
                        break;
                    }
                }
                env->cr[CR_IIR] = ldl_phys(cs->as, paddr);
            }
            break;

        default:
            /* Other exceptions do not set IIR.  */
            break;
        }

        /* step 6 */
        env->shadow[0] = env->gr[1];
        env->shadow[1] = env->gr[8];
        env->shadow[2] = env->gr[9];
        env->shadow[3] = env->gr[16];
        env->shadow[4] = env->gr[17];
        env->shadow[5] = env->gr[24];
        env->shadow[6] = env->gr[25];
    }

    /* step 7 */
    env->iaoq_f = env->cr[CR_IVA] + 32 * i;
    env->iaoq_b = env->iaoq_f + 4;
    env->iasq_f = 0;
    env->iasq_b = 0;
#endif

    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        static const char * const names[] = {
            [EXCP_HPMC]          = "high priority machine check",
            [EXCP_POWER_FAIL]    = "power fail interrupt",
            [EXCP_RC]            = "recovery counter trap",
            [EXCP_EXT_INTERRUPT] = "external interrupt",
            [EXCP_LPMC]          = "low priority machine check",
            [EXCP_ITLB_MISS]     = "instruction tlb miss fault",
            [EXCP_IMP]           = "instruction memory protection trap",
            [EXCP_ILL]           = "illegal instruction trap",
            [EXCP_BREAK]         = "break instruction trap",
            [EXCP_PRIV_OPR]      = "privileged operation trap",
            [EXCP_PRIV_REG]      = "privileged register trap",
            [EXCP_OVERFLOW]      = "overflow trap",
            [EXCP_COND]          = "conditional trap",
            [EXCP_ASSIST]        = "assist exception trap",
            [EXCP_DTLB_MISS]     = "data tlb miss fault",
            [EXCP_NA_ITLB_MISS]  = "non-access instruction tlb miss",
            [EXCP_NA_DTLB_MISS]  = "non-access data tlb miss",
            [EXCP_DMP]           = "data memory protection trap",
            [EXCP_DMB]           = "data memory break trap",
            [EXCP_TLB_DIRTY]     = "tlb dirty bit trap",
            [EXCP_PAGE_REF]      = "page reference trap",
            [EXCP_ASSIST_EMU]    = "assist emulation trap",
            [EXCP_HPT]           = "high-privilege transfer trap",
            [EXCP_LPT]           = "low-privilege transfer trap",
            [EXCP_TB]            = "taken branch trap",
            [EXCP_DMAR]          = "data memory access rights trap",
            [EXCP_DMPI]          = "data memory protection id trap",
            [EXCP_UNALIGN]       = "unaligned data reference trap",
            [EXCP_PER_INTERRUPT] = "performance monitor interrupt",
            [EXCP_SYSCALL]       = "syscall",
            [EXCP_SYSCALL_LWS]   = "syscall-lws",
        };
        static int count;
        const char *name = NULL;
        char unknown[16];

        if (i >= 0 && i < ARRAY_SIZE(names)) {
            name = names[i];
        }
        if (!name) {
            snprintf(unknown, sizeof(unknown), "unknown %d", i);
            name = unknown;
        }
        qemu_log("INT %6d: %s @ " TARGET_FMT_lx "," TARGET_FMT_lx
                 " -> " TREG_FMT_lx " " TARGET_FMT_lx "\n",
                 ++count, name,
                 hppa_form_gva(env, iasq_f, iaoq_f),
                 hppa_form_gva(env, iasq_b, iaoq_b),
                 env->iaoq_f,
                 hppa_form_gva(env, (uint64_t)env->cr[CR_ISR] << 32,
                               env->cr[CR_IOR]));
    }
    cs->exception_index = -1;
}

bool hppa_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
#ifndef CONFIG_USER_ONLY
    HPPACPU *cpu = HPPA_CPU(cs);
    CPUHPPAState *env = &cpu->env;

    /* If interrupts are requested and enabled, raise them.  */
    if ((env->psw & PSW_I) && (interrupt_request & CPU_INTERRUPT_HARD)) {
        cs->exception_index = EXCP_EXT_INTERRUPT;
        hppa_cpu_do_interrupt(cs);
        return true;
    }
#endif
    return false;
}
