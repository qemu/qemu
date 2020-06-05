/*
 *  S/390 helpers
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
#include "internal.h"
#include "exec/gdbstub.h"
#include "qemu/timer.h"
#include "qemu/qemu-print.h"
#include "hw/s390x/ioinst.h"
#include "hw/s390x/pv.h"
#include "sysemu/hw_accel.h"
#include "sysemu/runstate.h"
#ifndef CONFIG_USER_ONLY
#include "sysemu/tcg.h"
#endif

#ifndef CONFIG_USER_ONLY
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

    if (s390_cpu_halt(cpu) == 0) {
        if (is_special_wait_psw(cpu->env.psw.addr)) {
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        } else {
            cpu->env.crash_reason = S390_CRASH_REASON_DISABLED_WAIT;
            qemu_system_guest_panicked(cpu_get_crash_info(cs));
        }
    }
}

void load_psw(CPUS390XState *env, uint64_t mask, uint64_t addr)
{
    uint64_t old_mask = env->psw.mask;

    env->psw.addr = addr;
    env->psw.mask = mask;

    /* KVM will handle all WAITs and trigger a WAIT exit on disabled_wait */
    if (!tcg_enabled()) {
        return;
    }
    env->cc_op = (mask >> 44) & 3;

    if ((old_mask ^ mask) & PSW_MASK_PER) {
        s390_cpu_recompute_watchpoints(env_cpu(env));
    }

    if (mask & PSW_MASK_WAIT) {
        s390_handle_wait(env_archcpu(env));
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
    LowCore *lowcore;
    hwaddr len = sizeof(LowCore);

    lowcore = cpu_physical_memory_map(env->psa, &len, true);

    if (len < sizeof(LowCore)) {
        cpu_abort(env_cpu(env), "Could not map lowcore\n");
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
    env->pending_int &= ~INTERRUPT_RESTART;

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

typedef struct SigpSaveArea {
    uint64_t    fprs[16];                       /* 0x0000 */
    uint64_t    grs[16];                        /* 0x0080 */
    PSW         psw;                            /* 0x0100 */
    uint8_t     pad_0x0110[0x0118 - 0x0110];    /* 0x0110 */
    uint32_t    prefix;                         /* 0x0118 */
    uint32_t    fpc;                            /* 0x011c */
    uint8_t     pad_0x0120[0x0124 - 0x0120];    /* 0x0120 */
    uint32_t    todpr;                          /* 0x0124 */
    uint64_t    cputm;                          /* 0x0128 */
    uint64_t    ckc;                            /* 0x0130 */
    uint8_t     pad_0x0138[0x0140 - 0x0138];    /* 0x0138 */
    uint32_t    ars[16];                        /* 0x0140 */
    uint64_t    crs[16];                        /* 0x0384 */
} SigpSaveArea;
QEMU_BUILD_BUG_ON(sizeof(SigpSaveArea) != 512);

int s390_store_status(S390CPU *cpu, hwaddr addr, bool store_arch)
{
    static const uint8_t ar_id = 1;
    SigpSaveArea *sa;
    hwaddr len = sizeof(*sa);
    int i;

    /* For PVMs storing will occur when this cpu enters SIE again */
    if (s390_is_pv()) {
        return 0;
    }

    sa = cpu_physical_memory_map(addr, &len, true);
    if (!sa) {
        return -EFAULT;
    }
    if (len != sizeof(*sa)) {
        cpu_physical_memory_unmap(sa, len, 1, 0);
        return -EFAULT;
    }

    if (store_arch) {
        cpu_physical_memory_write(offsetof(LowCore, ar_access_id), &ar_id, 1);
    }
    for (i = 0; i < 16; ++i) {
        sa->fprs[i] = cpu_to_be64(*get_freg(&cpu->env, i));
    }
    for (i = 0; i < 16; ++i) {
        sa->grs[i] = cpu_to_be64(cpu->env.regs[i]);
    }
    sa->psw.addr = cpu_to_be64(cpu->env.psw.addr);
    sa->psw.mask = cpu_to_be64(get_psw_mask(&cpu->env));
    sa->prefix = cpu_to_be32(cpu->env.psa);
    sa->fpc = cpu_to_be32(cpu->env.fpc);
    sa->todpr = cpu_to_be32(cpu->env.todpr);
    sa->cputm = cpu_to_be64(cpu->env.cputm);
    sa->ckc = cpu_to_be64(cpu->env.ckc >> 8);
    for (i = 0; i < 16; ++i) {
        sa->ars[i] = cpu_to_be32(cpu->env.aregs[i]);
    }
    for (i = 0; i < 16; ++i) {
        sa->crs[i] = cpu_to_be64(cpu->env.cregs[i]);
    }

    cpu_physical_memory_unmap(sa, len, 1, len);

    return 0;
}

typedef struct SigpAdtlSaveArea {
    uint64_t    vregs[32][2];                     /* 0x0000 */
    uint8_t     pad_0x0200[0x0400 - 0x0200];      /* 0x0200 */
    uint64_t    gscb[4];                          /* 0x0400 */
    uint8_t     pad_0x0420[0x1000 - 0x0420];      /* 0x0420 */
} SigpAdtlSaveArea;
QEMU_BUILD_BUG_ON(sizeof(SigpAdtlSaveArea) != 4096);

#define ADTL_GS_MIN_SIZE 2048 /* minimal size of adtl save area for GS */
int s390_store_adtl_status(S390CPU *cpu, hwaddr addr, hwaddr len)
{
    SigpAdtlSaveArea *sa;
    hwaddr save = len;
    int i;

    sa = cpu_physical_memory_map(addr, &save, true);
    if (!sa) {
        return -EFAULT;
    }
    if (save != len) {
        cpu_physical_memory_unmap(sa, len, 1, 0);
        return -EFAULT;
    }

    if (s390_has_feat(S390_FEAT_VECTOR)) {
        for (i = 0; i < 32; i++) {
            sa->vregs[i][0] = cpu_to_be64(cpu->env.vregs[i][0]);
            sa->vregs[i][1] = cpu_to_be64(cpu->env.vregs[i][1]);
        }
    }
    if (s390_has_feat(S390_FEAT_GUARDED_STORAGE) && len >= ADTL_GS_MIN_SIZE) {
        for (i = 0; i < 4; i++) {
            sa->gscb[i] = cpu_to_be64(cpu->env.gscb[i]);
        }
    }

    cpu_physical_memory_unmap(sa, len, 1, len);
    return 0;
}
#endif /* CONFIG_USER_ONLY */

void s390_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    int i;

    if (env->cc_op > 3) {
        qemu_fprintf(f, "PSW=mask %016" PRIx64 " addr %016" PRIx64 " cc %15s\n",
                     env->psw.mask, env->psw.addr, cc_name(env->cc_op));
    } else {
        qemu_fprintf(f, "PSW=mask %016" PRIx64 " addr %016" PRIx64 " cc %02x\n",
                     env->psw.mask, env->psw.addr, env->cc_op);
    }

    for (i = 0; i < 16; i++) {
        qemu_fprintf(f, "R%02d=%016" PRIx64, i, env->regs[i]);
        if ((i % 4) == 3) {
            qemu_fprintf(f, "\n");
        } else {
            qemu_fprintf(f, " ");
        }
    }

    if (flags & CPU_DUMP_FPU) {
        if (s390_has_feat(S390_FEAT_VECTOR)) {
            for (i = 0; i < 32; i++) {
                qemu_fprintf(f, "V%02d=%016" PRIx64 "%016" PRIx64 "%c",
                             i, env->vregs[i][0], env->vregs[i][1],
                             i % 2 ? '\n' : ' ');
            }
        } else {
            for (i = 0; i < 16; i++) {
                qemu_fprintf(f, "F%02d=%016" PRIx64 "%c",
                             i, *get_freg(env, i),
                             (i % 4) == 3 ? '\n' : ' ');
            }
        }
    }

#ifndef CONFIG_USER_ONLY
    for (i = 0; i < 16; i++) {
        qemu_fprintf(f, "C%02d=%016" PRIx64, i, env->cregs[i]);
        if ((i % 4) == 3) {
            qemu_fprintf(f, "\n");
        } else {
            qemu_fprintf(f, " ");
        }
    }
#endif

#ifdef DEBUG_INLINE_BRANCHES
    for (i = 0; i < CC_OP_MAX; i++) {
        qemu_fprintf(f, "  %15s = %10ld\t%10ld\n", cc_name(i),
                     inline_branch_miss[i], inline_branch_hit[i]);
    }
#endif

    qemu_fprintf(f, "\n");
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
        [CC_OP_LCBB]      = "CC_OP_LCBB",
        [CC_OP_VC]        = "CC_OP_VC",
    };

    return cc_names[cc_op];
}
