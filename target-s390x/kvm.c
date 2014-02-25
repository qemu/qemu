/*
 * QEMU S390x KVM implementation
 *
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
 * Copyright IBM Corp. 2012
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
 * Contributions after 2012-10-29 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 *
 * You should have received a copy of the GNU (Lesser) General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/kvm.h>
#include <asm/ptrace.h>

#include "qemu-common.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "cpu.h"
#include "sysemu/device_tree.h"
#include "qapi/qmp/qjson.h"
#include "monitor/monitor.h"
#include "trace.h"

/* #define DEBUG_KVM */

#ifdef DEBUG_KVM
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

#define IPA0_DIAG                       0x8300
#define IPA0_SIGP                       0xae00
#define IPA0_B2                         0xb200
#define IPA0_B9                         0xb900
#define IPA0_EB                         0xeb00

#define PRIV_B2_SCLP_CALL               0x20
#define PRIV_B2_CSCH                    0x30
#define PRIV_B2_HSCH                    0x31
#define PRIV_B2_MSCH                    0x32
#define PRIV_B2_SSCH                    0x33
#define PRIV_B2_STSCH                   0x34
#define PRIV_B2_TSCH                    0x35
#define PRIV_B2_TPI                     0x36
#define PRIV_B2_SAL                     0x37
#define PRIV_B2_RSCH                    0x38
#define PRIV_B2_STCRW                   0x39
#define PRIV_B2_STCPS                   0x3a
#define PRIV_B2_RCHP                    0x3b
#define PRIV_B2_SCHM                    0x3c
#define PRIV_B2_CHSC                    0x5f
#define PRIV_B2_SIGA                    0x74
#define PRIV_B2_XSCH                    0x76

#define PRIV_EB_SQBS                    0x8a

#define PRIV_B9_EQBS                    0x9c

#define DIAG_IPL                        0x308
#define DIAG_KVM_HYPERCALL              0x500
#define DIAG_KVM_BREAKPOINT             0x501

#define ICPT_INSTRUCTION                0x04
#define ICPT_WAITPSW                    0x1c
#define ICPT_SOFT_INTERCEPT             0x24
#define ICPT_CPU_STOP                   0x28
#define ICPT_IO                         0x40

const KVMCapabilityInfo kvm_arch_required_capabilities[] = {
    KVM_CAP_LAST_INFO
};

static int cap_sync_regs;
static int cap_async_pf;

static void *legacy_s390_alloc(size_t size);

int kvm_arch_init(KVMState *s)
{
    cap_sync_regs = kvm_check_extension(s, KVM_CAP_SYNC_REGS);
    cap_async_pf = kvm_check_extension(s, KVM_CAP_ASYNC_PF);
    if (!kvm_check_extension(s, KVM_CAP_S390_GMAP)
        || !kvm_check_extension(s, KVM_CAP_S390_COW)) {
        phys_mem_set_alloc(legacy_s390_alloc);
    }
    return 0;
}

unsigned long kvm_arch_vcpu_id(CPUState *cpu)
{
    return cpu->cpu_index;
}

int kvm_arch_init_vcpu(CPUState *cpu)
{
    /* nothing todo yet */
    return 0;
}

void kvm_s390_reset_vcpu(S390CPU *cpu)
{
    CPUState *cs = CPU(cpu);

    /* The initial reset call is needed here to reset in-kernel
     * vcpu data that we can't access directly from QEMU
     * (i.e. with older kernels which don't support sync_regs/ONE_REG).
     * Before this ioctl cpu_synchronize_state() is called in common kvm
     * code (kvm-all) */
    if (kvm_vcpu_ioctl(cs, KVM_S390_INITIAL_RESET, NULL)) {
        perror("Can't reset vcpu\n");
    }
}

