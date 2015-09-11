/*
 *  emulator main execution loop
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
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

#include "config.h"
#include "cpu.h"
#include "sysemu/cpus.h"
#include "exec/memory-internal.h"

bool exit_request;
CPUState *tcg_current_cpu;

/* exit the current TB from a signal handler. The host registers are
   restored in a state compatible with the CPU emulator
 */
#if defined(CONFIG_SOFTMMU)
void cpu_resume_from_signal(CPUState *cpu, void *puc)
{
    /* XXX: restore cpu registers saved in host registers */

    cpu->exception_index = -1;
    siglongjmp(cpu->jmp_env, 1);
}

void cpu_reload_memory_map(CPUState *cpu)
{
    AddressSpaceDispatch *d;

    if (qemu_in_vcpu_thread()) {
        /* Do not let the guest prolong the critical section as much as it
         * as it desires.
         *
         * Currently, this is prevented by the I/O thread's periodinc kicking
         * of the VCPU thread (iothread_requesting_mutex, qemu_cpu_kick_thread)
         * but this will go away once TCG's execution moves out of the global
         * mutex.
         *
         * This pair matches cpu_exec's rcu_read_lock()/rcu_read_unlock(), which
         * only protects cpu->as->dispatch.  Since we reload it below, we can
         * split the critical section.
         */
        rcu_read_unlock();
        rcu_read_lock();
    }

    /* The CPU and TLB are protected by the iothread lock.  */
    d = atomic_rcu_read(&cpu->as->dispatch);
    cpu->memory_dispatch = d;
    tlb_flush(cpu, 1);
}
#endif

void cpu_loop_exit(CPUState *cpu)
{
    cpu->current_tb = NULL;
    siglongjmp(cpu->jmp_env, 1);
}

void cpu_loop_exit_restore(CPUState *cpu, uintptr_t pc)
{
    if (pc) {
        cpu_restore_state(cpu, pc);
    }
    cpu->current_tb = NULL;
    siglongjmp(cpu->jmp_env, 1);
}
