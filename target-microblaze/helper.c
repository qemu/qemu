/*
 *  MicroBlaze helper routines.
 *
 *  Copyright (c) 2009 Edgar E. Iglesias <edgar.iglesias@gmail.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "config.h"
#include "cpu.h"
#include "exec-all.h"
#include "host-utils.h"

#define D(x)
#define DMMU(x)

#if defined(CONFIG_USER_ONLY)

void do_interrupt (CPUState *env)
{
    env->exception_index = -1;
    env->regs[14] = env->sregs[SR_PC];
}

int cpu_mb_handle_mmu_fault(CPUState * env, target_ulong address, int rw,
                             int mmu_idx, int is_softmmu)
{
    env->exception_index = 0xaa;
    cpu_dump_state(env, stderr, fprintf, 0);
    return 1;
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState * env, target_ulong addr)
{
    return addr;
}

#else /* !CONFIG_USER_ONLY */

int cpu_mb_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                               int mmu_idx, int is_softmmu)
{
    unsigned int hit;
    unsigned int mmu_available;
    int r = 1;
    int prot;

    mmu_available = 0;
    if (env->pvr.regs[0] & PVR0_USE_MMU) {
        mmu_available = 1;
        if ((env->pvr.regs[0] & PVR0_PVR_FULL_MASK)
            && (env->pvr.regs[11] & PVR11_USE_MMU) != PVR11_USE_MMU) {
            mmu_available = 0;
        }
    }

    /* Translate if the MMU is available and enabled.  */
    if (mmu_available && (env->sregs[SR_MSR] & MSR_VM)) {
        target_ulong vaddr, paddr;
        struct microblaze_mmu_lookup lu;

        hit = mmu_translate(&env->mmu, &lu, address, rw, mmu_idx);
        if (hit) {
            vaddr = address & TARGET_PAGE_MASK;
            paddr = lu.paddr + vaddr - lu.vaddr;

            DMMU(qemu_log("MMU map mmu=%d v=%x p=%x prot=%x\n",
                     mmu_idx, vaddr, paddr, lu.prot));
            r = tlb_set_page(env, vaddr,
                             paddr, lu.prot, mmu_idx, is_softmmu);
        } else {
            env->sregs[SR_EAR] = address;
            DMMU(qemu_log("mmu=%d miss addr=%x\n", mmu_idx, vaddr));

            switch (lu.err) {
                case ERR_PROT:
                    env->sregs[SR_ESR] = rw == 2 ? 17 : 16;
                    env->sregs[SR_ESR] |= (rw == 1) << 10;
                    break;
                case ERR_MISS:
                    env->sregs[SR_ESR] = rw == 2 ? 19 : 18;
                    env->sregs[SR_ESR] |= (rw == 1) << 10;
                    break;
                default:
                    abort();
                    break;
            }

            if (env->exception_index == EXCP_MMU) {
                cpu_abort(env, "recursive faults\n");
            }

            /* TLB miss.  */
            env->exception_index = EXCP_MMU;
        }
    } else {
        /* MMU disabled or not available.  */
        address &= TARGET_PAGE_MASK;
        prot = PAGE_BITS;
        r = tlb_set_page(env, address, address, prot, mmu_idx, is_softmmu);
    }
    return r;
}