int kvm_arch_put_registers(CPUState *cs, int level)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    struct kvm_sregs sregs;
    struct kvm_regs regs;
    int r;
    int i;

    /* always save the PSW  and the GPRS*/
    cs->kvm_run->psw_addr = env->psw.addr;
    cs->kvm_run->psw_mask = env->psw.mask;

    if (cap_sync_regs && cs->kvm_run->kvm_valid_regs & KVM_SYNC_GPRS) {
        for (i = 0; i < 16; i++) {
            cs->kvm_run->s.regs.gprs[i] = env->regs[i];
            cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_GPRS;
        }
    } else {
        for (i = 0; i < 16; i++) {
            regs.gprs[i] = env->regs[i];
        }
        r = kvm_vcpu_ioctl(cs, KVM_SET_REGS, &regs);
        if (r < 0) {
            return r;
        }
    }

    /* Do we need to save more than that? */
    if (level == KVM_PUT_RUNTIME_STATE) {
        return 0;
    }

    /*
     * These ONE_REGS are not protected by a capability. As they are only
     * necessary for migration we just trace a possible error, but don't
     * return with an error return code.
     */
    kvm_set_one_reg(cs, KVM_REG_S390_CPU_TIMER, &env->cputm);
    kvm_set_one_reg(cs, KVM_REG_S390_CLOCK_COMP, &env->ckc);
    kvm_set_one_reg(cs, KVM_REG_S390_TODPR, &env->todpr);
    kvm_set_one_reg(cs, KVM_REG_S390_GBEA, &env->gbea);
    kvm_set_one_reg(cs, KVM_REG_S390_PP, &env->pp);

    if (cap_async_pf) {
        r = kvm_set_one_reg(cs, KVM_REG_S390_PFTOKEN, &env->pfault_token);
        if (r < 0) {
            return r;
        }
        r = kvm_set_one_reg(cs, KVM_REG_S390_PFCOMPARE, &env->pfault_compare);
        if (r < 0) {
            return r;
        }
        r = kvm_set_one_reg(cs, KVM_REG_S390_PFSELECT, &env->pfault_select);
        if (r < 0) {
            return r;
        }
    }

    if (cap_sync_regs &&
        cs->kvm_run->kvm_valid_regs & KVM_SYNC_ACRS &&
        cs->kvm_run->kvm_valid_regs & KVM_SYNC_CRS) {
        for (i = 0; i < 16; i++) {
            cs->kvm_run->s.regs.acrs[i] = env->aregs[i];
            cs->kvm_run->s.regs.crs[i] = env->cregs[i];
        }
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_ACRS;
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_CRS;
    } else {
        for (i = 0; i < 16; i++) {
            sregs.acrs[i] = env->aregs[i];
            sregs.crs[i] = env->cregs[i];
        }
        r = kvm_vcpu_ioctl(cs, KVM_SET_SREGS, &sregs);
        if (r < 0) {
            return r;
        }
    }

    /* Finally the prefix */
    if (cap_sync_regs && cs->kvm_run->kvm_valid_regs & KVM_SYNC_PREFIX) {
        cs->kvm_run->s.regs.prefix = env->psa;
        cs->kvm_run->kvm_dirty_regs |= KVM_SYNC_PREFIX;
    } else {
        /* prefix is only supported via sync regs */
    }
    return 0;
}

