/*
 *  S/390 helpers
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2011 Alexander Graf
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "internal.h"
#include "exec/gdbstub.h"
#include "qemu/timer.h"
#include "exec/exec-all.h"
#include "hw/s390x/ioinst.h"
#ifndef CONFIG_USER_ONLY
#include "sysemu/sysemu.h"
#endif

//#define DEBUG_S390
//#define DEBUG_S390_STDOUT

#ifdef DEBUG_S390
#ifdef DEBUG_S390_STDOUT
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); \
         if (qemu_log_separate()) qemu_log(fmt, ##__VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { qemu_log(fmt, ## __VA_ARGS__); } while (0)
#endif
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif


#ifndef CONFIG_USER_ONLY
void s390x_tod_timer(void *opaque)
{
    S390CPU *cpu = opaque;
    CPUS390XState *env = &cpu->env;

    env->pending_int |= INTERRUPT_TOD;
    cpu_interrupt(CPU(cpu), CPU_INTERRUPT_HARD);
}

void s390x_cpu_timer(void *opaque)
{
    S390CPU *cpu = opaque;
    CPUS390XState *env = &cpu->env;

    env->pending_int |= INTERRUPT_CPUTIMER;
    cpu_interrupt(CPU(cpu), CPU_INTERRUPT_HARD);
}
#endif

S390CPU *s390x_new_cpu(const char *typename, uint32_t core_id, Error **errp)
{
    S390CPU *cpu = S390_CPU(object_new(typename));
    Error *err = NULL;

    object_property_set_int(OBJECT(cpu), core_id, "core-id", &err);
    if (err != NULL) {
        goto out;
    }
    object_property_set_bool(OBJECT(cpu), true, "realized", &err);

out:
    if (err) {
        error_propagate(errp, err);
        object_unref(OBJECT(cpu));
        cpu = NULL;
    }
    return cpu;
}

#ifndef CONFIG_USER_ONLY

hwaddr s390_cpu_get_phys_page_debug(CPUState *cs, vaddr vaddr)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    target_ulong raddr;
    int prot;
    uint64_t asc = env->psw.mask & PSW_MASK_ASC;

    /* 31-Bit mode */
    if (!(env->psw.mask & PSW_MASK_64)) {
        vaddr &= 0x7fffffff;
    }

    if (mmu_translate(env, vaddr, MMU_INST_FETCH, asc, &raddr, &prot, false)) {
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

void load_psw(CPUS390XState *env, uint64_t mask, uint64_t addr)
{
    uint64_t old_mask = env->psw.mask;

    env->psw.addr = addr;
    env->psw.mask = mask;
    if (tcg_enabled()) {
        env->cc_op = (mask >> 44) & 3;
    }

    if ((old_mask ^ mask) & PSW_MASK_PER) {
        s390_cpu_recompute_watchpoints(CPU(s390_env_get_cpu(env)));
    }

    if (mask & PSW_MASK_WAIT) {
        S390CPU *cpu = s390_env_get_cpu(env);
        if (s390_cpu_halt(cpu) == 0) {
#ifndef CONFIG_USER_ONLY
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
#endif
        }
    }
}

uint64_t get_psw_mask(CPUS390XState *env)
{
    uint64_t r = env->psw.mask;

    if (tcg_enabled()) {
        env->cc_op = calc_cc(env, env->cc_op, env->cc_src, env->cc_dst,
                             env->cc_vr);

        r &= ~PSW_MASK_CC;
        assert(!(env->cc_op & ~3));
        r |= (uint64_t)env->cc_op << 44;
    }

    return r;
}

LowCore *cpu_map_lowcore(CPUS390XState *env)
{
    S390CPU *cpu = s390_env_get_cpu(env);
    LowCore *lowcore;
    hwaddr len = sizeof(LowCore);

    lowcore = cpu_physical_memory_map(env->psa, &len, 1);

    if (len < sizeof(LowCore)) {
        cpu_abort(CPU(cpu), "Could not map lowcore\n");
    }

    return lowcore;
}

void cpu_unmap_lowcore(LowCore *lowcore)
{
    cpu_physical_memory_unmap(lowcore, sizeof(LowCore), 1, sizeof(LowCore));
}

void do_restart_interrupt(CPUS390XState *env)
{
    uint64_t mask, addr;
    LowCore *lowcore;

    lowcore = cpu_map_lowcore(env);

    lowcore->restart_old_psw.mask = cpu_to_be64(get_psw_mask(env));
    lowcore->restart_old_psw.addr = cpu_to_be64(env->psw.addr);
    mask = be64_to_cpu(lowcore->restart_new_psw.mask);
    addr = be64_to_cpu(lowcore->restart_new_psw.addr);

    cpu_unmap_lowcore(lowcore);

    load_psw(env, mask, addr);
}

void s390_cpu_recompute_watchpoints(CPUState *cs)
{
    const int wp_flags = BP_CPU | BP_MEM_WRITE | BP_STOP_BEFORE_ACCESS;
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;

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

#endif /* CONFIG_USER_ONLY */

void s390_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                         int flags)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    int i;

    if (env->cc_op > 3) {
        cpu_fprintf(f, "PSW=mask %016" PRIx64 " addr %016" PRIx64 " cc %15s\n",
                    env->psw.mask, env->psw.addr, cc_name(env->cc_op));
    } else {
        cpu_fprintf(f, "PSW=mask %016" PRIx64 " addr %016" PRIx64 " cc %02x\n",
                    env->psw.mask, env->psw.addr, env->cc_op);
    }

    for (i = 0; i < 16; i++) {
        cpu_fprintf(f, "R%02d=%016" PRIx64, i, env->regs[i]);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }

    for (i = 0; i < 16; i++) {
        cpu_fprintf(f, "F%02d=%016" PRIx64, i, get_freg(env, i)->ll);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }

    for (i = 0; i < 32; i++) {
        cpu_fprintf(f, "V%02d=%016" PRIx64 "%016" PRIx64, i,
                    env->vregs[i][0].ll, env->vregs[i][1].ll);
        cpu_fprintf(f, (i % 2) ? "\n" : " ");
    }

#ifndef CONFIG_USER_ONLY
    for (i = 0; i < 16; i++) {
        cpu_fprintf(f, "C%02d=%016" PRIx64, i, env->cregs[i]);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }
#endif

#ifdef DEBUG_INLINE_BRANCHES
    for (i = 0; i < CC_OP_MAX; i++) {
        cpu_fprintf(f, "  %15s = %10ld\t%10ld\n", cc_name(i),
                    inline_branch_miss[i], inline_branch_hit[i]);
    }
#endif

    cpu_fprintf(f, "\n");
}

