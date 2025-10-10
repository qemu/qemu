/*
 *  S/390 helpers - system only
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2011 Alexander Graf
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
#include "cpu.h"
#include "s390x-internal.h"
#include "gdbstub/helpers.h"
#include "qemu/timer.h"
#include "hw/s390x/ioinst.h"
#include "system/hw_accel.h"
#include "system/memory.h"
#include "system/runstate.h"
#include "exec/target_page.h"
#include "exec/watchpoint.h"

void s390x_tod_timer(void *opaque)
{
    cpu_inject_clock_comparator((S390CPU *) opaque);
}

void s390x_cpu_timer(void *opaque)
{
    cpu_inject_cpu_timer((S390CPU *) opaque);
}

hwaddr s390_cpu_get_phys_page_debug(CPUState *cs, vaddr vaddr)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    target_ulong raddr;
    int prot;
    uint64_t asc = env->psw.mask & PSW_MASK_ASC;
    uint64_t tec;

    /* 31-Bit mode */
    if (!(env->psw.mask & PSW_MASK_64)) {
        vaddr &= 0x7fffffff;
    }

    /* We want to read the code (e.g., see what we are single-stepping).*/
    if (asc != PSW_ASC_HOME) {
        asc = PSW_ASC_PRIMARY;
    }

    /*
     * We want to read code even if IEP is active. Use MMU_DATA_LOAD instead
     * of MMU_INST_FETCH.
     */
    if (mmu_translate(env, vaddr, MMU_DATA_LOAD, asc, &raddr, &prot, &tec)) {
        return -1;
    }
    return raddr;
}

hwaddr s390_cpu_get_phys_addr_debug(CPUState *cs, vaddr vaddr)
{
    hwaddr phys_addr;
    target_ulong page;

    page = vaddr & TARGET_PAGE_MASK;
    phys_addr = cpu_get_phys_page_debug(cs, page);
    phys_addr += (vaddr & ~TARGET_PAGE_MASK);

    return phys_addr;
}

static inline bool is_special_wait_psw(uint64_t psw_addr)
{
    /* signal quiesce */
    return (psw_addr & 0xfffUL) == 0xfffUL;
}

void s390_handle_wait(S390CPU *cpu)
{
    CPUState *cs = CPU(cpu);

    s390_cpu_halt(cpu);

    if (s390_count_running_cpus() == 0) {
        if (is_special_wait_psw(cpu->env.psw.addr)) {
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        } else {
            cpu->env.crash_reason = S390_CRASH_REASON_DISABLED_WAIT;
            qemu_system_guest_panicked(cpu_get_crash_info(cs));
        }
    }
}

LowCore *cpu_map_lowcore(CPUS390XState *env)
{
    LowCore *lowcore;
    hwaddr len = sizeof(LowCore);
    CPUState *cs = env_cpu(env);
    const MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;

    lowcore = address_space_map(cs->as, env->psa, &len, true, attrs);

    if (len < sizeof(LowCore)) {
        cpu_abort(cs, "Could not map lowcore\n");
    }

    return lowcore;
}

void cpu_unmap_lowcore(CPUS390XState *env, LowCore *lowcore)
{
    AddressSpace *as = env_cpu(env)->as;

    address_space_unmap(as, lowcore, sizeof(LowCore), true, sizeof(LowCore));
}

void do_restart_interrupt(CPUS390XState *env)
{
    uint64_t mask, addr;
    LowCore *lowcore;

    lowcore = cpu_map_lowcore(env);

    lowcore->restart_old_psw.mask = cpu_to_be64(s390_cpu_get_psw_mask(env));
    lowcore->restart_old_psw.addr = cpu_to_be64(env->psw.addr);
    mask = be64_to_cpu(lowcore->restart_new_psw.mask);
    addr = be64_to_cpu(lowcore->restart_new_psw.addr);

    cpu_unmap_lowcore(env, lowcore);
    env->pending_int &= ~INTERRUPT_RESTART;

    s390_cpu_set_psw(env, mask, addr);
}

void s390_cpu_recompute_watchpoints(CPUState *cs)
{
    const int wp_flags = BP_CPU | BP_MEM_WRITE | BP_STOP_BEFORE_ACCESS;
    CPUS390XState *env = cpu_env(cs);

    /* We are called when the watchpoints have changed. First
       remove them all.  */
    cpu_watchpoint_remove_all(cs, BP_CPU);

    /* Return if PER is not enabled */
    if (!(env->psw.mask & PSW_MASK_PER)) {
        return;
    }

    /* Return if storage-alteration event is not enabled.  */
    if (!(env->cregs[9] & PER_CR9_EVENT_STORE)) {
        return;
    }

    if (env->cregs[10] == 0 && env->cregs[11] == -1LL) {
        /* We can't create a watchoint spanning the whole memory range, so
           split it in two parts.   */
        cpu_watchpoint_insert(cs, 0, 1ULL << 63, wp_flags, NULL);
        cpu_watchpoint_insert(cs, 1ULL << 63, 1ULL << 63, wp_flags, NULL);
    } else if (env->cregs[10] > env->cregs[11]) {
        /* The address range loops, create two watchpoints.  */
        cpu_watchpoint_insert(cs, env->cregs[10], -env->cregs[10],
                              wp_flags, NULL);
        cpu_watchpoint_insert(cs, 0, env->cregs[11] + 1, wp_flags, NULL);

    } else {
        /* Default case, create a single watchpoint.  */
        cpu_watchpoint_insert(cs, env->cregs[10],
                              env->cregs[11] - env->cregs[10] + 1,
                              wp_flags, NULL);
    }
}