int kvm_arch_get_registers(CPUState *cs)
{
    S390CPU *cpu = S390_CPU(cs);
    CPUS390XState *env = &cpu->env;
    struct kvm_sregs sregs;
    struct kvm_regs regs;
    int i, r;

    /* get the PSW */
    env->psw.addr = cs->kvm_run->psw_addr;
    env->psw.mask = cs->kvm_run->psw_mask;

    /* the GPRS */
    if (cap_sync_regs && cs->kvm_run->kvm_valid_regs & KVM_SYNC_GPRS) {
        for (i = 0; i < 16; i++) {
            env->regs[i] = cs->kvm_run->s.regs.gprs[i];
        }
    } else {
        r = kvm_vcpu_ioctl(cs, KVM_GET_REGS, &regs);
        if (r < 0) {
            return r;
        }
         for (i = 0; i < 16; i++) {
            env->regs[i] = regs.gprs[i];
        }
    }

    /* The ACRS and CRS */
    if (cap_sync_regs &&
        cs->kvm_run->kvm_valid_regs & KVM_SYNC_ACRS &&
        cs->kvm_run->kvm_valid_regs & KVM_SYNC_CRS) {
        for (i = 0; i < 16; i++) {
            env->aregs[i] = cs->kvm_run->s.regs.acrs[i];
            env->cregs[i] = cs->kvm_run->s.regs.crs[i];
        }
    } else {
        r = kvm_vcpu_ioctl(cs, KVM_GET_SREGS, &sregs);
        if (r < 0) {
            return r;
        }
         for (i = 0; i < 16; i++) {
            env->aregs[i] = sregs.acrs[i];
            env->cregs[i] = sregs.crs[i];
        }
    }

    /* The prefix */
    if (cap_sync_regs && cs->kvm_run->kvm_valid_regs & KVM_SYNC_PREFIX) {
        env->psa = cs->kvm_run->s.regs.prefix;
    }

    /*
     * These ONE_REGS are not protected by a capability. As they are only
     * necessary for migration we just trace a possible error, but don't
     * return with an error return code.
     */
    kvm_get_one_reg(cs, KVM_REG_S390_CPU_TIMER, &env->cputm);
    kvm_get_one_reg(cs, KVM_REG_S390_CLOCK_COMP, &env->ckc);
    kvm_get_one_reg(cs, KVM_REG_S390_TODPR, &env->todpr);
    kvm_get_one_reg(cs, KVM_REG_S390_GBEA, &env->gbea);
    kvm_get_one_reg(cs, KVM_REG_S390_PP, &env->pp);

    if (cap_async_pf) {
        r = kvm_get_one_reg(cs, KVM_REG_S390_PFTOKEN, &env->pfault_token);
        if (r < 0) {
            return r;
        }
        r = kvm_get_one_reg(cs, KVM_REG_S390_PFCOMPARE, &env->pfault_compare);
        if (r < 0) {
            return r;
        }
        r = kvm_get_one_reg(cs, KVM_REG_S390_PFSELECT, &env->pfault_select);
        if (r < 0) {
            return r;
        }
    }

    return 0;
}

/*
 * Legacy layout for s390:
 * Older S390 KVM requires the topmost vma of the RAM to be
 * smaller than an system defined value, which is at least 256GB.
 * Larger systems have larger values. We put the guest between
 * the end of data segment (system break) and this value. We
 * use 32GB as a base to have enough room for the system break
 * to grow. We also have to use MAP parameters that avoid
 * read-only mapping of guest pages.
 */
static void *legacy_s390_alloc(size_t size)
{
    void *mem;

    mem = mmap((void *) 0x800000000ULL, size,
               PROT_EXEC|PROT_READ|PROT_WRITE,
               MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    return mem == MAP_FAILED ? NULL : mem;
}

/* DIAG 501 is used for sw breakpoints */
static const uint8_t diag_501[] = {0x83, 0x24, 0x05, 0x01};

int kvm_arch_insert_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{

    if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn,
                            sizeof(diag_501), 0) ||
        cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)diag_501,
                            sizeof(diag_501), 1)) {
        return -EINVAL;
    }
    return 0;
}

int kvm_arch_remove_sw_breakpoint(CPUState *cs, struct kvm_sw_breakpoint *bp)
{
    uint8_t t[sizeof(diag_501)];

    if (cpu_memory_rw_debug(cs, bp->pc, t, sizeof(diag_501), 0)) {
        return -EINVAL;
    } else if (memcmp(t, diag_501, sizeof(diag_501))) {
        return -EINVAL;
    } else if (cpu_memory_rw_debug(cs, bp->pc, (uint8_t *)&bp->saved_insn,
                                   sizeof(diag_501), 1)) {
        return -EINVAL;
    }

    return 0;
}