const char *cc_name(enum cc_op cc_op)
{
    static const char * const cc_names[] = {
        [CC_OP_CONST0]    = "CC_OP_CONST0",
        [CC_OP_CONST1]    = "CC_OP_CONST1",
        [CC_OP_CONST2]    = "CC_OP_CONST2",
        [CC_OP_CONST3]    = "CC_OP_CONST3",
        [CC_OP_DYNAMIC]   = "CC_OP_DYNAMIC",
        [CC_OP_STATIC]    = "CC_OP_STATIC",
        [CC_OP_NZ]        = "CC_OP_NZ",
        [CC_OP_LTGT_32]   = "CC_OP_LTGT_32",
        [CC_OP_LTGT_64]   = "CC_OP_LTGT_64",
        [CC_OP_LTUGTU_32] = "CC_OP_LTUGTU_32",
        [CC_OP_LTUGTU_64] = "CC_OP_LTUGTU_64",
        [CC_OP_LTGT0_32]  = "CC_OP_LTGT0_32",
        [CC_OP_LTGT0_64]  = "CC_OP_LTGT0_64",
        [CC_OP_ADD_64]    = "CC_OP_ADD_64",
        [CC_OP_ADDU_64]   = "CC_OP_ADDU_64",
        [CC_OP_ADDC_64]   = "CC_OP_ADDC_64",
        [CC_OP_SUB_64]    = "CC_OP_SUB_64",
        [CC_OP_SUBU_64]   = "CC_OP_SUBU_64",
        [CC_OP_SUBB_64]   = "CC_OP_SUBB_64",
        [CC_OP_ABS_64]    = "CC_OP_ABS_64",
        [CC_OP_NABS_64]   = "CC_OP_NABS_64",
        [CC_OP_ADD_32]    = "CC_OP_ADD_32",
        [CC_OP_ADDU_32]   = "CC_OP_ADDU_32",
        [CC_OP_ADDC_32]   = "CC_OP_ADDC_32",
        [CC_OP_SUB_32]    = "CC_OP_SUB_32",
        [CC_OP_SUBU_32]   = "CC_OP_SUBU_32",
        [CC_OP_SUBB_32]   = "CC_OP_SUBB_32",
        [CC_OP_ABS_32]    = "CC_OP_ABS_32",
        [CC_OP_NABS_32]   = "CC_OP_NABS_32",
        [CC_OP_COMP_32]   = "CC_OP_COMP_32",
        [CC_OP_COMP_64]   = "CC_OP_COMP_64",
        [CC_OP_TM_32]     = "CC_OP_TM_32",
        [CC_OP_TM_64]     = "CC_OP_TM_64",
        [CC_OP_NZ_F32]    = "CC_OP_NZ_F32",
        [CC_OP_NZ_F64]    = "CC_OP_NZ_F64",
        [CC_OP_NZ_F128]   = "CC_OP_NZ_F128",
        [CC_OP_ICM]       = "CC_OP_ICM",
        [CC_OP_SLA_32]    = "CC_OP_SLA_32",
        [CC_OP_SLA_64]    = "CC_OP_SLA_64",
        [CC_OP_FLOGR]     = "CC_OP_FLOGR",
    };

    return cc_names[cc_op];
}