void do_interrupt(CPUState *env)
{
    uint32_t t;

    /* IMM flag cannot propagate accross a branch and into the dslot.  */
    assert(!((env->iflags & D_FLAG) && (env->iflags & IMM_FLAG)));
    assert(!(env->iflags & (DRTI_FLAG | DRTE_FLAG | DRTB_FLAG)));
/*    assert(env->sregs[SR_MSR] & (MSR_EE)); Only for HW exceptions.  */
    switch (env->exception_index) {
        case EXCP_MMU:
            env->regs[17] = env->sregs[SR_PC];

            /* Exception breaks branch + dslot sequence?  */
            if (env->iflags & D_FLAG) {
                D(qemu_log("D_FLAG set at exception bimm=%d\n", env->bimm));
                env->sregs[SR_ESR] |= 1 << 12 ;
                env->sregs[SR_BTR] = env->btarget;

                /* Reexecute the branch.  */
                env->regs[17] -= 4;
                /* was the branch immprefixed?.  */
                if (env->bimm) {
                    qemu_log_mask(CPU_LOG_INT,
                                  "bimm exception at pc=%x iflags=%x\n",
                                  env->sregs[SR_PC], env->iflags);
                    env->regs[17] -= 4;
                    log_cpu_state_mask(CPU_LOG_INT, env, 0);
                }
            } else if (env->iflags & IMM_FLAG) {
                D(qemu_log("IMM_FLAG set at exception\n"));
                env->regs[17] -= 4;
            }

            /* Disable the MMU.  */
            t = (env->sregs[SR_MSR] & (MSR_VM | MSR_UM)) << 1;
            env->sregs[SR_MSR] &= ~(MSR_VMS | MSR_UMS | MSR_VM | MSR_UM);
            env->sregs[SR_MSR] |= t;
            /* Exception in progress.  */
            env->sregs[SR_MSR] |= MSR_EIP;

            qemu_log_mask(CPU_LOG_INT,
                          "exception at pc=%x ear=%x iflags=%x\n",
                          env->sregs[SR_PC], env->sregs[SR_EAR], env->iflags);
            log_cpu_state_mask(CPU_LOG_INT, env, 0);
            env->iflags &= ~(IMM_FLAG | D_FLAG);
            env->sregs[SR_PC] = 0x20;
            break;

        case EXCP_IRQ:
            assert(!(env->sregs[SR_MSR] & (MSR_EIP | MSR_BIP)));
            assert(env->sregs[SR_MSR] & MSR_IE);
            assert(!(env->iflags & D_FLAG));

            t = (env->sregs[SR_MSR] & (MSR_VM | MSR_UM)) << 1;

#if 0
#include "disas.h"

/* Useful instrumentation when debugging interrupt issues in either
   the models or in sw.  */
            {
                const char *sym;

                sym = lookup_symbol(env->sregs[SR_PC]);
                if (sym
                    && (!strcmp("netif_rx", sym)
                        || !strcmp("process_backlog", sym))) {

                    qemu_log(
                         "interrupt at pc=%x msr=%x %x iflags=%x sym=%s\n",
                         env->sregs[SR_PC], env->sregs[SR_MSR], t, env->iflags,
                         sym);

                    log_cpu_state(env, 0);
                }
            }
#endif
            qemu_log_mask(CPU_LOG_INT,
                         "interrupt at pc=%x msr=%x %x iflags=%x\n",
                         env->sregs[SR_PC], env->sregs[SR_MSR], t, env->iflags);

            env->sregs[SR_MSR] &= ~(MSR_VMS | MSR_UMS | MSR_VM \
                                    | MSR_UM | MSR_IE);
            env->sregs[SR_MSR] |= t;

            env->regs[14] = env->sregs[SR_PC];
            env->sregs[SR_PC] = 0x10;
            //log_cpu_state_mask(CPU_LOG_INT, env, 0);
            break;

        case EXCP_BREAK:
        case EXCP_HW_BREAK:
            assert(!(env->iflags & IMM_FLAG));
            assert(!(env->iflags & D_FLAG));
            t = (env->sregs[SR_MSR] & (MSR_VM | MSR_UM)) << 1;
            qemu_log_mask(CPU_LOG_INT,
                        "break at pc=%x msr=%x %x iflags=%x\n",
                        env->sregs[SR_PC], env->sregs[SR_MSR], t, env->iflags);
            log_cpu_state_mask(CPU_LOG_INT, env, 0);
            env->sregs[SR_MSR] &= ~(MSR_VMS | MSR_UMS | MSR_VM | MSR_UM);
            env->sregs[SR_MSR] |= t;
            env->sregs[SR_MSR] |= MSR_BIP;
            if (env->exception_index == EXCP_HW_BREAK) {
                env->regs[16] = env->sregs[SR_PC];
                env->sregs[SR_MSR] |= MSR_BIP;
                env->sregs[SR_PC] = 0x18;
            } else
                env->sregs[SR_PC] = env->btarget;
            break;
        default:
            cpu_abort(env, "unhandled exception type=%d\n",
                      env->exception_index);
            break;
    }
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState * env, target_ulong addr)
{
    target_ulong vaddr, paddr = 0;
    struct microblaze_mmu_lookup lu;
    unsigned int hit;

    if (env->sregs[SR_MSR] & MSR_VM) {
        hit = mmu_translate(&env->mmu, &lu, addr, 0, 0);
        if (hit) {
            vaddr = addr & TARGET_PAGE_MASK;
            paddr = lu.paddr + vaddr - lu.vaddr;
        } else
            paddr = 0; /* ???.  */
    } else
        paddr = addr & TARGET_PAGE_MASK;

    return paddr;
}
#endif