int kvm_arch_insert_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type)
{
    return -ENOSYS;
}

int kvm_arch_remove_hw_breakpoint(target_ulong addr,
                                  target_ulong len, int type)
{
    return -ENOSYS;
}

void kvm_arch_remove_all_hw_breakpoints(void)
{
}

void kvm_arch_update_guest_debug(CPUState *cpu, struct kvm_guest_debug *dbg)
{
}

void kvm_arch_pre_run(CPUState *cpu, struct kvm_run *run)
{
}

void kvm_arch_post_run(CPUState *cpu, struct kvm_run *run)
{
}

int kvm_arch_process_async_events(CPUState *cs)
{
    return cs->halted;
}

void kvm_s390_interrupt_internal(S390CPU *cpu, int type, uint32_t parm,
                                 uint64_t parm64, int vm)
{
    CPUState *cs = CPU(cpu);
    struct kvm_s390_interrupt kvmint;
    int r;

    if (!cs->kvm_state) {
        return;
    }

    kvmint.type = type;
    kvmint.parm = parm;
    kvmint.parm64 = parm64;

    if (vm) {
        r = kvm_vm_ioctl(cs->kvm_state, KVM_S390_INTERRUPT, &kvmint);
    } else {
        r = kvm_vcpu_ioctl(cs, KVM_S390_INTERRUPT, &kvmint);
    }

    if (r < 0) {
        fprintf(stderr, "KVM failed to inject interrupt\n");
        exit(1);
    }
}

void kvm_s390_virtio_irq(S390CPU *cpu, int config_change, uint64_t token)
{
    kvm_s390_interrupt_internal(cpu, KVM_S390_INT_VIRTIO, config_change,
                                token, 1);
}

void kvm_s390_interrupt(S390CPU *cpu, int type, uint32_t code)
{
    kvm_s390_interrupt_internal(cpu, type, code, 0, 0);
}

static void enter_pgmcheck(S390CPU *cpu, uint16_t code)
{
    kvm_s390_interrupt(cpu, KVM_S390_PROGRAM_INT, code);
}

static int kvm_sclp_service_call(S390CPU *cpu, struct kvm_run *run,
                                 uint16_t ipbh0)
{
    CPUS390XState *env = &cpu->env;
    uint64_t sccb;
    uint32_t code;
    int r = 0;

    cpu_synchronize_state(CPU(cpu));
    sccb = env->regs[ipbh0 & 0xf];
    code = env->regs[(ipbh0 & 0xf0) >> 4];

    r = sclp_service_call(env, sccb, code);
    if (r < 0) {
        enter_pgmcheck(cpu, -r);
    } else {
        setcc(cpu, r);
    }

    return 0;
}

static int handle_b2(S390CPU *cpu, struct kvm_run *run, uint8_t ipa1)
{
    CPUS390XState *env = &cpu->env;
    int rc = 0;
    uint16_t ipbh0 = (run->s390_sieic.ipb & 0xffff0000) >> 16;

    cpu_synchronize_state(CPU(cpu));

    switch (ipa1) {
    case PRIV_B2_XSCH:
        ioinst_handle_xsch(cpu, env->regs[1]);
        break;
    case PRIV_B2_CSCH:
        ioinst_handle_csch(cpu, env->regs[1]);
        break;
    case PRIV_B2_HSCH:
        ioinst_handle_hsch(cpu, env->regs[1]);
        break;
    case PRIV_B2_MSCH:
        ioinst_handle_msch(cpu, env->regs[1], run->s390_sieic.ipb);
        break;
    case PRIV_B2_SSCH:
        ioinst_handle_ssch(cpu, env->regs[1], run->s390_sieic.ipb);
        break;
    case PRIV_B2_STCRW:
        ioinst_handle_stcrw(cpu, run->s390_sieic.ipb);
        break;
    case PRIV_B2_STSCH:
        ioinst_handle_stsch(cpu, env->regs[1], run->s390_sieic.ipb);
        break;
    case PRIV_B2_TSCH:
        /* We should only get tsch via KVM_EXIT_S390_TSCH. */
        fprintf(stderr, "Spurious tsch intercept\n");
        break;
    case PRIV_B2_CHSC:
        ioinst_handle_chsc(cpu, run->s390_sieic.ipb);
        break;
    case PRIV_B2_TPI:
        /* This should have been handled by kvm already. */
        fprintf(stderr, "Spurious tpi intercept\n");
        break;
    case PRIV_B2_SCHM:
        ioinst_handle_schm(cpu, env->regs[1], env->regs[2],
                           run->s390_sieic.ipb);
        break;
    case PRIV_B2_RSCH:
        ioinst_handle_rsch(cpu, env->regs[1]);
        break;
    case PRIV_B2_RCHP:
        ioinst_handle_rchp(cpu, env->regs[1]);
        break;
    case PRIV_B2_STCPS:
        /* We do not provide this instruction, it is suppressed. */
        break;
    case PRIV_B2_SAL:
        ioinst_handle_sal(cpu, env->regs[1]);
        break;
    case PRIV_B2_SIGA:
        /* Not provided, set CC = 3 for subchannel not operational */
        setcc(cpu, 3);
        break;
    case PRIV_B2_SCLP_CALL:
        rc = kvm_sclp_service_call(cpu, run, ipbh0);
        break;
    default:
        rc = -1;
        DPRINTF("KVM: unhandled PRIV: 0xb2%x\n", ipa1);
        break;
    }

    return rc;
}

static int handle_b9(S390CPU *cpu, struct kvm_run *run, uint8_t ipa1)
{
    int r = 0;

    switch (ipa1) {
    case PRIV_B9_EQBS:
        /* just inject exception */
        r = -1;
        break;
    default:
        r = -1;
        DPRINTF("KVM: unhandled PRIV: 0xb9%x\n", ipa1);
        break;
    }

    return r;
}

static int handle_eb(S390CPU *cpu, struct kvm_run *run, uint8_t ipa1)
{
    int r = 0;

    switch (ipa1) {
    case PRIV_EB_SQBS:
        /* just inject exception */
        r = -1;
        break;
    default:
        r = -1;
        DPRINTF("KVM: unhandled PRIV: 0xeb%x\n", ipa1);
        break;
    }

    return r;
}

static int handle_hypercall(S390CPU *cpu, struct kvm_run *run)
{
    CPUS390XState *env = &cpu->env;
    int ret;

    cpu_synchronize_state(CPU(cpu));
    ret = s390_virtio_hypercall(env);
    if (ret == -EINVAL) {
        enter_pgmcheck(cpu, PGM_SPECIFICATION);
        return 0;
    }

    return ret;
}

static void kvm_handle_diag_308(S390CPU *cpu, struct kvm_run *run)
{
    uint64_t r1, r3;

    cpu_synchronize_state(CPU(cpu));
    r1 = (run->s390_sieic.ipa & 0x00f0) >> 8;
    r3 = run->s390_sieic.ipa & 0x000f;
    handle_diag_308(&cpu->env, r1, r3);
}

#define DIAG_KVM_CODE_MASK 0x000000000000ffff

static int handle_diag(S390CPU *cpu, struct kvm_run *run, uint32_t ipb)
{
    int r = 0;
    uint16_t func_code;

    /*
     * For any diagnose call we support, bits 48-63 of the resulting
     * address specify the function code; the remainder is ignored.
     */
    func_code = decode_basedisp_rs(&cpu->env, ipb) & DIAG_KVM_CODE_MASK;
    switch (func_code) {
    case DIAG_IPL:
        kvm_handle_diag_308(cpu, run);
        break;
    case DIAG_KVM_HYPERCALL:
        r = handle_hypercall(cpu, run);
        break;
    case DIAG_KVM_BREAKPOINT:
        sleep(10);
        break;
    default:
        DPRINTF("KVM: unknown DIAG: 0x%x\n", func_code);
        r = -1;
        break;
    }

    return r;
}

static int kvm_s390_cpu_start(S390CPU *cpu)
{
    s390_add_running_cpu(cpu);
    qemu_cpu_kick(CPU(cpu));
    DPRINTF("DONE: KVM cpu start: %p\n", &cpu->env);
    return 0;
}

int kvm_s390_cpu_restart(S390CPU *cpu)
{
    kvm_s390_interrupt(cpu, KVM_S390_RESTART, 0);
    s390_add_running_cpu(cpu);
    qemu_cpu_kick(CPU(cpu));
    DPRINTF("DONE: KVM cpu restart: %p\n", &cpu->env);
    return 0;
}

static void sigp_initial_cpu_reset(void *arg)
{
    CPUState *cpu = arg;
    S390CPUClass *scc = S390_CPU_GET_CLASS(cpu);

    cpu_synchronize_state(cpu);
    scc->initial_cpu_reset(cpu);
}

static void sigp_cpu_reset(void *arg)
{
    CPUState *cpu = arg;
    S390CPUClass *scc = S390_CPU_GET_CLASS(cpu);

    cpu_synchronize_state(cpu);
    scc->cpu_reset(cpu);
}

#define SIGP_ORDER_MASK 0x000000ff

static int handle_sigp(S390CPU *cpu, struct kvm_run *run, uint8_t ipa1)
{
    CPUS390XState *env = &cpu->env;
    uint8_t order_code;
    uint16_t cpu_addr;
    S390CPU *target_cpu;
    uint64_t *statusreg = &env->regs[ipa1 >> 4];
    int cc;

    cpu_synchronize_state(CPU(cpu));

    /* get order code */
    order_code = decode_basedisp_rs(env, run->s390_sieic.ipb) & SIGP_ORDER_MASK;

    cpu_addr = env->regs[ipa1 & 0x0f];
    target_cpu = s390_cpu_addr2state(cpu_addr);
    if (target_cpu == NULL) {
        cc = 3;    /* not operational */
        goto out;
    }

    switch (order_code) {
    case SIGP_START:
        cc = kvm_s390_cpu_start(target_cpu);
        break;
    case SIGP_RESTART:
        cc = kvm_s390_cpu_restart(target_cpu);
        break;
    case SIGP_SET_ARCH:
        *statusreg &= 0xffffffff00000000UL;
        *statusreg |= SIGP_STAT_INVALID_PARAMETER;
        cc = 1;   /* status stored */
        break;
    case SIGP_INITIAL_CPU_RESET:
        run_on_cpu(CPU(target_cpu), sigp_initial_cpu_reset, CPU(target_cpu));
        cc = 0;
        break;
    case SIGP_CPU_RESET:
        run_on_cpu(CPU(target_cpu), sigp_cpu_reset, CPU(target_cpu));
        cc = 0;
        break;
    default:
        DPRINTF("KVM: unknown SIGP: 0x%x\n", order_code);
        *statusreg &= 0xffffffff00000000UL;
        *statusreg |= SIGP_STAT_INVALID_ORDER;
        cc = 1;   /* status stored */
        break;
    }

out:
    setcc(cpu, cc);
    return 0;
}

static void handle_instruction(S390CPU *cpu, struct kvm_run *run)
{
    unsigned int ipa0 = (run->s390_sieic.ipa & 0xff00);
    uint8_t ipa1 = run->s390_sieic.ipa & 0x00ff;
    int r = -1;

    DPRINTF("handle_instruction 0x%x 0x%x\n",
            run->s390_sieic.ipa, run->s390_sieic.ipb);
    switch (ipa0) {
    case IPA0_B2:
        r = handle_b2(cpu, run, ipa1);
        break;
    case IPA0_B9:
        r = handle_b9(cpu, run, ipa1);
        break;
    case IPA0_EB:
        r = handle_eb(cpu, run, ipa1);
        break;
    case IPA0_DIAG:
        r = handle_diag(cpu, run, run->s390_sieic.ipb);
        break;
    case IPA0_SIGP:
        r = handle_sigp(cpu, run, ipa1);
        break;
    }

    if (r < 0) {
        enter_pgmcheck(cpu, 0x0001);
    }
}

static bool is_special_wait_psw(CPUState *cs)
{
    /* signal quiesce */
    return cs->kvm_run->psw_addr == 0xfffUL;
}

static int handle_intercept(S390CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    struct kvm_run *run = cs->kvm_run;
    int icpt_code = run->s390_sieic.icptcode;
    int r = 0;

    DPRINTF("intercept: 0x%x (at 0x%lx)\n", icpt_code,
            (long)cs->kvm_run->psw_addr);
    switch (icpt_code) {
        case ICPT_INSTRUCTION:
            handle_instruction(cpu, run);
            break;
        case ICPT_WAITPSW:
            /* disabled wait, since enabled wait is handled in kernel */
            if (s390_del_running_cpu(cpu) == 0) {
                if (is_special_wait_psw(cs)) {
                    qemu_system_shutdown_request();
                } else {
                    QObject *data;

                    data = qobject_from_jsonf("{ 'action': %s }", "pause");
                    monitor_protocol_event(QEVENT_GUEST_PANICKED, data);
                    qobject_decref(data);
                    vm_stop(RUN_STATE_GUEST_PANICKED);
                }
            }
            r = EXCP_HALTED;
            break;
        case ICPT_CPU_STOP:
            if (s390_del_running_cpu(cpu) == 0) {
                qemu_system_shutdown_request();
            }
            r = EXCP_HALTED;
            break;
        case ICPT_SOFT_INTERCEPT:
            fprintf(stderr, "KVM unimplemented icpt SOFT\n");
            exit(1);
            break;
        case ICPT_IO:
            fprintf(stderr, "KVM unimplemented icpt IO\n");
            exit(1);
            break;
        default:
            fprintf(stderr, "Unknown intercept code: %d\n", icpt_code);
            exit(1);
            break;
    }

    return r;
}

static int handle_tsch(S390CPU *cpu)
{
    CPUS390XState *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    struct kvm_run *run = cs->kvm_run;
    int ret;

    cpu_synchronize_state(cs);

    ret = ioinst_handle_tsch(env, env->regs[1], run->s390_tsch.ipb);
    if (ret >= 0) {
        /* Success; set condition code. */
        setcc(cpu, ret);
        ret = 0;
    } else if (ret < -1) {
        /*
         * Failure.
         * If an I/O interrupt had been dequeued, we have to reinject it.
         */
        if (run->s390_tsch.dequeued) {
            uint16_t subchannel_id = run->s390_tsch.subchannel_id;
            uint16_t subchannel_nr = run->s390_tsch.subchannel_nr;
            uint32_t io_int_parm = run->s390_tsch.io_int_parm;
            uint32_t io_int_word = run->s390_tsch.io_int_word;
            uint32_t type = ((subchannel_id & 0xff00) << 24) |
                ((subchannel_id & 0x00060) << 22) | (subchannel_nr << 16);

            kvm_s390_interrupt_internal(cpu, type,
                                        ((uint32_t)subchannel_id << 16)
                                        | subchannel_nr,
                                        ((uint64_t)io_int_parm << 32)
                                        | io_int_word, 1);
        }
        ret = 0;
    }
    return ret;
}

static int kvm_arch_handle_debug_exit(S390CPU *cpu)
{
    return -ENOSYS;
}

int kvm_arch_handle_exit(CPUState *cs, struct kvm_run *run)
{
    S390CPU *cpu = S390_CPU(cs);
    int ret = 0;

    switch (run->exit_reason) {
        case KVM_EXIT_S390_SIEIC:
            ret = handle_intercept(cpu);
            break;
        case KVM_EXIT_S390_RESET:
            qemu_system_reset_request();
            break;
        case KVM_EXIT_S390_TSCH:
            ret = handle_tsch(cpu);
            break;
        case KVM_EXIT_DEBUG:
            ret = kvm_arch_handle_debug_exit(cpu);
            break;
        default:
            fprintf(stderr, "Unknown KVM exit: %d\n", run->exit_reason);
            break;
    }

    if (ret == 0) {
        ret = EXCP_INTERRUPT;
    }
    return ret;
}

bool kvm_arch_stop_on_emulation_error(CPUState *cpu)
{
    return true;
}

int kvm_arch_on_sigbus_vcpu(CPUState *cpu, int code, void *addr)
{
    return 1;
}

int kvm_arch_on_sigbus(int code, void *addr)
{
    return 1;
}

void kvm_s390_io_interrupt(S390CPU *cpu, uint16_t subchannel_id,
                           uint16_t subchannel_nr, uint32_t io_int_parm,
                           uint32_t io_int_word)
{
    uint32_t type;

    if (io_int_word & IO_INT_WORD_AI) {
        type = KVM_S390_INT_IO(1, 0, 0, 0);
    } else {
        type = ((subchannel_id & 0xff00) << 24) |
            ((subchannel_id & 0x00060) << 22) | (subchannel_nr << 16);
    }
    kvm_s390_interrupt_internal(cpu, type,
                                ((uint32_t)subchannel_id << 16) | subchannel_nr,
                                ((uint64_t)io_int_parm << 32) | io_int_word, 1);
}

void kvm_s390_crw_mchk(S390CPU *cpu)
{
    kvm_s390_interrupt_internal(cpu, KVM_S390_MCHK, 1 << 28,
                                0x00400f1d40330000, 1);
}

void kvm_s390_enable_css_support(S390CPU *cpu)
{
    int r;

    /* Activate host kernel channel subsystem support. */
    r = kvm_vcpu_enable_cap(CPU(cpu), KVM_CAP_S390_CSS_SUPPORT, 0);
    assert(r == 0);
}

void kvm_arch_init_irq_routing(KVMState *s)
{
    /*
     * Note that while irqchip capabilities generally imply that cpustates
     * are handled in-kernel, it is not true for s390 (yet); therefore, we
     * have to override the common code kvm_halt_in_kernel_allowed setting.
     */
    if (kvm_check_extension(s, KVM_CAP_IRQ_ROUTING)) {
        kvm_irqfds_allowed = true;
        kvm_gsi_routing_allowed = true;
        kvm_halt_in_kernel_allowed = false;
    }
}

int kvm_s390_assign_subch_ioeventfd(EventNotifier *notifier, uint32_t sch,
                                    int vq, bool assign)
{
    struct kvm_ioeventfd kick = {
        .flags = KVM_IOEVENTFD_FLAG_VIRTIO_CCW_NOTIFY |
        KVM_IOEVENTFD_FLAG_DATAMATCH,
        .fd = event_notifier_get_fd(notifier),
        .datamatch = vq,
        .addr = sch,
        .len = 8,
    };
    if (!kvm_check_extension(kvm_state, KVM_CAP_IOEVENTFD)) {
        return -ENOSYS;
    }
    if (!assign) {
        kick.flags |= KVM_IOEVENTFD_FLAG_DEASSIGN;
    }
    return kvm_vm_ioctl(kvm_state, KVM_IOEVENTFD, &kick);
}
